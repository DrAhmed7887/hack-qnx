#!/usr/bin/env python3
"""Run build/qw over records in data/bin/, compare its decision to
data/bin/labels.csv, and write eval/results/{split}_{version}.json.

--split full|train|heldout selects records (train/heldout use the frozen
eval/split.json). Heldout runs are capped at 3 (eval/heldout_runs.log) and
their output omits per-record details and suppressed record IDs.

Definitions (per .kiro/specs/quiet-ward/tasks.md):
    TRUE alarm confirmed  -> TP
    FALSE alarm suppressed -> TN
    FALSE alarm confirmed -> FP
    TRUE alarm suppressed  -> FN

Challenge score = (TP + TN) / (TP + TN + FP + 5*FN)   (PhysioNet 2015 Table 1)

Also writes a "baseline_no_suppression" row: what the metrics would be if
every alarm were always confirmed (no suppression logic at all), for
comparison against the actual qw decisions.
"""
import argparse
import csv
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QW_BIN = ROOT / "build" / "qw"
LABELS_CSV = ROOT / "data" / "bin" / "labels.csv"
RULES_JSON = ROOT / "rules" / "alarm_rules.json"
BIN_DIR = ROOT / "data" / "bin"
RESULTS_DIR = ROOT / "eval" / "results"
SPLIT_JSON = ROOT / "eval" / "split.json"
HELDOUT_LOG = ROOT / "eval" / "heldout_runs.log"
CONFIG_JSON = ROOT / "config" / "detector.json"
HELDOUT_MAX_RUNS = 3

ALL_ALARM_TYPES = [
    "Asystole",
    "Bradycardia",
    "Tachycardia",
    "Ventricular_Tachycardia",
    "Ventricular_Flutter_Fib",
]


def load_labels():
    rows = []
    with open(LABELS_CSV, "r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(
                {
                    "record": row["record"],
                    "alarm_type": row["alarm_type"],
                    "true_alarm": int(row["true_alarm"]),
                }
            )
    return rows


def run_qw(record: str, alarm_type: str, bin_path=None, rules=None, config=None):
    qw_path = BIN_DIR / f"{record}.qw"
    proc = subprocess.run(
        [str(bin_path or QW_BIN), str(qw_path), alarm_type,
         str(rules or RULES_JSON), str(config or CONFIG_JSON)],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"qw failed for record={record} alarm={alarm_type}: "
            f"rc={proc.returncode} stderr={proc.stderr.strip()}"
        )
    return json.loads(proc.stdout.strip())


def classify(decision: str, true_alarm: int):
    """Return one of TP, TN, FP, FN given the qw decision and ground truth."""
    confirmed = decision == "CONFIRM"
    if true_alarm == 1:
        return "TP" if confirmed else "FN"
    else:
        return "TN" if not confirmed else "FP"


def empty_counts():
    return {"TP": 0, "TN": 0, "FP": 0, "FN": 0}


def challenge_score(counts):
    tp, tn, fp, fn = counts["TP"], counts["TN"], counts["FP"], counts["FN"]
    denom = tp + tn + fp + 5 * fn
    if denom == 0:
        return None
    return (tp + tn) / denom


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def heldout_runs_used():
    if not HELDOUT_LOG.exists():
        return 0
    return sum(1 for line in HELDOUT_LOG.read_text().splitlines() if line.strip())


