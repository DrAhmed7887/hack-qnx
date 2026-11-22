"""Round-trip test for scripts/convert.py: write a tiny synthetic WFDB
record with wfdb.wrsamp, run the real conversion routine against it, then
read the .qw file back with raw struct parsing and check the samples and
header match what was written.
"""
import csv
import struct
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import convert  # noqa: E402

wfdb = pytest.importorskip("wfdb")


def read_qw_raw(path: Path):
    with open(path, "rb") as f:
        magic = f.read(4)
        assert magic == b"QW01"
        fs, n_channels, n_samples = struct.unpack("<III", f.read(12))
        names = []
        for _ in range(n_channels):
            raw = f.read(16)
            names.append(raw.rstrip(b"\x00").decode("ascii"))
        data = np.frombuffer(f.read(), dtype="<f4")
        assert data.size == n_channels * n_samples
        samples = data.reshape(n_channels, n_samples)
    return fs, names, samples


def test_convert_round_trip(tmp_path):
    fs = 250
    duration_s = 2
    n = fs * duration_s
    t = np.arange(n) / fs

    ecg = 0.5 * np.sin(2 * np.pi * 1.2 * t)
    pleth = 0.3 + 0.2 * np.sin(2 * np.pi * 1.0 * t)
    sig = np.stack([ecg, pleth], axis=1)

    src_dir = tmp_path / "raw"
    src_dir.mkdir()
    record_name = "x001s"

    wfdb.wrsamp(
        record_name,
        fs=fs,
        units=["mV", "NU"],
        sig_name=["II", "PLETH"],
        p_signal=sig,
        fmt=["16", "16"],
        write_dir=str(src_dir),
        comments=["Asystole", "True alarm"],
    )

    (src_dir / "RECORDS").write_text(record_name + "\n")
    (src_dir / "ALARMS").write_text(f"{record_name},Asystole,1\n")

    dst_dir = tmp_path / "bin"
    comments = convert.convert_record(record_name, src_dir, dst_dir)
    assert convert.hea_comment_label(comments) == ("Asystole", 1)

    qw_path = dst_dir / f"{record_name}.qw"
    assert qw_path.exists()

    out_fs, out_names, out_samples = read_qw_raw(qw_path)
    assert out_fs == fs
    assert out_names == ["II", "PLETH"]
    assert out_samples.shape == (2, n)

    # WFDB quantizes to the storage format's resolution, so compare with a
    # loose tolerance rather than bit-for-bit equality.
    np.testing.assert_allclose(out_samples[0], ecg, atol=1e-2)
    np.testing.assert_allclose(out_samples[1], pleth, atol=1e-2)


def test_convert_main_writes_labels_csv(tmp_path):
    fs = 250
    n = fs * 2
    t = np.arange(n) / fs
    sig = np.stack([0.5 * np.sin(2 * np.pi * t), 0.3 + 0.1 * t * 0], axis=1)

    src_dir = tmp_path / "raw"
    src_dir.mkdir()

    records = [("b001s", "Bradycardia", 1), ("t002l", "Tachycardia", 0)]
    lines = []
    for name, alarm_type, true_alarm in records:
        wfdb.wrsamp(
            name,
            fs=fs,
            units=["mV", "NU"],
            sig_name=["II", "PLETH"],
            p_signal=sig,
            fmt=["16", "16"],
            write_dir=str(src_dir),
            comments=[alarm_type, "True alarm" if true_alarm else "False alarm"],
        )
        lines.append(f"{name},{alarm_type},{true_alarm}")

    (src_dir / "RECORDS").write_text("\n".join(n for n, _, _ in records) + "\n")
    (src_dir / "ALARMS").write_text("\n".join(lines) + "\n")

    dst_dir = tmp_path / "bin"

    import subprocess

    proc = subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "convert.py"), "--src", str(src_dir), "--dst", str(dst_dir)],
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 0, proc.stderr

    labels_path = dst_dir / "labels.csv"
    assert labels_path.exists()
    with open(labels_path) as f:
        rows = list(csv.DictReader(f))
    assert len(rows) == 2
    assert {r["record"] for r in rows} == {"b001s", "t002l"}
    for r in rows:
        if r["record"] == "b001s":
            assert r["alarm_type"] == "Bradycardia"
            assert r["true_alarm"] == "1"
        else:
            assert r["alarm_type"] == "Tachycardia"
            assert r["true_alarm"] == "0"
