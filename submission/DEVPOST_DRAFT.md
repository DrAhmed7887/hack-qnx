# Quiet Ward 2.0: Devpost submission draft

> Research demo, not a medical device. The alarm rules are `pending_signoff`: engineering defaults, not signed clinical rules. Nothing below is a clinical claim. Draft status: no QNX runtime result exists yet; every place where one belongs is marked `[PENDING P1: ...]`.

Category: Robotics & Medical. Bonus: Best use of QNX-specific Technologies.

## Elevator Pitch

A second opinion on bedside alarms that can never silence the first. Quiet Ward checks the signals behind an arrhythmia alarm and, when a second channel contradicts it, queues the alarm for a nurse to review instead of treating it as urgent. The bedside alarm is never removed, and every fault fails open to CONFIRM.

## Inspiration

Any bedside alarm system has to answer one question first: what happens when something goes wrong? In clinical monitoring, silent alarm suppression can hide true patient deterioration behind software faults or dropped messages. Our answer is an invariant: an alarm is never silently dropped.

## What it does

- Reads a real ICU record (ECG plus a pulsatile channel such as ABP or PLETH) and the alarm type that fired.
- Decides **CONFIRM** or **SUPPRESS_TO_REVIEW**. A candidate suppression is not discarded: it goes to a review queue on `/dev/quietward` for a nurse ACK, with timeout escalation, and the reason is logged.
- Never suppresses on the never-suppress list (ventricular flutter/fibrillation) and does not attempt suppression when the pulse channel quality is too low.
- Fails open to CONFIRM on: detector crash, decision deadline, a 17th pending alarm, detached shared memory, backward clock step, and restart (journal replay).

On six real records (a demonstration, not an accuracy measurement) five are confirmed and one false asystole alarm (a109l) goes to review; no true alarm is queued for review. Record t116s is a false alarm that is still confirmed: the intended failure direction.

![Real PhysioNet 2015 demo waveforms](../docs/plots/demo_overview.svg)

## How we built it

**Verified on the host (portable C11, logged in `submission/g3/host_tests_20261124.log`):**
- The detection and rule core (`qw_core`, `rules`, detector config loader), reentrant.
- The IPC building blocks as plain C11: the per-bed seqlock ring, the gateway state machine (pending table, review queue, deadline, nurse ACK validation, restart journal), the audit-line formatter and the JSONL log serializer.
- `make gateway-test`: `qw_gateway_test: 529 checks, 0 failed`; `make reentrant-test`: `320` concurrent `qw_decide()` calls across `8` threads; `make fuzz`: `1000/1000` seeded mutations passed.

**Written but not yet compiled or run on QNX:** the six `#ifdef __QNX__` process sources under `src/qnx/` (acquisition, detector, alarm manager, fail-open helper, supervisor, bed control) and the `make qnx` cross-build. They use QNX message passing and pulses, a resource manager at `/dev/quietward`, and `slog2` (`QPLMS` planned at P1, with `qw_supervisor` as fallback). None of this has been built with `qcc` or booted: it waits on the SDP 8.0 licence (P1).

`[PENDING P1: replace with measured QEMU pidin/slog2info capture after SDP 8.0 boot]`

### Where QNX 8.0 Appears (Reviewer Map)

Every primitive below is present in the committed source. Nothing below has been compiled with `qcc` or booted; video scenes S02, S05 and S07 are in `video/SCRIPT_V2_DRAFT.md`.

