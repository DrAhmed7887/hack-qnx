/* qw_ipc.h -- Quiet Ward gateway IPC contract (portable C11).
 *
 * Shared message, status, pulse and shared-memory-ring definitions for the
 * detector, alarm manager and /dev/quietward resource manager. Everything
 * here compiles on the host (macOS/Linux) so the state machines can be
 * tested without a QNX target; the few QNX-only includes sit behind
 * #ifdef __QNX__. No clinical thresholds live here.
 */
#ifndef QW_IPC_H
#define QW_IPC_H

#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "qw_core.h"
#include "rules.h"

#ifdef __QNX__
#include <devctl.h>
#include <sys/neutrino.h>
#define QW_PULSE_BASE _PULSE_CODE_MINAVAIL
#else
#define QW_PULSE_BASE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define QW_IPC_VERSION 1

#define QW_NAME_DETECTOR "qw_detector"
#define QW_NAME_ALARM_MGR "qw_alarm_mgr"
#define QW_DEV_PATH "/dev/quietward"
#define QW_STATE_DIR "/var/run/quietward" /* root-owned 0700 runtime dir on QNX 8.0; never /tmp */
#define QW_RING_SHM_FMT "/qw_ring_bed%u"

#define QW_MAX_BEDS 4
#define QW_MAX_PENDING 16
#define QW_LOG_RING 256
#define QW_ACK_MAX_BYTES 512
#define QW_DECISION_DEADLINE_MS 500
#define QW_REVIEW_ESCALATE_MS 30000

#define QW_RING_MAGIC 0x51575247u

/* ---- alarm ids (names match the alarm types in rules/alarm_rules.json) ---- */

typedef enum {
    QW_ALARM_ASYSTOLE = 0,
    QW_ALARM_BRADYCARDIA,
    QW_ALARM_TACHYCARDIA,
    QW_ALARM_VT,
    QW_ALARM_VFLUTTER_FIB,
    QW_ALARM_COUNT
} qw_alarm_id_t;

static inline const char *qw_alarm_name(qw_alarm_id_t id) {
    static const char *const names[QW_ALARM_COUNT] = {
        "Asystole", "Bradycardia", "Tachycardia",
        "Ventricular_Tachycardia", "Ventricular_Flutter_Fib"};
    return ((unsigned)id < (unsigned)QW_ALARM_COUNT) ? names[id] : NULL;
}

/* Returns the id, or -1 if name is NULL or unknown. */
static inline int qw_alarm_id_from_name(const char *name) {
    if (name == NULL) return -1;
    for (int i = 0; i < (int)QW_ALARM_COUNT; i++) {
        if (strcmp(qw_alarm_name((qw_alarm_id_t)i), name) == 0) return i;
    }
    return -1;
}

/* ---- pulses ---- */

enum {
    QW_PULSE_SAMPLE_TICK = QW_PULSE_BASE + 0,
    QW_PULSE_ALARM_RAISED = QW_PULSE_BASE + 1,
    QW_PULSE_WATCHDOG_TICK = QW_PULSE_BASE + 2
};

/* Pulse value layout (32-bit pulse value):
 *   bit 31      always 0, so the value is a non-negative signed int
 *   bits 29:27  bed id        (3 bits, QW_MAX_BEDS <= 8)
 *   bits 26:24  alarm id      (3 bits, QW_ALARM_COUNT <= 8)
 *   bits 23:0   seq, low 24 bits only
 * The pulse carries only the low 24 bits of seq, as a wakeup hint. The
 * authoritative, full 32-bit seq travels in qw_msg_decision_t.seq; the
 * alarm manager matches decisions on that field, never on the pulse.
 * Unpacking casts to uint32_t first: no signed shifts. */
#define QW_PULSE_VAL(bed, aid, seq)                                  \
    ((int)((((uint32_t)(bed) & 0x7u) << 27) |                        \
           (((uint32_t)(aid) & 0x7u) << 24) | ((uint32_t)(seq) & 0xFFFFFFu)))
