"""Fail-closed validation tests for src/rules.c's qw_rules_load(), driven
through the real C reader via build/qw_rules_dump (see
src/qw_rules_dump.c) and through the qw CLI end to end.

Covers review finding #1: qw_rules_load() must reject (nonzero return,
clear stderr message) a rules file missing window_s, min_signal_quality,
or never_suppress, or with window_s <= 0 or min_signal_quality outside
(0, 1]. The CLI must exit nonzero in that case.
"""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from helpers import (  # noqa: E402
    DEFAULT_DETECTOR_CONFIG,
    RULES_DUMP_BIN,
    make_sine_pulse,
    run_qw_raw,
    write_qw,
    write_rules,
)

import subprocess  # noqa: E402

FS = 250
ALARM_OFFSET_S = 300.0


def dump_rules_raw(rules_path: Path):
    assert RULES_DUMP_BIN.exists(), f"{RULES_DUMP_BIN} not found; run `make test` first"
    return subprocess.run([str(RULES_DUMP_BIN), str(rules_path)], capture_output=True, text=True)


@pytest.mark.parametrize("missing_key", ["window_s", "min_signal_quality", "never_suppress"])
def test_missing_suppression_key_fails_closed(tmp_path, missing_key):
    rules_path = tmp_path / "rules.json"
    write_rules(rules_path, omit_suppression_keys=[missing_key])

    proc = dump_rules_raw(rules_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


@pytest.mark.parametrize("bad_window_s", [0, -1, -16])
def test_non_positive_window_s_fails_closed(tmp_path, bad_window_s):
    rules_path = tmp_path / "rules.json"
    write_rules(rules_path, window_s=bad_window_s)

    proc = dump_rules_raw(rules_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


@pytest.mark.parametrize("bad_sqi", [0, -0.1, 1.1, 2.0])
def test_min_signal_quality_out_of_range_fails_closed(tmp_path, bad_sqi):
    rules_path = tmp_path / "rules.json"
    write_rules(rules_path, min_signal_quality=bad_sqi)

    proc = dump_rules_raw(rules_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


def test_min_signal_quality_exactly_one_is_accepted(tmp_path):
    """Upper bound is inclusive: (0, 1] admits exactly 1.0."""
    rules_path = tmp_path / "rules.json"
    write_rules(rules_path, min_signal_quality=1.0)

    proc = dump_rules_raw(rules_path)

    assert proc.returncode == 0


def test_cli_exits_nonzero_on_missing_window_s(tmp_path):
    """End-to-end: the qw CLI itself must exit nonzero and print a
    message to stderr when the rules file fails closed, not just the
    internal test helper."""
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"

    abp = make_sine_pulse(FS, ALARM_OFFSET_S, 70.0)
    ecg = make_sine_pulse(FS, ALARM_OFFSET_S, 70.0, amplitude=0.05)
    write_qw(record_path, FS, {"II": ecg, "ABP": abp})
    write_rules(rules_path, omit_suppression_keys=["window_s"])

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)

    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


def test_cli_exits_nonzero_on_bad_min_signal_quality(tmp_path):
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"

    abp = make_sine_pulse(FS, ALARM_OFFSET_S, 70.0)
    ecg = make_sine_pulse(FS, ALARM_OFFSET_S, 70.0, amplitude=0.05)
    write_qw(record_path, FS, {"II": ecg, "ABP": abp})
    write_rules(rules_path, min_signal_quality=1.5)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)

    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


def test_alarm_missing_every_threshold_field_fails_closed(tmp_path):
    """An alarm definition with none of the recognized threshold fields
    (no_beat_s, hr_below_bpm, hr_above_bpm, duration_s) must fail closed
    rather than silently being treated as unconditionally confirmable."""
    import json

    rules_path = tmp_path / "rules.json"
    rules = {
        "status": "pending_signoff",
        "alarms": {
            "Asystole": {"definition": "No QRS for at least 4 seconds"},  # no threshold field
        },
        "suppression": {
            "window_s": 16,
            "min_signal_quality": 0.9,
            "never_suppress": [],
        },
    }
    with open(rules_path, "w") as f:
        json.dump(rules, f)

    proc = dump_rules_raw(rules_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr
