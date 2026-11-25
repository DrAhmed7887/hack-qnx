# Quiet Ward 2.0: Devpost submission draft

ICU nurses face relentless **alarm fatigue**: bedside monitors fire overwhelmingly false arrhythmia alerts, burying genuine patient deterioration in noise. Quiet Ward 2.0 is a deterministic C11 secondary review gateway on **QNX Neutrino RTOS 8.0** built around one hard safety invariant: **an alarm is never silently dropped.** The bedside alarm stays intact, candidate false alarms with clear hemodynamic contradiction are routed to `/dev/quietward` for nurse review, and every fault path (detector crash, deadline miss, queue overflow, detached shared memory, restart) fails open to `CONFIRM`.

> Research demo, not a medical device. The alarm rules carry `status: "pending_signoff"`: engineering defaults, not signed clinical rules. Built, cross-compiled (`qcc -Vgcc_ntox86_64 -Wall -Wextra -Werror`, 0 warnings), and verified on booted **QNX Neutrino RTOS 8.0 (`QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64`)** under QEMU TCG (`10/10` target soak runs, `250/250` checks passed, `0` failed; logs under `submission/g3/`).

Category: Robotics & Medical. Bonus: Best use of QNX-specific Technologies.

## Elevator Pitch

A second opinion on bedside alarms that can never silence the first. On `n=227` held-out PhysioNet/CinC 2015 ICU records, Quiet Ward routes **26.8% (37/138)** of all false alarms -- including **76.7% (23/30)** of false Asystole alarms and **69.2% (9/13)** of false Bradycardia alarms -- to nurse review while preserving **96.6% (86/89)** true-alarm recall and dropping **0** alarms (score **51.46%** vs **39.21%** no-suppression baseline).

## Inspiration

Bedside monitors in the ICU generate constant alarm fatigue, yet any automated filter has to answer one question first: what happens when signal quality drops or a process crashes? Quiet Ward enforces a single architectural design invariant: an alarm is never silently dropped, and every uncertainty or fault latches `CONFIRM`.

## What it does

- Reads a real ICU record (ECG plus a pulsatile channel such as ABP or PLETH) and the alarm type that fired.
- Decides **CONFIRM** or **SUPPRESS_TO_REVIEW**. Candidate false alarms are never discarded: they move to a nurse review queue on `/dev/quietward` with timeout escalation and full signal rationale in `slog2`.
- Never suppresses on the never-suppress list (`Ventricular_Flutter_Fib`) and confirms unconditionally when pulsatile channel quality falls below `0.90`.
- Fails open to `CONFIRM` on detector crash, decision deadline expiry, a 17th pending alarm, detached shared memory, backward clock step, and process restart (journal replay).
- **Live `/dev/quietward` + `slog2` pairing:** `qw_detector` logs full multi-channel signal rationale into `slog2` (`slog2info -b quietward`), while `/dev/quietward` (`qw_resmgr`) publishes compact JSONL state transitions (`seq`, `bed`, `alarm`, `decision`, `state`, `ts_ms`) and accepts nurse ACK `write()`s; both streams are paired side-by-side in the **Quiet Ward Companion** (`docs/index.html`).

On six real `train`-split demonstration records (`data/demo/`), five are confirmed and one false asystole alarm (`a109l`) is routed to nurse review with zero true alarms suppressed (`t116s` is a conservative confirmed false alarm -- the intended failure direction).

![Real PhysioNet 2015 demo waveforms](../docs/plots/demo_overview.svg)

## How we built it

**Verified on the host (portable C11, logged in `submission/g3/host_tests_20261124.log`):**
- Reentrant signal detection and rule core (`qw_core`, `rules`, `qw_detector_config`).
- IPC and gateway building blocks in plain C11: per-bed `_Atomic` seqlock ring, pending/review state machine with `O_NOFOLLOW` restart journal, audit-line formatter, and JSONL serializer.
- `make gateway-test`: `529 checks, 0 failed`; `make reentrant-test`: `320` concurrent `qw_decide()` calls across `8` threads; `make fuzz`: `1000/1000` seeded mutations passed; `pytest`: `105 passed`.

