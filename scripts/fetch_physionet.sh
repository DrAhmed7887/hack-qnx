#!/usr/bin/env bash
# Fetch PhysioNet/CinC Challenge 2015 training set (ODC-BY 1.0) and convert to data/bin.
# Source: https://physionet.org/content/challenge-2015/1.0.0/
set -euo pipefail
cd "$(dirname "$0")/.."

BASE="https://physionet.org/content/challenge-2015/1.0.0"
ZIP=data/raw/training.zip
mkdir -p data/raw

if [ ! -f "$ZIP" ]; then
  curl -fL --retry 3 -o "$ZIP" "$BASE/training.zip?download"
fi
if [ ! -d data/raw/training ]; then
  unzip -q "$ZIP" -d data/raw
fi
[ -d data/raw/training ] || { echo "error: data/raw/training missing after unzip" >&2; exit 1; }

.venv/bin/python scripts/convert.py --src data/raw/training --dst data/bin
