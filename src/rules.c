/* rules.c -- tiny reader for rules/alarm_rules.json's fixed shape.
 * See rules.h for the exact structure this understands.
 *
 * This is a minimal, hand-rolled JSON scanner. It is not a general
 * purpose JSON parser: it walks the token stream looking for the known
 * keys at the known nesting depths and ignores everything else (so it
 * tolerates added metadata fields, reordering of keys, and whitespace
 * differences, but not a different document shape).
 */
#include "rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------- minimal JSON tokenizer ---------- */

typedef enum {
    TOK_EOF,
    TOK_LBRACE,   /* { */
    TOK_RBRACE,   /* } */
    TOK_LBRACKET, /* [ */
    TOK_RBRACKET, /* ] */
    TOK_COLON,    /* : */
    TOK_COMMA,    /* , */
    TOK_STRING,
    TOK_NUMBER,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_ERROR
} tok_kind_t;

typedef struct {
    tok_kind_t kind;
    char text[512]; /* decoded string value, or raw number text */
    double num;
} token_t;

typedef struct {
    const char *buf;
    size_t len;
    size_t pos;
} lexer_t;

static void lex_skip_ws(lexer_t *lx) {
    while (lx->pos < lx->len) {
        char c = lx->buf[lx->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            lx->pos++;
        } else {
            break;
        }
    }
}

static int lex_next(lexer_t *lx, token_t *tok) {
    lex_skip_ws(lx);
    if (lx->pos >= lx->len) {
        tok->kind = TOK_EOF;
        return 0;
    }
    char c = lx->buf[lx->pos];
    switch (c) {
        case '{': lx->pos++; tok->kind = TOK_LBRACE; return 0;
        case '}': lx->pos++; tok->kind = TOK_RBRACE; return 0;
        case '[': lx->pos++; tok->kind = TOK_LBRACKET; return 0;
        case ']': lx->pos++; tok->kind = TOK_RBRACKET; return 0;
        case ':': lx->pos++; tok->kind = TOK_COLON; return 0;
        case ',': lx->pos++; tok->kind = TOK_COMMA; return 0;
        default: break;
    }
    if (c == '"') {
        lx->pos++;
        size_t out = 0;
        while (lx->pos < lx->len && lx->buf[lx->pos] != '"') {
            char ch = lx->buf[lx->pos];
            if (ch == '\\' && lx->pos + 1 < lx->len) {
                lx->pos++;
                char esc = lx->buf[lx->pos];
                switch (esc) {
                    case 'n': ch = '\n'; break;
                    case 't': ch = '\t'; break;
                    case 'r': ch = '\r'; break;
                    case '"': ch = '"'; break;
                    case '\\': ch = '\\'; break;
                    case '/': ch = '/'; break;
                    default: ch = esc; break;
                }
            }
            if (out + 1 < sizeof(tok->text)) {
                tok->text[out++] = ch;
            }
            lx->pos++;
        }
        if (lx->pos >= lx->len) {
            tok->kind = TOK_ERROR;
            return -1;
        }
        lx->pos++; /* closing quote */
        tok->text[out] = '\0';
        tok->kind = TOK_STRING;
        return 0;
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        size_t start = lx->pos;
        if (c == '-') lx->pos++;
        while (lx->pos < lx->len && isdigit((unsigned char)lx->buf[lx->pos])) lx->pos++;
        if (lx->pos < lx->len && lx->buf[lx->pos] == '.') {
            lx->pos++;
            while (lx->pos < lx->len && isdigit((unsigned char)lx->buf[lx->pos])) lx->pos++;
        }
        if (lx->pos < lx->len && (lx->buf[lx->pos] == 'e' || lx->buf[lx->pos] == 'E')) {
            lx->pos++;
            if (lx->pos < lx->len && (lx->buf[lx->pos] == '+' || lx->buf[lx->pos] == '-')) lx->pos++;
            while (lx->pos < lx->len && isdigit((unsigned char)lx->buf[lx->pos])) lx->pos++;
        }
        size_t n = lx->pos - start;
        if (n >= sizeof(tok->text)) n = sizeof(tok->text) - 1;
        memcpy(tok->text, lx->buf + start, n);
        tok->text[n] = '\0';
        tok->num = atof(tok->text);
        tok->kind = TOK_NUMBER;
        return 0;
    }
    if (lx->pos + 4 <= lx->len && strncmp(lx->buf + lx->pos, "true", 4) == 0) {
        lx->pos += 4;
        tok->kind = TOK_TRUE;
        return 0;
    }
    if (lx->pos + 5 <= lx->len && strncmp(lx->buf + lx->pos, "false", 5) == 0) {
        lx->pos += 5;
        tok->kind = TOK_FALSE;
        return 0;
    }
    if (lx->pos + 4 <= lx->len && strncmp(lx->buf + lx->pos, "null", 4) == 0) {
        lx->pos += 4;
        tok->kind = TOK_NULL;
        return 0;
    }
    tok->kind = TOK_ERROR;
    return -1;
}

