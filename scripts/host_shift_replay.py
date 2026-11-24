#!/usr/bin/env python3
"""Replay the data/demo playlist through build/qw and write a shift review
(submission/g3/host_shift_review.{json,md}). Stdlib only.

Every record is a real PhysioNet/CinC 2015 train-split ICU record. The
rules file is pending_signoff, so this is an engineering replay, not a
clinical result.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build/qw"
RULES = "rules/alarm_rules.json"
CONFIG = "config/detector.json"
OUT = ROOT / "submission/g3"
LABEL = "Real PhysioNet/CinC 2015 train-split ICU records; ODC-BY 1.0 (DOI 10.13026/c9fg-a467)"


def main():
    if not BIN.exists():
        print("missing build/qw; run `make` first", file=sys.stderr)
        return 2
    playlist = json.loads((ROOT / "data/demo/PLAYLIST.json").read_text())["playlist"]
    manifest = {r["record"]: r for r in json.loads((ROOT / "data/demo/MANIFEST.json").read_text())["records"]}
    rows = []
    for name in playlist:
        m = manifest[name]
        p = subprocess.run([str(BIN), f"data/demo/{m['file']}", m["alarm_type"], RULES, CONFIG],
                           capture_output=True, text=True, cwd=ROOT, timeout=60)
        if p.returncode != 0:
            print(f"qw failed on {name}: {p.stderr.strip()}", file=sys.stderr)
            return 1
        r = json.loads(p.stdout)
        rows.append({
            "record": name,
            "alarm_type": m["alarm_type"],
            "true_alarm": bool(m["true_alarm"]),
            "decision": r["decision"],
            "ecg_sqi": r["ecg_sqi"],
            "pulse_sqi": r["pulse_sqi"],
            "reason": r["reason"],
        })
    n_conf = sum(r["decision"] == "CONFIRM" for r in rows)
    n_sup = len(rows) - n_conf
    agg = {
        "records": len(rows),
        "CONFIRM": n_conf,
        "SUPPRESS_TO_REVIEW": n_sup,
        "queued_for_nurse_review": n_sup,
        "true_alarms_in_review_queue": sum(r["decision"] == "SUPPRESS_TO_REVIEW" and r["true_alarm"] for r in rows),
        "false_alarms_confirmed": sum(r["decision"] == "CONFIRM" and not r["true_alarm"] for r in rows),
    }
    doc = {"label": LABEL, "rules_status": json.loads(p.stdout)["rules_status"],
           "rules_file": RULES, "detector_config": CONFIG, "aggregate": agg, "records": rows}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "host_shift_review.json").write_text(json.dumps(doc, indent=2) + "\n")

    md = [
        "# Host shift replay (data/demo playlist)", "",
        f"**{LABEL}**", "",
        f"- rules_status: `{doc['rules_status']}` (thresholds are engineering defaults, not signed clinical rules)",
        "- Research demo, not a medical device. Six records, an engineering replay only: no accuracy claim.",
        "- Regenerate: `make && python3 scripts/host_shift_replay.py`", "",
        "| Record | Alarm | Ground truth | Decision | ecg_sqi | pulse_sqi |",
        "|---|---|---|---|---|---|",
    ]
    for r in rows:
        md.append(f"| {r['record']} | {r['alarm_type']} | {'true' if r['true_alarm'] else 'false'} | "
                  f"{r['decision']} | {r['ecg_sqi']:.2f} | {r['pulse_sqi']:.2f} |")
    md += ["", "## Aggregate", "",
           f"- CONFIRM: {n_conf} of {len(rows)} (alarm stays on, no delay)",
           f"- SUPPRESS_TO_REVIEW: {n_sup} of {len(rows)} (queued for nurse review, never discarded)",
           f"- True alarms placed in the review queue: {agg['true_alarms_in_review_queue']}",
           f"- False alarms still confirmed (conservative): {agg['false_alarms_confirmed']}",
           "", "## Reasons (verbatim from build/qw)", ""]
    for r in rows:
        md.append(f"- **{r['record']}** ({r['decision']}): {r['reason']}")
    (OUT / "host_shift_review.md").write_text("\n".join(md) + "\n")
    print(f"host_shift_replay: {len(rows)} records, CONFIRM={n_conf}, SUPPRESS_TO_REVIEW={n_sup}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
