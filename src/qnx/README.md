# Quiet Ward on QNX 8.0 — planned mapping (design doc, no code yet)

This document describes how the host-side v0 core (`src/qw_core.c`,
`src/rules.c`, `src/qw_cli.c`) is planned to map onto QNX Neutrino 8.0
primitives for an in-ward deployment. Nothing in this directory is built
or linked today; `make` only builds the host CLI (`build/qw`) from the
existing `src/*.c` files. Per project rules, any QNX-only code that is
written later must live behind `#ifdef __QNX__` inside `src/qnx/`, so that
`src/qw_core.c` keeps building unmodified on macOS for host evaluation.

No performance numbers appear in this document. Per project rules, QEMU
timing is logical time, not real-time, and no number here is sourced from
`eval/results/`, so none of it is a claim — it is a plan to validate later.

## Why QNX

The core (`qw_core.c`) is already plain C11 + POSIX file I/O with no
allocation in the per-decision path after `qw_record_load()`, which is
the property a hard real-time monitoring core needs. The porting work is
entirely in *how the core is driven* — acquisition, scheduling, IPC, fault
recovery, logging — not in the decision logic itself.

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

## Logging: slog2

All decisions (`CONFIRM` and `SUPPRESS_TO_REVIEW`, each with its reason
string) and all `qw_record_load()`/`qw_rules_load()` failures are logged
through `slog2_add()`/`slogf()` to a dedicated slog2 buffer
(`SLOG2_INFO` for routine `CONFIRM`/`SUPPRESS_TO_REVIEW` decisions,
`SLOG2_WARNING` for signal-quality-driven fallbacks to `CONFIRM`,
`SLOG2_ERROR` for load/parse failures), separate from the general system
log buffer, so the full audit trail of "what was suppressed, when, and
why" survives independently of other system noise and can be pulled with
`slog2info` for incident review. This is additive to whatever the alarm
manager writes to `/dev/quietward`'s decision log, not a replacement — the
resource manager's log is the operational record nurses and audits read;
slog2 is the systems-level diagnostic trail for engineering and incident
response.

## Fault recovery: HAM

The acquisition thread, detector, and alarm manager are registered as
QNX **High Availability Manager (HAM)** entities so a crash in any one of
them triggers an automatic, supervised restart rather than silently
leaving a bed unmonitored:

- The acquisition process is registered with HAM so that if it dies, HAM
  restarts it immediately; because the ring buffer it feeds has a bounded
  window, a brief acquisition gap is visible as "no data" rather than
  stale data, and the detector's own signal-quality check already treats
  insufficient fresh data as low quality (`qw_signal_quality()` already
  returns 0 for a flat/absent channel today), so a restart glitch degrades
  safely toward "confirm the alarm" rather than toward a false suppression.
- The alarm manager is the most safety-critical process and is registered
  with the strictest HAM policy available (fastest detection + restart,
  and if it cannot come back cleanly, HAM's policy should be to fail over
  to sounding every pending alarm rather than staying silent — "fail open"
  for alarms, never "fail closed").
- HAM restart events themselves are logged via slog2 (see above) so a
  crash-and-recover cycle is part of the audit trail, not invisible.

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
