# Quiet Ward on QNX 8.0 — mapping and status

## Status (25 Nov 2026 — QNX 8.0 Target Verified)

Host-verified (portable C11, builds on macOS and Linux, no QNX headers):

- `qw_ipc.h` (message, pulse and shared-memory layouts)
- `qw_ring.{h,c}` (per-bed seqlock ring)
- `qw_gateway_state.{h,c}` (pending/review state machine, deadline, journal replay)
- `qw_log.{h,c}` (audit line formatter, 511-byte slog2 limit; slog2 calls only behind `__QNX__`)
- `qw_resmgr.{h,c}` (`/dev/quietward` JSONL log serializer; the resource manager itself only behind `__QNX__`)
- `qw_gateway_test.c`: `make gateway-test` -> `qw_gateway_test: 529 checks, 0 failed`

Cross-compiled with **`qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror -lm -lslog2`** (`0` warnings, `readelf -h` x86_64 ELF) and verified running on **booted QNX Neutrino 8.0.0 (`QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64`)** under QEMU TCG (`-accel tcg -cpu IvyBridge`):

- `qw_acq.c`, `qw_detector.c`, `qw_alarm_mgr.c`, `qw_failopen.c`, `qw_supervisor.c`, `qw_bedctl.c` (`submission/g3/p1_qnx8_guest_20261125.log`, `submission/g3/qnx8_live_proof_20261125.log`, `submission/g3/g3_run_01..10.txt`: **10/10 soak runs, 230/230 checks, `0` failed**)
- QPLMS check (`scripts/p1_qplms_probe.sh`): `PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK` (`plms` and `plmsctrl` absent on target image; `qw_supervisor` fallback active).

## Where QNX 8.0 Appears (Reviewer Map)

Same table as the top-level `README.md`. Every primitive below is compiled and verified on the booted QNX 8.0 QEMU target.

