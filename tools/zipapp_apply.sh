#!/bin/sh
# zipapp_apply.sh - roda NA TV (root). Aplicacao/restauracao do ZIP app.
#
#   sh /tmp/miragem/zipapp_apply.sh status  <slot>
#   sh /tmp/miragem/zipapp_apply.sh apply   <slot>
#   sh /tmp/miragem/zipapp_apply.sh restore <slot>
#
# O "push" (tools/zipapp.sh, no PC) deixa:
#   /tmp/miragem/zipapp/<slot>.zip        ZIP repackado (manifest do slot)
#   /tmp/miragem/zipapp/slot              ultimo slot preparado
#   /tmp/miragem/zipapp/marker-<slot>     marcador de prova (texto)
#   /tmp/miragem/mqfd, /tmp/miragem/ecp   helpers ARM
#
# Metodo (provado em 2026-11-16):
#   1. ZipMountServer monta o ZIP por mqueue (fds 4/5);
#   2. mount --bind do mount FUSE sobre /tmp/plugin/<dir>/pkg:;
#   3. rm do cache de bytecode do slot (mod_cache/<slot>.{mod,ccc});
#   4. SIGKILL somente no SdkLauncher do app (memory:/plugins/<pid>);
#   5. ECP POST /launch/<slot> (127.0.0.1:8060; se o build recusar loopback,
#      usa o relay do PC em /tmp/miragem/ecp_relay).
#
# Nada e escrito em /plugins, /nvram ou rootfs. O restore volta o cache
# original (byte a byte) e remove bind/mount. Vale por boot/sessao.
set -u

BASE=/tmp/miragem
ZB="$BASE/zipapp"
MQ="$BASE/mqfd"
ECP="$BASE/ecp"
CACHE=/var/cache/cachefs/nopersist/mod_cache
REPO=/tmp/ipc/plugins/repository.data
ATTRS=/tmp/ipc/plugins

MODE="${1:-status}"
SLOT="${2:-}"
[ -n "$SLOT" ] || { echo "erro=slot_ausente"; exit 2; }
case "$SLOT" in *[!A-Za-z0-9._-]*) echo "erro=slot_invalido"; exit 2;; esac

ZIP="$ZB/$SLOT.zip"
# O ZipMountServer só abre ZIPs dentro de /tmp/rsgPlugins (sandbox do
# servidor). Cada apply usa caminhos novos: o servidor guarda o caminho e
# devolve result:true sem remontar quando o mesmo mountpoint reaparece.
RSG=/tmp/rsgPlugins
MNTFILE="$ZB/state-$SLOT"
MNT=""
RFILE=""
if [ -f "$MNTFILE" ]; then
    MNT=$(sed -n 1p "$MNTFILE" 2>/dev/null)
    RFILE=$(sed -n 2p "$MNTFILE" 2>/dev/null)
fi
BK="$ZB/backup/$SLOT"
APPLIED="$ZB/applied-$SLOT"
MARKERF="$ZB/marker-$SLOT"
MOD="$CACHE/$SLOT.mod"
CCC="$CACHE/$SLOT.ccc"
SENTINEL=.miragem_zipapp

log() { echo "$*" >&2; }

