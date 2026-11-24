#!/usr/bin/env python3
"""Parser fail-closed fuzzer for build/qw (stdlib only).

Mutates COPIES (in a temp dir) of the real data/demo/*.qw files plus the
rules and detector JSON files, runs build/qw on each, and checks that the
binary always exits cleanly: never killed by a signal, never a timeout,
always a message on stderr for a nonzero exit, and never exit 0 on an
input that is malformed by construction. It never synthesises physiological
waveforms; it only attacks the parsers.
"""
import argparse
import json
import math
import random
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TIMEOUT_S = 30
HDR = 16  # magic(4) + fs, n_channels, n_samples (3 x u32 LE)

QW_KINDS = ["bad_magic", "truncated_header", "size_mismatch", "nan_inf",
            "fs_zero", "fs_huge", "channels_zero", "channels_huge", "random_bytes"]
# Kinds that are malformed by construction: exit 0 would be a bug.
QW_MUST_REJECT = set(QW_KINDS) - {"random_bytes"}


def mutate_qw(kind, data, rng):
    b = bytearray(data)
    if kind == "bad_magic":
        b[0:4] = bytes(rng.randrange(256) for _ in range(4))
        if bytes(b[0:4]) == bytes(data[0:4]):
            b[0] ^= 0xFF
    elif kind == "truncated_header":
        b = b[:rng.randrange(0, HDR)]
    elif kind == "size_mismatch":
        if rng.random() < 0.5:
            b = b[:len(b) - rng.randrange(1, 4096)]
        else:
            b += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 4096)))
    elif kind == "nan_inf":
        n_ch = struct.unpack_from("<I", b, 8)[0]
        # float payload sits at the end of the file (after the channel name table)
        n_samples = struct.unpack_from("<I", b, 12)[0]
        payload = 4 * n_ch * n_samples
        start = len(b) - payload
        bad = rng.choice([float("nan"), float("inf"), float("-inf")])
        for _ in range(rng.randrange(1, 5)):
            off = start + 4 * rng.randrange(0, n_ch * n_samples)
            struct.pack_into("<f", b, off, bad)
    elif kind == "fs_zero":
        struct.pack_into("<I", b, 4, 0)
    elif kind == "fs_huge":
        struct.pack_into("<I", b, 4, rng.choice([99999, 10001, 0xFFFFFFFF]))
    elif kind == "channels_zero":
        struct.pack_into("<I", b, 8, 0)
    elif kind == "channels_huge":
        struct.pack_into("<I", b, 8, rng.choice([99, 17, 0xFFFFFFFF]))
    elif kind == "random_bytes":
        for _ in range(rng.randrange(1, 33)):
            b[rng.randrange(len(b))] = rng.randrange(256)
    return bytes(b)


