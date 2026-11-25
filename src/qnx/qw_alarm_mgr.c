/* qw_alarm_mgr.c -- alarm manager process (QNX 8.0).
 *
 *   qw_alarm_mgr [--watchdog-ms N] [--escalate-ms N] [--journal PATH]
 *
 * Three threads share one qw_gw_state_t cleanly without blocking RT IPC:
 *   A: the /dev/quietward resource manager (qw_resmgr_run).
 *   B (main): real-time message loop on "qw_alarm_mgr": alarm pulses,
 *      50 ms watchdog tick, and detector decision messages (never fsyncs).
 *   C (journal_thread): wakes on signal to batch unseen log entries, fsync
 *      journal lines ('C'/'Q'), and emit slog2 audit records off the RT loop.
 *
 * Journal discipline: qw_acq durably writes 'R' via atomic O_APPEND write()
 * before pulsing qw_alarm_mgr, so a crash at any point replays as CONFIRM.
 * Every fault path in the state machine latches CONFIRM; nothing here can
 * drop an alarm.
 */
#ifdef __QNX__

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "qw_gateway_state.h"
#include "qw_ipc.h"
#include "qw_log.h"
#include "qw_resmgr.h"

#define WATCHDOG_TICK_MS 50

static qw_gw_state_t g_st;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_journal_mu = PTHREAD_MUTEX_INITIALIZER; /* lock order: g_journal_mu -> g_mu */
static pthread_mutex_t g_jnotify_mu = PTHREAD_MUTEX_INITIALIZER; /* leaf lock for journal_thread wakeup */
static pthread_cond_t g_journal_cv = PTHREAD_COND_INITIALIZER;
static uint64_t g_jnotify_seq = 0;
static int g_jnotify_stop = 0;
static int g_journal_degraded = 0; /* sticky latch under g_journal_mu: withhold A after any write failure */
static const char *g_journal = QW_JOURNAL_PATH;
static uint64_t g_seen; /* log entries already journalled and logged; guarded by g_journal_mu -> g_mu */
static volatile sig_atomic_t g_stop;

