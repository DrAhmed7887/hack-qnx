# Quiet Ward 2.0: Devpost submission draft

> Research demo, not a medical device. The alarm rules are `pending_signoff`: engineering defaults, not signed clinical rules. Nothing below is a clinical claim. Built, cross-compiled (`qcc -Vgcc_ntox86_64 -Wall -Wextra -Werror`, 0 warnings), and verified on booted **QNX Neutrino RTOS 8.0 (`QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64`)** under QEMU TCG (`10/10` target soak runs, `230/230` checks passed, `0` failed; logs under `submission/g3/`).

Category: Robotics & Medical. Bonus: Best use of QNX-specific Technologies.

## Elevator Pitch

A second opinion on bedside alarms that can never silence the first. Quiet Ward checks the signals behind an arrhythmia alarm and, when a second channel contradicts it, queues the alarm for a nurse to review instead of treating it as urgent. The bedside alarm is never removed, and every fault fails open to CONFIRM.

## Inspiration

Any bedside alarm system has to answer one question first: what happens when something goes wrong? Quiet Ward enforces a single architectural design invariant: an alarm is never silently dropped.

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

**Verified on QNX Neutrino RTOS 8.0 (`QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64` on QEMU TCG, logged in `submission/g3/p1_qnx8_guest_20261125.log`, `submission/g3/qnx8_live_proof_20261125.log`, and `submission/g3/g3_run_01..10.txt`):**
- All six `#ifdef __QNX__` process binaries (`qw_acq`, `qw_detector`, `qw_alarm_mgr`, `qw_failopen`, `qw_supervisor`, `qw_bedctl`) plus `p1_hello_ipc` and `p1_tcg_jitter` compiled cleanly with `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror -lm -lslog2` (`0` warnings; `readelf -h` `Advanced Micro Devices X86-64`).
- `p1_hello_ipc` (`P1_HELLO_RESULT=PASS`, `slog2info -b qw_hello`), `p1_qplms_probe.sh` (`PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK`; `plms` absent on the QEMU image, `qw_supervisor` active), and `p1_tcg_jitter` (`p50=0.789 ms`, `recommended --watchdog-ms 200`).
- Live `/dev/quietward` character device (`crw-rw----`): `cat` JSONL stream, nurse ACK `write()` (`"nurse ack: proof-rn"`), `qw_bedctl 0..2` `devctl` JSON, standalone `qw_failopen` (`alarm_out=CONFIRM_ALL` + `sev=CRIT` across beds `0..3`), and **10/10 G3 soak runs (`230/230` checks passed, `0` failed)** including `slay -s KILL qw_detector` fail-open latch + supervisor restart with a new PID (`slog2_run_01..10.txt`).

### Where QNX 8.0 Appears (Reviewer Map)

Every primitive below is compiled and verified on the booted QNX 8.0 QEMU target (`submission/g3/*`). Video scenes S02, S05 and S07 are in `video/SCRIPT_V2_DRAFT.md`.

