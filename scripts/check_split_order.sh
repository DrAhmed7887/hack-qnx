#!/usr/bin/env bash
# Prove eval/split.json was committed before the first post-v0-frozen change to src/qw_core.c.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

if ! git ls-files --error-unmatch eval/split.json >/dev/null 2>&1; then
  echo "FAIL: eval/split.json is not committed" >&2; exit 1
fi
if ! git diff --quiet HEAD -- eval/split.json; then
  echo "FAIL: eval/split.json has uncommitted changes" >&2; exit 1
fi

split_commit=$(git log --diff-filter=A --format=%H --reverse -- eval/split.json | head -n 1)
first_core=$(git rev-list --reverse v0-frozen..HEAD -- src/qw_core.c | head -n 1)

if [ -z "$first_core" ]; then
  echo "OK: src/qw_core.c unchanged since v0-frozen; eval/split.json tracked cleanly"
  exit 0
fi
if git merge-base --is-ancestor "$split_commit" "$first_core"; then
  echo "OK: split ($split_commit) precedes first core change ($first_core)"
else
  echo "FAIL: split ($split_commit) is not an ancestor of first core change ($first_core)" >&2
  exit 1
fi
