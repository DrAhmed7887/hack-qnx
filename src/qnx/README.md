# Quiet Ward on QNX 8.0 — planned mapping (design doc, no QNX code yet)

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

High Availability Manager (`HAM`) is deprecated in QNX 8.0; the QNX 8.0
replacement is **QNX Process Launch and Monitor Service (`QPLMS`: `plms`,
`libplms`, dev utility `plmsctrl`)**, configured declaratively via a
component DAG config file (`depend`, `task "recovery"`, `task "monitor"`,
`on_fault`) rather than a C registration API (`research/QPLMS_NOTES.md`).
SDP 8.x removes `aps`, enforces priorities `1-253`, and replaces `runmask`
with `cluster`.

- **Acquisition (`qw_acq`)** feeds a per-bed seqlock shared-memory ring and
  raises `QW_PULSE_ALARM_RAISED` directly to both `qw_detector` and
  `qw_alarm_mgr` so `qw_acq` never blocks on analysis.
- **Alarm manager (`qw_alarm_mgr`)** enforces a parameterized decision
  deadline (`--watchdog-ms`, calibrated from QEMU TCG jitter) and latches
  `CONFIRM` automatically on timeout or pending-table overflow. Fail-open
  (`qw_failopen`) is decoupled from process supervision so alarm
  preservation does not depend on QPLMS staying up.
- **P1 QSTI verification & fallback:** At QNX 8.0 target bring-up, QPLMS
  availability in the target image is checked within a 45-minute cap. If
  `plms` is absent or unsupported in the QSTI image, a ~40-line parent
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
