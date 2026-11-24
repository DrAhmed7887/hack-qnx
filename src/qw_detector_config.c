/* qw_detector_config.c -- loader for config/detector.json. See
 * qw_detector_config.h for the fixed shape this understands and the
 * fail-closed validation contract.
 */
#include "qw_detector_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qw_json_lexer.h"

typedef struct {
    int has_min_prominence_frac;
    double min_prominence_frac;
    int has_min_pulse_interval_s;
    double min_pulse_interval_s;
    int has_trailing_margin_s;
    double trailing_margin_s;
    int has_max_trailing_gap_frac;
    double max_trailing_gap_frac;
    int has_min_trailing_pulses;
    double min_trailing_pulses;
    int has_activity_weight;
    double activity_weight;
    int has_regularity_weight;
    double regularity_weight;
    int has_min_expected_bpm_floor;
    double min_expected_bpm_floor;
    int has_min_expected_fraction;
    double min_expected_fraction;
    int has_max_trusted_interval_cv;
    double max_trusted_interval_cv;
} qw_detector_config_raw_t;

static int parse_pulse_detector_object(qw_lexer_t *lx, qw_detector_config_raw_t *raw) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        char key[64];
        qw_copy_str(key, sizeof(key), t.text);

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;

        if (strcmp(key, "min_prominence_frac") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_min_prominence_frac = 1;
            raw->min_prominence_frac = t.num;
        } else if (strcmp(key, "min_pulse_interval_s") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_min_pulse_interval_s = 1;
            raw->min_pulse_interval_s = t.num;
        } else {
            if (qw_skip_value_depth(lx, &t, 0) != 0) return -1;
        }

        if (qw_lex_next(lx, &t) != 0) return -1;
        if (t.kind == QW_TOK_RBRACE) return 0;
        if (t.kind != QW_TOK_COMMA) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_signal_quality_object(qw_lexer_t *lx, qw_detector_config_raw_t *raw) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        char key[64];
        qw_copy_str(key, sizeof(key), t.text);

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;

        if (strcmp(key, "activity_weight") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_activity_weight = 1;
            raw->activity_weight = t.num;
        } else if (strcmp(key, "regularity_weight") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_regularity_weight = 1;
            raw->regularity_weight = t.num;
        } else {
            if (qw_skip_value_depth(lx, &t, 0) != 0) return -1;
        }

        if (qw_lex_next(lx, &t) != 0) return -1;
        if (t.kind == QW_TOK_RBRACE) return 0;
        if (t.kind != QW_TOK_COMMA) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_pulse_presence_object(qw_lexer_t *lx, qw_detector_config_raw_t *raw) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        char key[64];
        qw_copy_str(key, sizeof(key), t.text);

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;

        if (strcmp(key, "min_expected_bpm_floor") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_min_expected_bpm_floor = 1;
            raw->min_expected_bpm_floor = t.num;
        } else if (strcmp(key, "min_expected_fraction") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_min_expected_fraction = 1;
            raw->min_expected_fraction = t.num;
        } else {
            if (qw_skip_value_depth(lx, &t, 0) != 0) return -1;
        }

        if (qw_lex_next(lx, &t) != 0) return -1;
        if (t.kind == QW_TOK_RBRACE) return 0;
        if (t.kind != QW_TOK_COMMA) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_suppression_contradiction_object(qw_lexer_t *lx, qw_detector_config_raw_t *raw) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        char key[64];
        qw_copy_str(key, sizeof(key), t.text);

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;

        if (strcmp(key, "max_trusted_interval_cv") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_max_trusted_interval_cv = 1;
            raw->max_trusted_interval_cv = t.num;
        } else {
            if (qw_skip_value_depth(lx, &t, 0) != 0) return -1;
        }

        if (qw_lex_next(lx, &t) != 0) return -1;
        if (t.kind == QW_TOK_RBRACE) return 0;
        if (t.kind != QW_TOK_COMMA) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_asystole_trailing_object(qw_lexer_t *lx, qw_detector_config_raw_t *raw) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        char key[64];
        qw_copy_str(key, sizeof(key), t.text);

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;

        if (strcmp(key, "trailing_margin_s") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_trailing_margin_s = 1;
            raw->trailing_margin_s = t.num;
        } else if (strcmp(key, "max_trailing_gap_frac") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_max_trailing_gap_frac = 1;
            raw->max_trailing_gap_frac = t.num;
        } else if (strcmp(key, "min_trailing_pulses") == 0 && t.kind == QW_TOK_NUMBER) {
            raw->has_min_trailing_pulses = 1;
            raw->min_trailing_pulses = t.num;
        } else {
            if (qw_skip_value_depth(lx, &t, 0) != 0) return -1;
        }

        if (qw_lex_next(lx, &t) != 0) return -1;
        if (t.kind == QW_TOK_RBRACE) return 0;
        if (t.kind != QW_TOK_COMMA) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_root(qw_lexer_t *lx, qw_detector_config_raw_t *raw) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        char key[64];
        qw_copy_str(key, sizeof(key), t.text);

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;

        if (strcmp(key, "pulse_detector") == 0) {
            if (parse_pulse_detector_object(lx, raw) != 0) return -1;
        } else if (strcmp(key, "asystole_trailing") == 0) {
            if (parse_asystole_trailing_object(lx, raw) != 0) return -1;
        } else if (strcmp(key, "signal_quality") == 0) {
            if (parse_signal_quality_object(lx, raw) != 0) return -1;
        } else if (strcmp(key, "pulse_presence") == 0) {
            if (parse_pulse_presence_object(lx, raw) != 0) return -1;
        } else if (strcmp(key, "suppression_contradiction") == 0) {
            if (parse_suppression_contradiction_object(lx, raw) != 0) return -1;
        } else {
            if (qw_lex_next(lx, &t) != 0) return -1;
            if (qw_skip_value_depth(lx, &t, 0) != 0) return -1;
        }

        if (qw_lex_next(lx, &t) != 0) return -1;
        if (t.kind == QW_TOK_RBRACE) return 0;
        if (t.kind != QW_TOK_COMMA) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;
    }
}

