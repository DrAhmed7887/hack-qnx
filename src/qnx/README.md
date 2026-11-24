# Quiet Ward on QNX 8.0 — mapping and status

## Status (24 Nov 2026)

Host-verified (portable C11, builds on macOS and Linux, no QNX headers):

- `qw_ipc.h` (message, pulse and shared-memory layouts)
- `qw_ring.{h,c}` (per-bed seqlock ring)
- `qw_gateway_state.{h,c}` (pending/review state machine, deadline, journal replay)
- `qw_log.{h,c}` (audit line formatter, 511-byte slog2 limit; slog2 calls only behind `__QNX__`)
- `qw_resmgr.{h,c}` (`/dev/quietward` JSONL log serializer; the resource manager itself only behind `__QNX__`)
- `qw_gateway_test.c`: `make gateway-test` -> `qw_gateway_test: 529 checks, 0 failed`

Written, **not compiled with `qcc` and not run on QEMU** (the `#ifdef __QNX__` bodies are unverified; on other hosts they build to a stub):

- `qw_acq.c`, `qw_detector.c`, `qw_alarm_mgr.c`, `qw_failopen.c`, `qw_supervisor.c`, `qw_bedctl.c`

Build entry points: `make qnx` (cross-build with `qcc`) and `make qnx-check` (is `qcc` available). Both wait on P1, the myQNX SDP 8.0 licence. The sections below describe the intended mapping; every QNX primitive named there is a plan until it has been built and booted on 8.0.

## Where QNX 8.0 Appears (Reviewer Map)

Same table as the top-level `README.md`. Every primitive below is present in the committed source in this directory; every target output is unbooted.

| File | QNX 8.0 / POSIX / C11 primitives & tooling present in source | Expected target output | Scene |
|---|---|---|---|
| `qw_acq.c` | `shm_open`, `mmap`, `ChannelCreate`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceivePulse`, `name_open`, `MsgSendPulse`, `name_close` | [PENDING P1: replay .qw ring + QW_PULSE_ALARM_RAISED pulse, visible in pidin] | S02, S05 |
| `qw_detector.c` | `name_attach`, `name_open`, `name_close`, `shm_open`, `mmap`, `MsgReceive`, `MsgReply`, `MsgError`, `TimerTimeout`, `MsgSend`, `ConnectDetach` | [PENDING P1: reads seqlock ring, sends QW_MSG_DECISION via MsgSend, restarted/killed in soak] | S02, S05 |
| `qw_alarm_mgr.c` | `name_attach`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceive`, `MsgReply`, `MsgError` (`sigval.sival_ptr`->`rcvid_t` cast to confirm under `qcc` at P1), `pthread_create`, `pthread_detach` | [PENDING P1: 50 ms watchdog tick, deadline latch CONFIRM, journal + /dev/quietward thread, visible in pidin] | S02, S05 |
| `qw_failopen.c` | standalone fail-open binary (invoked by `qw_supervisor` after >3 restarts or manually; needs no running supervisor/IPC to write `CONFIRM_ALL`): `open` with `O_NOFOLLOW`, `write` of `CONFIRM_ALL`, `qw_log_emit` at CRIT | [PENDING P1: writes CONFIRM_ALL to /var/run/quietward/alarm_out + CRIT in slog2info -b quietward] | S02, S05 |
| `qw_supervisor.c` | `posix_spawn`, `waitpid`, `kill`, `sigaction` | [PENDING P1: spawns/restarts qw_alarm_mgr and qw_detector, invokes qw_failopen after >3 restarts, visible in pidin] | S02, S05 |
| `qw_bedctl.c` | `open("/dev/quietward")`, `devctl(QW_DCMD_BED_STATE)` | [PENDING P1: prints per-bed JSON including failopen_active via devctl] | S05 |
| `qw_resmgr.c` | `dispatch_create`, `iofunc_func_init`, `iofunc_attr_init`, `resmgr_attach("/dev/quietward")`, `dispatch_context_alloc`, `dispatch_block`, `dispatch_handler`, `iofunc_read_verify`, `iofunc_write_verify`, `iofunc_devctl_default`, `resmgr_msgget`, `_IO_READ_GET_NBYTES`, `_IO_WRITE_GET_NBYTES`, `_IO_SET_READ_NBYTES`, `_IO_SET_WRITE_NBYTES`, `MsgReply` | [PENDING P1: cat /dev/quietward JSONL stream, write() nurse ACK] | S02, S05 |
| `qw_log.c` | `slog2_register`, `slog2f` (`SLOG2_INFO`, `SLOG2_WARNING`, `SLOG2_ERROR`, `SLOG2_CRITICAL`) | [PENDING P1: slog2info -b quietward audit lines] | S05 |
| `qw_ring.h`, `qw_ring.c` | C11 `_Atomic` seqlock ring over `/qw_ring_bed%u` shared memory (`qw_ring_init`, `qw_ring_write`, `qw_ring_read_tail`; mapped with `shm_open`/`mmap` on QNX) | [PENDING P1: lock-free sample ring between qw_acq and qw_detector over /dev/shmem/qw_ring_bed*] | S02, S05 |
| `qw_ipc.h` | `QW_DCMD_BED_STATE`, `QW_PULSE_*`, `QW_MSG_DECISION`, `qw_ring_hdr_t` (`_Atomic` ring header) | [PENDING P1: shared IPC contract across all QNX processes] | S02, S05 |
| `qw_gateway_state.h`, `qw_gateway_state.c`, `qw_gateway_test.c` | portable C11 gateway state machine, `O_NOFOLLOW` journal, `529 checks, 0 failed` on host | `qw_gateway_test: 529 checks, 0 failed` (host); [PENDING P1: linked into qw_alarm_mgr, qw_acq, qw_failopen] | S02, S05 |
| `qw_resmgr.h`, `qw_log.h` | C11 interfaces for `/dev/quietward` JSONL serializer and `slog2` audit formatter | host-verified in `qw_gateway_test`; [PENDING P1: /dev/quietward + slog2f on target] | S02, S05 |
| `../../scripts/p1_hello_ipc.c` | `ChannelCreate`, `ConnectAttach`, `MsgSend`, `MsgReceive`, `MsgReply`, `TimerTimeout`, `slog2_register`, `slog2f`, `clock_gettime` | [PENDING P1: P1_HELLO_RESULT=PASS + slog2info -b qw_hello] | S05 |
| `../../scripts/p1_qplms_probe.sh` | `plms` / `plmsctrl` / `slog2info -b plms` availability probe | [PENDING P1: PROBE_RESULT=QPLMS_PRESENT or USE_QW_SUPERVISOR_FALLBACK] | S02, S07 |
| `../../scripts/p1_tcg_jitter.c` | `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_REALTIME)`, `nanosleep` QEMU TCG jitter calibrator | [PENDING P1: recommended --watchdog-ms] | S05, S07 |
| `../../Makefile` | `qnx-check`, `qnx`, `qnx-build`, `qnx-clean`: `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror`, `-lm -lslog2` | [PENDING P1: build/qnx/{qw_acq,qw_detector,qw_alarm_mgr,qw_failopen,qw_supervisor,qw_bedctl}] | S07 |
| `../../scripts/p3_qnx_soak.sh` | `pidin`, `slay`, `/dev/quietward`, `qw_bedctl`, `slog2info` (10 runs) | [PENDING P1: submission/g3/g3_run_01..10.txt] | S05 |
| `../../.devcontainer/devcontainer.json` | Ubuntu 24.04 + `qemu-system-x86` + `build-essential` bring-up container | [PENDING P1: devcontainer config written, not yet built or run] | S07 |

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

