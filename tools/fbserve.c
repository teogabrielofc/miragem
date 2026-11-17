/*
 * fbserve.c - Miragem: transmite o framebuffer da UI (/dev/fb1) por TCP.
 *
 * Roda na TV (ARM), sem dependencias. Le o buffer visivel do fb (respeita o
 * yoffset do double buffer via mmap) e envia apenas os tiles 64x64 que
 * mudaram. E o mesmo principio do scrcpy, sem encoder de video: o PC recebe
 * pedacos crus e monta o frame localmente.
 *
 * Protocolo (little-endian):
 *   cabecalho de sessao (32 B):
 *     char magic[4] = "FBS2"; u32 w, h, stride, bpp, fps, format, tile
 *     format: 1 = BGRX (32 bpp), 2 = RGB565 (16 bpp); tile = 64
 *   cada frame:
 *     u32 payload_len, u32 seq, u32 flags
 *     payload:
 *       u32 ntiles
 *       ntiles x { u16 tx, u16 ty, dados do tile (tw*th*bpp/8, linhas
 *                  compactas; tw/th = min(tile, borda)) }
 *     flags bit0 = 1 -> tiles (atual protocolo); 0 -> frame cru (w*h*bpp/8)
 *
 * Build:
 *   arm-linux-musleabihf-gcc -O2 -static -no-pie -s \
 *     -o tools/fbserve.arm tools/fbserve.c
 *
 * Uso:
 *   fbserve [-d /dev/fb1] [-p 2331] [-f 10] [-b 0.0.0.0] [-n 1] [-q]
 *     -n N   encerra depois de entregar N frames (1 = screenshot)
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_W 4096
#define MAX_H 4096
#define MAGIC "FBS2"
#define TILE 64

static volatile sig_atomic_t g_stop = 0;
static unsigned char *g_map = NULL;
static size_t g_maplen = 0;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static int write_all(int fd, const void *buf, size_t n) {
    const unsigned char *p = (const unsigned char *)buf;
    while (n) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (k == 0) return -1;
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

static int fb_open_dev(const char *dev, int *w, int *h, int *stride, int *bpp) {
    int fd = open(dev, O_RDWR);
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (fd < 0) return -1;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) || ioctl(fd, FBIOGET_FSCREENINFO, &fi)) {
        close(fd);
        return -1;
    }
    *w = (int)vi.xres;
    *h = (int)vi.yres;
    *stride = (int)fi.line_length;
    *bpp = (int)vi.bits_per_pixel;
    if (*w <= 0 || *h <= 0 || *w > MAX_W || *h > MAX_H || (*bpp != 16 && *bpp != 32) ||
        *stride < *w * (*bpp / 8)) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    return fd;
}

/* Le apenas a area visivel (yoffset do double buffer do compositor). */
static int read_visible(int fd, unsigned char *buf, int w, int h, int stride, int bpp) {
    struct fb_var_screeninfo vi, vi2;
    int row = w * (bpp / 8);
    int attempt;
    if (!g_map || !g_maplen) {
        errno = ENODEV;
        return -1;
    }
    for (attempt = 0; attempt < 4; attempt++) {
        int y;
        size_t base;
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vi)) return -1;
        base = (size_t)vi.yoffset * (size_t)stride;
        if (base + (size_t)(h - 1) * (size_t)stride + (size_t)row > g_maplen) return -1;
        for (y = 0; y < h; y++) {
            memcpy(buf + (size_t)y * (size_t)row,
                   g_map + base + (size_t)y * (size_t)stride, (size_t)row);
        }
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vi2)) return -1;
        if (vi2.yoffset == vi.yoffset) return 0;
    }
    return 0;
}

static void put_u32(unsigned char *p, uint32_t v) { memcpy(p, &v, 4); }
static void put_u16(unsigned char *p, uint16_t v) { memcpy(p, &v, 2); }

