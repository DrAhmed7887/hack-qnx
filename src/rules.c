/* rules.c -- tiny reader for rules/alarm_rules.json's fixed shape.
 * See rules.h for the exact structure this understands.
 *
 * This is a minimal, hand-rolled JSON scanner (src/qw_json_lexer.h). It
 * is not a general purpose JSON parser: it walks the token stream
 * looking for the known keys at the known nesting depths and ignores
 * everything else (so it tolerates added metadata fields, reordering of
 * keys, and whitespace differences, but not a different document shape).
 *
 * Fail-closed contract: qw_rules_load() returns nonzero -- and prints a
 * clear message to stderr identifying which field is missing or out of
 * range -- if any of the following is true, rather than silently running
 * with a guessed or zeroed value:
 *   - suppression.window_s is missing, or <= 0
 *   - suppression.min_signal_quality is missing, or outside (0, 1]
 *   - suppression.never_suppress is missing (an empty array is fine)
 *   - any alarm definition is missing every recognized threshold field
 * The caller (qw_cli.c) treats a nonzero return as fatal and exits
 * nonzero; the alarm path never runs on a partially validated rules file.
 */
#include "rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qw_json_lexer.h"

/* ---------- object-field parsers scoped to our known shape ---------- */

static int parse_alarm_def_object(qw_lexer_t *lx, qw_alarm_def_t *def) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        char key[QW_NAME_LEN];
        strncpy(key, t.text, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;

        if (strcmp(key, "no_beat_s") == 0 && t.kind == QW_TOK_NUMBER) {
            def->has_no_beat_s = 1;
            def->no_beat_s = t.num;
        } else if (strcmp(key, "hr_below_bpm") == 0 && t.kind == QW_TOK_NUMBER) {
            def->has_hr_below_bpm = 1;
            def->hr_below_bpm = t.num;
        } else if (strcmp(key, "hr_above_bpm") == 0 && t.kind == QW_TOK_NUMBER) {
            def->has_hr_above_bpm = 1;
            def->hr_above_bpm = t.num;
        } else if (strcmp(key, "beats") == 0 && t.kind == QW_TOK_NUMBER) {
            def->has_beats = 1;
            def->beats = (long)t.num;
        } else if (strcmp(key, "duration_s") == 0 && t.kind == QW_TOK_NUMBER) {
            def->has_duration_s = 1;
            def->duration_s = t.num;
        } else {
            /* definition, or any future field: skip */
            if (qw_skip_value_depth(lx, &t, 0) != 0) return -1;
        }

        if (qw_lex_next(lx, &t) != 0) return -1;
        if (t.kind == QW_TOK_RBRACE) return 0;
        if (t.kind != QW_TOK_COMMA) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_alarms_object(qw_lexer_t *lx, qw_rules_t *rules) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        if (rules->n_alarms >= sizeof(rules->alarms) / sizeof(rules->alarms[0])) return -1;
        qw_alarm_def_t *def = &rules->alarms[rules->n_alarms];
        memset(def, 0, sizeof(*def));
        strncpy(def->name, t.text, sizeof(def->name) - 1);
        def->name[sizeof(def->name) - 1] = '\0';

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;
        if (parse_alarm_def_object(lx, def) != 0) return -1;
        rules->n_alarms++;

        if (qw_lex_next(lx, &t) != 0) return -1;
        if (t.kind == QW_TOK_RBRACE) return 0;
        if (t.kind != QW_TOK_COMMA) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_string_array(qw_lexer_t *lx, char out[][QW_NAME_LEN], size_t max, size_t *n_out) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACKET) return -1;
    *n_out = 0;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACKET) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        if (*n_out < max) {
            strncpy(out[*n_out], t.text, QW_NAME_LEN - 1);
            out[*n_out][QW_NAME_LEN - 1] = '\0';
            (*n_out)++;
        }
        if (qw_lex_next(lx, &t) != 0) return -1;
        if (t.kind == QW_TOK_RBRACKET) return 0;
        if (t.kind != QW_TOK_COMMA) return -1;
        if (qw_lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_suppression_object(qw_lexer_t *lx, qw_suppression_t *supp) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        char key[QW_NAME_LEN];
        strncpy(key, t.text, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;

        if (strcmp(key, "window_s") == 0) {
            if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_NUMBER) return -1;
            supp->window_s = t.num;
            supp->has_window_s = 1;
        } else if (strcmp(key, "min_signal_quality") == 0) {
            if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_NUMBER) return -1;
            supp->min_signal_quality = t.num;
            supp->has_min_signal_quality = 1;
        } else if (strcmp(key, "never_suppress") == 0) {
            if (parse_string_array(lx, supp->never_suppress, QW_MAX_NEVER_SUPPRESS,
                                    &supp->n_never_suppress) != 0) {
                return -1;
            }
            supp->has_never_suppress = 1;
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

static int parse_root(qw_lexer_t *lx, qw_rules_t *rules) {
    qw_token_t t;
    if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return -1;
    if (qw_lex_next(lx, &t) != 0) return -1;
    if (t.kind == QW_TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != QW_TOK_STRING) return -1;
        char key[QW_NAME_LEN];
        strncpy(key, t.text, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';

        if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;

        if (strcmp(key, "status") == 0) {
            if (qw_lex_next(lx, &t) != 0) return -1;
            if (t.kind == QW_TOK_STRING) {
                strncpy(rules->status, t.text, sizeof(rules->status) - 1);
                rules->status[sizeof(rules->status) - 1] = '\0';
            } else if (qw_skip_value_depth(lx, &t, 0) != 0) {
                return -1;
            }
        } else if (strcmp(key, "alarms") == 0) {
            if (parse_alarms_object(lx, rules) != 0) return -1;
        } else if (strcmp(key, "suppression") == 0) {
            if (parse_suppression_object(lx, &rules->suppression) != 0) return -1;
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

/* Fail-closed validation pass: run after a structurally successful parse.
 * Prints exactly which required field is missing or out of range to
 * stderr and returns nonzero; qw_rules_load() propagates that failure. */
static int validate_rules(const qw_rules_t *rules, const char *path) {
    if (rules->n_alarms == 0) {
        fprintf(stderr, "rules error: %s: 'alarms' is missing or empty\n", path);
        return -1;
    }

    for (size_t i = 0; i < rules->n_alarms; i++) {
        const qw_alarm_def_t *a = &rules->alarms[i];
        if (!a->has_no_beat_s && !a->has_hr_below_bpm && !a->has_hr_above_bpm && !a->has_duration_s) {
            fprintf(stderr,
                    "rules error: %s: alarm '%s' has no recognized threshold field "
                    "(no_beat_s, hr_below_bpm, hr_above_bpm, duration_s)\n",
                    path, a->name);
            return -1;
        }
    }

    double min_required_window_s = 0.0;
    for (size_t i = 0; i < rules->n_alarms; i++) {
        const qw_alarm_def_t *a = &rules->alarms[i];
        if (a->has_no_beat_s) {
            if (!(a->no_beat_s > 0.0)) {
                fprintf(stderr, "rules error: %s: alarm '%s' no_beat_s=%g must be > 0\n",
                        path, a->name, a->no_beat_s);
                return -1;
            }
            if (a->no_beat_s > min_required_window_s) min_required_window_s = a->no_beat_s;
        }
        if (a->has_hr_below_bpm || a->has_hr_above_bpm) {
            if (!a->has_beats || a->beats < 2) {
                fprintf(stderr,
                        "rules error: %s: alarm '%s' has an hr threshold and requires beats >= 2\n",
                        path, a->name);
                return -1;
            }
        }
        if (a->has_hr_below_bpm) {
            if (!(a->hr_below_bpm > 0.0)) {
                fprintf(stderr, "rules error: %s: alarm '%s' hr_below_bpm=%g must be > 0\n",
                        path, a->name, a->hr_below_bpm);
                return -1;
            }
            double w = (double)(a->beats - 1) * 60.0 / a->hr_below_bpm;
            if (w > min_required_window_s) min_required_window_s = w;
        }
        if (a->has_hr_above_bpm) {
            if (!(a->hr_above_bpm > 0.0)) {
                fprintf(stderr, "rules error: %s: alarm '%s' hr_above_bpm=%g must be > 0\n",
                        path, a->name, a->hr_above_bpm);
                return -1;
            }
            double w = (double)(a->beats - 1) * 60.0 / a->hr_above_bpm;
            if (w > min_required_window_s) min_required_window_s = w;
        }
    }

    const qw_suppression_t *s = &rules->suppression;
    if (!s->has_window_s) {
        fprintf(stderr, "rules error: %s: suppression.window_s is missing\n", path);
        return -1;
    }
    if (s->window_s <= 0.0) {
        fprintf(stderr, "rules error: %s: suppression.window_s=%g must be > 0\n", path, s->window_s);
        return -1;
    }
    if (s->window_s < min_required_window_s) {
        fprintf(stderr,
                "rules error: %s: suppression.window_s=%g is shorter than the longest rolling span "
                "required by the alarm definitions (%g s)\n",
                path, s->window_s, min_required_window_s);
        return -1;
    }
    if (!s->has_min_signal_quality) {
        fprintf(stderr, "rules error: %s: suppression.min_signal_quality is missing\n", path);
        return -1;
    }
    if (s->min_signal_quality <= 0.0 || s->min_signal_quality > 1.0) {
        fprintf(stderr,
                "rules error: %s: suppression.min_signal_quality=%g must be in (0, 1]\n",
                path, s->min_signal_quality);
        return -1;
    }
    if (!s->has_never_suppress) {
        fprintf(stderr, "rules error: %s: suppression.never_suppress is missing\n", path);
        return -1;
    }

    return 0;
}

int qw_rules_load(const char *path, qw_rules_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "rules error: %s: cannot open file\n", path);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "rules error: %s: seek failed\n", path);
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "rules error: %s: ftell failed\n", path);
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "rules error: %s: seek failed\n", path);
        fclose(f);
        return -1;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fprintf(stderr, "rules error: %s: out of memory reading file\n", path);
        fclose(f);
        return -1;
    }

    size_t n_read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n_read != (size_t)size) {
        fprintf(stderr, "rules error: %s: short read\n", path);
        free(buf);
        return -1;
    }
    buf[size] = '\0';

    memset(out, 0, sizeof(*out));

    qw_lexer_t lx = { buf, (size_t)size, 0 };
    int rc = parse_root(&lx, out);

    free(buf);
    if (rc != 0) {
        fprintf(stderr, "rules error: %s: malformed JSON or unexpected document shape\n", path);
        return -1;
    }

    if (out->status[0] == '\0') {
        strcpy(out->status, "unknown");
    }

    if (validate_rules(out, path) != 0) {
        return -1;
    }

    return 0;
}

const qw_alarm_def_t *qw_rules_find_alarm(const qw_rules_t *rules, const char *alarm_type) {
    for (size_t i = 0; i < rules->n_alarms; i++) {
        if (strcmp(rules->alarms[i].name, alarm_type) == 0) {
            return &rules->alarms[i];
        }
    }
    return NULL;
}

int qw_rules_never_suppress(const qw_rules_t *rules, const char *alarm_type) {
    for (size_t i = 0; i < rules->suppression.n_never_suppress; i++) {
        if (strcmp(rules->suppression.never_suppress[i], alarm_type) == 0) {
            return 1;
        }
    }
    return 0;
}