static void on_term(int sig) {
    (void)sig;
    g_stop = 1;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void *resmgr_thread(void *arg) {
    qw_resmgr_run((qw_resmgr_ctx_t *)arg);
    /* Fail-open: the alarm path keeps running without /dev/quietward. */
    qw_log_emit(QW_LOG_ERROR, 0, 0, NULL, NULL, "/dev/quietward resource manager stopped");
    return NULL;
}

/* Copy unseen log-ring entries under g_mu; flush disk fsync + slog2 outside g_mu. */
static size_t collect_log_batch(qw_log_entry_t *out, size_t cap) {
    if (g_st.log_total - g_seen > QW_LOG_RING) g_seen = g_st.log_total - QW_LOG_RING;
    size_t n = 0;
    for (; g_seen < g_st.log_total && n < cap; g_seen++) {
        out[n++] = g_st.log[g_seen % QW_LOG_RING];
    }
    return n;
}

static int flush_log_batch(const qw_log_entry_t *batch, size_t n) {
    int rc = 0;
    for (size_t i = 0; i < n; i++) {
        const qw_log_entry_t *e = &batch[i];
        const char *alarm = qw_alarm_name((qw_alarm_id_t)e->alarm_id);
        if (e->state == QW_LOG_LATCHED) {
            if (qw_gw_journal_append(g_journal, 'C', e->seq, e->bed_id, e->alarm_id, "latched") != 0) {
                g_journal_degraded = 1;
                rc = -1;
            }
            qw_log_emit(QW_LOG_CRIT, e->bed_id, e->seq, alarm, "CONFIRM", e->reason);
        } else if (e->state == QW_LOG_REVIEW) {
            if (qw_gw_journal_append(g_journal, 'Q', e->seq, e->bed_id, e->alarm_id, "review") != 0) {
                g_journal_degraded = 1;
                rc = -1;
            }
            qw_log_emit(QW_LOG_WARN, e->bed_id, e->seq, alarm, "SUPPRESS_TO_REVIEW", e->reason);
        }
        /* ACKED lines are journalled and logged by the resource manager. */
    }
    return rc;
}

/* Caller must hold g_journal_mu and must NOT hold g_mu. */
static int flush_pending_journal_locked(void) {
    qw_log_entry_t batch[QW_LOG_RING];
    pthread_mutex_lock(&g_mu);
    size_t n = collect_log_batch(batch, QW_LOG_RING);
    pthread_mutex_unlock(&g_mu);
    if (flush_log_batch(batch, n) != 0) g_journal_degraded = 1;
    return g_journal_degraded ? -1 : 0;
}

static void sync_and_flush_journal(void) {
    pthread_mutex_lock(&g_journal_mu);
    flush_pending_journal_locked();
    pthread_mutex_unlock(&g_journal_mu);
}

static void wake_journal_thread(void) {
    pthread_mutex_lock(&g_jnotify_mu);
    g_jnotify_seq++;
    pthread_cond_signal(&g_journal_cv);
    pthread_mutex_unlock(&g_jnotify_mu);
}

static void *journal_thread(void *arg) {
    (void)arg;
    uint64_t seen_seq = 0;
    for (;;) {
        pthread_mutex_lock(&g_jnotify_mu);
        while (g_jnotify_seq == seen_seq && !g_jnotify_stop) {
            pthread_cond_wait(&g_journal_cv, &g_jnotify_mu);
        }
        seen_seq = g_jnotify_seq;
        int stop = g_jnotify_stop;
        pthread_mutex_unlock(&g_jnotify_mu);

        sync_and_flush_journal();
        if (stop) break;
    }
    return NULL;
}

static void journal_ack_ordered(uint32_t seq, uint32_t bed, uint32_t alarm, const char *nurse) {
    pthread_mutex_lock(&g_journal_mu);
    if (flush_pending_journal_locked() != 0) {
        qw_log_emit(QW_LOG_CRIT, bed, seq, qw_alarm_name((qw_alarm_id_t)alarm), "CONFIRM",
                    "prior journal flush failed; withholding A so replay fails open");
    } else if (qw_gw_journal_append(g_journal, 'A', seq, bed, alarm, nurse) != 0) {
        g_journal_degraded = 1;
        qw_log_emit(QW_LOG_CRIT, bed, seq, qw_alarm_name((qw_alarm_id_t)alarm), "CONFIRM",
                    "journal A append failed; marking journal degraded");
    }
    pthread_mutex_unlock(&g_journal_mu);
}

static int write_alarm_out_failopen(void) {
    const char *apath = getenv("QW_ALARM_OUT_PATH");
    if (apath == NULL || apath[0] == '\0') apath = QW_ALARM_OUT_PATH;
    int fd = open(apath, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) return -1;
    static const char msg[] = "CONFIRM_ALL\n";
    struct stat sb;
    int ok = fstat(fd, &sb) == 0 && S_ISREG(sb.st_mode) && sb.st_uid == geteuid() &&
             sb.st_nlink == 1 &&
             write(fd, msg, sizeof msg - 1) == (ssize_t)(sizeof msg - 1);
    if (ok && fsync(fd) != 0) ok = 0;
    if (close(fd) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int parse_u32(const char *s, uint32_t *out) {
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || s[0] == '-' || v == 0 || v > 3600000ul) return -1;
    *out = (uint32_t)v;
    return 0;
}

int main(int argc, char **argv) {
    const char *env_j = getenv("QW_JOURNAL_PATH");
    if (env_j != NULL && env_j[0] != '\0') g_journal = env_j;
    uint32_t watchdog_ms = QW_DECISION_DEADLINE_MS, escalate_ms = QW_REVIEW_ESCALATE_MS;
    for (int i = 1; i < argc; i++) {
        int bad = 1;
        if (i + 1 < argc && strcmp(argv[i], "--watchdog-ms") == 0)
            bad = parse_u32(argv[++i], &watchdog_ms);
        else if (i + 1 < argc && strcmp(argv[i], "--escalate-ms") == 0)
            bad = parse_u32(argv[++i], &escalate_ms);
        else if (i + 1 < argc && strcmp(argv[i], "--journal") == 0) {
            g_journal = argv[++i];
            bad = 0;
        }
        if (bad) {
            fprintf(stderr, "usage: %s [--watchdog-ms N] [--escalate-ms N] [--journal PATH]\n", argv[0]);
            return 2;
        }
    }
    qw_log_init("qw_alarm_mgr");

    if (mkdir(QW_STATE_DIR, 0700) != 0 && errno != EEXIST)
        qw_log_emit(QW_LOG_ERROR, 0, 0, NULL, NULL, "cannot create state dir");

    qw_gw_init(&g_st, watchdog_ms, escalate_ms);
    int replayed = qw_gw_journal_replay(g_journal, &g_st);
    if (replayed != 0) {
        qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, "CONFIRM", "journal replay latched unresolved alarms");
    }
    sync_and_flush_journal();

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term; /* no SA_RESTART: MsgReceive returns EINTR */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    static qw_resmgr_ctx_t ctx;
    ctx.st = &g_st;
    ctx.mu = &g_mu;
    ctx.journal_path = g_journal;
    ctx.now_ms_fn = now_ms;
    ctx.ack_journal_fn = journal_ack_ordered;
    pthread_t rt, jt;
    if (pthread_create(&rt, NULL, resmgr_thread, &ctx) != 0)
        qw_log_emit(QW_LOG_ERROR, 0, 0, NULL, NULL, "cannot start resource manager thread");
    else
        pthread_detach(rt);
    int have_jt = pthread_create(&jt, NULL, journal_thread, NULL) == 0;
    if (!have_jt)
        qw_log_emit(QW_LOG_ERROR, 0, 0, NULL, NULL, "cannot start journal thread; synchronous fallback");

    name_attach_t *att = name_attach(NULL, QW_NAME_ALARM_MGR, 0);
    if (att == NULL) {
        qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, NULL, "name_attach qw_alarm_mgr failed");
        return 1;
    }

    int tick_coid = ConnectAttach(0, 0, att->chid, _NTO_SIDE_CHANNEL, 0);
    timer_t tid;
    int have_timer = 0;
    if (tick_coid >= 0) {
        struct sigevent ev;
        SIGEV_PULSE_INIT(&ev, tick_coid, SIGEV_PULSE_PRIO_INHERIT, QW_PULSE_WATCHDOG_TICK, 0);
        struct itimerspec its;
        its.it_value.tv_sec = its.it_interval.tv_sec = 0;
        its.it_value.tv_nsec = its.it_interval.tv_nsec = (long)WATCHDOG_TICK_MS * 1000000L;
        have_timer = timer_create(CLOCK_MONOTONIC, &ev, &tid) == 0 &&
                     timer_settime(tid, 0, &its, NULL) == 0;
    }
    if (!have_timer) {
        qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, "CONFIRM", "watchdog timer unavailable; failing open and exiting");
        if (write_alarm_out_failopen() != 0)
            qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, "CONFIRM", "alarm_out fail-open write FAILED");
        if (tick_coid >= 0) ConnectDetach(tick_coid);
        name_detach(att, 0);
        return 1;
    }

    while (!g_stop) {
        union {
            struct _pulse pulse;
            uint16_t type;
            qw_msg_decision_t decision;
            char raw[256];
        } m;
        m.type = 0;
        struct _msg_info info;
        rcvid_t rcvid = MsgReceive(att->chid, &m, sizeof m, &info);
        if (rcvid == -1) {
            if (errno == EINTR) continue;
            break;
        }

        if (rcvid > 0) {
            if (info.msglen >= 0 && (size_t)info.msglen == sizeof(qw_msg_decision_t)) {
                qw_msg_reply_t reply = {QW_IPC_VERSION, (int32_t)QW_ST_BAD_VERSION, 0};
                if (m.decision.version == QW_IPC_VERSION) {
                    pthread_mutex_lock(&g_mu);
                    qw_gw_apply_decision(&g_st, &m.decision, now_ms(), &reply);
                    pthread_mutex_unlock(&g_mu);
                    if (have_jt)
                        wake_journal_thread();
                    else
                        sync_and_flush_journal();
                } else {
                    qw_log_emit(QW_LOG_ERROR, 0, 0, NULL, NULL, "decision with bad version rejected");
                }
                MsgReply(rcvid, EOK, &reply, sizeof reply);
            } else if (info.msglen >= (ssize_t)sizeof m.type && m.type == _IO_CONNECT) { /* name_open() handshake */
                MsgReply(rcvid, EOK, NULL, 0);
            } else {
                MsgError(rcvid, EBADMSG);
            }
            continue;
        }

        if (m.pulse.code == _PULSE_CODE_DISCONNECT) {
            ConnectDetach(m.pulse.scoid);
            continue;
        }
        if (m.pulse.code == _PULSE_CODE_UNBLOCK) {
            /* QNX 8.0 x86_64: rcvid_t is 64-bit; _pulse.value.sival_ptr carries the full client rcvid. */
            _Static_assert(sizeof(m.pulse.value.sival_ptr) >= sizeof(rcvid_t),
                           "sival_ptr must hold QNX 8.0 rcvid_t");
            MsgError((rcvid_t)(intptr_t)m.pulse.value.sival_ptr, EINTR);
            continue;
        }

        if (m.pulse.code == QW_PULSE_ALARM_RAISED) {
            uint32_t bed = QW_PULSE_BED(m.pulse.value.sival_int);
            uint32_t aid = QW_PULSE_AID(m.pulse.value.sival_int);
            uint32_t seq = QW_PULSE_SEQ(m.pulse.value.sival_int);
            if (bed >= QW_MAX_BEDS || aid >= (uint32_t)QW_ALARM_COUNT) {
                qw_log_emit(QW_LOG_ERROR, bed, seq, NULL, NULL, "malformed alarm pulse ignored");
                continue;
            }
            /* qw_acq durably appended 'R' via O_APPEND write() prior to this pulse. */
            int failopen = 0;
            pthread_mutex_lock(&g_mu);
            qw_gw_raise_alarm(&g_st, bed, (qw_alarm_id_t)aid, seq, now_ms(), &failopen);
            pthread_mutex_unlock(&g_mu);
            if (have_jt)
                wake_journal_thread();
            else
                sync_and_flush_journal();
        } else if (m.pulse.code == QW_PULSE_WATCHDOG_TICK) {
            pthread_mutex_lock(&g_mu);
            uint64_t t = now_ms();
            qw_gw_check_deadlines(&g_st, t);
            qw_gw_check_escalations(&g_st, t);
            pthread_mutex_unlock(&g_mu);
            if (have_jt)
                wake_journal_thread();
            else
                sync_and_flush_journal();
        }
    }

    if (have_jt) {
        pthread_mutex_lock(&g_jnotify_mu);
        g_jnotify_stop = 1;
        pthread_cond_broadcast(&g_journal_cv);
        pthread_mutex_unlock(&g_jnotify_mu);
        pthread_join(jt, NULL);
    } else {
        sync_and_flush_journal();
    }
    if (have_timer) timer_delete(tid);
    if (tick_coid >= 0) ConnectDetach(tick_coid);
    name_detach(att, 0);
    return 0;
}

#else /* !__QNX__ */

#include <stdio.h>

int main(void) {
    fputs("qw_alarm_mgr: QNX Neutrino only; build with make qnx\n", stderr);
    return 1;
}

#endif