static int tile_changed(const unsigned char *cur, const unsigned char *prev,
                        int w, int psize, int tx, int ty, int tw, int th) {
    size_t row = (size_t)w * (size_t)psize;
    size_t off = (size_t)(ty * TILE) * row + (size_t)(tx * TILE) * (size_t)psize;
    size_t n = (size_t)tw * (size_t)psize;
    int y;
    for (y = 0; y < th; y++) {
        if (memcmp(cur + off, prev + off, n) != 0) return 1;
        off += row;
    }
    return 0;
}

static void copy_tile(const unsigned char *cur, unsigned char *out,
                      int w, int psize, int tx, int ty, int tw, int th) {
    size_t row = (size_t)w * (size_t)psize;
    size_t off = (size_t)(ty * TILE) * row + (size_t)(tx * TILE) * (size_t)psize;
    size_t n = (size_t)tw * (size_t)psize;
    int y;
    for (y = 0; y < th; y++) {
        memcpy(out, cur + off, n);
        out += n;
        off += row;
    }
}

static int serve_client(int cfd, int ffd, int w, int h, int stride, int bpp,
                        int fps, int oneshot, int quiet, unsigned char *cur,
                        unsigned char *prev, unsigned char *enc, size_t raw) {
    unsigned char shead[32];
    uint32_t seq = 0;
    int first = 1;
    int psize = bpp / 8;
    size_t row = (size_t)w * (size_t)psize;
    int tiles_x = (w + TILE - 1) / TILE;
    int tiles_y = (h + TILE - 1) / TILE;
    int got_any = 0;

    (void)row;
    memcpy(shead, MAGIC, 4);
    put_u32(shead + 4, (uint32_t)w);
    put_u32(shead + 8, (uint32_t)h);
    put_u32(shead + 12, (uint32_t)stride);
    put_u32(shead + 16, (uint32_t)bpp);
    put_u32(shead + 20, (uint32_t)fps);
    put_u32(shead + 24, (uint32_t)(psize == 4 ? 1 : 2));
    put_u32(shead + 28, (uint32_t)TILE);
    if (write_all(cfd, shead, sizeof(shead))) {
        perror("send header");
        return -1;
    }

    while (!g_stop) {
        struct timespec t0, t1;
        unsigned char fhead[12];
        size_t o = 4;
        uint32_t nt = 0;
        int tx, ty;
        int changed_any = 0;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        if (read_visible(ffd, cur, w, h, stride, bpp)) {
            perror("read_visible");
            return -1;
        }

        for (ty = 0; ty < tiles_y; ty++) {
            int th = h - ty * TILE;
            if (th > TILE) th = TILE;
            for (tx = 0; tx < tiles_x; tx++) {
                int tw = w - tx * TILE;
                if (tw > TILE) tw = TILE;
                if (!first && !tile_changed(cur, prev, w, psize, tx, ty, tw, th)) continue;
                put_u16(enc + o, (uint16_t)tx);
                put_u16(enc + o + 2, (uint16_t)ty);
                o += 4;
                copy_tile(cur, enc + o, w, psize, tx, ty, tw, th);
                o += (size_t)tw * (size_t)th * (size_t)psize;
                nt++;
                changed_any = 1;
            }
        }
        if (!changed_any) goto pace;

        put_u32(enc, nt);
        put_u32(fhead, (uint32_t)o);
        put_u32(fhead + 4, seq++);
        put_u32(fhead + 8, 1); /* flags: tiles */
        if (write_all(cfd, fhead, sizeof(fhead))) {
            perror("send fhead");
            return -1;
        }
        if (write_all(cfd, enc, o)) {
            perror("send payload");
            return -1;
        }

        memcpy(prev, cur, raw);
        first = 0;
        got_any = 1;
        if (!quiet) {
            fprintf(stderr, "frame %u: %u tiles, %zu B\n", seq, nt, o);
        }
        if (oneshot > 0 && seq >= (uint32_t)oneshot) break;

    pace:
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (fps > 0) {
            long long ns = (long long)(t1.tv_sec - t0.tv_sec) * 1000000000LL +
                           (t1.tv_nsec - t0.tv_nsec);
            long long frame_ns = 1000000000LL / fps;
            if (ns < frame_ns) {
                struct timespec sl;
                long long d = frame_ns - ns;
                sl.tv_sec = (time_t)(d / 1000000000LL);
                sl.tv_nsec = (long)(d % 1000000000LL);
                nanosleep(&sl, NULL);
            }
        }
    }
    return got_any ? 0 : -1;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "uso: %s [-d /dev/fb1] [-p 2331] [-f 10] [-b 0.0.0.0] [-n 1] [-q]\n"
            "  -n N  sai depois de entregar N frames (1 = screenshot)\n",
            prog);
}

