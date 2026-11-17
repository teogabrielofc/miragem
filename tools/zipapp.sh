#!/usr/bin/env bash
# zipapp.sh - injeta um app BrightScript (ZIP/FUSE) num slot SDK ja lancavel,
# por sessao, sem flash/reboot e sem tocar no pacote assinado.
#
#   ROKU_HOST=192.168.1.100 bash tools/zipapp.sh push 2213 tools/zipapp_sample \
#       --beacon "http://192.168.1.2:8901/zipapp/beacon?slot=2213"
#   ROKU_HOST=192.168.1.100 bash tools/zipapp.sh apply 2213
#   ROKU_HOST=192.168.1.100 bash tools/zipapp.sh status 2213
#   ROKU_HOST=192.168.1.100 bash tools/zipapp.sh restore 2213
#
#   ROKU_HOST=... bash tools/zipapp.sh dlna status|apply|restore
#
# Como funciona (provado em 2026-11-16, Roku OS 15.0.4.5537):
#   1. o ZIP <dir|zip> e' sobreposto no pacote ORIGINAL do slot (manifest
#      original preservado -> token/permissoes do slot);
#   2. o ZIP vai para /tmp/miragem/zipapp/<slot>.zip (tvput na 2323);
#   3. o helper na TV monta o ZIP pelo ZipMountServer (mqfd, mqueue fd 4/5),
#      faz bind sobre /tmp/plugin/<dir>/pkg:, apaga mod_cache/<slot>.{mod,ccc},
#      mata SO o SdkLauncher do app e relanca por ECP /launch/<slot>.
#
# O restore remove bind/mount, devolve o cache original (byte a byte) e
# relanca o app stock. Nada e' escrito em /plugins, /nvram, rootfs ou flash.
# Precisa de root (uid 0) na 2222 e superroot (mount) ativo.
set -u

HOST="${ROKU_HOST:?defina ROKU_HOST com o IP da TV}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
TVCMD=(python3 "$HERE/tvcmd.py")
TVPUT=(python3 "$HERE/tvput.py")
TVGET=(python3 "$HERE/tvget.py")
export ROKU_HOST="$HOST"

MQFD="$HERE/mqfd.arm"
ECP="$HERE/ecp.arm"
ZSH="$HERE/zipapp_apply.sh"
DSH="$HERE/dlna_apply.sh"

die() { echo "zipapp: $*" >&2; exit 1; }
info() { echo "== $*"; }
tv() { "${TVCMD[@]}" "$1" | tr -d '\r'; }
PUSH_TMP=""
trap 'rm -rf "$PUSH_TMP"' EXIT

port_open() { # $1=host $2=port
    python3 - "$1" "$2" <<'PY'
import socket, sys
s = socket.socket(); s.settimeout(0.7)
sys.exit(0 if s.connect_ex((sys.argv[1], int(sys.argv[2]))) == 0 else 1)
PY
}

pc_ip() {
    local ip
    ip="$(ip -4 route get "$HOST" 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p' | head -1)"
    [ -n "$ip" ] || ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
    printf '%s' "$ip"
}

relay_running() {
    local pid
    [ -f /tmp/miragem-relay.pid ] || return 1
    pid="$(cat /tmp/miragem-relay.pid 2>/dev/null)"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

relay_health() { # $1=host $2=port
    python3 - "$1" "$2" <<'PY'
import sys, urllib.request
try:
    with urllib.request.urlopen("http://%s:%s/health" % (sys.argv[1], sys.argv[2]), timeout=2) as r:
        sys.exit(0 if r.read().strip() == b"ok" else 1)
except Exception:
    sys.exit(1)
PY
}

ensure_relay() { # sobe o relay do PC (ECP recusa loopback na TV) e grava o IP
    local ip="${1:-}"
    [ -n "$ip" ] || ip="$(pc_ip)"
    [ -n "$ip" ] || { echo "zipapp: aviso: IP do PC nao detectado; relay nao configurado" >&2; return 1; }
    if ! relay_health "$ip" 8092; then
        if ! relay_running; then
            if port_open 127.0.0.1 8092; then
                echo "zipapp: aviso: porta 8092 ocupada por outro processo (nao e' o relay)" >&2
                return 1
            fi
            nohup python3 "$HERE/ecp_relay.py" 8092 >/tmp/miragem-relay.log 2>&1 &
            echo $! >/tmp/miragem-relay.pid
            sleep 1
        fi
    fi
    if ! relay_health "$ip" 8092; then
        echo "zipapp: aviso: relay nao respondeu em $ip:8092 (veja /tmp/miragem-relay.log)" >&2
        return 1
    fi
    printf '%s:8092\n' "$ip" >/tmp/miragem-relay.addr
    "${TVPUT[@]}" /tmp/miragem/ecp_relay /tmp/miragem-relay.addr >/dev/null || return 1
    echo "zipapp: relay ECP $ip:8092 -> $HOST:8060"
    return 0
}

ensure_2323() {
    if port_open "$HOST" 2323; then return 0; fi
    info "subindo agente 2323 (precisa de root na 2222)"
    tv "/tmp/ra6 bind 2323 >/dev/null 2>&1 &" >/dev/null 2>&1 || true
    sleep 1
    port_open "$HOST" 2323 || die "porta 2323 fechada (root ativo? /tmp/ra6 existe?)"
}

valid_slot() {
    case "$1" in ''|*[!A-Za-z0-9._-]*) return 1;; esac
    return 0
}

