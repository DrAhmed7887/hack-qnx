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


def write_rules(path: Path, window_s=16, min_signal_quality=0.9, never_suppress=None):
    if never_suppress is None:
        never_suppress = ["Ventricular_Flutter_Fib"]
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
        "suppression": {
            "status": "pending_signoff",
            "rule": "Suppress only when a high-quality pulsatile channel contradicts the alarm.",
            "window_s": window_s,
            "min_signal_quality": min_signal_quality,
            "never_suppress": never_suppress,
        },
        "score": "Challenge score = (TP+TN)/(TP+TN+FP+5*FN)",
    }
    with open(path, "w") as f:
        json.dump(rules, f, indent=2)


def run_qw(record_path: Path, alarm_type: str, rules_path: Path):
    assert QW_BIN.exists(), f"{QW_BIN} not found; run `make` first"
    proc = subprocess.run(
        [str(QW_BIN), str(record_path), alarm_type, str(rules_path)],
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 0, f"qw failed: {proc.stderr}"
    return json.loads(proc.stdout.strip())
