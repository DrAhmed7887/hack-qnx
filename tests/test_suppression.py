"""End-to-end tests of the `qw` CLI's suppression decision against small,
synthetic .qw records built with a clean sine-wave pulse. These check the
two behavioural guarantees from .kiro/specs/quiet-ward/tasks.md:

  * a high-quality pulsatile channel that contradicts a suppressible alarm
    for the whole suppression window must cause SUPPRESS_TO_REVIEW;
  * an alarm type listed in suppression.never_suppress is never suppressed,
    even when the pulse channel would otherwise contradict it.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from helpers import make_sine_pulse, run_qw, write_qw, write_rules  # noqa: E402

FS = 250
ALARM_OFFSET_S = 300.0  # PhysioNet 2015 Challenge convention: alarm always at 300s


def _padded_record(path: Path, fs: int, pulse_bpm: float, duration_s: float = ALARM_OFFSET_S):
    """A record with a clean, regular pulsatile ABP channel at pulse_bpm
    for the full pre-alarm duration, plus a flat ECG channel (so the ECG
    detector doesn't interfere; the pulsatile channel alone should drive
    the decision here)."""
    abp = 80.0 + 20.0 * (make_sine_pulse(fs, duration_s, pulse_bpm) - 0.5)
    ecg = make_sine_pulse(fs, duration_s, pulse_bpm, amplitude=0.05, offset=0.0)
    write_qw(path, fs, {"II": ecg, "ABP": abp})


def test_suppression_fires_when_pulse_contradicts_bradycardia(tmp_path):
    """Bradycardia alarm (hr_below_bpm=40) with a steady 70bpm pulse
    throughout the suppression window should be routed to quiet review."""
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"
    _padded_record(record_path, FS, pulse_bpm=70.0)
    write_rules(rules_path, window_s=16, min_signal_quality=0.9)

    result = run_qw(record_path, "Bradycardia", rules_path)

    assert result["decision"] == "SUPPRESS_TO_REVIEW"
    assert result["sqi"] >= 0.9
    assert "reason" in result and len(result["reason"]) > 0


def test_suppression_fires_when_pulse_contradicts_tachycardia(tmp_path):
    """Tachycardia alarm (hr_above_bpm=140) with a steady 80bpm pulse
    (well under threshold) throughout the window should be suppressed."""
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"
    _padded_record(record_path, FS, pulse_bpm=80.0)
    write_rules(rules_path, window_s=16, min_signal_quality=0.9)

    result = run_qw(record_path, "Tachycardia", rules_path)

    assert result["decision"] == "SUPPRESS_TO_REVIEW"


def test_no_suppression_when_pulse_agrees_with_tachycardia(tmp_path):
    """A steady 160bpm pulse does NOT contradict a >140bpm tachycardia
    alarm, so it must stay CONFIRM."""
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"
    _padded_record(record_path, FS, pulse_bpm=160.0)
    write_rules(rules_path, window_s=16, min_signal_quality=0.9)

    result = run_qw(record_path, "Tachycardia", rules_path)

    assert result["decision"] == "CONFIRM"


def test_never_suppress_type_always_confirmed(tmp_path):
    """Ventricular_Flutter_Fib is in suppression.never_suppress. Even with
    a clean, steady, strongly-contradicting pulse present throughout the
    window, the alarm must be CONFIRMed, never suppressed."""
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"
    # A strong, regular 75bpm pulse -- if anything should look suppressible.
    _padded_record(record_path, FS, pulse_bpm=75.0)
    write_rules(rules_path, window_s=16, min_signal_quality=0.9, never_suppress=["Ventricular_Flutter_Fib"])

    result = run_qw(record_path, "Ventricular_Flutter_Fib", rules_path)

    assert result["decision"] == "CONFIRM"
    assert "never_suppress" in result["reason"]


def test_never_suppress_list_is_read_from_rules_file_not_hardcoded(tmp_path):
    """If a DIFFERENT alarm type is listed in never_suppress in the rules
    file, that type must not be suppressed either -- proving the list is
    read from the file at runtime, not hard-coded to one alarm name."""
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"
    _padded_record(record_path, FS, pulse_bpm=70.0)  # would otherwise contradict Bradycardia
    write_rules(rules_path, window_s=16, min_signal_quality=0.9, never_suppress=["Bradycardia"])

    result = run_qw(record_path, "Bradycardia", rules_path)

    assert result["decision"] == "CONFIRM"
    assert "never_suppress" in result["reason"]


def test_low_signal_quality_blocks_suppression(tmp_path):
    """Even a contradicting pulse must not trigger suppression if the
    rules file demands a higher min_signal_quality than a noisy channel
    can reach."""
    record_path = tmp_path / "rec.qw"
    rules_path = tmp_path / "rules.json"

    import numpy as np

    n = int(FS * ALARM_OFFSET_S)
    rng = np.random.default_rng(0)
    noisy_abp = (rng.standard_normal(n) * 30.0 + 80.0).astype(np.float32)
    ecg = make_sine_pulse(FS, ALARM_OFFSET_S, 70.0, amplitude=0.05)
    write_qw(record_path, FS, {"II": ecg, "ABP": noisy_abp})

    write_rules(rules_path, window_s=16, min_signal_quality=0.999)

    result = run_qw(record_path, "Bradycardia", rules_path)

    assert result["decision"] == "CONFIRM"
    assert "sqi" in result["reason"] or "signal_quality" in result["reason"] or "min_signal_quality" in result["reason"]
