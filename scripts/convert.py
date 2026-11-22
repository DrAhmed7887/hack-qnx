#!/usr/bin/env python3
"""Convert PhysioNet 2015 Challenge WFDB records into the Quiet Ward .qw
binary format, and build data/bin/labels.csv.

Usage:
    .venv/bin/python scripts/convert.py [--src data/raw/training] [--dst data/bin]

Binary format (.qw), all little-endian:
    char[4]   magic = "QW01"
    uint32    fs            sampling frequency in Hz
    uint32    n_channels
    uint32    n_samples      samples per channel
    for each channel: char[16] name (NUL padded / truncated, not necessarily
                                       NUL-terminated if exactly 16 bytes)
    float32[n_channels][n_samples]   samples, channel-major (all samples of
                                       channel 0, then channel 1, ...)

labels.csv columns: record,alarm_type,true_alarm
    alarm_type is one of the 5 Challenge alarm types.
    true_alarm is 0 or 1 (1 = true alarm, 0 = false alarm).

The alarm type and true/false label for each record come from
data/raw/training/ALARMS, which has lines "record,alarm_type,0|1".
As a cross-check we also read the trailing comment lines of each .hea file
(e.g. "#Ventricular_Tachycardia" / "#True alarm" or "#False alarm") and
assert they agree with ALARMS.
"""
import argparse
import csv
import struct
import sys
from pathlib import Path

import numpy as np
import wfdb

MAGIC = b"QW01"
CHANNEL_NAME_LEN = 16

ALARM_PREFIX_TO_TYPE = {
    "a": "Asystole",
    "b": "Bradycardia",
    "t": "Tachycardia",
    "v": "Ventricular_Tachycardia",
    "f": "Ventricular_Flutter_Fib",
}


def parse_alarms_file(alarms_path: Path):
    """Parse data/raw/training/ALARMS -> {record: (alarm_type, true_alarm)}."""
    labels = {}
    with open(alarms_path, "r") as f:
        for line_no, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) != 3:
                raise ValueError(
                    f"{alarms_path}:{line_no}: expected 3 comma-separated fields, got {line!r}"
                )
            record, alarm_type, true_alarm = parts
            true_alarm = int(true_alarm)
            if true_alarm not in (0, 1):
                raise ValueError(
                    f"{alarms_path}:{line_no}: true_alarm must be 0 or 1, got {true_alarm}"
                )
            labels[record] = (alarm_type, true_alarm)
    return labels


def hea_comment_label(comments):
    """Extract (alarm_type, true_alarm) from wfdb record comments, for
    cross-checking against the ALARMS file. Returns None if not present."""
    alarm_type = None
    true_alarm = None
    for c in comments:
        c = c.strip()
        if c in ALARM_PREFIX_TO_TYPE.values():
            alarm_type = c
        elif c == "True alarm":
            true_alarm = 1
        elif c == "False alarm":
            true_alarm = 0
    if alarm_type is None or true_alarm is None:
        return None
    return (alarm_type, true_alarm)


def write_qw(dst_path: Path, fs: int, sig_name, samples: np.ndarray):
    """samples: float32 array shaped (n_channels, n_samples)."""
    n_channels, n_samples = samples.shape
    dst_path.parent.mkdir(parents=True, exist_ok=True)
    with open(dst_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<III", fs, n_channels, n_samples))
        for name in sig_name:
            name_bytes = name.encode("ascii", errors="replace")[:CHANNEL_NAME_LEN]
            name_bytes = name_bytes + b"\x00" * (CHANNEL_NAME_LEN - len(name_bytes))
            f.write(name_bytes)
        f.write(np.ascontiguousarray(samples, dtype="<f4").tobytes())


def convert_record(record_name: str, src_dir: Path, dst_dir: Path):
    rec = wfdb.rdrecord(str(src_dir / record_name))
    fs = int(round(rec.fs))
    sig_name = rec.sig_name
    # p_signal is (n_samples, n_channels) float64 with NaN for missing values.
    samples = np.nan_to_num(rec.p_signal, nan=0.0).astype(np.float32).T
    write_qw(dst_dir / f"{record_name}.qw", fs, sig_name, samples)
    return rec.comments


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", default="data/raw/training", help="WFDB source directory")
    ap.add_argument("--dst", default="data/bin", help="Destination directory for .qw files")
    ap.add_argument(
        "--records",
        default=None,
        help="Optional path to a RECORDS-style file listing record names (default: <src>/RECORDS)",
    )
    args = ap.parse_args()

    src_dir = Path(args.src)
    dst_dir = Path(args.dst)
    dst_dir.mkdir(parents=True, exist_ok=True)

    records_path = Path(args.records) if args.records else src_dir / "RECORDS"
    alarms_path = src_dir / "ALARMS"

    with open(records_path, "r") as f:
        record_names = [line.strip() for line in f if line.strip()]

    alarm_labels = parse_alarms_file(alarms_path)

    missing_in_alarms = [r for r in record_names if r not in alarm_labels]
    if missing_in_alarms:
        raise SystemExit(
            f"{len(missing_in_alarms)} records in RECORDS missing from ALARMS, e.g. {missing_in_alarms[:5]}"
        )

    rows = []
    n_ok = 0
    n_mismatch = 0
    for record_name in record_names:
        alarm_type, true_alarm = alarm_labels[record_name]

        prefix = record_name[0]
        expected_type = ALARM_PREFIX_TO_TYPE.get(prefix)
        if expected_type is not None and expected_type != alarm_type:
            raise SystemExit(
                f"record {record_name}: filename prefix implies {expected_type} "
                f"but ALARMS says {alarm_type}"
            )

        comments = convert_record(record_name, src_dir, dst_dir)
        hea_label = hea_comment_label(comments)
        if hea_label is not None:
            if hea_label != (alarm_type, true_alarm):
                n_mismatch += 1
                print(
                    f"warning: {record_name}: ALARMS says {(alarm_type, true_alarm)} "
                    f"but .hea comments say {hea_label}",
                    file=sys.stderr,
                )
            else:
                n_ok += 1

        rows.append((record_name, alarm_type, true_alarm))

    labels_path = dst_dir / "labels.csv"
    with open(labels_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["record", "alarm_type", "true_alarm"])
        writer.writerows(rows)

    print(f"Converted {len(rows)} records to {dst_dir}")
    print(f"Wrote {labels_path}")
    print(f"Cross-check vs .hea comments: {n_ok} agree, {n_mismatch} mismatched")


if __name__ == "__main__":
    main()