def git_short_head():
    try:
        return subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--split", choices=["full", "train", "heldout"], default="full")
    ap.add_argument("--bin", default=str(QW_BIN))
    ap.add_argument("--rules", default=str(RULES_JSON))
    ap.add_argument("--config", default=str(ROOT / "config" / "detector.json"))
    ap.add_argument("--version", default="custom")
    ap.add_argument("--out", default=None, help="override output path")
    ap.add_argument("--allow-overwrite-v0", action="store_true")
    return ap.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    bin_path = Path(args.bin)
    rules_path = Path(args.rules)
    config_path = Path(args.config)

    if not bin_path.exists():
        print(f"error: {bin_path} not found; run `make` first", file=sys.stderr)
        return 1

    out_path = Path(args.out) if args.out else RESULTS_DIR / f"{args.split}_{args.version}.json"
    if out_path.resolve() == (RESULTS_DIR / "train_v0.json").resolve() and not args.allow_overwrite_v0:
        print("error: refusing to overwrite eval/results/train_v0.json "
              "(pass --allow-overwrite-v0)", file=sys.stderr)
        return 1

    labels = load_labels()
    split_sha = None
    if args.split in ("train", "heldout"):
        if not SPLIT_JSON.exists():
            print(f"error: {SPLIT_JSON} missing; run scripts/make_split.py", file=sys.stderr)
            return 1
        split = json.loads(SPLIT_JSON.read_text())
        if split["labels_csv_sha256"] != sha256_file(LABELS_CSV):
            print("error: labels.csv SHA-256 does not match eval/split.json (fail closed)",
                  file=sys.stderr)
            return 1
        split_sha = sha256_file(SPLIT_JSON)
        keep = set(split[args.split])
        labels = [r for r in labels if r["record"] in keep]
        if len(labels) != len(keep):
            print("error: split records missing from labels.csv", file=sys.stderr)
            return 1

    if args.split == "heldout" and heldout_runs_used() >= HELDOUT_MAX_RUNS:
        print(f"error: heldout run budget exhausted ({HELDOUT_MAX_RUNS} runs in "
              f"{HELDOUT_LOG.name}); refusing", file=sys.stderr)
        return 1

    try:
        rules_doc = json.loads(rules_path.read_text())
    except (OSError, json.JSONDecodeError):
        rules_doc = {}
    rules_status = rules_doc.get("status")

    overall = empty_counts()
    baseline_overall = empty_counts()
    per_type = {t: empty_counts() for t in ALL_ALARM_TYPES}
    baseline_per_type = {t: empty_counts() for t in ALL_ALARM_TYPES}

    true_alarms_suppressed = []  # list of records where a TRUE alarm got suppressed (FN)
    details = []

    for row in labels:
        record = row["record"]
        alarm_type = row["alarm_type"]
        true_alarm = row["true_alarm"]

        result = run_qw(record, alarm_type, bin_path, rules_path, config_path)
        if result.get("rules_status"):
            rules_status = result["rules_status"]
        decision = result["decision"]

        outcome = classify(decision, true_alarm)
        baseline_outcome = classify("CONFIRM", true_alarm)  # no suppression ever

        overall[outcome] += 1
        baseline_overall[baseline_outcome] += 1

        if alarm_type not in per_type:
            # Defensive: alarm types not in the fixed list (shouldn't happen
            # with this dataset, but don't silently drop data).
            per_type[alarm_type] = empty_counts()
            baseline_per_type[alarm_type] = empty_counts()
        per_type[alarm_type][outcome] += 1
        baseline_per_type[alarm_type][baseline_outcome] += 1

        if outcome == "FN":
            true_alarms_suppressed.append(
                {"record": record, "alarm_type": alarm_type, "reason": result["reason"]}
            )

        details.append(
            {
                "record": record,
                "alarm_type": alarm_type,
                "true_alarm": true_alarm,
                "decision": decision,
                "outcome": outcome,
                "sqi": result["sqi"],
                "reason": result["reason"],
            }
        )

    is_heldout = args.split == "heldout"

    def with_pct(d):
        d["score_pct"] = None if d["challenge_score"] is None else round(100 * d["challenge_score"], 2)
        return d

    results = {
        "split": args.split,
        "split_sha256": split_sha,
        "version": args.version,
        "rules_status": rules_status,
        "n_records": len(labels),
        "overall": with_pct({**overall, "challenge_score": challenge_score(overall)}),
        "per_alarm_type": {
            t: with_pct({**per_type[t], "challenge_score": challenge_score(per_type[t])})
            for t in per_type
        },
        "true_alarms_suppressed_count": len(true_alarms_suppressed),
        "baseline_no_suppression": {
            "overall": with_pct({**baseline_overall, "challenge_score": challenge_score(baseline_overall)}),
            "per_alarm_type": {
                t: with_pct({**baseline_per_type[t], "challenge_score": challenge_score(baseline_per_type[t])})
                for t in baseline_per_type
            },
        },
    }
    if not is_heldout:
        results["true_alarms_suppressed"] = true_alarms_suppressed
        results["details"] = details

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(results, f, indent=2)

    if is_heldout:
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        with open(HELDOUT_LOG, "a") as f:
            f.write("\t".join([ts, git_short_head(), args.version, str(rules_status),
                               str(results["overall"]["score_pct"])]) + "\n")

    print(f"Wrote {out_path}")
    cs = results["overall"]["challenge_score"]
    bs = results["baseline_no_suppression"]["overall"]["challenge_score"]
    print(f"Overall: {overall}  challenge_score={cs:.4f}" if cs is not None else f"Overall: {overall}")
    print(f"Baseline (no suppression): {baseline_overall}  challenge_score={bs:.4f}" if bs is not None
          else f"Baseline (no suppression): {baseline_overall}")
    print(f"True alarms suppressed: {len(true_alarms_suppressed)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
