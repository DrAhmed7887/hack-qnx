#!/bin/sh
# p1_qplms_probe.sh -- run inside the booted QNX 8.0 guest during the P1 gate.
# Looks for a QNX process-lifecycle (plms) service. Read-only.
# Last line is PROBE_RESULT=QPLMS_PRESENT or PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK.

found=0

echo "== uname =="
uname -a

echo "== binaries =="
for b in plms plmsctrl; do
    p=$(command -v "$b" 2>/dev/null)
    if [ -n "$p" ]; then echo "found: $p"; found=1; else echo "missing: $b"; fi
done

echo "== headers / libs =="
for d in /usr/include /proc/boot /system; do
    [ -d "$d" ] || { echo "no dir: $d"; continue; }
    hits=$(find "$d" \( -iname '*plms*' -o -iname '*libplms*' \) 2>/dev/null | head -n 10)
    if [ -n "$hits" ]; then echo "$hits"; found=1; else echo "none under $d"; fi
done

echo "== slog2 =="
if command -v slog2info >/dev/null 2>&1; then
    slog2info -b plms 2>&1 | tail -n 20
else
    echo "slog2info not available"
fi

if [ "$found" -eq 1 ]; then
    echo "PROBE_RESULT=QPLMS_PRESENT"
else
    echo "PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK"
fi