/* Skip over a JSON value (already consumed its opening token via peek
 * semantics: this function receives the first token of the value). */
static int skip_value(lexer_t *lx, token_t *tok) {
    switch (tok->kind) {
        case TOK_STRING:
        case TOK_NUMBER:
        case TOK_TRUE:
        case TOK_FALSE:
        case TOK_NULL:
            return 0;
        case TOK_LBRACE: {
            token_t t;
            if (lex_next(lx, &t) != 0) return -1;
            if (t.kind == TOK_RBRACE) return 0;
            for (;;) {
                if (t.kind != TOK_STRING) return -1; /* key */
                if (lex_next(lx, &t) != 0 || t.kind != TOK_COLON) return -1;
                if (lex_next(lx, &t) != 0) return -1;
                if (skip_value(lx, &t) != 0) return -1;
                if (lex_next(lx, &t) != 0) return -1;
                if (t.kind == TOK_RBRACE) return 0;
                if (t.kind != TOK_COMMA) return -1;
                if (lex_next(lx, &t) != 0) return -1;
            }
        }
        case TOK_LBRACKET: {
            token_t t;
            if (lex_next(lx, &t) != 0) return -1;
            if (t.kind == TOK_RBRACKET) return 0;
            for (;;) {
                if (skip_value(lx, &t) != 0) return -1;
                if (lex_next(lx, &t) != 0) return -1;
                if (t.kind == TOK_RBRACKET) return 0;
                if (t.kind != TOK_COMMA) return -1;
                if (lex_next(lx, &t) != 0) return -1;
            }
        }
        default:
            return -1;
    }
}

/* ---------- object-field parsers scoped to our known shape ---------- */