| File | QNX 8.0 / POSIX / C11 primitives & tooling present in source | Verified QNX 8.0 / Host output | Scene |
|---|---|---|---|
| `qw_acq.c` | `shm_open`, `mmap`, `ChannelCreate`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceivePulse`, `name_open`, `MsgSendPulse`, `name_close` | Compiled (`build/qnx/qw_acq`); replayed `a109l`, `a653l`, `v188s` into `/dev/shmem/qw_ring_bed*` and raised `QW_PULSE_ALARM_RAISED` in 10/10 soak runs (`g3_run_01..10.txt`, `qnx8_live_proof_20261125.log`) | S02, S05 |
| `qw_detector.c` | `name_attach`, `name_open`, `name_close`, `shm_open`, `mmap`, `MsgReceive`, `MsgReply`, `MsgError`, `TimerTimeout`, `MsgSend`, `ConnectDetach` | Compiled (`build/qnx/qw_detector`); visible in `pidin`, sends `QW_MSG_DECISION` via `MsgSend`, killed (`slay -s KILL`) and respawned in 10/10 soak runs | S02, S05 |
| `qw_alarm_mgr.c` | `name_attach`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceive`, `MsgReply`, `MsgError`, `pthread_create`, `pthread_detach` | Compiled (`build/qnx/qw_alarm_mgr`); visible in `pidin` (2 threads), 50 ms watchdog tick latches `CONFIRM` (`sev=CRIT` `no decision within deadline (fail-open)` in `slog2_run_01..10.txt`) | S02, S05 |
| `qw_failopen.c` | standalone fail-open binary (`open` with `O_NOFOLLOW`, `write` of `CONFIRM_ALL`, `qw_log_emit` at CRIT) | Compiled (`build/qnx/qw_failopen`); verified standalone in `qnx8_live_proof_20261125.log` (`alarm_out=CONFIRM_ALL` + 4 `sev=CRIT` lines for beds `0..3`) | S02, S05 |
| `qw_supervisor.c` | `posix_spawn`, `waitpid`, `kill`, `sigaction` | Compiled (`build/qnx/qw_supervisor`); visible in `pidin`, spawns/restarts `qw_alarm_mgr` and `qw_detector` with new PID in 10/10 soak runs | S02, S05 |
| `qw_bedctl.c` | `open("/dev/quietward")`, `devctl(QW_DCMD_BED_STATE)` | Compiled (`build/qnx/qw_bedctl`); prints per-bed JSON including `failopen_active:1`, refuses `qw_bedctl 9` (`qnx8_live_proof_20261125.log`, `g3_run_01..10.txt`) | S05 |
| `qw_resmgr.c` | `dispatch_create`, `iofunc_func_init`, `iofunc_attr_init`, `resmgr_attach("/dev/quietward")`, `dispatch_context_alloc`, `dispatch_block`, `dispatch_handler`, `iofunc_read_verify`, `iofunc_write_verify`, `iofunc_devctl_default`, `resmgr_msgget`, `_IO_READ_GET_NBYTES`, `_IO_WRITE_GET_NBYTES`, `_IO_SET_READ_NBYTES`, `_IO_SET_WRITE_NBYTES`, `MsgReply` | Compiled (`RESMGR_OCB_T qw_ocb_t`, `&ocb->base`); `/dev/quietward` (`crw-rw----`) `cat` JSONL + nurse ACK `write()` verified live; malformed ACK refused in 10/10 runs | S02, S05 |
| `qw_log.c` | `slog2_register`, `slog2f` (`SLOG2_INFO`, `SLOG2_WARNING`, `SLOG2_ERROR`, `SLOG2_CRITICAL`) | `slog2info -b quietward` captured in `qnx8_live_proof_20261125.log` and all 10 `slog2_run_01..10.txt` | S05 |
| `qw_ring.h`, `qw_ring.c` | C11 `_Atomic` seqlock ring over `/qw_ring_bed%u` shared memory (`qw_ring_init`, `qw_ring_write`, `qw_ring_read_tail`; mapped with `shm_open`/`mmap` on QNX) | Host: 10000 frames, 4 readers, `torn=0`; QNX: shared sample rings active across 10/10 soak runs | S02, S05 |
| `qw_ipc.h` | `QW_DCMD_BED_STATE`, `QW_PULSE_*`, `QW_MSG_DECISION`, `qw_ring_hdr_t` (`_Atomic` ring header) | Compiled & verified across all 6 QNX 8.0 processes and host `qw_gateway_test` | S02, S05 |
| `qw_gateway_state.h`, `qw_gateway_state.c`, `qw_gateway_test.c` | portable C11 gateway state machine, `O_NOFOLLOW` journal, `529 checks, 0 failed` on host | `qw_gateway_test: 529 checks, 0 failed` (host); linked into `qw_acq`, `qw_alarm_mgr`, `qw_failopen` on QNX 8.0 | S02, S05 |
| `qw_resmgr.h`, `qw_log.h` | C11 interfaces for `/dev/quietward` JSONL serializer and `slog2` audit formatter | Verified on host (`qw_gateway_test`) and on QNX 8.0 target (`qnx8_live_proof_20261125.log`) | S02, S05 |
| `../../scripts/p1_hello_ipc.c` | `ChannelCreate`, `ConnectAttach`, `MsgSend`, `MsgReceive`, `MsgReply`, `TimerTimeout`, `slog2_register`, `slog2f`, `clock_gettime` | `P1_HELLO_RESULT=PASS` + `slog2info -b qw_hello`: `p1_hello_ipc: slog2 ok` (`p1_qnx8_guest_20261125.log`) | S05 |
| `../../scripts/p1_qplms_probe.sh` | `plms` / `plmsctrl` / `slog2info -b plms` availability probe | `PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK` (`p1_qnx8_guest_20261125.log`) | S02, S07 |
| `../../scripts/p1_tcg_jitter.c` | `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_REALTIME)`, `nanosleep` QEMU TCG jitter calibrator | `jitter_ms p50=0.789 p95=1.539 p99=2.613 max=3.630`, `recommended --watchdog-ms 200` (`p1_qnx8_guest_20261125.log`) | S05, S07 |
| `../../Makefile` | `qnx-check`, `qnx`, `qnx-build`, `qnx-clean`: `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror`, `-lm -lslog2` | Built all 6 `build/qnx/*` ELFs with `0` warnings (`p1_qnx8_guest_20261125.log`) | S07 |
| `../../scripts/p3_qnx_soak.sh` | `pidin`, `slay`, `/dev/quietward`, `qw_bedctl`, `slog2info` (10 runs) | `soak: 10 runs, 0 failed runs` (`submission/g3/g3_run_01..10.txt`) | S05 |
| `../../.devcontainer/devcontainer.json` | Ubuntu 24.04 + `qemu-system-x86` + `build-essential` + `sshd` bring-up container | Executed in x86_64 GitHub Codespace for QNX 8.0 SDP build & QEMU run | S07 |