#define QW_PULSE_BED(v) ((uint32_t)(((uint32_t)(v) >> 27) & 0x7u))
#define QW_PULSE_AID(v) ((uint32_t)(((uint32_t)(v) >> 24) & 0x7u))
#define QW_PULSE_SEQ(v) ((uint32_t)((uint32_t)(v) & 0xFFFFFFu))

_Static_assert(QW_MAX_BEDS <= 8, "bed id must fit the 3-bit pulse field");
_Static_assert((int)QW_ALARM_COUNT <= 8, "alarm id must fit the 3-bit pulse field");
_Static_assert(((0x7u << 27) | (0x7u << 24) | 0xFFFFFFu) <= (uint32_t)INT32_MAX,
               "pulse value must stay non-negative as a signed int");
_Static_assert(sizeof(int) >= 4, "pulse value needs a 32-bit int");

/* ---- status codes ---- */

typedef enum {
    QW_ST_OK = 0,
    QW_ST_BAD_VERSION,
    QW_ST_BAD_ARGS,
    QW_ST_NOT_FOUND,
    QW_ST_DUPLICATE,
    QW_ST_BUSY,
    QW_ST_DETACHED
} qw_status_t;

#define QW_ST_UNKNOWN_SEQ QW_ST_NOT_FOUND

/* ---- detector -> alarm manager message ---- */

enum { QW_MSG_DECISION = 0x1001 };

typedef struct {
    uint32_t version;  /* QW_IPC_VERSION */
    uint32_t bed_id;   /* < QW_MAX_BEDS */
    uint32_t alarm_id; /* qw_alarm_id_t */
    uint32_t seq;      /* matches the seq of the raised alarm */
    uint32_t decision; /* qw_decision_t: QW_DECISION_CONFIRM / _SUPPRESS_TO_REVIEW */
} qw_msg_decision_t;

typedef struct {
    uint32_t version;
    int32_t status;   /* qw_status_t */
    uint32_t latched; /* 1 if the alarm ended up latched as CONFIRM */
} qw_msg_reply_t;

/* Per-bed state exposed through /dev/quietward. */
typedef struct {
    uint32_t bed_id;
    uint32_t detector_alive;  /* 0 until a decision arrives; 0 again after a missed deadline */
    uint32_t failopen_active; /* 1 after a fail-open latch caused by a missed deadline */
    uint32_t pending;         /* alarms awaiting a detector decision */
    uint32_t in_review;       /* suppressed alarms awaiting nurse ACK */
    uint32_t latched_confirm; /* total alarms latched as CONFIRM on this bed */
    uint32_t last_seq;
} qw_bed_state_t;

#ifdef __QNX__
#define QW_DCMD_BED_STATE __DIOTF(_DCMD_MISC, 1, qw_bed_state_t)
#endif

/* ---- shared-memory ring header ---- */

/* All header words that readers and the writer touch concurrently are
 * lock-free atomics (shared memory between processes needs lock-free). */
_Static_assert(sizeof(_Atomic uint32_t) == 4 && sizeof(_Atomic uint64_t) == 8 &&
                   ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2 &&
                   ATOMIC_LONG_LOCK_FREE == 2 && sizeof(_Atomic char) == 1 &&
                   ATOMIC_CHAR_LOCK_FREE == 2,
               "lock-free shared-memory atomics required");

typedef struct {
    _Atomic uint32_t magic; /* QW_RING_MAGIC; published last by init, cleared on detach */
    _Atomic uint32_t version;
    _Atomic uint32_t fs;
    _Atomic uint32_t n_channels;
    _Atomic uint32_t ring_len;  /* sample capacity per channel */
    _Atomic uint32_t write_pos; /* next sample slot, in [0, ring_len) */
    _Atomic uint32_t seq_lock;  /* odd while the writer is mid-update */
    _Atomic uint32_t writer_alive;
    _Atomic uint64_t total_written; /* samples per channel, ever */
    _Atomic char names[QW_MAX_CHANNELS][QW_CHANNEL_NAME_LEN]; /* read/written inside the seqlock */
} qw_ring_hdr_t;

#ifdef __cplusplus
}
#endif

#endif /* QW_IPC_H */
