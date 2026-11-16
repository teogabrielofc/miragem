#!/usr/bin/env bash
# install_manager.sh - instala/atualiza o gerenciador (miragemd) e o srpatch
# na TV, com root ativo (porta 2222), e deixa o srpatch persistido em /nvram.
#
#   ROKU_HOST=192.168.1.100 bash tools/install_manager.sh
#   ROKU_TARGET=h536x-15.0.4.5537 (default)
#
# O srpatch fica em /nvram/miragem/srpatch (sobrevive ao reboot) e o miragemd
# restaura /tmp/srpatch no boot. Assim o botão Superroot funciona sem reenvio.
set -u
TV="${ROKU_HOST:?defina ROKU_HOST com o IP da TV}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
TARGET="${ROKU_TARGET:-h536x-15.0.4.5537}"
MG="$HERE/miragemd.arm"
SR="$REPO/payload/$TARGET/srpatch"

[ -f "$MG" ] || { echo "ERRO: $MG nao existe"; exit 1; }
[ -f "$SR" ] || { echo "ERRO: $SR nao existe (rode tools/build.mjs)"; exit 1; }

export ROKU_HOST="$TV"
echo "== TV $TV · alvo $TARGET"

# 1) agente 2323 (o tvput usa; precisa de root na 2222)
python3 "$HERE/tvcmd.py" "/tmp/ra6 bind 2323 >/dev/null 2>&1 &" >/dev/null 2>&1 || true
sleep 1

# 2) envia os binarios
python3 "$HERE/tvput.py" /tmp/miragemd "$MG" || { echo "ERRO: envio do miragemd"; exit 1; }
python3 "$HERE/tvput.py" /tmp/srpatch "$SR" || { echo "ERRO: envio do srpatch"; exit 1; }

# 3) instala, persiste em /nvram, reinicia o miragemd
python3 "$HERE/tvcmd.py" '
set -e
mkdir -p /tmp/miragem /nvram/miragem
cp /tmp/srpatch /nvram/miragem/srpatch
cp /tmp/srpatch /tmp/miragem/srpatch
cp /tmp/miragemd /tmp/miragem/miragemd
chmod 755 /tmp/miragem/miragemd /tmp/srpatch /nvram/miragem/srpatch
for p in /proc/[0-9]*; do c=$(cat $p/comm 2>/dev/null); [ "$c" = "miragemd" ] && kill ${p#/proc/} 2>/dev/null; done
sleep 1
nohup /tmp/miragem/miragemd -p 8090 -d /tmp/miragem/www >/tmp/miragem/miragemd.log 2>&1 &
sleep 1
echo "-- instalado:"
ls -l /tmp/miragem/miragemd /tmp/srpatch /nvram/miragem/srpatch
' || { echo "ERRO ao instalar na TV"; exit 1; }
echo "PRONTO: gerenciador na 127.0.0.1:8090 e srpatch persistido em /nvram/miragem"