Build and run steps: top-level `README.md`, section "QNX 8.0 Target, Toolchain & Build/Run Instructions".

## Planned mapping

This document describes how the host-side C11 core (`src/qw_core.c`,
`src/rules.c`, `src/qw_cli.c`, `src/qw_detector_config.c`) maps onto QNX
Neutrino 8.0 primitives for an in-ward review & escalation gateway.
Per project rules, QNX-only code lives behind `#ifdef __QNX__` inside
`src/qnx/`, so `src/qw_core.c` keeps building unmodified on macOS for host
evaluation (`make`, `make test`, `make eval-*`). Simulated/QEMU runs replay
the committed de-identified PhysioNet/CinC Challenge 2015 `train`-split
records under `data/demo/` (ODC-BY 1.0, `10.13026/c9fg-a467`).

No performance numbers appear in this document. QEMU timing is logical
time (under TCG), not hardware real-time, and no accuracy or runtime
number here is a claim until measured on a booted QNX 8.0 target and
backed by `eval/results/`.

## Why QNX

The core (`qw_core.c`) is plain C11 + POSIX with no heap allocation in the
per-decision path (`qw_decide()` uses caller-owned `qw_decide_workspace_t`),
and zero internal static state (verified reentrant across threads). Quiet
Ward acts as a secondary review & escalation gateway that never silently
drops an alarm: the raw bedside alarm stays visible, candidate non-lethal
false alarms are deferred to `/dev/quietward`, and any fault in the detector,
shared memory ring, supervisor, or alarm manager fails open to `CONFIRM`.

## Process / thread layout

- **Acquisition thread** — one per monitored bed, reading the bedside
  monitor's data feed (serial/Ethernet depending on monitor vendor) and
  writing samples into a lock-free or mutex-protected ring buffer sized
  for `suppression.window_s` seconds of history per channel (today 16s at
  250Hz, read from `rules/alarm_rules.json`, not hard-coded). This thread
  runs at an **elevated, fixed priority** under QNX's priority-driven
  preemptive scheduler (`SCHED_FIFO`), since missing samples is worse than
  a late detector cycle. It arms a QNX **timer pulse**
  (`timer_create()` + `SIGEV_PULSE`, delivered via
  `MsgReceivePulse()`/`MsgReceive()` on a channel created with
  `ChannelCreate()`) at the monitor's native sample or frame interval so
  the acquisition loop is woken deterministically rather than polling.

- **Detector thread/process** — owns a `qw_record_t`-equivalent rolling
  window and calls the existing `qw_detect_beats_ecg()`,
  `qw_detect_pulses()`, `qw_signal_quality()`, and `qw_decide()` exactly as
  they exist today (no `__QNX__`-specific changes expected inside
  `qw_core.c` itself — it is already generic C11). It runs at a priority
  below acquisition but above general housekeeping, and is woken either by
  the acquisition thread posting a pulse once a window is full, or by its
  own timer pulse at the suppression-window cadence.

- **Alarm manager process** — receives decisions from the detector and is
  the single place that is allowed to actually sound/silence a bedside or
  central-station alarm. Keeping it a separate process (not a thread in
  the detector) means a crash or bad state in signal processing cannot
  corrupt or bypass the part of the system that must *never silently drop
  a true alarm*.

## IPC: detector → alarm manager

The detector sends each `qw_decision_result_t`-equivalent payload
(`record`/bed id, alarm type, decision, reason string, sqi) to the alarm
manager over native QNX IPC: `MsgSend()` from the detector, with the alarm
manager blocked in `MsgReceive()` on its own channel and replying with
`MsgReply()` once the decision is durably logged and, if `CONFIRM`, the
physical alarm is latched. This is a synchronous call on purpose: the
detector must not consider a `SUPPRESS_TO_REVIEW` or `CONFIRM` decision
"delivered" until the alarm manager has acknowledged it, so a decision is
never lost between the two without either side noticing. For `CONFIRM`,
the alarm manager owns sounding the physical/central alarm; for
`SUPPRESS_TO_REVIEW`, it owns writing the reason to the nurse-facing quiet
review queue — per project rules, this path never discards an alarm
silently, so the write to the review queue and the human acknowledgement
of it are both mandatory, logged steps, never a bare suppression.

