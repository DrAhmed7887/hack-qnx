/* qw_gateway_test.c -- host tests for the gateway subsystems (ring, alarm
 * manager state machine, ACK parser, journal replay). Exit 0 = all pass. */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qw_gateway_state.h"
#include "qw_ring.h"

static int g_fail = 0;
static int g_checks = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_fail++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define MAX_TMP 16
static char g_journal[64];
static char g_tmp[MAX_TMP][96];
static int g_ntmp = 0;
static void cleanup_journal(void) {
    if (g_journal[0]) unlink(g_journal);
    for (int i = 0; i < g_ntmp; i++) unlink(g_tmp[i]); /* files and symlinks */
}
static const char *tmp_path(const char *tag) {
    if (g_ntmp >= MAX_TMP) {
        fprintf(stderr, "too many temp paths\n");
        exit(2);
    }
    snprintf(g_tmp[g_ntmp], sizeof g_tmp[0], "/tmp/qw_gw_%s_%ld", tag, (long)getpid());
    unlink(g_tmp[g_ntmp]);
    return g_tmp[g_ntmp++];
}
static void write_file(const char *path, const char *data, size_t n) {
    FILE *f = fopen(path, "w");
    if (f == NULL || fwrite(data, 1, n, f) != n || fclose(f) != 0) {
        fprintf(stderr, "write_file failed: %s\n", path);
        exit(2);
    }
}

/* ---- pulses ---- */

static int pulse_bad(uint32_t b, uint32_t a, uint32_t s) {
    int v = QW_PULSE_VAL(b, a, s);
    return v < 0 || QW_PULSE_BED(v) != b || QW_PULSE_AID(v) != a || QW_PULSE_SEQ(v) != (s & 0xFFFFFFu);
}

static void test_pulse(void) {
    int bad = 0;
    static const uint32_t edge[] = {0u,        1u,        0xFFu,     0x100u,    0xFFFFu,
                                    0x10000u,  0x7FFFFFu, 0x800000u, 0xFFFFFEu, 0xFFFFFFu};
    for (uint32_t b = 0; b < QW_MAX_BEDS; b++)
        for (uint32_t a = 0; a < (uint32_t)QW_ALARM_COUNT; a++) {
            for (uint32_t s = 0; s <= 0xFFFFu; s++) bad += pulse_bad(b, a, s);          /* low */
            for (uint32_t s = 0xFF0000u; s <= 0xFFFFFFu; s++) bad += pulse_bad(b, a, s); /* high */
            for (size_t i = 0; i < sizeof edge / sizeof edge[0]; i++) bad += pulse_bad(b, a, edge[i]);
        }
    CHECK(bad == 0);
    /* only the low 24 bits of a wider seq survive in the pulse */
    int v = QW_PULSE_VAL(QW_MAX_BEDS - 1, QW_ALARM_COUNT - 1, 0x01000005u);
    CHECK(v >= 0 && QW_PULSE_SEQ(v) == 5u && QW_PULSE_BED(v) == QW_MAX_BEDS - 1 &&
          QW_PULSE_AID(v) == (uint32_t)QW_ALARM_COUNT - 1);
    /* the largest representable value is still non-negative */
    CHECK(QW_PULSE_VAL(7, 7, 0xFFFFFFu) > 0);
    CHECK(qw_alarm_id_from_name(qw_alarm_name(QW_ALARM_VT)) == (int)QW_ALARM_VT);
    CHECK(qw_alarm_id_from_name("nope") == -1);
    CHECK(qw_alarm_name(QW_ALARM_COUNT) == NULL);
}

/* ---- ring ---- */

#define NCH 4
#define RLEN 64

static const char CH_NAMES[NCH][QW_CHANNEL_NAME_LEN] = {"II", "V", "PLETH", "ABP"};

#define RING_BYTES(len) qw_ring_bytes_needed(NCH, (len))

static void *make_ring(uint32_t ring_len) {
    size_t need = qw_ring_bytes_needed(NCH, ring_len);
    size_t alloc_bytes = (need + 15u) & ~(size_t)15u;
    void *mem = aligned_alloc(16, alloc_bytes);
    if (mem != NULL) memset(mem, 0, alloc_bytes);
    if (mem == NULL || qw_ring_init(mem, need, 250, NCH, ring_len, CH_NAMES) != QW_ST_OK) {
        fprintf(stderr, "ring setup failed\n");
        exit(2);
    }
    return mem;
}

