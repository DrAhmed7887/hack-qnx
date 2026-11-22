/* qw_core.h -- Quiet Ward C11 core.
 *
 * Pure C11 + POSIX. No allocations in the per-window decision path after
 * qw_record_load() returns (qw_decide() and its helpers only read memory
 * already owned by the qw_record_t). This is required so the same core
 * builds and runs predictably on QNX 8.0 (qcc) as well as macOS (host
 * eval). QNX-only code must live behind #ifdef __QNX__ in src/qnx/, never
 * here.
 *
 * All clinical thresholds (bpm cutoffs, beat counts, durations, signal
 * quality floor, suppression window) are read from rules/alarm_rules.json
 * via rules.h at runtime. Nothing here hard-codes a clinical default.
 */
#ifndef QW_CORE_H
#define QW_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QW_MAGIC "QW01"
#define QW_CHANNEL_NAME_LEN 16
#define QW_MAX_CHANNELS 8

typedef enum {
    QW_CHANNEL_UNKNOWN = 0,
    QW_CHANNEL_ECG,    /* II, V, III, I, MCL, aVR, aVF, aVL */
    QW_CHANNEL_PULSE,  /* PLETH, ABP -- pulsatile */
    QW_CHANNEL_OTHER   /* RESP etc: not used for beat/pulse detection */
} qw_channel_kind_t;

typedef struct {
    char name[QW_CHANNEL_NAME_LEN + 1]; /* NUL-terminated copy */
    qw_channel_kind_t kind;
    const float *samples; /* points into qw_record_t.sample_buf, length n_samples */
} qw_channel_t;

typedef struct {
    uint32_t fs;
    uint32_t n_channels;
    uint32_t n_samples;
    qw_channel_t channels[QW_MAX_CHANNELS];

    /* Backing storage. qw_record_load() allocates this once; everything
     * above just points into it. qw_record_free() releases it. */
    float *sample_buf;
} qw_record_t;

/* Load a .qw file written by scripts/convert.py. Returns 0 on success. */
int qw_record_load(const char *path, qw_record_t *rec);
void qw_record_free(qw_record_t *rec);

/* ---- beat / pulse detection ----
 * Both detectors write detected event sample-indices into out_idx (caller
 * supplied, capacity max_idx) and return the count written (<= max_idx).
 * Detection happens once per channel per record; no heap allocation.
 */

/* Simple Pan-Tompkins-like QRS detector for an ECG-like channel. */
size_t qw_detect_beats_ecg(const float *samples, uint32_t n_samples, uint32_t fs,
                            uint32_t *out_idx, size_t max_idx);

/* Pulse onset (foot-of-wave) detector for a pulsatile channel (ABP/PPG). */
size_t qw_detect_pulses(const float *samples, uint32_t n_samples, uint32_t fs,
                         uint32_t *out_idx, size_t max_idx);

/* Signal quality index in [0, 1] for one channel. Combines a flatline /
 * clipping check with beat-to-beat interval regularity. */
double qw_signal_quality(const float *samples, uint32_t n_samples, uint32_t fs,
                          const uint32_t *beat_idx, size_t n_beats);

/* ---- decision ---- */

typedef enum {
    QW_DECISION_CONFIRM = 0,
    QW_DECISION_SUPPRESS_TO_REVIEW = 1
} qw_decision_t;

#define QW_REASON_LEN 256

typedef struct {
    qw_decision_t decision;
    char reason[QW_REASON_LEN];
    double sqi; /* best available signal-quality index considered */
} qw_decision_result_t;

/* Decide CONFIRM vs SUPPRESS_TO_REVIEW for alarm_type raised at the end of
 * the record in rec, using thresholds from rules. Never returns
 * SUPPRESS_TO_REVIEW for an alarm_type listed in rules.suppression.never_suppress;
 * the alarm path never suppresses silently, so result->reason always
 * explains the decision, including why a suppression candidate was or was
 * not suppressed. */
int qw_decide(const qw_record_t *rec, const char *alarm_type, const qw_rules_t *rules,
              qw_decision_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* QW_CORE_H */
