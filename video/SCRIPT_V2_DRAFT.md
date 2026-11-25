# Quiet Ward 2.0: demo video storyboard (draft v2)

## Compliance Header

- **Target length:** `2:46` (`165.77 s` / `4973 frames @ 30fps`; verified `<= 2:55`, Devpost hard limit `< 3:00`; eight strictly sequential scenes, zero overlap)
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
| S01 | 0:00-0:13.7 | 13.7 s |
| S02 | 0:13.7-0:33.6 | 19.9 s |
| S03 | 0:33.6-0:53.9 | 20.3 s |
| S04 | 0:53.9-1:17.7 | 23.8 s |
| S05 | 1:17.7-1:41.8 | 24.0 s |
| S06 | 1:41.8-2:05.8 | 24.0 s |
| S07 | 2:05.8-2:25.5 | 19.7 s |
| S08 | 2:25.5-2:45.8 | 20.3 s |

*Fallback if S01 is skipped:* if you prefer not to open on a MAUDE malfunction card, omit S01 and open directly with S02 ("The one rule"), shifting `S02`–`S08` to a `2:45` total runtime (still `<= 2:55` and `< 3:00`).

---

## S01 · 0:00-0:10 · An alarm that never sounded

**Screen:** plain-text card displaying the omitted-model quote below on a neutral slide (no openFDA, PhysioNet, or vendor logos or webpage screenshots), text highlighted, nothing else.

**On-screen quote (from `docs/EVIDENCE.md`, `mdr_report_key=10055677`, `mdr_text_key=191262317`, monitor model omitted):**
"THE CUSTOMER REPORTED THAT THE EXTERNAL ALARM LAMP FOR NICU BED 4 DID NOT LIGHT AND NO ALARM SOUNDED ON THE [bedside patient monitor — model omitted]. THE DEVICE WAS REPORTED TO BE IN USE ON A PATIENT, BUT NO ADVERSE EVENT TO PATIENT OR USER WAS REPORTED."
Caption: "openFDA MAUDE, report 10055677, Malfunction report, received 14 May 2020 (model name omitted)."

**VO:** "In FDA report 1005 5677, a bedside monitor's alarm never sounded. Any real clinical gateway must answer one question first: what happens when something goes wrong?"

*Verified from openFDA (`date_received=20200514`, `mdr_text_key=191262317`) and committed in `docs/EVIDENCE.md` (`CLAIMS.md` row 18 `VERIFIED-REPO`). Keep the third-party hardware brand/model explicitly omitted on screen (`[bedside patient monitor — model omitted]` and `"model name omitted"` in the caption) so no third-party product brand/logo is displayed in the public YouTube video, and do not describe this as a patient harm event.*

## S02 · 0:10-0:30 · The one rule

**Screen:** Light architectural canvas animating the QNX 8.0 microkernel topology (`docs/workflow.svg`): `qw_acq` -> `/dev/shmem/qw_ring_bed*` -> `qw_detector` -> `MsgSend`/`MsgReply` -> `qw_alarm_mgr` -> `/dev/quietward` + `slog2`, with the amber fail-open path highlighted last.

**VO:** "Quiet Ward is a secondary review gateway built on QNX Neutrino 8.0. One rule: never silently drop an alarm. Multi-lead signals stream over lock-free shared memory. If the detector stalls, crashes, or overflows, the alarm manager latches confirm."

**Overlay:** "Raw alarm preserved. Built on QNX 8.0. Every fault fails open to CONFIRM."

## S03 · 0:30-0:50 · A false alarm goes to review, not to the bin

**Screen:** terminal, `build/qw` on record a109l (Asystole), real ICU record from the PhysioNet/CinC 2015 training set. Show the JSON line: decision SUPPRESS_TO_REVIEW, pulse present in the trailing 5 s, "logged for nurse review (never discarded)".

**VO:** "Real ICU record a109l: the monitor calls asystole, yet the pulse channel beats steadily through the final five seconds at 0.98 signal quality. Quiet Ward keeps the bedside alarm intact and queues the event on `/dev/quietward` for nurse review."

**Overlay:** "Real ICU record, ODC-BY 1.0. a109l: false alarm (ground truth)."

## S04 · 0:50-1:15 · True alarms stay on

**Screen:** two terminal runs side by side, then the one-line replay summary from `submission/g3/host_shift_review.md`.
1. a653l (Asystole, true alarm): CONFIRM, trailing gap 3.41 s over the 3.20 s limit.
2. v188s (Ventricular tachycardia, true alarm): CONFIRM, ECG max heart rate 242 bpm.