**Verified on QNX Neutrino RTOS 8.0 (`QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64` on QEMU TCG, logged in `submission/g3/p1_qnx8_guest_20261125.log`, `submission/g3/qnx8_live_proof_20261125.log`, and `submission/g3/g3_run_01..10.txt`):**
- All six `#ifdef __QNX__` process binaries (`qw_acq`, `qw_detector`, `qw_alarm_mgr`, `qw_failopen`, `qw_supervisor`, `qw_bedctl`) plus `p1_hello_ipc` and `p1_tcg_jitter` compiled cleanly with `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror -lm -lslog2` (`0` warnings; `readelf -h` x86-64).
- `p1_hello_ipc` (`P1_HELLO_RESULT=PASS`, `slog2info -b qw_hello`), `p1_qplms_probe.sh` (`PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK`; `plms` absent on QEMU image, `qw_supervisor` active), and `p1_tcg_jitter` (`p50=0.789 ms`, `recommended --watchdog-ms 200`).
- Live `/dev/quietward` (`crw-rw----`): `cat` JSONL stream, nurse ACK `write()` (`"nurse ack: proof-rn"`), `qw_bedctl 0..2` `devctl` JSON, standalone `qw_failopen` (`alarm_out=CONFIRM_ALL` + `sev=CRIT` across beds `0..3`), and **10/10 target soak runs (`250/250` checks passed, `0` failed)** including `slay -s KILL qw_detector` fail-open latch + supervisor respawn with a new PID (`slog2_run_01..10.txt`).

### Where QNX 8.0 Appears (Reviewer Map)

Every primitive below is compiled and verified on the booted QNX 8.0 QEMU target (`submission/g3/*`). Video scenes S02, S05, and S07 are in `video/SCRIPT_V2_DRAFT.md`.

