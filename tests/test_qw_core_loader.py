"""Fail-closed tests for src/qw_core.c's qw_record_load(), driven end to
end through the qw CLI (build/qw).

Covers review finding #3 (header validated against fstat file size
before malloc; fs/n_channels range checks) and finding #6 (NaN/Inf
samples rejected, counted, and reported).
"""
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from helpers import (  # noqa: E402
    DEFAULT_DETECTOR_CONFIG,
    make_sine_pulse,
    run_qw_raw,
    write_qw,
    write_qw_raw,
    write_rules,
)

FS = 250
ALARM_OFFSET_S = 300.0


def _rules(tmp_path):
    rules_path = tmp_path / "rules.json"
    write_rules(rules_path)
    return rules_path


def _basic_channels():
    abp = 80.0 + 20.0 * (make_sine_pulse(FS, ALARM_OFFSET_S, 70.0) - 0.5)
    ecg = make_sine_pulse(FS, ALARM_OFFSET_S, 70.0, amplitude=0.05)
    return {"II": ecg, "ABP": abp}


def test_valid_record_still_loads(tmp_path):
    """Sanity baseline: an untampered record must still load and decide
    successfully after the loader hardening."""
    record_path = tmp_path / "rec.qw"
    write_qw(record_path, FS, _basic_channels())
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode == 0, proc.stderr


def test_bad_magic_rejected(tmp_path):
    record_path = tmp_path / "rec.qw"
    write_qw_raw(record_path, FS, _basic_channels(), magic=b"BAD0")
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


def test_header_n_samples_larger_than_file_rejected(tmp_path):
    """Header claims more samples than the file actually contains: this
    must be caught by the fstat-based size check BEFORE a malloc sized
    from the (wrong) header value, not crash or read out of bounds."""
    record_path = tmp_path / "rec.qw"
    channels = _basic_channels()
    write_qw_raw(record_path, FS, channels, header_n_samples=len(channels["II"]) * 10)
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


def test_header_n_samples_smaller_than_file_rejected(tmp_path):
    """Header claims fewer samples than the file actually contains
    (trailing garbage / wrong header): also a size mismatch, must be
    rejected rather than silently truncating."""
    record_path = tmp_path / "rec.qw"
    channels = _basic_channels()
    write_qw_raw(record_path, FS, channels, header_n_samples=10)
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


def test_header_n_channels_larger_than_actual_rejected(tmp_path):
    """Header claims more channels than name-table/sample data actually
    present; must be rejected by the size check."""
    record_path = tmp_path / "rec.qw"
    channels = _basic_channels()
    write_qw_raw(record_path, FS, channels, header_n_channels=5)
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


@pytest.mark.parametrize("bad_fs", [0, 10001, 2**32 - 1])
def test_fs_out_of_range_rejected(tmp_path, bad_fs):
    record_path = tmp_path / "rec.qw"
    channels = _basic_channels()
    write_qw_raw(record_path, FS, channels, header_fs=bad_fs)
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


@pytest.mark.parametrize("bad_n_channels", [0, 17, 2**32 - 1])
def test_n_channels_out_of_range_rejected(tmp_path, bad_n_channels):
    record_path = tmp_path / "rec.qw"
    channels = _basic_channels()
    write_qw_raw(record_path, FS, channels, header_n_channels=bad_n_channels)
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


def test_file_smaller_than_header_rejected(tmp_path):
    """A file too small to even contain the fixed 16-byte magic+header
    must be rejected before any header field is trusted."""
    record_path = tmp_path / "rec.qw"
    with open(record_path, "wb") as f:
        f.write(b"QW01")  # magic only, no header
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode != 0
    assert proc.stderr.strip() != ""


def test_nan_samples_rejected_and_counted(tmp_path):
    """Review finding #6: NaN samples must be rejected, counted, and
    reported, not silently propagated into the decision path."""
    record_path = tmp_path / "rec.qw"
    channels = _basic_channels()
    ecg = channels["II"].copy()
    ecg[100] = np.nan
    ecg[200] = np.nan
    channels["II"] = ecg
    write_qw(record_path, FS, channels)
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode != 0
    assert "NaN" in proc.stderr or "nan" in proc.stderr.lower()
    assert "2" in proc.stderr  # the count of rejected samples


def test_inf_samples_rejected_and_counted(tmp_path):
    """Review finding #6: +/-Inf samples must also be rejected and
    counted."""
    record_path = tmp_path / "rec.qw"
    channels = _basic_channels()
    abp = channels["ABP"].copy()
    abp[50] = np.inf
    abp[51] = -np.inf
    abp[52] = np.inf
    channels["ABP"] = abp
    write_qw(record_path, FS, channels)
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode != 0
    assert "infinite" in proc.stderr.lower() or "inf" in proc.stderr.lower()
    assert "3" in proc.stderr  # the count of rejected samples


def test_all_finite_samples_still_pass(tmp_path):
    """Negative control: a record with no NaN/Inf samples at all must not
    be rejected by the finite-sample check."""
    record_path = tmp_path / "rec.qw"
    write_qw(record_path, FS, _basic_channels())
    rules_path = _rules(tmp_path)

    proc = run_qw_raw(record_path, "Bradycardia", rules_path, DEFAULT_DETECTOR_CONFIG)
    assert proc.returncode == 0, proc.stderr