static void test_ring_basic(void) {
    void *mem = make_ring(RLEN);
    float out[QW_MAX_CHANNELS * 8];
    uint32_t fs = 0, nch = 0, n = 0;
    char names[QW_MAX_CHANNELS][QW_CHANNEL_NAME_LEN];

    CHECK(qw_ring_bytes_needed(0, 10) == 0 && qw_ring_bytes_needed(QW_MAX_CHANNELS + 1, 10) == 0);
    CHECK(qw_ring_read_tail(mem, RING_BYTES(RLEN), 8, out, &fs, &nch, &n, names, 4) == QW_ST_OK && n == 0);

    float frame[NCH * 3]; /* channel-major, 3 samples per channel */
    for (int c = 0; c < NCH; c++)
        for (int i = 0; i < 3; i++) frame[c * 3 + i] = (float)(c * 10 + i);
    CHECK(qw_ring_write_frame(mem, frame, 3, 1) == QW_ST_OK);
    CHECK(qw_ring_write_frame(mem, frame, 0, 1) == QW_ST_BAD_ARGS);
    CHECK(qw_ring_write_frame(mem, frame, RLEN + 1, 1) == QW_ST_BAD_ARGS);
    CHECK(qw_ring_read_tail(mem, RING_BYTES(RLEN), 8, out, &fs, &nch, &n, names, 4) == QW_ST_OK);
    CHECK(fs == 250 && nch == NCH && n == 3);
    CHECK(out[0] == 0.f && out[2] == 2.f && out[3 * 3 + 1] == 31.f);
    CHECK(strcmp(names[2], "PLETH") == 0);

    float inter[NCH * 2]; /* interleaved, 2 samples */
    for (int i = 0; i < 2; i++)
        for (int c = 0; c < NCH; c++) inter[i * NCH + c] = (float)(100 + c * 10 + i);
    CHECK(qw_ring_write_frame(mem, inter, 2, 0) == QW_ST_OK);
    CHECK(qw_ring_read_tail(mem, RING_BYTES(RLEN), 2, out, &fs, &nch, &n, NULL, 4) == QW_ST_OK && n == 2);
    CHECK(out[0] == 100.f && out[1] == 101.f && out[2] == 110.f && out[4] == 120.f);

    /* wrap: push far more than RLEN, tail stays chronological */
    for (int k = 0; k < 200; k++) {
        float one[NCH];
        for (int c = 0; c < NCH; c++) one[c] = (float)(k * 100 + c);
        CHECK(qw_ring_write_frame(mem, one, 1, 0) == QW_ST_OK);
    }
    CHECK(qw_ring_read_tail(mem, RING_BYTES(RLEN), 8, out, &fs, &nch, &n, NULL, 4) == QW_ST_OK && n == 8);
    CHECK(out[7] == 199 * 100.f && out[0] == 192 * 100.f);
    free(mem);
}

static void test_ring_reinit(void) {
    void *mem = make_ring(RLEN);
    qw_ring_hdr_t *h = (qw_ring_hdr_t *)mem;
    float one[NCH] = {1, 2, 3, 4};
    CHECK(qw_ring_write_frame(mem, one, 1, 0) == QW_ST_OK);
    uint32_t before = atomic_load(&h->seq_lock);
    CHECK(before != 0 && (before & 1u) == 0);
    CHECK(qw_ring_init(mem, RING_BYTES(RLEN), 250, NCH, RLEN, CH_NAMES) == QW_ST_OK);
    uint32_t after = atomic_load(&h->seq_lock);
    CHECK(after != 0 && (after & 1u) == 0 && after > before);
    CHECK(atomic_load(&h->magic) == QW_RING_MAGIC && atomic_load(&h->total_written) == 0);
    /* Detach then re-init must also advance seq_lock cleanly (no ABA). */
    qw_ring_detach(mem);
    CHECK(qw_ring_init(mem, RING_BYTES(RLEN), 250, NCH, RLEN, CH_NAMES) == QW_ST_OK);
    uint32_t after_detach = atomic_load(&h->seq_lock);
    CHECK(after_detach != 0 && (after_detach & 1u) == 0 && after_detach > after);
    free(mem);
}