**VO:** "True alarms stay confirmed. Record a653l loses pulse continuity for 3.41 seconds, confirming asystole. Record v188s hits ventricular tachycardia at 242 beats per minute: confirmed. Across all six demo records, zero true alarms queue for review."

**Overlay:** "Six-record replay: 5 CONFIRM, 1 SUPPRESS_TO_REVIEW, 0 true alarms in the queue. A demonstration, not an accuracy claim."

## S05 · 1:15-1:40 · Fail-open under fault on QNX 8.0

**Screen:** Real `QNX noname 8.0.0 x86_64` QEMU captures (`submission/g3/qnx8_live_proof_20261125.log`, `submission/g3/slog2_run_01.txt`, `submission/g3/g3_run_01..10.txt`): `pidin`, `/dev/quietward` JSONL + nurse ACK, `slay -s KILL qw_detector`, 500 ms watchdog latch `CONFIRM` (`failopen_active: 1`, `sev=CRIT`), supervisor respawn with new PID, standalone `qw_failopen` (`CONFIRM_ALL`), and 10/10 soak score (`250/250` checks, `0` failed).

**VO:** "Live on QNX 8.0: nurse acknowledgement of sequence 201 writes cleanly to `/dev/quietward`. Kill the detector with slay -s KILL: within 525 milliseconds, the watchdog expires, latches confirm, logs critical to slog2, and the supervisor respawns a fresh detector. Ten soak runs: all 250 checks pass."

**Overlay:** "QNX 8.0.0 x86_64 QEMU · 10/10 Soak Passed (250/250 Checks) · Fail-Open Latched."

## S06 · 1:40-2:10 · Honest results

**Screen:** the held-out table from `eval/results/heldout_fixed.json` and `heldout_v0-frozen.json`, then the Kill Rule line.

| Held-out, N=227 | Score | False negatives |
|---|---|---|
| Base (no suppression) | 39.2% | 0 |
| v0 | 40.8% | 18 |
| fixed | 51.5% | 3 |

**Then, on screen:** "Kill Rule: FAIL. Asystole misses 0 to 1 (37 records). Not retuned on held-out. So: Variant B." and, as a separate card: "PhysioNet/CinC 2015 winners: 81.39 / 85.04, on a different 500-record hidden test set. Not comparable."

**VO:** "Honest held-out results on 227 records: fixed rules raise Challenge score from 40.8 to 51.5 percent, cutting false negatives from 18 to 3. Yet asystole misses moved from zero to one across 37 records, breaking our Kill Rule. So we do not retune: we ship Variant B."

## S07 · 2:10-2:40 · Hostile input and reproducibility

**Screen:** Offline verification cards: `qcc -Vgcc_ntox86_64 -Wall -Wextra -Werror` (`0` warnings, 8 ELF binaries), `make fuzz` (`1000/1000 passed`), `make gateway-test` (`529 checks, 0 failed`, `torn=0`), `make reentrant-test` (`320` calls), ASan+UBSan clean, and `submission/g3/stress-checklist.md` (`HOST PASS` and `QEMU PASS`, plus honest untested edge rows).

**VO:** "All eight QNX C11 binaries compile under qcc with zero warnings. One thousand seeded fuzz mutations crash zero parsers. 529 host gateway checks, ASan, UBSan, and our offline archive build all pass cleanly."

**Overlay:** "qcc: 0 warnings · Fuzz: 1000/1000 · Stress Matrix: Host & QEMU Verified."

---

## S08 · 2:40-2:55 · Attribution card

**Screen:** Dark closing card (zero logos/music), high contrast, readable for 15 s.

> **Data:** PhysioNet/Computing in Cardiology Challenge 2015 dataset (real de-identified ICU records, ODC-BY 1.0, DOI 10.13026/c9fg-a467).
> **Paper:** Clifford GD, Silva I, Moody B, Li Q, Kella D, Shahin A, Kooistra T, Perry D, Mark RG (2015). Computing in Cardiology 42:273-276.
> **Research demo, not a medical device.** Clinical thresholds are pending sign-off.
> Quiet Ward 2.0 · QNX Global Hackathon · repo, JUDGES.md and DATA_CARD.md in the description.

**VO:** "Real PhysioNet 2015 ICU records under ODC-BY 1.0. Thresholds remain pending clinical sign-off: a research demo on QNX Neutrino 8.0. Quiet Ward 2.0: a second opinion that can never silence the first."

---

## QNX 8.0 Scene Map (aligned with `submission/DEVPOST_DRAFT.md`)