slot_dir() {
    d=""
    [ -f "$REPO" ] && d=$(awk -F'"' -v id="$SLOT" '$2==id{print $4; exit}' "$REPO" 2>/dev/null)
    if [ -z "$d" ]; then
        f=$(grep -l "\"_id\":\"$SLOT\"" "$ATTRS"/*.attr 2>/dev/null | head -1)
        [ -n "$f" ] && d=$(sed -n 's/.*"_install_path":"\([^"]*\)".*/\1/p' "$f" | head -1)
    fi
    printf '%s' "$d"
}

mounted() {    [ -n "$MNT" ] || return 1
    [ -d "$MNT" ] || return 1
    awk -v m="$MNT" '$5==m{found=1} END{exit found?0:1}' /proc/self/mountinfo 2>/dev/null
}

# Remove mounts/arquivos de tentativas anteriores deste slot.
clean_slot_mounts() {
    for m in $(awk -v p="/rsgPlugins/miragem-$SLOT-" '$5 ~ p && $5 ~ /-mnt$/ {print $5}' /proc/self/mountinfo 2>/dev/null); do
        umount "$m" 2>/dev/null || true
    done
    for d in /tmp/rsgPlugins/miragem-$SLOT*-mnt; do
        [ -d "$d" ] && rmdir "$d" 2>/dev/null || true
    done
    rm -f /tmp/rsgPlugins/miragem-$SLOT*.zip 2>/dev/null || true
    rm -f "$MNTFILE" 2>/dev/null || true
    MNT=""; RFILE=""
}

# O bind nao deixa rastro de path no mountinfo; usamos um arquivo sentinela
# que so existe dentro do ZIP preparado.
bound() { [ -f "$PKG/$SENTINEL" ]; }

manifest_mounted() {
    [ -n "$PKG" ] && awk -v m="$PKG/manifest" '$5==m{found=1} END{exit found?0:1}' /proc/self/mountinfo 2>/dev/null
}

# Um bind do DLNA sobre pkg:/manifest deixa o pkg: ocupado (o umount do ZIP
# falha com EBUSY). Ao aplicar/restaurar o ZIP, removemos esse override: o
# manifesto volta a ser o do ZIP/pacote stock.
unmount_manifest() {
    if manifest_mounted; then
        umount "$PKG/manifest" 2>/dev/null || { sleep 1; umount "$PKG/manifest" 2>/dev/null; }
        log "zipapp: bind de manifesto (DLNA) removido"
    fi
}

zipmount_pid() {
    for p in /proc/[0-9]*; do
        [ "$(cat "$p/comm" 2>/dev/null)" = ZipMountServer ] && { echo "${p#/proc/}"; return 0; }
    done
    return 1
}

mq_fds() { # $1=pid; ecoa "<send> <recv>"
    s=""; r=""
    for f in /proc/"$1"/fd/*; do
        l=$(readlink "$f" 2>/dev/null)
        [ "$l" = "/zip-client" ] && s=${f##*/}
        [ "$l" = "/zip-server" ] && r=${f##*/}
    done
    echo "${s:-4} ${r:-5}"
}

# SdkLauncher do app: o cgroup de memoria e memory:/plugins/<proprio pid>.
# O launcher "spare"/background-idle tem memory:/ e NAO pode ser morto.
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

ecp_launch() {
    out=$(ecp_call POST "/launch/$SLOT")
    if [ $? -eq 0 ]; then
        echo "launch=ok"
        return 0
    fi
    echo "launch=fail"
    return 1
}

# O ECP recusa loopback neste build ("ECP is not allowed from loopback
# address"). Tenta 127.0.0.1 e, se falhar, usa o relay do PC configurado em
# /tmp/miragem/ecp_relay (<ip>:<porta>) pelo tools/zipapp.sh.
relay_addr() { cat "$BASE/ecp_relay" 2>/dev/null | tr -d ' \r\n'; }

ecp_call() { # $1=metodo $2=path; imprime o corpo
    [ -x "$ECP" ] || return 1
    mkdir -p "$ZB" 2>/dev/null || true
    if "$ECP" "$1" "$2" >"$ZB/ecp.out" 2>/dev/null; then
        cat "$ZB/ecp.out"
        return 0
    fi
    ra=$(relay_addr)
    [ -n "$ra" ] || return 1
    rh=${ra%%:*}; rp=${ra##*:}
    [ -n "$rh" ] && [ -n "$rp" ] || return 1
    "$ECP" "$1" "$2" "$rp" "$rh"
}

active_app() {
    ecp_call GET /query/active-app 2>/dev/null \
        | sed -n 's/.*<app id="\([^"]*\)".*/\1/p' | head -1
}

print_status() {
    printf 'slot=%s\n' "$SLOT"
    printf 'dir=%s\n' "$DIR"
    printf 'pkg=%s\n' "$PKG"
    printf 'zip=%s\n' "$ZIP"
    printf 'mount_zip=%s\n' "$RFILE"
    if [ -f "$ZIP" ]; then printf 'zip_bytes=%s\n' "$(wc -c < "$ZIP" | tr -d ' ')"; else printf 'zip_bytes=0\n'; fi
    if [ -f "$ZIP" ]; then printf 'staged=1\n'; else printf 'staged=0\n'; fi
    if [ -f "$APPLIED" ]; then printf 'applied=1\n'; else printf 'applied=0\n'; fi
    if mounted; then printf 'mounted=1\n'; else printf 'mounted=0\n'; fi
    if bound; then printf 'bound=1\n'; else printf 'bound=0\n'; fi
    if [ -f "$MOD" ]; then printf 'cache=1\n'; printf 'has_mod=1\n'; printf 'cache_mod=%s\n' "$(wc -c < "$MOD" | tr -d ' ')"; else printf 'cache=0\nhas_mod=0\ncache_mod=0\n'; fi
    if [ -f "$CCC" ]; then printf 'has_ccc=1\n'; printf 'cache_ccc=%s\n' "$(wc -c < "$CCC" | tr -d ' ')"; else printf 'has_ccc=0\ncache_ccc=0\n'; fi
    if [ -f "$BK/state" ]; then printf 'backup=1\n'; else printf 'backup=0\n'; fi
    if [ -f "$BK/state" ] && grep -q '^active=1$' "$BK/state" 2>/dev/null; then printf 'backup_active=1\n'; else printf 'backup_active=0\n'; fi
    printf 'app_pid=%s\n' "$(app_pid || true)"
    printf 'active_app=%s\n' "$(active_app)"
}

do_mount() {
    if mounted; then
        log "zipapp: FUSE ja montado em $MNT"
        return 0
    fi
    pid=$(zipmount_pid) || { log "zipapp: ZipMountServer nao encontrado"; return 1; }
    set -- $(mq_fds "$pid")
    SFD="$1"; RFD="$2"
    [ -x "$MQ" ] || { log "zipapp: mqfd ausente ($MQ)"; return 1; }
    mkdir -p "$RSG" "$MNT"
    if ! cp -f "$ZIP" "$RFILE"; then
        log "zipapp: nao consegui copiar o ZIP para $RSG"
        return 1
    fi
    chmod 644 "$RFILE" 2>/dev/null || true
    json="{\"action\":\"mount\",\"type\":\"zip\",\"file\":\"$RFILE\",\"mount\":\"$MNT\",\"flags\":0,\"uid\":501,\"gid\":501}"
    "$MQ" send "$pid" "$SFD" "$json" >"$ZB/mq-send.log" 2>&1 || { log "zipapp: mq send falhou"; return 1; }
    # O ZipMountServer pode levar ~20 s para montar; espera o mount aparecer
    # em /proc/self/mountinfo (e drena a resposta quando chegar).
    out=""
    i=0
    while [ $i -lt 120 ]; do
        if mounted; then
            out=$("$MQ" recv "$pid" "$RFD" 1 2>&1)
            echo "$out" >"$ZB/mq-recv.log"
            case "$out" in
                *'"result":true'*|*'(no reply)'*) : ;;
            esac
            mounted || { log "zipapp: mountinfo perdeu $MNT"; return 1; }
            log "zipapp: FUSE montado em $MNT (ZipMountServer pid $pid fds $SFD/$RFD)"
            return 0
        fi
        sleep 0.2 2>/dev/null || sleep 1
        i=$((i+1))
    done
    out=$("$MQ" recv "$pid" "$RFD" 2 2>&1)
    echo "$out" >"$ZB/mq-recv.log"
    log "zipapp: mount falhou ($out)"
    return 1
}

do_apply() {
    [ -f "$ZIP" ] || { log "zipapp: ZIP ausente: $ZIP"; print_status; return 1; }

    mkdir -p "$BK"
    if [ ! -f "$BK/state" ]; then
        hm=0; hc=0
        if [ -f "$MOD" ]; then cp -p "$MOD" "$BK/orig.mod" && hm=1; fi
        if [ -f "$CCC" ]; then cp -p "$CCC" "$BK/orig.ccc" && hc=1; fi
        printf 'had_mod=%s\nhad_ccc=%s\nactive=1\n' "$hm" "$hc" >"$BK/state"
        log "zipapp: cache original salvo (mod=$hm ccc=$hc) em $BK"
    else
        sed -i 's/^active=.*/active=1/' "$BK/state" 2>/dev/null || true
    fi

    # Um ZIP novo no mesmo slot exige remontar o FUSE (conteudo e' cacheado no
    # mount). Remove primeiro o bind (o topo) e o mount antigo; nunca desmonta
    # o pkg: stock.
    unmount_manifest
    if [ -f "$PKG/$SENTINEL" ]; then
        umount "$PKG" 2>/dev/null || true
    fi
    if mounted; then
        umount "$MNT" 2>/dev/null || true
    fi
    clean_slot_mounts
    stamp=$(date +%s 2>/dev/null || echo 0)
    RFILE="$RSG/miragem-$SLOT-$stamp.zip"
    MNT="$RSG/miragem-$SLOT-$stamp-mnt"
    mkdir -p "$ZB"
    printf '%s\n%s\n' "$MNT" "$RFILE" >"$MNTFILE"
    do_mount || { print_status; return 1; }

    if mount --bind "$MNT" "$PKG"; then
        log "zipapp: bind $MNT -> $PKG"
    else
        log "zipapp: mount --bind falhou"
        print_status
        return 1
    fi
    if [ ! -f "$PKG/$SENTINEL" ]; then
        log "zipapp: pkg: nao mostra o sentinela $SENTINEL"
        print_status
        return 1
    fi

    rm -f "$MOD" "$CCC"
    log "zipapp: cache invalidado ($SLOT.mod/.ccc)"
    date >"$APPLIED" 2>/dev/null || echo applied >"$APPLIED"

    kill_app
    ecp_launch || { print_status; return 1; }

    i=0
    while [ $i -lt 25 ]; do
        a=$(active_app)
        [ "$a" = "$SLOT" ] && break
        sleep 0.2 2>/dev/null || sleep 1
        i=$((i+1))
    done
    log "zipapp: aplicado (active_app=$(active_app))"
    print_status
    return 0
}

do_restore() {
    needs=0
    [ -f "$PKG/$SENTINEL" ] && needs=1
    [ -f "$APPLIED" ] && needs=1
    if [ "$needs" = 0 ]; then
        log "zipapp: nada aplicado; restaurar e no-op"
        print_status
        return 0
    fi

    kill_app

    unmount_manifest
    if [ -f "$PKG/$SENTINEL" ]; then
        umount "$PKG" 2>/dev/null || { sleep 1; umount "$PKG" 2>/dev/null; }
        log "zipapp: bind removido (rc=$?)"
    fi
    if mounted; then
        umount "$MNT" 2>/dev/null || true
        log "zipapp: FUSE desmontado"
    fi
    clean_slot_mounts

    if [ -f "$BK/state" ]; then
        . "$BK/state"
        if [ "${had_mod:-0}" = 1 ] && [ -f "$BK/orig.mod" ]; then
            cp -p "$BK/orig.mod" "$MOD"
        else
            rm -f "$MOD"
        fi
        if [ "${had_ccc:-0}" = 1 ] && [ -f "$BK/orig.ccc" ]; then
            cp -p "$BK/orig.ccc" "$CCC"
        else
            rm -f "$CCC"
        fi
        sed -i 's/^active=.*/active=0/' "$BK/state" 2>/dev/null || true
        log "zipapp: cache original restaurado (had_mod=${had_mod:-0} had_ccc=${had_ccc:-0})"
    else
        rm -f "$MOD" "$CCC"
        log "zipapp: sem backup; cache do ZIP removido"
    fi

    # provas escritas pelo app de teste (tmp: do slot)
    if [ -f "$MARKERF" ]; then
        marker=$(cat "$MARKERF" 2>/dev/null)
        for f in "$DIR/tmp:/zipapp_proof.txt" "$DIR/tmp:/zipapp_steps.txt"; do
            if [ -f "$f" ] && grep -qF "$marker" "$f" 2>/dev/null; then
                rm -f "$f"
                log "zipapp: prova removida ($f)"
            fi
        done
    fi

    rm -f "$APPLIED"
    ecp_launch || true
    i=0
    while [ $i -lt 25 ]; do
        a=$(active_app)
        [ "$a" = "$SLOT" ] && break
        sleep 0.2 2>/dev/null || sleep 1
        i=$((i+1))
    done
    log "zipapp: restaurado (active_app=$(active_app))"
    print_status
    return 0
}

DIR=$(slot_dir)
if [ -z "$DIR" ] || [ ! -d "$DIR" ]; then
    echo "erro=slot_nao_encontrado"
    echo "slot=$SLOT"
    exit 1
fi
PKG="$DIR/pkg:"

case "$MODE" in
    status)  print_status; exit 0 ;;
    apply)   do_apply ;;
    restore) do_restore ;;
    *) echo "uso: $0 status|apply|restore <slot>"; exit 2 ;;
esac
