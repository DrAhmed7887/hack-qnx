/* qw_core.c -- Quiet Ward C11 core implementation. See qw_core.h. */
#include "qw_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---------------- loading ---------------- */

/* Format/sanity bounds for the .qw header fields. These are not clinical
 * thresholds -- they bound what a plausible monitoring device could ever
 * produce, so a corrupt or truncated file is rejected before we trust its
 * header enough to compute a malloc size from it. */
#define QW_MIN_FS 1u
#define QW_MAX_FS 10000u
#define QW_MIN_CHANNELS 1u
#define QW_MAX_CHANNELS_RANGE 16u /* documented contract bound; QW_MAX_CHANNELS further bounds storage */
/* PhysioNet 2015 Challenge convention used throughout qw_decide(): every
 * alarm fires at this many seconds into the record. 300*fs must fit in a
 * uint32_t sample-index without overflow for any fs this loader accepts. */
#define QW_ALARM_OFFSET_S_LOADER 300u

static qw_channel_kind_t classify_channel(const char *name) {
    /* ECG-like leads seen in the PhysioNet 2015 Challenge training set. */
    static const char *ecg_names[] = {
        "II", "I", "III", "MCL", "MCL1", "aVR", "aVF", "aVL", NULL
    };
    /* Pulsatile channels. */
    static const char *pulse_names[] = { "PLETH", "ABP", NULL };

    for (int i = 0; ecg_names[i] != NULL; i++) {
        if (strcmp(name, ecg_names[i]) == 0) return QW_CHANNEL_ECG;
    }
    for (int i = 0; pulse_names[i] != NULL; i++) {
        if (strcmp(name, pulse_names[i]) == 0) return QW_CHANNEL_PULSE;
    }
    return QW_CHANNEL_OTHER;
}

