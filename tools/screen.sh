#!/usr/bin/env bash
# screen.sh - Miragem: espelha a tela da TV (fb1) no PC, tipo um "scrcpy".
#
#   ROKU_HOST=192.168.1.100 bash tools/screen.sh            # janela ao vivo
#   ROKU_HOST=192.168.1.100 bash tools/screen.sh shot tela.png
#   ROKU_HOST=192.168.1.100 bash tools/screen.sh stop
#
# Requer shell root na TV (porta 2222) e o agente de upload (2323); ambos
# sobem com o instalador/gerenciador (tools/install_manager.sh). O binario
# fbserve.arm e enviado a cada uso para /tmp/fbserve (some no reboot).
#
# A janela usa ffplay (se existir) ou Tk; --tk forca Tk, --scale N amplia.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TV="${ROKU_HOST:?defina ROKU_HOST com o IP da TV}"
PORT="${SCREEN_PORT:-2331}"
FPS="${SCREEN_FPS:-10}"
BIN="$HERE/fbserve.arm"
VIEWER="$HERE/tvscreen.py"
CMD="${1:-start}"
[ "$#" -gt 0 ] && shift

die() { echo "ERRO: $*" >&2; exit 1; }

have_port() {
    timeout 2 bash -c "echo > /dev/tcp/$TV/$1" 2>/dev/null
}

push_bin() {
    have_port 2222 || die "shell root fechado em $TV:2222 (rode o instalador/root primeiro)"
    if ! have_port 2323; then
        python3 "$HERE/tvcmd.py" '/tmp/ra6 bind 2323 >/dev/null 2>&1 &' >/dev/null 2>&1 || true
        sleep 1
        have_port 2323 || die "upload (2323) indisponivel: rode tools/install_manager.sh"
    fi
    [ -f "$BIN" ] || die "$BIN nao existe (compile com a toolchain ARM; veja docs/SCREEN.md)"
    ROKU_HOST="$TV" python3 "$HERE/tvput.py" /tmp/fbserve "$BIN" >/dev/null || die "envio do fbserve"
    python3 "$HERE/tvcmd.py" 'chmod 755 /tmp/fbserve' >/dev/null 2>&1 || true
}

stop_tv() {
    python3 "$HERE/tvcmd.py" 'killall fbserve 2>/dev/null; sleep 0.2; killall -9 fbserve 2>/dev/null; true' >/dev/null 2>&1 || true
    sleep 0.2
}

start_tv() {  # $1 = flags extras (ex.: "-n 1")
    stop_tv
    python3 "$HERE/tvcmd.py" ": > /tmp/fbserve.log; nohup /tmp/fbserve -d /dev/fb1 -p $PORT -f $FPS $1 >>/tmp/fbserve.log 2>&1 & sleep 0.5; echo ok" >/dev/null 2>&1 || true
    # sem checar a porta com TCP: um connect vazio conta como "cliente" e,
    # no modo -n 1, o servidor sairia antes do viewer. O viewer tenta sozinho.
    sleep 0.8
}

case "$CMD" in
    start)
        push_bin
        start_tv ""
        echo "tvscreen: abrindo janela (Ctrl-C encerra; audio nao e transmitido)"
        trap 'stop_tv' EXIT INT TERM
        ROKU_HOST="$TV" SCREEN_PORT="$PORT" python3 "$VIEWER" --host "$TV" --port "$PORT" "$@"
        ;;
    shot)
        OUT="${1:-tela.png}"
        push_bin
        start_tv "-n 1"
        ROKU_HOST="$TV" SCREEN_PORT="$PORT" python3 "$VIEWER" --host "$TV" --port "$PORT" --shot "$OUT"
        RC=$?
        stop_tv
        exit "$RC"
        ;;
    stop)
        stop_tv
        echo "fbserve encerrado na TV"
        ;;
    *)
        echo "uso: ROKU_HOST=IP $0 {start [--scale N] [--tk] | shot <png> | stop}" >&2
        exit 2
        ;;
esac