| File | QNX 8.0 / POSIX / C11 primitives & tooling | Verified QNX 8.0 / Host output | Scene |
|---|---|---|---|
| `src/qnx/qw_acq.c` | `shm_open`, `mmap`, `ChannelCreate`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceivePulse`, `name_open`, `MsgSendPulse`, `name_close` | Compiled (`build/qnx/qw_acq`); replayed `a109l`, `a653l`, `v188s` into `/dev/shmem/qw_ring_bed*` and raised `QW_PULSE_ALARM_RAISED` in 10/10 soak runs | S02, S05 |
| `src/qnx/qw_detector.c` | `name_attach`, `name_open`, `name_close`, `shm_open`, `mmap`, `MsgReceive`, `MsgReply`, `MsgError`, `TimerTimeout`, `MsgSend`, `ConnectDetach` | Compiled (`build/qnx/qw_detector`); sends `QW_MSG_DECISION`, logs full rationale in `slog2`; killed (`slay -s KILL`) and respawned in 10/10 soak runs | S02, S05 |
| `src/qnx/qw_alarm_mgr.c` | `name_attach`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceive`, `MsgReply`, `MsgError`, `pthread_create`, `pthread_detach` | Compiled (`build/qnx/qw_alarm_mgr`); 50 ms watchdog tick latches `CONFIRM` (`sev=CRIT` `no decision within deadline (fail-open)` in `slog2_run_01..10.txt`) | S02, S05 |
| `src/qnx/qw_failopen.c` | standalone fail-open binary (`open(O_NOFOLLOW)`, `write("CONFIRM_ALL")`, `qw_log_emit` at `CRIT`) | Verified standalone in `qnx8_live_proof_20261125.log` (`alarm_out=CONFIRM_ALL` + 4 `sev=CRIT` lines for beds `0..3`) | S02, S05 |
| `src/qnx/qw_supervisor.c` | `posix_spawn`, `waitpid`, `kill`, `sigaction` | Spawns/restarts `qw_alarm_mgr` and `qw_detector` with new PID in 10/10 soak runs | S02, S05 |
| `src/qnx/qw_bedctl.c` | `open("/dev/quietward")`, `devctl(QW_DCMD_BED_STATE)` | Prints per-bed JSON including `failopen_active:1`, refuses `qw_bedctl 9` (10/10 soak runs) | S05 |
| `src/qnx/qw_resmgr.c` | `dispatch_create`, `iofunc_func_init`, `iofunc_attr_init`, `resmgr_attach("/dev/quietward")`, `dispatch_context_alloc`, `dispatch_block`, `dispatch_handler`, `iofunc_read_verify`, `iofunc_write_verify`, `iofunc_devctl_default`, `resmgr_msgget`, `MsgReply` | `/dev/quietward` (`crw-rw----`) `cat` JSONL transitions + nurse ACK `write()` verified live; malformed ACK refused in 10/10 runs | S02, S05 |
| `src/qnx/qw_log.c` | `slog2_register`, `slog2f` (`SLOG2_INFO`, `SLOG2_WARNING`, `SLOG2_ERROR`, `SLOG2_CRITICAL`) | `slog2info -b quietward` captured in `qnx8_live_proof_20261125.log` and `slog2_run_01..10.txt` | S05 |
| `src/qnx/qw_ring.{h,c}` | C11 `_Atomic` seqlock ring over `/qw_ring_bed%u` shared memory (`shm_open`/`mmap` on QNX) | Host: 10000 frames, 4 readers, `torn=0`; QNX: shared sample rings active across 10/10 runs | S02, S05 |
| `src/qnx/qw_ipc.h`, `qw_gateway_state.{h,c}` | `QW_DCMD_BED_STATE`, `QW_PULSE_*`, `QW_MSG_DECISION`, `O_NOFOLLOW` journal | Host: `qw_gateway_test: 529 checks, 0 failed`; linked into `qw_acq`, `qw_alarm_mgr`, `qw_failopen` on QNX 8.0 | S02, S05 |
| `scripts/p1_hello_ipc.c`, `p1_qplms_probe.sh`, `p1_tcg_jitter.c` | `ChannelCreate`, `MsgSend`/`MsgReply`, `slog2`, `plms` probe, `clock_gettime` | `P1_HELLO_RESULT=PASS`, `PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK`, jitter `p50=0.789 ms` | S05, S07 |
| `Makefile`, `scripts/p3_qnx_soak.sh`, `.devcontainer/devcontainer.json` | `make qnx` (`0` warnings), 10-run soak (`pidin`, `slay`, `/dev/quietward`, `qw_bedctl`, `slog2info`), Ubuntu 24.04 + QEMU TCG container | `soak: 10 runs, 0 failed runs` (`250/250` checks, `submission/g3/g3_run_01..10.txt`) | S05, S07 |

## Accomplishments & Pre-Registered Evaluation

Source: real PhysioNet/CinC 2015 training records, local split (`eval/split.json`), Challenge score in percent (`Score = (TP+TN)/(TP+TN+FP+5*FN)`). "Base" confirms every alarm.

| Split | Records | Base Score | v0 (frozen) Score (FN) | Fixed Score (FN) | Fixed True-Alarm Recall (95% CI) | False Alarms Routed |
|---|---|---|---|---|---|---|
| Train (`eval/results/train_fixed.json`) | 523 | 39.20% | 40.19% (49) | 53.83% (3) | 98.5% (202/205) [95.8-99.5] | 27.0% (86/318) |
| Held-out (`eval/results/heldout_fixed.json`) | 227 | 39.21% | 40.80% (18) | 51.46% (3) | 96.6% (86/89) [90.6-98.8] | 26.8% (37/138) |

**Pre-registered threshold integrity:** We locked all thresholds on the `n=523` train split before scoring the `n=227` held-out split and report the unedited held-out result with zero post-hoc retuning (Ventricular Tachycardia missed true alarms fell from 11 under v0 to 0, while 1 of 7 true Asystole alarms on `n=37` Asystole records went to nurse review vs 0 in v0, keeping Quiet Ward framed strictly as a secondary nurse-review gateway). Unlike offline retrospective PhysioNet 2015 ML ensembles (which scored 81.39 and 85.04 on a different 500-record hidden test set with unconstrained retrospective pipelines), Quiet Ward runs as a deterministic, zero-alloc, reentrant C11 RTOS watchdog gateway on QNX 8.0 where any uncertainty or fault latches `CONFIRM`.

