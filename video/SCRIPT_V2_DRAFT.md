# Quiet Ward 2.0: demo video storyboard (draft v2)

## Compliance Header

- **Target length:** `<= 2:55` (175 s, to be verified with `ffprobe`; Devpost hard limit `< 3:00`; eight strictly sequential scenes, no overlap)
- **YouTube visibility:** Public
- **Audio:** original voiceover only — zero copyrighted music (no BGM/soundtrack)
- **Language:** 100% English (on-screen text, captions, overlays, and narration)
- **Third-party product brands/logos:** none (monitor model omitted in S01; plain-text citations only for openFDA/PhysioNet — no openFDA, PhysioNet, or vendor logos on screen)
- **S01 evidence status:** `[BLOCKED ON MAUDE 10055677 RE-FETCH — see CLAIMS.md row 18]` (do not display quote or date on screen until the re-fetched exact text is recorded in `docs/EVIDENCE.md`)

Variant B framing: a review and escalation gateway that never silently drops an alarm, with honest held-out results.

**Allowed facts only:** openFDA report 10055677 (BLOCKED until re-fetched into `docs/EVIDENCE.md` — see `CLAIMS.md` row 18), the real `data/demo/` records, the measured tables in `eval/results/`, the host test results, the PhysioNet 2015 winner scores with their caveat, and the ODC-BY 1.0 attribution. **No clinical figures from unsigned claims.** The rules file is `pending_signoff`, so no on-screen line may imply clinical validation.

**Always on screen (lower third, every scene):** "Research demo, not a medical device."

| Scene | Time | Length |
|---|---|---|
| S01 | 0:00-0:10 | 10 s |
| S02 | 0:10-0:30 | 20 s |
| S03 | 0:30-0:50 | 20 s |
| S04 | 0:50-1:15 | 25 s |
| S05 | 1:15-1:40 | 25 s |
| S06 | 1:40-2:10 | 30 s |
| S07 | 2:10-2:40 | 30 s |
| S08 | 2:40-2:55 | 15 s |

*Fallback if S01 stays blocked:* if openFDA MAUDE report 10055677 is not re-fetched and committed into `docs/EVIDENCE.md` before recording, omit S01 and open directly with S02 ("The one rule"), shifting `S02`–`S08` to a `2:45` total runtime (still `<= 2:55` and `< 3:00`).

---

## S01 · 0:00-0:10 · An alarm that never sounded `[BLOCKED ON MAUDE 10055677 RE-FETCH — see CLAIMS.md row 18]`

**Screen:** plain-text card displaying the omitted-model quote below on a neutral slide (no openFDA, PhysioNet, or vendor logos or webpage screenshots), text highlighted, nothing else.

**Draft on-screen quote `[PLACEHOLDER — BLOCKED ON MAUDE 10055677 RE-FETCH — see CLAIMS.md row 18]`:**
"[Insert verified openFDA MAUDE 10055677 quote from docs/EVIDENCE.md after re-fetch, keeping monitor model omitted as [... — model omitted]]."
Caption `[PLACEHOLDER]`: "openFDA MAUDE, report 10055677 — insert verified date/type from docs/EVIDENCE.md (model name omitted)."

**VO (blocked until MAUDE 10055677 re-fetch — see CLAIMS.md row 18):** "[Placeholder — confirm verified openFDA MAUDE 10055677 summary in docs/EVIDENCE.md before recording.] Any alarm system has to answer one question first: what happens when something goes wrong?"

*`[BLOCKED ON MAUDE 10055677 RE-FETCH — see CLAIMS.md row 18]` `CLAIMS.md` row 18 marks openFDA MAUDE report 10055677 as `UNSOURCED IN REPO`, so no quote text or receipt date appears here until re-fetched from openFDA and added verbatim to `docs/EVIDENCE.md`. Keep the third-party hardware brand/model explicitly omitted on screen (`[... — model omitted]` and `"model name omitted"` in the caption) so no third-party product brand/logo is displayed in the public YouTube video, and do not describe this as a patient harm event.*

## S02 · 0:10-0:30 · The one rule

**Screen:** `docs/workflow.svg`, with the amber fail-open path highlighted last.

**VO:** "Quiet Ward is a second opinion on bedside alarms, designed for QNX 8.0 with the QNX build pending. One rule: it never silently drops an alarm. The bedside alarm stays. If a second channel contradicts it, the alarm goes to a review queue for a nurse. If the detector stalls, crashes, overflows, loses shared memory or restarts, the answer is always confirm."

**Overlay:** "Raw alarm preserved. Every fault fails open to CONFIRM."

## S03 · 0:30-0:50 · A false alarm goes to review, not to the bin

**Screen:** terminal, `build/qw` on record a109l (Asystole), real ICU record from the PhysioNet/CinC 2015 training set. Show the JSON line: decision SUPPRESS_TO_REVIEW, pulse present in the trailing 5 s, "logged for nurse review (never discarded)".

