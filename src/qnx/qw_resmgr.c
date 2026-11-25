/* qw_resmgr.c -- JSONL log serializer (portable) and /dev/quietward
 * resource manager (QNX only). See qw_resmgr.h. */
#include "qw_resmgr.h"

#include <stdio.h>
#include <string.h>

/* ---- portable JSONL serializer ---- */

/* Worst case per line: reason 96 bytes * 6 (\u00xx) + alarm/decision/state +
 * fixed keys and three numbers is well under this. */
#define JSONL_LINE_MAX 1024

typedef struct {
    char *p;
    size_t cap;
    size_t n;
    int overflow;
} jw_t;

static void jw_ch(jw_t *w, char c) {
    if (w->n + 1 < w->cap) {
        w->p[w->n++] = c;
    } else {
        w->overflow = 1;
    }
}

static void jw_lit(jw_t *w, const char *s) {
    for (; *s != '\0'; s++) jw_ch(w, *s);
}

static void jw_u64(jw_t *w, uint64_t v) {
    char tmp[21];
    int i = 0;
    do {
        tmp[i++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0);
    while (i > 0) jw_ch(w, tmp[--i]);
}

/* JSON string body with escapes; s is read for at most max bytes. */
static void jw_str(jw_t *w, const char *s, size_t max) {
    static const char hex[] = "0123456789abcdef";
    jw_ch(w, '"');
    for (size_t i = 0; i < max && s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': jw_lit(w, "\\\""); break;
        case '\\': jw_lit(w, "\\\\"); break;
        case '\b': jw_lit(w, "\\b"); break;
        case '\f': jw_lit(w, "\\f"); break;
        case '\n': jw_lit(w, "\\n"); break;
        case '\r': jw_lit(w, "\\r"); break;
        case '\t': jw_lit(w, "\\t"); break;
        default:
            if (c < 0x20) {
                jw_lit(w, "\\u00");
                jw_ch(w, hex[c >> 4]);
                jw_ch(w, hex[c & 0xF]);
            } else {
                jw_ch(w, (char)c);
            }
        }
    }
    jw_ch(w, '"');
}

static void jsonl_entry(jw_t *w, const qw_log_entry_t *e) {
    const char *alarm = qw_alarm_name((qw_alarm_id_t)e->alarm_id);
    const char *decision, *state;
    switch (e->state) {
    case QW_LOG_LATCHED: decision = "CONFIRM"; state = "latched"; break;
    case QW_LOG_REVIEW: decision = "SUPPRESS_TO_REVIEW"; state = "review"; break;
    case QW_LOG_ACKED: decision = "ACK"; state = "acked"; break;
    default: decision = "unknown"; state = "unknown"; break;
    }
    jw_lit(w, "{\"seq\":");
    jw_u64(w, e->seq);
    jw_lit(w, ",\"bed\":");
    jw_u64(w, e->bed_id);
    jw_lit(w, ",\"alarm\":");
    jw_str(w, alarm != NULL ? alarm : "unknown", 64);
    jw_lit(w, ",\"decision\":");
    jw_str(w, decision, 32);
    jw_lit(w, ",\"reason\":");
    jw_str(w, e->reason, sizeof e->reason);
    jw_lit(w, ",\"state\":");
    jw_str(w, state, 16);
    jw_lit(w, ",\"ts_ms\":");
    jw_u64(w, e->t_ms);
    jw_lit(w, "}\n");
}

int qw_gw_format_log_jsonl(const qw_gw_state_t *st, char *buf, size_t cap, size_t *out_len) {
    if (st == NULL || buf == NULL || cap == 0) return -1;
    buf[0] = '\0';
    if (out_len != NULL) *out_len = 0;
    if (st->log_total == 0) return 0;

    uint64_t held = st->log_total < (uint64_t)QW_LOG_RING ? st->log_total : (uint64_t)QW_LOG_RING;
    uint64_t first = st->log_total - held;
    size_t used = 0;
    for (uint64_t k = first; k < st->log_total; k++) {
        char line[JSONL_LINE_MAX];
        jw_t w = {line, sizeof line, 0, 0};
        jsonl_entry(&w, &st->log[k % QW_LOG_RING]);
        if (w.overflow || used + w.n + 1 > cap) break; /* never write half a line */
        memcpy(buf + used, line, w.n);
        used += w.n;
    }
    buf[used] = '\0';
    if (out_len != NULL) *out_len = used;
    return 0;
}

/* ---- /dev/quietward resource manager ---- */

#ifdef __QNX__

#include <devctl.h>
#include <errno.h>
#include <stdlib.h>
struct qw_ocb;
typedef struct qw_ocb qw_ocb_t;
#define IOFUNC_OCB_T qw_ocb_t
#define RESMGR_OCB_T qw_ocb_t
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/resmgr.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "qw_log.h"

