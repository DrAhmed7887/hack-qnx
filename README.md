# Quiet Ward 2.0

ICU nurses face relentless **alarm fatigue**: bedside monitors fire overwhelmingly false arrhythmia alerts, burying genuine deterioration in noise. Quiet Ward 2.0 is a deterministic C11 secondary review gateway on **QNX Neutrino RTOS 8.0** built around one hard safety invariant: **an alarm is never silently dropped.** The bedside alarm remains intact, candidate false alarms with clear hemodynamic contradiction are routed to `/dev/quietward` for nurse review, and every fault path (detector crash, deadline miss, queue overflow, detached shared memory, restart) fails open to `CONFIRM`.

> **Research demo, not a medical device.** Not validated for clinical use or patient care. The thresholds in `rules/alarm_rules.json` carry `status: "pending_signoff"` (engineering defaults, not signed clinical rules).

Interactive walkthrough and live QNX 8.0 proof viewer: **Quiet Ward Companion** ([`docs/index.html`](docs/index.html)).

## Held-Out Workload Result & Threshold Integrity

On `n=227` held-out PhysioNet/CinC 2015 ICU records ([`eval/results/heldout_fixed.json`](eval/results/heldout_fixed.json)), Quiet Ward routes **26.8% (37/138)** of all false alarms -- including **76.7% (23/30)** of false Asystole alarms and **69.2% (9/13)** of false Bradycardia alarms -- to nurse review while preserving **96.6% (86/89)** true-alarm recall and dropping **0** alarms (Challenge score **51.46%** vs **39.21%** no-suppression baseline).

| Split | Records | Baseline Score | Quiet Ward Score | True-Alarm Recall (95% CI) | False Alarms Routed |
|---|---|---|---|---|---|
| Train (`eval/results/train_fixed.json`) | 523 | 39.20% | 53.83% | 98.5% (202/205) [95.8-99.5] | 27.0% (86/318) |
| Held-out (`eval/results/heldout_fixed.json`) | 227 | 39.21% | 51.46% | 96.6% (86/89) [90.6-98.8] | 26.8% (37/138) |

**Pre-registered threshold integrity:** All signal thresholds were locked on the `n=523` training split before evaluating the `n=227` held-out split, and we report the unedited held-out result without post-hoc retuning (Ventricular Tachycardia missed true alarms fell from 11 under the frozen v0 draft to 0, while 1 of 7 true Asystole alarms on `n=37` Asystole records was routed to review vs 0 in v0, so Quiet Ward ships strictly as a non-discarding nurse-review gateway). Unlike offline retrospective PhysioNet 2015 ML ensembles (which scored 81.39 and 85.04 on a different 500-record hidden test set with unconstrained retrospective pipelines), Quiet Ward runs as a deterministic, zero-alloc, reentrant C11 RTOS watchdog gateway on QNX 8.0 where any uncertainty or fault latches `CONFIRM`.

Six real `train`-split demonstration records (`data/demo/`, [`docs/plots/demo_overview.svg`](docs/plots/demo_overview.svg)) show every decision path (`a109l` false Asystole routed to review; `a653l`, `t116s`, `t106s`, `v188s`, `f450s` confirmed).

## Architecture, `/dev/quietward` + `slog2` Pairing & Safety Invariant

![Architecture](docs/workflow.svg)

Quiet Ward separates signal analysis from alarm latching across isolated QNX processes (`src/qnx/README.md`, [`submission/g3/stress-checklist.md`](submission/g3/stress-checklist.md)):
- **`/dev/quietward` + `slog2` pairing:** `qw_detector` logs the full multi-channel signal rationale (trailing gap, heart-rate bounds, SQI) into `slog2` (`slog2info -b quietward`), while `/dev/quietward` (`qw_resmgr`) publishes compact JSONL state transitions (`seq`, `bed`, `alarm`, `decision`, `state`, `ts_ms`) and accepts nurse ACK `write()`s. Both streams are paired side-by-side in the Quiet Ward Companion ([`docs/index.html`](docs/index.html)).
- **Fail-open enforcement:** `qw_alarm_mgr` runs a 50 ms watchdog tick (500 ms default decision deadline), latches `CONFIRM` on timeout/overflow/restart journal replay, and `qw_failopen` writes `CONFIRM_ALL` independently of `qw_supervisor`. Verified across **10/10 QNX 8.0 target soak runs (250/250 checks, 0 failed)** ([`submission/g3/qnx8_live_proof_20261125.log`](submission/g3/qnx8_live_proof_20261125.log), `submission/g3/g3_run_01..10.txt`).

