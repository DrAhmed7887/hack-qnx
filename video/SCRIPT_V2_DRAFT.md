# Quiet Ward 2.0: demo video storyboard (draft v2)

## Compliance Header

- **Target length:** `<= 2:55` (175 s, to be verified with `ffprobe`; Devpost hard limit `< 3:00`; eight strictly sequential scenes, no overlap)
- **YouTube visibility:** Public
- **Audio:** original voiceover only — zero copyrighted music (no BGM/soundtrack)
- **Language:** 100% English (on-screen text, captions, overlays, and narration)
- **Third-party product brands/logos:** none (monitor model omitted in S01; plain-text citations only for openFDA/PhysioNet — no openFDA, PhysioNet, or vendor logos on screen)
- **S01 evidence status:** `VERIFIED-REPO` (`docs/EVIDENCE.md`, `CLAIMS.md` row 18 — `mdr_report_key=10055677`, `date_received=20200514`, `mdr_text_key=191262317`, monitor model omitted)

Variant B framing: a review and escalation gateway that never silently drops an alarm, with honest held-out results and verified QNX 8.0 target execution.

**Allowed facts only:** openFDA report 10055677 (`docs/EVIDENCE.md`, `CLAIMS.md` row 18), the real `data/demo/` records, the measured tables in `eval/results/`, the host test results, the QNX 8.0 target logs in `submission/g3/{p1_qnx8_guest_20261125.log,qnx8_live_proof_20261125.log,g3_run_01..10.txt,slog2_run_01..10.txt}`, the PhysioNet 2015 winner scores with their caveat, and the ODC-BY 1.0 attribution. **No clinical figures from unsigned claims.** The rules file is `pending_signoff`, so no on-screen line may imply clinical validation.

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

*Fallback if S01 is skipped:* if you prefer not to open on a MAUDE malfunction card, omit S01 and open directly with S02 ("The one rule"), shifting `S02`–`S08` to a `2:45` total runtime (still `<= 2:55` and `< 3:00`).

---

## S01 · 0:00-0:10 · An alarm that never sounded

**Screen:** plain-text card displaying the omitted-model quote below on a neutral slide (no openFDA, PhysioNet, or vendor logos or webpage screenshots), text highlighted, nothing else.

**On-screen quote (from `docs/EVIDENCE.md`, `mdr_report_key=10055677`, `mdr_text_key=191262317`, monitor model omitted):**
"THE CUSTOMER REPORTED THAT THE EXTERNAL ALARM LAMP FOR NICU BED 4 DID NOT LIGHT AND NO ALARM SOUNDED ON THE [bedside patient monitor — model omitted]. THE DEVICE WAS REPORTED TO BE IN USE ON A PATIENT, BUT NO ADVERSE EVENT TO PATIENT OR USER WAS REPORTED."
Caption: "openFDA MAUDE, report 10055677, Malfunction report, received 14 May 2020 (model name omitted)."

**VO:** "This is a real device report: an alarm that never sounded. Any alarm system has to answer one question first: what happens when something goes wrong?"

*Verified from openFDA (`date_received=20200514`, `mdr_text_key=191262317`) and committed in `docs/EVIDENCE.md` (`CLAIMS.md` row 18 `VERIFIED-REPO`). Keep the third-party hardware brand/model explicitly omitted on screen (`[bedside patient monitor — model omitted]` and `"model name omitted"` in the caption) so no third-party product brand/logo is displayed in the public YouTube video, and do not describe this as a patient harm event.*

## S02 · 0:10-0:30 · The one rule

**Screen:** Light architectural canvas animating the QNX 8.0 microkernel topology (`docs/workflow.svg`): `qw_acq` -> `/dev/shmem/qw_ring_bed*` -> `qw_detector` -> `MsgSend`/`MsgReply` -> `qw_alarm_mgr` -> `/dev/quietward` + `slog2`, with the amber fail-open path highlighted last.

**VO:** "Quiet Ward is a second opinion on bedside alarms, built and verified on QNX Neutrino 8.0. One rule: it never silently drops an alarm. The bedside alarm stays. If a second channel contradicts it, the alarm queues on `/dev/quietward` for a nurse. If the detector stalls, crashes, overflows, or loses shared memory, the answer is always confirm."

**Overlay:** "Raw alarm preserved. Built on QNX 8.0. Every fault fails open to CONFIRM."

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

## S05 · 1:15-1:40 · Fail-open under fault on QNX 8.0

**Screen:** Real `QNX noname 8.0.0 x86_64` QEMU captures (`submission/g3/qnx8_live_proof_20261125.log`, `submission/g3/slog2_run_01.txt`, `submission/g3/g3_run_01..10.txt`): `pidin`, `/dev/quietward` JSONL + nurse ACK, `slay -s KILL qw_detector`, 500 ms watchdog latch `CONFIRM` (`failopen_active: 1`, `sev=CRIT`), supervisor respawn with new PID, standalone `qw_failopen` (`CONFIRM_ALL`), and 10/10 soak score (`230/230` checks, `0` failed).

**VO:** "On booted QNX 8.0, kill the detector with slay minus s KILL. The 500 millisecond watchdog expires, and the alarm manager latches CONFIRM (`failopen_active: 1`) and logs critical to slog2, while the supervisor respawns the detector cleanly. Across ten reset-state soak runs, all 230 checks pass."