**VO:** "Record a109l: a monitor says asystole, but the pulse channel shows a pulse right up to the end. Quiet Ward does not cancel anything. It queues the alarm for review, with the reason attached."

**Overlay:** "Real ICU record, ODC-BY 1.0. a109l: false alarm (ground truth)."

## S04 · 0:50-1:15 · True alarms stay on

**Screen:** two terminal runs side by side, then the one-line replay summary from `submission/g3/host_shift_review.md`.
1. a653l (Asystole, true alarm): CONFIRM, trailing gap 3.41 s over the 3.20 s limit.
2. v188s (Ventricular tachycardia, true alarm): CONFIRM, ECG max heart rate 242 bpm.

**VO:** "Record a653l is a true asystole: the pulse really is gone, so it confirms. Record v188s is ventricular tachycardia: confirmed. Across the six demo records, five confirm and one goes to review, and no true alarm is queued for review."

**Overlay:** "Six-record replay: 5 CONFIRM, 1 SUPPRESS_TO_REVIEW, 0 true alarms in the queue. A demonstration, not an accuracy claim."

## S05 · 1:15-1:40 · Fail-open under fault

**Screen (use what exists on recording day, and label it):**
- If the QNX 8.0 QEMU boot is working: kill the detector process, show the alarm manager latch CONFIRM at the deadline, then `slog2info` lines.
- Otherwise (current state): the host test output, labelled "host-portable C11, not QNX": `make gateway-test` -> "529 checks, 0 failed", with the deadline-expiry and queue-overflow cases highlighted.

**VO (host version):** "The gateway logic is tested on its own. No decision within the deadline: latched confirm. The seventeenth pending alarm: confirm and a busy status. Detached shared memory: reported as detached, never as fresh data. The QNX processes themselves are the next step; this part runs on the host today."

**Overlay:** "Host test: 529 checks, 0 failed. QNX runtime pending SDK bring-up." Do not show a QEMU clip unless it was actually recorded.

## S06 · 1:40-2:10 · Honest results

**Screen:** the held-out table from `eval/results/heldout_fixed.json` and `heldout_v0-frozen.json`, then the Kill Rule line.

| Held-out, N=227 | Score | False negatives |
|---|---|---|
| Base (no suppression) | 39.2% | 0 |
| v0 | 40.8% | 18 |
| fixed | 51.5% | 3 |

**Then, on screen:** "Kill Rule: FAIL. Asystole misses 0 to 1 (37 records). Not retuned on held-out. So: Variant B." and, as a separate card: "PhysioNet/CinC 2015 winners: 81.39 / 85.04, on a different 500-record hidden test set. Not comparable."

**VO:** "On held-out records the fixed rules go from 18 missed true alarms to 3, and the score from 40.8 to 51.5 percent. But one alarm type, asystole, moved from zero misses to one, so our own Kill Rule fails. We did not retune on the held-out set. That is why this is a review gateway, not a suppressor. The 2015 challenge winners scored 81.39 and 85.04, but on a different 500-record hidden set, so these numbers are not comparable."

## S07 · 2:10-2:40 · Hostile input and reproducibility

**Screen:** terminal: `make fuzz` (runs `scripts/fuzz_qw.py --count 1000` via `.venv/bin/python` when present, else `python3`) finishing with "1000/1000 passed"; then `make && make gateway-test && make reentrant-test && make demo-verify` running offline; then the first lines of `submission/g3/stress-checklist.md` showing HOST PASS and QEMU PENDING columns.

**VO:** "Bad input fails closed. A thousand seeded mutations of record files and configs: no crashes, every rejection explained. The ring buffer and the alarm queue are tested under load. The matrix says exactly what passed on the host and what still needs the QNX target. The host build and checks run from a clean git archive export without .git or .venv, with no network-using commands."

**Overlay:** "Fuzz: 1000/1000. Stress matrix: host passes and QEMU pending, both shown."

## S08 · 2:40-2:55 · Attribution card

**Screen:** static card, high contrast, readable for 15 s.

> **Data:** PhysioNet/Computing in Cardiology Challenge 2015 dataset (real de-identified ICU records, ODC-BY 1.0, DOI 10.13026/c9fg-a467).
> **Paper:** Clifford GD, Silva I, Moody B, Li Q, Kella D, Shahin A, Kooistra T, Perry D, Mark RG (2015). Computing in Cardiology 42:273-276.
> **Research demo, not a medical device.** Clinical thresholds are pending sign-off.
> Quiet Ward 2.0 · QNX Global Hackathon · repo, JUDGES.md and DATA_CARD.md in the description.

**VO:** "Quiet Ward: a second opinion that can never silence the first. Real data, honest results, open source."

---

## QNX 8.0 Scene Map (aligned with `submission/DEVPOST_DRAFT.md`)

