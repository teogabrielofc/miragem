#!/usr/bin/env bash
# rmp_dlna.sh - liga o DLNA no USB Media Player (canal 2213) por sessão.
#
# O 2213 é a variante "USB only": o manifesto traz usbonly=1 e o
# components/CustomTasks/ConfigReader/ConfigReader.brs desliga
# enableDLNA/enableHTTPFS/romOnly por causa disso. O código DLNA/SSDP/UPnP
# existe no pacote; o que falta é o flag.
#
# Este script NÃO altera o pacote assinado: ele descompacta /plugins/2213.zip
# em /tmp, troca usbonly=1 -> 0 e faz bind do manifesto por cima de
# pkg:/manifest. Depois reinicia o SdkLauncher (cold start) — sem isso o app
# vem de instant-resume e não relê o manifesto.
#
# Uso (com root ativo na TV, porta 2222):
#   ROKU_HOST=192.168.1.100 bash tools/rmp_dlna.sh status
#   ROKU_HOST=192.168.1.100 bash tools/rmp_dlna.sh apply
#   ROKU_HOST=192.168.1.100 bash tools/rmp_dlna.sh restore
#
# Vale por boot: reboot/remount do plugin volta a USB-only. O pacote em
# /plugins continua assinado e intocado.
set -u
TV="${ROKU_HOST:?defina ROKU_HOST com o IP da TV}"
HERE="$(cd "$(dirname "$0")" && pwd)"
MODE="${1:-status}"

tv() { ROKU_HOST="$TV" python3 "$HERE/tvcmd.py" "$1"; }
ecp() { curl -s -m 5 -d '' "http://$TV:8060$1" -o /dev/null -w '%{http_code}'; }

# acha o pkg: do 2213 pelo título (o glob principal não pega o path peer)
FIND='P=$(grep -l "^title=USB Media Player" /tmp/plugin/*/pkg:/manifest 2>/dev/null | head -1); [ -n "$P" ] && P=${P%/manifest}; echo "$P"'
PKG="$(tv "$FIND" | tr -d '\r' | grep '^/tmp/plugin/.*pkg:$' | tail -1)"
[ -n "$PKG" ] || { echo "ERRO: pkg: do 2213 não encontrado"; exit 1; }

case "$MODE" in
  status)
    tv "echo pkg=$PKG; echo -n 'manifest: '; grep -n '^usbonly=' $PKG/manifest; echo -n 'bind: '; grep manifest_nousb /proc/self/mountinfo | head -1"
    echo -n "active-app: "; curl -s -m 5 "http://$TV:8060/query/active-app" | grep -o 'id="[^"]*"' | head -1
    ;;
  apply)
    APPLIED="$(tv "sed -n 's/^usbonly=//p' $PKG/manifest" | tr -d '\r' | grep -E '^[01]$' | head -1)"
    if [ "$APPLIED" = "0" ]; then
      echo "já aplicado (bind ativo). Nada a fazer."
      exit 0
    fi
    tv "set -e
      rm -rf /tmp/rmp_dlna; mkdir -p /tmp/rmp_dlna
      /usr/bin/unzip -o /plugins/2213.zip -d /tmp/rmp_dlna >/dev/null
      sed -i 's/^usbonly=1\$/usbonly=0/' /tmp/rmp_dlna/manifest
      mount --bind /tmp/rmp_dlna/manifest $PKG/manifest
      grep -n '^usbonly=' $PKG/manifest
      for p in /proc/[0-9]*; do [ \"\$(cat \$p/comm 2>/dev/null)\" = SdkLauncher ] && kill \${p#/proc/} 2>/dev/null; done
      exit 0" || { echo "ERRO ao aplicar"; exit 1; }
    sleep 1
    echo -n "launch 2213: "; ecp /launch/2213; echo
    echo "aplicado (DLNA ligado por esta sessão)"
    ;;
  restore)
    tv "umount $PKG/manifest 2>/dev/null; echo umount_rc=\$?; grep -n '^usbonly=' $PKG/manifest; for p in /proc/[0-9]*; do [ \"\$(cat \$p/comm 2>/dev/null)\" = SdkLauncher ] && kill \${p#/proc/} 2>/dev/null; done; exit 0"
    sleep 1
    echo -n "launch 2213: "; ecp /launch/2213; echo
    echo "restaurado (USB-only; vale a partir do próximo cold start)"
    ;;
  *)
    echo "uso: $0 [status|apply|restore]"; exit 2;;
esac