**Overlay:** "QNX 8.0.0 x86_64 QEMU · 10/10 Soak Passed (230/230 Checks) · Fail-Open Latched."

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

**Screen:** Offline verification cards: `qcc -Vgcc_ntox86_64 -Wall -Wextra -Werror` (`0` warnings, 8 ELF binaries), `make fuzz` (`1000/1000 passed`), `make gateway-test` (`529 checks, 0 failed`, `torn=0`), `make reentrant-test` (`320` calls), ASan+UBSan clean, and `submission/g3/stress-checklist.md` (`HOST PASS` and `QEMU PASS`, plus honest untested edge rows).

**VO:** "Bad input fails closed. All eight QNX binaries compile cleanly with warnings treated as errors. One thousand seeded mutations crash zero parsers. Five hundred twenty-nine host gateway checks, ASan and UBSan, and our offline archive build all pass cleanly."

**Overlay:** "qcc: 0 warnings · Fuzz: 1000/1000 · Stress Matrix: Host & QEMU Verified."

---

## S08 · 2:40-2:55 · Attribution card

**Screen:** Dark closing card (zero logos/music), high contrast, readable for 15 s.

> **Data:** PhysioNet/Computing in Cardiology Challenge 2015 dataset (real de-identified ICU records, ODC-BY 1.0, DOI 10.13026/c9fg-a467).
> **Paper:** Clifford GD, Silva I, Moody B, Li Q, Kella D, Shahin A, Kooistra T, Perry D, Mark RG (2015). Computing in Cardiology 42:273-276.
> **Research demo, not a medical device.** Clinical thresholds are pending sign-off.
> Quiet Ward 2.0 · QNX Global Hackathon · repo, JUDGES.md and DATA_CARD.md in the description.

**VO:** "Quiet Ward 2.0: a second opinion that can never silence the first. Real ICU data, verified QNX Neutrino 8.0 fault tolerance, and open source."

---

## QNX 8.0 Scene Map (aligned with `submission/DEVPOST_DRAFT.md`)

| Scene | QNX 8.0 / Host Scope Shown | Exact Repo Files & Primitives | Verified Status |
|---|---|---|---|
| **S02** | Architecture & QNX 8.0 IPC/process layout | `src/qnx/qw_acq.c` (`ChannelCreate`, `ConnectAttach`, `MsgSendPulse`, `timer_create`, `shm_open`, `mmap`), `src/qnx/qw_ring.{h,c}` (`/qw_ring_bed%u`), `src/qnx/qw_detector.c` (`MsgReceive`, `MsgSend`, `MsgReply`), `src/qnx/qw_alarm_mgr.c` + `src/qnx/qw_resmgr.c` (`dispatch_create`, `resmgr_attach` on `/dev/quietward`), `src/qnx/qw_log.c` (`slog2_register`, `slog2f`), `src/qnx/qw_supervisor.c` (`posix_spawn`), `src/qnx/qw_failopen.c` | `VERIFIED-QNX8` (`p1_qnx8_guest_20261125.log`, `PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK`) |
| **S05** | Live QNX 8.0 target & fail-open under fault | `slay -s KILL qw_detector`, `--watchdog-ms` latch (`sev=CRIT`), `qw_bedctl 3` (`failopen_active:1`), `/dev/quietward` ACK, `qw_supervisor` respawn, standalone `qw_failopen` (`CONFIRM_ALL`), `p3_qnx_soak.sh -n 10` | `VERIFIED-QNX8` (`qnx8_live_proof_20261125.log`, `10/10` runs in `g3_run_01..10.txt`) |
| **S07** | Hostile input & offline reproducibility | `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror` (`0` warnings), `make && make gateway-test && make reentrant-test && make demo-verify && make fuzz` (`1000/1000`), `submission/g3/stress-checklist.md` (`HOST PASS` + `QEMU PASS`) | `VERIFIED-HOST` + `VERIFIED-QNX8` |

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

- [x] Re-fetched openFDA report 10055677 (`date_received=20200514`, `mdr_text_key=191262317`) into `docs/EVIDENCE.md`; render S01 as a recreated plain-text card (never an unredacted openFDA webpage screenshot), keeping the hardware brand/model explicitly marked `[bedside patient monitor — model omitted]` in the quote and `"model name omitted"` in the caption.
- [ ] Re-run `python3 scripts/host_shift_replay.py` and `make gateway-test` on the recording day; if any number changes, update `submission/g3/` and `CLAIMS.md` first before recording.
- [ ] Held-out table matches `eval/results/` exactly (51.46% rounded to 51.5% in the 1-decimal table, FN 18 to 3).
- [ ] No on-screen text implies clinical validation, a medical device, or any figure outside the allowed list above.
- [ ] S05 shows only footage that exists. Label host versus QNX.
- [ ] Target length `<= 2:55` (175 s, to be verified with `ffprobe`; Devpost hard limit `< 3:00`); YouTube visibility set to Public; 100% English; zero music; zero third-party product brands/logos.
- [ ] `[PLANNED FOR VIDEO PRODUCTION]` Verify eight strictly sequential scene blocks (`S01`–`S08`) with zero overlap and run `ffmpeg` `blackdetect`, `silencedetect`, `volumedetect` checks before upload.
