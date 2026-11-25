/* qw_acq.c -- acquisition process (QNX 8.0).
 *
 * Replays one recorded .qw file into the per-bed shared-memory ring at the
 * recording's pace (timer pulse -> MsgReceivePulse), then raises the alarm:
 * journal 'R', then a non-blocking pulse to qw_detector and qw_alarm_mgr.
 * It never waits on the detector, so analysis cannot stall acquisition.
 *
 *   qw_acq <qw_file> <alarm_name> [--bed N] [--seq N] [--rate-ms N] [--speed N]
 *
 * The PhysioNet 2015 convention used by qw_decide() puts the alarm 300 s
 * into the record, so only samples before that point are streamed, and only
 * the last ring_len of them (older samples would fall out of the ring).
 * seq must fit the pulse's 24 bits; larger values are refused.
 */
#ifdef __QNX__

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "qw_core.h"
#include "qw_gateway_state.h"
#include "qw_log.h"
#include "qw_resmgr.h"
#include "qw_ring.h"

#define ACQ_ALARM_OFFSET_S 300u   /* PhysioNet 2015: alarm fires 300 s into the record */
#define ACQ_RING_SAMPLES 16384u   /* samples per channel kept in the ring */
#define ACQ_DEFAULT_RATE_MS 100u
#define ACQ_HOLD_MS 1500u         /* keep the ring alive so the detector can read it */

static int parse_u32(const char *s, uint32_t max, uint32_t *out) {
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || s[0] == '-' || v > max) return -1;
    *out = (uint32_t)v;
    return 0;
}