## Resource manager: `/dev/quietward`

A QNX **resource manager** (`resmgr_attach()` with a device name of
`/dev/quietward`) exposes the alarm manager's current state and decision
log to the rest of the system (nurse-station UI process, central
monitoring, audit tooling) as ordinary POSIX file operations:

- `open()`/`read()` on `/dev/quietward` streams recent decisions (the same
  `{record, alarm, decision, reason, sqi}` shape the host CLI already
  prints as one JSON line per decision today).
- `write()` is how a nurse's acknowledgement of a suppressed alarm is
  recorded back into the system (the "human hold" the project rules
  require), rather than acknowledgement being an in-memory-only UI state.
- `devctl()` carries out-of-band queries (e.g. "what is bed N's current
  suppression state") without disturbing the read stream.

This keeps the alarm manager's internal IPC private while giving every
other process on the box a normal file-like interface, which is the
idiomatic QNX way to expose a service without every client needing to
know the detector's native message format.

## Logging: `slog2_register()` + `slog2f()`

All decisions (`CONFIRM` and `SUPPRESS_TO_REVIEW`, each with its reason
string), deadline fail-open latches, and load/parse failures are logged via
`slog2_register()` (registering a `slog2_buffer_set_config_t` buffer set
with `<sys/slog2.h>` and `-l slog2` before any log call to avoid `EPERM`)
and `slog2f()` (`SLOG2_INFO` for routine decisions, `SLOG2_WARNING` for
signal-quality fallbacks, `SLOG2_CRITICAL` / `SLOG2_ERROR` for fail-open
latches and load failures; max 512 bytes per formatted log entry). Logs
are inspected with `slog2info` and remain additive to the `/dev/quietward`
operational record.

## Fault recovery & supervision: `qw_supervisor` (after `QPLMS` probe)

We checked the target image for **QNX Process Launch and Monitor Service (`QPLMS`: `plms`, `plmsctrl`)** via `scripts/p1_qplms_probe.sh`: `plms` and `plmsctrl` are absent (`PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK` in `submission/g3/p1_qnx8_guest_20261125.log`), so `qw_supervisor` (`posix_spawn()` + `waitpid()`) provides the honestly labelled process supervision.

- **Acquisition (`qw_acq`)** feeds a per-bed seqlock shared-memory ring and
  raises `QW_PULSE_ALARM_RAISED` directly to both `qw_detector` and
  `qw_alarm_mgr` so `qw_acq` never blocks on analysis.
- **Alarm manager (`qw_alarm_mgr`)** enforces a parameterized decision
  deadline (`--watchdog-ms`, 500 ms default; `p1_tcg_jitter` measured `p50=0.789 ms`, `recommended --watchdog-ms 200` on QEMU TCG) and latches
  `CONFIRM` automatically on timeout or pending-table overflow. Fail-open
  (`qw_failopen`, standalone fail-open binary invoked by `qw_supervisor` after >3 restarts or manually; writes `CONFIRM_ALL` and logs `CRIT`) is decoupled from process supervision so alarm
  preservation does not depend on any supervisor staying up.
- **QNX 8.0 target verification:** Verified end-to-end across 10 soak runs (`scripts/p3_qnx_soak.sh -n 10`, `soak: 10 runs, 0 failed runs`, 230/230 checks) and `submission/g3/qnx8_live_proof_20261125.log`.

## What does NOT change

- `qw_core.c`, `rules.c`, and the detection/decision algorithms are
  intended to be used as-is; they are already plain C11 + POSIX with no
  QNX-specific API calls and no per-window heap allocation, which is the
  property that makes this port mostly an integration/IPC exercise rather
  than a rewrite.
- All clinical thresholds keep coming from `rules/alarm_rules.json` at
  runtime on QNX exactly as on the host; nothing above hard-codes a
  threshold, and this document does not propose changing that.
- Any code written to realize this plan must live under `#ifdef __QNX__`
  inside `src/qnx/`, so the host build (`make`, `make test`, `make eval`)
  continues to work unmodified on macOS.
