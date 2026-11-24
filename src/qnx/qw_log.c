/* qw_log.c -- see qw_log.h. */
#include "qw_log.h"

#include <stdio.h>
#include <string.h>

#ifdef __QNX__
#include <sys/slog2.h>
#endif

/* ---- pure C11 formatting (always compiled, host-tested) ---- */

typedef struct {
    char *out;
    size_t limit; /* max bytes of text */
    size_t n;
} appender_t;

static void app_char(appender_t *a, char c) {
    if (a->n < a->limit) a->out[a->n++] = c;
}

static void app_lit(appender_t *a, const char *s) {
    for (; *s != '\0'; s++) app_char(a, *s);
}

/* Free text: control characters become spaces so the entry stays one line. */
static void app_text(appender_t *a, const char *s) {
    if (s == NULL) return;
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        app_char(a, (c < 0x20 || c == 0x7f) ? ' ' : (char)c);
    }
}

static void app_u32(appender_t *a, uint32_t v) {
    char tmp[11];
    int i = 0;
    do {
        tmp[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0);
    while (i > 0) app_char(a, tmp[--i]);
}

static const char *sev_name(qw_log_sev_t sev) {
    switch (sev) {
    case QW_LOG_INFO: return "INFO";
    case QW_LOG_WARN: return "WARN";
    case QW_LOG_ERROR: return "ERROR";
    case QW_LOG_CRIT: return "CRIT";
    }
    return "UNKNOWN";
}

int qw_log_format_line(char *out, size_t cap, qw_log_sev_t sev, uint32_t bed, uint32_t seq,
                       const char *alarm_name, const char *decision, const char *reason) {
    if (out == NULL || cap < 2) return -1;
    appender_t a = {out, cap - 1 < QW_SLOG_MAX_LEN ? cap - 1 : (size_t)QW_SLOG_MAX_LEN, 0};
    app_lit(&a, "sev=");
    app_lit(&a, sev_name(sev));
    app_lit(&a, " bed=");
    app_u32(&a, bed);
    app_lit(&a, " seq=");
    app_u32(&a, seq);
    app_lit(&a, " alarm=");
    app_text(&a, alarm_name);
    app_lit(&a, " decision=");
    app_text(&a, decision);
    app_lit(&a, " reason=");
    app_text(&a, reason);
    out[a.n] = '\0';
    return (int)a.n;
}

/* ---- sinks ---- */

#ifdef __QNX__
static slog2_buffer_t g_slog_buf;
static int g_slog_ready = 0;
static uint8_t g_slog_severity[4] = {SLOG2_INFO, SLOG2_WARNING, SLOG2_ERROR, SLOG2_CRITICAL};

int qw_log_init(const char *prog_name) {
    (void)prog_name; /* the buffer set is per process; the program name is in slog2info's PID column */
    slog2_buffer_set_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.buffer_set_name = "quietward";
    cfg.num_buffers = 1;
    cfg.verbosity_level = SLOG2_INFO;
    cfg.buffer_config[0].buffer_name = "audit";
    cfg.buffer_config[0].num_pages = 4;
    if (slog2_register(&cfg, &g_slog_buf, 0) != 0) {
        g_slog_ready = 0;
        return -1;
    }
    g_slog_ready = 1;
    return 0;
}

void qw_log_emit(qw_log_sev_t sev, uint32_t bed, uint32_t seq, const char *alarm_name,
                 const char *decision, const char *reason) {
    char buf[QW_SLOG_BUF_LEN];
    if (qw_log_format_line(buf, sizeof buf, sev, bed, seq, alarm_name, decision, reason) < 0) return;
    unsigned idx = ((unsigned)sev < 4u) ? (unsigned)sev : (unsigned)QW_LOG_CRIT;
    if (g_slog_ready) {
        slog2f(g_slog_buf, 0, g_slog_severity[idx], "%s", buf);
    } else {
        fprintf(stderr, "[quietward] %s\n", buf);
    }
}
#else
static char g_prog_name[64];

int qw_log_init(const char *prog_name) {
    size_t n = 0;
    if (prog_name != NULL)
        for (; prog_name[n] != '\0' && n + 1 < sizeof g_prog_name; n++) g_prog_name[n] = prog_name[n];
    g_prog_name[n] = '\0';
    return 0;
}

void qw_log_emit(qw_log_sev_t sev, uint32_t bed, uint32_t seq, const char *alarm_name,
                 const char *decision, const char *reason) {
    char buf[QW_SLOG_BUF_LEN];
    if (qw_log_format_line(buf, sizeof buf, sev, bed, seq, alarm_name, decision, reason) < 0) return;
    fprintf(stderr, "[quietward] %s\n", buf);
}
#endif