/* Full ring capacity: QW_LOG_RING (256) * worst-case single JSONL line. */
#define SNAPSHOT_MAX ((size_t)QW_LOG_RING * (size_t)JSONL_LINE_MAX)
#define MAX_OPEN_SNAPS 8

struct qw_ocb {
    iofunc_ocb_t base;
    char *snap;
    size_t len;
};

static qw_resmgr_ctx_t *g_ctx; /* one resource manager per process */
static int g_open_snaps = 0;   /* guarded by g_ctx->mu */

static qw_ocb_t *qw_ocb_calloc(resmgr_context_t *ctp, IOFUNC_ATTR_T *attr) {
    (void)ctp;
    (void)attr;
    return (qw_ocb_t *)calloc(1, sizeof(qw_ocb_t));
}

static void qw_ocb_free(qw_ocb_t *ocb) {
    if (ocb != NULL) {
        if (ocb->snap != NULL) {
            pthread_mutex_lock(g_ctx->mu);
            if (g_open_snaps > 0) g_open_snaps--;
            pthread_mutex_unlock(g_ctx->mu);
            free(ocb->snap);
        }
        free(ocb);
    }
}

static uint64_t ctx_now_ms(const qw_resmgr_ctx_t *c) {
    if (c->now_ms_fn != NULL) return c->now_ms_fn();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* cat /dev/quietward: snapshot ONCE on first read at offset 0 so concurrent
 * ticks/decisions never shift byte offsets mid-read. */
static int qw_io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb) {
    int status = iofunc_read_verify(ctp, msg, &ocb->base, NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;
    if (ocb->base.offset < 0) return EINVAL;

    if (ocb->snap == NULL && ocb->base.offset == 0) {
        pthread_mutex_lock(g_ctx->mu);
        if (g_open_snaps >= MAX_OPEN_SNAPS) {
            pthread_mutex_unlock(g_ctx->mu);
            return EBUSY;
        }
        g_open_snaps++;
        pthread_mutex_unlock(g_ctx->mu);

        ocb->snap = malloc(SNAPSHOT_MAX);
        if (ocb->snap == NULL) {
            pthread_mutex_lock(g_ctx->mu);
            if (g_open_snaps > 0) g_open_snaps--;
            pthread_mutex_unlock(g_ctx->mu);
            return ENOMEM;
        }
        size_t len = 0;
        pthread_mutex_lock(g_ctx->mu);
        int rc = qw_gw_format_log_jsonl(g_ctx->st, ocb->snap, SNAPSHOT_MAX, &len);
        pthread_mutex_unlock(g_ctx->mu);
        if (rc != 0) {
            pthread_mutex_lock(g_ctx->mu);
            if (g_open_snaps > 0) g_open_snaps--;
            pthread_mutex_unlock(g_ctx->mu);
            free(ocb->snap);
            ocb->snap = NULL;
            return EIO;
        }
        ocb->len = len;
    }

    size_t off = (size_t)ocb->base.offset;
    size_t req_bytes = (size_t)_IO_READ_GET_NBYTES(msg);
    size_t nbytes = 0;
    if (ocb->snap != NULL && off < ocb->len) {
        nbytes = ocb->len - off;
        if (nbytes > req_bytes) nbytes = req_bytes;
    }
    _IO_SET_READ_NBYTES(ctp, (int)nbytes);
    if (nbytes > 0) {
        MsgReply(ctp->rcvid, (int)nbytes, ocb->snap + off, nbytes);
        ocb->base.offset += (off_t)nbytes;
        ocb->base.attr->flags |= IOFUNC_ATTR_ATIME;
    } else {
        MsgReply(ctp->rcvid, 0, NULL, 0);
    }
    return _RESMGR_NOREPLY;
}

/* echo '{"ack":SEQ,"nurse":"NAME"}' > /dev/quietward */
static int qw_io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb) {
    int status = iofunc_write_verify(ctp, msg, &ocb->base, NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    size_t nbytes = (size_t)_IO_WRITE_GET_NBYTES(msg);
    if (nbytes > QW_ACK_MAX_BYTES) return EMSGSIZE;
    char buf[QW_ACK_MAX_BYTES + 1];
    if (nbytes > 0 && resmgr_msgget(ctp, buf, nbytes, sizeof(msg->i)) != (ssize_t)nbytes) return EIO;
    buf[nbytes] = '\0';
    _IO_SET_WRITE_NBYTES(ctp, (int)nbytes);

    uint32_t seq = 0;
    char nurse[64];
    if (qw_gw_parse_ack_json(buf, nbytes, &seq, nurse, sizeof nurse) != QW_ST_OK) return EINVAL;

    uint32_t bed = 0, alarm = 0;
    int found = 0;
    uint64_t now = ctx_now_ms(g_ctx);
    pthread_mutex_lock(g_ctx->mu);
    for (int i = 0; i < QW_MAX_PENDING; i++) {
        if (g_ctx->st->review[i].used && g_ctx->st->review[i].seq == seq) {
            bed = g_ctx->st->review[i].bed_id;
            alarm = g_ctx->st->review[i].alarm_id;
            found = 1;
            break;
        }
    }
    qw_status_t st = qw_gw_ack_review(g_ctx->st, seq, nurse, now);
    pthread_mutex_unlock(g_ctx->mu);

    if (st == QW_ST_OK) {
        if (found) {
            if (g_ctx->ack_journal_fn != NULL)
                g_ctx->ack_journal_fn(seq, bed, alarm, nurse);
            else if (g_ctx->journal_path != NULL)
                qw_gw_journal_append(g_ctx->journal_path, 'A', seq, bed, alarm, nurse);
        }
        qw_log_emit(QW_LOG_INFO, bed, seq, qw_alarm_name((qw_alarm_id_t)alarm), "ACK", "nurse acknowledged");
        return _RESMGR_NPARTS(0); /* status = nbytes from _IO_SET_WRITE_NBYTES */
    }
    /* A late ACK was latched CONFIRM by the state machine; the alarm manager
     * journals that 'C' on its next tick. */
    return st == QW_ST_BUSY ? EBUSY : ENOENT;
}

static int qw_io_devctl(resmgr_context_t *ctp, io_devctl_t *msg, RESMGR_OCB_T *ocb) {
    int status = iofunc_devctl_default(ctp, msg, &ocb->base);
    if (status != _RESMGR_DEFAULT) return status;

    if (msg->i.dcmd != QW_DCMD_BED_STATE) return ENOSYS;
    if (msg->i.nbytes < (int)sizeof(qw_bed_state_t)) return EINVAL;

    const qw_bed_state_t *in = (const qw_bed_state_t *)_DEVCTL_DATA(msg->i);
    uint32_t bed = in->bed_id;
    if (bed >= QW_MAX_BEDS) return EINVAL;

    qw_bed_state_t out;
    pthread_mutex_lock(g_ctx->mu);
    qw_status_t st = qw_gw_get_bed_state(g_ctx->st, bed, &out);
    pthread_mutex_unlock(g_ctx->mu);
    if (st != QW_ST_OK) return EINVAL;

    memset(&msg->o, 0, sizeof msg->o);
    msg->o.ret_val = EOK;
    msg->o.nbytes = (int)sizeof out;
    memcpy(_DEVCTL_DATA(msg->o), &out, sizeof out);
    return _RESMGR_PTR(ctp, &msg->o, sizeof(msg->o) + sizeof out);
}

int qw_resmgr_run(qw_resmgr_ctx_t *ctx) {
    if (ctx == NULL || ctx->st == NULL || ctx->mu == NULL) return -1;
    g_ctx = ctx;

    dispatch_t *dpp = dispatch_create();
    if (dpp == NULL) return -1;

    resmgr_attr_t rattr;
    memset(&rattr, 0, sizeof rattr);
    rattr.nparts_max = 1;
    rattr.msg_max_size = 2048;

    resmgr_connect_funcs_t cfuncs;
    resmgr_io_funcs_t ifuncs;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &cfuncs, _RESMGR_IO_NFUNCS, &ifuncs);
    ifuncs.read = qw_io_read;
    ifuncs.write = qw_io_write;
    ifuncs.devctl = qw_io_devctl;

    static iofunc_funcs_t ocb_funcs = {
        .nfuncs = _IOFUNC_NFUNCS,
        .ocb_calloc = qw_ocb_calloc,
        .ocb_free = qw_ocb_free,
    };
    static iofunc_mount_t mount = {
        .funcs = &ocb_funcs,
    };
    iofunc_attr_t ioattr;
    iofunc_attr_init(&ioattr, S_IFCHR | 0660, NULL, NULL); /* inherits process UID/GID; deploy under dedicated nurse-station service GID */
    ioattr.mount = &mount;

    if (resmgr_attach(dpp, &rattr, QW_DEV_PATH, _FTYPE_ANY, 0, &cfuncs, &ifuncs, &ioattr) == -1) {
        qw_log_emit(QW_LOG_ERROR, 0, 0, NULL, NULL, "resmgr_attach /dev/quietward failed");
        return -1;
    }

    dispatch_context_t *dctp = dispatch_context_alloc(dpp);
    if (dctp == NULL) return -1;
    for (;;) {
        dispatch_context_t *next = dispatch_block(dctp);
        if (next == NULL) {
            if (errno == EINTR) continue;
            break;
        }
        dctp = next;
        dispatch_handler(dctp);
    }
    return -1;
}

#else /* !__QNX__ */

int qw_resmgr_run(void *unused) {
    (void)unused;
    return -1;
}

#endif