int qw_record_load(const char *path, qw_record_t *rec) {
    memset(rec, 0, sizeof(*rec));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "record error: %s: cannot open file\n", path);
        return -1;
    }

    struct stat st;
    if (fstat(fileno(f), &st) != 0) {
        fprintf(stderr, "record error: %s: fstat failed\n", path);
        fclose(f);
        return -1;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size < (4 + 12)) {
        fprintf(stderr, "record error: %s: file too small for header\n", path);
        fclose(f);
        return -1;
    }
    uint64_t file_size = (uint64_t)st.st_size;

    unsigned char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, QW_MAGIC, 4) != 0) {
        fprintf(stderr, "record error: %s: bad magic\n", path);
        fclose(f);
        return -1;
    }

    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12) {
        fprintf(stderr, "record error: %s: truncated header\n", path);
        fclose(f);
        return -1;
    }
    uint32_t fs = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    uint32_t n_channels = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    uint32_t n_samples = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8) | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);

    /* Range-check the header BEFORE trusting it for any size arithmetic
     * or malloc. fs and n_channels bound the device's plausible output;
     * n_channels is additionally bounded by QW_MAX_CHANNELS, the fixed
     * storage this struct provides. */
    if (fs < QW_MIN_FS || fs > QW_MAX_FS) {
        fprintf(stderr, "record error: %s: fs=%u out of range [%u, %u]\n", path, fs, QW_MIN_FS, QW_MAX_FS);
        fclose(f);
        return -1;
    }
    if (n_channels < QW_MIN_CHANNELS || n_channels > QW_MAX_CHANNELS_RANGE) {
        fprintf(stderr, "record error: %s: n_channels=%u out of range [%u, %u]\n",
                path, n_channels, QW_MIN_CHANNELS, QW_MAX_CHANNELS_RANGE);
        fclose(f);
        return -1;
    }
    if (n_channels > QW_MAX_CHANNELS) {
        fprintf(stderr, "record error: %s: n_channels=%u exceeds QW_MAX_CHANNELS=%u\n",
                path, n_channels, (unsigned)QW_MAX_CHANNELS);
        fclose(f);
        return -1;
    }

    /* 300*fs and window_s*fs (window_s comes from rules.json, read by the
     * caller, but the largest sane window is bounded by the record length
     * convention) must fit a uint32_t sample index without overflow.
     * fs<=10000 and the offset constant are both small enough that this
     * can't overflow in practice, but check explicitly rather than assume
     * -- this is the contract qw_decide() depends on. */
    {
        uint64_t offset_samples = (uint64_t)QW_ALARM_OFFSET_S_LOADER * (uint64_t)fs;
        if (offset_samples > (uint64_t)UINT32_MAX) {
            fprintf(stderr, "record error: %s: 300*fs=%llu overflows uint32_t for fs=%u\n",
                    path, (unsigned long long)offset_samples, fs);
            fclose(f);
            return -1;
        }
        /* window_s*fs: rules.json enforces window_s > 0 but places no
         * upper bound; guard generously up to 24h worth of samples at this
         * fs, which is far beyond any plausible suppression window and
         * still fits uint32_t for the fs range accepted above. */
        uint64_t max_window_s = 24u * 3600u;
        uint64_t window_samples_bound = max_window_s * (uint64_t)fs;
        if (window_samples_bound > (uint64_t)UINT32_MAX) {
            fprintf(stderr, "record error: %s: window_s*fs could overflow uint32_t for fs=%u\n", path, fs);
            fclose(f);
            return -1;
        }
    }

    /* Validate the header against the actual file size BEFORE allocating
     * the sample buffer: a corrupted or truncated header must not drive a
     * malloc sized from an untrustworthy n_samples. */
    uint64_t expected_channel_names = (uint64_t)n_channels * (uint64_t)QW_CHANNEL_NAME_LEN;
    uint64_t expected_samples = (uint64_t)n_channels * (uint64_t)n_samples;
    uint64_t expected_sample_bytes = expected_samples * (uint64_t)sizeof(float);
    uint64_t expected_total = 4 + 12 + expected_channel_names + expected_sample_bytes;

    if (expected_total != file_size) {
        fprintf(stderr,
                "record error: %s: header declares %" PRIu64 " bytes but file is %" PRIu64 " bytes\n",
                path, expected_total, file_size);
        fclose(f);
        return -1;
    }

    char names[QW_MAX_CHANNELS][QW_CHANNEL_NAME_LEN];
    for (uint32_t c = 0; c < n_channels; c++) {
        if (fread(names[c], 1, QW_CHANNEL_NAME_LEN, f) != QW_CHANNEL_NAME_LEN) {
            fprintf(stderr, "record error: %s: truncated channel name table\n", path);
            fclose(f);
            return -1;
        }
    }

    size_t total_samples = (size_t)n_channels * (size_t)n_samples;
    float *buf = NULL;
    if (total_samples > 0) {
        buf = (float *)malloc(total_samples * sizeof(float));
        if (!buf) {
            fprintf(stderr, "record error: %s: out of memory allocating sample buffer\n", path);
            fclose(f);
            return -1;
        }
        size_t n_read = fread(buf, sizeof(float), total_samples, f);
        if (n_read != total_samples) {
            fprintf(stderr, "record error: %s: truncated sample data\n", path);
            free(buf);
            fclose(f);
            return -1;
        }
    }
    fclose(f);

    /* Reject NaN/Inf samples outright: a detector fed non-finite input can
     * silently propagate garbage through the whole decision path (e.g. an
     * SQI of NaN compares false against every threshold, which look like
     * "low quality" but for the wrong reason, and is not something we want
     * to guess about on an alarm path). Count every bad sample before
     * failing so the stderr message gives a useful diagnostic. */
    size_t n_nonfinite = 0;
    for (size_t i = 0; i < total_samples; i++) {
        if (!isfinite((double)buf[i])) n_nonfinite++;
    }
    if (n_nonfinite > 0) {
        fprintf(stderr,
                "record error: %s: %zu of %zu samples are NaN or infinite; rejecting record\n",
                path, n_nonfinite, total_samples);
        free(buf);
        return -1;
    }

    rec->fs = fs;
    rec->n_channels = n_channels;
    rec->n_samples = n_samples;
    rec->sample_buf = buf;

    for (uint32_t c = 0; c < n_channels; c++) {
        char name_z[QW_CHANNEL_NAME_LEN + 1];
        memcpy(name_z, names[c], QW_CHANNEL_NAME_LEN);
        name_z[QW_CHANNEL_NAME_LEN] = '\0';
        /* names are NUL padded already; strnlen-safe copy */
        strncpy(rec->channels[c].name, name_z, QW_CHANNEL_NAME_LEN);
        rec->channels[c].name[QW_CHANNEL_NAME_LEN] = '\0';
        rec->channels[c].kind = classify_channel(rec->channels[c].name);
        rec->channels[c].samples = buf ? (buf + (size_t)c * n_samples) : NULL;
    }

    return 0;
}