## Where QNX 8.0 Appears (Reviewer Map)

Every binary cross-compiles with `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror` (`0` warnings) and is verified on booted `QNX noname 8.0.0 x86_64` under QEMU TCG (`submission/g3/p1_qnx8_guest_20261125.log`, `qnx8_live_proof_20261125.log`, `g3_run_01..10.txt`).

| File | QNX 8.0 / POSIX / C11 primitives | Verified QNX 8.0 / Host output | Scene |
|---|---|---|---|
| `src/qnx/qw_acq.c` | `shm_open`, `mmap`, `ChannelCreate`, `ConnectAttach`, `SIGEV_PULSE_INIT`, `timer_create`, `MsgReceivePulse`, `name_open`, `MsgSendPulse` | Replayed `a109l`, `a653l`, `v188s` to `/dev/shmem/qw_ring_bed*`, sent `QW_PULSE_ALARM_RAISED` in 10/10 soak runs | S02, S05 |
| `src/qnx/qw_detector.c` | `name_attach`, `name_open`, `shm_open`, `mmap`, `MsgReceive`, `MsgReply`, `TimerTimeout`, `MsgSend` | Sends `QW_MSG_DECISION`; killed (`slay -s KILL`) and respawned in 10/10 soak runs | S02, S05 |
| `src/qnx/qw_alarm_mgr.c` | `name_attach`, `SIGEV_PULSE_INIT`, `timer_create`, `MsgReceive`, `MsgReply`, `pthread_create` | 50 ms watchdog tick latches `CONFIRM` (`sev=CRIT` in `slog2_run_01..10.txt`) | S02, S05 |
| `src/qnx/qw_failopen.c` | `open(O_NOFOLLOW)`, `write("CONFIRM_ALL")`, `qw_log_emit` at `CRIT` | Verified standalone (`alarm_out=CONFIRM_ALL` + 4 `sev=CRIT` lines for beds `0..3`) | S02, S05 |
| `src/qnx/qw_supervisor.c` | `posix_spawn`, `waitpid`, `kill`, `sigaction` | Supervises `qw_alarm_mgr` & `qw_detector` (`plms` absent on QEMU image); respawns new PID | S02, S05 |
| `src/qnx/qw_bedctl.c` | `open("/dev/quietward")`, `devctl(QW_DCMD_BED_STATE)` | Prints per-bed JSON (`failopen_active:1`), rejects invalid `qw_bedctl 9` | S05 |
| `src/qnx/qw_resmgr.c` | `dispatch_create`, `resmgr_attach("/dev/quietward")`, `iofunc_read_verify`, `iofunc_write_verify`, `iofunc_devctl_default`, `MsgReply` | `/dev/quietward` (`crw-rw----`) JSONL `cat`, nurse ACK `write()`, malformed ACK refused (10/10 runs) | S02, S05 |
| `src/qnx/qw_log.c` | `slog2_register`, `slog2f` (`SLOG2_INFO/WARNING/ERROR/CRITICAL`) | `slog2info -b quietward` captured in `qnx8_live_proof_20261125.log` & `slog2_run_01..10.txt` | S05 |
| `src/qnx/qw_ring.{h,c}` | C11 `_Atomic` seqlock ring over `/qw_ring_bed%u` | Host: 10000 frames, 4 readers, `torn=0`; QNX: shared sample rings active (10/10 runs) | S02, S05 |
| `src/qnx/qw_ipc.h`, `qw_gateway_state.{h,c}` | `QW_DCMD_BED_STATE`, `QW_PULSE_*`, `O_NOFOLLOW` journal, `qw_gateway_test` | Host: `529 checks, 0 failed`; linked into `qw_acq`, `qw_alarm_mgr`, `qw_failopen` on QNX 8.0 | S02, S05 |
| `scripts/p1_hello_ipc.c`, `p1_qplms_probe.sh`, `p1_tcg_jitter.c` | `MsgSend`/`MsgReply`, `slog2`, `plms` probe, `clock_gettime` jitter check | `P1_HELLO_RESULT=PASS`, `PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK`, jitter `p50=0.789 ms` | S05, S07 |
| `Makefile`, `scripts/p3_qnx_soak.sh`, `.devcontainer/` | `make qnx`, 10-run target soak, Ubuntu 24.04 + QEMU TCG bring-up | 6 ELFs built (`0` warnings); soak `10/10 runs, 250/250 checks, 0 failed` | S05, S07 |

