/* qw_gateway_state.c -- see qw_gateway_state.h. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE /* macOS hides O_NOFOLLOW/O_DIRECTORY under strict POSIX */
#endif

#include "qw_gateway_state.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../qw_json_lexer.h"

#define QW_JOURNAL_MAX_OPEN 256

/* The journal's symlink defence must never silently compile away. */
#ifndef O_NOFOLLOW
#error "O_NOFOLLOW is required for the restart journal"
#endif
#ifndef O_CLOEXEC
#error "O_CLOEXEC is required for the restart journal"
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

/* ---- helpers ---- */

static void set_reason(char *dst, size_t cap, const char *a, const char *b) {
    size_t n = 0;
    if (cap == 0) return;
    for (; a && *a && n + 1 < cap; a++) dst[n++] = *a;
    for (; b && *b && n + 1 < cap; b++) dst[n++] = *b;
    dst[n] = '\0';
}

static void log_event(qw_gw_state_t *st, uint64_t now_ms, uint32_t seq, uint32_t bed,
                      uint32_t alarm, qw_log_state_t state, const char *r1, const char *r2) {
    qw_log_entry_t *e = &st->log[st->log_total % QW_LOG_RING];
    e->t_ms = now_ms;
    e->seq = seq;
    e->bed_id = bed;
    e->alarm_id = alarm;
    e->state = state;
    set_reason(e->reason, sizeof e->reason, r1, r2);
    st->log_total++;
}

static void latch_confirm(qw_gw_state_t *st, uint64_t now_ms, uint32_t seq, uint32_t bed,
                          uint32_t alarm, const char *reason) {
    if (st->beds[bed].latched_confirm < UINT32_MAX) st->beds[bed].latched_confirm++;
    log_event(st, now_ms, seq, bed, alarm, QW_LOG_LATCHED, reason, NULL);
}

static int seq_active(const qw_gw_state_t *st, uint32_t seq) {
    for (int i = 0; i < QW_MAX_PENDING; i++) {
        if (st->pending[i].used && st->pending[i].seq == seq) return 1;
        if (st->review[i].used && st->review[i].seq == seq) return 1;
    }
    return 0;
}

static void reply_fill(qw_msg_reply_t *r, qw_status_t s, uint32_t latched) {
    if (r == NULL) return;
    r->version = QW_IPC_VERSION;
    r->status = (int32_t)s;
    r->latched = latched;
}

/* ---- state machine ---- */

void qw_gw_init(qw_gw_state_t *st, uint32_t watchdog_ms, uint32_t review_escalate_ms) {
    if (st == NULL) return;
    memset(st, 0, sizeof *st);
    st->deadline_ms = watchdog_ms;
    st->escalate_ms = review_escalate_ms;
    for (uint32_t b = 0; b < QW_MAX_BEDS; b++) st->beds[b].bed_id = b;
}

qw_status_t qw_gw_raise_alarm(qw_gw_state_t *st, uint32_t bed_id, qw_alarm_id_t alarm_id,
                              uint32_t seq, uint64_t now_ms, int *out_failopen_latched) {
    if (out_failopen_latched) *out_failopen_latched = 0;
    if (st == NULL || bed_id >= QW_MAX_BEDS || (unsigned)alarm_id >= (unsigned)QW_ALARM_COUNT) {
        return QW_ST_BAD_ARGS;
    }
    if (seq_active(st, seq)) {
        /* A colliding seq must never silently drop an alarm: sound it. */
        latch_confirm(st, now_ms, seq, bed_id, (uint32_t)alarm_id, "duplicate alarm seq (fail-open)");
        if (out_failopen_latched) *out_failopen_latched = 1;
        return QW_ST_DUPLICATE;
    }

    for (int i = 0; i < QW_MAX_PENDING; i++) {
        if (!st->pending[i].used) {
            st->pending[i] = (qw_pending_t){1, bed_id, (uint32_t)alarm_id, seq, now_ms};
            st->beds[bed_id].last_seq = seq;
            return QW_ST_OK;
        }
    }
    latch_confirm(st, now_ms, seq, bed_id, (uint32_t)alarm_id,
                  "pending table full (fail-open)");
    st->beds[bed_id].last_seq = seq;
    if (out_failopen_latched) *out_failopen_latched = 1;
    return QW_ST_BUSY;
}