void qw_record_free(qw_record_t *rec) {
    if (rec->sample_buf) {
        free(rec->sample_buf);
        rec->sample_buf = NULL;
    }
}

/* ---------------- beat detection (Pan-Tompkins-like) ---------------- */

/* Band-limited derivative + squaring + moving-window integration, then
 * adaptive-threshold peak picking with a refractory period. No dynamic
 * allocation: the caller supplies a fixed-size scratch via static
 * thread-local-free design (we use a bounded VLA-free loop instead,
 * processing in place with O(1) extra memory via running buffers sized
 * to a max window at compile time; see WINDOW_MAX). */

#define QW_BEAT_WINDOW_MAX_S 0.15 /* 150 ms integration window, standard PT choice */

size_t qw_detect_beats_ecg(const float *samples, uint32_t n_samples, uint32_t fs,
                            uint32_t *out_idx, size_t max_idx) {
    if (!samples || n_samples < 4 || fs == 0 || max_idx == 0) return 0;

    size_t win = (size_t)(QW_BEAT_WINDOW_MAX_S * (double)fs);
    if (win < 1) win = 1;

    /* Running moving-average integration of squared derivative, computed
     * on the fly with O(win) ring buffer state (constant extra memory,
     * independent of n_samples). */
    double ring_sum = 0.0;
    /* We cannot allocate a dynamic ring buffer without heap allocation in
     * the per-window path, but win is bounded (<= fs * 0.15, and fs is a
     * fixed small sampling rate for this device class), so a fixed-size
     * stack array sized generously covers any realistic fs. */
    enum { RING_CAP = 256 };
    double ring[RING_CAP];
    size_t ring_len = win < RING_CAP ? win : RING_CAP;
    for (size_t i = 0; i < ring_len; i++) ring[i] = 0.0;
    size_t ring_pos = 0;

    double prev = samples[0];
    double thresh = 0.0;
    double signal_peak = 0.0;
    int since_last = 1000000000;
    int min_rr = (int)(0.25 * (double)fs); /* refractory ~250ms, physiological upper HR bound */
    if (min_rr < 1) min_rr = 1;

    size_t n_found = 0;

    for (uint32_t i = 1; i < n_samples; i++) {
        double deriv = (double)samples[i] - prev;
        prev = samples[i];
        double sq = deriv * deriv;

        ring_sum -= ring[ring_pos];
        ring[ring_pos] = sq;
        ring_sum += sq;
        ring_pos = (ring_pos + 1) % ring_len;

        double integrated = ring_sum / (double)ring_len;

        /* adaptive threshold: exponential moving estimate of the peak */
        if (integrated > signal_peak) {
            signal_peak = 0.875 * signal_peak + 0.125 * integrated;
        } else {
            signal_peak = 0.995 * signal_peak;
        }
        thresh = 0.35 * signal_peak;

        since_last++;
        if (integrated > thresh && integrated > 1e-9 && since_last >= min_rr) {
            if (n_found < max_idx) {
                out_idx[n_found] = i;
            }
            n_found++;
            since_last = 0;
        }
    }

    return n_found < max_idx ? n_found : max_idx;
}

/* ---------------- pulse onset detection ---------------- */