| File | QNX 8.0 / POSIX / C11 primitives & tooling present in source | Expected target output | Scene |
|---|---|---|---|
| `src/qnx/qw_acq.c` | `shm_open`, `mmap`, `ChannelCreate`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceivePulse`, `name_open`, `MsgSendPulse`, `name_close` | [PENDING P1: replay .qw ring + QW_PULSE_ALARM_RAISED pulse, visible in pidin] | S02, S05 |
| `src/qnx/qw_detector.c` | `name_attach`, `name_open`, `name_close`, `shm_open`, `mmap`, `MsgReceive`, `MsgReply`, `MsgError`, `TimerTimeout`, `MsgSend`, `ConnectDetach` | [PENDING P1: reads seqlock ring, sends QW_MSG_DECISION via MsgSend, restarted/killed in soak] | S02, S05 |
| `src/qnx/qw_alarm_mgr.c` | `name_attach`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceive`, `MsgReply`, `MsgError`, `pthread_create`, `pthread_detach` | [PENDING P1: 50 ms watchdog tick, deadline latch CONFIRM, journal + /dev/quietward thread, visible in pidin] | S02, S05 |
| `src/qnx/qw_failopen.c` | decoupled fail-open helper: `open` with `O_NOFOLLOW`, `write` of `CONFIRM_ALL`, `qw_log_emit` at CRIT | [PENDING P1: writes CONFIRM_ALL to /var/run/quietward/alarm_out + CRIT in slog2info -b quietward] | S02, S05 |
| `src/qnx/qw_supervisor.c` | `posix_spawn`, `waitpid`, `kill`, `sigaction` | [PENDING P1: spawns/restarts qw_alarm_mgr and qw_detector, invokes qw_failopen after >3 restarts, visible in pidin] | S02, S05 |
| `src/qnx/qw_bedctl.c` | `open("/dev/quietward")`, `devctl(QW_DCMD_BED_STATE)` | [PENDING P1: prints per-bed JSON including failopen_active via devctl] | S05 |
| `src/qnx/qw_resmgr.c` | `dispatch_create`, `iofunc_func_init`, `iofunc_attr_init`, `resmgr_attach("/dev/quietward")`, `dispatch_context_alloc`, `dispatch_block`, `dispatch_handler`, `iofunc_read_verify`, `iofunc_write_verify`, `iofunc_devctl_default`, `resmgr_msgget`, `_IO_READ_GET_NBYTES`, `_IO_WRITE_GET_NBYTES`, `_IO_SET_READ_NBYTES`, `_IO_SET_WRITE_NBYTES`, `MsgReply` | [PENDING P1: cat /dev/quietward JSONL stream, write() nurse ACK] | S02, S05 |
| `src/qnx/qw_log.c` | `slog2_register`, `slog2f` (`SLOG2_INFO`, `SLOG2_WARNING`, `SLOG2_ERROR`, `SLOG2_CRITICAL`) | [PENDING P1: slog2info -b quietward audit lines] | S05 |
| `src/qnx/qw_ring.h`, `src/qnx/qw_ring.c` | C11 `_Atomic` seqlock ring over `/qw_ring_bed%u` shared memory (`qw_ring_init`, `qw_ring_write`, `qw_ring_read_tail`; mapped with `shm_open`/`mmap` on QNX) | [PENDING P1: lock-free sample ring between qw_acq and qw_detector over /dev/shmem/qw_ring_bed*] | S02, S05 |
| `src/qnx/qw_ipc.h` | `QW_DCMD_BED_STATE`, `QW_PULSE_*`, `QW_MSG_DECISION`, `qw_ring_hdr_t` (`_Atomic` ring header) | [PENDING P1: shared IPC contract across all QNX processes] | S02, S05 |
| `src/qnx/qw_gateway_state.h`, `src/qnx/qw_gateway_state.c`, `src/qnx/qw_gateway_test.c` | portable C11 gateway state machine, `O_NOFOLLOW` journal, `529 checks, 0 failed` on host | `qw_gateway_test: 529 checks, 0 failed` (host); [PENDING P1: linked into qw_alarm_mgr, qw_acq, qw_failopen] | S02, S05 |
| `src/qnx/qw_resmgr.h`, `src/qnx/qw_log.h` | C11 interfaces for `/dev/quietward` JSONL serializer and `slog2` audit formatter | host-verified in `qw_gateway_test`; [PENDING P1: /dev/quietward + slog2f on target] | S02, S05 |
| `scripts/p1_hello_ipc.c` | `ChannelCreate`, `ConnectAttach`, `MsgSend`, `MsgReceive`, `MsgReply`, `TimerTimeout`, `slog2_register`, `slog2f`, `clock_gettime` | [PENDING P1: P1_HELLO_RESULT=PASS + slog2info -b qw_hello] | S05 |
| `scripts/p1_qplms_probe.sh` | `plms` / `plmsctrl` / `slog2info -b plms` availability probe | [PENDING P1: PROBE_RESULT=QPLMS_PRESENT or USE_QW_SUPERVISOR_FALLBACK] | S02, S07 |
| `scripts/p1_tcg_jitter.c` | `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_REALTIME)`, `nanosleep` QEMU TCG jitter calibrator | [PENDING P1: recommended --watchdog-ms] | S05, S07 |
| `Makefile` | `qnx-check`, `qnx`, `qnx-build`, `qnx-clean`: `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror`, `-lm -lslog2` | [PENDING P1: build/qnx/{qw_acq,qw_detector,qw_alarm_mgr,qw_failopen,qw_supervisor,qw_bedctl}] | S07 |
| `scripts/p3_qnx_soak.sh` | 10-run target soak: `pidin`, `slay`, `/dev/quietward`, `qw_bedctl`, `slog2info` | [PENDING P1: submission/g3/g3_run_01..10.txt] | S05 |
| `.devcontainer/devcontainer.json` | Ubuntu 24.04 Linux + `qemu-system-x86` + `build-essential` bring-up container for QNX SDP 8.0 | [PENDING P1: devcontainer config written, not yet built or run] | S07 |