qw_status_t qw_gw_apply_decision(qw_gw_state_t *st, const qw_msg_decision_t *msg,
                                 uint64_t now_ms, qw_msg_reply_t *reply) {
    qw_status_t s;
    uint32_t latched = 0;

    if (st == NULL || msg == NULL) {
        reply_fill(reply, QW_ST_BAD_ARGS, 0);
        return QW_ST_BAD_ARGS;
    }
    if (msg->version != QW_IPC_VERSION) {
        reply_fill(reply, QW_ST_BAD_VERSION, 0);
        return QW_ST_BAD_VERSION;
    }
    if (msg->bed_id >= QW_MAX_BEDS || msg->alarm_id >= (uint32_t)QW_ALARM_COUNT ||
        (msg->decision != (uint32_t)QW_DECISION_CONFIRM &&
         msg->decision != (uint32_t)QW_DECISION_SUPPRESS_TO_REVIEW)) {
        reply_fill(reply, QW_ST_BAD_ARGS, 0);
        return QW_ST_BAD_ARGS;
    }

    int idx = -1;
    for (int i = 0; i < QW_MAX_PENDING; i++) {
        const qw_pending_t *p = &st->pending[i];
        if (p->used && p->seq == msg->seq && p->bed_id == msg->bed_id &&
            p->alarm_id == msg->alarm_id) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        reply_fill(reply, QW_ST_NOT_FOUND, 0);
        return QW_ST_NOT_FOUND;
    }

    {
        const qw_pending_t *p = &st->pending[idx];
        if (now_ms < p->raised_ms || now_ms - p->raised_ms >= st->deadline_ms) {
            /* Decision arrived after the deadline (or the clock stepped back):
             * the detector is not trusted for this alarm. Fail open. */
            st->pending[idx].used = 0;
            latch_confirm(st, now_ms, msg->seq, msg->bed_id, msg->alarm_id,
                          "decision after deadline (fail-open)");
            st->beds[msg->bed_id].detector_alive = 0;
            st->beds[msg->bed_id].failopen_active = 1;
            reply_fill(reply, QW_ST_BUSY, 1);
            return QW_ST_BUSY;
        }
    }

    st->pending[idx].used = 0;
    st->beds[msg->bed_id].detector_alive = 1;
    st->beds[msg->bed_id].failopen_active = 0;
    st->beds[msg->bed_id].last_seq = msg->seq;
    s = QW_ST_OK;

    if (msg->decision == (uint32_t)QW_DECISION_CONFIRM) {
        latch_confirm(st, now_ms, msg->seq, msg->bed_id, msg->alarm_id, "detector decision: CONFIRM");
        latched = 1;
    } else {
        int slot = -1;
        for (int i = 0; i < QW_MAX_PENDING; i++) {
            if (!st->review[i].used) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            latch_confirm(st, now_ms, msg->seq, msg->bed_id, msg->alarm_id,
                          "review queue full (fail-open)");
            latched = 1;
            s = QW_ST_BUSY;
        } else {
            st->review[slot] = (qw_review_t){1, msg->bed_id, msg->alarm_id, msg->seq, now_ms};
            log_event(st, now_ms, msg->seq, msg->bed_id, msg->alarm_id, QW_LOG_REVIEW,
                      "suppressed to nurse review", NULL);
        }
    }
    reply_fill(reply, s, latched);
    return s;
}

size_t qw_gw_check_deadlines(qw_gw_state_t *st, uint64_t now_ms) {
    size_t n = 0;
    if (st == NULL) return 0;
    for (int i = 0; i < QW_MAX_PENDING; i++) {
        qw_pending_t *p = &st->pending[i];
        if (!p->used) continue;
        /* now_ms < raised_ms (backward clock step) counts as expired. */
        if (now_ms >= p->raised_ms && now_ms - p->raised_ms < st->deadline_ms) continue;
        p->used = 0;
        st->beds[p->bed_id].detector_alive = 0;
        st->beds[p->bed_id].failopen_active = 1;
        latch_confirm(st, now_ms, p->seq, p->bed_id, p->alarm_id,
                      "no decision within deadline (fail-open)");
        n++;
    }
    return n;
}