size_t qw_detect_pulses(const float *samples, uint32_t n_samples, uint32_t fs,
                         uint32_t *out_idx, size_t max_idx,
                         const qw_detector_config_t *cfg) {
    if (!samples || n_samples < 4 || fs == 0 || max_idx == 0 || !cfg) return 0;

    /* Global min/max over the window gives a stable amplitude reference.
     * A slow-drifting local envelope (updated sample by sample) gets
     * dragged down during clipped or near-flat stretches, which makes tiny
     * baseline noise there look like full-height pulses; anchoring the
     * prominence requirement to the whole-window range avoids that. This
     * is one O(n) pass with O(1) extra memory, same as the rest of this
     * file. */
    double g_min = (double)samples[0];
    double g_max = (double)samples[0];
    for (uint32_t i = 1; i < n_samples; i++) {
        double x = (double)samples[i];
        if (x < g_min) g_min = x;
        if (x > g_max) g_max = x;
    }
    double g_range = g_max - g_min;
    if (g_range < 1e-9) return 0; /* flat channel: no pulses */

    /* Smooth with a short moving average (constant extra memory), then
     * find local maxima separated by at least a physiological minimum
     * pulse interval, requiring each candidate peak to rise a meaningful
     * fraction of the global range above the preceding local minimum. */
    enum { SMOOTH_CAP = 32 };
    size_t smooth_win = (size_t)(0.04 * (double)fs); /* 40 ms */
    if (smooth_win < 1) smooth_win = 1;
    if (smooth_win > SMOOTH_CAP) smooth_win = SMOOTH_CAP;

    double ring[SMOOTH_CAP];
    for (size_t i = 0; i < smooth_win; i++) ring[i] = (double)samples[0];
    size_t ring_pos = 0;
    double ring_sum = (double)samples[0] * (double)smooth_win;

    int min_rr = (int)(0.3 * (double)fs); /* <=200bpm cap on pulse rate */
    if (min_rr < 1) min_rr = 1;
    int since_last = 1000000000;

    double prev_smoothed = (double)samples[0];
    int rising = 0;
    double trough_since_last_peak = (double)samples[0];
    double min_prominence = cfg->min_prominence_frac * g_range; /* reject sub-fraction-of-range wiggles; see config/detector.json */

    size_t n_found = 0;

    for (uint32_t i = 0; i < n_samples; i++) {
        double x = (double)samples[i];
        ring_sum -= ring[ring_pos];
        ring[ring_pos] = x;
        ring_sum += x;
        ring_pos = (ring_pos + 1) % smooth_win;
        double smoothed = ring_sum / (double)smooth_win;

        if (smoothed < trough_since_last_peak) trough_since_last_peak = smoothed;

        since_last++;
        /* detect local maximum: was rising, now falling */
        int now_rising = smoothed > prev_smoothed;
        if (rising && !now_rising) {
            double peak_height = prev_smoothed - trough_since_last_peak;
            if (peak_height >= min_prominence && since_last >= min_rr) {
                if (n_found < max_idx) {
                    out_idx[n_found] = (i >= 1) ? (i - 1) : 0;
                }
                n_found++;
                since_last = 0;
                trough_since_last_peak = smoothed;
            }
        }
        rising = now_rising;
        prev_smoothed = smoothed;
    }

    return n_found < max_idx ? n_found : max_idx;
}

/* ---------------- signal quality index ---------------- */

