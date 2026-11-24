#!/usr/bin/env python3
"""Build (or --check) the frozen train/heldout split in eval/split.json.

Stratified by alarm_type|true_alarm, 30% heldout per stratum (floor(0.30*n+0.5)),
seeded and fully deterministic. Pinned records are train by construction.
"""
import argparse
import csv
import hashlib
import json
import math
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LABELS_CSV = ROOT / "data" / "bin" / "labels.csv"
SPLIT_JSON = ROOT / "eval" / "split.json"

SEED = 20261124
PINNED_TRAIN_IDS = ["a109l", "a161l", "a639l", "a653l", "a670s", "a754s", "a776s",
                    "f450s", "t106s", "t116s", "v133l", "v188s", "v206s", "v328s"]
DEMO_IDS = ["a109l", "a653l", "v188s", "t116s", "t106s", "f450s"]


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_labels():
    with open(LABELS_CSV, newline="") as f:
        return [(r["record"], r["alarm_type"], int(r["true_alarm"])) for r in csv.DictReader(f)]


def build():
    labels = load_labels()
    ids = [r[0] for r in labels]
    assert len(set(ids)) == len(ids), "duplicate record IDs in labels.csv"
    missing = set(PINNED_TRAIN_IDS) - set(ids)
    assert not missing, f"pinned IDs not in labels.csv: {sorted(missing)}"

    strata = {}
    for rec, atype, true_alarm in labels:
        strata.setdefault(f"{atype}|{true_alarm}", []).append(rec)

    pinned = set(PINNED_TRAIN_IDS)
    rng = random.Random(SEED)
    train, heldout, strata_info = [], [], {}
    for key in sorted(strata):
        recs = sorted(strata[key])
        n = len(recs)
        heldout_n = math.floor(0.30 * n + 0.5)
        pinned_in = [r for r in recs if r in pinned]
        unpinned = [r for r in recs if r not in pinned]
        assert heldout_n <= len(unpinned), f"stratum {key}: not enough unpinned records"
        held = rng.sample(unpinned, heldout_n)
        held_set = set(held)
        heldout += held
        train += [r for r in unpinned if r not in held_set] + pinned_in
        strata_info[key] = {"n": n, "heldout_n": heldout_n, "pinned_n": len(pinned_in)}

    train, heldout = sorted(train), sorted(heldout)
    assert set(DEMO_IDS).isdisjoint(heldout)
    assert pinned.issubset(train)
    assert not set(train) & set(heldout) and len(train) + len(heldout) == len(labels)

    split = {
        "seed": SEED,
        "rounding": "floor(0.30*n+0.5)",
        "labels_csv_sha256": sha256_file(LABELS_CSV),
        "core_tag": "v0-frozen",
        "pinned_train": {
            "records": sorted(PINNED_TRAIN_IDS),
            "count": len(PINNED_TRAIN_IDS),
            "demo_ids": DEMO_IDS,
            "demo_count": len(DEMO_IDS),
            "reason": "human-audited during forensic analysis or committed in data/demo/; train by construction",
        },
        "strata": strata_info,
        "train": train,
        "heldout": heldout,
        "counts": {"train": len(train), "heldout": len(heldout), "total": len(labels)},
    }
    return split, json.dumps(split, indent=2) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="verify eval/split.json instead of writing")
    args = ap.parse_args()

    split, text = build()
    if not args.check:
        SPLIT_JSON.parent.mkdir(parents=True, exist_ok=True)
        SPLIT_JSON.write_text(text)
        print(f"Wrote {SPLIT_JSON}: train={split['counts']['train']} heldout={split['counts']['heldout']}")
        return 0

    errors = []
    if not SPLIT_JSON.exists():
        errors.append(f"{SPLIT_JSON} missing")
    else:
        on_disk = SPLIT_JSON.read_text()
        if on_disk != text:
            errors.append("eval/split.json differs from recomputed split")
        try:
            d = json.loads(on_disk)
            if d.get("labels_csv_sha256") != sha256_file(LABELS_CSV):
                errors.append("labels_csv_sha256 does not match data/bin/labels.csv")
            if set(DEMO_IDS) & set(d.get("heldout", [])):
                errors.append("DEMO_IDS present in heldout")
        except json.JSONDecodeError as e:
            errors.append(f"split.json invalid JSON: {e}")
    for e in errors:
        print(f"FAIL: {e}", file=sys.stderr)
    if errors:
        return 1
    print("split OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
