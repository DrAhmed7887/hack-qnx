# Quiet Ward 2.0: stress verification matrix (G3)

Research demo, not a medical device. `rules/alarm_rules.json` carries `status: "pending_signoff"`, so nothing here is a clinical claim. This matrix tracks engineering checks only.

Legend: **HOST PASS** = measured on the host-portable C11 code (macOS or Ubuntu), command and result given. **QEMU PENDING** = needs the booted QNX 8.0 target (blocked on P1, myQNX SDP 8.0 licence bring-up). QEMU runs under TCG, so any timing there is logical time, not hardware real time.

Last host run: `submission/g3/host_tests_20261124.log` (clean `git archive HEAD` export without `.git`/`.venv`/`.pytest_cache`; suite contains no network-using commands; all 8 commands: `make`, `make gateway-test`, `make reentrant-test`, `make demo-verify`, `make fuzz`, `python3 scripts/fuzz_qw.py --count 1000`, `python3 scripts/host_shift_replay.py`, `python3 scripts/render_demo_plots.py`) plus `submission/g3/asan_ubsan_20261124.log` (clean ASan+UBSan builds of `qw_gateway_test` and `qw_reentrant_test`, `exit=0`).

| # | Area | Check | Host (portable C11) | QNX 8.0 QEMU G3 soak |
|---|---|---|---|---|
| 1 | Load | 16 pending alarms per bed accepted, the 17th refused with `QW_ST_BUSY` and latched `CONFIRM` | HOST PASS: `test_pending_overflow` in `qw_gateway_test` | n/a (same state machine) |
| 2 | Load | Concurrent readers on the seqlock ring never see a torn frame | HOST PASS: 10000 frames, 4 reader threads, torn=0 (`test_ring_stress`) | QEMU PENDING: real processes, `/qw_ring_bed%u` over QNX shm |
| 3 | Load | 8 concurrent clients on `/dev/quietward` and the decision channel | Partial: 8 threads x `qw_decide()` consistent (320 calls, `qw_reentrant_test`). The 8-client resmgr path does not exist on host | QEMU PENDING |
| 4 | Fail-open | Detached, undersized or corrupt-header shared memory reports `QW_ST_DETACHED`, never stale data as fresh (a stalled live writer reports `QW_ST_BUSY`; the caller fails open) | HOST PASS: `test_ring_detached` | QEMU PENDING: kill `qw_acq` mid-run |
| 5 | Fail-open | Deadline expiry or stalled manager latches `CONFIRM` (500 ms default deadline, logged "no decision within deadline"). A late decision, a late nurse ACK and a backward clock step also fail open | HOST PASS: `test_deadline` (late decision returns `QW_ST_BUSY` and latches `CONFIRM`; backward clock step latches) and `test_review_and_ack` (late ACK returns `QW_ST_BUSY` and latches) | QEMU PENDING: `--watchdog-ms` timer pulse on a real clock |
| 6 | Fail-open | Review queue full (16 in review) fails open to `CONFIRM` with `QW_ST_BUSY` | HOST PASS: `test_decisions` | n/a |
| 7 | Fail-open | Supervisor fault limit / manual invocation: standalone `qw_failopen` binary (invoked by `qw_supervisor` after >3 restarts or manually; needs no running supervisor/IPC to write `CONFIRM_ALL`) latches `CONFIRM` | QEMU PENDING: not compiled with `qcc` or booted yet (needs `qw_failopen` and `QPLMS` or `qw_supervisor`) | QEMU PENDING |
| 8 | Honesty | Explicit status codes, never a fake OK: `QW_ST_BUSY`, `QW_ST_BAD_ARGS`, `QW_ST_NOT_FOUND`, `QW_ST_DUPLICATE`, `QW_ST_BAD_VERSION` | HOST PASS: asserted in `test_decisions` and `test_pending_overflow` | QEMU PENDING: same codes through `MsgReply` |
| 9 | Real turns | Every record in `data/demo/PLAYLIST.json` runs end to end through `build/qw` | HOST PASS: 6 of 6 ran; 5 `CONFIRM`, 1 `SUPPRESS_TO_REVIEW`; 0 true alarms in the review queue (`submission/g3/host_shift_review.md`) | QEMU PENDING: same playlist through the IPC chain |
| 10 | Safety filter | Out-of-range bed id or alarm id rejected with `QW_ST_BAD_ARGS`; bad decision enum rejected; duplicate and replayed sequence numbers rejected (a duplicate also latches `CONFIRM` on its bed) | HOST PASS: `test_pending_overflow`, `test_decisions` | QEMU PENDING |
| 11 | Hostile input | 1000 seeded parser mutations (`.qw`, rules, detector config): no crash, no timeout, stderr on every rejection, no accept of malformed-by-construction input | HOST PASS: `python3 scripts/fuzz_qw.py --count 1000` -> 1000/1000 (seed 20261124) | n/a (parser is the same code) |
| 12 | Hostile input | Hostile nurse ACK JSON (oversize, trailing garbage, duplicate keys, negative or fractional ack, control characters, wrong types) all rejected | HOST PASS: `test_hostile_ack` (22 hostile strings, embedded NUL bytes, plus edge cases) | QEMU PENDING: via `write()` on `/dev/quietward` |
| 13 | Limits | Restart journal replay: unacked raises and unacked reviews come back as latched `CONFIRM`; acked or confirmed ones stay clear. A corrupt, empty, over-long or NUL-bearing line, a bad number (negative, overflow, bed or alarm out of range), a torn final `A`/`C` line, table overflow, or a symlinked journal (`O_NOFOLLOW`) fails open across all beds (every bed latched `CONFIRM`, return = `QW_MAX_BEDS`). A torn final `R`/`Q` line that still parses keeps its bed latched | HOST PASS: `test_journal` | QEMU PENDING: kill and restart `qw_alarm_mgr` |
| 14 | Spend | 0 paid APIs, 0 cloud inference, commands ran from a clean `git archive HEAD` export without `.git`/`.venv`/`.pytest_cache`; suite contains no network-using commands or paid APIs | HOST PASS: commands ran from a clean `git archive HEAD` export without `.git`/`.venv`/`.pytest_cache` (`NO_GIT_NO_VENV_NO_CACHE_OK` in `submission/g3/host_tests_20261124.log`); suite contains no network-using commands or paid APIs | Codespaces and QEMU use free tiers only |
| 15 | Afterwards | ASan+UBSan runtime checks clean (`exit=0`, `2>&1` captured in `submission/g3/asan_ubsan_20261124.log`; leak detection not run on macOS) | HOST PASS: ASan+UBSan builds of `qw_gateway_test` (529 checks, 0 failed) and `qw_reentrant_test` (320 calls) ran with `exit=0` and 0 diagnostics (`submission/g3/asan_ubsan_20261124.log`; leak detection not run on macOS) | QEMU PENDING: same flags on the QNX toolchain if supported |
| 16 | Audit trail | Audit log line format (`qw_log_format_line`: one line, control characters replaced, at most 511 bytes for the slog2 limit) and the `/dev/quietward` JSONL serializer (`qw_gw_format_log_jsonl`: only whole lines, NUL-terminated, null and zero-size arguments rejected) | HOST PASS: `test_log_format` and `test_resmgr_jsonl` in `qw_gateway_test` (formatting only) | QEMU PENDING: real `slog2f` output and `slog2info` capture, `read()` on `/dev/quietward` |

## Totals

- Host column: 14 of 16 rows HOST PASS, row 3 partial (threads only, no 8-client resmgr path on host), row 7 pending (not compiled with `qcc` or booted yet). Many HOST PASS rows still have a QEMU leg listed in the last column.
- Whole matrix is **not** complete until the QEMU G3 soak runs. No QNX result is claimed here.
- `qw_gateway_test`: 529 checks, 0 failed. `qw_reentrant_test`: 320 concurrent `qw_decide()` calls across 8 threads, all consistent.

## Observation from fuzzing (not a defect fix)

A rules file with `suppression.never_suppress` set to an empty list `[]` is accepted by the loader (it is a well-typed value). That removes the unconditional-`CONFIRM` guard for ventricular flutter/fibrillation. It is a rules-content question for the clinical sign-off, so the fuzzer does not treat it as a malformed input.