double qw_signal_quality(const float *samples, uint32_t n_samples, uint32_t fs,
                          const uint32_t *beat_idx, size_t n_beats,
                          const qw_detector_config_t *cfg) {
    if (!samples || n_samples < 2 || !cfg) return 0.0;

    /* 1. Flatline / clipping check: fraction of samples that differ from
     *    their predecessor by more than a tiny epsilon (catches a
     *    disconnected lead stuck at one constant value), and fraction of
     *    samples sitting within 1% of the global range of either extreme
     *    (catches a lead that is saturated/clipped for long stretches but
     *    still has enough residual noise riding on the plateau to look
     *    "active" by the first check alone). */
    double eps = 1e-6;
    uint32_t n_diff = 0;
    double min_v = (double)samples[0];
    double max_v = (double)samples[0];
    for (uint32_t i = 1; i < n_samples; i++) {
        double d = fabs((double)samples[i] - (double)samples[i - 1]);
        if (d > eps) n_diff++;
        if (samples[i] < min_v) min_v = samples[i];
        if (samples[i] > max_v) max_v = samples[i];
    }
    double activity_frac = (double)n_diff / (double)(n_samples - 1);
    double range = max_v - min_v;
    double activity_score = activity_frac;
    if (range < 1e-6) {
        activity_score = 0.0; /* truly flat channel */
    } else {
        double clip_band = 0.01 * range;
        /* Genuine clipping/saturation shows up as a long RUN of samples
         * sitting at (near) one extreme, not merely samples that happen to
         * be close to the global min/max -- a smooth periodic waveform
         * like a healthy ABP/PPG trace naturally lingers near its peak and
         * trough for a fraction of each cycle without that being a quality
         * problem. Count samples that are part of a near-constant run of
         * at least QW_CLIP_RUN_S seconds near an extreme, where "near
         * constant" means staying within flat_eps of the value where the
         * run STARTED (cumulative deviation), not merely having a small
         * sample-to-sample delta -- a slowly drifting sine can have tiny
         * adjacent-sample deltas throughout its peak/trough yet still move
         * well away from its starting value over a fraction of a second,
         * which this distinguishes from genuine saturation. */
        const double QW_CLIP_RUN_S = 0.3;
        uint32_t clip_run_min = (uint32_t)(QW_CLIP_RUN_S * (double)fs);
        if (clip_run_min < 2) clip_run_min = 2;
        double flat_eps = 0.02 * range;

        uint32_t n_clipped = 0;
        uint32_t run_len = 1;
        double run_start_val = (double)samples[0];
        for (uint32_t i = 1; i <= n_samples; i++) {
            int continues = (i < n_samples) && (fabs((double)samples[i] - run_start_val) <= flat_eps);
            if (continues) {
                run_len++;
                continue;
            }
            /* run [i-run_len, i-1] just ended (or we're at the last sample) */
            if (run_len >= clip_run_min) {
                if (run_start_val >= max_v - clip_band || run_start_val <= min_v + clip_band) {
                    n_clipped += run_len;
                }
            }
            run_len = 1;
            run_start_val = (double)samples[i < n_samples ? i : n_samples - 1];
        }
        double clipped_frac = (double)n_clipped / (double)n_samples;
        activity_score *= (1.0 - clipped_frac);
    }

    /* 2. Beat-to-beat interval regularity: coefficient of variation of RR
     *    intervals, mapped to [0,1] (lower CV -> higher quality). Too few
     *    beats to judge regularity is treated as low-but-not-zero quality
     *    so a short noisy window doesn't look identical to a dead channel. */
    double regularity_score;
    if (n_beats < 3 || fs == 0) {
        regularity_score = 0.3;
    } else {
        double mean_rr = 0.0;
        for (size_t i = 1; i < n_beats; i++) {
            mean_rr += (double)(beat_idx[i] - beat_idx[i - 1]);
        }
        mean_rr /= (double)(n_beats - 1);
        double var = 0.0;
        for (size_t i = 1; i < n_beats; i++) {
            double d = (double)(beat_idx[i] - beat_idx[i - 1]) - mean_rr;
            var += d * d;
        }
        var /= (double)(n_beats - 1);
        double sd = sqrt(var);
        double cv = (mean_rr > 1e-9) ? (sd / mean_rr) : 1.0;
        /* Real pulsatile channels (ABP/PPG) commonly show RR-interval CV in
         * the 0.1-0.3 range from normal physiological variability plus a
         * sample or two of detector jitter; that is not itself a quality
         * problem. cv of 0 -> score 1.0; cv of 1.0 or more -> score ~0. */
        regularity_score = 1.0 - cv;
        if (regularity_score < 0.0) regularity_score = 0.0;
        if (regularity_score > 1.0) regularity_score = 1.0;
    }

    double sqi = cfg->activity_weight * activity_score + cfg->regularity_weight * regularity_score;
    if (sqi < 0.0) sqi = 0.0;
    if (sqi > 1.0) sqi = 1.0;
    return sqi;
}

