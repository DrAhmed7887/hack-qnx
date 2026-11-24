/* qw_bedctl.c -- query one bed's state from /dev/quietward (QNX 8.0).
 *
 *   qw_bedctl <bed_id>
 *
 * Prints one JSON object. Exit 0 on success, 1 on a device error, 2 on bad
 * arguments.
 */
#ifdef __QNX__

#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qw_ipc.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <bed_id>\n", argv[0]);
        return 2;
    }
    char *end = NULL;
    unsigned long bed = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || argv[1][0] == '-' || bed >= QW_MAX_BEDS) {
        fprintf(stderr, "%s: bed_id must be 0..%d\n", argv[0], QW_MAX_BEDS - 1);
        return 2;
    }

    int fd = open(QW_DEV_PATH, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "%s: cannot open %s: %s\n", argv[0], QW_DEV_PATH, strerror(errno));
        return 1;
    }
    qw_bed_state_t bs;
    memset(&bs, 0, sizeof bs);
    bs.bed_id = (uint32_t)bed;
    int rc = devctl(fd, QW_DCMD_BED_STATE, &bs, sizeof bs, NULL); /* returns an errno value */
    close(fd);
    if (rc != EOK) {
        fprintf(stderr, "%s: devctl failed: %s\n", argv[0], strerror(rc));
        return 1;
    }
    printf("{\"bed_id\":%u,\"detector_alive\":%u,\"failopen_active\":%u,\"pending\":%u,"
           "\"in_review\":%u,\"latched_confirm\":%u,\"last_seq\":%u}\n",
           bs.bed_id, bs.detector_alive, bs.failopen_active, bs.pending, bs.in_review,
           bs.latched_confirm, bs.last_seq);
    return 0;
}

#else /* !__QNX__ */

#include <stdio.h>

int main(void) {
    fputs("qw_bedctl: QNX Neutrino only; build with make qnx\n", stderr);
    return 1;
}

#endif