### QNX 8.0 target, toolchain and build/run (full steps in README.md)

- **QNX version:** QNX SDP 8.0 / QNX Neutrino RTOS 8.0. **Target:** QNX 8.0 x86_64 QEMU image (built from SDP 8.0) plus host-portable C11. Host requirement: GitHub Codespaces (x86_64) or an x86_64 Linux/Windows host; a local arm64 devcontainer on Apple Silicon will not run SDP 8.0 x86_64 host tools natively.
- **Toolchain:** `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror`, `-lm -lslog2`, GNU Make, `source ~/qnx800/qnxsdp-env.sh` on x86_64 Linux, `.devcontainer/devcontainer.json`.
- **Cross-build** [PENDING P1: not yet run]: `source ~/qnx800/qnxsdp-env.sh`, `make qnx-check`, `make qnx`.
- **QEMU image boot & `scp` copy** [PENDING P1: not yet run]: build and boot the QNX 8.0 x86_64 QEMU image from SDP 8.0 (e.g., `mkqnximage` / QEMU boot under `.devcontainer/devcontainer.json`; exact SDP 8.0 image-builder flags to be verified at P1), then copy `build/qnx/*`, `rules/alarm_rules.json`, `config/detector.json`, `data/demo/*.qw`, and `scripts/p3_qnx_soak.sh` via `scp` to `/opt/quietward/{bin,rules,config,data/demo,scripts}`.
- **Target run** [PENDING P1: no output captured yet]: `mkdir -p /var/run/quietward && chmod 0700 /var/run/quietward`, `qw_supervisor`, `qw_acq`, `cat /dev/quietward | head -n 1`, `qw_bedctl`, `slog2info -b quietward`, `sh /opt/quietward/scripts/p3_qnx_soak.sh -o /opt/quietward/out` (then copy `/opt/quietward/out/g3_run_01..10.txt` and `slog2_run_01..10.txt` back to `submission/g3/` via `scp` or QEMU shared directory).
- **Offline host build & checks (fresh export, no `.git`/`.venv`; offline build; no network access required, no pip):** `make`, `make gateway-test`, `make reentrant-test`, `make demo-verify`, `make fuzz` (runs `scripts/fuzz_qw.py --count 1000` via `.venv/bin/python` when present, else `python3`), `python3 scripts/host_shift_replay.py`, `python3 scripts/render_demo_plots.py`. Additional host suites: `make test` additionally requires `pytest` and `numpy`; `scripts/fetch_physionet.sh` and full 750-record `make eval` additionally require `wfdb` and `numpy`.

## Challenges we ran into

- **Our own Kill Rule failed on held-out data.** Asystole false negatives moved from 0 to 1 on only 37 Asystole records. We did not retune on the held-out set. We ship the honest framing (Variant B): a secondary review and escalation gateway, not a validated suppressor.
- **QNX 8.0 has no macOS or ARM host, and HAM is gone.** The SDK runs on x86_64 Linux, so the QNX build and QEMU boot need a Linux host (a free Codespace or similar) once the licence is in. High Availability Manager is deprecated in 8.0; the replacement is QPLMS, which we plan to check in the target image and fall back from honestly.
- **Clinical rules need a physician.** The thresholds stay `pending_signoff`, and clinical literature claims are held back until signed.