/* ---------------- decision ---------------- */

static double heart_rate_from_beats(const uint32_t *idx, size_t n, uint32_t fs, size_t use_last_n) {
    if (n < 2 || fs == 0) return -1.0;
    size_t start = (use_last_n < n) ? (n - use_last_n) : 0;
    size_t count = n - start;
    if (count < 2) return -1.0;
    double span_samples = (double)(idx[n - 1] - idx[start]);
    if (span_samples <= 0.0) return -1.0;
    double span_s = span_samples / (double)fs;
    double n_intervals = (double)(count - 1);
    return 60.0 * n_intervals / span_s;
}

/* Coefficient of variation of inter-event intervals among the last
 * use_last_n events. Returns -1 if there are too few events to judge. A
 * pulse-rate estimate built on highly irregular intervals (e.g. a
 * detector missing beats intermittently on a noisy waveform) is not
 * trustworthy evidence for contradicting an alarm, even if the average
 * works out to a plausible-looking number. */
static double interval_cv_from_beats(const uint32_t *idx, size_t n, size_t use_last_n) {
    size_t start = (use_last_n < n) ? (n - use_last_n) : 0;
    size_t count = n - start;
    if (count < 4) return -1.0;
    double mean = 0.0;
    for (size_t i = start + 1; i < n; i++) mean += (double)(idx[i] - idx[i - 1]);
    mean /= (double)(count - 1);
    if (mean <= 1e-9) return -1.0;
    double var = 0.0;
    for (size_t i = start + 1; i < n; i++) {
        double d = (double)(idx[i] - idx[i - 1]) - mean;
        var += d * d;
    }
    var /= (double)(count - 1);
    return sqrt(var) / mean;
}

/* Does ANY pulse-channel event occur within the suppression window
 * immediately before the alarm onset? Used as the "pulsatile channel
 * contradicts the alarm" signal. alarm_sample is the sample index at
 * which the alarm fired (see QW_ALARM_OFFSET_S in qw_decide). cfg
 * supplies the minimum-expected-pulse-rate engineering constants. */
static int pulse_present_in_window(const uint32_t *pulse_idx, size_t n_pulses,
                                    uint32_t alarm_sample, uint32_t fs, double window_s,
                                    const qw_detector_config_t *cfg) {
    if (n_pulses == 0 || fs == 0) return 0;
    uint32_t window_samples = (uint32_t)(window_s * (double)fs);
    uint32_t window_start = (alarm_sample > window_samples) ? (alarm_sample - window_samples) : 0;
    size_t count_in_window = 0;
    for (size_t i = 0; i < n_pulses; i++) {
        if (pulse_idx[i] >= window_start && pulse_idx[i] <= alarm_sample) count_in_window++;
    }
    /* require a plausible number of pulses across the window, not just one
     * stray detection, so we don't treat noise as a contradicting pulse. */
    double expected_min_pulses = (window_s / 60.0) * cfg->min_expected_bpm_floor;
    return count_in_window >= 2 && (double)count_in_window >= expected_min_pulses * cfg->min_expected_fraction;
}

