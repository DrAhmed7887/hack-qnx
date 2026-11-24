# Host shift replay (data/demo playlist)

**Real PhysioNet/CinC 2015 train-split ICU records; ODC-BY 1.0 (DOI 10.13026/c9fg-a467)**

- rules_status: `pending_signoff` (thresholds are engineering defaults, not signed clinical rules)
- Research demo, not a medical device. Six records, an engineering replay only: no accuracy claim.
- Regenerate: `make && python3 scripts/host_shift_replay.py`

| Record | Alarm | Ground truth | Decision | ecg_sqi | pulse_sqi |
|---|---|---|---|---|---|
| a109l | Asystole | false | SUPPRESS_TO_REVIEW | 0.91 | 0.98 |
| a653l | Asystole | true | CONFIRM | 0.95 | 0.97 |
| t116s | Tachycardia | false | CONFIRM | 0.89 | 0.96 |
| t106s | Tachycardia | true | CONFIRM | 0.96 | 0.85 |
| v188s | Ventricular_Tachycardia | true | CONFIRM | 0.97 | 0.96 |
| f450s | Ventricular_Flutter_Fib | true | CONFIRM | 0.92 | 0.06 |

## Aggregate

- CONFIRM: 5 of 6 (alarm stays on, no delay)
- SUPPRESS_TO_REVIEW: 1 of 6 (queued for nurse review, never discarded)
- True alarms placed in the review queue: 0
- False alarms still confirmed (conservative): 1

## Reasons (verbatim from build/qw)

- **a109l** (SUPPRESS_TO_REVIEW): SUPPRESS_TO_REVIEW: pulse present in trailing 5.0s (n=5, trailing_gap=0.12s, max_gap=1.02s < 3.20s) contradicts asystole; sqi=0.98, logged for nurse review (never discarded)
- **a653l** (CONFIRM): pulse does not contradict Asystole (pulse not continuous in trailing 5.0s (trail_count=3, trailing_gap=3.41s, max_gap=3.41s, limit 3.20s)); confirming (sqi=0.97)
- **t116s** (CONFIRM): pulse does not contradict Tachycardia (ecg max hr=160bpm exceeds threshold >140bpm (pulse max hr=71bpm)); confirming (sqi=0.97)
- **t106s** (CONFIRM): pulsatile channel sqi=0.85 below min_signal_quality=0.90; confirming Tachycardia
- **v188s** (CONFIRM): pulse does not contradict Ventricular_Tachycardia (ecg max hr=242bpm exceeds threshold >100bpm (pulse max hr=69bpm)); confirming (sqi=0.96)
- **f450s** (CONFIRM): Ventricular_Flutter_Fib is in suppression.never_suppress; confirming unconditionally (sqi=0.92)
