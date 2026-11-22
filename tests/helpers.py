"""Shared helpers for the Quiet Ward pytest suite.

Tests build small synthetic .qw files and rules.json fixtures on the fly
rather than depending on the full PhysioNet training set, so they run fast
and don't require data/raw/ or data/bin/ to be present.
"""
import json
import struct
import subprocess
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
QW_BIN = ROOT / "build" / "qw"
RULES_DUMP_BIN = ROOT / "build" / "qw_rules_dump"
DETECTOR_CONFIG_DUMP_BIN = ROOT / "build" / "qw_detector_config_dump"
DEFAULT_DETECTOR_CONFIG = ROOT / "config" / "detector.json"
MAGIC = b"QW01"
CHANNEL_NAME_LEN = 16


def write_qw(path: Path, fs: int, channels: dict):
    """channels: {name: 1d float array}. All arrays must be the same length."""
    names = list(channels.keys())
    n_channels = len(names)
    n_samples = len(channels[names[0]])
    for name in names:
        assert len(channels[name]) == n_samples, "all channels must have equal length"

    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<III", fs, n_channels, n_samples))
        for name in names:
            name_bytes = name.encode("ascii")[:CHANNEL_NAME_LEN]
            name_bytes = name_bytes + b"\x00" * (CHANNEL_NAME_LEN - len(name_bytes))
            f.write(name_bytes)
        for name in names:
            arr = np.asarray(channels[name], dtype="<f4")
            f.write(arr.tobytes())


def write_qw_raw(path: Path, fs: int, channels: dict, header_fs=None, header_n_channels=None,
                  header_n_samples=None, magic=None):
    """Like write_qw, but lets the caller override the header fields
    independently of the actual channel data written, to build malformed
    .qw files (bad magic, header/size mismatch, out-of-range fs or
    n_channels) for fail-closed loader tests. Any header_* left as None
    uses the value implied by fs/channels, matching write_qw."""
    names = list(channels.keys())
    n_channels = len(names)
    n_samples = len(channels[names[0]]) if names else 0
    for name in names:
        assert len(channels[name]) == n_samples, "all channels must have equal length"

    out_magic = magic if magic is not None else MAGIC
    out_fs = header_fs if header_fs is not None else fs
    out_n_channels = header_n_channels if header_n_channels is not None else n_channels
    out_n_samples = header_n_samples if header_n_samples is not None else n_samples

    with open(path, "wb") as f:
        f.write(out_magic)
        f.write(struct.pack("<III", out_fs, out_n_channels, out_n_samples))
        for name in names:
            name_bytes = name.encode("ascii")[:CHANNEL_NAME_LEN]
            name_bytes = name_bytes + b"\x00" * (CHANNEL_NAME_LEN - len(name_bytes))
            f.write(name_bytes)
        for name in names:
            arr = np.asarray(channels[name], dtype="<f4")
            f.write(arr.tobytes())


def make_sine_pulse(fs: int, duration_s: float, bpm: float, amplitude: float = 1.0, offset: float = 0.0):
    """A clean, regular pulsatile-looking waveform: rectified sine at the
    given heart rate, like an idealized ABP/PPG trace."""
    n = int(duration_s * fs)
    t = np.arange(n) / fs
    freq_hz = bpm / 60.0
    wave = offset + amplitude * (0.5 + 0.5 * np.sin(2 * np.pi * freq_hz * t - np.pi / 2))
    return wave.astype(np.float32)


def make_ecg_like(fs: int, duration_s: float, bpm: float, amplitude: float = 1.0):
    """A crude but detectable QRS-like periodic spike train on top of a
    small baseline, sufficient to exercise the beat detector without
    needing a real ECG morphology model."""
    n = int(duration_s * fs)
    sig = np.zeros(n, dtype=np.float64)
    period = fs * 60.0 / bpm
    i = 0.0
    while i < n:
        idx = int(i)
        # narrow spike +/- a few samples wide, like a QRS complex
        for d in range(-2, 3):
            j = idx + d
            if 0 <= j < n:
                sig[j] += amplitude * (1.0 - abs(d) / 3.0)
        i += period
    return sig.astype(np.float32)


