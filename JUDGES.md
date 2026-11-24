# JUDGES.md — Quiet Ward 2.0 (QNX 8.0 Global Hackathon)

**Entry:** Quiet Ward 2.0 (`https://qnx.devpost.com/`, Category: **Robotics & Medical**, Default Bonus: **Best use of QNX-specific Technologies**).
**Judging rubric (`[V:C11]`, QNX Global Hackathon Official Rules §6):** Stage 1 pass/fail viability, then **4 equally weighted criteria (25% each)**. No LLM is required.

| Criterion (25% each) | Official Verbatim Rubric (`[V:C11]`) | Planned Proof Artifact(s) | Exact Verification Command / Target | Evidence IDs | Status |
|---|---|---|---|---|---|
| **Technological Implementation** | "How well is the project built, and how effectively does it use QNX 8.0? Does the code reflect real engineering effort and a working, non-trivial implementation that genuinely builds for or runs on QNX?" | `src/qw_core.{h,c}`, `src/rules.c`, `src/qw_detector_config.c`, `src/qnx/*` (`#ifdef __QNX__` after P1), `Makefile` (`make` + `make qnx`) | `make test`, ASan/UBSan + `qw_reentrant_test`, QEMU `pidin` + `slog2info` (P1/P3) | `[V:T10]`, `[V:T18]`–`[V:T22]`, `[V:C15]` | Host C11 active; QNX runtime gated on P1 licence |
| **Design** | "Does the project deliver a complete, coherent product experience not just a technical proof of concept?" | `<3 min` demo video (`S01`–`S08`), `/dev/quietward` nurse ACK queue + escalation workflow, real `data/demo/` trace pack + `DATA_CARD.md` | Fresh-clone offline `make && make test && make demo-verify`; logged-out video watch-through | `[V:D5]`, `[V:D6]`, `[V:C11]` | Core architecture aligned; video pending P3/P6 |
| **Potential Impact** | "Does the project make a credible, specific case for solving a real problem in a market QNX serves or wants to serve, and does it show why a real-time foundation like QNX is the right choice for it?" | Fail-open gateway invariant (never silently drop an alarm), UCSF alarm-fatigue evidence (pending clinical sign-off `[V:K1]`/`[V:K2]`), FDA MAUDE 10055677 `[V:D4]`, ICU market context | Open `CLAIMS.md` rows + kill-detector fail-open demo on QEMU (`S01`–`S02`) | `[V:D1]`, `[V:D4]`, `[V:C9]`, `[V:C10]` | Clinical claims `K1`–`K6` pending physician sign-off |
| **Innovation & AI Creativity** | "Is this a creative, non-obvious use of QNX and does the team show genuine understanding of the problem space they're working in?" | Multi-lead hemodynamic cross-check (ECG vs ABP/PLETH, pulse deficit VT guard, trailing-gap asystole), zero-drop `/dev/quietward` review gateway, deterministic ODC-BY 1.0 train/heldout eval discipline | `make eval-heldout` + `eval/report.py` (`0–100` Challenge score, per-alarm `TP/FP/FN/TN` + CIs + Kill Rule) | `[V:D1]`–`[V:D3]`, `[V:C11]` | Heldout eval Run 2 in Sprint 1 |

## Open Verification Backlog (Resolved Before G5 Public Submission)

| # | Item | Source | Owner / Target Gate | Status |
|---|---|---|---|---|
| 1 | Claim non-commercial QNX SDP 8.0 licence (`qnx.com/getqnx`) + read third-party cloud terms | P1 (`SPEC.md`) | Ahmed (`~10 min` click) → unlocks Codespaces `x86_64` QSTI | Open |
| 2 | Run 45-min QPLMS check inside booted QNX 8.0 (`plms` / `libplms`); select `QPLMS` vs `qw_supervisor` fallback | `research/QPLMS_NOTES.md` | P1 (`Hour 0–6` boot) | Open |
| 3 | Sign `rules/alarm_rules.json` + clinical claims `K1–K6` in `research/REVIEW.md` / `signoff.txt` | `rules/alarm_rules.json` | Dr. Ahmed (`⚑ Pending Dr. Ahmed's sign-off`) | Open |
| 4 | Verify ODC-BY 1.0 attribution (`data/demo/LICENSE-ODC-BY.txt` + `NOTICE` citing Clifford et al., 2015, DOI `10.13026/c9fg-a467`) | `[V:D5]`, `[V:D6]` | Created P4.1 / re-checked P7 | Tracked |
| 5 | Confirm single Bonus prize choice (`Best use of QNX-specific Technologies` $3,500 vs Conditional Post-G5 `Best Codelab/Tutorial` $3,500) | `[V:C12]`, `[V:C13]`, `[V:C14]` | Default: Best use of QNX-specific Tech; Ahmed confirms at P7 | Tracked |
| 6 | Check if MAUDE live hit counts need day-of refresh; keep `[V:D4]` wording exact | `[V:D4]` | P7 G5 check | Tracked |
