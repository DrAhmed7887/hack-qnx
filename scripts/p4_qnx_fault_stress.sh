#!/bin/sh
# p4_qnx_fault_stress.sh -- Extended QNX 8.0 target fault-injection & load stress suite.
# Covers:
#   1. All 6 demo records (a109l, a653l, t116s, t106s, v188s, f450s) through QNX IPC
#   2. 8 concurrent /dev/quietward snapshot readers + 9th reader EBUSY cap
#   3. 4th worker crash -> qw_supervisor invokes Tier-2 qw_failopen (CONFIRM_ALL)
#   4. Corrupt restart journal fail-open across all 4 beds
#   5. Symlinked journal (O_NOFOLLOW) fail-open across all 4 beds without target clobber
#   6. qw_acq alarm admission failure when qw_alarm_mgr is down

set -u

BIN_DIR=${QW_BIN_DIR:-/data/quietward/bin}
RULES=${QW_RULES:-/data/quietward/rules/alarm_rules.json}
DETCFG=${QW_DETECTOR_CFG:-/data/quietward/config/detector.json}
DEMO_DIR=${QW_DEMO_DIR:-/data/quietward/data/demo}
STATE_DIR=/var/run/quietward

CHECKS=0
FAILS=0

check() {
    desc=$1
    shift
    CHECKS=$((CHECKS + 1))
    if "$@" >/dev/null 2>&1; then
        echo "PASS  $desc"
    else
        echo "FAIL  $desc"
        FAILS=$((FAILS + 1))
    fi
}

reset_gw() {
    slay -f qw_supervisor qw_detector qw_alarm_mgr qw_acq 2>/dev/null || true
    sleep 1
    rm -rf "$STATE_DIR" /tmp/qw_symlink_target /dev/shmem/qw_ring_bed* /dev/shm/qw_ring_bed* 2>/dev/null || true
    mkdir -p "$STATE_DIR" && chmod 0700 "$STATE_DIR"
    slog2info -c >/dev/null 2>&1 || true
}

echo "=== P4 QNX 8.0 EXTENDED FAULT-INJECTION STRESS SUITE ==="

# ---- Test 1 & Test 2: 6/6 demo playlist + 8-reader /dev/quietward EBUSY cap ----
reset_gw
"$BIN_DIR/qw_supervisor" "$RULES" "$DETCFG" --bin-dir "$BIN_DIR" --restart-delay-ms 5000 </dev/null >/dev/null 2>&1 &
SUP_PID=$!
sleep 2

replay_one() {
    "$BIN_DIR/qw_acq" "$DEMO_DIR/$1.qw" "$2" --bed "$3" --seq "$4" --rate-ms 50 --speed 100
}

replay_one a109l Asystole 0 101
check "playlist a109l (Asystole, bed 0, seq 101) exit 0" test $? -eq 0
replay_one a653l Asystole 1 102
check "playlist a653l (Asystole, bed 1, seq 102) exit 0" test $? -eq 0
replay_one t116s Tachycardia 2 103
check "playlist t116s (Tachycardia, bed 2, seq 103) exit 0" test $? -eq 0
replay_one t106s Tachycardia 3 104
check "playlist t106s (Tachycardia, bed 3, seq 104) exit 0" test $? -eq 0
replay_one v188s Ventricular_Tachycardia 0 105
check "playlist v188s (Ventricular_Tachycardia, bed 0, seq 105) exit 0" test $? -eq 0
replay_one f450s Ventricular_Flutter_Fib 1 106
check "playlist f450s (Ventricular_Flutter_Fib, bed 1, seq 106) exit 0" test $? -eq 0
sleep 1

check "playlist a109l -> Q 101 (SUPPRESS_TO_REVIEW)" grep -q '^Q 101 ' "$STATE_DIR/journal"
check "playlist a653l -> C 102 (CONFIRM)" grep -q '^C 102 ' "$STATE_DIR/journal"
check "playlist t116s -> C 103 (conservative CONFIRM)" grep -q '^C 103 ' "$STATE_DIR/journal"
check "playlist t106s -> C 104 (low-SQI CONFIRM)" grep -q '^C 104 ' "$STATE_DIR/journal"
check "playlist v188s -> C 105 (TRUE VT CONFIRM)" grep -q '^C 105 ' "$STATE_DIR/journal"
check "playlist f450s -> C 106 (never_suppress CONFIRM)" grep -q '^C 106 ' "$STATE_DIR/journal"