## Fault recovery & supervision: `QPLMS` (with `qw_supervisor` fallback)

We plan to use **QNX Process Launch and Monitor Service (`QPLMS`: `plms`, dev utility `plmsctrl`)** declaratively via a component DAG config file (`depend`, `task "recovery"`, `task "monitor"`, `on_fault`) instead of `HAM` (unverified on our target image until P1; all `QPLMS`, `aps`, priority `1-253`, and `cluster` details remain to be verified against SDP 8.0 headers/image at P1).

- **Acquisition (`qw_acq`)** feeds a per-bed seqlock shared-memory ring and
  raises `QW_PULSE_ALARM_RAISED` directly to both `qw_detector` and
  `qw_alarm_mgr` so `qw_acq` never blocks on analysis.
- **Alarm manager (`qw_alarm_mgr`)** enforces a parameterized decision
  deadline (`--watchdog-ms`, calibrated from QEMU TCG jitter) and latches
  `CONFIRM` automatically on timeout or pending-table overflow. Fail-open
  (`qw_failopen`, a standalone fail-open binary invoked by `qw_supervisor` after >3 restarts or manually; needs no running supervisor/IPC to write `CONFIRM_ALL`) so alarm
  preservation does not depend on QPLMS staying up.
- **P1 QEMU target verification & fallback:** At QNX 8.0 target bring-up, QPLMS
  availability in the target image is checked within a 45-minute cap. If
  `plms` is absent or unsupported in the QNX 8.0 QEMU image, a ~40-line parent
  `qw_supervisor` (`posix_spawn()` + `waitpid()` / death pulse) provides
  honestly labelled process supervision instead of claiming QPLMS.

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
