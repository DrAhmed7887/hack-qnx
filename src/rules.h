/* rules.h -- tiny reader for rules/alarm_rules.json's fixed shape.
 *
 * This is NOT a general JSON parser. It only understands the exact
 * structure that rules/alarm_rules.json uses today:
 *
 * {
 *   "status": "...", "signed_by": ..., "signed_on": ..., "note": "...",
 *   "source": "...",
 *   "alarms": {
 *     "Asystole": {"definition": "...", "no_beat_s": 4},
 *     "Bradycardia": {"definition": "...", "hr_below_bpm": 40, "beats": 5},
 *     "Tachycardia": {"definition": "...", "hr_above_bpm": 140, "beats": 17},
 *     "Ventricular_Tachycardia": {"definition": "...", "beats": 5, "hr_above_bpm": 100},
 *     "Ventricular_Flutter_Fib": {"definition": "...", "duration_s": 4}
 *   },
 *   "suppression": {
 *     "status": "...",
 *     "rule": "...",
 *     "window_s": 16,
 *     "min_signal_quality": 0.9,
 *     "never_suppress": ["Ventricular_Flutter_Fib"]
 *   },
 *   "score": "..."
 * }
 *
 * All thresholds are read from this file at runtime; nothing here is
 * hard-coded as a clinical default. If a field this code expects is
 * missing, qw_rules_load() fails loudly rather than guessing a value.
 */
#ifndef QW_RULES_H
#define QW_RULES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QW_MAX_NEVER_SUPPRESS 16
#define QW_NAME_LEN 64

typedef struct {
    char name[QW_NAME_LEN];
    int has_no_beat_s;
    double no_beat_s;
    int has_hr_below_bpm;
    double hr_below_bpm;
    int has_hr_above_bpm;
    double hr_above_bpm;
    int has_beats;
    long beats;
    int has_duration_s;
    double duration_s;
} qw_alarm_def_t;

typedef struct {
    double window_s;
    double min_signal_quality;
    char never_suppress[QW_MAX_NEVER_SUPPRESS][QW_NAME_LEN];
    size_t n_never_suppress;
} qw_suppression_t;

typedef struct {
    qw_alarm_def_t alarms[8];
    size_t n_alarms;
    qw_suppression_t suppression;
} qw_rules_t;

/* Load and parse rules.json (the fixed shape described above) into *out.
 * Returns 0 on success, nonzero on failure (bad path, malformed JSON, or
 * missing expected fields). On failure *out is left in an unspecified
 * state and must not be used. */
int qw_rules_load(const char *path, qw_rules_t *out);

/* Look up an alarm definition by name. Returns NULL if not found. */
const qw_alarm_def_t *qw_rules_find_alarm(const qw_rules_t *rules, const char *alarm_type);

/* Returns 1 if alarm_type is in suppression.never_suppress, else 0. */
int qw_rules_never_suppress(const qw_rules_t *rules, const char *alarm_type);

#ifdef __cplusplus
}
#endif

#endif /* QW_RULES_H */