int qw_decide(const qw_record_t *rec, const char *alarm_type, const qw_rules_t *rules,
              const qw_detector_config_t *cfg, qw_decide_workspace_t *ws,
              qw_decision_result_t *result) {
    if (!rec || !alarm_type || !rules || !cfg || !ws || !result) return -1;

    memset(result, 0, sizeof(*result));
    result->decision = QW_DECISION_CONFIRM;

    const qw_alarm_def_t *def = qw_rules_find_alarm(rules, alarm_type);
    if (!def) {
        snprintf(result->reason, QW_REASON_LEN, "unknown alarm_type '%s' in rules; confirming by default", alarm_type);
        return 0;
    }

    /* PhysioNet 2015 Challenge convention: every alarm fires exactly
     * QW_ALARM_OFFSET_S seconds into the record. Records with a trailing
     * "l" (long) suffix carry an extra ~30s of signal AFTER that point for
     * annotation context; records with an "s" (short) suffix end right at
     * the alarm. Either way, "the window before the alarm" means the
     * QW_ALARM_OFFSET_S mark, not the end of the file -- using n_samples
     * directly would look 30s into the future for "l" records and silently
     * evaluate the wrong window. This is a dataset/recording convention,
     * not a clinical threshold, so it is a named constant here rather than
     * something pulled from rules/alarm_rules.json. */
    const double QW_ALARM_OFFSET_S = 300.0;
    uint32_t alarm_sample = (uint32_t)(QW_ALARM_OFFSET_S * (double)rec->fs);
    if (alarm_sample > rec->n_samples) alarm_sample = rec->n_samples;

    /* ws->beat_idx / ws->pulse_idx are caller-owned scratch space (see
     * qw_decide_workspace_t in qw_core.h); this function keeps no static
     * or thread-local state, so it is reentrant across concurrent calls
     * each with their own ws. */
    uint32_t *beat_idx = ws->beat_idx;
    uint32_t *pulse_idx = ws->pulse_idx;

    /* Pick the best ECG channel (first one found) for beat detection, and
     * the best pulsatile channel (first one found) for pulse detection. */
    const qw_channel_t *ecg_ch = NULL;
    const qw_channel_t *pulse_ch = NULL;
    for (uint32_t c = 0; c < rec->n_channels; c++) {
        if (rec->channels[c].kind == QW_CHANNEL_ECG && !ecg_ch) ecg_ch = &rec->channels[c];
        if (rec->channels[c].kind == QW_CHANNEL_PULSE && !pulse_ch) pulse_ch = &rec->channels[c];
    }

    /* All detection and quality scoring operates only on signal up to the
     * alarm onset -- data after the alarm fired cannot inform whether the
     * alarm should have been suppressed. */
    size_t n_beats = 0;
    double ecg_sqi = 0.0;
    if (ecg_ch) {
        n_beats = qw_detect_beats_ecg(ecg_ch->samples, alarm_sample, rec->fs, beat_idx, QW_MAX_BEATS);
        ecg_sqi = qw_signal_quality(ecg_ch->samples, alarm_sample, rec->fs, beat_idx, n_beats, cfg);
    }

    size_t n_pulses = 0;
    double pulse_sqi = 0.0;
    if (pulse_ch) {
        n_pulses = qw_detect_pulses(pulse_ch->samples, alarm_sample, rec->fs, pulse_idx, QW_MAX_BEATS, cfg);
        pulse_sqi = qw_signal_quality(pulse_ch->samples, alarm_sample, rec->fs, pulse_idx, n_pulses, cfg);
    }

    double best_sqi = (pulse_sqi > ecg_sqi) ? pulse_sqi : ecg_sqi;
    result->sqi = best_sqi;

    int never_suppress = qw_rules_never_suppress(rules, alarm_type);

    if (never_suppress) {
        snprintf(result->reason, QW_REASON_LEN,
                 "%s is in suppression.never_suppress; confirming unconditionally (sqi=%.2f)",
                 alarm_type, best_sqi);
        result->decision = QW_DECISION_CONFIRM;
        return 0;
    }

    if (!pulse_ch) {
        snprintf(result->reason, QW_REASON_LEN,
                 "no pulsatile channel (ABP/PPG) available to contradict %s; confirming (sqi=%.2f)",
                 alarm_type, best_sqi);
        result->decision = QW_DECISION_CONFIRM;
        return 0;
    }

    if (pulse_sqi < rules->suppression.min_signal_quality) {
        snprintf(result->reason, QW_REASON_LEN,
                 "pulsatile channel sqi=%.2f below min_signal_quality=%.2f; confirming %s",
                 pulse_sqi, rules->suppression.min_signal_quality, alarm_type);
        result->decision = QW_DECISION_CONFIRM;
        return 0;
    }

    int pulse_present = pulse_present_in_window(pulse_idx, n_pulses, alarm_sample, rec->fs,
                                                 rules->suppression.window_s, cfg);
    if (!pulse_present) {
        snprintf(result->reason, QW_REASON_LEN,
                 "no regular pulse detected in suppression window (%.0fs) despite high sqi=%.2f; confirming %s",
                 rules->suppression.window_s, pulse_sqi, alarm_type);
        result->decision = QW_DECISION_CONFIRM;
        return 0;
    }

    /* We have a high-quality pulsatile signal with a regular pulse
     * throughout the suppression window immediately before the alarm.
     * Check whether that pulse contradicts the specific alarm condition. */
    uint32_t window_samples = (uint32_t)(rules->suppression.window_s * (double)rec->fs);
    uint32_t window_start = (alarm_sample > window_samples) ? (alarm_sample - window_samples) : 0;
    size_t first_in_window = n_pulses;
    for (size_t i = 0; i < n_pulses; i++) {
        if (pulse_idx[i] >= window_start) { first_in_window = i; break; }
    }
    size_t count_in_window = (first_in_window < n_pulses) ? (n_pulses - first_in_window) : 0;
    double pulse_hr = heart_rate_from_beats(pulse_idx, n_pulses, rec->fs, count_in_window);
    double window_cv = interval_cv_from_beats(pulse_idx, n_pulses, count_in_window);

    /* A pulse-rate estimate built from highly irregular intervals in the
     * window (e.g. the detector missing beats intermittently on a weak
     * waveform) is not reliable evidence that the rate contradicts the
     * alarm; treat that the same as "could not compute" rather than
     * trusting a possibly-wrong number sign suppression. Asystole's
     * contradiction test only needs pulse presence (checked above via
     * pulse_present_in_window's minimum count), so it is not gated here.
     * max_trusted_interval_cv is an engineering constant from
     * config/detector.json, not a clinical threshold. */
    int hr_trustworthy = (window_cv >= 0.0) && (window_cv <= cfg->max_trusted_interval_cv);

    int contradicts = 0;
    char detail[128];
    detail[0] = '\0';

    if (pulse_hr < 0.0) {
        contradicts = 0;
        snprintf(detail, sizeof(detail), "pulse hr could not be computed");
    } else if (def->has_no_beat_s) {
        /* Asystole: a present, regular pulse throughout the window directly
         * contradicts "no QRS for N seconds". */
        contradicts = 1;
        snprintf(detail, sizeof(detail), "pulse present throughout %.0fs window contradicts asystole", rules->suppression.window_s);
    } else if (!hr_trustworthy) {
        contradicts = 0;
        snprintf(detail, sizeof(detail), "pulse interval cv=%.2f too irregular to trust hr=%.0fbpm estimate", window_cv, pulse_hr);
    } else if (def->has_hr_below_bpm) {
        /* Bradycardia: contradicted if pulse rate is NOT below threshold. */
        contradicts = pulse_hr >= def->hr_below_bpm;
        snprintf(detail, sizeof(detail), "pulse hr=%.0fbpm vs bradycardia threshold <%.0fbpm", pulse_hr, def->hr_below_bpm);
    } else if (def->has_hr_above_bpm) {
        /* Tachycardia / VT: contradicted if pulse rate is NOT above threshold. */
        contradicts = pulse_hr <= def->hr_above_bpm;
        snprintf(detail, sizeof(detail), "pulse hr=%.0fbpm vs threshold >%.0fbpm", pulse_hr, def->hr_above_bpm);
    } else if (def->has_duration_s) {
        /* Flutter/Fib handled via never_suppress already; if reached here
         * (future rules change), be conservative and do not contradict. */
        contradicts = 0;
        snprintf(detail, sizeof(detail), "no contradiction rule defined for duration-based alarms");
    }

    if (contradicts) {
        snprintf(result->reason, QW_REASON_LEN,
                 "SUPPRESS_TO_REVIEW: %s; sqi=%.2f, logged for nurse review (never discarded)",
                 detail, pulse_sqi);
        result->decision = QW_DECISION_SUPPRESS_TO_REVIEW;
    } else {
        snprintf(result->reason, QW_REASON_LEN,
                 "pulse does not contradict %s (%s); confirming (sqi=%.2f)",
                 alarm_type, detail, pulse_sqi);
        result->decision = QW_DECISION_CONFIRM;
    }

    return 0;
}
