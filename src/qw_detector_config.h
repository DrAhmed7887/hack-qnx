/* qw_detector_config.h -- loader for config/detector.json's fixed shape.
 *
 * config/detector.json holds detector/signal-processing ENGINEERING
 * constants (pulse-prominence fraction, signal-quality weights, the
 * minimum-expected-pulse-rate floor used to decide whether "a pulse is
 * present" in a window, and the maximum trusted interval coefficient of
 * variation used before trusting a derived heart rate). None of these
 * are clinical thresholds -- those live in rules/alarm_rules.json and are
 * read via rules.h. This file's status is "engineering_default", never
 * "pending_signoff" or a clinical status.
 *
 * Like rules.c, this is NOT a general JSON parser: it understands only
 * the fixed shape below and fails closed (nonzero return) if a field it
 * depends on is missing or out of a sane range, rather than silently
 * substituting a hard-coded default.
 *
 * {
 *   "status": "engineering_default",
 *   "note": "...",
 *   "pulse_detector": { "min_prominence_frac": 0.15 },
 *   "signal_quality": { "activity_weight": 0.8, "regularity_weight": 0.2 },
 *   "pulse_presence": { "min_expected_bpm_floor": 30.0, "min_expected_fraction": 0.5 },
 *   "suppression_contradiction": { "max_trusted_interval_cv": 0.3 }
 * }
 */
#ifndef QW_DETECTOR_CONFIG_H
#define QW_DETECTOR_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double min_prominence_frac;      /* pulse_detector.min_prominence_frac; must be in (0, 1) */
    double activity_weight;          /* signal_quality.activity_weight; must be in [0, 1] */
    double regularity_weight;        /* signal_quality.regularity_weight; must be in [0, 1] */
    double min_expected_bpm_floor;   /* pulse_presence.min_expected_bpm_floor; must be > 0 */
    double min_expected_fraction;    /* pulse_presence.min_expected_fraction; must be in (0, 1] */
    double max_trusted_interval_cv;  /* suppression_contradiction.max_trusted_interval_cv; must be > 0 */
} qw_detector_config_t;

/* Load and validate config/detector.json (the fixed shape described
 * above) into *out. Returns 0 on success, nonzero on failure: bad path,
 * malformed JSON, a missing expected field, or any field outside its
 * documented range (fail closed -- never silently substitutes a
 * hard-coded default). On failure *out is left in an unspecified state
 * and must not be used. */
int qw_detector_config_load(const char *path, qw_detector_config_t *out);

#ifdef __cplusplus
}
#endif

#endif /* QW_DETECTOR_CONFIG_H */
