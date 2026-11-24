#!/bin/sh
# p3_qnx_soak.sh -- G3 soak harness for the Quiet Ward QNX 8.0 gateway.
#
# Run ON the QNX target (QEMU image) as root, after `make qnx` binaries and
# the config files are copied over. It performs RUNS (default 10) independent
# runs, each from reset state, and writes one transcript per run:
#   submission/g3/g3_run_01.txt .. g3_run_10.txt
#
# Per run:
#   1. reset state: stop old processes, remove /var/run/quietward, shm rings
#   2. start qw_supervisor (alarm manager + detector) and check with pidin
#   3. replay the demo records (a109l, a653l, v188s) through qw_acq
#   4. read /dev/quietward, write a nurse ACK, query qw_bedctl
#   5. slay -s KILL qw_detector: expect the next alarm to fail open (CONFIRM
#      latched after the deadline), the supervisor to restart the detector,
#      and the event to appear in slog2info
#   6. capture slog2info and clean up
#
# QEMU/TCG time is logical time, not hardware real-time: this script checks
# behaviour (alarms never dropped, fail-open, restart), not latency numbers.
#
# Usage: scripts/p3_qnx_soak.sh [-n RUNS] [-b BIN_DIR] [-r RULES] [-d DETECTOR]
#                               [-D DEMO_DIR] [-o OUT_DIR] [-m RATE_MS]
# Exit status: 0 only if every run passed every check.

set -u

RUNS=10
BIN_DIR=${QW_BIN_DIR:-/opt/quietward/bin}
RULES=${QW_RULES:-/opt/quietward/rules/alarm_rules.json}
DETCFG=${QW_DETECTOR_CFG:-/opt/quietward/config/detector.json}
DEMO_DIR=${QW_DEMO_DIR:-/opt/quietward/data/demo}
OUT_DIR=${QW_OUT_DIR:-submission/g3}
RATE_MS=${QW_RATE_MS:-100}
STATE_DIR=/var/run/quietward
DEV=/dev/quietward
RECORDS="a109l a653l v188s"
ALARM_FOR_a109l=Asystole
ALARM_FOR_a653l=Asystole
ALARM_FOR_v188s=Ventricular_Tachycardia
# Wait slightly longer than the 500 ms decision deadline, in whole seconds.
DEADLINE_WAIT_S=2

while getopts "n:b:r:d:D:o:m:h" opt; do
    case "$opt" in
        n) RUNS=$OPTARG ;;
        b) BIN_DIR=$OPTARG ;;
        r) RULES=$OPTARG ;;
        d) DETCFG=$OPTARG ;;
        D) DEMO_DIR=$OPTARG ;;
        o) OUT_DIR=$OPTARG ;;
        m) RATE_MS=$OPTARG ;;
        *) sed -n '2,31p' "$0"; exit 2 ;;
    esac
done

FAILS=0
CHECKS=0

check() { # check <description> <command...>
    check_desc=$1
    shift
    CHECKS=$((CHECKS + 1))
    if "$@" >/dev/null 2>&1; then
        echo "PASS  $check_desc"
    else
        echo "FAIL  $check_desc"
        FAILS=$((FAILS + 1))
    fi
}

alive() { pidin -p "$1" 2>/dev/null | grep -q "$1"; }

reset_state() {
    slay -f qw_supervisor qw_detector qw_alarm_mgr qw_acq 2>/dev/null
    sleep 1
    rm -rf "$STATE_DIR"
    rm -f /dev/shmem/qw_ring_bed* /dev/shm/qw_ring_bed* 2>/dev/null
    mkdir -p "$STATE_DIR" && chmod 0700 "$STATE_DIR"
}

replay_record() { # replay_record <name> <alarm> <bed> <seq>
    "$BIN_DIR/qw_acq" "$DEMO_DIR/$1.qw" "$2" --bed "$3" --seq "$4" --rate-ms "$RATE_MS"
}

journal_has() { grep -q "^$1 $2 " "$STATE_DIR/journal"; } # journal_has <op> <seq>

