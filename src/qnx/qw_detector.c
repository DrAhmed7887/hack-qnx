/* qw_detector.c -- detector process (QNX 8.0).
 *
 *   qw_detector <rules.json> <detector.json>
 *
 * Fails closed at startup if either config is invalid (a detector running
 * on guessed thresholds must not exist). Woken by QW_PULSE_ALARM_RAISED, it
 * maps that bed's shared-memory ring read-only, copies the newest window,
 * runs the unchanged qw_decide(), and sends the decision to qw_alarm_mgr
 * with a send/reply timeout. Any problem on the way (ring busy, detached or
 * malformed, qw_decide failure, window too large) answers CONFIRM: the alarm
 * is sounded, never suppressed, on a fault.
 *
 * The pulse carries only the low 24 bits of seq; qw_acq refuses larger
 * values, so the seq echoed here is the full seq.
 */
#ifdef __QNX__

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "qw_core.h"
#include "qw_ipc.h"
#include "qw_log.h"
#include "qw_ring.h"

#define DET_MAX_TAIL 16384u            /* samples per channel we will analyse */
#define DET_SEND_TIMEOUT_NS 250000000ULL /* 250 ms */
#define DET_RING_RETRIES 16u

static qw_rules_t g_rules;
static qw_detector_config_t g_cfg;
static qw_decide_workspace_t g_ws;                       /* caller-owned scratch for qw_decide */
static float g_samples[QW_MAX_CHANNELS * DET_MAX_TAIL];  /* preallocated record storage */
static volatile sig_atomic_t g_stop;

static void on_term(int sig) {
    (void)sig;
    g_stop = 1;
}

static qw_channel_kind_t classify(const char *name) {
    static const char *const ecg[] = {"II", "I", "III", "MCL", "MCL1", "aVR", "aVF", "aVL",
                                      "V", "V1", "V2", "V3", "V4", "V5", "V6", NULL};
    static const char *const pulse[] = {"PLETH", "ABP", NULL};
    for (int i = 0; ecg[i] != NULL; i++)
        if (strcmp(name, ecg[i]) == 0) return QW_CHANNEL_ECG;
    for (int i = 0; pulse[i] != NULL; i++)
        if (strcmp(name, pulse[i]) == 0) return QW_CHANNEL_PULSE;
    return QW_CHANNEL_OTHER;
}

