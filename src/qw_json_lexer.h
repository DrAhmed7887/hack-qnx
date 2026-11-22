/* qw_json_lexer.h -- minimal, header-only JSON tokenizer shared by the
 * fixed-shape readers in rules.c and qw_detector_config.c.
 *
 * This is NOT a general purpose JSON parser. It only tokenizes: braces,
 * brackets, colon, comma, strings (with the common backslash escapes),
 * numbers, true/false/null. Callers walk the token stream themselves to
 * match their own known shape and fail if the document doesn't match.
 *
 * Defined `static` (translation-unit-local) so multiple .c files can
 * each #include this header without link-time duplicate-symbol errors.
 */
#ifndef QW_JSON_LEXER_H
#define QW_JSON_LEXER_H

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    QW_TOK_EOF,
    QW_TOK_LBRACE,   /* { */
    QW_TOK_RBRACE,   /* } */
    QW_TOK_LBRACKET, /* [ */
    QW_TOK_RBRACKET, /* ] */
    QW_TOK_COLON,    /* : */
    QW_TOK_COMMA,    /* , */
    QW_TOK_STRING,
    QW_TOK_NUMBER,
    QW_TOK_TRUE,
    QW_TOK_FALSE,
    QW_TOK_NULL,
    QW_TOK_ERROR
} qw_tok_kind_t;

typedef struct {
    qw_tok_kind_t kind;
    char text[512]; /* decoded string value, or raw number text */
    double num;
} qw_token_t;

typedef struct {
    const char *buf;
    size_t len;
    size_t pos;
} qw_lexer_t;

static void qw_lex_skip_ws(qw_lexer_t *lx) {
    while (lx->pos < lx->len) {
        char c = lx->buf[lx->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            lx->pos++;
        } else {
            break;
        }
    }
}

static int qw_lex_next(qw_lexer_t *lx, qw_token_t *tok) {
    qw_lex_skip_ws(lx);
    if (lx->pos >= lx->len) {
        tok->kind = QW_TOK_EOF;
        return 0;
    }
    char c = lx->buf[lx->pos];
    switch (c) {
        case '{': lx->pos++; tok->kind = QW_TOK_LBRACE; return 0;
        case '}': lx->pos++; tok->kind = QW_TOK_RBRACE; return 0;
        case '[': lx->pos++; tok->kind = QW_TOK_LBRACKET; return 0;
        case ']': lx->pos++; tok->kind = QW_TOK_RBRACKET; return 0;
        case ':': lx->pos++; tok->kind = QW_TOK_COLON; return 0;
        case ',': lx->pos++; tok->kind = QW_TOK_COMMA; return 0;
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
            tok->kind = QW_TOK_ERROR;
            return -1;
        }
        lx->pos++; /* closing quote */
        tok->text[out] = '\0';
        tok->kind = QW_TOK_STRING;
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
        tok->kind = QW_TOK_NUMBER;
        return 0;
    }
    if (lx->pos + 4 <= lx->len && strncmp(lx->buf + lx->pos, "true", 4) == 0) {
        lx->pos += 4;
        tok->kind = QW_TOK_TRUE;
        return 0;
    }
    if (lx->pos + 5 <= lx->len && strncmp(lx->buf + lx->pos, "false", 5) == 0) {
        lx->pos += 5;
        tok->kind = QW_TOK_FALSE;
        return 0;
    }
    if (lx->pos + 4 <= lx->len && strncmp(lx->buf + lx->pos, "null", 4) == 0) {
        lx->pos += 4;
        tok->kind = QW_TOK_NULL;
        return 0;
    }
    tok->kind = QW_TOK_ERROR;
    return -1;
}

/* Maximum nesting depth skip_value will descend into. Bounds stack usage
 * (this is a recursive-descent skipper) against malicious or malformed
 * documents with pathological nesting. */
#define QW_JSON_MAX_SKIP_DEPTH 32

/* Skip over a JSON value (tok is already the first token of the value).
 * depth is the current nesting depth of this call (callers pass 0 at the
 * top level); returns -1 once depth exceeds QW_JSON_MAX_SKIP_DEPTH. */
static int qw_skip_value_depth(qw_lexer_t *lx, qw_token_t *tok, int depth) {
    if (depth > QW_JSON_MAX_SKIP_DEPTH) return -1;
    switch (tok->kind) {
        case QW_TOK_STRING:
        case QW_TOK_NUMBER:
        case QW_TOK_TRUE:
        case QW_TOK_FALSE:
        case QW_TOK_NULL:
            return 0;
        case QW_TOK_LBRACE: {
            qw_token_t t;
            if (qw_lex_next(lx, &t) != 0) return -1;
            if (t.kind == QW_TOK_RBRACE) return 0;
            for (;;) {
                if (t.kind != QW_TOK_STRING) return -1; /* key */
                if (qw_lex_next(lx, &t) != 0 || t.kind != QW_TOK_COLON) return -1;
                if (qw_lex_next(lx, &t) != 0) return -1;
                if (qw_skip_value_depth(lx, &t, depth + 1) != 0) return -1;
                if (qw_lex_next(lx, &t) != 0) return -1;
                if (t.kind == QW_TOK_RBRACE) return 0;
                if (t.kind != QW_TOK_COMMA) return -1;
                if (qw_lex_next(lx, &t) != 0) return -1;
            }
        }
        case QW_TOK_LBRACKET: {
            qw_token_t t;
            if (qw_lex_next(lx, &t) != 0) return -1;
            if (t.kind == QW_TOK_RBRACKET) return 0;
            for (;;) {
                if (qw_skip_value_depth(lx, &t, depth + 1) != 0) return -1;
                if (qw_lex_next(lx, &t) != 0) return -1;
                if (t.kind == QW_TOK_RBRACKET) return 0;
                if (t.kind != QW_TOK_COMMA) return -1;
                if (qw_lex_next(lx, &t) != 0) return -1;
            }
        }
        default:
            return -1;
    }
}

#endif /* QW_JSON_LEXER_H */