size_t qw_gw_check_escalations(qw_gw_state_t *st, uint64_t now_ms) {
    size_t n = 0;
    if (st == NULL) return 0;
    for (int i = 0; i < QW_MAX_PENDING; i++) {
        qw_review_t *r = &st->review[i];
        if (!r->used) continue;
        if (now_ms >= r->queued_ms && now_ms - r->queued_ms < st->escalate_ms) continue;
        r->used = 0;
        latch_confirm(st, now_ms, r->seq, r->bed_id, r->alarm_id,
                      "review not acknowledged in time (escalated)");
        n++;
    }
    return n;
}

/* ---- ACK parsing ---- */

static int ack_uint32(const char *text, uint32_t *out) {
    size_t n = strlen(text);
    if (n == 0 || n > 10) return -1;
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (text[i] < '0' || text[i] > '9') return -1; /* no sign, fraction or exponent */
        v = v * 10u + (uint64_t)(text[i] - '0');
    }
    if (v > UINT32_MAX) return -1;
    *out = (uint32_t)v;
    return 0;
}

qw_status_t qw_gw_parse_ack_json(const char *buf, size_t len, uint32_t *out_seq, char *out_nurse,
                                 size_t nurse_cap) {
    if (buf == NULL || out_seq == NULL || out_nurse == NULL || nurse_cap < 2) return QW_ST_BAD_ARGS;
    if (len == 0 || len > QW_ACK_MAX_BYTES) return QW_ST_BAD_ARGS;
    if (memchr(buf, '\0', len) != NULL) return QW_ST_BAD_ARGS; /* embedded NUL */

    qw_lexer_t lx = {buf, len, 0};
    qw_token_t t;
    int have_ack = 0, have_nurse = 0;
    uint32_t seq = 0;
    char nurse[512];
    nurse[0] = '\0';

    if (qw_lex_next(&lx, &t) != 0 || t.kind != QW_TOK_LBRACE) return QW_ST_BAD_ARGS;
    if (qw_lex_next(&lx, &t) != 0) return QW_ST_BAD_ARGS;
    if (t.kind == QW_TOK_RBRACE) return QW_ST_BAD_ARGS; /* empty object: required keys missing */

    for (;;) {
        if (t.kind != QW_TOK_STRING) return QW_ST_BAD_ARGS;
        int is_ack = strcmp(t.text, "ack") == 0;
        int is_nurse = strcmp(t.text, "nurse") == 0;
        if (qw_lex_next(&lx, &t) != 0 || t.kind != QW_TOK_COLON) return QW_ST_BAD_ARGS;
        if (qw_lex_next(&lx, &t) != 0) return QW_ST_BAD_ARGS;

        if (is_ack) {
            if (have_ack || t.kind != QW_TOK_NUMBER || ack_uint32(t.text, &seq) != 0) {
                return QW_ST_BAD_ARGS;
            }
            have_ack = 1;
        } else if (is_nurse) {
            if (have_nurse || t.kind != QW_TOK_STRING) return QW_ST_BAD_ARGS;
            size_t n = strlen(t.text);
            if (n == 0 || n >= nurse_cap) return QW_ST_BAD_ARGS;
            for (size_t i = 0; i < n; i++) {
                unsigned char c = (unsigned char)t.text[i];
                if (c < 0x20 || c == 0x7f) return QW_ST_BAD_ARGS;
            }
            memcpy(nurse, t.text, n + 1);
            have_nurse = 1;
        } else if (qw_skip_value_depth(&lx, &t, 0) != 0) {
            return QW_ST_BAD_ARGS;
        }

        if (qw_lex_next(&lx, &t) != 0) return QW_ST_BAD_ARGS;
        if (t.kind == QW_TOK_RBRACE) break;
        if (t.kind != QW_TOK_COMMA) return QW_ST_BAD_ARGS;
        if (qw_lex_next(&lx, &t) != 0) return QW_ST_BAD_ARGS;
    }

    if (qw_lex_next(&lx, &t) != 0 || t.kind != QW_TOK_EOF) return QW_ST_BAD_ARGS; /* trailing junk */
    if (!have_ack || !have_nurse) return QW_ST_BAD_ARGS;

    *out_seq = seq;
    memcpy(out_nurse, nurse, strlen(nurse) + 1);
    return QW_ST_OK;
}

