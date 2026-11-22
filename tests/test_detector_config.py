"""Tests for src/qw_detector_config.c's loader, exercised through the
build/qw_detector_config_dump test helper (see
src/qw_detector_config_dump.c) and end to end through the qw CLI.

Covers review finding #2: detector engineering constants live in
config/detector.json (status "engineering_default", not a clinical
rule), loaded at startup with fail-closed checks matching rules.c's
contract.
"""
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from helpers import (  # noqa: E402
    DEFAULT_DETECTOR_CONFIG,
    DETECTOR_CONFIG_DUMP_BIN,
    make_sine_pulse,
    run_qw_raw,
    write_detector_config,
    write_qw,
    write_rules,
)

import pytest  # noqa: E402

FS = 250
ALARM_OFFSET_S = 300.0


def dump_config_raw(path: Path):
    assert DETECTOR_CONFIG_DUMP_BIN.exists(), f"{DETECTOR_CONFIG_DUMP_BIN} not found; run `make test` first"
    return subprocess.run([str(DETECTOR_CONFIG_DUMP_BIN), str(path)], capture_output=True, text=True)


def test_real_project_detector_config_has_engineering_default_status():
    assert DEFAULT_DETECTOR_CONFIG.exists()
    with open(DEFAULT_DETECTOR_CONFIG) as f:
        cfg = json.load(f)
    assert cfg["status"] == "engineering_default"
    assert cfg["status"] != "pending_signoff"


def test_reads_real_project_detector_config():
    proc = dump_config_raw(DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode == 0, proc.stderr

    out = {}
    for line in proc.stdout.strip().splitlines():
        key, _, value = line.partition("=")
        out[key] = value

    assert out["min_prominence_frac"] == "0.15"
    assert out["activity_weight"] == "0.8"
    assert out["regularity_weight"] == "0.2"
    assert out["min_expected_bpm_floor"] == "30"
    assert out["min_expected_fraction"] == "0.5"
    assert out["max_trusted_interval_cv"] == "0.3"


def test_reads_synthetic_fixture(tmp_path):
    cfg_path = tmp_path / "detector.json"
    write_detector_config(cfg_path, min_prominence_frac=0.2, activity_weight=0.7, regularity_weight=0.3)

    proc = dump_config_raw(cfg_path)
    assert proc.returncode == 0, proc.stderr
    assert "min_prominence_frac=0.2" in proc.stdout
    assert "activity_weight=0.7" in proc.stdout
    assert "regularity_weight=0.3" in proc.stdout


def test_missing_file_fails_loudly(tmp_path):
    missing = tmp_path / "does_not_exist.json"
    proc = dump_config_raw(missing)
    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


@pytest.mark.parametrize(
    "missing_key",
    [
        "pulse_detector.min_prominence_frac",
        "signal_quality.activity_weight",
        "signal_quality.regularity_weight",
        "pulse_presence.min_expected_bpm_floor",
        "pulse_presence.min_expected_fraction",
        "suppression_contradiction.max_trusted_interval_cv",
    ],
)
def test_missing_field_fails_closed(tmp_path, missing_key):
    cfg_path = tmp_path / "detector.json"
    write_detector_config(cfg_path, omit_keys=[missing_key])

    proc = dump_config_raw(cfg_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


@pytest.mark.parametrize("bad_frac", [0, -0.1, 1.0, 1.5])
def test_min_prominence_frac_out_of_range_fails_closed(tmp_path, bad_frac):
    cfg_path = tmp_path / "detector.json"
    write_detector_config(cfg_path, min_prominence_frac=bad_frac)

    proc = dump_config_raw(cfg_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


@pytest.mark.parametrize("bad_weight", [-0.1, 1.1])
def test_signal_quality_weights_out_of_range_fail_closed(tmp_path, bad_weight):
    cfg_path = tmp_path / "detector.json"
    write_detector_config(cfg_path, activity_weight=bad_weight)

    proc = dump_config_raw(cfg_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


@pytest.mark.parametrize("bad_floor", [0, -5])
def test_min_expected_bpm_floor_must_be_positive(tmp_path, bad_floor):
    cfg_path = tmp_path / "detector.json"
    write_detector_config(cfg_path, min_expected_bpm_floor=bad_floor)

    proc = dump_config_raw(cfg_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


@pytest.mark.parametrize("bad_cv", [0, -0.1])
def test_max_trusted_interval_cv_must_be_positive(tmp_path, bad_cv):
    cfg_path = tmp_path / "detector.json"
    write_detector_config(cfg_path, max_trusted_interval_cv=bad_cv)

    proc = dump_config_raw(cfg_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


def test_cli_exits_nonzero_on_bad_detector_config(tmp_path):
    """End-to-end: the qw CLI itself must exit nonzero with a stderr
    message when the detector config fails closed."""
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"
    cfg_path = tmp_path / "detector.json"

    abp = make_sine_pulse(FS, ALARM_OFFSET_S, 70.0)
    ecg = make_sine_pulse(FS, ALARM_OFFSET_S, 70.0, amplitude=0.05)
    write_qw(record_path, FS, {"II": ecg, "ABP": abp})
    write_rules(rules_path)
    write_detector_config(cfg_path, omit_keys=["pulse_detector.min_prominence_frac"])

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, cfg_path)

    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


def test_cli_still_works_with_valid_custom_detector_config(tmp_path):
    """Sanity check that passing an explicit (valid) detector config path
    as the 4th CLI argument works end to end and produces the same
    engineering behaviour as the default."""
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"
    cfg_path = tmp_path / "detector.json"

    abp = 80.0 + 20.0 * (make_sine_pulse(FS, ALARM_OFFSET_S, 70.0) - 0.5)
    ecg = make_sine_pulse(FS, ALARM_OFFSET_S, 70.0, amplitude=0.05)
    write_qw(record_path, FS, {"II": ecg, "ABP": abp})
    write_rules(rules_path, window_s=16, min_signal_quality=0.9)
    write_detector_config(cfg_path)

    from helpers import run_qw

    result = run_qw(record_path, "Bradycardia", rules_path, cfg_path)
    assert result["decision"] == "SUPPRESS_TO_REVIEW"