static int notify(const char *name, int code, int value) {
    int coid = name_open(name, 0);
    if (coid < 0) {
        qw_log_emit(QW_LOG_WARN, 0, 0, NULL, NULL, "name_open failed; peer not running");
        return -1;
    }
    int rc = MsgSendPulse(coid, -1, code, value); /* -1 = inherit sender priority; pulses never block */
    name_close(coid);
    if (rc != 0) {
        qw_log_emit(QW_LOG_ERROR, 0, 0, NULL, NULL, "MsgSendPulse failed");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <qw_file> <alarm_name> [--bed N] [--seq N] [--rate-ms N] [--speed N]\n", argv[0]);
        return 2;
    }
    const char *qw_file = argv[1];
    int aid = qw_alarm_id_from_name(argv[2]);
    uint32_t bed = 0, seq = 1, rate_ms = ACQ_DEFAULT_RATE_MS, speed = 1;
    if (aid < 0) {
        fprintf(stderr, "%s: unknown alarm '%s'\n", argv[0], argv[2]);
        return 2;
    }
    for (int i = 3; i < argc; i++) {
        int bad = 1;
        if (i + 1 < argc && strcmp(argv[i], "--bed") == 0)
            bad = parse_u32(argv[++i], QW_MAX_BEDS - 1, &bed);
        else if (i + 1 < argc && strcmp(argv[i], "--seq") == 0)
            bad = parse_u32(argv[++i], 0xFFFFFFu, &seq);
        else if (i + 1 < argc && strcmp(argv[i], "--rate-ms") == 0)
            bad = parse_u32(argv[++i], 60000u, &rate_ms) || rate_ms == 0;
        else if (i + 1 < argc && strcmp(argv[i], "--speed") == 0)
            bad = parse_u32(argv[++i], 1000u, &speed) || speed == 0;
        if (bad) {
            fprintf(stderr, "%s: bad option near '%s'\n", argv[0], argv[i]);
            return 2;
        }
    }

    qw_log_init("qw_acq");
    qw_record_t rec;
    if (qw_record_load(qw_file, &rec) != 0) {
        qw_record_free(&rec);
        return 1;
    }

    int rc = 1;
    int shm_fd = -1, chid = -1, coid = -1, own_shm = 0;
    timer_t tid = 0;
    int have_timer = 0;
    void *mem = NULL;
    float *frame = NULL;
    char shm_name[32];
    snprintf(shm_name, sizeof shm_name, QW_RING_SHM_FMT, bed);

    uint64_t off_samples = (uint64_t)ACQ_ALARM_OFFSET_S * rec.fs;
    uint32_t alarm_sample = off_samples > (uint64_t)rec.n_samples ? rec.n_samples : (uint32_t)off_samples;
    uint32_t ring_len = ACQ_RING_SAMPLES;
    uint32_t pos = alarm_sample > ring_len ? alarm_sample - ring_len : 0;
    uint32_t chunk = (uint32_t)(((uint64_t)rec.fs * rate_ms * speed) / 1000u);
    if (chunk == 0) chunk = 1;
    if (chunk > ring_len) chunk = ring_len;

    size_t need = qw_ring_bytes_needed(rec.n_channels, ring_len);
    if (need == 0) goto done;
    shm_fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (shm_fd < 0) {
        qw_log_emit(QW_LOG_ERROR, bed, seq, argv[2], NULL,
                    errno == EEXIST ? "bed ring already exists; refusing to hijack active/stale shm"
                                    : "shm_open exclusive create failed");
        goto done;
    }
    own_shm = 1;
    if (ftruncate(shm_fd, (off_t)need) != 0) {
        qw_log_emit(QW_LOG_ERROR, bed, seq, argv[2], NULL, "ftruncate failed");
        goto done;
    }
    mem = mmap(NULL, need, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (mem == MAP_FAILED) {
        mem = NULL;
        goto done;
    }
    memset(mem, 0, need);
    char names[QW_MAX_CHANNELS][QW_CHANNEL_NAME_LEN];
    memset(names, 0, sizeof names);
    for (uint32_t c = 0; c < rec.n_channels; c++) {
        size_t n = strlen(rec.channels[c].name);
        if (n >= QW_CHANNEL_NAME_LEN) n = QW_CHANNEL_NAME_LEN - 1;
        memcpy(names[c], rec.channels[c].name, n);
        names[c][n] = '\0';
    }
    if (qw_ring_init(mem, need, rec.fs, rec.n_channels, ring_len, names) != QW_ST_OK) goto done;

    frame = malloc((size_t)chunk * rec.n_channels * sizeof(float));
    chid = ChannelCreate(0);
    coid = chid >= 0 ? ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0) : -1;
    if (frame == NULL || coid < 0) goto done;

    struct sigevent ev;
    SIGEV_PULSE_INIT(&ev, coid, SIGEV_PULSE_PRIO_INHERIT, QW_PULSE_SAMPLE_TICK, 0);
    if (timer_create(CLOCK_MONOTONIC, &ev, &tid) != 0) goto done;
    have_timer = 1;
    struct itimerspec its;
    its.it_value.tv_sec = its.it_interval.tv_sec = rate_ms / 1000u;
    its.it_value.tv_nsec = its.it_interval.tv_nsec = (long)(rate_ms % 1000u) * 1000000L;
    if (timer_settime(tid, 0, &its, NULL) != 0) goto done;

    while (pos < alarm_sample) {
        struct _pulse pulse;
        /* MsgReceivePulse returns int (0 on pulse, -1 on error), unlike MsgReceive which returns rcvid_t. */
        int rcvid = MsgReceivePulse(chid, &pulse, sizeof pulse, NULL);
        if (rcvid < 0) {
            if (errno == EINTR) continue;
            goto done;
        }
        if (pulse.code != QW_PULSE_SAMPLE_TICK) continue;
        uint32_t n = alarm_sample - pos < chunk ? alarm_sample - pos : chunk;
        for (uint32_t c = 0; c < rec.n_channels; c++)
            memcpy(frame + (size_t)c * n, rec.channels[c].samples + pos, (size_t)n * sizeof(float));
        if (qw_ring_write_frame(mem, frame, n, 1) != QW_ST_OK) goto done;
        pos += n;
    }
    timer_delete(tid);
    have_timer = 0;

    /* Alarm: durable first, then wake alarm_mgr BEFORE detector so the alarm
     * is registered before a fast decision arrives. */
    const char *jpath = getenv("QW_JOURNAL_PATH");
    if (jpath == NULL || jpath[0] == '\0') jpath = QW_JOURNAL_PATH;
    int jrc = qw_gw_journal_append(jpath, 'R', seq, bed, (uint32_t)aid, "acq raise");
    int pv = QW_PULSE_VAL(bed, aid, seq);
    int mrc = notify(QW_NAME_ALARM_MGR, QW_PULSE_ALARM_RAISED, pv);
    if (jrc != 0 || mrc != 0) {
        qw_log_emit(QW_LOG_CRIT, bed, seq, argv[2], "CONFIRM",
                    "alarm admission or durable journal failed; skipping detector suppression path");
        goto done;
    }
    (void)notify(QW_NAME_DETECTOR, QW_PULSE_ALARM_RAISED, pv);
    qw_log_emit(QW_LOG_INFO, bed, seq, argv[2], "RAISED", "alarm raised at end of replay");

    struct timespec hold = {ACQ_HOLD_MS / 1000u, (long)(ACQ_HOLD_MS % 1000u) * 1000000L};
    nanosleep(&hold, NULL);
    rc = 0;

done:
    if (have_timer) timer_delete(tid);
    if (coid >= 0) ConnectDetach(coid);
    if (chid >= 0) ChannelDestroy(chid);
    if (mem != NULL) {
        if (own_shm) qw_ring_detach(mem);
        munmap(mem, need);
    }
    if (shm_fd >= 0) {
        close(shm_fd);
        if (own_shm) shm_unlink(shm_name);
    }
    free(frame);
    qw_record_free(&rec);
    return rc;
}

#else /* !__QNX__ */

#include <stdio.h>

int main(void) {
    fputs("qw_acq: QNX Neutrino only; build with make qnx\n", stderr);
    return 1;
}

#endif