| Scene | QNX 8.0 / Host Scope Shown | Exact Repo Files & Primitives | Label / Runtime Honesty Rule |
|---|---|---|---|
| **S02** | Architecture & QNX 8.0 IPC/process layout (`docs/workflow.svg`) | `src/qnx/qw_acq.c` (`ChannelCreate`, `ConnectAttach`, `MsgSendPulse`, `timer_create`, `shm_open`, `mmap`), `src/qnx/qw_ring.{h,c}` (seqlock ring `/qw_ring_bed%u`), `src/qnx/qw_detector.c` (`MsgReceive`, `MsgSend`, `MsgReply`), `src/qnx/qw_alarm_mgr.c` + `src/qnx/qw_resmgr.c` (`dispatch_create`, `resmgr_attach` on `/dev/quietward`), `src/qnx/qw_log.c` (`slog2_register`, `slog2f`), `src/qnx/qw_supervisor.c` (`posix_spawn`); no `QPLMS` config committed (probe script `scripts/p1_qplms_probe.sh` only; fallback `src/qnx/qw_supervisor.c`) + `src/qnx/qw_failopen.c` | Present as the QNX 8.0 architecture; note runtime execution waits on P1 (`myQNX SDP 8.0`) unless captured before recording |
| **S05** | Fail-open under fault (QNX / host demo) | **If P1 QNX 8.0 QEMU boot is captured:** `slay -s KILL qw_detector`, `--watchdog-ms` latch, `qw_bedctl 3`, `slog2info -b quietward`, `pidin`. **Otherwise (current host demo):** `make gateway-test` (`qw_gateway_test: 529 checks, 0 failed`, documented in `submission/g3/stress-checklist.md` rows 1, 4, 5, 13 covering deadline expiry, 17th pending `QW_ST_BUSY`, `QW_ST_DETACHED`, and restart journal replay) | Explicitly label `"host-portable C11, not QNX"` unless real QNX 8.0 QEMU footage was recorded (`[PENDING P1: ...]`) |
| **S07** | Hostile input & offline reproducibility | Offline fresh-export host suite (`make && make gateway-test && make reentrant-test && make demo-verify && make fuzz` — `make fuzz` runs `scripts/fuzz_qw.py --count 1000` via `.venv/bin/python` when present, else `python3` -> `1000/1000 passed`), `submission/g3/stress-checklist.md` (`HOST PASS` vs `QEMU PENDING`), plus `Makefile` (`make qnx` with `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror -lm -lslog2`) and `scripts/p3_qnx_soak.sh` | Show both `HOST PASS` and `QEMU PENDING` columns clearly; zero fabricated QNX build or soak results |

---

## Video production gate checks `[PLANNED FOR VIDEO PRODUCTION]`

- **Scope note:** No video project or rendered `.mp4` is committed in this repository; the checks below are the planned production gates when assembling the video externally.
- **Planned sequential structure:** Eight strictly sequential scene blocks (`S01`–`S08`), zero overlap, total runtime `<= 2:55` (or `2:45` if S01 is omitted; Devpost hard limit `< 3:00`).
- **Audio & voiceover policy:** Original spoken English voiceover track only; zero background music, zero third-party audio samples.
- **Planned post-render `ffmpeg` QA gates (run externally before upload):**
  - `blackdetect`: verify zero unintended black frames inside or between scene transitions (`ffmpeg -i render.mp4 -vf blackdetect=d=0.1:pix_th=0.10 -an -f null -`).
  - `silencedetect`: verify no unintended dead-air gaps (`ffmpeg -i render.mp4 -af silencedetect=n=-45dB:d=1.5 -f null -`).
  - `volumedetect`: verify consistent narration level and zero clipping (`max_volume < 0.0 dB`; `ffmpeg -i render.mp4 -af volumedetect -f null -`).

---

## Recording checklist

- [ ] `[BLOCKED ON MAUDE 10055677 RE-FETCH — see CLAIMS.md row 18]` Re-fetch openFDA report 10055677, add the exact quote to `docs/EVIDENCE.md`, render S01 as a recreated plain-text card (never an unredacted openFDA webpage screenshot), and compare all non-omitted wording word for word, keeping the hardware brand/model explicitly marked `[... — model omitted]` in the quote and `"model name omitted"` in the caption.
- [ ] Re-run `python3 scripts/host_shift_replay.py` and `make gateway-test` on the recording day; if any number changes, update `submission/g3/` and `CLAIMS.md` first before recording.
- [ ] Held-out table matches `eval/results/` exactly (51.5%, FN 18 to 3).
- [ ] No on-screen text implies clinical validation, a medical device, or any figure outside the allowed list above.
- [ ] S05 shows only footage that exists. Label host versus QNX.
- [ ] Target length `<= 2:55` (175 s, to be verified with `ffprobe`; Devpost hard limit `< 3:00`); YouTube visibility set to Public; 100% English; zero music; zero third-party product brands/logos.
- [ ] `[PLANNED FOR VIDEO PRODUCTION]` Verify eight strictly sequential scene blocks (`S01`–`S08`) with zero overlap and run `ffmpeg` `blackdetect`, `silencedetect`, `volumedetect` checks before upload.
