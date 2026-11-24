# EVIDENCE (Quiet Ward)
| Claim / rule | Source | Exact quote / number | Checked |
|---|---|---|---|
| 5 alarm definitions | physionet.org/content/challenge-2015/1.0.0 Table 1 | see rules/alarm_rules.json (verbatim) | 2026-11-22 |
| Score formula | same page | Score = (TP+TN)/(TP+TN+FP+5*FN) | 2026-11-22 |
| Published top scores | same page | "Event 1 ... score of 81.39 ... Event 2 ... 85.04" (hidden test set) | 2026-11-22 |
| Sample baseline approach | same page | BP/PPG quality check, suppress if pulse HR outside alarm range | 2026-11-22 |
| Licence | same page + mitdb page | Open Data Commons Attribution License v1.0 | 2026-11-22 |
| No macOS/ARM SDP host; APS discontinued in 8.0 | see Drive SPEC.md (qnx.com 8.0 docs) | — | 2026-11-19 |
| openFDA MAUDE report 10055677 | `api.fda.gov/device/event.json?search=mdr_report_key:10055677&limit=1` (`date_received=20200514`, `event_type=Malfunction`, `mdr_text_key=191262317`) | "THE CUSTOMER REPORTED THAT THE EXTERNAL ALARM LAMP FOR NICU BED 4 DID NOT LIGHT AND NO ALARM SOUNDED ON THE [bedside patient monitor — model omitted]. THE DEVICE WAS REPORTED TO BE IN USE ON A PATIENT, BUT NO ADVERSE EVENT TO PATIENT OR USER WAS REPORTED." (device brand/model omitted to avoid third-party hardware trademarks; not presented as patient harm) | 2026-11-25 |
