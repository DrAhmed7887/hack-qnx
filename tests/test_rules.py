"""Tests for src/rules.c's JSON reader, exercised through the
build/qw_rules_dump test helper (see src/qw_rules_dump.c) so we are
testing the real C parser, not a Python re-implementation of it.
"""
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from helpers import ROOT, write_rules  # noqa: E402

RULES_DUMP_BIN = ROOT / "build" / "qw_rules_dump"


def dump_rules(rules_path: Path, *never_suppress_queries: str):
    assert RULES_DUMP_BIN.exists(), f"{RULES_DUMP_BIN} not found; run `make test` first"
    proc = subprocess.run(
        [str(RULES_DUMP_BIN), str(rules_path), *never_suppress_queries],
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 0, proc.stderr
    out = {}
    for line in proc.stdout.strip().splitlines():
        key, _, value = line.partition("=")
        out[key] = value
    return out


def test_reads_real_project_rules_file():
    """The actual rules/alarm_rules.json shipped with the project must
    parse cleanly with every field this project depends on present."""
    real_rules = ROOT / "rules" / "alarm_rules.json"
    if not real_rules.exists():
        pytest.skip("rules/alarm_rules.json not present")

    out = dump_rules(real_rules, "Ventricular_Flutter_Fib", "Asystole")

    assert out["n_alarms"] == "5"
    assert out["alarm.Asystole.has_no_beat_s"] == "1"
    assert out["alarm.Asystole.no_beat_s"] == "4"
    assert out["alarm.Bradycardia.has_hr_below_bpm"] == "1"
    assert out["alarm.Bradycardia.hr_below_bpm"] == "40"
    assert out["alarm.Bradycardia.beats"] == "5"
    assert out["alarm.Tachycardia.hr_above_bpm"] == "140"
    assert out["alarm.Tachycardia.beats"] == "17"
    assert out["alarm.Ventricular_Tachycardia.hr_above_bpm"] == "100"
    assert out["alarm.Ventricular_Tachycardia.beats"] == "5"
    assert out["alarm.Ventricular_Flutter_Fib.has_duration_s"] == "1"
    assert out["alarm.Ventricular_Flutter_Fib.duration_s"] == "4"

    assert out["suppression.window_s"] == "16"
    assert out["suppression.min_signal_quality"] == "0.9"
    assert out["suppression.n_never_suppress"] == "1"
    assert out["suppression.never_suppress[0]"] == "Ventricular_Flutter_Fib"

    assert out["never_suppress(Ventricular_Flutter_Fib)"] == "1"
    assert out["never_suppress(Asystole)"] == "0"


def test_reads_synthetic_fixture(tmp_path):
    rules_path = tmp_path / "rules.json"
    write_rules(rules_path, window_s=20, min_signal_quality=0.75, never_suppress=["Asystole", "Tachycardia"])

    out = dump_rules(rules_path, "Asystole", "Bradycardia")

    assert out["n_alarms"] == "5"
    assert out["suppression.window_s"] == "20"
    assert out["suppression.min_signal_quality"] == "0.75"
    assert out["suppression.n_never_suppress"] == "2"
    assert out["never_suppress(Asystole)"] == "1"
    assert out["never_suppress(Bradycardia)"] == "0"


def test_missing_file_fails_loudly(tmp_path):
    missing = tmp_path / "does_not_exist.json"
    proc = subprocess.run([str(RULES_DUMP_BIN), str(missing)], capture_output=True, text=True)
    assert proc.returncode != 0
    assert "load_failed" in proc.stderr
