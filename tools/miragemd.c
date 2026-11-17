/*
 * miragemd - pequeno servidor HTTP local (127.0.0.1) do gerenciador Miragem.
 *
 * Roda como root na TV. Serve a UI (arquivos estaticos) e uma API JSON que o
 * app/channel usa para: status do root, ligar/desligar o tvsh, modo
 * desenvolvedor, listar/instalar plugins.
 *
 * build:
 *   arm-linux-musleabihf-gcc -O2 -static -no-pie -o miragemd miragemd.c
 *
 * uso:
 *   miragemd [-p porta] [-d diretorio]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEF_PORT 8090
#define DEF_DIR  "/tmp/miragem/www"
#define AGENT    "/tmp/ra6"
#define SRPATCH  "/tmp/srpatch"
#define GRSYS    "/proc/sys/kernel/grsecurity"
#define LINKDB   "/nvram/system.conf.db"
#define LINKDBE  "/bin/linkdbedit"
#define ZIPAPP_SH "/tmp/miragem/zipapp_apply.sh"
#define DLNA_SH   "/tmp/miragem/dlna_apply.sh"
#define ZIPAPP_SLOT "/tmp/miragem/zipapp/slot"

static int g_port = DEF_PORT;
static char g_dir[512] = DEF_DIR;
static char g_bind[64] = "127.0.0.1";

static const char *ctype(const char *p){
    const char *e = strrchr(p, '.');
    if(!e) return "application/octet-stream";
    if(!strcmp(e,".html")||!strcmp(e,".htm")) return "text/html; charset=utf-8";
    if(!strcmp(e,".js"))  return "text/javascript; charset=utf-8";
    if(!strcmp(e,".css")) return "text/css; charset=utf-8";
    if(!strcmp(e,".json"))return "application/json; charset=utf-8";
    if(!strcmp(e,".svg")) return "image/svg+xml";
    if(!strcmp(e,".png")) return "image/png";
    if(!strcmp(e,".jpg")||!strcmp(e,".jpeg")) return "image/jpeg";
    if(!strcmp(e,".woff2")) return "font/woff2";
    if(!strcmp(e,".ico")) return "image/x-icon";
    return "application/octet-stream";
}

static int readfile(const char *path, char **out, long *len){
    int fd = open(path, O_RDONLY);
    if(fd < 0) return -1;
    struct stat st;
    if(fstat(fd, &st) < 0){ close(fd); return -1; }
    char *b = malloc((size_t)st.st_size + 1);
    if(!b){ close(fd); return -1; }
    long r = 0;
    while(r < st.st_size){
        ssize_t n = read(fd, b + r, (size_t)(st.st_size - r));
        if(n <= 0) break;
        r += n;
    }
    close(fd);
    b[r] = 0;
    *out = b; *len = r;
    return 0;
}

/* --- helpers de sistema --- */

static int tcp_open(int port){
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0) return 0;
    struct sockaddr_in a;
    memset(&a,0,sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    struct timeval tv = {0, 300000};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int ok = connect(s,(struct sockaddr*)&a,sizeof a) == 0;
    close(s);
    return ok;
}

