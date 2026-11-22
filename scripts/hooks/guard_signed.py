#!/usr/bin/env python3
"""Kiro preToolUse hook: block writes to signed/pending clinical rule files."""
import json, sys
PROTECTED = ("rules/", "CLAIMS.md", ".kiro/")
try:
    ev = json.load(sys.stdin)
except Exception:
    sys.exit(0)
blob = json.dumps(ev.get("tool_input", ev))
for p in PROTECTED:
    if p in blob:
        print(f"BLOCKED: {p} is protected (clinical rules / claims / agent config). Leave it for the clinical lead.", file=sys.stderr)
        sys.exit(2)
sys.exit(0)