## Accomplishments that we're proud of

Source: real PhysioNet/CinC 2015 training records, local split, Challenge score in percent. "Base" confirms every alarm.

| Train (N=523) | Score | FN |
|---|---|---|
| Base | 39.2% | 0 |
| v0 (frozen) | 40.2% | 49 |
| fixed | 53.8% | 3 |

Fixed true-alarm recall 98.5% (202 of 205), Wilson 95% CI [95.8-99.5]. Kill Rule: PASS.

| Held-out (N=227) | Score | FN |
|---|---|---|
| Base | 39.2% | 0 |
| v0 (frozen) | 40.8% | 18 |
| fixed | 51.5% | 3 |

Fixed true-alarm recall 96.6% (86 of 89), Wilson 95% CI [90.6-98.8]. Kill Rule: **FAIL** (Asystole 0 to 1).

The PhysioNet/CinC 2015 winners scored 81.39 and 85.04 on a different 500-record hidden test set; our numbers come from a local split and are not comparable.

Also: 529 host gateway checks, 0 failed; 320 concurrent reentrant calls across 8 threads; fuzzer 1000/1000; zero synthetic waveforms; zero paid APIs (`submission/g3/stress-checklist.md` row 14); the host build and offline checks run from a fresh export (no `.git`/`.venv`), offline build; no network access required (`submission/g3/host_tests_20261124.log`).

## What's next

1. P1: claim the myQNX SDP 8.0 licence and bring up the target on an x86_64 Linux host.
2. `make qnx`: compile the six process sources with `qcc` and fix what the compiler finds.
3. A 10-run QEMU G3 soak (kill the detector, kill acquisition, restart the alarm manager) with real `pidin` and `slog2info` captures. QEMU runs under TCG, so timings are logical time, not hardware real time.
4. A video under 3 minutes (storyboard in `video/SCRIPT_V2_DRAFT.md`).

`[PENDING P1: replace with measured QEMU pidin/slog2info capture after SDP 8.0 boot]`

## Built With

C11, POSIX, QNX Neutrino 8.0 (message passing, pulses, resource manager, slog2; QPLMS and QEMU target planned, not yet built), Python (stdlib for demo/fuzzer/plots; pytest + numpy + wfdb for full test/eval), GNU Make, SVG, PhysioNet/CinC 2015 data (ODC-BY 1.0).

## Repository, licence and submission period

`DrAhmed7887/hack-qnx` is a new, non-fork repository (pending owner click); first commit `d1d0bd6` on 22 Nov 2026 (`submission/g3/git_birth_commit.txt`; GitHub creation date / submission-period check pending owner click). Code: Apache License 2.0 (`LICENSE` present; GitHub About detection pending owner click). Data: ODC-BY 1.0 attribution (PhysioNet/CinC Challenge 2015 dataset, DOI 10.13026/c9fg-a467; `NOTICE`, `DATA_CARD.md`, `data/demo/LICENSE-ODC-BY.txt`). Claim-to-file map: `CLAIMS.md`. Rules compliance: `submission/COMPLIANCE_CHECKLIST.md`.

## Data and citations

- Data: PhysioNet/Computing in Cardiology Challenge 2015 dataset, real de-identified ICU records, ODC-BY 1.0, DOI 10.13026/c9fg-a467. Paper: Clifford GD, Silva I, Moody B, Li Q, Kella D, Shahin A, Kooistra T, Perry D, Mark RG (2015). The PhysioNet/Computing in Cardiology Challenge 2015: Reducing False Arrhythmia Alarms in the ICU. Computing in Cardiology 42:273-276.
- openFDA MAUDE report 10055677: `[PENDING RE-FETCH into docs/EVIDENCE.md — see CLAIMS.md row 18]`.
- Repo documents: README, JUDGES.md, DATA_CARD.md, `submission/g3/stress-checklist.md`.