## QNX 8.0 Target, Toolchain & Build/Run Instructions

- **Target & toolchain:** QNX SDP 8.0 (`QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64`) in `.devcontainer/devcontainer.json`, booted via `mkqnximage --type=qemu --arch=x86_64` and `qemu-system-x86_64 -accel tcg -cpu IvyBridge -smp 2 -m 2048`. Cross-compile: `source ~/qnx800/qnxsdp-env.sh && make qnx-check && make qnx`.
- **On booted QNX 8.0 target** (after copying `build/qnx/*`, `rules/`, `config/`, `data/demo/`, `scripts/p3_qnx_soak.sh` to `/data/quietward/`):
```sh
mkdir -p /var/run/quietward && chmod 0700 /var/run/quietward
/data/quietward/bin/qw_supervisor /data/quietward/rules/alarm_rules.json /data/quietward/config/detector.json --bin-dir /data/quietward/bin --restart-delay-ms 5000 &
/data/quietward/bin/qw_acq /data/quietward/data/demo/a109l.qw Asystole --bed 0 --seq 201 --rate-ms 50 --speed 100
cat /dev/quietward && echo '{"ack":201,"nurse":"proof-rn"}' > /dev/quietward
/data/quietward/bin/qw_bedctl 0 && slog2info -b quietward
sh /data/quietward/scripts/p3_qnx_soak.sh -n 10 -b /data/quietward/bin -r /data/quietward/rules/alarm_rules.json -d /data/quietward/config/detector.json -D /data/quietward/data/demo -o /data/quietward/out
```
- **Host build & offline verification** (runs from a clean `git archive HEAD` export without `.git`/`.venv`/network):
```sh
make && make gateway-test && make reentrant-test && make demo-verify && make fuzz
python3 scripts/host_shift_replay.py && python3 scripts/render_demo_plots.py
```
`make test` additionally runs 105 `pytest` checks (needs `pytest` and `numpy`); full 750-record `make eval` needs `wfdb` and `numpy` (`scripts/fetch_physionet.sh`).

## Limitations

- **Un-signed thresholds:** `rules/alarm_rules.json` remains `pending_signoff`; thresholds are engineering defaults, not clinically validated rules.
- **No suppression of VF/VTach noise floor:** Ventricular Flutter/Fib is unconditionally confirmed (`never_suppress`), and high-rate Ventricular Tachycardia false alarms remain largely confirmed (`5.3%` routed on held-out).
- **Channel quality dependence:** Pulsatile SQI below `0.90` forces `CONFIRM` even on false alarms (`t106s`), trading extra alerts for safety.
- **QEMU TCG timing:** QNX 8.0 runs execute under QEMU TCG software emulation (`p50=0.789 ms` timer jitter), not bare-metal ICU hardware.
- **Evaluation split:** Held-out run budget reserves run 3 of 3 in `eval/heldout_runs.log` (do not re-run `make eval-heldout`); local 523/227 split differs from the 2015 hidden 500-record Challenge test set.

## Data, Attribution & Licence

- **Dataset:** Real, de-identified ICU records from PhysioNet/CinC Challenge 2015, DOI [10.13026/c9fg-a467](https://doi.org/10.13026/c9fg-a467) (**ODC-BY 1.0**; Clifford et al., Computing in Cardiology 42:273-276, 2015). Only the six `train` records in `data/demo/` are redistributed (`DATA_CARD.md`, `NOTICE`).
- **Provenance & code licence:** `DrAhmed7887/hack-qnx` is a new, non-fork repo created 22 Nov 2026 (`d1d0bd6`, **Apache License 2.0** in `LICENSE`). Claim-to-file map: [`CLAIMS.md`](CLAIMS.md); judging map: [`JUDGES.md`](JUDGES.md); rules compliance: [`submission/COMPLIANCE_CHECKLIST.md`](submission/COMPLIANCE_CHECKLIST.md).