| File | QNX 8.0 / POSIX / C11 primitives & tooling present in source | Verified QNX 8.0 / Host output | Scene |
|---|---|---|---|
| `src/qnx/qw_acq.c` | `shm_open`, `mmap`, `ChannelCreate`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceivePulse`, `name_open`, `MsgSendPulse`, `name_close` | Compiled (`build/qnx/qw_acq`); replayed `a109l`, `a653l`, `v188s` into `/dev/shmem/qw_ring_bed*` and raised `QW_PULSE_ALARM_RAISED` in 10/10 soak runs (`g3_run_01..10.txt`, `qnx8_live_proof_20261125.log`) | S02, S05 |
| `src/qnx/qw_detector.c` | `name_attach`, `name_open`, `name_close`, `shm_open`, `mmap`, `MsgReceive`, `MsgReply`, `MsgError`, `TimerTimeout`, `MsgSend`, `ConnectDetach` | Compiled (`build/qnx/qw_detector`); visible in `pidin`, sends `QW_MSG_DECISION` via `MsgSend`, killed (`slay -s KILL`) and respawned in 10/10 soak runs | S02, S05 |
| `src/qnx/qw_alarm_mgr.c` | `name_attach`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceive`, `MsgReply`, `MsgError`, `pthread_create`, `pthread_detach` | Compiled (`build/qnx/qw_alarm_mgr`); visible in `pidin` (2 threads), 50 ms watchdog tick latches `CONFIRM` (`sev=CRIT` `no decision within deadline (fail-open)` in `slog2_run_01..10.txt`) | S02, S05 |
| `src/qnx/qw_failopen.c` | standalone fail-open binary (`open` with `O_NOFOLLOW`, `write` of `CONFIRM_ALL`, `qw_log_emit` at CRIT) | Compiled (`build/qnx/qw_failopen`); verified standalone in `qnx8_live_proof_20261125.log` (`alarm_out=CONFIRM_ALL` + 4 `sev=CRIT` lines for beds `0..3`) | S02, S05 |
| `src/qnx/qw_supervisor.c` | `posix_spawn`, `waitpid`, `kill`, `sigaction` | Compiled (`build/qnx/qw_supervisor`); visible in `pidin`, spawns/restarts `qw_alarm_mgr` and `qw_detector` with new PID in 10/10 soak runs | S02, S05 |
| `src/qnx/qw_bedctl.c` | `open("/dev/quietward")`, `devctl(QW_DCMD_BED_STATE)` | Compiled (`build/qnx/qw_bedctl`); prints per-bed JSON including `failopen_active:1`, refuses `qw_bedctl 9` (`qnx8_live_proof_20261125.log`, `g3_run_01..10.txt`) | S05 |
| `src/qnx/qw_resmgr.c` | `dispatch_create`, `iofunc_func_init`, `iofunc_attr_init`, `resmgr_attach("/dev/quietward")`, `dispatch_context_alloc`, `dispatch_block`, `dispatch_handler`, `iofunc_read_verify`, `iofunc_write_verify`, `iofunc_devctl_default`, `resmgr_msgget`, `_IO_READ_GET_NBYTES`, `_IO_WRITE_GET_NBYTES`, `_IO_SET_READ_NBYTES`, `_IO_SET_WRITE_NBYTES`, `MsgReply` | Compiled (`RESMGR_OCB_T qw_ocb_t`, `&ocb->base`); `/dev/quietward` (`crw-rw----`) `cat` JSONL + nurse ACK `write()` verified live; malformed ACK refused in 10/10 runs | S02, S05 |
| `src/qnx/qw_log.c` | `slog2_register`, `slog2f` (`SLOG2_INFO`, `SLOG2_WARNING`, `SLOG2_ERROR`, `SLOG2_CRITICAL`) | `slog2info -b quietward` captured in `qnx8_live_proof_20261125.log` and all 10 `slog2_run_01..10.txt` | S05 |
| `src/qnx/qw_ring.h`, `src/qnx/qw_ring.c` | C11 `_Atomic` seqlock ring over `/qw_ring_bed%u` shared memory (`qw_ring_init`, `qw_ring_write`, `qw_ring_read_tail`; mapped with `shm_open`/`mmap` on QNX) | Host: 10000 frames, 4 readers, `torn=0`; QNX: shared sample rings active across 10/10 runs | S02, S05 |
| `src/qnx/qw_ipc.h` | `QW_DCMD_BED_STATE`, `QW_PULSE_*`, `QW_MSG_DECISION`, `qw_ring_hdr_t` (`_Atomic` ring header) | Verified across all 6 QNX 8.0 processes and host `qw_gateway_test` | S02, S05 |
| `src/qnx/qw_gateway_state.h`, `src/qnx/qw_gateway_state.c`, `src/qnx/qw_gateway_test.c` | portable C11 gateway state machine, `O_NOFOLLOW` journal, `529 checks, 0 failed` on host | `qw_gateway_test: 529 checks, 0 failed` (host); linked into `qw_acq`, `qw_alarm_mgr`, `qw_failopen` on QNX 8.0 | S02, S05 |
| `src/qnx/qw_resmgr.h`, `src/qnx/qw_log.h` | C11 interfaces for `/dev/quietward` JSONL serializer and `slog2` audit formatter | Verified on host (`qw_gateway_test`) and on QNX 8.0 target (`qnx8_live_proof_20261125.log`) | S02, S05 |
| `scripts/p1_hello_ipc.c` | `ChannelCreate`, `ConnectAttach`, `MsgSend`, `MsgReceive`, `MsgReply`, `TimerTimeout`, `slog2_register`, `slog2f`, `clock_gettime` | `P1_HELLO_RESULT=PASS` + `slog2info -b qw_hello` (`p1_qnx8_guest_20261125.log`) | S05 |
| `scripts/p1_qplms_probe.sh` | `plms` / `plmsctrl` / `slog2info -b plms` availability probe | `PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK` (`p1_qnx8_guest_20261125.log`) | S02, S07 |
| `scripts/p1_tcg_jitter.c` | `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_REALTIME)`, `nanosleep` QEMU TCG jitter calibrator | `jitter_ms p50=0.789 p95=1.539 p99=2.613 max=3.630`, `recommended --watchdog-ms 200` (`p1_qnx8_guest_20261125.log`) | S05, S07 |
| `Makefile` | `qnx-check`, `qnx`, `qnx-build`, `qnx-clean`: `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror`, `-lm -lslog2` | Built all 6 `build/qnx/*` ELFs with `0` warnings (`p1_qnx8_guest_20261125.log`) | S07 |
| `scripts/p3_qnx_soak.sh` | 10-run target soak: `pidin`, `slay`, `/dev/quietward`, `qw_bedctl`, `slog2info` | `soak: 10 runs, 0 failed runs` (`submission/g3/g3_run_01..10.txt`) | S05 |
| `.devcontainer/devcontainer.json` | Ubuntu 24.04 Linux + `qemu-system-x86` + `build-essential` + `sshd` bring-up container for QNX SDP 8.0 | Executed on x86_64 GitHub Codespace for QNX 8.0 build and QEMU boot | S07 |

