#!/usr/bin/env python3
"""Build or --verify the 6-record ODC-BY demo pack in data/demo/.

Default: copy DEMO_IDS from data/bin/ and write MANIFEST.json + PLAYLIST.json.
--verify: offline check (no data/bin/ needed) of files, sizes, SHA-256 and split.
"""
import argparse
import csv
import hashlib
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN_DIR = ROOT / "data" / "bin"
LABELS_CSV = BIN_DIR / "labels.csv"
DEMO_DIR = ROOT / "data" / "demo"
SPLIT_JSON = ROOT / "eval" / "split.json"

DEMO_IDS = ["a109l", "a653l", "v188s", "t116s", "t106s", "f450s"]
PLAYLIST = ["a109l", "a653l", "t116s", "t106s", "v188s", "f450s"]

DATASET = {
    "name": "PhysioNet/Computing in Cardiology Challenge 2015: Reducing False Arrhythmia Alarms in the ICU",
    "url": "https://physionet.org/content/challenge-2015/1.0.0/",
    "doi": "10.13026/c9fg-a467",
    "license": "ODC-BY 1.0",
    "license_url": "https://opendatacommons.org/licenses/by/1-0/",
    "license_file": "LICENSE-ODC-BY.txt",
    "citation": ("Clifford GD, Silva I, Moody B, Li Q, Kella D, Shahin A, Kooistra T, Perry D, "
                 "Mark RG (2015). The PhysioNet/Computing in Cardiology Challenge 2015: Reducing "
                 "False Arrhythmia Alarms in the ICU. Computing in Cardiology 42:273-276."),
}


def sha256_file(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def load_split():
    return json.loads(SPLIT_JSON.read_text()) if SPLIT_JSON.exists() else None


def check_split(split, ids):
    errs = []
    if split is None:
        return errs
    train, held = set(split["train"]), set(split["heldout"])
    for r in ids:
        if r in held:
            errs.append(f"{r} is in heldout")
        if r not in train:
            errs.append(f"{r} is not in train")
    return errs


def build():
    if not LABELS_CSV.exists():
        print(f"error: {LABELS_CSV} not found; run scripts/fetch_physionet.sh", file=sys.stderr)
        return 1
    errs = check_split(load_split(), DEMO_IDS)
    if errs:
        print("\n".join(f"FAIL: {e}" for e in errs), file=sys.stderr)
        return 1
    if not (DEMO_DIR / "LICENSE-ODC-BY.txt").exists():
        print("error: data/demo/LICENSE-ODC-BY.txt missing", file=sys.stderr)
        return 1
    with open(LABELS_CSV, newline="") as f:
        labels = {r["record"]: r for r in csv.DictReader(f)}

    records = []
    for rid in sorted(DEMO_IDS):
        if rid not in labels:
            print(f"error: {rid} not in labels.csv", file=sys.stderr)
            return 1
        dst = DEMO_DIR / f"{rid}.qw"
        shutil.copyfile(BIN_DIR / f"{rid}.qw", dst)
        records.append({
            "file": f"{rid}.qw",
            "record": rid,
            "alarm_type": labels[rid]["alarm_type"],
            "true_alarm": int(labels[rid]["true_alarm"]),
            "split": "train",
            "bytes": dst.stat().st_size,
            "sha256": sha256_file(dst),
        })
    manifest = {"dataset": DATASET, "records": records}
    (DEMO_DIR / "MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (DEMO_DIR / "PLAYLIST.json").write_text(json.dumps({"playlist": PLAYLIST}, indent=2) + "\n")
    print(f"Wrote demo pack: {len(records)} records in {DEMO_DIR}")
    return 0


def verify():
    errs = []
    for name in ("LICENSE-ODC-BY.txt", "MANIFEST.json", "PLAYLIST.json"):
        if not (DEMO_DIR / name).exists():
            errs.append(f"missing {name}")
    if not errs:
        manifest = json.loads((DEMO_DIR / "MANIFEST.json").read_text())
        playlist = json.loads((DEMO_DIR / "PLAYLIST.json").read_text()).get("playlist")
        recs = {r["record"]: r for r in manifest.get("records", [])}
        if sorted(recs) != sorted(DEMO_IDS):
            errs.append(f"manifest records {sorted(recs)} != expected {sorted(DEMO_IDS)}")
        if playlist != PLAYLIST:
            errs.append(f"playlist {playlist} != expected {PLAYLIST}")
        for rid in DEMO_IDS:
            p = DEMO_DIR / f"{rid}.qw"
            if not p.exists():
                errs.append(f"missing {p.name}")
                continue
            r = recs.get(rid)
            if r is None:
                continue
            if p.stat().st_size != r["bytes"]:
                errs.append(f"{p.name}: size mismatch")
            if sha256_file(p) != r["sha256"]:
                errs.append(f"{p.name}: sha256 mismatch")
        errs += check_split(load_split(), DEMO_IDS)
    for e in errs:
        print(f"FAIL: {e}", file=sys.stderr)
    if errs:
        return 1
    print("demo pack OK")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()
    return verify() if args.verify else build()


if __name__ == "__main__":
    sys.exit(main())
