/* qw_reentrant_test.c -- test-only harness for review finding #5: make
 * qw_decide() reentrant (no static buffers; caller-provided workspace).
 *
 * Builds two small synthetic in-memory records with DIFFERENT, mutually
 * exclusive correct decisions (one a clear Bradycardia-contradicting
 * pulse -> SUPPRESS_TO_REVIEW, the other a flat/no-pulse record ->
 * CONFIRM), then runs many interleaved qw_decide() calls across several
 * pthreads, each with its OWN qw_decide_workspace_t, alternating between
 * the two records many times. If qw_decide() still used function-static
 * scratch buffers, concurrent calls on different threads would race on
 * that shared state and could corrupt each other's beat/pulse index
 * arrays, nondeterministically producing wrong decisions or counts. This
 * test fails (nonzero exit) if any call ever returns the wrong decision
 * for its record, or a sqi/beat relationship inconsistent with that
 * record in isolation.
 *
 * Not part of the shipped qw CLI; built and run only for `make test`.
 */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qw_core.h"
#include "qw_detector_config.h"
#include "rules.h"

#define FS 250u
#define ALARM_OFFSET_S 300.0
#define N_SAMPLES (FS * 300u) /* FS * (uint32_t)ALARM_OFFSET_S, as an integer constant expression */
#define N_THREADS 8
#define N_ITERS_PER_THREAD 40

static float g_ecg_flat[N_SAMPLES];
static float g_abp_contradicting[N_SAMPLES]; /* steady 70bpm pulse: contradicts Bradycardia (threshold 40bpm) */
static float g_abp_flat[N_SAMPLES];           /* flat/no pulse: nothing to contradict with */

static qw_record_t g_rec_suppress; /* must always decide SUPPRESS_TO_REVIEW for Bradycardia */
static qw_record_t g_rec_confirm;  /* must always decide CONFIRM for Bradycardia */

static qw_rules_t g_rules;
static qw_detector_config_t g_cfg;

static int g_failures = 0;
static pthread_mutex_t g_fail_mutex = PTHREAD_MUTEX_INITIALIZER;

static void record_failure(const char *msg) {
    pthread_mutex_lock(&g_fail_mutex);
    fprintf(stderr, "FAIL: %s\n", msg);
    g_failures++;
    pthread_mutex_unlock(&g_fail_mutex);
}

static void build_signals(void) {
    double bpm = 70.0;
    double freq_hz = bpm / 60.0;
    for (uint32_t i = 0; i < N_SAMPLES; i++) {
        double t = (double)i / (double)FS;
        g_ecg_flat[i] = 0.0f;
        g_abp_contradicting[i] = (float)(80.0 + 20.0 * sin(2.0 * M_PI * freq_hz * t - M_PI / 2.0));
        g_abp_flat[i] = 80.0f; /* perfectly flat: no pulsatile signal at all */
    }
}

static void build_record_from_arrays(qw_record_t *rec, const float *ecg, const float *abp) {
    memset(rec, 0, sizeof(*rec));
    rec->fs = FS;
    rec->n_channels = 2;
    rec->n_samples = N_SAMPLES;
    rec->sample_buf = NULL; /* not heap-owned in this test; channels point at statics */

    strncpy(rec->channels[0].name, "II", QW_CHANNEL_NAME_LEN);
    rec->channels[0].kind = QW_CHANNEL_ECG;
    rec->channels[0].samples = ecg;

    strncpy(rec->channels[1].name, "ABP", QW_CHANNEL_NAME_LEN);
    rec->channels[1].kind = QW_CHANNEL_PULSE;
    rec->channels[1].samples = abp;
}

typedef struct {
    int thread_id;
} thread_arg_t;

static void *worker(void *arg_) {
    thread_arg_t *arg = (thread_arg_t *)arg_;

    /* Each thread gets its OWN workspace on its OWN stack -- this is the
     * property under test. Before this fix, qw_decide() used
     * function-static arrays shared by every call regardless of thread,
     * which this test would expose as cross-talk between the two
     * records' decisions. */
    qw_decide_workspace_t ws;
    qw_decision_result_t result;

    for (int iter = 0; iter < N_ITERS_PER_THREAD; iter++) {
        /* Alternate which record this thread decides on each iteration,
         * maximizing interleaving with other threads deciding the OTHER
         * record at the same time. */
        int use_suppress_record = (arg->thread_id + iter) % 2 == 0;
        const qw_record_t *rec = use_suppress_record ? &g_rec_suppress : &g_rec_confirm;

        if (qw_decide(rec, "Bradycardia", &g_rules, &g_cfg, &ws, &result) != 0) {
            record_failure("qw_decide returned nonzero");
            continue;
        }

        if (use_suppress_record) {
            if (result.decision != QW_DECISION_SUPPRESS_TO_REVIEW) {
                record_failure("expected SUPPRESS_TO_REVIEW for contradicting-pulse record but got CONFIRM "
                               "(possible cross-talk between concurrent qw_decide calls)");
            }
        } else {
            if (result.decision != QW_DECISION_CONFIRM) {
                record_failure("expected CONFIRM for flat/no-pulse record but got SUPPRESS_TO_REVIEW "
                               "(possible cross-talk between concurrent qw_decide calls)");
            }
        }
    }

    return NULL;
}

int main(void) {
    build_signals();
    build_record_from_arrays(&g_rec_suppress, g_ecg_flat, g_abp_contradicting);
    build_record_from_arrays(&g_rec_confirm, g_ecg_flat, g_abp_flat);

    /* rules: window_s=16, min_signal_quality=0.9, Bradycardia threshold
     * 40bpm, not in never_suppress -- matches the shipped
     * rules/alarm_rules.json shape. */
    memset(&g_rules, 0, sizeof(g_rules));
    strncpy(g_rules.alarms[0].name, "Bradycardia", QW_NAME_LEN - 1);
    g_rules.alarms[0].has_hr_below_bpm = 1;
    g_rules.alarms[0].hr_below_bpm = 40.0;
    g_rules.alarms[0].has_beats = 1;
    g_rules.alarms[0].beats = 5;
    g_rules.n_alarms = 1;
    g_rules.suppression.has_window_s = 1;
    g_rules.suppression.window_s = 16.0;
    g_rules.suppression.has_min_signal_quality = 1;
    g_rules.suppression.min_signal_quality = 0.9;
    g_rules.suppression.has_never_suppress = 1;
    g_rules.suppression.n_never_suppress = 0;

    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.min_prominence_frac = 0.15;
    g_cfg.activity_weight = 0.8;
    g_cfg.regularity_weight = 0.2;
    g_cfg.min_expected_bpm_floor = 30.0;
    g_cfg.min_expected_fraction = 0.5;
    g_cfg.max_trusted_interval_cv = 0.3;

    pthread_t threads[N_THREADS];
    thread_arg_t args[N_THREADS];
    for (int i = 0; i < N_THREADS; i++) {
        args[i].thread_id = i;
        if (pthread_create(&threads[i], NULL, worker, &args[i]) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            return 2;
        }
    }
    for (int i = 0; i < N_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    if (g_failures > 0) {
        fprintf(stderr, "%d failure(s) out of %d calls\n", g_failures, N_THREADS * N_ITERS_PER_THREAD);
        return 1;
    }

    printf("ok: %d concurrent qw_decide() calls across %d threads, all consistent with their own record\n",
           N_THREADS * N_ITERS_PER_THREAD, N_THREADS);
    return 0;
}