### QNX 8.0 target, toolchain and build/run (full steps in README.md)

- **QNX version:** QNX SDP 8.0 / QNX Neutrino RTOS 8.0 (`QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64`). **Target:** QNX 8.0 x86_64 QEMU image built with `mkqnximage --type=qemu --arch=x86_64 --sys-size=512 --data-size=512 --cpu=2 --ram=2G --ssh-ident=... --noprompt --build=all` and booted under `qemu-system-x86_64 -accel tcg -cpu IvyBridge -smp 2 -m 2048` on an x86_64 GitHub Codespace (`.devcontainer/devcontainer.json`).
- **Toolchain:** `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror`, `-lm -lslog2`, GNU Make, `source ~/qnx800/qnxsdp-env.sh`.
- **Cross-build (verified):** `source ~/qnx800/qnxsdp-env.sh && make qnx-check && make qnx` (`0` warnings across all 6 binaries).
- **Target deploy & soak (verified):** Copy `build/qnx/*`, `rules/alarm_rules.json`, `config/detector.json`, `data/demo/*`, and `scripts/p3_qnx_soak.sh` to `/data/quietward/{bin,rules,config,data/demo,scripts}`, create `/var/run/quietward` (`0700`), run `qw_supervisor`, `qw_acq`, `cat /dev/quietward`, `qw_bedctl`, `slog2info -b quietward`, and `sh /data/quietward/scripts/p3_qnx_soak.sh -n 10` (`10/10 runs passed, 0 failed`; logs saved in `submission/g3/`).
- **Host build & checks (clean `git archive HEAD` export without `.git`/`.venv`/`.pytest_cache`; suite contains no network-using commands, no pip):** `make`, `make gateway-test`, `make reentrant-test`, `make demo-verify`, `make fuzz` (runs `scripts/fuzz_qw.py --count 1000` via `.venv/bin/python` when present, else `python3`), `python3 scripts/host_shift_replay.py`, `python3 scripts/render_demo_plots.py`. Additional host suites: `make test` additionally requires `pytest` and `numpy`; `scripts/fetch_physionet.sh` and full 750-record `make eval` additionally require `wfdb` and `numpy`.

## Challenges we ran into

