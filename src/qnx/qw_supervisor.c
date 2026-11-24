/* qw_supervisor.c -- small fallback process supervisor (QNX 8.0).
 *
 *   qw_supervisor <rules.json> <detector.json> [--bin-dir DIR]
 *
 * Starts qw_alarm_mgr and qw_detector with posix_spawn(), waits with
 * waitpid(), restarts a dead worker up to MAX_RESTARTS times, and on a
 * repeated fault runs qw_failopen (every bed CONFIRM) and gives up. This is
 * a plain parent process, not the declarative supervisor of the platform
 * image: it only supervises these two workers and is labelled as such.
 */
#ifdef __QNX__

#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "qw_log.h"

#define MAX_RESTARTS 3
#define N_WORKERS 2

extern char **environ;
static volatile sig_atomic_t g_stop;

static void on_term(int sig) {
    (void)sig;
    g_stop = 1;
}

typedef struct {
    const char *name;
    pid_t pid;
    int restarts;
} worker_t;

static pid_t spawn_one(const char *dir, const char *name, char *const extra[]) {
    char path[256];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    char *argv[6] = {path, NULL, NULL, NULL, NULL, NULL};
    for (int i = 0; extra != NULL && extra[i] != NULL && i < 4; i++) argv[i + 1] = extra[i];
    pid_t pid = -1;
    if (posix_spawn(&pid, path, NULL, NULL, argv, environ) != 0) return -1;
    return pid;
}

static void kill_all(worker_t *w) {
    for (int i = 0; i < N_WORKERS; i++)
        if (w[i].pid > 0) kill(w[i].pid, SIGTERM);
    for (int iter = 0; iter < 50; iter++) { /* 1 s graceful grace window */
        while (waitpid(-1, NULL, WNOHANG) > 0) {
        }
        int any = 0;
        for (int i = 0; i < N_WORKERS; i++)
            if (w[i].pid > 0 && kill(w[i].pid, 0) == 0) any = 1;
        if (!any) return;
        struct timespec ts = {0, 20000000L}; /* 20 ms */
        nanosleep(&ts, NULL);
    }
    for (int i = 0; i < N_WORKERS; i++)
        if (w[i].pid > 0) kill(w[i].pid, SIGKILL);
    for (int iter = 0; iter < 25; iter++) {
        while (waitpid(-1, NULL, WNOHANG) > 0) {
        }
        int any = 0;
        for (int i = 0; i < N_WORKERS; i++)
            if (w[i].pid > 0 && kill(w[i].pid, 0) == 0) any = 1;
        if (!any) return;
        struct timespec ts = {0, 20000000L};
        nanosleep(&ts, NULL);
    }
}

int main(int argc, char **argv) {
    const char *dir = ".";
    if (argc < 3) {
        fprintf(stderr, "usage: %s <rules.json> <detector.json> [--bin-dir DIR]\n", argv[0]);
        return 2;
    }
    if (argc == 5 && strcmp(argv[3], "--bin-dir") == 0) dir = argv[4];
    qw_log_init("qw_supervisor");

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term; /* no SA_RESTART: waitpid returns EINTR */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    char *det_args[] = {argv[1], argv[2], NULL};
    worker_t w[N_WORKERS] = {{"qw_alarm_mgr", -1, 0}, {"qw_detector", -1, 0}};
    for (int i = 0; i < N_WORKERS; i++) {
        w[i].pid = spawn_one(dir, w[i].name, i == 1 ? det_args : NULL);
        if (w[i].pid < 0) goto fail;
    }

    while (!g_stop) {
        int status = 0;
        pid_t dead = waitpid(-1, &status, 0);
        if (dead < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < N_WORKERS; i++) {
            if (w[i].pid != dead) continue;
            qw_log_emit(QW_LOG_ERROR, 0, 0, NULL, NULL, i == 0 ? "qw_alarm_mgr exited" : "qw_detector exited");
            if (++w[i].restarts > MAX_RESTARTS) goto fail;
            w[i].pid = spawn_one(dir, w[i].name, i == 1 ? det_args : NULL);
            if (w[i].pid < 0) goto fail;
        }
    }
    kill_all(w);
    return 0;

fail:
    qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, "CONFIRM", "worker fault limit reached; running qw_failopen");
    /* Spawn fail-open BEFORE killing hung workers, with a bounded 2 s wait. */
    pid_t fo = spawn_one(dir, "qw_failopen", NULL);
    if (fo < 0) {
        qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, "CONFIRM", "qw_failopen spawn FAILED");
    } else {
        int fo_st = 0, reaped = 0;
        for (int iter = 0; iter < 100; iter++) {
            pid_t r = waitpid(fo, &fo_st, WNOHANG);
            if (r == fo) {
                reaped = 1;
                break;
            }
            if (r < 0 && errno != EINTR) break;
            struct timespec ts = {0, 20000000L}; /* 20 ms */
            nanosleep(&ts, NULL);
        }
        if (!reaped) {
            kill(fo, SIGKILL);
            waitpid(fo, &fo_st, WNOHANG);
            qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, "CONFIRM", "qw_failopen timed out after 2 s");
        } else if (!WIFEXITED(fo_st) || WEXITSTATUS(fo_st) != 0) {
            qw_log_emit(QW_LOG_CRIT, 0, 0, NULL, "CONFIRM", "qw_failopen exited non-zero");
        }
    }
    kill_all(w);
    return 1;
}

#else /* !__QNX__ */

#include <stdio.h>

int main(void) {
    fputs("qw_supervisor: QNX Neutrino only; build with make qnx\n", stderr);
    return 1;
}

#endif