static void test_ring_detached(void) {
    void *mem = make_ring(RLEN);
    qw_ring_hdr_t *h = (qw_ring_hdr_t *)mem;
    size_t need = RING_BYTES(RLEN);
    float out[QW_MAX_CHANNELS * 4], one[NCH] = {1, 2, 3, 4};
    uint32_t n;
    CHECK(qw_ring_write_frame(mem, one, 1, 0) == QW_ST_OK);
    qw_ring_detach(mem);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    CHECK(qw_ring_write_frame(mem, one, 1, 0) == QW_ST_DETACHED);
    free(mem);

    mem = make_ring(RLEN);
    h = (qw_ring_hdr_t *)mem;
    atomic_store(&h->magic, 0xDEADBEEFu);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    atomic_store(&h->magic, QW_RING_MAGIC);
    atomic_store(&h->version, 99u);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    atomic_store(&h->version, (uint32_t)QW_IPC_VERSION);
    /* writer stalled mid-update: seq_lock odd while writer_alive == 1 -> BUSY
     * (never stale data as fresh); the caller must fail open. */
    atomic_store(&h->seq_lock, 1u);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_BUSY);
    /* qw_ring_detach clears writer_alive and magic -> DETACHED, even with seq_lock odd */
    qw_ring_detach(mem);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    free(mem);

    /* undersized mapping: never touch the header or any slot */
    mem = make_ring(RLEN);
    h = (qw_ring_hdr_t *)mem;
    CHECK(qw_ring_read_tail(NULL, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_BAD_ARGS);
    CHECK(qw_ring_read_tail(mem, 0, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    CHECK(qw_ring_read_tail(mem, sizeof(qw_ring_hdr_t) - 1, 4, out, NULL, NULL, &n, NULL, 4) ==
          QW_ST_DETACHED);
    CHECK(qw_ring_read_tail(mem, sizeof(qw_ring_hdr_t), 4, out, NULL, NULL, &n, NULL, 4) ==
          QW_ST_DETACHED); /* header fits, claimed slots do not */
    CHECK(qw_ring_read_tail(mem, need - 1, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_OK);

    /* corrupt header geometry */
    atomic_store(&h->ring_len, 1u << 30);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    atomic_store(&h->ring_len, 0xFFFFFFFFu);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    atomic_store(&h->ring_len, 0u);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    atomic_store(&h->ring_len, (uint32_t)RLEN);
    atomic_store(&h->n_channels, (uint32_t)QW_MAX_CHANNELS + 1u);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_DETACHED);
    atomic_store(&h->n_channels, (uint32_t)NCH);
    CHECK(qw_ring_read_tail(mem, need, 4, out, NULL, NULL, &n, NULL, 4) == QW_ST_OK);
    free(mem);
}

#define STRESS_FRAMES 10000
#define N_READERS 4
#define READ_TAIL 8

typedef struct {
    void *mem;
    atomic_int *go, *writer_done;
    unsigned long ok, busy, torn;
} reader_arg_t;

static void *reader_main(void *p) {
    reader_arg_t *a = (reader_arg_t *)p;
    float out[QW_MAX_CHANNELS * READ_TAIL];
    while (!atomic_load(a->go)) sched_yield();
    for (;;) {
        int done = atomic_load(a->writer_done);
        uint32_t nch = 0, n = 0;
        qw_status_t s = qw_ring_read_tail(a->mem, RING_BYTES(RLEN), READ_TAIL, out, NULL, &nch, &n, NULL, 16);
        if (s == QW_ST_BUSY) {
            a->busy++;
        } else if (s != QW_ST_OK) {
            a->torn++; /* DETACHED is unexpected while the writer is alive */
        } else {
            a->ok++;
            long prev = -1;
            for (uint32_t i = 0; i < n; i++) {
                long frame = -1;
                for (uint32_t c = 0; c < nch; c++) {
                    long v = (long)out[c * n + i];
                    long f = (v - (long)c) / 100;
                    if (v != f * 100 + (long)c || (c > 0 && f != frame)) a->torn++;
                    frame = f;
                }
                if (prev >= 0 && frame != prev + 1) a->torn++; /* not contiguous/chronological */
                prev = frame;
            }
        }
        if (done) break;
    }
    return NULL;
}

typedef struct {
    void *mem;
    atomic_int *go, *writer_done;
} writer_arg_t;

static void *writer_main(void *p) {
    writer_arg_t *a = (writer_arg_t *)p;
    atomic_store(a->go, 1);
    for (int f = 0; f < STRESS_FRAMES; f++) {
        float one[NCH];
        for (int c = 0; c < NCH; c++) one[c] = (float)(f * 100 + c);
        if (qw_ring_write_frame(a->mem, one, 1, 0) != QW_ST_OK) {
            fprintf(stderr, "writer failed at frame %d\n", f);
            exit(2);
        }
        if ((f & 15) == 0) sched_yield();
    }
    atomic_store(a->writer_done, 1);
    return NULL;
}

static void test_ring_stress(void) {
    void *mem = make_ring(RLEN);
    atomic_int go = 0, done = 0;
    pthread_t rt[N_READERS], wt;
    reader_arg_t ra[N_READERS];
    writer_arg_t wa = {mem, &go, &done};
    for (int i = 0; i < N_READERS; i++) {
        ra[i] = (reader_arg_t){mem, &go, &done, 0, 0, 0};
        pthread_create(&rt[i], NULL, reader_main, &ra[i]);
    }
    pthread_create(&wt, NULL, writer_main, &wa);
    pthread_join(wt, NULL);
    unsigned long ok = 0, busy = 0, torn = 0;
    for (int i = 0; i < N_READERS; i++) {
        pthread_join(rt[i], NULL);
        ok += ra[i].ok;
        busy += ra[i].busy;
        torn += ra[i].torn;
    }
    printf("ring stress: %d frames, %d readers, ok=%lu busy=%lu torn=%lu\n", STRESS_FRAMES,
           N_READERS, ok, busy, torn);
    CHECK(torn == 0);
    CHECK(ok > 0);
    free(mem);
}

/* ---- gateway state ---- */

static qw_msg_decision_t mk(uint32_t bed, uint32_t alarm, uint32_t seq, qw_decision_t d) {
    return (qw_msg_decision_t){QW_IPC_VERSION, bed, alarm, seq, (uint32_t)d};
}

static void test_pending_overflow(void) {
    qw_gw_state_t st;
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    int fo = -1;
    for (uint32_t i = 0; i < QW_MAX_PENDING; i++) {
        CHECK(qw_gw_raise_alarm(&st, 0, QW_ALARM_TACHYCARDIA, i, 1000, &fo) == QW_ST_OK && fo == 0);
    }
    CHECK(qw_gw_raise_alarm(&st, 0, QW_ALARM_TACHYCARDIA, 100, 1000, &fo) == QW_ST_BUSY);
    CHECK(fo == 1);
    qw_bed_state_t b;
    CHECK(qw_gw_get_bed_state(&st, 0, &b) == QW_ST_OK);
    CHECK(b.latched_confirm == 1 && b.pending == QW_MAX_PENDING);
    CHECK(st.log[(st.log_total - 1) % QW_LOG_RING].state == QW_LOG_LATCHED);

    /* argument and duplicate checks */
    CHECK(qw_gw_raise_alarm(&st, QW_MAX_BEDS, QW_ALARM_VT, 200, 0, &fo) == QW_ST_BAD_ARGS);
    CHECK(qw_gw_raise_alarm(&st, 0, QW_ALARM_COUNT, 200, 0, &fo) == QW_ST_BAD_ARGS);
    /* a colliding seq (seq 3 is pending on bed 0) still fails open on the
     * bed it claims */
    qw_bed_state_t b1;
    CHECK(qw_gw_get_bed_state(&st, 1, &b1) == QW_ST_OK && b1.latched_confirm == 0);
    fo = 0;
    CHECK(qw_gw_raise_alarm(&st, 1, QW_ALARM_VT, 3, 0, &fo) == QW_ST_DUPLICATE);
    CHECK(fo == 1);
    CHECK(qw_gw_get_bed_state(&st, 1, &b1) == QW_ST_OK && b1.latched_confirm == 1);
    CHECK(strstr(st.log[(st.log_total - 1) % QW_LOG_RING].reason, "duplicate alarm seq") != NULL);
    CHECK(qw_gw_get_bed_state(&st, QW_MAX_BEDS, &b) == QW_ST_BAD_ARGS);
}

static void test_decisions(void) {
    qw_gw_state_t st;
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    qw_msg_reply_t r;
    qw_bed_state_t b;
    int fo;

    CHECK(qw_gw_raise_alarm(&st, 1, QW_ALARM_BRADYCARDIA, 7, 0, &fo) == QW_ST_OK);
    qw_msg_decision_t m = mk(1, QW_ALARM_BRADYCARDIA, 7, QW_DECISION_CONFIRM);
    m.version = 2;
    CHECK(qw_gw_apply_decision(&st, &m, 10, &r) == QW_ST_BAD_VERSION && r.status == QW_ST_BAD_VERSION);
    m = mk(9, QW_ALARM_BRADYCARDIA, 7, QW_DECISION_CONFIRM);
    CHECK(qw_gw_apply_decision(&st, &m, 10, &r) == QW_ST_BAD_ARGS);
    m = mk(1, 99, 7, QW_DECISION_CONFIRM);
    CHECK(qw_gw_apply_decision(&st, &m, 10, &r) == QW_ST_BAD_ARGS);
    m = mk(1, QW_ALARM_BRADYCARDIA, 7, (qw_decision_t)5);
    CHECK(qw_gw_apply_decision(&st, &m, 10, &r) == QW_ST_BAD_ARGS);
    m = mk(1, QW_ALARM_BRADYCARDIA, 8, QW_DECISION_CONFIRM);
    CHECK(qw_gw_apply_decision(&st, &m, 10, &r) == QW_ST_NOT_FOUND);
    CHECK(QW_ST_UNKNOWN_SEQ == QW_ST_NOT_FOUND);

    m = mk(1, QW_ALARM_BRADYCARDIA, 7, QW_DECISION_CONFIRM);
    CHECK(qw_gw_apply_decision(&st, &m, 10, &r) == QW_ST_OK && r.latched == 1);
    qw_gw_get_bed_state(&st, 1, &b);
    CHECK(b.detector_alive == 1 && b.latched_confirm == 1 && b.pending == 0);
    CHECK(qw_gw_apply_decision(&st, &m, 10, &r) == QW_ST_NOT_FOUND); /* replay of a decision */

    /* review queue full -> fail open */
    for (uint32_t i = 0; i < QW_MAX_PENDING; i++) {
        CHECK(qw_gw_raise_alarm(&st, 2, QW_ALARM_ASYSTOLE, 100 + i, 0, &fo) == QW_ST_OK);
        m = mk(2, QW_ALARM_ASYSTOLE, 100 + i, QW_DECISION_SUPPRESS_TO_REVIEW);
        CHECK(qw_gw_apply_decision(&st, &m, 0, &r) == QW_ST_OK && r.latched == 0);
    }
    qw_gw_get_bed_state(&st, 2, &b);
    CHECK(b.in_review == QW_MAX_PENDING && b.latched_confirm == 0);
    CHECK(qw_gw_raise_alarm(&st, 2, QW_ALARM_ASYSTOLE, 500, 0, &fo) == QW_ST_OK);
    m = mk(2, QW_ALARM_ASYSTOLE, 500, QW_DECISION_SUPPRESS_TO_REVIEW);
    CHECK(qw_gw_apply_decision(&st, &m, 0, &r) == QW_ST_BUSY && r.latched == 1);
    qw_gw_get_bed_state(&st, 2, &b);
    CHECK(b.latched_confirm == 1);
}

static void test_deadline(void) {
    qw_gw_state_t st;
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    qw_bed_state_t b;
    int fo;
    CHECK(qw_gw_raise_alarm(&st, 3, QW_ALARM_VT, 1, 1000, &fo) == QW_ST_OK);
    CHECK(qw_gw_check_deadlines(&st, 1000 + QW_DECISION_DEADLINE_MS - 1) == 0);
    qw_gw_get_bed_state(&st, 3, &b);
    CHECK(b.pending == 1 && b.latched_confirm == 0);
    CHECK(qw_gw_check_deadlines(&st, 1000 + QW_DECISION_DEADLINE_MS) == 1);
    qw_gw_get_bed_state(&st, 3, &b);
    CHECK(b.latched_confirm == 1 && b.detector_alive == 0 && b.failopen_active == 1 && b.pending == 0);
    CHECK(strstr(st.log[(st.log_total - 1) % QW_LOG_RING].reason, "no decision within deadline") != NULL);
    CHECK(qw_gw_check_deadlines(&st, 99999) == 0);

    /* a late decision for the already-latched alarm is rejected */
    qw_msg_decision_t m = mk(3, QW_ALARM_VT, 1, QW_DECISION_SUPPRESS_TO_REVIEW);
    CHECK(qw_gw_apply_decision(&st, &m, 2000, NULL) == QW_ST_NOT_FOUND);

    /* backward clock step: raised "in the future" counts as expired */
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_raise_alarm(&st, 0, QW_ALARM_VT, 1, 5000, &fo) == QW_ST_OK);
    CHECK(qw_gw_check_deadlines(&st, 4000) == 1);
    qw_gw_get_bed_state(&st, 0, &b);
    CHECK(b.latched_confirm == 1 && b.pending == 0 && b.failopen_active == 1);

    /* late SUPPRESS_TO_REVIEW at exactly the deadline fails open */
    qw_msg_reply_t r;
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_raise_alarm(&st, 3, QW_ALARM_VT, 2, 1000, &fo) == QW_ST_OK);
    m = mk(3, QW_ALARM_VT, 2, QW_DECISION_SUPPRESS_TO_REVIEW);
    CHECK(qw_gw_apply_decision(&st, &m, 1000 + QW_DECISION_DEADLINE_MS, &r) == QW_ST_BUSY);
    CHECK(r.status == QW_ST_BUSY && r.latched == 1 && r.version == QW_IPC_VERSION);
    qw_gw_get_bed_state(&st, 3, &b);
    CHECK(b.latched_confirm == 1 && b.pending == 0 && b.in_review == 0);
    CHECK(b.detector_alive == 0 && b.failopen_active == 1);
    CHECK(strstr(st.log[(st.log_total - 1) % QW_LOG_RING].reason, "decision after deadline") != NULL);
    CHECK(qw_gw_check_deadlines(&st, 99999) == 0);

    /* one millisecond earlier it is accepted into review */
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_raise_alarm(&st, 3, QW_ALARM_VT, 3, 1000, &fo) == QW_ST_OK);
    m = mk(3, QW_ALARM_VT, 3, QW_DECISION_SUPPRESS_TO_REVIEW);
    CHECK(qw_gw_apply_decision(&st, &m, 1000 + QW_DECISION_DEADLINE_MS - 1, &r) == QW_ST_OK);
    CHECK(r.latched == 0);
    qw_gw_get_bed_state(&st, 3, &b);
    CHECK(b.in_review == 1 && b.latched_confirm == 0 && b.detector_alive == 1);

    /* a decision stamped before the raise (clock stepped back) fails open */
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_raise_alarm(&st, 3, QW_ALARM_VT, 4, 5000, &fo) == QW_ST_OK);
    m = mk(3, QW_ALARM_VT, 4, QW_DECISION_SUPPRESS_TO_REVIEW);
    CHECK(qw_gw_apply_decision(&st, &m, 4000, &r) == QW_ST_BUSY && r.latched == 1);
    qw_gw_get_bed_state(&st, 3, &b);
    CHECK(b.latched_confirm == 1 && b.in_review == 0);
}