- **Our own Kill Rule failed on held-out data.** Asystole false negatives moved from 0 to 1 on only 37 Asystole records. We did not retune on the held-out set. We ship the honest framing (Variant B): a secondary review and escalation gateway, not a validated suppressor.
- **QNX 8.0 has no macOS or ARM host, and HAM is gone.** The SDK runs on x86_64 Linux (executed inside `.devcontainer/devcontainer.json` on an x86_64 GitHub Codespace). High Availability Manager is deprecated in 8.0; our `p1_qplms_probe.sh` check confirmed `plms` is absent on the QEMU image (`PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK`), so `qw_supervisor` handles restart and triggers `qw_failopen` cleanly. Also, real-time replay (`65.5 s/record` at `--rate-ms 100`) outran the 30 s nurse review escalation timer during soak testing, so we added `--speed` (`100x` in soak) to `qw_acq` and `--restart-delay-ms 5000` to `qw_supervisor` so the 500 ms fail-open deadline and supervisor respawn are deterministically tested without tight spin-restarts.
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

Also: 529 host gateway checks, 0 failed; 320 concurrent reentrant calls across 8 threads; fuzzer 1000/1000; zero synthetic waveforms; zero paid APIs (`submission/g3/stress-checklist.md` row 14); the host build and checks run from a clean `git archive HEAD` export without `.git`/`.venv`/`.pytest_cache` and the suite contains no network-using commands (`submission/g3/host_tests_20261124.log`).

## What's next

1. Render & master the `<3:00` 1080p demo video (`video/SCRIPT_V2_DRAFT.md`) and upload as Public on YouTube.
2. Owner sign-off (`rules/alarm_rules.json` clinical validation if intended, otherwise ship Variant B) and flip `DrAhmed7887/hack-qnx` to Public.
3. Submit final Devpost form before the 15 Dec 2026 deadline.

**Live QNX 8.0 `pidin` & `slog2info` capture (`submission/g3/qnx8_live_proof_20261125.log` & `slog2_run_01.txt`):**
- `uname -a`: `QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64`
- `/dev/quietward`: `{"seq":201,"bed":0,"alarm":"Asystole","decision":"ACK","reason":"nurse ack: proof-rn","state":"acked","ts_ms":943572}`
- `slog2info -b quietward`: `sev=CRIT bed=3 seq=104 alarm=Asystole decision=CONFIRM reason=no decision within deadline (fail-open)` + `qw_failopen`: `alarm_out=CONFIRM_ALL`

## Built With

C11, POSIX, QNX Neutrino 8.0 (message passing, pulses, `/dev/quietward` resource manager, `slog2`, shared-memory seqlock ring, `qw_supervisor`, QEMU x86_64), Python (stdlib for demo/fuzzer/plots; pytest + numpy + wfdb for full test/eval), GNU Make, SVG, PhysioNet/CinC 2015 data (ODC-BY 1.0).

## Repository, licence and submission period

`DrAhmed7887/hack-qnx` is a new, non-fork repository (pending owner click); first commit `d1d0bd6` on 22 Nov 2026 (`submission/g3/git_birth_commit.txt`; GitHub creation date / submission-period check pending owner click). Code: Apache License 2.0 (`LICENSE` present; GitHub About detection pending owner click). Data: ODC-BY 1.0 attribution (PhysioNet/CinC Challenge 2015 dataset, DOI 10.13026/c9fg-a467; `NOTICE`, `DATA_CARD.md`, `data/demo/LICENSE-ODC-BY.txt`). Claim-to-file map: `CLAIMS.md`. Rules compliance: `submission/COMPLIANCE_CHECKLIST.md`.

## Data and citations

- Data: PhysioNet/Computing in Cardiology Challenge 2015 dataset, real de-identified ICU records, ODC-BY 1.0, DOI 10.13026/c9fg-a467. Paper: Clifford GD, Silva I, Moody B, Li Q, Kella D, Shahin A, Kooistra T, Perry D, Mark RG (2015). The PhysioNet/Computing in Cardiology Challenge 2015: Reducing False Arrhythmia Alarms in the ICU. Computing in Cardiology 42:273-276.
- openFDA MAUDE report `10055677` (`Malfunction` report, received 14 May 2020, `mdr_text_key=191262317`; no adverse event to patient or user reported; verified in `docs/EVIDENCE.md`, device model omitted).
- Repo documents: README, JUDGES.md, DATA_CARD.md, `submission/g3/stress-checklist.md`.
