/* qw_log.h -- Quiet Ward audit logging (slog2 on QNX, stderr on the host).
 *
 * qw_log_format_line() is pure C11 and unconditionally compiled so the exact
 * line that reaches slog2 is unit-tested on the host. Only qw_log_init() and
 * qw_log_emit() touch <sys/slog2.h>, behind #ifdef __QNX__.
 *
 * One slog2 entry is limited to 512 bytes, so a formatted line never exceeds
 * QW_SLOG_MAX_LEN (511) bytes, and control characters in free-text fields
 * are replaced with spaces so every entry stays on one line.
 */
#ifndef QW_LOG_H
#define QW_LOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QW_SLOG_MAX_LEN 511 /* bytes of text, excluding the NUL */
#define QW_SLOG_BUF_LEN 512 /* buffer that holds one formatted line + NUL */

_Static_assert(QW_SLOG_MAX_LEN < 512, "slog2f entries are limited to 512 bytes");
_Static_assert(QW_SLOG_BUF_LEN == QW_SLOG_MAX_LEN + 1, "buffer holds the maximum line and its NUL");

typedef enum { QW_LOG_INFO = 0, QW_LOG_WARN, QW_LOG_ERROR, QW_LOG_CRIT } qw_log_sev_t;

/* QNX: register the "quietward" buffer set with one "audit" buffer; returns
 * -1 on failure (emit then falls back to stderr). Host: remember prog_name,
 * return 0. */
int qw_log_init(const char *prog_name);

/* Format "sev=%s bed=%u seq=%u alarm=%s decision=%s reason=%s" into out.
 * Control characters (< 0x20, 0x7f) in the three strings become spaces; NULL
 * strings print as empty. Output is truncated to min(cap - 1, 511) bytes and
 * always NUL-terminated. Returns bytes written (excluding the NUL), or -1 if
 * out == NULL or cap < 2. */
int qw_log_format_line(char *out, size_t cap, qw_log_sev_t sev, uint32_t bed, uint32_t seq,
                       const char *alarm_name, const char *decision, const char *reason);

/* Format and write one audit line (slog2 if registered, else stderr). */
void qw_log_emit(qw_log_sev_t sev, uint32_t bed, uint32_t seq, const char *alarm_name,
                 const char *decision, const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* QW_LOG_H */