static int run(const char *cmd){
    int st = system(cmd);
    if(st == -1) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int read_cmd(const char *cmd, char *buf, size_t n){
    FILE *f = popen(cmd, "r");
    if(!f) return -1;
    size_t r = fread(buf, 1, n - 1, f);
    buf[r] = 0;
    pclose(f);
    /* tira newlines do fim */
    while(r > 0 && (buf[r-1] == '\n' || buf[r-1] == '\r')) buf[--r] = 0;
    return (int)r;
}

static int agent_listening(int mode_bindsh){
    /* procura "bindsh 2222" / "bind 2323" em /proc */
    DIR *d = opendir("/proc");
    if(!d) return 0;
    struct dirent *e;
    const char *needle = mode_bindsh ? "bindsh" : "bind";
    int found = 0;
    while((e = readdir(d))){
        if(e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p[64]; snprintf(p,sizeof p,"/proc/%s/cmdline", e->d_name);
        int fd = open(p, O_RDONLY);
        if(fd < 0) continue;
        char b[256]; ssize_t n = read(fd, b, sizeof b - 1);
        close(fd);
        if(n <= 0) continue;
        b[n] = 0;
        for(ssize_t i = 0; i < n; i++) if(b[i] == 0) b[i] = ' ';
        if(strstr(b, needle) && strstr(b, AGENT + 5)){ found = 1; break; }
    }
    closedir(d);
    return found;
}

static void kill_agent(void){
    DIR *d = opendir("/proc");
    if(!d) return;
    struct dirent *e;
    char me[24]; snprintf(me,sizeof me,"%d",(int)getpid());
    while((e = readdir(d))){
        if(e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        if(!strcmp(e->d_name, me)) continue;
        char p[64]; snprintf(p,sizeof p,"/proc/%s/cmdline", e->d_name);
        int fd = open(p, O_RDONLY);
        if(fd < 0) continue;
        char b[256]; ssize_t n = read(fd, b, sizeof b - 1);
        close(fd);
        if(n <= 0) continue;
        b[n] = 0;
        for(ssize_t i = 0; i < n; i++) if(b[i] == 0) b[i] = ' ';
        if(strstr(b, "ra6") && strstr(b, "bindsh 2222")){
            kill(atoi(e->d_name), SIGKILL);
        }
    }
    closedir(d);
}

static void start_agent(int port){
    if(access(AGENT, X_OK) != 0) return;
    char cmd[256];
    if(port == 2222) snprintf(cmd,sizeof cmd,"%s bindsh 2222 >/dev/null 2>&1 &", AGENT);
    else             snprintf(cmd,sizeof cmd,"%s bind %d >/dev/null 2>&1 &", AGENT, port);
    (void)run(cmd);
    usleep(300000);
}

/* --- JSON --- */

static void jstr(char *dst, size_t n, const char *src){
    size_t o = 0;
    for(; src && *src && o + 2 < n; src++){
        unsigned char c = (unsigned char)*src;
        if(c == '"' || c == '\\'){ dst[o++] = '\\'; dst[o++] = (char)c; }
        else if(c < 0x20) dst[o++] = ' ';
        else dst[o++] = (char)c;
    }
    dst[o] = 0;
}

static void api_status(int c){
    int tvsh = tcp_open(2222);
    char agree[64] = "", ver[128] = "", model[128] = "", up[64] = "0";
    read_cmd(LINKDBE " " LINKDB " -g Developer/Agree 2>/dev/null", agree, sizeof agree);
    read_cmd("cat /etc/build-info 2>/dev/null", ver, sizeof ver);
    read_cmd("cat /proc/device-tree/model 2>/dev/null", model, sizeof model);
    read_cmd("cut -d. -f1 /proc/uptime 2>/dev/null", up, sizeof up);
    char ea[128], ev[256], em[256];
    jstr(ea,sizeof ea,agree); jstr(ev,sizeof ev,ver); jstr(em,sizeof em,model);
    char body[1200];
    int n = snprintf(body,sizeof body,
        "{\"root\":true,\"tvsh\":%s,\"port\":2222,"
        "\"devmode\":%s,\"devagree\":\"%s\",\"version\":\"%s\",\"model\":\"%s\","
        "\"uptime\":%ld,\"agent\":%s}",
        tvsh?"true":"false",
        (!strcmp(agree,"true"))?"true":"false", ea, ev, em,
        atol(up), agent_listening(1)?"true":"false");
    char hdr[256];
    int hl = snprintf(hdr,sizeof hdr,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nCache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", n);
    send(c,hdr,(size_t)hl,0); send(c,body,(size_t)n,0);
}

static void api_plugins(int c){
    DIR *d = opendir("/nvram/plugins");
    char body[8192]; size_t o = 0;
    o += (size_t)snprintf(body+o,sizeof body-o,"{\"plugins\":[");
    int first = 1;
    if(d){
        struct dirent *e;
        while((e = readdir(d))){
            const char *n = e->d_name;
            size_t l = strlen(n);
            if(l < 5 || strcmp(n + l - 4, ".zip")) continue;
            char p[320]; snprintf(p,sizeof p,"/nvram/plugins/%s", n);
            struct stat st; if(stat(p,&st) < 0) continue;
            char id[64]; snprintf(id,sizeof id,"%.*s",(int)(l - 4), n);
            char t[128];
            snprintf(t,sizeof t,"/nvram/plugins/%.*s.unloaded",(int)(l - 4), n);
            o += (size_t)snprintf(body+o,sizeof body-o,
                "%s{\"id\":\"%s\",\"bytes\":%ld}", first?"":",", id, (long)st.st_size);
            first = 0;
            if(o > sizeof body - 256) break;
        }
        closedir(d);
    }
    o += (size_t)snprintf(body+o,sizeof body-o,"]}");
    char hdr[256];
    int hl = snprintf(hdr,sizeof hdr,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n"
        "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
        (int)o);
    send(c,hdr,(size_t)hl,0); send(c,body,o,0);
}

/* --- superroot (grsec off, por boot) ------------------------------------ */

static const char *SR_FLAGS[] = {
    "chroot_findtask", "chroot_deny_unix", "chroot_deny_mount",
    "chroot_deny_shmat", "chroot_deny_chroot", "chroot_enforce_chdir",
    "chroot_deny_chmod", "chroot_deny_mknod", "chroot_restrict_nice",
    "chroot_caps", "chroot_deny_bad_rename", "chroot_deny_sysctl", NULL
};
static const char *SR_OPT[] = { "chroot_deny_fchdir", "chroot_deny_pivot", NULL };

static int copy_file(const char *src, const char *dst, int mode){
    int in = open(src, O_RDONLY);
    if(in < 0) return -1;
    int out = open(dst, O_WRONLY|O_CREAT|O_TRUNC, mode);
    if(out < 0){ close(in); return -1; }
    char b[8192]; ssize_t n; int rc = 0;
    while((n = read(in, b, sizeof b)) > 0){
        if(write(out, b, (size_t)n) != n){ rc = -1; break; }
    }
    close(in); close(out);
    chmod(dst, (mode_t)mode);
    return rc;
}

/* /tmp e' volatil: se o srpatch sumiu no reboot, restaura de /nvram ou
 * de /tmp/miragem (o instalador deixa uma copia em /nvram). */
static int ensure_srpatch(void){
    const char *src[] = { "/nvram/miragem/srpatch", "/tmp/miragem/srpatch", NULL };
    int i;
    if(access(SRPATCH, X_OK) == 0) return 0;
    for(i = 0; src[i]; i++)
        if(access(src[i], R_OK) == 0 && copy_file(src[i], SRPATCH, 0755) == 0) return 0;
    return -1;
}

static void send_json(int c, const char *body){
    int n = (int)strlen(body);
    char hdr[256];
    int hl = snprintf(hdr,sizeof hdr,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n"
        "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", n);
    send(c,hdr,(size_t)hl,0); send(c,body,(size_t)n,0);
}

/* --- zipapp / dlna (helpers em /tmp/miragem) ---------------------------- */

static int valid_slot(const char *s){
    size_t i;
    if(!s || !*s) return 0;
    if(strlen(s) >= 64) return 0;
    for(i = 0; s[i]; i++){
        unsigned char ch = (unsigned char)s[i];
        if(!(isalnum(ch) || ch == '.' || ch == '_' || ch == '-')) return 0;
    }
    return 1;
}

static const char *BOOL_KEYS[] = {
    "staged","applied","mounted","bound","cache","has_mod","has_ccc",
    "backup","backup_active","enabled","app_active", NULL
};

static int is_bool_key(const char *k){
    int i;
    for(i = 0; BOOL_KEYS[i]; i++) if(!strcmp(k, BOOL_KEYS[i])) return 1;
    return 0;
}

static int run_capture(const char *cmd, char *buf, size_t n){
    FILE *f;
    size_t r;
    int st;
    struct sigaction sa, old;
    /* SIGCHLD esta' em SIG_IGN no main; pclose precisa do wait. */
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, &old);
    f = popen(cmd, "r");
    if(!f){ sigaction(SIGCHLD, &old, NULL); return -1; }
    r = fread(buf, 1, n - 1, f);
    buf[r] = 0;
    st = pclose(f);
    sigaction(SIGCHLD, &old, NULL);
    if(st == -1) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Converte "chave=valor" (uma por linha) em objeto JSON. */
static void kv_to_json(const char *kv, char *out, size_t n){
    size_t o = 0;
    int first = 1;
    const char *p = kv;
    out[o++] = '{';
    while(*p && o + 16 < n){
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        const char *eq;
        while(len > 0 && p[len-1] == '\r') len--;
        eq = (const char *)memchr(p, '=', len);
        if(eq){
            size_t kl = (size_t)(eq - p), vl = len - kl - 1, k;
            if(kl > 0 && kl < 40 && vl < 900){
                char key[40], val[900];
                int ok = 1;
                memcpy(key, p, kl); key[kl] = 0;
                memcpy(val, eq + 1, vl); val[vl] = 0;
                for(k = 0; k < kl; k++)
                    if(!isalnum((unsigned char)key[k]) && key[k] != '_'){ ok = 0; break; }
                if(ok){
                    if(!first) out[o++] = ',';
                    o += (size_t)snprintf(out + o, n - o, "\"%s\":", key);
                    if(is_bool_key(key)){
                        o += (size_t)snprintf(out + o, n - o, "%s", atoi(val) ? "true" : "false");
                    } else {
                        char ev[920];
                        jstr(ev, sizeof ev, val);
                        o += (size_t)snprintf(out + o, n - o, "\"%s\"", ev);
                    }
                    first = 0;
                }
            }
        }
        p = e ? e + 1 : p + len;
    }
    out[o] = 0;
    if(o + 1 < n){ out[o++] = '}'; out[o] = 0; }
}

static void default_zip_slot(char *slot, size_t n){
    slot[0] = 0;
    (void)read_cmd("cat " ZIPAPP_SLOT " 2>/dev/null", slot, n);
    if(!valid_slot(slot)) snprintf(slot, n, "2213");
}

/* Junta um objeto JSON (ex.: "{}" ou {"a":1}) a um prefixo sem fechar. */
static void merge_json(char *out, size_t n, const char *prefix, const char *obj){
    size_t ol = strlen(obj);
    if(ol <= 2) snprintf(out, n, "%s}", prefix);
    else snprintf(out, n, "%s,%s", prefix, obj + 1);
}

static void api_zipapp_status(int c, const char *slot_in){
    char slot[64], kv[4096], js[6144], body[7168], cmd[300], pre[256];
    int rc;
    if(slot_in && valid_slot(slot_in)) snprintf(slot, sizeof slot, "%s", slot_in);
    else default_zip_slot(slot, sizeof slot);
    snprintf(cmd, sizeof cmd, "%s status %s 2>&1", ZIPAPP_SH, slot);
    rc = run_capture(cmd, kv, sizeof kv);
    kv_to_json(kv, js, sizeof js);
    snprintf(pre, sizeof pre, "{\"ok\":%s,\"helper\":%s",
        rc == 0 ? "true" : "false",
        access(ZIPAPP_SH, X_OK) == 0 ? "true" : "false");
    merge_json(body, sizeof body, pre, js);
    send_json(c, body);
}

static void api_zipapp_action(int c, const char *sub){
    char op[16] = "", slot[64] = "", kv[4096], st[6144], body[12288], pre[512];
    char cmd[300], log[1400], elog[1500];
    int rc, rc2;
    if(sscanf(sub, "%15[^/]/%63s", op, slot) != 2 || !valid_slot(slot) ||
       (strcmp(op, "apply") && strcmp(op, "restore"))){
        send_json(c, "{\"ok\":false,\"msg\":\"uso: apply|restore/<slot>\"}");
        return;
    }
    snprintf(cmd, sizeof cmd, "%s %s %s 2>&1", ZIPAPP_SH, op, slot);
    rc = run_capture(cmd, kv, sizeof kv);
    snprintf(log, sizeof log, "%s", kv);
    snprintf(cmd, sizeof cmd, "%s status %s 2>&1", ZIPAPP_SH, slot);
    rc2 = run_capture(cmd, kv, sizeof kv);
    if(rc2 != 0) rc = rc2;
    kv_to_json(kv, st, sizeof st);
    jstr(elog, sizeof elog, log);
    snprintf(pre, sizeof pre,
        "{\"ok\":%s,\"op\":\"%s\",\"msg\":\"%s %s: %s\",\"log\":\"%s\"",
        rc == 0 ? "true" : "false", op, op, slot, rc == 0 ? "ok" : "falhou",
        elog);
    merge_json(body, sizeof body, pre, st);
    send_json(c, body);
}

static void api_dlna_status(int c){
    char kv[2048], js[3072], body[4096], cmd[200], pre[256];
    int rc;
    snprintf(cmd, sizeof cmd, "%s status 2>&1", DLNA_SH);
    rc = run_capture(cmd, kv, sizeof kv);
    kv_to_json(kv, js, sizeof js);
    snprintf(pre, sizeof pre, "{\"ok\":%s,\"helper\":%s",
        rc == 0 ? "true" : "false",
        access(DLNA_SH, X_OK) == 0 ? "true" : "false");
    merge_json(body, sizeof body, pre, js);
    send_json(c, body);
}

static void api_dlna_action(int c, const char *sub){
    char kv[2048], st[3072], body[6144], cmd[220], log[900], elog[950], pre[1200];
    int rc, rc2;
    if(strcmp(sub, "apply") && strcmp(sub, "restore")){
        send_json(c, "{\"ok\":false,\"msg\":\"uso: apply|restore\"}");
        return;
    }
    snprintf(cmd, sizeof cmd, "%s %s 2>&1", DLNA_SH, sub);
    rc = run_capture(cmd, kv, sizeof kv);
    snprintf(log, sizeof log, "%s", kv);
    snprintf(cmd, sizeof cmd, "%s status 2>&1", DLNA_SH);
    rc2 = run_capture(cmd, kv, sizeof kv);
    if(rc2 != 0) rc = rc2;
    kv_to_json(kv, st, sizeof st);
    jstr(elog, sizeof elog, log);
    snprintf(pre, sizeof pre,
        "{\"ok\":%s,\"op\":\"%s\",\"msg\":\"dlna %s: %s\",\"log\":\"%s\"",
        rc == 0 ? "true" : "false", sub, sub, rc == 0 ? "ok" : "falhou",
        elog);
    merge_json(body, sizeof body, pre, st);
    send_json(c, body);
}

static int sr_get(const char *name, char *buf, size_t n){
    char cmd[256];
    snprintf(cmd,sizeof cmd,"cat %s/%s 2>/dev/null", GRSYS, name);
    buf[0] = 0;
    return read_cmd(cmd, buf, n) < 0 ? -1 : 0;
}

static int sr_set(const char *name, int val){
    char cmd[256];
    snprintf(cmd,sizeof cmd,"echo %d > %s/%s 2>/dev/null", val, GRSYS, name);
    return run(cmd);
}

static int sr_pids(void){
    char b[32];
    if(read_cmd("ls -d /proc/[0-9]* 2>/dev/null | wc -l", b, sizeof b) < 0) return -1;
    return atoi(b);
}

static void sr_status_fields(char *body, size_t n){
    char ft[32] = "", lk[32] = "", p1[64] = "";
    sr_get("chroot_findtask", ft, sizeof ft);
    sr_get("grsec_lock", lk, sizeof lk);
    read_cmd("cat /proc/1/comm 2>/dev/null", p1, sizeof p1);
    int pids = sr_pids();
    int active = !strcmp(ft, "0");
    char ep1[128]; jstr(ep1, sizeof ep1, p1);
    snprintf(body, n,
        "\"active\":%s,\"findtask\":%s,\"lock\":%s,\"pids\":%d,\"pid1\":\"%s\","
        "\"binary\":%s,\"path\":\"" SRPATCH "\"",
        active ? "true" : "false",
        ft[0] ? ft : "null",
        lk[0] ? lk : "null",
        pids, ep1,
        access(SRPATCH, X_OK) == 0 ? "true" : "false");
}

static void api_superroot(int c){
    char st[512], body[768];
    sr_status_fields(st, sizeof st);
    snprintf(body, sizeof body, "{\"ok\":true,%s}", st);
    send_json(c, body);
}

static int sr_ensure(const char *name, int tries){
    char cmd[256], v[32];
    int i;
    for(i = 0; i < tries; i++){
        if(sr_get(name, v, sizeof v) == 0 && !strcmp(v, "0")) return 1;
        snprintf(cmd, sizeof cmd, SRPATCH " step %s --no-prim", name);
        (void)run(cmd);
    }
    return sr_get(name, v, sizeof v) == 0 && !strcmp(v, "0");
}

static void api_superroot_action(int c, const char *sub, const char *lockaddr){
    int ok = 1, i;
    char log[1024];
    log[0] = 0;
    const char *mode = "?";

    if(access(SRPATCH, X_OK) != 0){
        char st[512], body[768];
        sr_status_fields(st, sizeof st);
        snprintf(body, sizeof body,
            "{\"ok\":false,\"mode\":\"none\",\"msg\":\"" SRPATCH " ausente ou sem +x\",%s}", st);
        send_json(c, body);
        return;
    }

    if(!strcmp(sub, "apply/lock")){
        mode = "lock";
        char v[32];
        /* 1) lock (1 corrida) */
        if(sr_get("grsec_lock", v, sizeof v) != 0 || strcmp(v, "0")){
            char lc[128];
            snprintf(lc, sizeof lc, SRPATCH " w %s 0", lockaddr);
            (void)run(lc); (void)run(lc);
            usleep(50000);
            if(sr_get("grsec_lock", v, sizeof v) != 0 || strcmp(v, "0"))
                { ok = 0; strncat(log, "lock falhou; ", sizeof log - strlen(log) - 1); }
        }
        /* 2) caps e gate de sysctl sao pre-requisito para o echo (1 corrida cada) */
        if(ok && !sr_ensure("chroot_caps", 4))
            { ok = 0; strncat(log, "caps falhou; ", sizeof log - strlen(log) - 1); }
        if(ok && !sr_ensure("chroot_deny_sysctl", 4))
            { ok = 0; strncat(log, "sysctl gate falhou; ", sizeof log - strlen(log) - 1); }
        /* 3) o resto por echo (sysctl); segue mesmo se uma falhar */
        for(i = 0; SR_FLAGS[i]; i++){
            if(!strcmp(SR_FLAGS[i], "chroot_caps") || !strcmp(SR_FLAGS[i], "chroot_deny_sysctl")) continue;
            if(sr_get(SR_FLAGS[i], v, sizeof v) == 0 && !strcmp(v, "0")) continue;
            (void)sr_set(SR_FLAGS[i], 0);
            usleep(50000);
            if(sr_get(SR_FLAGS[i], v, sizeof v) != 0 || strcmp(v, "0")){
                ok = 0;
                strncat(log, SR_FLAGS[i], sizeof log - strlen(log) - 1);
                strncat(log, " ", sizeof log - strlen(log) - 1);
            }
        }
        for(i = 0; SR_OPT[i]; i++) (void)sr_set(SR_OPT[i], 0);
    } else if(!strcmp(sub, "apply/races")){
        mode = "races";
        for(i = 0; SR_FLAGS[i]; i++){
            char v[32];
            if(sr_get(SR_FLAGS[i], v, sizeof v) == 0 && !strcmp(v, "0")) continue;
            if(!sr_ensure(SR_FLAGS[i], 4)){
                ok = 0;
                strncat(log, SR_FLAGS[i], sizeof log - strlen(log) - 1);
                strncat(log, " ", sizeof log - strlen(log) - 1);
            }
        }
        for(i = 0; SR_OPT[i]; i++) (void)sr_ensure(SR_OPT[i], 2);
    } else if(!strcmp(sub, "restore")){
        mode = "restore";
        /* ordem importa: caps e sysctl por ultimo, senao os echos seguintes falham */
        for(i = 0; SR_FLAGS[i]; i++){
            if(!strcmp(SR_FLAGS[i], "chroot_caps") || !strcmp(SR_FLAGS[i], "chroot_deny_sysctl")) continue;
            (void)sr_set(SR_FLAGS[i], 1);
        }
        for(i = 0; SR_OPT[i]; i++) (void)sr_set(SR_OPT[i], 1);
        (void)sr_set("chroot_deny_sysctl", 1);
        (void)sr_set("grsec_lock", 1);
        (void)sr_set("chroot_caps", 1);   /* caps por ultimo: bloqueia os echos seguintes */
    } else {
        char st[512], body[768];
        sr_status_fields(st, sizeof st);
        snprintf(body, sizeof body, "{\"ok\":false,\"mode\":\"unknown\",%s}", st);
        send_json(c, body);
        return;
    }

    char st[512], body[1400], elog[1100];
    sr_status_fields(st, sizeof st);
    jstr(elog, sizeof elog, log);
    snprintf(body, sizeof body, "{\"ok\":%s,\"mode\":\"%s\",\"msg\":\"%s\",%s}",
        ok ? "true" : "false", mode, elog, st);
    send_json(c, body);
}

static void api_action(int c, const char *what){
    char body[512];
    if(!strcmp(what, "tvsh/start")){
        int up = tcp_open(2222);
        if(!up) start_agent(2222);
        int now = tcp_open(2222);
        snprintf(body,sizeof body,
            "{\"ok\":%s,\"tvsh\":%s,\"msg\":\"%s\"}", now?"true":"false", now?"true":"false",
            now ? "shell ativo na 2222" : "nao subiu (agente ausente?)");
    } else if(!strcmp(what, "tvsh/stop")){
        kill_agent();
        usleep(250000);
        int up = tcp_open(2222);
        snprintf(body,sizeof body,"{\"ok\":true,\"tvsh\":%s,\"msg\":\"%s\"}",
            up?"true":"false", up ? "ainda ativo" : "shell encerrado");
    } else if(!strcmp(what, "devmode")){
        int a = run(LINKDBE " " LINKDB " -s Developer/Agree true");
        int b = run(LINKDBE " " LINKDB " -s Developer/Installer true");
        snprintf(body,sizeof body,
            "{\"ok\":%s,\"msg\":\"%s\"}", (a==0&&b==0)?"true":"false",
            (a==0&&b==0) ? "flags gravadas; reinicie a TV para o instalador subir"
                         : "falha ao gravar as flags");
    } else if(!strcmp(what, "ping")){
        snprintf(body,sizeof body,"{\"ok\":true,\"msg\":\"pong\"}");
    } else {
        snprintf(body,sizeof body,"{\"ok\":false,\"msg\":\"acao desconhecida\"}");
    }
    int n = (int)strlen(body);
    char hdr[256];
    int hl = snprintf(hdr,sizeof hdr,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n"
        "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", n);
    send(c,hdr,(size_t)hl,0); send(c,body,(size_t)n,0);
}

static void send_file(int c, const char *path){
    char *buf = NULL; long len = 0;
    if(readfile(path, &buf, &len) < 0){
        const char *m = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nnot found";
        send(c,m,strlen(m),0);
        return;
    }
    char hdr[256];
    int hl = snprintf(hdr,sizeof hdr,
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n"
        "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
        ctype(path), len);
    send(c,hdr,(size_t)hl,0);
    long o = 0;
    while(o < len){ ssize_t w = send(c, buf + o, (size_t)(len - o), 0); if(w <= 0) break; o += w; }
    free(buf);
}

static void handle(int c){
    char req[4096];
    ssize_t n = recv(c, req, sizeof req - 1, 0);
    if(n <= 0){ close(c); return; }
    req[n] = 0;
    char method[8] = "", target[1024] = "";
    sscanf(req, "%7s %1023s", method, target);
    char *q = strchr(target, '?'); if(q) *q = 0;

    /* API */
    if(!strncmp(target, "/api/", 5)){
        const char *a = target + 5;
        if(!strcmp(a, "status"))  { api_status(c); close(c); return; }
        if(!strcmp(a, "plugins")) { api_plugins(c); close(c); return; }
        if(!strcmp(a, "info"))    { api_status(c); close(c); return; }
        if(!strcmp(a, "superroot")) { api_superroot(c); close(c); return; }
        if(!strcmp(a, "superroot/install")) { (void)ensure_srpatch(); api_superroot(c); close(c); return; }
        if(!strncmp(a, "superroot/", 10)) {
            char sub[64] = "", addr[16] = "0x8091e574";
            const char *p = a + 10, *last = strrchr(p, '/');
            const char *x = NULL;
            if(last && last[1] == '0' && (last[2] == 'x' || last[2] == 'X')) x = last + 1;
            size_t n = x ? (size_t)(last - p) : strlen(p);
            if(n >= sizeof sub) n = sizeof sub - 1;
            memcpy(sub, p, n); sub[n] = 0;
            if(x){
                size_t m = strlen(x);
                int valid = (m > 2 && m < sizeof addr);
                size_t k;
                for(k = 2; valid && k < m; k++)
                    if(!strchr("0123456789abcdefABCDEF", x[k])) valid = 0;
                if(valid) snprintf(addr, sizeof addr, "%s", x);
            }
            api_superroot_action(c, sub, addr);
            close(c); return;
        }
        if(!strcmp(a, "zipapp") || !strcmp(a, "zipapp/status")) {
            api_zipapp_status(c, NULL); close(c); return;
        }
        if(!strncmp(a, "zipapp/status/", 14)) {
            api_zipapp_status(c, a + 14); close(c); return;
        }
        if(!strncmp(a, "zipapp/", 7)) {
            api_zipapp_action(c, a + 7); close(c); return;
        }
        if(!strcmp(a, "dlna")) {
            api_dlna_status(c); close(c); return;
        }
        if(!strncmp(a, "dlna/", 5)) {
            api_dlna_action(c, a + 5); close(c); return;
        }
        if(!strcmp(method, "OPTIONS")){
            const char *r = "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\n"
                            "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
                            "Access-Control-Allow-Headers: Content-Type\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
            send(c,r,strlen(r),0); close(c); return;
        }
        api_action(c, a);
        close(c); return;
    }

    if(!strcmp(method, "GET")){
        char path[1024];
        if(!strcmp(target, "/") || !strcmp(target, "")) snprintf(path,sizeof path,"%s/index.html", g_dir);
        else snprintf(path,sizeof path,"%s%s", g_dir, target);
        if(strstr(path, "..")){ close(c); return; }
        send_file(c, path);
    } else {
        const char *r = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(c,r,strlen(r),0);
    }
    close(c);
}

int main(int argc, char **argv){
    int i;
    for(i = 1; i < argc; i++){
        if(!strcmp(argv[i], "-p") && i + 1 < argc) g_port = atoi(argv[++i]);
        else if(!strcmp(argv[i], "-d") && i + 1 < argc){ snprintf(g_dir,sizeof g_dir,"%s",argv[++i]); }
        else if(!strcmp(argv[i], "-a") && i + 1 < argc){ snprintf(g_bind,sizeof g_bind,"%s",argv[++i]); }
    }
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0){ perror("socket"); return 1; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a,0,sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)g_port);
    a.sin_addr.s_addr = inet_addr(g_bind);
    if(bind(s,(struct sockaddr*)&a,sizeof a) < 0){ perror("bind"); return 1; }
    if(listen(s, 16) < 0){ perror("listen"); return 1; }

    (void)ensure_srpatch();
    fprintf(stderr, "miragemd: http://%s:%d/ (dir %s)\n", g_bind, g_port, g_dir);
    for(;;){
        int c = accept(s, 0, 0);
        if(c < 0){ if(errno == EINTR) continue; usleep(50000); continue; }
        if(fork() == 0){ close(s); handle(c); _exit(0); }
        close(c);
        while(waitpid(-1, 0, WNOHANG) > 0) ;
    }
    return 0;
}