static void test_review_and_ack(void) {
    qw_gw_state_t st;
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    qw_bed_state_t b;
    int fo;
    for (uint32_t s = 1; s <= 2; s++) {
        CHECK(qw_gw_raise_alarm(&st, 0, QW_ALARM_BRADYCARDIA, s, 0, &fo) == QW_ST_OK);
        qw_msg_decision_t m = mk(0, QW_ALARM_BRADYCARDIA, s, QW_DECISION_SUPPRESS_TO_REVIEW);
        CHECK(qw_gw_apply_decision(&st, &m, 100, NULL) == QW_ST_OK);
    }
    /* escalation */
    CHECK(qw_gw_check_escalations(&st, 100 + QW_REVIEW_ESCALATE_MS - 1) == 0);

    /* valid ACK clears seq 1 */
    uint32_t seq = 0;
    char nurse[32];
    const char *ok = "{\"ack\":1,\"nurse\":\"rn1\"}";
    CHECK(qw_gw_parse_ack_json(ok, strlen(ok), &seq, nurse, sizeof nurse) == QW_ST_OK);
    CHECK(seq == 1 && strcmp(nurse, "rn1") == 0);
    CHECK(qw_gw_ack_review(&st, seq, nurse, 200) == QW_ST_OK);
    CHECK(qw_gw_ack_review(&st, seq, nurse, 200) == QW_ST_NOT_FOUND);
    CHECK(qw_gw_ack_review(&st, 1, "", 200) == QW_ST_BAD_ARGS);
    CHECK(qw_gw_ack_review(&st, 1, NULL, 200) == QW_ST_BAD_ARGS);
    qw_gw_get_bed_state(&st, 0, &b);
    CHECK(b.in_review == 1 && b.latched_confirm == 0);

    /* forged seq parses but matches nothing */
    const char *forged = "{\"ack\":999,\"nurse\":\"rn1\"}";
    CHECK(qw_gw_parse_ack_json(forged, strlen(forged), &seq, nurse, sizeof nurse) == QW_ST_OK);
    CHECK(qw_gw_ack_review(&st, seq, nurse, 200) == QW_ST_NOT_FOUND);

    /* remaining entry escalates */
    CHECK(qw_gw_check_escalations(&st, 100 + QW_REVIEW_ESCALATE_MS) == 1);
    qw_gw_get_bed_state(&st, 0, &b);
    CHECK(b.in_review == 0 && b.latched_confirm == 1);
    CHECK(qw_gw_check_escalations(&st, 999999) == 0);

    /* backward clock step on a queued review escalates at once */
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_raise_alarm(&st, 1, QW_ALARM_VT, 7, 0, &fo) == QW_ST_OK);
    qw_msg_decision_t m = mk(1, QW_ALARM_VT, 7, QW_DECISION_SUPPRESS_TO_REVIEW);
    CHECK(qw_gw_apply_decision(&st, &m, 100, NULL) == QW_ST_OK);
    CHECK(qw_gw_check_escalations(&st, 50) == 1);
    qw_gw_get_bed_state(&st, 1, &b);
    CHECK(b.in_review == 0 && b.latched_confirm == 1);

    /* late nurse ACK (at or after escalate_ms) fails open and returns BUSY */
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_raise_alarm(&st, 1, QW_ALARM_VT, 8, 0, &fo) == QW_ST_OK);
    m = mk(1, QW_ALARM_VT, 8, QW_DECISION_SUPPRESS_TO_REVIEW);
    CHECK(qw_gw_apply_decision(&st, &m, 100, NULL) == QW_ST_OK);
    CHECK(qw_gw_ack_review(&st, 8, "rn1", 100 + QW_REVIEW_ESCALATE_MS) == QW_ST_BUSY);
    qw_gw_get_bed_state(&st, 1, &b);
    CHECK(b.in_review == 0 && b.latched_confirm == 1);
    CHECK(strstr(st.log[(st.log_total - 1) % QW_LOG_RING].reason, "nurse ack after escalation") != NULL);
    CHECK(qw_gw_ack_review(&st, 8, "rn1", 100 + QW_REVIEW_ESCALATE_MS) == QW_ST_NOT_FOUND);

    /* one millisecond earlier is a valid ACK, logged at the ACK time */
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_raise_alarm(&st, 1, QW_ALARM_VT, 9, 0, &fo) == QW_ST_OK);
    m = mk(1, QW_ALARM_VT, 9, QW_DECISION_SUPPRESS_TO_REVIEW);
    CHECK(qw_gw_apply_decision(&st, &m, 100, NULL) == QW_ST_OK);
    CHECK(qw_gw_ack_review(&st, 9, "rn1", 100 + QW_REVIEW_ESCALATE_MS - 1) == QW_ST_OK);
    qw_gw_get_bed_state(&st, 1, &b);
    CHECK(b.in_review == 0 && b.latched_confirm == 0);
    const qw_log_entry_t *le = &st.log[(st.log_total - 1) % QW_LOG_RING];
    CHECK(le->state == QW_LOG_ACKED && le->t_ms == 100 + QW_REVIEW_ESCALATE_MS - 1);

    /* ACK stamped before the queue time (clock stepped back) fails open */
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_raise_alarm(&st, 1, QW_ALARM_VT, 10, 5000, &fo) == QW_ST_OK);
    m = mk(1, QW_ALARM_VT, 10, QW_DECISION_SUPPRESS_TO_REVIEW);
    CHECK(qw_gw_apply_decision(&st, &m, 5000, NULL) == QW_ST_OK);
    CHECK(qw_gw_ack_review(&st, 10, "rn1", 4000) == QW_ST_BUSY);
    qw_gw_get_bed_state(&st, 1, &b);
    CHECK(b.in_review == 0 && b.latched_confirm == 1);
}

