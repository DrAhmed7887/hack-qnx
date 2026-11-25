/* p4_resmgr_cap_test.c -- QNX target test for MAX_OPEN_SNAPS (8) on /dev/quietward. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define CAP 8
#define DEV "/dev/quietward"

int main(void) {
    int fds[CAP];
    for (int i = 0; i < CAP; i++) fds[i] = -1;

    for (int i = 0; i < CAP; i++) {
        fds[i] = open(DEV, O_RDONLY);
        if (fds[i] < 0) {
            perror("open(0..7)");
            return 2;
        }
        char ch;
        ssize_t n = read(fds[i], &ch, 1);
        if (n < 1) {
            fprintf(stderr, "read(%d) returned %zd (errno=%d)\n", i, n, errno);
            return 3;
        }
    }

    int extra = open(DEV, O_RDONLY);
    if (extra < 0) {
        perror("open(9th)");
        return 4;
    }
    char ch;
    errno = 0;
    ssize_t n9 = read(extra, &ch, 1);
    int e9 = errno;
    close(extra);
    if (n9 != -1 || e9 != EBUSY) {
        fprintf(stderr, "expected 9th read EBUSY, got n=%zd errno=%d\n", n9, e9);
        return 5;
    }

    for (int i = 0; i < CAP; i++) {
        close(fds[i]);
        fds[i] = -1;
    }

    int after = open(DEV, O_RDONLY);
    if (after < 0) {
        perror("open(after close)");
        return 6;
    }
    ssize_t na = read(after, &ch, 1);
    close(after);
    if (na < 1) {
        fprintf(stderr, "read(after close) returned %zd (errno=%d)\n", na, errno);
        return 7;
    }

    puts("RESMGR_CAP_PASS 8_pinned_9th_EBUSY_released_ok");
    return 0;
}
