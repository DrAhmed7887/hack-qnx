# CLAIMS (only these may appear in public text)

Every measured benchmark, test result, and external reference figure in `README.md`, `JUDGES.md`, and `submission/DEVPOST_DRAFT.md` maps to a committed file below (except row 18, which is `UNSOURCED IN REPO`). Every row citing a committed file uses plain relative paths (no wildcards or globs) verified on disk (`VERIFIED-REPO`, `VERIFIED-HOST`, `PENDING-AHMED-CLICK`, `PENDING P1`, `CAVEAT`); row 18 (`UNSOURCED IN REPO`) intentionally has `none committed` until re-fetched. Research demo, not a medical device; rules are `pending_signoff`, so no row is a clinical claim.

Status values: `VERIFIED-REPO` = backed directly by static files committed in this repository; `VERIFIED-HOST` = measured on the host-portable C11 or Python code and recorded in a committed file or log; `PENDING-AHMED-CLICK` = requires owner action in GitHub/Devpost/myQNX web UI; `PENDING P1` = needs a booted QNX 8.0 target (myQNX SDP 8.0 licence), nothing is claimed; `CAVEAT` = external reference benchmark cited strictly as non-comparable context; `UNSOURCED IN REPO` = external record not yet committed verbatim and blocked until re-fetched.

| # | Claim | Source file(s) | Status |
|---|---|---|---|
| 1 | Data: real, de-identified PhysioNet/CinC Challenge 2015 dataset (ODC-BY 1.0, DOI 10.13026/c9fg-a467); paper Clifford et al. (2015), Computing in Cardiology 42:273-276. Only six records redistributed | `DATA_CARD.md`, `NOTICE`, `data/demo/LICENSE-ODC-BY.txt`, `data/demo/MANIFEST.json` | VERIFIED-REPO |
| 2 | Code licence Apache 2.0; new non-fork repo DrAhmed7887/hack-qnx, first commit d1d0bd6 on 22 Nov 2026 (GitHub creation date / submission-period check pending owner click) | `LICENSE`, `submission/g3/git_birth_commit.txt`, `submission/COMPLIANCE_CHECKLIST.md` | PENDING-AHMED-CLICK |
| 3 | Train split N=523, Base (no suppression) 39.20% | `eval/results/train_fixed.json` | VERIFIED-HOST |
| 4 | Train v0-frozen 40.19%, FN=49 | `eval/results/train_v0-frozen.json` | VERIFIED-HOST |
| 5 | Train fixed 53.83%, FN=3; true-alarm recall 202/205 = 98.5%, Wilson 95% CI [95.8-99.5]; Kill Rule PASS | `eval/results/train_fixed.json`, `eval/results/train_v0-frozen.json`, `eval/report.py`, `submission/g3/eval_train_report.txt` | VERIFIED-HOST |
| 6 | Held-out N=227, Base 39.21% | `eval/results/heldout_fixed.json` | VERIFIED-HOST |
| 7 | Held-out v0-frozen 40.80%, FN=18 | `eval/results/heldout_v0-frozen.json` | VERIFIED-HOST |
| 8 | Held-out fixed 51.46%, FN=3; recall 86/89 = 96.6%, Wilson 95% CI [90.6-98.8] | `eval/results/heldout_fixed.json`, `eval/report.py`, `submission/g3/eval_heldout_report.txt` | VERIFIED-HOST |
| 9 | Held-out Ventricular_Tachycardia FN 11 to 0 (v0-frozen to fixed); Asystole FN 0 to 1 on N=37 Asystole records; Kill Rule FAIL, Variant B framing, no retune; run 3 of 3 reserved | `eval/results/heldout_v0-frozen.json`, `eval/results/heldout_fixed.json`, `eval/heldout_runs.log`, `submission/g3/eval_heldout_report.txt` | VERIFIED-HOST |
| 10 | Split definition shared by train and held-out | `eval/split.json` | VERIFIED-REPO |
| 11 | Six-record demo replay: 5 CONFIRM, 1 SUPPRESS_TO_REVIEW (a109l, false Asystole), 0 true alarms queued for review, t116s (false Tachycardia) still CONFIRM | `submission/g3/host_shift_review.json`, `submission/g3/host_shift_review.md` | VERIFIED-HOST |
| 12 | qw_gateway_test: 529 checks, 0 failed | `src/qnx/qw_gateway_test.c`, `submission/g3/stress-checklist.md`, `submission/g3/host_tests_20261124.log` | VERIFIED-HOST |
| 13 | qw_reentrant_test: 320 concurrent calls across 8 threads, all consistent | `src/qw_reentrant_test.c`, `submission/g3/stress-checklist.md`, `submission/g3/host_tests_20261124.log` | VERIFIED-HOST |
| 14 | Fuzzer: 1000/1000 seeded mutations handled, seed 20261124 | `scripts/fuzz_qw.py`, `submission/g3/stress-checklist.md`, `submission/g3/host_tests_20261124.log` | VERIFIED-HOST |
| 15 | Stress matrix: 14 of 16 rows HOST PASS (row 3 partial, row 7 pending); ASan+UBSan clean (0 diagnostics); zero paid APIs and offline fresh-clone host build/tests (stress-checklist.md row 14) | `submission/g3/stress-checklist.md`, `submission/g3/asan_ubsan_20261124.log`, `submission/g3/host_tests_20261124.log`, `Makefile` | VERIFIED-HOST |
| 16 | Plots are generated from the six real records (no synthetic waveforms) | `scripts/render_demo_plots.py`, `docs/plots/demo_overview.svg`, `data/demo/MANIFEST.json` | VERIFIED-HOST |
| 17 | Caveat: PhysioNet/CinC 2015 winners scored 81.39 (Event 1) and 85.04 (Event 2) on a different 500-record hidden test set; our local-split scores are not comparable | `docs/EVIDENCE.md` | CAVEAT |
| 18 | Caveat: openFDA MAUDE report 10055677 is a manufacturer malfunction report (NICU external alarm lamp did not light, no alarm sounded); no adverse event to patient or user reported; not presented as patient harm | none committed (external openFDA record) | UNSOURCED IN REPO |
| 19 | QNX runtime: six process sources written behind the QNX guard; NOT compiled with qcc, NOT booted on QEMU. No QNX build, pidin, slog2info or soak result exists | `src/qnx/README.md`, `submission/g3/stress-checklist.md`, `scripts/p3_qnx_soak.sh`, `Makefile` | PENDING P1 |

Anything not in this table is not claimable. If a number changes, change the file first, then this table, then the public text.
