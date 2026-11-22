"""Tests for review finding #4: src/rules.c's JSON value-skipper
(qw_skip_value_depth in src/qw_json_lexer.h, used by rules.c to tolerate
unrecognized fields) must bound its recursion depth at 32, so a
maliciously or accidentally deeply-nested rules.json cannot exhaust the
stack. Exercised through the real C reader via build/qw_rules_dump.
"""
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from helpers import RULES_DUMP_BIN, write_rules  # noqa: E402


def dump_rules_raw(rules_path: Path):
    assert RULES_DUMP_BIN.exists(), f"{RULES_DUMP_BIN} not found; run `make test` first"
    return subprocess.run([str(RULES_DUMP_BIN), str(rules_path)], capture_output=True, text=True)


def _nested_object(depth: int):
    """Build a JSON object nested `depth` levels deep: {"x": {"x": {...: 1}}}."""
    value = 1
    for _ in range(depth):
        value = {"x": value}
    return value


def _rules_with_extra_field(tmp_path: Path, depth: int) -> Path:
    rules_path = tmp_path / "rules.json"
    rules = {
        "status": "pending_signoff",
        "alarms": {
            "Asystole": {"definition": "No QRS for at least 4 seconds", "no_beat_s": 4},
        },
        "suppression": {
            "window_s": 16,
            "min_signal_quality": 0.9,
            "never_suppress": [],
        },
        # Unrecognized top-level field, deeply nested: exercises the
        # skip_value() path, not any field this project depends on.
        "extra_metadata": _nested_object(depth),
    }
    with open(rules_path, "w") as f:
        json.dump(rules, f)
    return rules_path


def test_moderately_nested_unknown_field_still_parses(tmp_path):
    """Nesting well within the depth limit must still parse fine -- the
    limit must not be so tight it breaks legitimate (if unusual)
    metadata fields added in the future."""
    rules_path = _rules_with_extra_field(tmp_path, depth=10)

    proc = dump_rules_raw(rules_path)

    assert proc.returncode == 0, proc.stderr


def test_deeply_nested_unknown_field_rejected(tmp_path):
    """Nesting well beyond the depth limit (32) must be rejected rather
    than recursing without bound. Depth chosen to clear the C skip_value
    depth limit while staying well under Python's own json.dump
    recursion limit for building the fixture."""
    rules_path = _rules_with_extra_field(tmp_path, depth=200)

    proc = dump_rules_raw(rules_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


def test_nesting_just_over_limit_rejected(tmp_path):
    rules_path = _rules_with_extra_field(tmp_path, depth=40)

    proc = dump_rules_raw(rules_path)

    assert proc.returncode != 0
    assert "load_failed" in proc.stderr


def test_never_suppress_array_unaffected_by_depth_limit(tmp_path):
    """Sanity check: the normal (shallow) document shape this project
    actually uses is completely unaffected by the depth limit."""
    rules_path = tmp_path / "rules.json"
    write_rules(rules_path, never_suppress=["Asystole", "Bradycardia", "Tachycardia"])

    proc = dump_rules_raw(rules_path)

    assert proc.returncode == 0, proc.stderr
