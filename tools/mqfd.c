// mqfd - usa o fd de mqueue de outro processo (via /proc/PID/fd/N).
//
// O ZipMountServer da TV expoe um par de mqueues: fd 4 = /zip-client
// (pedidos) e fd 5 = /zip-server (respostas). Este helper permite falar com
// ele sem precisar de link simbolico/namespace de mqueue.
//
// uso:
//   mqfd send <pid> <fd> '<json>'
//   mqfd recv <pid> <fd> [timeout_ms]
//
// build (ARMv7 estatico):
//   arm-linux-musleabihf-gcc -O2 -static -s -o mqfd.arm mqfd.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/types.h>

#ifndef __NR_mq_timedsend
#define __NR_mq_timedsend 276
#define __NR_mq_timedreceive 277
#endif

static int open_fd(pid_t pid, int n, int flags, char *path, size_t pl) {
    snprintf(path, pl, "/proc/%d/fd/%d", (int)pid, n);
    int fd = open(path, flags);
    if (fd < 0) fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
    return fd;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s send|recv <pid> <fd> [json|timeout]\n", argv[0]); return 2; }
    pid_t pid = atoi(argv[2]);
    int n = atoi(argv[3]);
    char path[64];
    if (!strcmp(argv[1], "send")) {
        if (argc < 5) return 2;
        int fd = open_fd(pid, n, O_WRONLY, path, sizeof path);
        if (fd < 0) return 1;
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 2;
        const char *m = argv[4];
        long r = syscall(__NR_mq_timedsend, fd, m, strlen(m) + 1, 1, &ts);
        printf("send fd=%d r=%ld errno=%d (%s)\n", fd, r, errno, strerror(errno));
        close(fd);
        return r < 0 ? 1 : 0;
    } else if (!strcmp(argv[1], "recv")) {
        long tmo = argc > 4 ? atol(argv[4]) : 2000;
        int fd = open_fd(pid, n, O_RDONLY, path, sizeof path);
        if (fd < 0) return 1;
        char buf[32768];
        int got = 0, tries = (int)(tmo / 200) + 1;
        for (int i = 0; i < tries; i++) {
            struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 200 * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            unsigned prio = 0;
            long r = syscall(__NR_mq_timedreceive, fd, buf, sizeof(buf) - 1, &prio, &ts);
            if (r > 0) { buf[r] = 0; printf("RECV[%ld]: %s\n", r, buf); got++; if (got >= 4) break; }
            else if (errno == ETIMEDOUT || errno == EAGAIN) { if (got) break; }
            else { printf("recv errno=%d (%s)\n", errno, strerror(errno)); break; }
        }
        if (!got) printf("(no reply)\n");
        close(fd);
        return 0;
    }
    return 2;
}
