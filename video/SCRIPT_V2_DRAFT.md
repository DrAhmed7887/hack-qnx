# Quiet Ward 2.0: demo video storyboard (draft v2)

Target length 2:55 (hard limit: under 3:00). Eight strictly sequential scenes, no overlap. Variant B framing: a review and escalation gateway that never silently drops an alarm, with honest held-out results.

**Allowed facts only:** openFDA report 10055677, the real `data/demo/` records, the measured tables in `eval/results/`, the host test results, the PhysioNet 2015 winner scores with their caveat, and the ODC-BY 1.0 attribution. **No clinical figures from unsigned claims.** The rules file is `pending_signoff`, so no on-screen line may imply clinical validation.

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

---

## S01 · 0:00-0:10 · An alarm that never sounded

**Screen:** the public openFDA record for report 10055677, text highlighted, nothing else.

**On-screen quote (verbatim, all caps as published):**
"THE CUSTOMER REPORTED THAT THE EXTERNAL ALARM LAMP FOR NICU BED 4 DID NOT LIGHT AND NO ALARM SOUNDED ON THE INTELLIVUE MX500 PATIENT MONITOR. THE DEVICE WAS REPORTED TO BE IN USE ON A PATIENT, BUT NO ADVERSE EVENT TO PATIENT OR USER WAS REPORTED."
Caption: "openFDA MAUDE, report 10055677, manufacturer malfunction report, received 14 May 2020."

**VO:** "This is a real device report: an alarm that never sounded. Any alarm system has to answer one question first: what happens when something goes wrong?"

*Re-fetch the record before recording and keep the quote exact. Do not describe this as a patient harm event: the report says none was reported.*

## S02 · 0:10-0:30 · The one rule

**Screen:** `docs/workflow.svg`, with the amber fail-open path highlighted last.

**VO:** "Quiet Ward is a second opinion on bedside alarms, built on QNX. One rule: it never silently drops an alarm. The bedside alarm stays. If a second channel contradicts it, the alarm goes to a review queue for a nurse. If the detector stalls, crashes, overflows, loses shared memory or restarts, the answer is always confirm."

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

**Screen:** terminal: `python3 scripts/fuzz_qw.py --count 1000` finishing with "1000/1000 passed"; then `make && make gateway-test && make reentrant-test && make demo-verify` running offline; then the first lines of `submission/g3/stress-checklist.md` showing HOST PASS and QEMU PENDING columns.

**VO:** "Bad input fails closed. A thousand seeded mutations of record files and configs: no crashes, every rejection explained. The ring buffer and the alarm queue are tested under load. The matrix says exactly what passed on the host and what still needs the QNX target. Everything builds offline from a fresh clone."

**Overlay:** "Fuzz: 1000/1000. Stress matrix: host passes and QEMU pending, both shown."

## S08 · 2:40-2:55 · Attribution card

**Screen:** static card, high contrast, readable for 15 s.

> **Data:** PhysioNet/Computing in Cardiology Challenge 2015, real de-identified ICU records, ODC-BY 1.0.
> Clifford GD, Silva I, Moody B, Li Q, Kella D, Shahin A, Kooistra T, Perry D, Mark RG (2015). Computing in Cardiology 42:273-276. DOI 10.13026/c9fg-a467
> **Research demo, not a medical device.** Clinical thresholds are pending sign-off.
> Quiet Ward 2.0 · QNX Global Hackathon · repo, JUDGES.md and DATA_CARD.md in the description.

**VO:** "Quiet Ward: a second opinion that can never silence the first. Real data, honest results, open source."

---

## Recording checklist

- [ ] Re-fetch openFDA report 10055677 and compare the quote word for word.
- [ ] Re-run `python3 scripts/host_shift_replay.py` and `make gateway-test` on the recording day; use those numbers.
- [ ] Held-out table matches `eval/results/` exactly (51.5%, FN 18 to 3).
- [ ] No on-screen text implies clinical validation, a medical device, or any figure outside the allowed list above.
- [ ] S05 shows only footage that exists. Label host versus QNX.
- [ ] Total runtime at or under 2:55.
