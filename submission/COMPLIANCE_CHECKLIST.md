# Compliance checklist: QNX 8.0 Global Hackathon (Devpost emails)

Status as of 24 Nov 2026. PASS = proven by a committed file. PENDING-AHMED-CLICK = only Ahmed can do it (GitHub, myQNX, YouTube, or Devpost web UI). Nothing here claims a QNX runtime result: the QNX processes are uncompiled and unbooted, pending P1 (myQNX SDP 8.0 licence).

| # | Email item | Status | Proof in the repo | What is left |
|---|---|---|---|---|
| 1 | Naming QNX without showing it | PASS (docs) / QNX output PENDING P1 | "Where QNX 8.0 Appears (Reviewer Map)" in `README.md`, `submission/DEVPOST_DRAFT.md`, and `src/qnx/README.md`: exact files (`src/qnx/qw_acq.c`, `qw_detector.c`, `qw_alarm_mgr.c`, `qw_failopen.c`, `qw_supervisor.c`, `qw_bedctl.c`, `qw_resmgr.c`, `qw_log.c`, `qw_ring.h`, `qw_ring.c`, `qw_ipc.h`, `qw_gateway_state.h`, `qw_gateway_state.c`, `qw_gateway_test.c`, `qw_resmgr.h`, `qw_log.h`, `scripts/p1_hello_ipc.c`, `scripts/p1_qplms_probe.sh`, `scripts/p1_tcg_jitter.c`, `Makefile`, `scripts/p3_qnx_soak.sh`, `.devcontainer/devcontainer.json`), primitives present in source, expected output, scenes S02, S05, S07 | Replace each `[PENDING P1: ...]` with a real capture after P1 QEMU boot |
| 2 | Repo created during the submission period | PENDING-AHMED-CLICK (first commit `d1d0bd6` in git history; GitHub UI check pending) | `submission/g3/git_birth_commit.txt` (first commit `d1d0bd6` on 22 Nov 2026; GitHub creation date / submission-period check pending owner click); stated in `README.md`, `JUDGES.md`, and `submission/DEVPOST_DRAFT.md` | Click: confirm creation date shows on GitHub (Insights or initial commit `d1d0bd6` on 22 Nov 2026) |
| 3 | No build and run instructions | PASS (docs) / QNX run PENDING P1 | `README.md` section "QNX 8.0 Target, Toolchain & Build/Run Instructions" (and concise mirror in `submission/DEVPOST_DRAFT.md`); `Makefile`; `scripts/p3_qnx_soak.sh`; `.devcontainer/devcontainer.json`; offline host commands verified | Run `source ~/qnx800/qnxsdp-env.sh && make qnx-check && make qnx` on x86_64 Linux after P1 and paste real output |
| 4 | Open-source licence detected | PASS in file / PENDING-AHMED-CLICK on GitHub | `LICENSE` (Apache License 2.0); `NOTICE` + `DATA_CARD.md` + `data/demo/LICENSE-ODC-BY.txt` (ODC-BY 1.0 data attribution) | Click: open the repo page and confirm GitHub About shows Apache-2.0 |
| 5 | Not a fork, new project | PASS (docs) / PENDING-AHMED-CLICK | `README.md`, `JUDGES.md`, and `submission/DEVPOST_DRAFT.md`: "new, non-fork repository" (`DrAhmed7887/hack-qnx`) | Click: confirm no "forked from" line under the repo name |
| 6 | Video problems (<3 min, public YouTube, shows project working, no copyrighted music or third-party trademarks without permission) | PENDING (script drafted; S01 MAUDE quote unsourced, video not yet recorded) / PENDING-AHMED-CLICK (record & upload) | `video/SCRIPT_V2_DRAFT.md`: compliance header specifies target `<= 2:55` (`<3:00`), YouTube Public, original spoken voiceover only (no BGM/soundtrack), S01 third-party monitor model omitted (`[... — model omitted]`), QNX 8.0 Scene Map (`S02`, `S05`, `S07`) | Re-fetch MAUDE 10055677 into `docs/EVIDENCE.md`, record/render video after P1, verify `<3:00` + zero music + `blackdetect`/`silencedetect`, upload to YouTube as Public |
| 7 | Language of materials (All English) | PASS | Non-ASCII character scan (`submission/g3/english_scan.txt`, detecting non-ASCII characters across all 98 scanned text files) + human review of tracked docs/code/scripts (`README.md`, `JUDGES.md`, `CLAIMS.md`, `submission/DEVPOST_DRAFT.md`, `video/SCRIPT_V2_DRAFT.md`, `docs/`, `src/`, `scripts/`, `rules/`) | None |
| 8 | Start submission form today | PENDING-AHMED-CLICK | `submission/DEVPOST_DRAFT.md` is ready to paste | Click: open `https://qnx.devpost.com/` and start the draft submission today |

Evidence rule: every measured benchmark, test result, and external reference figure in the public text maps to a file in `CLAIMS.md`. One open gap there: the openFDA MAUDE 10055677 wording has no committed source yet (`CLAIMS.md` row 18).

## Prepared GitHub About (paste into the repo settings)

Description (one line, no clinical claims):

> Quiet Ward 2.0: a research demo of a fail-open second-review gateway for bedside alarms, built as C11 with QNX 8.0 message passing, resource manager and slog2 (QNX build pending). Not a medical device.

Website: leave blank, or the Devpost project URL once it exists.

Topics: `qnx`, `qnx-neutrino`, `rtos`, `c11`, `medical-devices`, `patient-monitoring`, `alarm-fatigue`, `physionet`, `fail-safe`, `embedded`, `hackathon`

## Ahmed's remaining clicks (in order)

1. **Devpost (today, ~5 min):** open `https://qnx.devpost.com/` and start the draft submission form early; paste from `submission/DEVPOST_DRAFT.md` (Category: Robotics & Medical, Bonus: Best use of QNX-specific Technologies).
2. **P1 Gate (~10 min):** claim the free non-commercial QNX SDP 8.0 licence at `https://www.qnx.com/getqnx`, accept and deploy on x86_64 Linux (or `.devcontainer/devcontainer.json`), then run `make qnx-check`, `make qnx`, QEMU boot, and `sh scripts/p3_qnx_soak.sh`; replace every `[PENDING P1: ...]` with real captures.
3. **Clinical sign-off (optional/pending):** review and sign `rules/alarm_rules.json` if clinical claims are ever intended; otherwise keep `pending_signoff` + Variant B framing.
4. **Before G5 submit (Fri 11 Dec target):**
   - Re-fetch openFDA MAUDE report 10055677 and add the exact quote to `docs/EVIDENCE.md`.
   - Record/render the `<3:00` video from `video/SCRIPT_V2_DRAFT.md` and upload as **Public** on YouTube.
   - Review pre-existing `.kiro/*` agent files (`.kiro/agents/qw-builder.json`, `.kiro/steering/rules.md`, `.kiro/specs/quiet-ward/tasks.md`) for public-repo hygiene before making the repo Public.
   - GitHub repo About gear: paste the description and topics above, confirm `Apache-2.0` is detected and there is no "forked from" line, and switch visibility to Public before judging.
   - Finalize and submit on `https://qnx.devpost.com/`.
