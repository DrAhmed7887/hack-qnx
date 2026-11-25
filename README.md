# Quiet Ward 2.0

> **Research demo, not a medical device.** Not validated for clinical use, not for patient care.
> The suppression thresholds in `rules/alarm_rules.json` carry `status: "pending_signoff"`: they are engineering defaults, not signed clinical rules.

Quiet Ward is a **secondary review and escalation gateway** for bedside monitor alarms, built for the QNX Global Hackathon (Robotics & Medical). It looks at the signals behind an arrhythmia alarm and, when a second channel clearly contradicts it, queues that alarm for a nurse to review instead of treating it as urgent. It never removes the bedside alarm.

**Status of the QNX runtime.** Built and verified on **QNX Neutrino RTOS 8.0 (`QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64`)** running under QEMU TCG (`-accel tcg -cpu IvyBridge`) inside an x86_64 Ubuntu 24.04 GitHub Codespace (`.devcontainer/devcontainer.json`). All 6 gateway processes (`qw_acq`, `qw_detector`, `qw_alarm_mgr`, `qw_failopen`, `qw_supervisor`, `qw_bedctl`) plus `p1_hello_ipc` and `p1_tcg_jitter` cross-compile cleanly with `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror` (`0` warnings; verified in [`submission/g3/p1_qnx8_guest_20261125.log`](submission/g3/p1_qnx8_guest_20261125.log)). `p1_qplms_probe.sh` confirmed `plms` is absent (`PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK`), so `qw_supervisor` supervises `qw_alarm_mgr` and `qw_detector`. On the booted QNX 8.0 target, `scripts/p3_qnx_soak.sh -n 10` passed **10 of 10 runs (230/230 checks, `0` failed)** ([`submission/g3/g3_run_01.txt`](submission/g3/g3_run_01.txt)..`10`, [`submission/g3/slog2_run_01.txt`](submission/g3/slog2_run_01.txt)..`10`), alongside [`submission/g3/qnx8_live_proof_20261125.log`](submission/g3/qnx8_live_proof_20261125.log). Host-portable C11 tests (`make gateway-test`: 529 checks, 0 failed; `make reentrant-test`: 320 calls; `make fuzz`: 1000/1000) also pass (`submission/g3/host_tests_20261124.log`).

## Architecture and safety invariant

The invariant: **an alarm is never silently dropped.**

- The raw bedside alarm is preserved. Quiet Ward only adds a second opinion.
- A candidate suppression is not discarded: it is queued on `/dev/quietward` for a nurse ACK, with timeout escalation.
- Every fault path fails open to `CONFIRM`: detector crash, decision timeout, a 17th pending alarm, detached shared memory, and process restart (journal replay).

![Architecture](docs/workflow.svg)

Diagram source: [`docs/workflow.svg`](docs/workflow.svg). Process layout and QNX primitive mapping: [`src/qnx/README.md`](src/qnx/README.md). Stress and fail-open verification status (host + QNX 8.0 QEMU 10/10 soak): [`submission/g3/stress-checklist.md`](submission/g3/stress-checklist.md).

## Where QNX 8.0 Appears (Reviewer Map)

Every primitive below is compiled (`qcc -Vgcc_ntox86_64 -Wall -Wextra -Werror`, 0 warnings) and verified against the booted `QNX noname 8.0.0 x86_64` QEMU image (`submission/g3/p1_qnx8_guest_20261125.log`, `submission/g3/qnx8_live_proof_20261125.log`, `submission/g3/g3_run_01..10.txt`). Video scenes refer to `video/SCRIPT_V2_DRAFT.md`.

