# Quiet Ward 2.0

> **Research demo, not a medical device.** Not validated for clinical use, not for patient care.
> The suppression thresholds in `rules/alarm_rules.json` carry `status: "pending_signoff"`: they are engineering defaults, not signed clinical rules.

Quiet Ward is a **secondary review and escalation gateway** for bedside monitor alarms, built for the QNX Global Hackathon (Robotics & Medical). It looks at the signals behind an arrhythmia alarm and, when a second channel clearly contradicts it, queues that alarm for a nurse to review instead of treating it as urgent. It never removes the bedside alarm.

**Status of the QNX runtime.** The target runtime is QNX 8.0 (IPC processes under `src/qnx/`, QEMU boot). Today the verified part is host-portable C11: the seqlock ring (`qw_ring`), the gateway state machine (`qw_gateway_state`), logging and the resource-manager helpers, tested by `qw_gateway_test` (`make gateway-test`: 529 checks, 0 failed). The six process sources under `src/qnx/` (acquisition, detector, alarm manager, fail-open helper, supervisor, bed control) are written behind `#ifdef __QNX__` but have **not** been compiled with `qcc` or run on QEMU: that is pending P1, the myQNX SDP 8.0 licence bring-up. Nothing in this repository claims a result measured on QNX.

## Architecture and safety invariant

The invariant: **an alarm is never silently dropped.**

- The raw bedside alarm is preserved. Quiet Ward only adds a second opinion.
- A candidate suppression is not discarded: it is queued on `/dev/quietward` for a nurse ACK, with timeout escalation.
- Every fault path fails open to `CONFIRM`: detector crash, decision timeout, a 17th pending alarm, detached shared memory, and process restart (journal replay).

![Architecture](docs/workflow.svg)

Diagram source: [`docs/workflow.svg`](docs/workflow.svg). Process layout and QNX primitive mapping: [`src/qnx/README.md`](src/qnx/README.md). Stress and fail-open verification status (host passes, QEMU pending): [`submission/g3/stress-checklist.md`](submission/g3/stress-checklist.md).

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
make fuzz            # 1000 seeded parser mutations (stdlib Python)
python3 scripts/host_shift_replay.py   # six-record replay report
python3 scripts/render_demo_plots.py   # docs/plots/*.svg from the six real records
```

`make gateway-test` currently reports `qw_gateway_test: 529 checks, 0 failed`.

**`make qnx` (not run yet).** On an x86_64 Linux host with the QNX SDP 8.0 environment sourced, `make qnx` cross-builds the `#ifdef __QNX__` processes under `src/qnx/` with `qcc`; `make qnx-check` only verifies that `qcc` is on the path. These targets are uncompiled and untested on the host until SDP 8.0 P1 bring-up, so no QNX build or QEMU result is claimed.

`make test` additionally runs the pytest suite (needs `pytest` and `numpy`).

**Full 750-record evaluation.** The raw records are not stored in the repository. `scripts/fetch_physionet.sh` downloads the PhysioNet/CinC 2015 training set and converts it to `data/bin/` (needs `wfdb` and `numpy` in `.venv`). Then `make eval-train` reproduces the train table above. The held-out split has a limited run budget (`eval/heldout_runs.log`); do not spend run 3.

## Data and attribution

All signals are real, de-identified ICU records from the PhysioNet/Computing in Cardiology Challenge 2015, redistributed under the **Open Data Commons Attribution License (ODC-BY) 1.0**.

> Clifford GD, Silva I, Moody B, Li Q, Kella D, Shahin A, Kooistra T, Perry D, Mark RG (2015). The PhysioNet/Computing in Cardiology Challenge 2015: Reducing False Arrhythmia Alarms in the ICU. Computing in Cardiology 42:273-276. DOI [10.13026/c9fg-a467](https://doi.org/10.13026/c9fg-a467)

Only the six `data/demo/` records are redistributed, converted from WFDB to the `.qw` format. See [`LICENSE`](LICENSE), [`NOTICE`](NOTICE), [`DATA_CARD.md`](DATA_CARD.md), `data/demo/LICENSE-ODC-BY.txt` and [`JUDGES.md`](JUDGES.md).
