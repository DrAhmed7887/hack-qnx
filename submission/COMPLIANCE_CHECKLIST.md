# Compliance checklist: QNX 8.0 Global Hackathon (Devpost emails)

Status as of 25 Nov 2026. PASS = proven by a committed file. PENDING-OWNER-CLICK = only the repo owner can do it (GitHub, YouTube, or Devpost web UI). QNX 8.0 cross-compile (`qcc -Vgcc_ntox86_64`), QEMU x86_64 boot, live proof, and 10/10 G3 soak are all verified in `submission/g3/`.

| # | Email item | Status | Proof in the repo | What is left |
|---|---|---|---|---|
| 1 | Naming QNX without showing it | PASS (QNX 8.0 built & verified) | "Where QNX 8.0 Appears (Reviewer Map)" in `README.md`, `submission/DEVPOST_DRAFT.md`, and `src/qnx/README.md`, backed by verbatim QNX 8.0 logs: `submission/g3/p1_qnx8_guest_20261125.log`, `submission/g3/qnx8_live_proof_20261125.log`, `submission/g3/g3_run_01..10.txt`, `submission/g3/slog2_run_01..10.txt` | None |
| 2 | Repo created during the submission period | PASS (API verified; repo currently `PRIVATE`) | `gh repo view DrAhmed7887/hack-qnx`: `createdAt: 2026-11-22T17:26:02Z` (after 2 Nov 2026); `submission/g3/git_birth_commit.txt` (first commit `d1d0bd6` on 22 Nov 2026) | Flip visibility to Public before G5 submit |
| 3 | No build and run instructions | PASS (Host + QNX 8.0 verified) | `README.md` section "QNX 8.0 Target, Toolchain & Build/Run Instructions" and `submission/DEVPOST_DRAFT.md`; `Makefile` (`make qnx-check`, `make qnx`, `0` warnings across 8 ELFs); `scripts/p3_qnx_soak.sh` (`10/10` runs passed); `.devcontainer/devcontainer.json`; offline host suite in `submission/g3/host_tests_20261124.log` | None |
| 4 | Open-source licence detected | PASS (API verified; repo currently `PRIVATE`) | `LICENSE` (Apache License 2.0) detected by GitHub (`gh repo view`: `licenseInfo.key: apache-2.0`); `NOTICE` + `DATA_CARD.md` + `data/demo/LICENSE-ODC-BY.txt` (ODC-BY 1.0 data attribution) | Flip visibility to Public before G5 submit |
| 5 | Not a fork, new project | PASS (API verified) | `gh repo view DrAhmed7887/hack-qnx`: `isFork: false`; stated in `README.md`, `JUDGES.md`, and `submission/DEVPOST_DRAFT.md` | None |
| 6 | Video (<3 min, public YouTube, shows project working, no copyrighted music or third-party trademarks without permission) | READY-FOR-UPLOAD (`169.6 s` 1080p V3 rendered & QC-passed) | `video/SCRIPT_V2_DRAFT.md`: `S00`–`S08` (`2:49.6`, `<3:00`), original ElevenLabs Daniel VO + original ElevenLabs Music Creator-license instrumental bed (zero third-party copyrighted music), S01 MAUDE `10055677` with monitor model omitted, QNX 8.0 Scene Map (`S02`, `S05`, `S07`) backed by `submission/g3/` | Watch rendered MP4, upload to YouTube as Public |
| 7 | Language of materials (All English) | PENDING-OWNER-CLICK (non-ASCII byte scan committed; owner English read-through before Public) | Non-ASCII byte scan (`submission/g3/english_scan.txt`, scanning all 98 text files; matched non-ASCII bytes are typographic symbols `—`, `–`, `§`, `·`, `→`, curly quotes) + agent review (`reviewer=docs-sub,verifier`, `date=2026-11-24`; ASCII non-English text cannot be ruled out by byte scan alone) of tracked docs/code/scripts (`README.md`, `JUDGES.md`, `CLAIMS.md`, `submission/DEVPOST_DRAFT.md`, `video/SCRIPT_V2_DRAFT.md`, `docs/`, `src/`, `scripts/`, `rules/`, `.kiro/`) | Owner read-through before flipping repo to Public |
| 8 | Start submission form today | PENDING-OWNER-CLICK | `submission/DEVPOST_DRAFT.md` updated with verified QNX 8.0 outputs and ready to paste | Click: solve Cloudflare check in Chrome on `https://qnx.devpost.com/` and save Draft / Submit |

Evidence rule: every measured benchmark, test result, QNX target capture, and external clinical/leaderboard figure in the public text maps to a file in `CLAIMS.md` (all 19 rows verified, including openFDA MAUDE `10055677` and QNX 8.0 `VERIFIED-QNX8`). Pre-existing `.kiro/` files (`.kiro/agents/qw-builder.json`, `.kiro/steering/rules.md`, `.kiro/specs/quiet-ward/tasks.md`) inspected: 100% English, 0 secrets, 0 PII.

## Prepared GitHub About (paste into the repo settings)

Description (one line, no clinical claims):

> Quiet Ward 2.0: research demo of a fail-open second-review gateway for bedside alarms in C11 on QNX Neutrino 8.0 (IPC, /dev/quietward resmgr, slog2, 10/10 QEMU soak). Not a medical device.

Website: leave blank, or the Devpost project URL once it exists.

Topics: `qnx`, `qnx-neutrino`, `rtos`, `c11`, `medical-devices`, `patient-monitoring`, `alarm-fatigue`, `physionet`, `fail-safe`, `embedded`, `hackathon`

## Owner's remaining clicks (in order)

1. **Devpost (~5 min):** complete the browser check on `https://qnx.devpost.com/` and paste `submission/DEVPOST_DRAFT.md` (Category: Robotics & Medical, Bonus: Best use of QNX-specific Technologies).
2. **Watch & upload video:** inspect `/Users/ahmedzayed/QNX/quiet-ward-video/out/QUIET_WARD_2_DEMO_1080P.mp4` (`2:49.6` / `169.6 s`, `<3:00`, public YouTube, original ElevenLabs instrumental bed, zero third-party logos) and link it on Devpost.
3. **Clinical sign-off (optional/pending):** review and sign `rules/alarm_rules.json` if clinical claims are ever intended; otherwise keep `pending_signoff` + Variant B framing.
4. **Before G5 submit (Fri 11 Dec target):** switch GitHub visibility of `DrAhmed7887/hack-qnx` to Public and submit on `https://qnx.devpost.com/`.
