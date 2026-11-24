"""P2.3 C11 clinical-core fixes: end-to-end checks on the six real demo
records (data/demo), the CLI's extra JSON keys, and the rules.c
fail-closed window/beats validation.

The demo records are ODC-BY training records; the heldout split is never
touched here (only eval/split.json is read, to prove zero overlap).
"""
import json
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from helpers import (  # noqa: E402
    DEFAULT_DETECTOR_CONFIG,
    ROOT,
    RULES_DUMP_BIN,
    run_qw,
    write_rules,
)

DEMO_DIR = ROOT / "data" / "demo"
REAL_RULES = ROOT / "rules" / "alarm_rules.json"

# (record, alarm, expected decision)
# - a653l (Asystole, true_alarm=1): v0 FN rescued to CONFIRM via trailing gap (3.41s > 3.20s limit)
# - v188s (Ventricular_Tachycardia, true_alarm=1): v0 FN rescued to CONFIRM via ECG cross-check (242 bpm)
# - t116s (Tachycardia, true_alarm=0): Pan-Tompkins ECG detections hit 250ms refractory (~160 bpm > 140),
#   so the conservative ECG cross-check safely refuses suppression (CONFIRM) rather than risking FN
DEMO_EXPECTED = [
    ("a653l", "Asystole", "CONFIRM"),
    ("v188s", "Ventricular_Tachycardia", "CONFIRM"),
    ("a109l", "Asystole", "SUPPRESS_TO_REVIEW"),
    ("t116s", "Tachycardia", "CONFIRM"),
    ("t106s", "Tachycardia", "CONFIRM"),
    ("f450s", "Ventricular_Flutter_Fib", "CONFIRM"),
]


def test_demo_ids_do_not_overlap_heldout():
    split = json.loads((ROOT / "eval" / "split.json").read_text())
    demo_ids = {p.stem for p in DEMO_DIR.glob("*.qw")}
    assert len(demo_ids) == 6
    assert demo_ids.isdisjoint(set(split["heldout"]))


@pytest.mark.parametrize("rec,alarm,expected", DEMO_EXPECTED, ids=[r[0] for r in DEMO_EXPECTED])
def test_demo_record_decision(rec, alarm, expected):
    out = run_qw(DEMO_DIR / f"{rec}.qw", alarm, REAL_RULES, DEFAULT_DETECTOR_CONFIG)
    assert out["decision"] == expected, out["reason"]


def test_cli_json_has_rules_status_and_sqis():
    out = run_qw(DEMO_DIR / "a109l.qw", "Asystole", REAL_RULES, DEFAULT_DETECTOR_CONFIG)
    assert list(out)[:5] == ["record", "alarm", "decision", "reason", "sqi"]
    assert out["rules_status"] == "pending_signoff"
    assert 0.0 <= out["ecg_sqi"] <= 1.0
    assert 0.0 <= out["pulse_sqi"] <= 1.0
    assert out["sqi"] == pytest.approx(max(out["ecg_sqi"], out["pulse_sqi"]), abs=1e-4)


def _dump(path):
    return subprocess.run([str(RULES_DUMP_BIN), str(path)], capture_output=True, text=True)


def test_rules_dump_prints_status():
    proc = _dump(REAL_RULES)
    assert proc.returncode == 0, proc.stderr
    assert "rules.status=pending_signoff" in proc.stdout


def test_rules_status_unknown_when_absent(tmp_path):
    p = tmp_path / "r.json"
    write_rules(p)
    d = json.loads(p.read_text())
    del d["status"]
    p.write_text(json.dumps(d))
    proc = _dump(p)
    assert proc.returncode == 0, proc.stderr
    assert "rules.status=unknown" in proc.stdout


def test_window_shorter_than_rolling_span_fails_closed(tmp_path):
    # Default fixture: Tachycardia needs 16*60/140 = 6.86 s, Bradycardia 6.0 s.
    p = tmp_path / "r.json"
    write_rules(p, window_s=3.0)
    proc = _dump(p)
    assert proc.returncode != 0
    assert "window_s" in proc.stderr


def test_window_boundary_is_longest_span_over_alarms(tmp_path):
    shorten = {"Tachycardia": {"beats": 5}, "Ventricular_Tachycardia": {"beats": 5}}
    p = tmp_path / "r.json"
    # Only Bradycardia (4 intervals at 40 bpm = 6.0 s) and Asystole (4 s) bind.
    write_rules(p, window_s=5.9, alarm_overrides=shorten)
    assert _dump(p).returncode != 0
    write_rules(p, window_s=6.0, alarm_overrides=shorten)
    assert _dump(p).returncode == 0
    # Real rules: Tachycardia 6.857 s binds.
    write_rules(p, window_s=6.8)
    assert _dump(p).returncode != 0
    write_rules(p, window_s=6.9)
    assert _dump(p).returncode == 0


@pytest.mark.parametrize("alarm,fields", [
    ("Bradycardia", {"beats": 1}),
    ("Tachycardia", {"beats": 0}),
    ("Ventricular_Tachycardia", {"beats": 1}),
])
def test_hr_alarm_with_fewer_than_two_beats_fails_closed(tmp_path, alarm, fields):
    p = tmp_path / "r.json"
    write_rules(p, alarm_overrides={alarm: fields})
    proc = _dump(p)
    assert proc.returncode != 0
    assert "beats" in proc.stderr


def test_hr_alarm_missing_beats_fails_closed(tmp_path):
    p = tmp_path / "r.json"
    write_rules(p)
    d = json.loads(p.read_text())
    del d["alarms"]["Bradycardia"]["beats"]
    p.write_text(json.dumps(d))
    proc = _dump(p)
    assert proc.returncode != 0
    assert "beats" in proc.stderr


@pytest.mark.parametrize("alarm,fields", [
    ("Asystole", {"no_beat_s": 0}),
    ("Bradycardia", {"hr_below_bpm": 0}),
    ("Tachycardia", {"hr_above_bpm": -5}),
])
def test_nonpositive_thresholds_fail_closed(tmp_path, alarm, fields):
    p = tmp_path / "r.json"
    write_rules(p, alarm_overrides={alarm: fields})
    assert _dump(p).returncode != 0