# A saida do tvcmd inclui o eco do comando; use um marcador de linha inteira.
remote_yes() { tv "$1 && echo ZIPAPP_YES || echo ZIPAPP_NO" | grep -qx 'ZIPAPP_YES'; }

slot_dir_remote() { # $1 = slot
    local slot="$1" out f
    out="$(tv "awk -F'\"' -v id='$slot' '\$2==id{print \$4; exit}' /tmp/ipc/plugins/repository.data 2>/dev/null" | tail -1)"
    if [ -z "$out" ]; then
        f="$(tv "grep -l '\"_id\":\"$slot\"' /tmp/ipc/plugins/*.attr 2>/dev/null | head -1" | tail -1)"
        if [ -n "$f" ]; then
            out="$(tv "sed -n 's/.*\"_install_path\":\"\\([^\"]*\\)\".*/\\1/p' '$f' | head -1" | tail -1)"
        fi
    fi
    printf '%s' "$out"
}

ensure_helpers() {
    [ -f "$MQFD" ] || die "tools/mqfd.arm ausente (compile: arm-linux-musleabihf-gcc -O2 -static -s -o tools/mqfd.arm tools/mqfd.c)"
    [ -f "$ECP" ]  || die "tools/ecp.arm ausente (compile: arm-linux-musleabihf-gcc -O2 -static -s -o tools/ecp.arm tools/ecp.c)"
    ensure_2323
    tv "mkdir -p /tmp/miragem/zipapp/backup /tmp/miragem/dlna" >/dev/null
    ensure_relay || true
    "${TVPUT[@]}" /tmp/miragem/mqfd "$MQFD" >/dev/null || die "envio do mqfd falhou"
    "${TVPUT[@]}" /tmp/miragem/ecp "$ECP" >/dev/null || die "envio do ecp falhou"
    "${TVPUT[@]}" /tmp/miragem/zipapp_apply.sh "$ZSH" >/dev/null || die "envio do zipapp_apply.sh falhou"
    "${TVPUT[@]}" /tmp/miragem/dlna_apply.sh "$DSH" >/dev/null || die "envio do dlna_apply.sh falhou"
    tv "chmod 755 /tmp/miragem/mqfd /tmp/miragem/ecp /tmp/miragem/zipapp_apply.sh /tmp/miragem/dlna_apply.sh" >/dev/null
}

fetch_base() { # $1=slot $2=destino (dir)
    local slot="$1" dst="$2" tmp; tmp="$(dirname "$dst")"
    mkdir -p "$dst"
    if remote_yes "[ -f /plugins/$slot.zip ]"; then
        info "lendo pacote base /plugins/$slot.zip"
        "${TVGET[@]}" "/plugins/$slot.zip" "$tmp/base.zip" >/dev/null || return 1
        unzip -q "$tmp/base.zip" -d "$dst" || return 1
        return 0
    fi
    local dir; dir="$(slot_dir_remote "$slot")"
    [ -n "$dir" ] || return 1
    info "lendo pacote base instalado $dir/pkg: (tar)"
    tv "cd \"$dir/pkg:\" && tar -cf /tmp/miragem/zipapp/base-$slot.tar ." >/dev/null || return 1
    "${TVGET[@]}" "/tmp/miragem/zipapp/base-$slot.tar" "$tmp/base.tar" >/dev/null || return 1
    tv "rm -f /tmp/miragem/zipapp/base-$slot.tar" >/dev/null
    tar -xf "$tmp/base.tar" -C "$dst" || return 1
}