/* Fail-closed validation: every field must be present and within its
 * documented range. Prints exactly which field is missing or out of
 * range to stderr. */
static int validate_raw(const qw_detector_config_raw_t *raw, const char *path) {
    if (!raw->has_min_prominence_frac) {
        fprintf(stderr, "detector config error: %s: pulse_detector.min_prominence_frac is missing\n", path);
        return -1;
    }
    if (raw->min_prominence_frac <= 0.0 || raw->min_prominence_frac >= 1.0) {
        fprintf(stderr,
                "detector config error: %s: pulse_detector.min_prominence_frac=%g must be in (0, 1)\n",
                path, raw->min_prominence_frac);
        return -1;
    }

    if (!raw->has_min_pulse_interval_s) {
        fprintf(stderr, "detector config error: %s: pulse_detector.min_pulse_interval_s is missing\n", path);
        return -1;
    }
    if (!(raw->min_pulse_interval_s > 0.0 && raw->min_pulse_interval_s < 1.0)) {
        fprintf(stderr,
                "detector config error: %s: pulse_detector.min_pulse_interval_s=%g must be in (0, 1)\n",
                path, raw->min_pulse_interval_s);
        return -1;
    }

    if (!raw->has_trailing_margin_s) {
        fprintf(stderr, "detector config error: %s: asystole_trailing.trailing_margin_s is missing\n", path);
        return -1;
    }
    if (!(raw->trailing_margin_s >= 0.0 && raw->trailing_margin_s <= 10.0)) {
        fprintf(stderr,
                "detector config error: %s: asystole_trailing.trailing_margin_s=%g must be in [0, 10]\n",
                path, raw->trailing_margin_s);
        return -1;
    }

    if (!raw->has_max_trailing_gap_frac) {
        fprintf(stderr, "detector config error: %s: asystole_trailing.max_trailing_gap_frac is missing\n", path);
        return -1;
    }
    if (!(raw->max_trailing_gap_frac > 0.0 && raw->max_trailing_gap_frac <= 1.0)) {
        fprintf(stderr,
                "detector config error: %s: asystole_trailing.max_trailing_gap_frac=%g must be in (0, 1]\n",
                path, raw->max_trailing_gap_frac);
        return -1;
    }

    if (!raw->has_min_trailing_pulses) {
        fprintf(stderr, "detector config error: %s: asystole_trailing.min_trailing_pulses is missing\n", path);
        return -1;
    }
    if (!(raw->min_trailing_pulses >= 1.0 && raw->min_trailing_pulses <= 1000000.0) ||
        raw->min_trailing_pulses != (double)(long)raw->min_trailing_pulses) {
        fprintf(stderr,
                "detector config error: %s: asystole_trailing.min_trailing_pulses=%g must be an integer >= 1\n",
                path, raw->min_trailing_pulses);
        return -1;
    }

    if (!raw->has_activity_weight) {
        fprintf(stderr, "detector config error: %s: signal_quality.activity_weight is missing\n", path);
        return -1;
    }
    if (raw->activity_weight < 0.0 || raw->activity_weight > 1.0) {
        fprintf(stderr,
                "detector config error: %s: signal_quality.activity_weight=%g must be in [0, 1]\n",
                path, raw->activity_weight);
        return -1;
    }

    if (!raw->has_regularity_weight) {
        fprintf(stderr, "detector config error: %s: signal_quality.regularity_weight is missing\n", path);
        return -1;
    }
    if (raw->regularity_weight < 0.0 || raw->regularity_weight > 1.0) {
        fprintf(stderr,
                "detector config error: %s: signal_quality.regularity_weight=%g must be in [0, 1]\n",
                path, raw->regularity_weight);
        return -1;
    }

    if (!raw->has_min_expected_bpm_floor) {
        fprintf(stderr, "detector config error: %s: pulse_presence.min_expected_bpm_floor is missing\n", path);
        return -1;
    }
    if (raw->min_expected_bpm_floor <= 0.0) {
        fprintf(stderr,
                "detector config error: %s: pulse_presence.min_expected_bpm_floor=%g must be > 0\n",
                path, raw->min_expected_bpm_floor);
        return -1;
    }

    if (!raw->has_min_expected_fraction) {
        fprintf(stderr, "detector config error: %s: pulse_presence.min_expected_fraction is missing\n", path);
        return -1;
    }
    if (raw->min_expected_fraction <= 0.0 || raw->min_expected_fraction > 1.0) {
        fprintf(stderr,
                "detector config error: %s: pulse_presence.min_expected_fraction=%g must be in (0, 1]\n",
                path, raw->min_expected_fraction);
        return -1;
    }

    if (!raw->has_max_trusted_interval_cv) {
        fprintf(stderr,
                "detector config error: %s: suppression_contradiction.max_trusted_interval_cv is missing\n",
                path);
        return -1;
    }
    if (raw->max_trusted_interval_cv <= 0.0) {
        fprintf(stderr,
                "detector config error: %s: suppression_contradiction.max_trusted_interval_cv=%g must be > 0\n",
                path, raw->max_trusted_interval_cv);
        return -1;
    }

    return 0;
}

