/*
 * ecp - cliente HTTP minimo para o ECP/API local da TV (sem curl/wget).
 *
 * uso:
 *   ecp [GET|POST] <path> [porta] [host]
 *
 *   ecp POST /launch/2213                 # ECP, 127.0.0.1:8060
 *   ecp GET /query/active-app
 *   ecp GET /api/zipapp 8090              # testa o miragemd
 *
 * Imprime o corpo da resposta em stdout e a linha de status em stderr.
 * Sai com 0 para 2xx, 1 para o resto, 2 para erro de uso/rede.
 *
 * build (ARMv7 estatico):
 *   arm-linux-musleabihf-gcc -O2 -static -s -o ecp.arm ecp.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int drain_body(const char *buf, size_t n){
    const char *p = NULL;
    size_t i;
    for(i = 0; i + 3 < n; i++){
        if(buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n'){
            p = buf + i + 4;
            break;
        }
    }
    if(!p){
        for(i = 0; i + 1 < n; i++)
            if(buf[i] == '\n' && buf[i+1] == '\n'){ p = buf + i + 2; break; }
    }
    if(!p) p = buf; /* sem cabecalho: imprime tudo */
    fwrite(p, 1, n - (size_t)(p - buf), stdout);
    return 0;
}

int main(int argc, char **argv){
    const char *method = "GET", *path = "/";
    int port = 8060;
    const char *host = "127.0.0.1";

    if(argc > 1) method = argv[1];
    if(argc > 2) path = argv[2];
    if(argc > 3) port = atoi(argv[3]);
    if(argc > 4) host = argv[4];
    if(strcasecmp(method, "GET") && strcasecmp(method, "POST")){
        fprintf(stderr, "uso: %s [GET|POST] <path> [porta] [host]\n", argv[0]);
        return 2;
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0){ perror("socket"); return 2; }
    struct timeval tv = {60, 0};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    if(inet_aton(host, &a.sin_addr) == 0){ fprintf(stderr, "host invalido: %s\n", host); close(s); return 2; }
    if(connect(s, (struct sockaddr *)&a, sizeof a) < 0){
        fprintf(stderr, "connect %s:%d: %s\n", host, port, strerror(errno));
        close(s);
        return 2;
    }

    char req[1024];
    int rl = snprintf(req, sizeof req,
        "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: miragem-ecp\r\n"
        "Content-Length: 0\r\nConnection: close\r\n\r\n",
        method, path, host);
    if(send(s, req, (size_t)rl, 0) != rl){ perror("send"); close(s); return 2; }

    char buf[262144];
    size_t n = 0;
    for(;;){
        ssize_t r = recv(s, buf + n, sizeof buf - 1 - n, 0);
        if(r <= 0) break;
        n += (size_t)r;
        if(n >= sizeof buf - 1) break;
    }
    close(s);
    buf[n] = 0;
    if(n == 0){ fprintf(stderr, "sem resposta\n"); return 2; }

    int code = 0;
    if(sscanf(buf, "HTTP/%*s %d", &code) != 1){
        fprintf(stderr, "resposta invalida\n");
        return 2;
    }
    char line[128];
    const char *e = strchr(buf, '\n');
    size_t ll = e ? (size_t)(e - buf) : sizeof line - 1;
    if(ll >= sizeof line) ll = sizeof line - 1;
    memcpy(line, buf, ll); line[ll] = 0;
    fprintf(stderr, "%s\n", line);

    drain_body(buf, n);
    return (code >= 200 && code < 300) ? 0 : 1;
}