cmd_push() {
    local slot="${1:-}" src="${2:-}" beacon="${ZIPAPP_BEACON:-}" marker="" manifest=""
    [ -n "$slot" ] && [ -n "$src" ] || die "uso: push <slot> <dir|zip> [--beacon URL] [--marker TXT] [--manifest FILE]"
    valid_slot "$slot" || die "slot invalido"
    shift 2
    while [ $# -gt 0 ]; do
        case "$1" in
            --beacon) beacon="${2:-}"; shift 2;;
            --marker) marker="${2:-}"; shift 2;;
            --manifest) manifest="${2:-}"; shift 2;;
            *) die "opcao desconhecida: $1";;
        esac
    done
    [ -e "$src" ] || die "origem nao existe: $src"
    [ -f "$MQFD" ] || die "tools/mqfd.arm ausente"
    [ -f "$ECP" ]  || die "tools/ecp.arm ausente"
    command -v zip >/dev/null || die "zip nao instalado no PC"
    [ -n "$marker" ] || marker="ZIPAPP_OK_${slot}_$(date -u +%Y%m%d-%H%M%S)"

    local tmp; tmp="$(mktemp -d "${TMPDIR:-/tmp}/miragem-zipapp.XXXXXX")"
    PUSH_TMP="$tmp"
    local base="$tmp/base"
    fetch_base "$slot" "$base" || die "nao consegui ler o pacote base do slot $slot (so ZIP/FUSE)"
    chmod -R u+rwX "$base" 2>/dev/null || true

    local orig_manifest="$tmp/manifest.orig"
    if [ -f "$base/manifest" ]; then cp -p "$base/manifest" "$orig_manifest"; fi

    info "sobrepondo $src"
    if [ -d "$src" ]; then
        cp -a "$src/." "$base/"
    else
        local ov="$tmp/ov"; mkdir -p "$ov"
        unzip -q "$src" -d "$ov" || die "unzip da origem falhou"
        rm -f "$ov/manifest"
        cp -a "$ov/." "$base/"
    fi

    if [ -n "$manifest" ]; then
        [ -f "$manifest" ] || die "--manifest nao existe: $manifest"
        cp -p "$manifest" "$base/manifest"
        info "manifest do slot substituido por $manifest"
    elif [ -f "$orig_manifest" ]; then
        cp -p "$orig_manifest" "$base/manifest"
        info "manifest original do slot preservado (token/perms)"
    else
        die "slot sem manifest (pacote base sem manifest; use --manifest)"
    fi

    if ! grep -q '^channel_token=' "$base/manifest" 2>/dev/null; then
        echo "zipapp: aviso: manifest sem channel_token (permissoes podem cair)" >&2
    fi

    # sentinela usado pelo helper para saber que o bind e' nosso
    printf 'slot=%s\nmarker=%s\nbuilt=%s\n' "$slot" "$marker" "$(date -u +%FT%TZ)" >"$base/.miragem_zipapp"

    # troca placeholders do sample (ou de qualquer overlay que os use)
    python3 - "$base" "$marker" "$beacon" <<'PY'
import os, sys
root, marker, beacon = sys.argv[1], sys.argv[2], sys.argv[3]
for dp, _, fs in os.walk(root):
    for f in fs:
        p = os.path.join(dp, f)
        try:
            d = open(p, 'rb').read()
        except OSError:
            continue
        if b'__ZIPAPP_MARKER__' in d or b'__ZIPAPP_BEACON__' in d:
            d = d.replace(b'__ZIPAPP_MARKER__', marker.encode())
            d = d.replace(b'__ZIPAPP_BEACON__', beacon.encode())
            open(p, 'wb').write(d)