qw_status_t qw_gw_ack_review(qw_gw_state_t *st, uint32_t seq, const char *nurse, uint64_t now_ms) {
    if (st == NULL || nurse == NULL || nurse[0] == '\0') return QW_ST_BAD_ARGS;
    for (int i = 0; i < QW_MAX_PENDING; i++) {
        qw_review_t *r = &st->review[i];
        if (r->used && r->seq == seq) {
            if (now_ms < r->queued_ms || now_ms - r->queued_ms >= st->escalate_ms) {
                /* Too late (or clock stepped back): the entry has escalated. */
                r->used = 0;
                latch_confirm(st, now_ms, seq, r->bed_id, r->alarm_id,
                              "nurse ack after escalation (fail-open)");
                return QW_ST_BUSY;
            }
            r->used = 0;
            log_event(st, now_ms, seq, r->bed_id, r->alarm_id, QW_LOG_ACKED, "nurse ack: ", nurse);
            return QW_ST_OK;
        }
    }
    return QW_ST_NOT_FOUND;
}

qw_status_t qw_gw_get_bed_state(const qw_gw_state_t *st, uint32_t bed_id, qw_bed_state_t *out) {
    if (st == NULL || out == NULL || bed_id >= QW_MAX_BEDS) return QW_ST_BAD_ARGS;
    *out = st->beds[bed_id];
    out->pending = 0;
    out->in_review = 0;
    for (int i = 0; i < QW_MAX_PENDING; i++) {
        if (st->pending[i].used && st->pending[i].bed_id == bed_id) out->pending++;
        if (st->review[i].used && st->review[i].bed_id == bed_id) out->in_review++;
    }
    return QW_ST_OK;
}

/* ---- journal ---- */

static void failopen_all(qw_gw_state_t *st, const char *reason) {
    for (uint32_t b = 0; b < QW_MAX_BEDS; b++) {
        st->beds[b].failopen_active = 1;
        latch_confirm(st, 0, 0, b, 0, reason);
    }
}

int qw_gw_journal_append(const char *path, char op, uint32_t seq, uint32_t bed_id,
                         uint32_t alarm_id, const char *note) {
    if (path == NULL || (op != 'R' && op != 'C' && op != 'Q' && op != 'A')) return -1;
    char clean[64];
    size_t n = 0;
    if (note != NULL) {
        for (; note[n] && n + 1 < sizeof clean; n++) {
            unsigned char c = (unsigned char)note[n];
            clean[n] = (c > 0x20 && c < 0x7f) ? (char)c : '_';
        }
    }
    if (n == 0) clean[n++] = '-';
    clean[n] = '\0';

    /* O_NONBLOCK: a planted FIFO must not block us. fstat before use: only a
     * regular, single-link file owned by us is ever appended to. */
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) return -1;
    struct stat sb;
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_uid != geteuid() ||
        sb.st_nlink != 1) {
        close(fd);
        return -1;
    }
    FILE *f = fdopen(fd, "a");
    if (f == NULL) {
        close(fd);
        return -1;
    }
    int rc = 0;
    if (fprintf(f, "%c %" PRIu32 " %" PRIu32 " %" PRIu32 " %s\n", op, seq, bed_id, alarm_id,
                clean) < 0) {
        rc = -1;
    }
    if (fflush(f) != 0) rc = -1;
    if (rc == 0 && fsync(fileno(f)) != 0) rc = -1;
    if (fclose(f) != 0) rc = -1;

    /* Best effort: make the (possibly new) directory entry durable too. */
    if (rc == 0) {
        char dir[256];
        size_t pl = strlen(path);
        if (pl < sizeof dir) {
            memcpy(dir, path, pl + 1);
            char *slash = strrchr(dir, '/');
            if (slash == NULL) {
                strcpy(dir, ".");
            } else if (slash == dir) {
                dir[1] = '\0';
            } else {
                *slash = '\0';
            }
            int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (dfd >= 0) {
                (void)fsync(dfd); /* EINVAL/ENOTSUP on some filesystems: ignored */
                close(dfd);
            }
        }
    }
    return rc;
}

typedef struct {
    uint32_t seq, bed, alarm;
    char op; /* 'R' or 'Q' */
} open_entry_t;

/* Strict unsigned decimal: digits only (no sign, no space), no overflow. On
 * success *p points just past the number. */
static int parse_u32(const char **p, uint32_t *out) {
    const char *s = *p;
    if (*s < '0' || *s > '9') return -1;
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno == ERANGE || end == s || v > UINT32_MAX) return -1;
    *out = (uint32_t)v;
    *p = end;
    return 0;
}

