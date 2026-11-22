# Project rules (Quiet Ward) — binding for every agent
- No invented metrics. Every number in README, docs or slides comes from a committed script output under eval/results/. If unsure, write "unknown".
- Data: open data only (PhysioNet 2015 False Alarm Challenge, MIT-BIH; both ODC-BY 1.0, cite them) or seeded synthetic. Never PHI, never real patient identifiers beyond public record names.
- Public claims only from CLAIMS.md. Don't add claims; propose them in a PR comment.
- rules/alarm_rules.json is a clinical rule file, status pending_signoff. Never edit it. Thresholds are read from it at runtime, never hard-coded.
- The alarm path never suppresses silently: every suppressed alarm is logged with the reason, and the nurse acknowledgement is the human hold.
- C11 + POSIX for the core so it builds on macOS (host eval) and QNX 8.0 (qcc). QNX-only code lives behind `#ifdef __QNX__` in src/qnx/.
- QEMU timing is logical time; never claim real-time performance numbers from QEMU.
