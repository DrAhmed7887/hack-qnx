/* p1_tcg_jitter.c -- timing calibration for QEMU/TCG (or any host).
 *
 * Usage: p1_tcg_jitter [iters=500] [period_ms=10]
 *
 * Sleeps period_ms with nanosleep() against CLOCK_MONOTONIC deadlines,
 * records how late each wakeup is, and prints p50/p95/p99/max lateness,
 * the CLOCK_MONOTONIC vs CLOCK_REALTIME drift over the run, and a
 * suggested --watchdog-ms: max(200, ceil(3 * p99_ms + 50)).
 * Under TCG this is logical-time scheduling jitter, not a real-time
 * guarantee. Builds on macOS, Linux and QNX 8.0 (qcc).
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int64_t ts_ns(const struct timespec *t) {
    return (int64_t)t->tv_sec * 1000000000LL + t->tv_nsec;
}

/* Returns 0 and the time in *out, or -1 (errno set) if the clock read fails. */
static int now_ns(clockid_t c, int64_t *out) {
    struct timespec t;
    if (clock_gettime(c, &t) != 0) return -1;
    *out = ts_ns(&t);
    return 0;
}

static int cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static double pct_ms(const int64_t *sorted, long n, double p) {
    long idx = (long)ceil(p / 100.0 * (double)n) - 1;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return (double)sorted[idx] / 1e6;
}

int main(int argc, char **argv) {
    long iters = 500;
    long period_ms = 10;
    if (argc > 1) iters = strtol(argv[1], NULL, 10);
    if (argc > 2) period_ms = strtol(argv[2], NULL, 10);
    if (iters < 1 || iters > 1000000 || period_ms < 1 || period_ms > 10000) {
        fprintf(stderr, "usage: %s [iters=500] [period_ms=10]\n", argv[0]);
        return 2;
    }

    int64_t *late = malloc((size_t)iters * sizeof *late);
    if (late == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    int64_t period_ns = (int64_t)period_ms * 1000000LL;
    int64_t mono0, real0, mono1, real1;
    if (now_ns(CLOCK_MONOTONIC, &mono0) != 0 || now_ns(CLOCK_REALTIME, &real0) != 0) {
        perror("clock_gettime");
        free(late);
        return 1;
    }
    int64_t next = mono0;

    for (long i = 0; i < iters; i++) {
        next += period_ns;
        int64_t woke;
        for (;;) {
            int64_t cur;
            if (now_ns(CLOCK_MONOTONIC, &cur) != 0) {
                perror("clock_gettime");
                free(late);
                return 1;
            }
            if (next <= cur) {
                woke = cur;
                break;
            }
            struct timespec req = {(time_t)((next - cur) / 1000000000LL),
                                   (long)((next - cur) % 1000000000LL)};
            if (nanosleep(&req, NULL) == -1 && errno != EINTR) {
                perror("nanosleep");
                free(late);
                return 1;
            }
            /* Slept fully or was interrupted (EINTR): loop re-reads the clock
             * and sleeps only the remaining time to the same deadline. */
        }
        late[i] = woke > next ? woke - next : 0;
    }

    if (now_ns(CLOCK_MONOTONIC, &mono1) != 0 || now_ns(CLOCK_REALTIME, &real1) != 0) {
        perror("clock_gettime");
        free(late);
        return 1;
    }
    double drift_ms = (double)((real1 - real0) - (mono1 - mono0)) / 1e6;

    qsort(late, (size_t)iters, sizeof *late, cmp_i64);
    double p50 = pct_ms(late, iters, 50), p95 = pct_ms(late, iters, 95);
    double p99 = pct_ms(late, iters, 99), pmax = (double)late[iters - 1] / 1e6;
    double rec = ceil(3.0 * p99 + 50.0);
    if (rec < 200.0) rec = 200.0;

    printf("iters=%ld period_ms=%ld\n", iters, period_ms);
    printf("jitter_ms p50=%.3f p95=%.3f p99=%.3f max=%.3f\n", p50, p95, p99, pmax);
    printf("drift_ms (realtime - monotonic over run) = %.3f\n", drift_ms);
    printf("recommended --watchdog-ms %.0f\n", rec);
    free(late);
    return 0;
}