/* Parse "<op> <seq> <bed> <alarm>[ <note>]" (newline already stripped). */
static int parse_journal_line(const char *line, char *op, uint32_t *seq, uint32_t *bed,
                              uint32_t *alarm) {
    const char *p = line;
    char o = *p++;
    if (o != 'R' && o != 'Q' && o != 'A' && o != 'C') return -1;
    if (*p++ != ' ' || parse_u32(&p, seq) != 0) return -1;
    if (*p++ != ' ' || parse_u32(&p, bed) != 0) return -1;
    if (*p++ != ' ' || parse_u32(&p, alarm) != 0) return -1;
    if (*p != '\0' && *p != ' ') return -1;
    if (*bed >= QW_MAX_BEDS || *alarm >= (uint32_t)QW_ALARM_COUNT) return -1;
    *op = o;
    return 0;
}

int qw_gw_journal_replay(const char *path, qw_gw_state_t *st) {
    if (path == NULL || st == NULL) return -1;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) return 0;
        failopen_all(st, "restart journal open failed (fail-open)");
        return (int)QW_MAX_BEDS;
    }
    struct stat sb;
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_uid != geteuid() ||
        sb.st_nlink != 1) {
        close(fd);
        failopen_all(st, "restart journal not a private regular file (fail-open)");
        return (int)QW_MAX_BEDS;
    }
    FILE *f = fdopen(fd, "r");
    if (f == NULL) {
        close(fd);
        failopen_all(st, "restart journal open failed (fail-open)");
        return (int)QW_MAX_BEDS;
    }

    static const char corrupt[] = "corrupt restart journal record (fail-open)";
    open_entry_t open_tab[QW_JOURNAL_MAX_OPEN];
    size_t n_open = 0;
    char line[160];

    while (fgets(line, sizeof line, f) != NULL) {
        size_t ll = strlen(line);
        int has_nl = ll > 0 && line[ll - 1] == '\n';
        if (has_nl) line[--ll] = '\0';
        /* No newline and more data to come: over-long, NUL-bearing or
         * truncated mid-file. Empty lines are corrupt too. */
        if ((!has_nl && !feof(f)) || ll == 0) goto corrupt_journal;

        char op;
        uint32_t seq, bed, alarm;
        if (parse_journal_line(line, &op, &seq, &bed, &alarm) != 0) goto corrupt_journal;
        /* A torn final line is only trusted when it is an open (R/Q) record:
         * that bed then latches CONFIRM. A torn A/C could hide a live alarm. */
        if (!has_nl && op != 'R' && op != 'Q') goto corrupt_journal;

        size_t at = n_open;
        for (size_t i = 0; i < n_open; i++) {
            if (open_tab[i].seq == seq) {
                at = i;
                break;
            }
        }
        /* An existing open entry for this seq must agree on bed and alarm for
         * every record type; a mismatch means the journal is corrupt. */
        if (at < n_open && (open_tab[at].bed != bed || open_tab[at].alarm != alarm)) {
            goto corrupt_journal;
        }
        if (op == 'R' && at < n_open) {
            continue; /* identical duplicate raise: keep the entry open as is */
        }
        if (op == 'R' || op == 'Q') {
            if (at == n_open) {
                if (n_open == QW_JOURNAL_MAX_OPEN) goto corrupt_journal; /* table overflow */
                n_open++;
            }
            open_tab[at] = (open_entry_t){seq, bed, alarm, op};
        } else if (at < n_open && (op == 'C' || (op == 'A' && open_tab[at].op == 'Q'))) {
            open_tab[at] = open_tab[--n_open];
        }
    }
    if (ferror(f)) goto corrupt_journal;
    fclose(f);

    int latched = 0;
    for (size_t i = 0; i < n_open; i++) {
        st->beds[open_tab[i].bed].failopen_active = 1;
        latch_confirm(st, 0, open_tab[i].seq, open_tab[i].bed, open_tab[i].alarm,
                      open_tab[i].op == 'R' ? "restart: unresolved raise (fail-open)"
                                            : "restart: unacked review (fail-open)");
        latched++;
    }
    return latched;

corrupt_journal:
    fclose(f);
    failopen_all(st, corrupt);
    return (int)QW_MAX_BEDS;
}