| File | QNX 8.0 / POSIX / C11 primitives & tooling present in source | Verified QNX 8.0 / Host output | Video scene |
|---|---|---|---|
| `src/qnx/qw_acq.c` | `shm_open`, `mmap`, `ChannelCreate`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceivePulse`, `name_open`, `MsgSendPulse`, `name_close` | Compiled (`build/qnx/qw_acq`); replayed `a109l`, `a653l`, `v188s` into `/dev/shmem/qw_ring_bed*` and raised `QW_PULSE_ALARM_RAISED` in 10/10 soak runs (`g3_run_01..10.txt`, `qnx8_live_proof_20261125.log`) | S02, S05 |
| `src/qnx/qw_detector.c` | `name_attach`, `name_open`, `name_close`, `shm_open`, `mmap`, `MsgReceive`, `MsgReply`, `MsgError`, `TimerTimeout`, `MsgSend`, `ConnectDetach` | Compiled (`build/qnx/qw_detector`); visible in `pidin` (`8900632`), sends `QW_MSG_DECISION` via `MsgSend`, killed with `slay -s KILL` and respawned in 10/10 soak runs | S02, S05 |
| `src/qnx/qw_alarm_mgr.c` | `name_attach`, `ConnectAttach`, `ConnectDetach`, `SIGEV_PULSE_INIT`, `timer_create`, `timer_settime`, `MsgReceive`, `MsgReply`, `MsgError`, `pthread_create`, `pthread_detach` | Compiled (`build/qnx/qw_alarm_mgr`); visible in `pidin` (2 threads), 50 ms watchdog tick latches `CONFIRM` (`no decision within deadline (fail-open)` at `sev=CRIT` in `slog2_run_01..10.txt`) | S02, S05 |
| `src/qnx/qw_failopen.c` | standalone fail-open binary (`open` with `O_NOFOLLOW`, `write` of `CONFIRM_ALL`, `qw_log_emit` at CRIT) | Compiled (`build/qnx/qw_failopen`); verified standalone in `qnx8_live_proof_20261125.log` (`alarm_out=CONFIRM_ALL` + 4 `sev=CRIT` lines for beds `0..3`) | S02, S05 |
| `src/qnx/qw_supervisor.c` | `posix_spawn`, `waitpid`, `kill`, `sigaction` | Compiled (`build/qnx/qw_supervisor`); visible in `pidin`, spawns/restarts `qw_alarm_mgr` and `qw_detector` with new PID in 10/10 soak runs | S02, S05 |
| `src/qnx/qw_bedctl.c` | `open("/dev/quietward")`, `devctl(QW_DCMD_BED_STATE)` | Compiled (`build/qnx/qw_bedctl`); prints per-bed JSON including `failopen_active:1`, refuses `qw_bedctl 9` (`qnx8_live_proof_20261125.log`, `g3_run_01..10.txt`) | S05 |
| `src/qnx/qw_resmgr.c` | `dispatch_create`, `iofunc_func_init`, `iofunc_attr_init`, `resmgr_attach("/dev/quietward")`, `dispatch_context_alloc`, `dispatch_block`, `dispatch_handler`, `iofunc_read_verify`, `iofunc_write_verify`, `iofunc_devctl_default`, `resmgr_msgget`, `_IO_READ_GET_NBYTES`, `_IO_WRITE_GET_NBYTES`, `_IO_SET_READ_NBYTES`, `_IO_SET_WRITE_NBYTES`, `MsgReply` | Compiled (`RESMGR_OCB_T qw_ocb_t`, `&ocb->base`); `/dev/quietward` (`crw-rw----`) `cat` JSONL stream and nurse ACK `write()` verified live; malformed ACK refused in 10/10 runs | S02, S05 |
| `src/qnx/qw_log.c` | `slog2_register`, `slog2f` (`SLOG2_INFO`, `SLOG2_WARNING`, `SLOG2_ERROR`, `SLOG2_CRITICAL`) | `slog2info -b quietward` audit lines captured in `qnx8_live_proof_20261125.log` and all 10 `slog2_run_01..10.txt` | S05 |
| `src/qnx/qw_ring.h`, `src/qnx/qw_ring.c` | C11 `_Atomic` seqlock ring over `/qw_ring_bed%u` shared memory (`qw_ring_init`, `qw_ring_write`, `qw_ring_read_tail`; `shm_open`/`mmap` on QNX) | Host: 10000 frames, 4 readers, `torn=0`; QNX: locks/reads real sample rings between `qw_acq` and `qw_detector` in 10/10 runs | S02, S05 |
| `src/qnx/qw_ipc.h` | `QW_DCMD_BED_STATE`, `QW_PULSE_*`, `QW_MSG_DECISION`, `qw_ring_hdr_t` (`_Atomic` ring header) | Verified across all 6 cross-compiled QNX 8.0 binaries and host `qw_gateway_test` | S02, S05 |
| `src/qnx/qw_gateway_state.h`, `src/qnx/qw_gateway_state.c`, `src/qnx/qw_gateway_test.c` | portable C11 gateway state machine, `O_NOFOLLOW` journal, `529 checks, 0 failed` on host | `qw_gateway_test: 529 checks, 0 failed` (host); linked into `qw_acq`, `qw_alarm_mgr`, and `qw_failopen` on QNX 8.0 | S02, S05 |
| `src/qnx/qw_resmgr.h`, `src/qnx/qw_log.h` | C11 interfaces for `/dev/quietward` JSONL serializer and `slog2` audit formatter | Verified on host (`qw_gateway_test`) and on QNX 8.0 target (`qnx8_live_proof_20261125.log`) | S02, S05 |
| `scripts/p1_hello_ipc.c` | `ChannelCreate`, `ConnectAttach`, `MsgSend`, `MsgReceive`, `MsgReply`, `TimerTimeout`, `slog2_register`, `slog2f`, `clock_gettime` | `P1_HELLO_RESULT=PASS` + `slog2info -b qw_hello`: `p1_hello_ipc: slog2 ok` (`p1_qnx8_guest_20261125.log`) | S05 |
| `scripts/p1_qplms_probe.sh` | `plms` / `plmsctrl` / `slog2info -b plms` availability probe | `PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK` (`plms` absent on QEMU image; `p1_qnx8_guest_20261125.log`) | S02, S07 |
| `scripts/p1_tcg_jitter.c` | `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_REALTIME)`, `nanosleep` QEMU TCG jitter calibrator | `jitter_ms p50=0.789 p95=1.539 p99=2.613 max=3.630`, `recommended --watchdog-ms 200` (`p1_qnx8_guest_20261125.log`) | S05, S07 |
| `Makefile` | QNX targets `qnx-check`, `qnx`, `qnx-build`, `qnx-clean`: `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror`, `-lm -lslog2` | Built all 6 `build/qnx/*` ELFs with `0` warnings (`p1_qnx8_guest_20261125.log`) | S07 |
| `scripts/p3_qnx_soak.sh` | 10-run QNX target soak using `pidin`, `slay`, `/dev/quietward`, `qw_bedctl`, `slog2info` | `soak: 10 runs, 0 failed runs` (230/230 checks passed in `submission/g3/g3_run_01..10.txt`) | S05 |
| `.devcontainer/devcontainer.json` | Ubuntu 24.04 Linux image + `qemu-system-x86` + `build-essential` + `sshd` bring-up container for QNX SDP 8.0 | Executed in x86_64 GitHub Codespace for QNX 8.0 SDP cross-build and QEMU boot | S07 |

The host-portable parts (`qw_ring`, `qw_gateway_state`, `qw_log` formatter, `qw_resmgr` JSONL serializer) also run offline without QNX headers via `make gateway-test`.

## QNX 8.0 Target, Toolchain & Build/Run Instructions

- **QNX version:** QNX SDP 8.0 / QNX Neutrino RTOS 8.0 (`QNX noname 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64`).
- **Host requirement:** GitHub Codespaces (`x86_64`) or an `x86_64` Linux host (`~/qnx800/qnxsdp-env.sh`; Ubuntu 24.04 via `.devcontainer/devcontainer.json`).
- **Target:** QNX 8.0 `x86_64` QEMU image built with `mkqnximage --type=qemu --arch=x86_64 --sys-size=512 --data-size=512 --cpu=2 --ram=2G --ssh-ident=~/.ssh/qnx_id_ed25519.pub --noprompt --build=all` and booted via `qemu-system-x86_64 -accel tcg -cpu IvyBridge -smp 2 -m 2048` (`NO_KVM` TCG emulation).
- **Toolchain:** `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror`, linked with `-lm -lslog2`; GNU Make; `source ~/qnx800/qnxsdp-env.sh`.

**Cross-build (verified on x86_64 Linux Codespace with SDP 8.0):**

```sh
source ~/qnx800/qnxsdp-env.sh
make qnx-check                  # confirms qcc is on PATH
make qnx                        # -> build/qnx/{qw_acq,qw_detector,qw_alarm_mgr,qw_failopen,qw_supervisor,qw_bedctl}
```

**Build, boot, and deploy to the QNX 8.0 x86_64 QEMU image (verified):**

1. Generate the minimal image with `mkqnximage` (writable target storage on `/data`; `/var -> /data/var`) and boot headlessly in TCG mode with user-mode SSH forwarding (`-netdev user,id=net0,hostfwd=tcp:127.0.0.1:8022-:22 -device virtio-net-pci,netdev=net0`).
2. Create `/data/quietward/{bin,rules,config,data/demo,scripts}` on the guest (`127.0.0.1:8022`) and stream over `build/qnx/*`, `rules/alarm_rules.json`, `config/detector.json`, `data/demo/*`, and `scripts/p3_qnx_soak.sh`.

**Run on the booted QNX 8.0 target (verified in `submission/g3/qnx8_live_proof_20261125.log` and `g3_run_01..10.txt`):**

```sh
mkdir -p /var/run/quietward && chmod 0700 /var/run/quietward
/data/quietward/bin/qw_supervisor /data/quietward/rules/alarm_rules.json /data/quietward/config/detector.json --bin-dir /data/quietward/bin --restart-delay-ms 5000 &
/data/quietward/bin/qw_acq /data/quietward/data/demo/a109l.qw Asystole --bed 0 --seq 201 --rate-ms 50 --speed 100
cat /dev/quietward                       # JSONL decision stream
echo '{"ack":201,"nurse":"proof-rn"}' > /dev/quietward
/data/quietward/bin/qw_bedctl 0          # per-bed JSON via devctl
slog2info -b quietward                   # audit lines
sh /data/quietward/scripts/p3_qnx_soak.sh -n 10 \
  -b /data/quietward/bin -r /data/quietward/rules/alarm_rules.json \
  -d /data/quietward/config/detector.json -D /data/quietward/data/demo \
  -o /data/quietward/out                 # -> 10/10 runs, 0 failed -> pulled into submission/g3/
```

**Host commands (clean `git archive HEAD` export without `.git`/`.venv`/`.pytest_cache`; suite contains no network-using commands, no pip, no SDK):**

```sh
make
make gateway-test
make reentrant-test
make demo-verify
make fuzz            # runs scripts/fuzz_qw.py --count 1000 via .venv/bin/python when present, else python3
python3 scripts/host_shift_replay.py
python3 scripts/render_demo_plots.py
```

Additional host suites (beyond stdlib/C11): `make test` additionally requires `pytest` and `numpy`; `scripts/fetch_physionet.sh` and the full 750-record `make eval` additionally require `wfdb` and `numpy`.

## Evaluated results (no clinical claims)

Source: real PhysioNet/CinC 2015 training records, local split in `eval/split.json`, rules `pending_signoff`. Scores are the Challenge score in percent. "Base" is no suppression at all (every alarm confirmed). FN counts true alarms that were queued for review instead of confirmed.

**Train split** (`eval/results/train_fixed.json`, N=523)

| Version | Score | FN |
|---|---|---|
| Base (no suppression) | 39.2% | 0 |
| v0 (frozen) | 40.2% | 49 |
| fixed | 53.8% | 3 |

Fixed true-alarm recall: 98.5% (202 of 205), 95% CI [95.8-99.5]. Kill Rule: **PASS**.

**Held-out split** (`eval/results/heldout_fixed.json`, N=227; runs 1 and 2 of 3 used, run 3 reserved and untouched)

| Version | Score | FN |
|---|---|---|
| Base (no suppression) | 39.2% | 0 |
| v0 (frozen) | 40.8% | 18 |
| fixed | 51.5% | 3 |

Fixed true-alarm recall: 96.6% (86 of 89), 95% CI [90.6-98.8]. Ventricular tachycardia FN went 11 to 0.

**Kill Rule: FAIL, so we ship Variant B.** On held-out, Asystole FN moved from 0 to 1, on only 37 Asystole records (low n). We did **not** retune on the held-out set. The honest read: the fixed rules beat v0 and the baseline on held-out, but they break our own rule that no alarm type may miss more true alarms than v0 did. So the entry is framed as a review gateway (Variant B), not as a validated suppressor.

**Not comparable to the Challenge leaderboard.** The PhysioNet/CinC 2015 winners scored 81.39 (Event 1) and 85.04 (Event 2) on a different 500-record hidden test set. Our numbers come from a different, local split, so they are not directly comparable.

### The six `data/demo/` records

Regenerate with `python3 scripts/host_shift_replay.py` (writes `submission/g3/host_shift_review.{json,md}`). Six records is a demonstration, not an accuracy measurement.

![Real PhysioNet 2015 Demo Waveforms](docs/plots/demo_overview.svg)

Overview: [`docs/plots/demo_overview.svg`](docs/plots/demo_overview.svg) (one SVG per record sits beside it). Each plot shows the 16 s before the alarm at 300 s, an ECG lead and the pulse channel with their quality indices, and the exact `build/qw` reason. They are generated offline with `python3 scripts/render_demo_plots.py` (stdlib Python only) from the real ODC-BY 1.0 `train`-split records: zero synthetic data.

| Record | Alarm | Truth | Decision | Why |
|---|---|---|---|---|
| a109l | Asystole | false | SUPPRESS_TO_REVIEW | pulse present in the trailing 5 s, so the asystole alarm is contradicted; queued for nurse review |
| a653l | Asystole | true | CONFIRM | pulse not continuous in the trailing 5 s (trailing gap 3.41 s over the 3.20 s limit) |
| t116s | Tachycardia | false | CONFIRM | conservative miss: the ECG cross-check still reports a max heart rate of 160 bpm, above the 140 bpm threshold, so the alarm stays confirmed |
| t106s | Tachycardia | true | CONFIRM | pulsatile channel quality 0.85, below the 0.90 minimum, so no suppression is attempted |
| v188s | Ventricular_Tachycardia | true | CONFIRM | ECG max heart rate 242 bpm exceeds the threshold; pulse does not contradict |
| f450s | Ventricular_Flutter_Fib | true | CONFIRM | on the never-suppress list; confirmed unconditionally |

`t116s` is the instructive one: a false alarm that is still confirmed. That is the intended failure direction (a missed suppression adds noise, a missed alarm would add risk).

## Build, tests and fresh-clone reproduce

Offline, no network and no pip needed for the C builds and the stdlib checks:

```sh
make                 # build/qw
make gateway-test    # seqlock ring + gateway state machine (host-portable C11)
make reentrant-test  # concurrent qw_decide() calls, no shared state
make demo-verify     # checks data/demo/ against MANIFEST.json hashes
make fuzz            # runs scripts/fuzz_qw.py --count 1000 via .venv/bin/python when present, else python3 (1000 seeded mutations, stdlib Python)
python3 scripts/host_shift_replay.py   # six-record replay report
python3 scripts/render_demo_plots.py   # docs/plots/*.svg from the six real records
```

`make gateway-test` currently reports `qw_gateway_test: 529 checks, 0 failed`.

**`make qnx` (verified on QNX SDP 8.0 x86_64 Linux Codespace).** With `~/qnx800/qnxsdp-env.sh` sourced, `make qnx` cross-builds all six gateway binaries under `build/qnx/` (`qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror`) with `0` warnings, verified by `readelf -h` and executed end-to-end on QEMU QNX 8.0 (`submission/g3/p1_qnx8_guest_20261125.log`, `submission/g3/qnx8_live_proof_20261125.log`, and `submission/g3/g3_run_01..10.txt`).

`make test` additionally runs the pytest suite (needs `pytest` and `numpy`).

**Full 750-record evaluation.** The raw records are not stored in the repository. `scripts/fetch_physionet.sh` downloads the PhysioNet/CinC 2015 training set and converts it to `data/bin/` (needs `wfdb` and `numpy` in `.venv`). Then `make eval-train` reproduces the train table above. The held-out split has a limited run budget (`eval/heldout_runs.log`); do not spend run 3.

## Data and attribution

All signals are real, de-identified ICU records from the PhysioNet/Computing in Cardiology Challenge 2015, redistributed under the **Open Data Commons Attribution License (ODC-BY) 1.0**.

> Dataset: PhysioNet/Computing in Cardiology Challenge 2015, DOI [10.13026/c9fg-a467](https://doi.org/10.13026/c9fg-a467) (ODC-BY 1.0). Paper: Clifford GD, Silva I, Moody B, Li Q, Kella D, Shahin A, Kooistra T, Perry D, Mark RG (2015). The PhysioNet/Computing in Cardiology Challenge 2015: Reducing False Arrhythmia Alarms in the ICU. Computing in Cardiology 42:273-276.

Only the six `data/demo/` records are redistributed, converted from WFDB to the `.qw` format. See [`LICENSE`](LICENSE), [`NOTICE`](NOTICE), [`DATA_CARD.md`](DATA_CARD.md), `data/demo/LICENSE-ODC-BY.txt` and [`JUDGES.md`](JUDGES.md).

## Repository provenance and licence

`DrAhmed7887/hack-qnx` is a new, non-fork repository (pending owner click); first commit `d1d0bd6` on 22 Nov 2026 (`submission/g3/git_birth_commit.txt`; GitHub creation date / submission-period check pending owner click). Code is under the **Apache License 2.0** (`LICENSE` present; GitHub About detection pending owner click); the data keeps its **ODC-BY 1.0** attribution (PhysioNet/CinC Challenge 2015 dataset, DOI 10.13026/c9fg-a467). Compliance status for every Devpost email item: [`submission/COMPLIANCE_CHECKLIST.md`](submission/COMPLIANCE_CHECKLIST.md). Every measured benchmark, test result, and external clinical/leaderboard figure in the public text maps to a committed file in [`CLAIMS.md`](CLAIMS.md).