def mutate_json_file(text, must_reject_keys, rng):
    """Return (kind, bytes, must_reject) for a malformed JSON config."""
    obj = json.loads(text)
    choice = rng.choice(["empty", "garbage", "truncated", "not_object", "deep_nest",
                         "drop_required", "bad_type", "random_bytes", "missing_file"])
    raw = text.encode()
    if choice == "empty":
        return choice, b"", True
    if choice == "garbage":
        return choice, bytes(rng.randrange(256) for _ in range(rng.randrange(1, 512))), True
    if choice == "truncated":
        return choice, raw[:rng.randrange(1, max(2, len(raw) // 2))], True
    if choice == "not_object":
        return choice, rng.choice([b"[]", b"42", b'"x"', b"null"]), True
    if choice == "deep_nest":
        d = rng.randrange(200, 20000)
        return choice, b"[" * d + b"]" * d, True
    if choice == "drop_required":
        sect, key = rng.choice(must_reject_keys)
        o = json.loads(text)
        del o[sect][key]
        return choice, json.dumps(o).encode(), True
    if choice == "bad_type":
        sect, key = rng.choice(must_reject_keys)
        o = json.loads(text)
        # [] is a well-typed (if clinically risky) never_suppress value, so it
        # is not a parser-malformed input; skip it for that key.
        vals = ["x", None, {}, True] + ([] if key == "never_suppress" else [[]])
        o[sect][key] = rng.choice(vals)
        return choice, json.dumps(o).encode(), True
    if choice == "random_bytes":
        b = bytearray(raw)
        for _ in range(rng.randrange(1, 17)):
            b[rng.randrange(len(b))] = rng.randrange(256)
        return choice, bytes(b), False  # may still be valid JSON
    return "missing_file", None, True


def run(cmd):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=TIMEOUT_S)
    except subprocess.TimeoutExpired:
        return None, b"", b""
    return p.returncode, p.stdout, p.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--count", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=20261124)
    ap.add_argument("--bin", default="build/qw")
    ap.add_argument("--rules", default="rules/alarm_rules.json")
    ap.add_argument("--config", default="config/detector.json")
    args = ap.parse_args()

    def rel(p):
        p = Path(p)
        return p if p.is_absolute() else ROOT / p

    binp, rules_p, cfg_p = rel(args.bin), rel(args.rules), rel(args.config)
    for p in (binp, rules_p, cfg_p):
        if not p.exists():
            print(f"fuzz: missing {p.name}; run `make` first", file=sys.stderr)
            return 2

    manifest = json.loads((ROOT / "data/demo/MANIFEST.json").read_text())
    recs = [(ROOT / "data/demo" / r["file"], r["alarm_type"]) for r in manifest["records"]]
    blobs = {p: p.read_bytes() for p, _ in recs}
    rules_text = rules_p.read_text()
    cfg_text = cfg_p.read_text()
    rules_keys = [("suppression", k) for k in ("window_s", "min_signal_quality", "never_suppress")]
    cfg_obj = json.loads(cfg_text)
    cfg_keys = [(s, k) for s, v in cfg_obj.items() if isinstance(v, dict) for k in v]

    n_rules = args.count // 4
    n_det = args.count // 4
    n_qw = args.count - n_rules - n_det
    plan = ["qw"] * n_qw + ["rules"] * n_rules + ["detector"] * n_det

    rng = random.Random(args.seed)
    passed = 0
    failures = []
    t0 = time.time()
    with tempfile.TemporaryDirectory(prefix="qw_fuzz_") as td:
        td = Path(td)
        for i, target in enumerate(plan):
            rec_path, alarm = rng.choice(recs)
            rules_use, cfg_use, qw_use = str(rules_p), str(cfg_p), str(rec_path)
            must_reject = True
            if target == "qw":
                kind = QW_KINDS[i % len(QW_KINDS)]
                qw_use = str(td / "m.qw")
                Path(qw_use).write_bytes(mutate_qw(kind, blobs[rec_path], rng))
                must_reject = kind in QW_MUST_REJECT
            elif target == "rules":
                kind, data, must_reject = mutate_json_file(rules_text, rules_keys, rng)
                rules_use = str(td / "r.json")
                if data is not None:
                    Path(rules_use).write_bytes(data)
            else:
                kind, data, must_reject = mutate_json_file(cfg_text, cfg_keys, rng)
                cfg_use = str(td / "d.json")
                if data is not None:
                    Path(cfg_use).write_bytes(data)
            label = f"{target}:{kind}#{i}"
            rc, out, err = run([str(binp), qw_use, alarm, rules_use, cfg_use])
            problem = None
            if rc is None:
                problem = f"timeout > {TIMEOUT_S}s"
            elif rc < 0:
                problem = f"killed by signal {-rc}"
            elif rc == 0:
                if must_reject:
                    problem = "exit 0 on malformed input (unexpected accept)"
                else:
                    try:
                        json.loads(out)
                    except ValueError:
                        problem = "exit 0 but stdout is not JSON"
            elif not err.strip():
                problem = f"rc={rc} with empty stderr"
            if problem:
                failures.append(f"{label}: {problem}")
                if target == "qw":
                    keep = ROOT / "build" / f"fuzz_fail_{i}.qw"
                    keep.write_bytes(Path(qw_use).read_bytes())
            else:
                passed += 1

    elapsed = time.time() - t0
    for f in failures[:20]:
        print("FAIL", f)
    print(f"fuzz: {passed}/{len(plan)} passed ({n_qw} qw, {n_rules} rules, {n_det} detector; "
          f"seed {args.seed}) in {elapsed:.1f}s; {len(failures)} failure(s)")
    return 0 if not failures and passed == args.count else 1


if __name__ == "__main__":
    sys.exit(main())