static void test_hostile_ack(void) {
    uint32_t seq = 0;
    char nurse[32];
    char big[QW_ACK_MAX_BYTES + 16];

    /* oversize: valid shape, 513 bytes */
    size_t pre = strlen("{\"ack\":1,\"nurse\":\"");
    memcpy(big, "{\"ack\":1,\"nurse\":\"", pre);
    size_t len = QW_ACK_MAX_BYTES + 1;
    memset(big + pre, 'x', len - pre - 2);
    big[len - 2] = '"';
    big[len - 1] = '}';
    CHECK(qw_gw_parse_ack_json(big, len, &seq, nurse, sizeof nurse) == QW_ST_BAD_ARGS);

    static const char *const bad[] = {
        "",
        "not json",
        "{",
        "{}",
        "{\"ack\":1,\"nurse\":\"rn1\"",
        "{\"nurse\":\"rn1\"}",
        "{\"ack\":1}",
        "{\"ack\":-1,\"nurse\":\"rn1\"}",
        "{\"ack\":1.5,\"nurse\":\"rn1\"}",
        "{\"ack\":1e2,\"nurse\":\"rn1\"}",
        "{\"ack\":4294967296,\"nurse\":\"rn1\"}",
        "{\"ack\":\"1\",\"nurse\":\"rn1\"}",
        "{\"ack\":1,\"nurse\":\"\"}",
        "{\"ack\":1,\"nurse\":\"a\\nb\"}",
        "{\"ack\":1,\"nurse\":\"a\x01z\"}",
        "{\"ack\":1,\"nurse\":\"a\x7fz\"}",
        "{\"ack\":1,\"nurse\":7}",
        "{\"ack\":1,\"ack\":2,\"nurse\":\"rn1\"}",
        "{\"ack\":1,\"nurse\":\"rn1\"} extra",
        "{\"ack\":1,\"nurse\":\"rn1\"}}",
        "{\"ack\":1,\"nurse\":\"rn1\",}",
        "[{\"ack\":1,\"nurse\":\"rn1\"}]",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        qw_status_t s = qw_gw_parse_ack_json(bad[i], strlen(bad[i]), &seq, nurse, sizeof nurse);
        if (s != QW_ST_BAD_ARGS) fprintf(stderr, "hostile case %zu not rejected: %s\n", i, bad[i]);
        CHECK(s == QW_ST_BAD_ARGS);
    }
    CHECK(qw_gw_parse_ack_json(NULL, 3, &seq, nurse, sizeof nurse) == QW_ST_BAD_ARGS);

    /* embedded NUL inside the declared length (trailing, mid-object, in the name) */
    {
        const char good[] = "{\"ack\":1,\"nurse\":\"rn1\"}";
        size_t gl = sizeof good - 1;
        char raw[QW_ACK_MAX_BYTES];
        memcpy(raw, good, gl);
        raw[gl] = '\0';
        raw[gl + 1] = 'x';
        CHECK(qw_gw_parse_ack_json(raw, gl, &seq, nurse, sizeof nurse) == QW_ST_OK); /* control */
        CHECK(qw_gw_parse_ack_json(raw, gl + 1, &seq, nurse, sizeof nurse) == QW_ST_BAD_ARGS);
        CHECK(qw_gw_parse_ack_json(raw, gl + 2, &seq, nurse, sizeof nurse) == QW_ST_BAD_ARGS);
        memcpy(raw, good, gl);
        raw[2] = '\0'; /* inside the "ack" key */
        CHECK(qw_gw_parse_ack_json(raw, gl, &seq, nurse, sizeof nurse) == QW_ST_BAD_ARGS);
        memcpy(raw, good, gl);
        raw[gl - 3] = '\0'; /* inside the nurse name */
        CHECK(qw_gw_parse_ack_json(raw, gl, &seq, nurse, sizeof nurse) == QW_ST_BAD_ARGS);
    }

    /* accepted edge cases */
    const char *max = "{\"ack\":4294967295,\"nurse\":\"rn1\"}";
    CHECK(qw_gw_parse_ack_json(max, strlen(max), &seq, nurse, sizeof nurse) == QW_ST_OK && seq == UINT32_MAX);
    const char *sp = " {\"nurse\":\"rn2\", \"extra\":[1,{\"a\":null}], \"ack\":5} \n";
    CHECK(qw_gw_parse_ack_json(sp, strlen(sp), &seq, nurse, sizeof nurse) == QW_ST_OK && seq == 5);
    /* nurse longer than the caller's buffer is rejected, not truncated */
    CHECK(qw_gw_parse_ack_json(max, strlen(max), &seq, nurse, 3) == QW_ST_BAD_ARGS);
}