| Scene | QNX 8.0 / Host Scope Shown | Exact Repo Files & Primitives | Verified Status |
|---|---|---|---|
| **S02** | Architecture & QNX 8.0 IPC/process layout | `src/qnx/qw_acq.c` (`ChannelCreate`, `ConnectAttach`, `MsgSendPulse`, `timer_create`, `shm_open`, `mmap`), `src/qnx/qw_ring.{h,c}` (`/qw_ring_bed%u`), `src/qnx/qw_detector.c` (`MsgReceive`, `MsgSend`, `MsgReply`), `src/qnx/qw_alarm_mgr.c` + `src/qnx/qw_resmgr.c` (`dispatch_create`, `resmgr_attach` on `/dev/quietward`), `src/qnx/qw_log.c` (`slog2_register`, `slog2f`), `src/qnx/qw_supervisor.c` (`posix_spawn`), `src/qnx/qw_failopen.c` | `VERIFIED-QNX8` (`p1_qnx8_guest_20261125.log`, `PROBE_RESULT=USE_QW_SUPERVISOR_FALLBACK`) |
| **S05** | Live QNX 8.0 target & fail-open under fault | `slay -s KILL qw_detector`, `--watchdog-ms` latch (`sev=CRIT`), `qw_bedctl 3` (`failopen_active:1`), `/dev/quietward` ACK, `qw_supervisor` respawn, standalone `qw_failopen` (`CONFIRM_ALL`), `p3_qnx_soak.sh -n 10` | `VERIFIED-QNX8` (`qnx8_live_proof_20261125.log`, `10/10` runs in `g3_run_01..10.txt`) |
| **S07** | Hostile input & offline reproducibility | `qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror` (`0` warnings), `make && make gateway-test && make reentrant-test && make demo-verify && make fuzz` (`1000/1000`), `submission/g3/stress-checklist.md` (`HOST PASS` + `QEMU PASS`) | `VERIFIED-HOST` + `VERIFIED-QNX8` |

---

## Video production gate checks (verified externally in `/Users/ahmedzayed/QNX/quiet-ward-video/`)

- **Scope note:** No video project, rendered `.mp4`, or `node_modules` is committed in this repository; the finished render lives externally at `/Users/ahmedzayed/QNX/quiet-ward-video/out/QUIET_WARD_2_DEMO_1080P.mp4` (`165.8 s`, `1920x1080`, `30 fps`, `15.07 MB`).
- **Sequential structure:** Eight strictly sequential scene blocks (`S01`–`S08`), zero overlap, total runtime `2:45.8` (`<= 2:55`; Devpost hard limit `< 3:00`).
- **Audio & voiceover policy:** Original spoken English voiceover track only (ElevenLabs Daniel, mastered to `-16.0 LUFS` / `-1.0 dBTP`); zero background music, zero third-party audio samples.
- **Post-render `ffmpeg` QA gates (`qc_video.py`, `qc_pass: true`):**
  - `blackdetect`: `0` unintended black segments (`blackdetect=d=0.1:pix_th=0.10`).
  - `silencedetect`: `0` dead-air gaps `>= 2.0 s` (`silencedetect=n=-45dB:d=2.0`).
  - `ebur128`: integrated loudness `-16.0 LUFS`, true peak `-1.0 dBTP`, full decode check clean.

---

## Recording checklist

- [x] Re-fetched openFDA report 10055677 (`date_received=20200514`, `mdr_text_key=191262317`) into `docs/EVIDENCE.md`; render S01 as a recreated plain-text card (never an unredacted openFDA webpage screenshot), keeping the hardware brand/model explicitly marked `[bedside patient monitor — model omitted]` in the quote and `"model name omitted"` in the caption.
- [x] Verified `make test` (`105 passed`), `make gateway-test` (`529 checks, 0 failed`), `make reentrant-test` (`320` calls), and `make demo-verify` on render day; QNX 8.0 target logs in `submission/g3/` verified (`250/250` soak checks).
- [x] Held-out table matches `eval/results/` exactly (51.46% rounded to 51.5% in the 1-decimal table, FN 18 to 3, `Kill Rule: FAIL`).
- [x] No on-screen text implies clinical validation, a medical device, or any figure outside the allowed list above; `"Research demo, not a medical device."` visible on every frame.
- [x] S05 shows only QNX 8.0 target outputs that exist (`qnx8_live_proof_20261125.log`, `g3_run_01..10.txt`, `slog2_run_01..10.txt`).
- [x] Mastered length `165.8 s` (`<= 2:55`, `< 3:00` Devpost limit); 100% English; zero music; zero third-party product brands/logos; `[ ]` set YouTube visibility to Public when uploading.
- [x] Eight strictly sequential scene blocks (`S01`–`S08`) verified with `qc_video.py` (`blackdetect`, `silencedetect`, `ebur128`, full decode clean).