def write_rules(path: Path, window_s=16, min_signal_quality=0.9, never_suppress=None, omit_suppression_keys=None):
    """omit_suppression_keys is an optional iterable of keys among
    {"window_s", "min_signal_quality", "never_suppress"} to leave out of
    the suppression object entirely, for testing fail-closed behaviour."""
    if never_suppress is None:
        never_suppress = ["Ventricular_Flutter_Fib"]
    omit_suppression_keys = set(omit_suppression_keys or [])

    suppression = {
        "status": "pending_signoff",
        "rule": "Suppress only when a high-quality pulsatile channel contradicts the alarm.",
    }
    if "window_s" not in omit_suppression_keys:
        suppression["window_s"] = window_s
    if "min_signal_quality" not in omit_suppression_keys:
        suppression["min_signal_quality"] = min_signal_quality
    if "never_suppress" not in omit_suppression_keys:
        suppression["never_suppress"] = never_suppress

    rules = {
        "status": "pending_signoff",
        "signed_by": None,
        "signed_on": None,
        "note": "test fixture",
        "source": "test",
        "alarms": {
            "Asystole": {"definition": "No QRS for at least 4 seconds", "no_beat_s": 4},
            "Bradycardia": {"definition": "HR < 40bpm for 5 beats", "hr_below_bpm": 40, "beats": 5},
            "Tachycardia": {"definition": "HR > 140bpm for 17 beats", "hr_above_bpm": 140, "beats": 17},
            "Ventricular_Tachycardia": {
                "definition": "5+ ventricular beats with HR > 100bpm",
                "beats": 5,
                "hr_above_bpm": 100,
            },
            "Ventricular_Flutter_Fib": {
                "definition": "Fibrillatory/flutter waveform for at least 4 seconds",
                "duration_s": 4,
            },
        },
        "suppression": suppression,
        "score": "Challenge score = (TP+TN)/(TP+TN+FP+5*FN)",
    }
    with open(path, "w") as f:
        json.dump(rules, f, indent=2)


def write_detector_config(
    path: Path,
    min_prominence_frac=0.15,
    activity_weight=0.8,
    regularity_weight=0.2,
    min_expected_bpm_floor=30.0,
    min_expected_fraction=0.5,
    max_trusted_interval_cv=0.3,
    omit_keys=None,
):
    """Write a config/detector.json-shaped fixture. omit_keys is an
    optional iterable of dotted keys (e.g. "pulse_detector.min_prominence_frac")
    to leave out entirely, for testing fail-closed behaviour on missing
    fields."""
    omit_keys = set(omit_keys or [])

    def section(name, fields):
        out = {}
        for key, value in fields.items():
            dotted = f"{name}.{key}"
            if dotted in omit_keys:
                continue
            out[key] = value
        return out

    cfg = {
        "status": "engineering_default",
        "note": "test fixture",
    }
    pulse_detector = section("pulse_detector", {"min_prominence_frac": min_prominence_frac})
    if pulse_detector:
        cfg["pulse_detector"] = pulse_detector

    signal_quality = section(
        "signal_quality",
        {"activity_weight": activity_weight, "regularity_weight": regularity_weight},
    )
    if signal_quality:
        cfg["signal_quality"] = signal_quality

    pulse_presence = section(
        "pulse_presence",
        {"min_expected_bpm_floor": min_expected_bpm_floor, "min_expected_fraction": min_expected_fraction},
    )
    if pulse_presence:
        cfg["pulse_presence"] = pulse_presence

    suppression_contradiction = section(
        "suppression_contradiction", {"max_trusted_interval_cv": max_trusted_interval_cv}
    )
    if suppression_contradiction:
        cfg["suppression_contradiction"] = suppression_contradiction

    with open(path, "w") as f:
        json.dump(cfg, f, indent=2)


def run_qw(record_path: Path, alarm_type: str, rules_path: Path, detector_config_path: Path = None):
    assert QW_BIN.exists(), f"{QW_BIN} not found; run `make` first"
    args = [str(QW_BIN), str(record_path), alarm_type, str(rules_path)]
    if detector_config_path is not None:
        args.append(str(detector_config_path))
    proc = subprocess.run(
        args,
        capture_output=True,
        text=True,
        cwd=str(ROOT),
    )
    assert proc.returncode == 0, f"qw failed: {proc.stderr}"
    return json.loads(proc.stdout.strip())


def run_qw_raw(record_path: Path, alarm_type: str, rules_path: Path, detector_config_path: Path = None):
    """Like run_qw but returns the raw CompletedProcess without asserting
    success, for tests that expect qw to fail closed."""
    args = [str(QW_BIN), str(record_path), alarm_type, str(rules_path)]
    if detector_config_path is not None:
        args.append(str(detector_config_path))
    return subprocess.run(
        args,
        capture_output=True,
        text=True,
        cwd=str(ROOT),
    )
