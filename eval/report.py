#!/usr/bin/env python3
"""Compare a v0 and a fixed eval result; print per-alarm table, Wilson CIs, Kill Rule.

Usage: eval/report.py --v0 eval/results/train_v0.json --fixed eval/results/train_v1.json
Kill Rule: PASS iff fixed overall score > baseline overall score AND
FN_fixed <= FN_v0 on every alarm row. Baseline is read from fixed["baseline_no_suppression"].
"""
import argparse
import json
import math
import sys

Z = 1.96


def wilson(k, n, z=Z):
    if n == 0:
        return (None, None)
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (max(0.0, c - h), min(1.0, c + h))


def pct(x):
    return "  n/a" if x is None else f"{100 * x:5.1f}"


def ci_str(k, n):
    lo, hi = wilson(k, n)
    return "n/a" if lo is None else f"{100 * lo:.1f}-{100 * hi:.1f}"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--v0", required=True)
    ap.add_argument("--fixed", required=True)
    ap.add_argument("--assert-pass", action="store_true", help="exit nonzero if Kill Rule fails")
    args = ap.parse_args(argv)

    v0 = json.load(open(args.v0))
    fixed = json.load(open(args.fixed))
    base = fixed["baseline_no_suppression"]

    hdr = f"{'Alarm':<26}{'TP':>5}{'TN':>5}{'FP':>5}{'FN_v0':>7}{'FN_fixed':>9}" \
          f"{'Score_base%':>12}{'Score_v0%':>10}{'Score_fixed%':>13}  flags"
    print(hdr)
    print("-" * len(hdr))

    rows = [(t, fixed["per_alarm_type"][t], v0["per_alarm_type"].get(t), base["per_alarm_type"].get(t))
            for t in fixed["per_alarm_type"]]
    rows.append(("OVERALL", fixed["overall"], v0["overall"], base["overall"]))

    fn_ok = True
    for name, f, v, b in rows:
        flags = []
        if (f["TP"] + f["FN"]) < 10 or (f["TN"] + f["FP"]) < 10:
            flags.append("low-n")
        fn_v0 = v["FN"] if v else None
        if name != "OVERALL" and fn_v0 is not None and f["FN"] > fn_v0:
            fn_ok = False
            flags.append("FN-regress")
        sc = lambda d: pct(d["challenge_score"] if d else None)
        print(f"{name:<26}{f['TP']:>5}{f['TN']:>5}{f['FP']:>5}"
              f"{'-' if fn_v0 is None else fn_v0:>7}{f['FN']:>9}"
              f"{sc(b):>12}{sc(v):>10}{sc(f):>13}  {','.join(flags)}")

    of = fixed["overall"]
    n = of["TP"] + of["TN"] + of["FP"] + of["FN"]
    print()
    print(f"Overall accuracy (TP+TN)/N, Wilson 95% CI: {pct((of['TP'] + of['TN']) / n)}% "
          f"[{ci_str(of['TP'] + of['TN'], n)}]  N={n}")
    pos = of["TP"] + of["FN"]
    print(f"TP recall TP/(TP+FN), Wilson 95% CI: {pct(of['TP'] / pos if pos else None)}% "
          f"[{ci_str(of['TP'], pos)}]  positives={pos}")
    print("Per-alarm accuracy Wilson 95% CI:")
    for name, f, _, _ in rows[:-1]:
        m = f["TP"] + f["TN"] + f["FP"] + f["FN"]
        print(f"  {name:<26}[{ci_str(f['TP'] + f['TN'], m)}]  N={m}")

    beats = of["challenge_score"] is not None and base["overall"]["challenge_score"] is not None \
        and of["challenge_score"] > base["overall"]["challenge_score"]
    verdict = "PASS" if (beats and fn_ok) else "FAIL"
    print()
    print(f"Kill Rule: {verdict}  (fixed > baseline overall: {beats}; FN_fixed <= FN_v0 on every alarm: {fn_ok})")
    print("Note: 2015 Challenge Event 1/2 winners (81.39 / 85.04) were scored on a different "
          "500-record hidden set; not comparable to these numbers.")

    return 1 if (args.assert_pass and verdict == "FAIL") else 0


if __name__ == "__main__":
    sys.exit(main())
