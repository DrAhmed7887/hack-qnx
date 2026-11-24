/* p1_hello_ipc.c -- the five QNX 8.0 smoke checks for the P1 gate.
 *
 *   1. ChannelCreate / ConnectAttach
 *   2. MsgSend / MsgReceive / MsgReply round trip
 *   3. slog2_register + slog2f (read back with: slog2info -b qw_hello)
 *   4. clock_gettime(CLOCK_MONOTONIC)
 *
 * QNX build: qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror -o p1_hello_ipc scripts/p1_hello_ipc.c -lslog2
 * Off QNX this compiles to a stub that says so.
 */
#ifdef __QNX__
#ifndef _QNX_SOURCE
#define _QNX_SOURCE
#endif
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>

#ifdef __QNX__

#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/iofunc.h>
#include <sys/neutrino.h>
#include <sys/slog2.h>
#include <time.h>

typedef struct { int value; } req_t;
typedef struct { int value; } rep_t;

static _Atomic int g_chid = -1;

/* ChannelDestroy at most once across error and exit paths. */
static void destroy_channel_once(void) {
    int c = atomic_exchange(&g_chid, -1);
    if (c != -1) ChannelDestroy(c);
}
static int g_server_rc = 0; /* 0 = server handled one request cleanly */

/* One-shot server. On any error it records a non-zero g_server_rc and, if it
 * already holds a client's rcvid, unblocks that client with an error so the
 * client never hangs forever. */
static void *server(void *arg) {
    (void)arg;
    req_t req;
    struct _msg_info info;
    rcvid_t rcvid = MsgReceive(atomic_load(&g_chid), &req, sizeof req, &info);
    if (rcvid <= 0) { /* -1 error, 0 = pulse (none expected here) */
        g_server_rc = 1;
        destroy_channel_once(); /* unblocks a send-blocked client with an error */
        return NULL;
    }
    /* ChannelCreate(0) is private to main() and does not enable _NTO_CHF_SENDER_LEN; check received msglen. */
    if (info.msglen < 0 || (size_t)info.msglen != sizeof req) {
        MsgError(rcvid, EBADMSG);
        g_server_rc = 2;
        return NULL;
    }
    rep_t rep = {req.value + 1};
    if (MsgReply(rcvid, EOK, &rep, sizeof rep) == -1) {
        g_server_rc = 3;
        return NULL;
    }
    return NULL;
}

int main(void) {
    int fails = 0;

    slog2_buffer_set_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.num_buffers = 1;
    cfg.verbosity_level = SLOG2_INFO;
    cfg.buffer_set_name = "qw_hello";
    cfg.buffer_config[0].buffer_name = "main";
    cfg.buffer_config[0].num_pages = 1;
    slog2_buffer_t buf = NULL;
    if (slog2_register(&cfg, &buf, 0) != 0) {
        printf("FAIL slog2_register\n");
        fails++;
    } else {
        slog2f(buf, 0, SLOG2_INFO, "p1_hello_ipc: slog2 ok");
        printf("PASS slog2_register + slog2f (verify with: slog2info -b qw_hello)\n");
    }

    int chid = ChannelCreate(0);
    if (chid == -1) {
        printf("FAIL ChannelCreate: %s\n", strerror(errno));
        return 1;
    }
    atomic_store(&g_chid, chid);
    printf("PASS ChannelCreate chid=%d\n", chid);

    int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        printf("FAIL ConnectAttach: %s\n", strerror(errno));
        destroy_channel_once();
        return 1;
    }
    printf("PASS ConnectAttach coid=%d\n", coid);

    pthread_t th;
    int prc = pthread_create(&th, NULL, server, NULL);
    if (prc != 0) {
        fprintf(stderr, "FAIL pthread_create: %s\n", strerror(prc));
        ConnectDetach(coid);
        destroy_channel_once();
        return 1;
    }
    req_t req = {41};
    rep_t rep = {0};
    /* Bound the send/reply wait (2 s) so a dead server cannot hang us. */
    uint64_t timeout_ns = 2000000000ULL;
    TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, NULL, &timeout_ns, NULL);
    if (MsgSend(coid, &req, sizeof req, &rep, sizeof rep) == -1 || rep.value != 42) {
        printf("FAIL MsgSend/MsgReceive/MsgReply\n");
        fails++;
        destroy_channel_once(); /* make sure the server thread cannot stay blocked in MsgReceive */
    } else {
        printf("PASS MsgSend/MsgReceive/MsgReply (41 -> 42)\n");
    }
    pthread_join(th, NULL);
    if (g_server_rc != 0) {
        printf("FAIL server thread error code %d\n", g_server_rc);
        fails++;
    }

    struct timespec t0, t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0 || clock_gettime(CLOCK_MONOTONIC, &t1) != 0 ||
        (t1.tv_sec < t0.tv_sec || (t1.tv_sec == t0.tv_sec && t1.tv_nsec < t0.tv_nsec))) {
        printf("FAIL clock_gettime(CLOCK_MONOTONIC)\n");
        fails++;
    } else {
        printf("PASS clock_gettime(CLOCK_MONOTONIC)\n");
    }

    ConnectDetach(coid);
    destroy_channel_once(); /* may already be gone on an error path: failure ignored */
    printf(fails ? "P1_HELLO_RESULT=FAIL\n" : "P1_HELLO_RESULT=PASS\n");
    return fails ? 1 : 0;
}

#else

int main(void) {
    fprintf(stderr, "p1_hello_ipc: QNX Neutrino only; build with qcc on the QNX 8.0 target.\n");
    return 1;
}

#endif
