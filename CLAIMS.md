# CLAIMS (only these may appear in public text)

Every measured result, test figure, and external clinical or leaderboard number in public text maps to a row below (row 18 is `UNSOURCED IN REPO`). Design parameters (such as the 50 ms watchdog tick, ~40-line supervisor, and 45-min QPLMS check) and external Devpost rules figures (`25% each`, `$3,500`) sit outside this measured-results table. Research demo, not a medical device; rules are `pending_signoff`, so no row is a clinical claim.

Status values: `VERIFIED-REPO` = backed directly by static files committed in this repository; `VERIFIED-HOST` = measured on the host-portable C11 or Python code and recorded in a committed file or log; `PENDING-OWNER-CLICK` = requires owner action in GitHub/Devpost/myQNX web UI; `PENDING P1` = needs a booted QNX 8.0 target (myQNX SDP 8.0 licence), nothing is claimed; `CAVEAT` = external reference benchmark cited strictly as non-comparable context; `UNSOURCED IN REPO` = external record not yet committed verbatim and blocked until re-fetched.

| # | Claim | Source file(s) | Status |
|---|---|---|---|
| 1 | Data: real, de-identified PhysioNet/CinC Challenge 2015 dataset (ODC-BY 1.0, DOI 10.13026/c9fg-a467); paper Clifford et al. (2015), Computing in Cardiology 42:273-276. Only six records redistributed | `DATA_CARD.md`, `NOTICE`, `data/demo/LICENSE-ODC-BY.txt`, `data/demo/MANIFEST.json` | VERIFIED-REPO |
| 2 | Code licence Apache 2.0; new non-fork repo DrAhmed7887/hack-qnx (pending owner click), first commit d1d0bd6 on 22 Nov 2026 (GitHub creation date / submission-period check pending owner click) | `LICENSE`, `submission/g3/git_birth_commit.txt`, `submission/COMPLIANCE_CHECKLIST.md` | PENDING-OWNER-CLICK |
| 3 | Train split N=523, Base (no suppression) 39.20% | `eval/results/train_fixed.json` | VERIFIED-HOST |
| 4 | Train v0-frozen 40.19%, FN=49 | `eval/results/train_v0-frozen.json` | VERIFIED-HOST |
| 5 | Train fixed 53.83%, FN=3; true-alarm recall 202/205 = 98.5%, Wilson 95% CI [95.8-99.5]; Kill Rule PASS | `eval/results/train_fixed.json`, `eval/results/train_v0-frozen.json`, `eval/report.py`, `submission/g3/eval_train_report.txt` | VERIFIED-HOST |
| 6 | Held-out N=227, Base 39.21% | `eval/results/heldout_fixed.json` | VERIFIED-HOST |
| 7 | Held-out v0-frozen 40.80%, FN=18 | `eval/results/heldout_v0-frozen.json` | VERIFIED-HOST |
| 8 | Held-out fixed 51.46%, FN=3; recall 86/89 = 96.6%, Wilson 95% CI [90.6-98.8] | `eval/results/heldout_fixed.json`, `eval/report.py`, `submission/g3/eval_heldout_report.txt` | VERIFIED-HOST |
| 9 | Held-out Ventricular_Tachycardia FN 11 to 0 (v0-frozen to fixed); Asystole FN 0 to 1 on N=37 Asystole records; Kill Rule FAIL, Variant B framing, no retune; run 3 of 3 reserved (`HELDOUT_MAX_RUNS = 3` in `eval/run_eval.py`) | `eval/results/heldout_v0-frozen.json`, `eval/results/heldout_fixed.json`, `eval/heldout_runs.log`, `eval/run_eval.py`, `submission/g3/eval_heldout_report.txt` | VERIFIED-HOST |
| 10 | Split definition shared by train and held-out: 750 records total (523 train + 227 held-out) | `eval/split.json` | VERIFIED-REPO |
| 11 | Six-record demo replay: 5 CONFIRM, 1 SUPPRESS_TO_REVIEW (a109l, false Asystole), 0 true alarms queued for review, a653l confirmed (trailing gap 3.41 s > 3.20 s limit), t116s (false Tachycardia, ECG max HR 160 bpm > 140 bpm) still CONFIRM, t106s confirmed (pulse SQI 0.85 < 0.90), v188s confirmed (ECG max HR 242 bpm > 100 bpm threshold), f450s confirmed (never_suppress) | `submission/g3/host_shift_review.json`, `submission/g3/host_shift_review.md` | VERIFIED-HOST |
| 12 | qw_gateway_test: 529 checks, 0 failed | `src/qnx/qw_gateway_test.c`, `submission/g3/stress-checklist.md`, `submission/g3/host_tests_20261124.log` | VERIFIED-HOST |
| 13 | qw_reentrant_test: 320 concurrent calls across 8 threads, all consistent | `src/qw_reentrant_test.c`, `submission/g3/stress-checklist.md`, `submission/g3/host_tests_20261124.log` | VERIFIED-HOST |
| 14 | Fuzzer: 1000/1000 seeded mutations handled, seed 20261124 | `scripts/fuzz_qw.py`, `submission/g3/stress-checklist.md`, `submission/g3/host_tests_20261124.log` | VERIFIED-HOST |
| 15 | Stress matrix: 14 of 16 rows HOST PASS (row 3 partial, row 7 pending); ASan+UBSan runtime checks clean (exit=0, 2>&1 captured in asan_ubsan_20261124.log; leak detection not run on macOS); commands ran from a clean git archive HEAD export without .git/.venv/.pytest_cache (NO_GIT_NO_VENV_OK in submission/g3/host_tests_20261124.log); suite contains no network-using commands or paid APIs (stress-checklist.md row 14) | `submission/g3/stress-checklist.md`, `submission/g3/asan_ubsan_20261124.log`, `submission/g3/host_tests_20261124.log`, `Makefile` | VERIFIED-HOST |
| 16 | Plots are generated from the six real records (no synthetic waveforms) | `scripts/render_demo_plots.py`, `docs/plots/demo_overview.svg`, `data/demo/MANIFEST.json` | VERIFIED-HOST |
| 17 | Caveat: PhysioNet/CinC 2015 winners scored 81.39 (Event 1) and 85.04 (Event 2) on a different 500-record hidden test set; our local-split scores are not comparable | `docs/EVIDENCE.md` | CAVEAT |
| 18 | Caveat: openFDA MAUDE report 10055677 is a manufacturer malfunction report (NICU external alarm lamp did not light, no alarm sounded); no adverse event to patient or user reported; not presented as patient harm | none committed (external openFDA record) | UNSOURCED IN REPO |
| 19 | QNX runtime: six process sources written behind the QNX guard; NOT compiled with qcc, NOT booted on QEMU. No QNX build, pidin, slog2info or soak result exists | `src/qnx/README.md`, `submission/g3/stress-checklist.md`, `scripts/p3_qnx_soak.sh`, `Makefile` | PENDING P1 |

Anything not in this table is not claimable. If a number changes, change the file first, then this table, then the public text.