/* Decide for one raised alarm. Returns the decision; *reason says why. */
static qw_decision_t decide_alarm(uint32_t bed, uint32_t aid, const char *alarm, char *reason,
                                  size_t reason_cap) {
    char shm_name[32];
    snprintf(shm_name, sizeof shm_name, QW_RING_SHM_FMT, bed);
    int fd = shm_open(shm_name, O_RDONLY, 0);
    if (fd < 0) {
        snprintf(reason, reason_cap, "fail-open: ring %s not found", shm_name);
        return QW_DECISION_CONFIRM;
    }
    struct stat sb;
    void *mem = MAP_FAILED;
    size_t mem_bytes = 0;
    qw_decision_t out = QW_DECISION_CONFIRM;
    if (fstat(fd, &sb) != 0 || sb.st_size < (off_t)sizeof(qw_ring_hdr_t)) {
        snprintf(reason, reason_cap, "fail-open: ring too small");
        goto out;
    }
    mem_bytes = (size_t)sb.st_size;
    mem = mmap(NULL, mem_bytes, PROT_READ, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        snprintf(reason, reason_cap, "fail-open: mmap failed");
        goto out;
    }

    const qw_ring_hdr_t *h = (const qw_ring_hdr_t *)mem;
    if (atomic_load(&h->magic) != QW_RING_MAGIC) {
        snprintf(reason, reason_cap, "fail-open: ring detached");
        goto out;
    }
    uint32_t fs = atomic_load(&h->fs);
    uint32_t ring_len = atomic_load(&h->ring_len);
    if (fs == 0 || ring_len == 0 || !g_rules.suppression.has_window_s) {
        snprintf(reason, reason_cap, "fail-open: bad ring header");
        goto out;
    }
    double want = ceil(g_rules.suppression.window_s * (double)fs) + 1.0;
    if (want > (double)DET_MAX_TAIL || want < 1.0) {
        snprintf(reason, reason_cap, "fail-open: window exceeds %u samples", DET_MAX_TAIL);
        goto out;
    }
    uint32_t tail = (uint32_t)want < ring_len ? (uint32_t)want : ring_len;

    qw_record_t rec;
    memset(&rec, 0, sizeof rec);
    char names[QW_MAX_CHANNELS][QW_CHANNEL_NAME_LEN];
    uint32_t rfs = 0, nch = 0, n = 0;
    qw_status_t rs = qw_ring_read_tail(mem, mem_bytes, tail, g_samples, &rfs, &nch, &n, names,
                                       DET_RING_RETRIES);
    if (rs != QW_ST_OK || n == 0 || nch == 0 || nch > QW_MAX_CHANNELS) {
        snprintf(reason, reason_cap, "fail-open: ring read %s",
                 rs == QW_ST_BUSY ? "busy" : (rs == QW_ST_DETACHED ? "detached" : "empty or failed"));
        goto out;
    }
    rec.fs = rfs;
    rec.n_channels = nch;
    rec.n_samples = n;
    rec.sample_buf = g_samples; /* borrowed static storage; never freed */
    for (uint32_t c = 0; c < nch; c++) {
        memcpy(rec.channels[c].name, names[c], QW_CHANNEL_NAME_LEN);
        rec.channels[c].name[QW_CHANNEL_NAME_LEN] = '\0';
        rec.channels[c].kind = classify(rec.channels[c].name);
        rec.channels[c].samples = g_samples + (size_t)c * n;
    }

    qw_decision_result_t res;
    memset(&res, 0, sizeof res);
    if (qw_decide(&rec, alarm, &g_rules, &g_cfg, &g_ws, &res) != 0) {
        snprintf(reason, reason_cap, "fail-open: qw_decide failed");
        goto out;
    }
    out = res.decision;
    snprintf(reason, reason_cap, "%s", res.reason);
    (void)aid;

out:
    if (mem != MAP_FAILED) munmap(mem, mem_bytes);
    close(fd);
    return out;
}