one_run() {
    # POSIX sh/ksh: no `local`; one_run is not recursive, so plain variables
    # are safe (run_n is distinct from the main loop's n).
    unset review_seq old_det new_det
    run_n=$1
    seq=100
    echo "=== run $run_n of $RUNS: $(date) ==="
    reset_state

    "$BIN_DIR/qw_supervisor" "$RULES" "$DETCFG" --bin-dir "$BIN_DIR" &
    SUP_PID=$!
    sleep 2

    # 2. processes up
    check "pidin: qw_supervisor running" alive qw_supervisor
    check "pidin: qw_alarm_mgr running" alive qw_alarm_mgr
    check "pidin: qw_detector running" alive qw_detector
    check "$DEV exists" test -c "$DEV"

    # 3. replay the demo records, one bed each, and let the deadline pass
    bed=0
    for rec in $RECORDS; do
        case "$rec" in
            a109l) alarm=$ALARM_FOR_a109l ;;
            a653l) alarm=$ALARM_FOR_a653l ;;
            v188s) alarm=$ALARM_FOR_v188s ;;
            *)
                echo "error: unknown demo record '$rec'" >&2
                kill "$SUP_PID" 2>/dev/null
                wait "$SUP_PID" 2>/dev/null
                reset_state
                exit 2
                ;;
        esac
        seq=$((seq + 1))
        replay_record "$rec" "$alarm" "$bed" "$seq"
        check "replay $rec ($alarm, bed $bed, seq $seq): qw_acq exit 0" test $? -eq 0
        check "replay $rec: journal has R $seq" journal_has R "$seq"
        check "replay $rec: decision journalled (C or Q $seq)" sh -c \
            "grep -q '^[CQ] $seq ' $STATE_DIR/journal"
        bed=$((bed + 1))
    done

    # 4. resource manager: read, nurse ACK, bedctl
    check "cat $DEV returns JSONL" sh -c "cat $DEV | head -n 1 | grep -q '^{\"seq\":'"
    review_seq=$(grep '^Q ' "$STATE_DIR/journal" | tail -n 1 | cut -d' ' -f2)
    if [ -n "$review_seq" ]; then
        check "nurse ACK of seq $review_seq via write()" sh -c \
            "echo '{\"ack\":$review_seq,\"nurse\":\"soak-rn\"}' > $DEV"
        check "ACK journalled (A $review_seq)" journal_has A "$review_seq"
    else
        echo "NOTE  no review entry this run (every demo alarm confirmed); ACK path not exercised"
    fi
    check "write() of a malformed ACK is refused" sh -c "! echo 'not json' > $DEV"
    check "qw_bedctl 0 prints JSON" sh -c "$BIN_DIR/qw_bedctl 0 | grep -q '\"bed_id\":0'"
    check "qw_bedctl 9 (bad bed) fails" sh -c "! $BIN_DIR/qw_bedctl 9"

    # 5. detector killed: next alarm must still be latched CONFIRM
    old_det=$(pidin -p qw_detector 2>/dev/null | awk 'NR==2{print $1}')
    slay -s KILL qw_detector
    seq=$((seq + 1))
    replay_record a109l Asystole 3 "$seq"
    sleep "$DEADLINE_WAIT_S"
    check "fail-open: C $seq journalled with no detector" journal_has C "$seq"
    check "fail-open: bedctl 3 shows failopen_active" sh -c \
        "$BIN_DIR/qw_bedctl 3 | grep -q '\"failopen_active\":1'"
    sleep 2
    check "supervisor restarted qw_detector" alive qw_detector
    new_det=$(pidin -p qw_detector 2>/dev/null | awk 'NR==2{print $1}')
    check "restarted detector has a new pid (was ${old_det:-none})" \
        sh -c '[ -n "$1" ] && [ -n "$2" ] && [ "$1" != "$2" ]' _ "$old_det" "$new_det"

    # 6. slog2 evidence, then stop
    slog_out="$OUT_DIR/slog2_run_$(printf '%02d' "$run_n").txt"
    slog2info -b quietward > "$slog_out" 2>&1
    check "slog2info captured quietward audit lines" grep -q 'sev=' "$slog_out"
    check "slog2info shows the fail-open latch (CRIT)" grep -q 'sev=CRIT' "$slog_out"

    kill "$SUP_PID" 2>/dev/null
    wait "$SUP_PID" 2>/dev/null
    reset_state
    echo "=== run $run_n done ==="
}

mkdir -p "$OUT_DIR" || exit 2
for tool in qw_acq qw_detector qw_alarm_mgr qw_supervisor qw_bedctl; do
    [ -x "$BIN_DIR/$tool" ] || { echo "missing $BIN_DIR/$tool: run make qnx and copy build/qnx/*"; exit 2; }
done
for f in "$RULES" "$DETCFG"; do
    [ -r "$f" ] || { echo "missing config $f"; exit 2; }
done
for rec in $RECORDS; do
    [ -r "$DEMO_DIR/$rec.qw" ] || { echo "missing record $DEMO_DIR/$rec.qw"; exit 2; }
done

RUN_FAILS=0
n=1
while [ "$n" -le "$RUNS" ]; do
    out="$OUT_DIR/g3_run_$(printf '%02d' "$n").txt"
    before=$FAILS
    one_run "$n" > "$out" 2>&1
    if [ "$FAILS" -gt "$before" ]; then
        RUN_FAILS=$((RUN_FAILS + 1))
        echo "run $n: FAILED ($((FAILS - before)) checks) -> $out"
    else
        echo "run $n: ok -> $out"
    fi
    n=$((n + 1))
done

echo "soak: $RUNS runs, $RUN_FAILS failed runs"
[ "$RUN_FAILS" -eq 0 ]
