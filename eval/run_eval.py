#!/usr/bin/env python3
"""Run build/qw over every record in data/bin/, compare its decision to
data/bin/labels.csv, and write eval/results/train_v0.json.

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
import csv
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QW_BIN = ROOT / "build" / "qw"
LABELS_CSV = ROOT / "data" / "bin" / "labels.csv"
RULES_JSON = ROOT / "rules" / "alarm_rules.json"
BIN_DIR = ROOT / "data" / "bin"
RESULTS_DIR = ROOT / "eval" / "results"

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


def run_qw(record: str, alarm_type: str):
    qw_path = BIN_DIR / f"{record}.qw"
    proc = subprocess.run(
        [str(QW_BIN), str(qw_path), alarm_type, str(RULES_JSON)],
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


def main():
    if not QW_BIN.exists():
        print(f"error: {QW_BIN} not found; run `make` first", file=sys.stderr)
        return 1

    labels = load_labels()

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

        result = run_qw(record, alarm_type)
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

    results = {
        "n_records": len(labels),
        "overall": {
            **overall,
            "challenge_score": challenge_score(overall),
        },
        "per_alarm_type": {
            t: {**per_type[t], "challenge_score": challenge_score(per_type[t])}
            for t in per_type
        },
        "true_alarms_suppressed_count": len(true_alarms_suppressed),
        "true_alarms_suppressed": true_alarms_suppressed,
        "baseline_no_suppression": {
            "overall": {
                **baseline_overall,
                "challenge_score": challenge_score(baseline_overall),
            },
            "per_alarm_type": {
                t: {**baseline_per_type[t], "challenge_score": challenge_score(baseline_per_type[t])}
                for t in baseline_per_type
            },
        },
        "details": details,
    }

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    out_path = RESULTS_DIR / "train_v0.json"
    with open(out_path, "w") as f:
        json.dump(results, f, indent=2)

    print(f"Wrote {out_path}")
    print(f"Overall: {overall}  challenge_score={results['overall']['challenge_score']:.4f}")
    print(
        f"Baseline (no suppression): {baseline_overall}  "
        f"challenge_score={results['baseline_no_suppression']['overall']['challenge_score']:.4f}"
    )
    print(f"True alarms suppressed: {len(true_alarms_suppressed)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