Also verified: 529 host gateway checks (`0` failed), 320 concurrent reentrant calls across 8 threads, 105 pytest checks, 1000/1000 fuzzer inputs, zero synthetic waveforms, zero paid APIs, and clean offline host builds from `git archive HEAD`.

## Challenges & Limitations

- **QNX 8.0 target bring-up:** QNX 8.0 SDP requires an `x86_64` Linux host (executed in `.devcontainer/devcontainer.json`), and `plms` is absent on the minimal QEMU image (`PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK`), so `qw_supervisor` manages process restarts while `qw_failopen` stays decoupled.
- **Un-signed clinical rules:** `rules/alarm_rules.json` thresholds carry `pending_signoff` and require clinician review before any clinical deployment.
- **Conservative VF / VTach floor:** `Ventricular_Flutter_Fib` is never suppressed (`never_suppress`), and high-rate `Ventricular_Tachycardia` false alarms remain mostly confirmed (`5.3%` routed on held-out).
- **Signal quality gating:** Any pulsatile channel SQI below `0.90` forces `CONFIRM` (`t106s`), trading extra bedside alerts for safety.
- **QEMU TCG emulation:** Live QNX 8.0 soak runs (`p50=0.789 ms` timer jitter) reflect QEMU TCG logical timing rather than certified hardware real time.
- **Held-out run cap:** Run 3 of 3 is reserved in `eval/heldout_runs.log` so held-out evaluation cannot be overfit.

## What's next

1. Clinical threshold sign-off on `rules/alarm_rules.json` if deployment beyond a research review gateway is ever pursued.
2. Hardware-in-the-loop QNX 8.0 validation on bare-metal ICU monitor feeds.
3. Final Devpost video upload (`<3:00` 1080p) and public repository flip.

**Live QNX 8.0 `pidin` & `slog2info` capture (`submission/g3/qnx8_live_proof_20261125.log` & `slog2_run_01.txt`):**
- `uname -a`: `QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64`
- `/dev/quietward`: `{"seq":201,"bed":0,"alarm":"Asystole","decision":"ACK","reason":"nurse ack: proof-rn","state":"acked","ts_ms":943572}`
- `slog2info -b quietward`: `sev=CRIT bed=3 seq=104 alarm=Asystole decision=CONFIRM reason=no decision within deadline (fail-open)` + `qw_failopen`: `alarm_out=CONFIRM_ALL`

## Built With

C11, POSIX, QNX Neutrino 8.0 (message passing, pulses, `/dev/quietward` resource manager, `slog2`, shared-memory seqlock ring, `qw_supervisor`, QEMU x86_64), Python (stdlib for demo/fuzzer/plots/Companion builder; pytest + numpy + wfdb for full test/eval), GNU Make, SVG, PhysioNet/CinC 2015 data (ODC-BY 1.0).

## Repository, licence and citations

- `DrAhmed7887/hack-qnx` is a new, non-fork repository; first commit `d1d0bd6` on 22 Nov 2026 (`submission/g3/git_birth_commit.txt`). Code: Apache License 2.0 (`LICENSE`). Data: ODC-BY 1.0 (PhysioNet/CinC Challenge 2015 dataset, DOI 10.13026/c9fg-a467; Clifford et al., Computing in Cardiology 42:273-276, 2015).
- openFDA MAUDE report `10055677` (`Malfunction` report, received 14 May 2020, `mdr_text_key=191262317`; no adverse event to patient or user reported; verified in `docs/EVIDENCE.md`, device model omitted).
- Interactive companion: `docs/index.html`; claims map: `CLAIMS.md`; judges map: `JUDGES.md`; compliance: `submission/COMPLIANCE_CHECKLIST.md`.