static int all_beds_latched(const qw_gw_state_t *st) {
    for (uint32_t b = 0; b < QW_MAX_BEDS; b++)
        if (st->beds[b].latched_confirm == 0 || st->beds[b].failopen_active != 1) return 0;
    return 1;
}

/* Replay `content` and require fail-open on every bed. */
static int replay_fails_open_all(const char *path, const char *content, size_t n) {
    write_file(path, content, n);
    qw_gw_state_t st;
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    int rc = qw_gw_journal_replay(path, &st);
    if (rc != (int)QW_MAX_BEDS || !all_beds_latched(&st)) {
        fprintf(stderr, "journal case did not fail open: rc=%d\n", rc);
        return 0;
    }
    return 1;
}

static void test_journal(void) {
    strcpy(g_journal, "/tmp/qw_gw_journal_XXXXXX");
    int fd = mkstemp(g_journal);
    if (fd < 0) {
        fprintf(stderr, "mkstemp failed\n");
        exit(2);
    }
    close(fd);
    atexit(cleanup_journal);

    /* seq 10: unacked RAISE (bed 0); seq 11: unacked REVIEW (bed 1);
     * seq 12: acked REVIEW (bed 2); seq 13: raised then confirmed (bed 3) */
    CHECK(qw_gw_journal_append(g_journal, 'R', 10, 0, QW_ALARM_ASYSTOLE, "raise") == 0);
    CHECK(qw_gw_journal_append(g_journal, 'R', 11, 1, QW_ALARM_BRADYCARDIA, NULL) == 0);
    CHECK(qw_gw_journal_append(g_journal, 'Q', 11, 1, QW_ALARM_BRADYCARDIA, "to review") == 0);
    CHECK(qw_gw_journal_append(g_journal, 'R', 12, 2, QW_ALARM_TACHYCARDIA, "x") == 0);
    CHECK(qw_gw_journal_append(g_journal, 'Q', 12, 2, QW_ALARM_TACHYCARDIA, "x") == 0);
    CHECK(qw_gw_journal_append(g_journal, 'A', 12, 2, QW_ALARM_TACHYCARDIA, "nurse one") == 0);
    CHECK(qw_gw_journal_append(g_journal, 'R', 13, 3, QW_ALARM_VT, "x") == 0);
    CHECK(qw_gw_journal_append(g_journal, 'C', 13, 3, QW_ALARM_VT, "x") == 0);
    CHECK(qw_gw_journal_append(g_journal, 'Z', 1, 0, 0, "x") == -1);
    CHECK(qw_gw_journal_append(NULL, 'R', 1, 0, 0, "x") == -1);
    CHECK(qw_gw_journal_append("/tmp/qw_gw_no_such_dir/j", 'R', 1, 0, 0, "x") == -1);

    qw_gw_state_t st;
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(g_journal, &st) == 2);
    CHECK(st.beds[0].latched_confirm == 1);
    CHECK(st.beds[1].latched_confirm == 1);
    CHECK(st.beds[2].latched_confirm == 0);
    CHECK(st.beds[3].latched_confirm == 0);

    /* missing and empty journals are clean */
    CHECK(qw_gw_journal_replay("/tmp/qw_gw_no_such_journal_file", &st) == 0);
    const char *empty = tmp_path("empty");
    write_file(empty, "", 0);
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(empty, &st) == 0);
    CHECK(st.beds[0].latched_confirm == 0);

    /* torn final R without '\n' still parses: that bed latches CONFIRM */
    const char *torn = tmp_path("torn");
    const char t1[] = "R 20 2 1 x\nR 21 3 2 x";
    write_file(torn, t1, sizeof t1 - 1);
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(torn, &st) == 2);
    CHECK(st.beds[2].latched_confirm == 1 && st.beds[3].latched_confirm == 1);
    CHECK(st.beds[0].latched_confirm == 0 && st.beds[1].latched_confirm == 0);
    /* torn final Q too */
    const char t2[] = "Q 22 1 0 x";
    write_file(torn, t2, sizeof t2 - 1);
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(torn, &st) == 1 && st.beds[1].latched_confirm == 1);
    /* torn final A or C could hide a live alarm: fail open everywhere */
    const char t3[] = "R 20 2 1 x\nC 20 2 1 x";
    CHECK(replay_fails_open_all(torn, t3, sizeof t3 - 1));
    const char t4[] = "R 20 2 1 x\nQ 20 2 1 x\nA 20 2 1";
    CHECK(replay_fails_open_all(torn, t4, sizeof t4 - 1));
    /* torn mid-field */
    const char t5[] = "R 20 2 1 x\nR 2";
    CHECK(replay_fails_open_all(torn, t5, sizeof t5 - 1));

    /* corrupt records fail open across ALL beds */
    static const char *const corrupt[] = {
        "R 1 0 0 x\nR -1 0 0 x\n",         "R 1 0 0 x\nR +1 0 0 x\n",
        "R 1 0 0 x\nR 4294967296 0 0 x\n", "R 1 0 0 x\nR 99 77 0 x\n",
        "R 1 0 0 x\nR 99 0 77 x\n",        "R 1 0 0 x\ngarbage line\n",
        "R 1 0 0 x\n\n",                   "R 1 0 0 x\nR 1  0 0 x\n",
        "R 1 0 0 x\nR 99999999999999999999 0 0 x\n", "R 1 0 0 x\nX 5 0 0 x\n",
        "R 1 0 0 x\nR 5 0 0x\n",           "R 1 0 0 x\nR 5 0 0\t\n",
        "R 1 0 0 x\nR 5 0\n",
    };
    for (size_t i = 0; i < sizeof corrupt / sizeof corrupt[0]; i++) {
        int ok = replay_fails_open_all(torn, corrupt[i], strlen(corrupt[i]));
        if (!ok) fprintf(stderr, "corrupt journal case %zu\n", i);
        CHECK(ok);
    }
    /* embedded NUL, over-long line, table overflow */
    const char nul[] = "R 1 0 0 x\nR 5 0 0 \0x\n";
    CHECK(replay_fails_open_all(torn, nul, sizeof nul - 1));
    char longln[400];
    memset(longln, 'x', sizeof longln);
    memcpy(longln, "R 5 0 0 ", 8);
    longln[sizeof longln - 1] = '\n';
    CHECK(replay_fails_open_all(torn, longln, sizeof longln));
    {
        char *big = malloc(300 * 24);
        size_t off = 0;
        for (uint32_t i = 0; i < 300; i++) off += (size_t)sprintf(big + off, "R %u 0 0 x\n", 1000 + i);
        CHECK(replay_fails_open_all(torn, big, off));
        free(big);
    }
    /* the valid journal plus trailing garbage also fails open */
    FILE *f = fopen(g_journal, "a");
    if (f) {
        fputs("garbage line\nR 99 77 0 x\n", f);
        fclose(f);
    }
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(g_journal, &st) == (int)QW_MAX_BEDS && all_beds_latched(&st));

    /* O_NOFOLLOW: a symlink is neither appended through nor trusted on replay */
    const char *target = tmp_path("target");
    const char *lnk = tmp_path("link");
    write_file(target, "", 0);
    if (symlink(target, lnk) != 0) {
        fprintf(stderr, "symlink failed\n");
        exit(2);
    }
    CHECK(qw_gw_journal_append(lnk, 'R', 1, 0, 0, "x") == -1);
    struct stat sb;
    CHECK(stat(target, &sb) == 0 && sb.st_size == 0); /* nothing written through the link */
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(lnk, &st) == (int)QW_MAX_BEDS && all_beds_latched(&st));
    /* journal integrity: C/A/R/Q must agree with the open entry's bed+alarm */
    const char *mm = tmp_path("mismatch");
    const char m1[] = "R 5 0 0 x\nC 5 3 4 x\n";
    CHECK(replay_fails_open_all(mm, m1, sizeof m1 - 1));
    const char m2[] = "R 5 0 0 x\nR 5 1 0 x\n";
    CHECK(replay_fails_open_all(mm, m2, sizeof m2 - 1));
    const char m3[] = "R 5 0 0 x\nQ 5 1 0 x\n";
    CHECK(replay_fails_open_all(mm, m3, sizeof m3 - 1));
    const char m4[] = "R 5 0 0 x\nQ 5 0 0 x\nA 5 0 1 x\n";
    CHECK(replay_fails_open_all(mm, m4, sizeof m4 - 1));
    /* identical duplicate raise stays one open entry */
    const char m5[] = "R 5 0 0 x\nR 5 0 0 x\n";
    write_file(mm, m5, sizeof m5 - 1);
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(mm, &st) == 1 && st.beds[0].latched_confirm == 1);
    /* per-alarm restart latch also sets failopen_active on that bed only */
    const char m6[] = "R 7 2 1 x\n";
    write_file(mm, m6, sizeof m6 - 1);
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(mm, &st) == 1);
    CHECK(st.beds[2].failopen_active == 1 && st.beds[2].latched_confirm == 1);
    CHECK(st.beds[0].failopen_active == 0 && st.beds[1].failopen_active == 0 &&
          st.beds[3].failopen_active == 0);

    /* planted FIFO: neither append nor replay may block */
    const char *fifo = tmp_path("fifo");
    if (mkfifo(fifo, 0600) != 0) {
        fprintf(stderr, "mkfifo failed\n");
        exit(2);
    }
    CHECK(qw_gw_journal_append(fifo, 'R', 1, 0, 0, "x") == -1);
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(fifo, &st) == (int)QW_MAX_BEDS && all_beds_latched(&st));

    /* hardlinked journal (nlink != 1) is refused */
    const char *hl_a = tmp_path("hl_a");
    const char *hl_b = tmp_path("hl_b");
    write_file(hl_a, "R 1 0 0 x\n", 10);
    if (link(hl_a, hl_b) != 0) {
        fprintf(stderr, "link failed\n");
        exit(2);
    }
    CHECK(qw_gw_journal_append(hl_a, 'R', 2, 0, 0, "x") == -1);
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    CHECK(qw_gw_journal_replay(hl_a, &st) == (int)QW_MAX_BEDS && all_beds_latched(&st));

    /* append creates new journals with mode 0600 */
    const char *fresh = tmp_path("fresh");
    CHECK(qw_gw_journal_append(fresh, 'R', 1, 0, 0, "x") == 0);
    CHECK(stat(fresh, &sb) == 0 && (sb.st_mode & 0777) == 0600);
}

int main(void) {
    test_pulse();
    test_ring_basic();
    test_ring_reinit();
    test_ring_detached();
    test_ring_stress();
    test_pending_overflow();
    test_decisions();
    test_deadline();
    test_review_and_ack();
    test_hostile_ack();
    test_journal();
    printf("qw_gateway_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