int main(int argc, char **argv) {
    const char *dev = "/dev/fb1";
    const char *bindaddr = "0.0.0.0";
    int port = 2331;
    int fps = 10;
    int oneshot = 0;
    int quiet = 0;
    int ffd, w, h, stride, bpp, lfd, one = 1;
    size_t raw, enc_cap;
    int ntiles_max;
    unsigned char *cur, *prev, *enc;
    struct sockaddr_in sa;

    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d") && i + 1 < argc) dev = argv[++i];
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) bindaddr = argv[++i];
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) oneshot = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-q")) quiet = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 2; }
    }
    if (port <= 0 || port > 65535 || fps < 0 || fps > 60) {
        fprintf(stderr, "porta/fps invalidos\n");
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);
    {
        struct sigaction sa_sig;
        memset(&sa_sig, 0, sizeof(sa_sig));
        sa_sig.sa_handler = on_signal;
        sigemptyset(&sa_sig.sa_mask);
        sa_sig.sa_flags = 0; /* sem SA_RESTART: accept() volta com EINTR */
        sigaction(SIGINT, &sa_sig, NULL);
        sigaction(SIGTERM, &sa_sig, NULL);
    }

    ffd = fb_open_dev(dev, &w, &h, &stride, &bpp);
    if (ffd < 0) die("abrir fb");
    {
        struct fb_var_screeninfo vi0;
        if (ioctl(ffd, FBIOGET_VSCREENINFO, &vi0) == 0) {
            size_t ml = (size_t)stride * (size_t)vi0.yres_virtual;
            void *m = mmap(NULL, ml, PROT_READ, MAP_SHARED, ffd, 0);
            if (m != MAP_FAILED) {
                g_map = (unsigned char *)m;
                g_maplen = ml;
            }
        }
    }
    if (!g_map) {
        fprintf(stderr, "fbserve: mmap do fb falhou (%s)\n", strerror(errno));
        return 1;
    }

    raw = (size_t)w * (size_t)h * (size_t)(bpp / 8);
    ntiles_max = ((w + TILE - 1) / TILE) * ((h + TILE - 1) / TILE);
    enc_cap = raw + 4 + (size_t)ntiles_max * 4;
    cur = malloc(raw);
    prev = malloc(raw);
    enc = malloc(enc_cap);
    if (!cur || !prev || !enc) die("malloc");
    memset(prev, 0xAA, raw);

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) die("socket");
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bindaddr, &sa.sin_addr) != 1) {
        fprintf(stderr, "bind invalido: %s\n", bindaddr);
        return 2;
    }
    if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa))) die("bind");
    if (listen(lfd, 2)) die("listen");

    if (!quiet) {
        fprintf(stderr, "fbserve: %s %dx%d stride=%d bpp=%d porta=%d fps=%d tile=%d%s\n",
                dev, w, h, stride, bpp, port, fps, TILE, oneshot ? " (oneshot)" : "");
    }

    while (!g_stop) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int cfd = accept(lfd, (struct sockaddr *)&ca, &cl);
        int rc;
        if (cfd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (!quiet) {
            char ip[64];
            inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));
            fprintf(stderr, "cliente %s:%d\n", ip, ntohs(ca.sin_port));
        }
        rc = serve_client(cfd, ffd, w, h, stride, bpp, fps, oneshot, quiet,
                          cur, prev, enc, raw);
        close(cfd);
        /* -n 1: so sai depois de entregar um frame de verdade. */
        if (oneshot > 0 && rc == 0) break;
    }

    close(lfd);
    munmap(g_map, g_maplen);
    close(ffd);
    free(cur);
    free(prev);
    free(enc);
    return 0;
}