check "resmgr cap: 8 concurrent /dev/quietward readers + 9th EBUSY + release" "$BIN_DIR/p4_resmgr_cap_test"

kill "$SUP_PID" 2>/dev/null || true
wait "$SUP_PID" 2>/dev/null || true

# ---- Test 3: 4th worker crash -> qw_supervisor invokes Tier-2 qw_failopen ----
reset_gw
"$BIN_DIR/qw_supervisor" "$RULES" "$DETCFG" --bin-dir "$BIN_DIR" --restart-delay-ms 200 </dev/null >/dev/null 2>&1 &
SUP_PID=$!
sleep 2

for k in 1 2 3 4; do
    slay -s KILL qw_detector >/dev/null 2>&1 || true
    sleep 1
done

wait "$SUP_PID" 2>/dev/null
SUP_RC=$?
check "4th crash: qw_supervisor exited non-zero (rc=$SUP_RC)" test "$SUP_RC" -ne 0
check "4th crash: alarm_out contains CONFIRM_ALL" grep -qx 'CONFIRM_ALL' "$STATE_DIR/alarm_out"
check "4th crash: slog2 logged fault limit + qw_failopen" sh -c "slog2info -b quietward | grep -q 'worker fault limit reached; running qw_failopen'"
check "4th crash: slog2 logged fail-open all beds CONFIRM" sh -c "slog2info -b quietward | grep -q 'fail-open: all beds CONFIRM'"

# ---- Test 4: Corrupt restart journal fail-open across all beds ----
reset_gw
printf 'CORRUPT GARBAGE LINE\n' > "$STATE_DIR/journal"
"$BIN_DIR/qw_alarm_mgr" </dev/null >/dev/null 2>&1 &
MGR_PID=$!
sleep 1
for b in 0 1 2 3; do
    check "corrupt journal: bed $b failopen_active:1" sh -c "$BIN_DIR/qw_bedctl $b | grep -q '\"failopen_active\":1'"
done
check "corrupt journal: slog2 latched unresolved alarms" sh -c "slog2info -b quietward | grep -q 'journal replay latched unresolved alarms'"
kill "$MGR_PID" 2>/dev/null || true
wait "$MGR_PID" 2>/dev/null || true

# ---- Test 5: Symlinked journal O_NOFOLLOW fail-open across all beds ----
reset_gw
printf 'KEEP\n' > /tmp/qw_symlink_target
ln -s /tmp/qw_symlink_target "$STATE_DIR/journal"
"$BIN_DIR/qw_alarm_mgr" </dev/null >/dev/null 2>&1 &
MGR_PID=$!
sleep 1
for b in 0 1 2 3; do
    check "symlink journal: bed $b failopen_active:1" sh -c "$BIN_DIR/qw_bedctl $b | grep -q '\"failopen_active\":1'"
done
check "symlink journal: target file untouched" grep -qx 'KEEP' /tmp/qw_symlink_target
kill "$MGR_PID" 2>/dev/null || true
wait "$MGR_PID" 2>/dev/null || true

# ---- Test 6: qw_acq admission failure when qw_alarm_mgr is down ----
reset_gw
"$BIN_DIR/qw_acq" "$DEMO_DIR/a109l.qw" Asystole --bed 0 --seq 301 --rate-ms 50 --speed 100 >/dev/null 2>&1
ACQ_RC=$?
check "admission failure: qw_acq exits non-zero without manager (rc=$ACQ_RC)" test "$ACQ_RC" -ne 0
check "admission failure: slog2 logged CRIT admission/journal fail" sh -c "slog2info -b quietward | grep -q 'alarm admission or durable journal failed'"
check "admission failure: seq 301 did NOT log RAISED" sh -c "! slog2info -b quietward | grep -q 'seq=301 .*decision=RAISED'"

reset_gw
echo "=== P4 SUMMARY: $CHECKS checks, $FAILS failed ==="
[ "$FAILS" -eq 0 ]
