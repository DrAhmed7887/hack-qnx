/* qw_failopen.c -- decoupled fail-open helper (QNX 8.0).
 *
 *   qw_failopen
 *
 * Run by the supervisor on repeated worker faults, or by hand. It needs no
 * supervisor, detector or alarm manager: it reads the restart journal (so
 * unresolved alarms are accounted for and a corrupt journal is reported),
 * logs CRIT, and writes "CONFIRM_ALL\n" to QW_STATE_DIR/alarm_out, the file
 * the bedside/central-station output reads to sound every bed. The write is
 * a single write() on a freshly truncated, symlink-refusing file.
 */
#ifdef __QNX__

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qw_gateway_state.h"
#include "qw_log.h"
#include "qw_resmgr.h"

int main(void) {
    qw_log_init("qw_failopen");

    const char *jpath = getenv("QW_JOURNAL_PATH");
    if (jpath == NULL || jpath[0] == '\0') jpath = QW_JOURNAL_PATH;
    const char *apath = getenv("QW_ALARM_OUT_PATH");
    if (apath == NULL || apath[0] == '\0') apath = QW_ALARM_OUT_PATH;

    if (mkdir(QW_STATE_DIR, 0700) != 0 && errno != EEXIST)
        qw_log_emit(QW_LOG_ERROR, 0, 0, NULL, NULL, "cannot create state dir; trying anyway");

    /* Assert CONFIRM_ALL output BEFORE scanning a potentially unbounded journal, and
     * validate regular-file/ownership/link-count BEFORE truncating. */
    int fd = open(apath, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    static const char msg[] = "CONFIRM_ALL\n";
    int ok = 0;
    if (fd >= 0) {
        struct stat sb;
        ok = fstat(fd, &sb) == 0 && S_ISREG(sb.st_mode) && sb.st_uid == geteuid() &&
             sb.st_nlink == 1 && ftruncate(fd, 0) == 0 && lseek(fd, 0, SEEK_SET) == 0 &&
             write(fd, msg, sizeof msg - 1) == (ssize_t)(sizeof msg - 1);
        if (ok && fsync(fd) != 0) ok = 0;
        if (close(fd) != 0) ok = 0;
    }
    if (!ok) {
        for (uint32_t b = 0; b < QW_MAX_BEDS; b++)
            qw_log_emit(QW_LOG_CRIT, b, 0, NULL, "CONFIRM", "fail-open requested but alarm_out write FAILED");
        return 1;
    }

    static qw_gw_state_t st; /* large; keep off the stack */
    qw_gw_init(&st, QW_DECISION_DEADLINE_MS, QW_REVIEW_ESCALATE_MS);
    int unresolved = qw_gw_journal_replay(jpath, &st);
    char note[QW_LOG_REASON_LEN];
    snprintf(note, sizeof note, "fail-open: all beds CONFIRM; %d unresolved journal alarms", unresolved);

    for (uint32_t b = 0; b < QW_MAX_BEDS; b++)
        qw_log_emit(QW_LOG_CRIT, b, 0, NULL, "CONFIRM", note);
    return 0;
}

#else /* !__QNX__ */

#include <stdio.h>

int main(void) {
    fputs("qw_failopen: QNX Neutrino only; build with make qnx\n", stderr);
    return 1;
}

#endif