static int parse_alarm_def_object(lexer_t *lx, qw_alarm_def_t *def) {
    token_t t;
    if (lex_next(lx, &t) != 0 || t.kind != TOK_LBRACE) return -1;
    if (lex_next(lx, &t) != 0) return -1;
    if (t.kind == TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != TOK_STRING) return -1;
        char key[QW_NAME_LEN];
        strncpy(key, t.text, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';

        if (lex_next(lx, &t) != 0 || t.kind != TOK_COLON) return -1;
        if (lex_next(lx, &t) != 0) return -1;

        if (strcmp(key, "no_beat_s") == 0 && t.kind == TOK_NUMBER) {
            def->has_no_beat_s = 1;
            def->no_beat_s = t.num;
        } else if (strcmp(key, "hr_below_bpm") == 0 && t.kind == TOK_NUMBER) {
            def->has_hr_below_bpm = 1;
            def->hr_below_bpm = t.num;
        } else if (strcmp(key, "hr_above_bpm") == 0 && t.kind == TOK_NUMBER) {
            def->has_hr_above_bpm = 1;
            def->hr_above_bpm = t.num;
        } else if (strcmp(key, "beats") == 0 && t.kind == TOK_NUMBER) {
            def->has_beats = 1;
            def->beats = (long)t.num;
        } else if (strcmp(key, "duration_s") == 0 && t.kind == TOK_NUMBER) {
            def->has_duration_s = 1;
            def->duration_s = t.num;
        } else {
            /* definition, or any future field: skip */
            if (skip_value(lx, &t) != 0) return -1;
        }

        if (lex_next(lx, &t) != 0) return -1;
        if (t.kind == TOK_RBRACE) return 0;
        if (t.kind != TOK_COMMA) return -1;
        if (lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_alarms_object(lexer_t *lx, qw_rules_t *rules) {
    token_t t;
    if (lex_next(lx, &t) != 0 || t.kind != TOK_LBRACE) return -1;
    if (lex_next(lx, &t) != 0) return -1;
    if (t.kind == TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != TOK_STRING) return -1;
        if (rules->n_alarms >= sizeof(rules->alarms) / sizeof(rules->alarms[0])) return -1;
        qw_alarm_def_t *def = &rules->alarms[rules->n_alarms];
        memset(def, 0, sizeof(*def));
        strncpy(def->name, t.text, sizeof(def->name) - 1);

        if (lex_next(lx, &t) != 0 || t.kind != TOK_COLON) return -1;
        if (parse_alarm_def_object(lx, def) != 0) return -1;
        rules->n_alarms++;

        if (lex_next(lx, &t) != 0) return -1;
        if (t.kind == TOK_RBRACE) return 0;
        if (t.kind != TOK_COMMA) return -1;
        if (lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_string_array(lexer_t *lx, char out[][QW_NAME_LEN], size_t max, size_t *n_out) {
    token_t t;
    if (lex_next(lx, &t) != 0 || t.kind != TOK_LBRACKET) return -1;
    *n_out = 0;
    if (lex_next(lx, &t) != 0) return -1;
    if (t.kind == TOK_RBRACKET) return 0;
    for (;;) {
        if (t.kind != TOK_STRING) return -1;
        if (*n_out < max) {
            strncpy(out[*n_out], t.text, QW_NAME_LEN - 1);
            out[*n_out][QW_NAME_LEN - 1] = '\0';
            (*n_out)++;
        }
        if (lex_next(lx, &t) != 0) return -1;
        if (t.kind == TOK_RBRACKET) return 0;
        if (t.kind != TOK_COMMA) return -1;
        if (lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_suppression_object(lexer_t *lx, qw_suppression_t *supp) {
    token_t t;
    if (lex_next(lx, &t) != 0 || t.kind != TOK_LBRACE) return -1;
    if (lex_next(lx, &t) != 0) return -1;
    if (t.kind == TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != TOK_STRING) return -1;
        char key[QW_NAME_LEN];
        strncpy(key, t.text, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';

        if (lex_next(lx, &t) != 0 || t.kind != TOK_COLON) return -1;

        if (strcmp(key, "window_s") == 0) {
            if (lex_next(lx, &t) != 0 || t.kind != TOK_NUMBER) return -1;
            supp->window_s = t.num;
        } else if (strcmp(key, "min_signal_quality") == 0) {
            if (lex_next(lx, &t) != 0 || t.kind != TOK_NUMBER) return -1;
            supp->min_signal_quality = t.num;
        } else if (strcmp(key, "never_suppress") == 0) {
            if (parse_string_array(lx, supp->never_suppress, QW_MAX_NEVER_SUPPRESS,
                                    &supp->n_never_suppress) != 0) {
                return -1;
            }
        } else {
            if (lex_next(lx, &t) != 0) return -1;
            if (skip_value(lx, &t) != 0) return -1;
        }

        if (lex_next(lx, &t) != 0) return -1;
        if (t.kind == TOK_RBRACE) return 0;
        if (t.kind != TOK_COMMA) return -1;
        if (lex_next(lx, &t) != 0) return -1;
    }
}

static int parse_root(lexer_t *lx, qw_rules_t *rules) {
    token_t t;
    if (lex_next(lx, &t) != 0 || t.kind != TOK_LBRACE) return -1;
    if (lex_next(lx, &t) != 0) return -1;
    if (t.kind == TOK_RBRACE) return 0;
    for (;;) {
        if (t.kind != TOK_STRING) return -1;
        char key[QW_NAME_LEN];
        strncpy(key, t.text, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';

        if (lex_next(lx, &t) != 0 || t.kind != TOK_COLON) return -1;

        if (strcmp(key, "alarms") == 0) {
            if (parse_alarms_object(lx, rules) != 0) return -1;
        } else if (strcmp(key, "suppression") == 0) {
            if (parse_suppression_object(lx, &rules->suppression) != 0) return -1;
        } else {
            if (lex_next(lx, &t) != 0) return -1;
            if (skip_value(lx, &t) != 0) return -1;
        }

        if (lex_next(lx, &t) != 0) return -1;
        if (t.kind == TOK_RBRACE) return 0;
        if (t.kind != TOK_COMMA) return -1;
        if (lex_next(lx, &t) != 0) return -1;
    }
}

int qw_rules_load(const char *path, qw_rules_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return -1; }

    size_t n_read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n_read != (size_t)size) { free(buf); return -1; }
    buf[size] = '\0';

    memset(out, 0, sizeof(*out));

    lexer_t lx = { buf, (size_t)size, 0 };
    int rc = parse_root(&lx, out);

    free(buf);
    if (rc != 0) return -1;

    if (out->n_alarms == 0) return -1; /* must have at least the alarms map */

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