static void send_decision(int *coid, uint32_t bed, uint32_t aid, uint32_t seq, qw_decision_t d) {
    const char *alarm = qw_alarm_name((qw_alarm_id_t)aid);
    for (int attempt = 0; attempt < 6; attempt++) {
        if (*coid < 0) *coid = name_open(QW_NAME_ALARM_MGR, 0);
        if (*coid < 0) {
            qw_log_emit(QW_LOG_ERROR, bed, seq, alarm, NULL,
                        "qw_alarm_mgr unreachable; its deadline will latch CONFIRM");
            return;
        }
        qw_msg_decision_t msg = {QW_IPC_VERSION, bed, aid, seq, (uint32_t)d};
        qw_msg_reply_t reply;
        uint64_t timeout = DET_SEND_TIMEOUT_NS;
        if (TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, NULL, &timeout, NULL) == -1) {
            qw_log_emit(QW_LOG_ERROR, bed, seq, alarm, NULL,
                        "TimerTimeout setup failed; skipping send so alarm manager fails open");
            return;
        }
        if (MsgSend(*coid, &msg, sizeof msg, &reply, sizeof reply) == -1) {
            qw_log_emit(QW_LOG_ERROR, bed, seq, alarm, NULL,
                        "decision send failed; alarm manager deadline will latch CONFIRM");
            name_close(*coid);
            *coid = -1;
            return;
        }
        if (reply.version != QW_IPC_VERSION) {
            qw_log_emit(QW_LOG_ERROR, bed, seq, alarm, NULL,
                        "decision reply version mismatch; alarm manager will fail open");
            return;
        }
        if (reply.status == (int32_t)QW_ST_OK) return;
        if (reply.status == (int32_t)QW_ST_NOT_FOUND && attempt < 5) {
            /* Alarm pulse may still be queued ahead in qw_alarm_mgr; brief 10 ms backoff. */
            struct timespec ts = {0, 10000000L};
            nanosleep(&ts, NULL);
            continue;
        }
        char note[128];
        snprintf(note, sizeof note, "decision reply status %d; alarm manager will fail open",
                 (int)reply.status);
        qw_log_emit(QW_LOG_ERROR, bed, seq, alarm, NULL, note);
        return;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <rules.json> <detector.json>\n", argv[0]);
        return 2;
    }
    qw_log_init("qw_detector");
    if (qw_rules_load(argv[1], &g_rules) != 0 || qw_detector_config_load(argv[2], &g_cfg) != 0) {
        qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, NULL, "invalid rules/detector config; refusing to start");
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term; /* no SA_RESTART: MsgReceive returns EINTR */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    name_attach_t *att = name_attach(NULL, QW_NAME_DETECTOR, 0);
    if (att == NULL) {
        qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, NULL, "name_attach qw_detector failed");
        return 1;
    }
    int mgr = name_open(QW_NAME_ALARM_MGR, 0); /* may not be up yet; retried per decision */

    while (!g_stop) {
        union {
            struct _pulse pulse;
            uint16_t type;
            char raw[64];
        } m;
        m.type = 0;
        struct _msg_info info;
        rcvid_t rcvid = MsgReceive(att->chid, &m, sizeof m, &info);
        if (rcvid == -1) {
            if (errno == EINTR) continue;
            break;
        }
        if (rcvid > 0) { /* name_open() handshake or stray message */
            if (info.msglen >= (ssize_t)sizeof m.type && m.type == _IO_CONNECT)
                MsgReply(rcvid, EOK, NULL, 0);
            else
                MsgError(rcvid, ENOSYS);
            continue;
        }
        if (m.pulse.code == _PULSE_CODE_DISCONNECT) {
            ConnectDetach(m.pulse.scoid);
            continue;
        }
        if (m.pulse.code == _PULSE_CODE_UNBLOCK) {
            MsgError((rcvid_t)(intptr_t)m.pulse.value.sival_ptr, EINTR);
            continue;
        }
        if (m.pulse.code != QW_PULSE_ALARM_RAISED) continue;

        uint32_t bed = QW_PULSE_BED(m.pulse.value.sival_int);
        uint32_t aid = QW_PULSE_AID(m.pulse.value.sival_int);
        uint32_t seq = QW_PULSE_SEQ(m.pulse.value.sival_int);
        const char *alarm = qw_alarm_name((qw_alarm_id_t)aid);
        if (bed >= QW_MAX_BEDS || alarm == NULL) {
            qw_log_emit(QW_LOG_ERROR, bed, seq, NULL, NULL, "malformed alarm pulse ignored");
            continue;
        }

        char reason[QW_REASON_LEN];
        qw_decision_t d = decide_alarm(bed, aid, alarm, reason, sizeof reason);
        send_decision(&mgr, bed, aid, seq, d);
        qw_log_emit(d == QW_DECISION_CONFIRM ? QW_LOG_INFO : QW_LOG_WARN, bed, seq, alarm,
                    d == QW_DECISION_CONFIRM ? "CONFIRM" : "SUPPRESS_TO_REVIEW", reason);
    }

    if (mgr >= 0) name_close(mgr);
    name_detach(att, 0);
    return 0;
}

#else /* !__QNX__ */

#include <stdio.h>

int main(void) {
    fputs("qw_detector: QNX Neutrino only; build with make qnx\n", stderr);
    return 1;
}

#endif
