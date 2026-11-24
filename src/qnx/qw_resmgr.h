/* qw_resmgr.h -- /dev/quietward resource manager (QNX) and its portable
 * JSONL serializer (host-tested).
 *
 * qw_gw_format_log_jsonl() is pure C11 and always compiled. The resource
 * manager itself needs <sys/dispatch.h> and exists only under __QNX__; on
 * other hosts qw_resmgr_run() is a stub returning -1.
 */
#ifndef QW_RESMGR_H
#define QW_RESMGR_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "qw_gateway_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime files shared by the gateway processes (directory is QW_STATE_DIR). */
#define QW_JOURNAL_PATH QW_STATE_DIR "/journal"
#define QW_ALARM_OUT_PATH QW_STATE_DIR "/alarm_out"

/* Serialize the entries currently held in st->log, oldest first, one JSON
 * object per line:
 *   {"seq":N,"bed":B,"alarm":"...","decision":"...","reason":"...","state":"...","ts_ms":T}\n
 * Only complete lines are written; output stops before the first line that
 * does not fit, and buf is always NUL-terminated. *out_len (may be NULL)
 * gets the byte count excluding the NUL. Returns 0 on success (including an
 * empty log), -1 if st == NULL, buf == NULL or cap == 0. */
int qw_gw_format_log_jsonl(const qw_gw_state_t *st, char *buf, size_t cap, size_t *out_len);

typedef struct {
    qw_gw_state_t *st;
    pthread_mutex_t *mu;       /* protects *st; shared with the alarm manager */
    const char *journal_path;  /* nurse ACKs are appended here as 'A' */
    uint64_t (*now_ms_fn)(void); /* monotonic milliseconds; NULL = CLOCK_MONOTONIC */
    /* Optional causal-order ACK hook: flushes prior log entries (Q/C) to disk
     * before appending 'A' without holding mu across fsync. */
    void (*ack_journal_fn)(uint32_t seq, uint32_t bed, uint32_t alarm, const char *nurse);
} qw_resmgr_ctx_t;

#ifdef __QNX__
/* Attach QW_DEV_PATH and serve until the process is stopped. Returns -1 on
 * setup failure (never returns on success). */
int qw_resmgr_run(qw_resmgr_ctx_t *ctx);
#else
int qw_resmgr_run(void *unused);
#endif

#ifdef __cplusplus
}
#endif

#endif /* QW_RESMGR_H */
