#!/bin/sh
# dlna_apply.sh - roda NA TV (root). DLNA no USB Media Player (2213) por sessao.
#
#   sh /tmp/miragem/dlna_apply.sh status
#   sh /tmp/miragem/dlna_apply.sh apply
#   sh /tmp/miragem/dlna_apply.sh restore
#
# O 2213 e' a variante USB-only: o manifesto traz usbonly=1 e o
# ConfigReader.brs desliga enableDLNA/enableHTTPFS. O codigo DLNA existe no
# pacote; este helper descompacta /plugins/2213.zip em /tmp/miragem/dlna,
# troca usbonly=1 -> 0, faz bind do manifesto sobre pkg:/manifest e faz
# cold start SO do app (kill seletivo do SdkLauncher + ECP launch).
#
# Nao altera /plugins/2213.zip, /nvram nem rootfs. Vale por boot/sessao.
set -u

BASE=/tmp/miragem
DL="$BASE/dlna"
ECP="$BASE/ecp"
SLOT="${DLNA_SLOT:-2213}"
ATTRS=/tmp/ipc/plugins
MAN="$DL/manifest"

log() { echo "$*" >&2; }

slot_dir() {
    d=""
    [ -f /tmp/ipc/plugins/repository.data ] && \
        d=$(awk -F'"' -v id="$SLOT" '$2==id{print $4; exit}' /tmp/ipc/plugins/repository.data 2>/dev/null)
    if [ -z "$d" ]; then
        f=$(grep -l "\"_id\":\"$SLOT\"" "$ATTRS"/*.attr 2>/dev/null | head -1)
        [ -n "$f" ] && d=$(sed -n 's/.*"_install_path":"\([^"]*\)".*/\1/p' "$f" | head -1)
    fi
    printf '%s' "$d"
}

# Mesma regra do zipapp: so o SdkLauncher com memory:/plugins/<pid>.
app_pid() {
    active=""
    for p in /proc/[0-9]*; do
        pid=${p#/proc/}
        [ "$(cat "$p/comm" 2>/dev/null)" = SdkLauncher ] || continue
        grep -q "memory:/plugins/$pid\$" "$p/cgroup" 2>/dev/null || continue
        if grep -q "cpu:/active" "$p/cgroup" 2>/dev/null; then
            echo "$pid"; return 0
        fi
        [ -z "$active" ] && active=$pid
    done
    [ -n "$active" ] && { echo "$active"; return 0; }
    return 1
}

kill_app() {
    pid=$(app_pid) || { echo "killed="; return 0; }
    kill -9 "$pid" 2>/dev/null
    i=0
    while [ $i -lt 30 ] && [ -d "/proc/$pid" ]; do
        sleep 0.1 2>/dev/null || sleep 1
        i=$((i+1))
    done
    echo "killed=$pid"
}

active_app() {
    ecp_call GET /query/active-app 2>/dev/null \
        | sed -n 's/.*<app id="\([^"]*\)".*/\1/p' | head -1
}

relay_addr() { cat "$BASE/ecp_relay" 2>/dev/null | tr -d ' \r\n'; }

# ECP local primeiro; se o build recusar loopback, usa o relay do PC
# (/tmp/miragem/ecp_relay = <ip>:<porta>).
ecp_call() { # $1=metodo $2=path
    [ -x "$ECP" ] || return 1
    if "$ECP" "$1" "$2" >/tmp/miragem/ecp.out 2>/dev/null; then
        cat /tmp/miragem/ecp.out
        return 0
    fi
    ra=$(relay_addr)
    [ -n "$ra" ] || return 1
    rh=${ra%%:*}; rp=${ra##*:}
    [ -n "$rh" ] && [ -n "$rp" ] || return 1
    "$ECP" "$1" "$2" "$rp" "$rh"
}

ecp_launch() {
    ecp_call POST "/launch/$SLOT" >/dev/null 2>&1 && { echo "launch=ok"; return 0; }
    echo "launch=fail"
    return 1
}

usbonly() {
    [ -f "$PKG/manifest" ] || { echo ""; return 0; }
    sed -n 's/^usbonly=//p' "$PKG/manifest" | head -1
}

dlna_bound() {
    [ -n "$PKG" ] || return 1
    awk -v m="$PKG/manifest" '$5==m{found=1} END{exit found?0:1}' /proc/self/mountinfo 2>/dev/null
}

print_status() {
    printf 'slot=%s\n' "$SLOT"
    printf 'pkg=%s\n' "$PKG"
    printf 'usbonly=%s\n' "$(usbonly)"
    if dlna_bound; then printf 'bound=1\n'; else printf 'bound=0\n'; fi
    if [ "$(usbonly)" = "0" ]; then printf 'enabled=1\n'; else printf 'enabled=0\n'; fi
    printf 'app_pid=%s\n' "$(app_pid || true)"
    printf 'active_app=%s\n' "$(active_app)"
}

do_apply() {
    yes=0
    if dlna_bound && [ "$(usbonly)" = "0" ]; then
        log "dlna: ja aplicado (bind ativo)"
        yes=1
    fi
    if [ "$yes" = 0 ]; then
        [ -f /plugins/"$SLOT".zip ] || { log "dlna: /plugins/$SLOT.zip ausente"; print_status; return 1; }
        rm -rf "$DL"
        mkdir -p "$DL"
        /usr/bin/unzip -o /plugins/"$SLOT".zip -d "$DL" >/dev/null 2>&1 || {
            log "dlna: unzip falhou"; print_status; return 1; }
        sed -i 's/^usbonly=1$/usbonly=0/' "$MAN"
        [ "$(sed -n 's/^usbonly=//p' "$MAN" | head -1)" = "0" ] || {
            log "dlna: manifesto sem usbonly=1"; print_status; return 1; }
        if dlna_bound; then umount "$PKG/manifest" 2>/dev/null || true; fi
        if mount --bind "$MAN" "$PKG/manifest"; then
            log "dlna: bind do manifesto (usbonly=0)"
        else
            log "dlna: mount --bind do manifesto falhou"; print_status; return 1
        fi
    fi
    kill_app
    ecp_launch || { print_status; return 1; }
    i=0
    while [ $i -lt 25 ]; do
        [ "$(active_app)" = "$SLOT" ] && break
        sleep 0.2 2>/dev/null || sleep 1
        i=$((i+1))
    done
    log "dlna: aplicado (cold start; active_app=$(active_app))"
    print_status
    return 0
}

do_restore() {
    if ! dlna_bound; then
        log "dlna: sem bind; restaurar e no-op"
        print_status
        return 0
    fi
    kill_app
    umount "$PKG/manifest" 2>/dev/null || { sleep 1; umount "$PKG/manifest" 2>/dev/null; }
    log "dlna: bind removido"
    ecp_launch || true
    i=0
    while [ $i -lt 25 ]; do
        [ "$(active_app)" = "$SLOT" ] && break
        sleep 0.2 2>/dev/null || sleep 1
        i=$((i+1))
    done
    log "dlna: restaurado (USB-only; active_app=$(active_app))"
    print_status
    return 0
}

DIR=$(slot_dir)
[ -n "$DIR" ] || { echo "erro=slot_nao_encontrado"; exit 1; }
PKG="$DIR/pkg:"

MODE="${1:-status}"
case "$MODE" in
    status)  print_status; exit 0 ;;
    apply)   do_apply ;;
    restore) do_restore ;;
    *) echo "uso: $0 status|apply|restore"; exit 2 ;;
esac