PY

    ( cd "$base" && zip -qrX "$tmp/$slot.zip" . ) || die "zip falhou"
    info "ZIP: $(wc -c < "$tmp/$slot.zip") B  md5 $(md5sum "$tmp/$slot.zip" | cut -d' ' -f1)"

    ensure_helpers
    "${TVPUT[@]}" "/tmp/miragem/zipapp/$slot.zip" "$tmp/$slot.zip" >/dev/null || die "envio do ZIP falhou"
    printf '%s\n' "$slot" >"$tmp/slot.txt"
    printf '%s\n' "$marker" >"$tmp/marker.txt"
    "${TVPUT[@]}" /tmp/miragem/zipapp/slot "$tmp/slot.txt" >/dev/null || die "envio do slot falhou"
    "${TVPUT[@]}" "/tmp/miragem/zipapp/marker-$slot" "$tmp/marker.txt" >/dev/null || die "envio do marker falhou"

    local remote_md5="" local_md5 i
    local_md5="$(md5sum "$tmp/$slot.zip" | awk '{print $1}')"
    tv "sync" >/dev/null 2>&1 || true
    for i in 1 2 3 4 5; do
        remote_md5="$(tv "md5sum /tmp/miragem/zipapp/$slot.zip" | awk 'length($1)==32 && $1 ~ /^[0-9a-f]+$/ {print $1}' | tail -1)"
        [ "$remote_md5" = "$local_md5" ] && break
        sleep 1
    done
    [ "$remote_md5" = "$local_md5" ] || die "md5 remoto != local ($remote_md5 != $local_md5)"

    info "preparado: slot $slot -> /tmp/miragem/zipapp/$slot.zip (md5 ok)"
    echo "   beacon: ${beacon:-(nenhum)}"
    echo "   marker: $marker"
    echo "   aplicar: ROKU_HOST=$HOST bash $0 apply $slot"
}

cmd_tv_helper() { # $1=apply|restore|status $2=slot
    local op="$1" slot="$2"
    valid_slot "$slot" || die "slot invalido"
    if [ "$op" != status ]; then ensure_helpers; fi
    if ! remote_yes "[ -x /tmp/miragem/zipapp_apply.sh ]"; then
        ensure_helpers
    fi
    info "zipapp $op $slot"
    local out rc
    out="$(tv "sh /tmp/miragem/zipapp_apply.sh $op $slot; echo \"helper_rc=\$?\"")"
    printf '%s\n' "$out"
    rc="$(printf '%s\n' "$out" | sed -n 's/^helper_rc=//p' | tail -1)"
    [ "$rc" = "0" ]
}

cmd_dlna() {
    local op="${1:-status}"
    case "$op" in status|apply|restore) :;; *) die "uso: dlna status|apply|restore";; esac
    if [ "$op" != status ]; then ensure_helpers; fi
    if ! remote_yes "[ -x /tmp/miragem/dlna_apply.sh ]"; then
        ensure_helpers
    fi
    info "dlna $op"
    local out rc
    out="$(tv "sh /tmp/miragem/dlna_apply.sh $op; echo \"helper_rc=\$?\"")"
    printf '%s\n' "$out"
    rc="$(printf '%s\n' "$out" | sed -n 's/^helper_rc=//p' | tail -1)"
    [ "$rc" = "0" ]
}

MODE="${1:-}"
case "$MODE" in
    push)    shift; cmd_push "$@";;
    apply|restore|status) shift; cmd_tv_helper "$MODE" "${1:-}";;
    dlna)    shift; cmd_dlna "${1:-status}";;
    relay)   shift; info "relay ECP em 0.0.0.0:${1:-8092} -> $HOST:8060"; exec python3 "$HERE/ecp_relay.py" "${1:-8092}";;
    relay-bg) ensure_2323; ensure_relay || die "relay nao configurado";;
    relay-stop)
        if [ -f /tmp/miragem-relay.pid ]; then
            p="$(cat /tmp/miragem-relay.pid 2>/dev/null)"
            [ -n "$p" ] && kill "$p" 2>/dev/null || true
            rm -f /tmp/miragem-relay.pid
            echo "relay parado"
        else
            echo "relay nao esta rodando (sem pidfile)"
        fi;;
    *) cat >&2 <<EOF
uso:
  ROKU_HOST=<ip> $0 push <slot> <dir|zip> [--beacon URL] [--marker TXT] [--manifest FILE]
  ROKU_HOST=<ip> $0 apply|restore|status <slot>
  ROKU_HOST=<ip> $0 dlna status|apply|restore
  ROKU_HOST=<ip> $0 relay [porta] | relay-bg | relay-stop
EOF
       exit 2;;
esac
