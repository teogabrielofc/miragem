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
#define LINKDB   "/nvram/system.conf.db"
#define LINKDBE  "/bin/linkdbedit"

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
