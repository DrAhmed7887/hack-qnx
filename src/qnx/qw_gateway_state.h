/* qw_gateway_state.h -- alarm manager + /dev/quietward state machine.
 *
 * Portable C11, no heap allocation, no QNX calls: the logic is testable on
 * the host and the QNX resource manager is a thin wrapper around it.
 *
 * Fail-open rule: every fault path (full tables, missed detector deadline,
 * unacknowledged review, restart with unresolved journal entries) latches
 * CONFIRM, i.e. the alarm is sounded. An alarm is never dropped.
 */
#ifndef QW_GATEWAY_STATE_H
#define QW_GATEWAY_STATE_H

#include "qw_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int used;
    uint32_t bed_id, alarm_id, seq;
    uint64_t raised_ms;
} qw_pending_t;

typedef struct {
    int used;
    uint32_t bed_id, alarm_id, seq;
    uint64_t queued_ms;
} qw_review_t;

typedef enum {
    QW_LOG_LATCHED = 0, /* latched CONFIRM */
    QW_LOG_REVIEW,      /* queued for nurse review */
    QW_LOG_ACKED        /* nurse acknowledged a review entry */
} qw_log_state_t;

#define QW_LOG_REASON_LEN 96

typedef struct {
    uint64_t t_ms;
    uint32_t seq, bed_id, alarm_id;
    qw_log_state_t state;
    char reason[QW_LOG_REASON_LEN];
} qw_log_entry_t;

typedef struct {
    uint32_t deadline_ms;
    uint32_t escalate_ms;
    qw_pending_t pending[QW_MAX_PENDING];
    qw_review_t review[QW_MAX_PENDING];
    qw_bed_state_t beds[QW_MAX_BEDS];
    qw_log_entry_t log[QW_LOG_RING]; /* ring: newest at (log_total - 1) % QW_LOG_RING */
    uint64_t log_total;
} qw_gw_state_t;

/* watchdog_ms is the detector decision deadline (QW_DECISION_DEADLINE_MS). */
void qw_gw_init(qw_gw_state_t *st, uint32_t watchdog_ms, uint32_t review_escalate_ms);

/* Register a raised alarm awaiting a detector decision. A full pending
 * table latches CONFIRM at once (*out_failopen_latched = 1) and returns
 * QW_ST_BUSY. A duplicate seq also latches CONFIRM on bed_id
 * (*out_failopen_latched = 1) and returns QW_ST_DUPLICATE. */
qw_status_t qw_gw_raise_alarm(qw_gw_state_t *st, uint32_t bed_id, qw_alarm_id_t alarm_id,
                              uint32_t seq, uint64_t now_ms, int *out_failopen_latched);

/* Apply a detector decision. reply may be NULL. A valid, in-time decision
 * marks the bed's detector alive and clears its fail-open flag. A decision
 * at or after the deadline (or after a backward clock step) removes the
 * pending entry, latches CONFIRM and returns QW_ST_BUSY (reply->latched=1). */
qw_status_t qw_gw_apply_decision(qw_gw_state_t *st, const qw_msg_decision_t *msg,
                                 uint64_t now_ms, qw_msg_reply_t *reply);

/* Latch CONFIRM for pending alarms past their deadline, or whose raise time
 * is in the future (backward clock step); returns the count. */
size_t qw_gw_check_deadlines(qw_gw_state_t *st, uint64_t now_ms);

/* Escalate unacknowledged review entries to CONFIRM (also on a backward
 * clock step); returns the count. */
size_t qw_gw_check_escalations(qw_gw_state_t *st, uint64_t now_ms);

/* Parse {"ack": <uint32>, "nurse": "<name>"}. Unknown extra keys are
 * ignored (forward compatibility). Rejected with QW_ST_BAD_ARGS: duplicate
 * "ack" or "nurse", embedded NUL bytes anywhere in buf[0..len), control
 * characters in the nurse name, oversize input (> QW_ACK_MAX_BYTES), bad
 * integers (sign, fraction, exponent, > UINT32_MAX), wrong types, missing
 * keys and trailing tokens. */
qw_status_t qw_gw_parse_ack_json(const char *buf, size_t len, uint32_t *out_seq, char *out_nurse,
                                 size_t nurse_cap);

/* Nurse ACK of a review entry. An ACK at or after the escalation time
 * (or after a backward clock step) is too late: the entry is removed,
 * latched CONFIRM and QW_ST_BUSY is returned. */
qw_status_t qw_gw_ack_review(qw_gw_state_t *st, uint32_t seq, const char *nurse, uint64_t now_ms);

qw_status_t qw_gw_get_bed_state(const qw_gw_state_t *st, uint32_t bed_id, qw_bed_state_t *out);

/* Append one line to the journal (O_NOFOLLOW, mode 0600), fsync the file and
 * best-effort fsync its directory. op: 'R' raise, 'C' confirm
 * latched, 'Q' queued for review, 'A' nurse ack. Returns 0 on success. */
int qw_gw_journal_append(const char *path, char op, uint32_t seq, uint32_t bed_id,
                         uint32_t alarm_id, const char *note);

/* Replay at restart: every R or Q with no later C (or, for Q, A) is
 * latched CONFIRM; the count is returned. A missing or empty journal is 0.
 * Anything untrustworthy fails open across ALL beds and returns
 * QW_MAX_BEDS: a corrupt, empty-line, over-long or NUL-bearing record, a
 * non-strict number (sign, overflow, bed or alarm out of range), a torn
 * final A/C line, open-table overflow, a read error, or an open failure
 * other than ENOENT (e.g. a symlink: opened with O_NOFOLLOW). A torn final
 * R/Q line that still parses is kept open, so its bed latches CONFIRM.
 * -1 only for NULL arguments. Call after qw_gw_init. */
int qw_gw_journal_replay(const char *path, qw_gw_state_t *st);

#ifdef __cplusplus
}
#endif

#endif /* QW_GATEWAY_STATE_H */