int qw_detector_config_load(const char *path, qw_detector_config_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "detector config error: %s: cannot open file\n", path);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "detector config error: %s: seek failed\n", path);
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "detector config error: %s: ftell failed\n", path);
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "detector config error: %s: seek failed\n", path);
        fclose(f);
        return -1;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fprintf(stderr, "detector config error: %s: out of memory reading file\n", path);
        fclose(f);
        return -1;
    }

    size_t n_read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n_read != (size_t)size) {
        fprintf(stderr, "detector config error: %s: short read\n", path);
        free(buf);
        return -1;
    }
    buf[size] = '\0';

    qw_detector_config_raw_t raw;
    memset(&raw, 0, sizeof(raw));

    qw_lexer_t lx = { buf, (size_t)size, 0 };
    int rc = parse_root(&lx, &raw);

    free(buf);
    if (rc != 0) {
        fprintf(stderr, "detector config error: %s: malformed JSON or unexpected document shape\n", path);
        return -1;
    }

    if (validate_raw(&raw, path) != 0) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->min_prominence_frac = raw.min_prominence_frac;
    out->min_pulse_interval_s = raw.min_pulse_interval_s;
    out->trailing_margin_s = raw.trailing_margin_s;
    out->max_trailing_gap_frac = raw.max_trailing_gap_frac;
    out->min_trailing_pulses = (long)raw.min_trailing_pulses;
    out->activity_weight = raw.activity_weight;
    out->regularity_weight = raw.regularity_weight;
    out->min_expected_bpm_floor = raw.min_expected_bpm_floor;
    out->min_expected_fraction = raw.min_expected_fraction;
    out->max_trusted_interval_cv = raw.max_trusted_interval_cv;

    return 0;
}
