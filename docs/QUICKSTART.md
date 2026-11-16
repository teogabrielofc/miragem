# Guia rápido — do zero ao root

Este é o caminho curto: **PC na mesma rede** com os servidores no ar, **TV**
abrindo o instalador, e o resto acontece sozinho. Leia a seção 1 antes de
qualquer coisa: é ela que explica **quem puxa o quê** (e por que o
`gx_bind.rld` vem do PC).

> Requisitos: Roku OS **15.0.4** com **Ginga** (verificado no H536X / Philco
> PTV24G5YR2CP), PC Linux com Python 3, os dois na mesma sub-rede.
> **Não atualize para 16.0.**

## 0. Resumo em 30 segundos

1. No PC: `start_servers.sh` no ar (serve `agent8.rld`, `gx_bind.rld`,
   `rootagent7`, `miragemd.arm`).
2. Na TV: abra o instalador, digite o **IP do PC**, aperte **Instalar**.
3. A página vira `uid=500`; o PC manda o agente buscar o `gx_bind.rld`; o
   GhostLock vira root; o shell sobe na **2222**.

## 1. Quem puxa o quê (a parte que confunde)

A TV **não** baixa o exploit sozinha da internet: ela busca no **seu PC**, na
LAN. O PC só fica ouvindo e manda os comandos.

| # | Quem inicia | Para | Porta | O que trafega |
|---|---|---|---|---|
| 1 | TV (estágio V8 do instalador) | PC | **47127** | `agent8.rld` — agente `uid=500` |
| 2 | agente `uid=500` | PC | **47140** | conexão; o PC responde `fetch 47190 /tmp/gx_bind.rld` |
| 3 | agente `uid=500` | PC | **47190** | `gx_bind.rld` (GhostLock) |
| 4 | `gx_bind` (já root) | PC | **47192** | `rootagent7` (agente root) |
| 5 | agente root | TV | **2222** | shell root (`bindsh`) |
| 6 | `miragemd` | TV | **8090** | gerenciador local |
| 7 | (opcional) instalar o gerenciador | PC | **47193** | `miragemd.arm` |

Ou seja: **o `gx_bind` é puxado da 47190 pelo agente que já está na TV**, e não
por um download seu. O PC precisa (a) estar no IP que você digitar, (b) ter os
arquivos e (c) estar com as portas abertas.

**Por que o IP do PC importa em dois lugares:**

- o shellcode do estágio 1 tem o IPv4 do PC embutido em 4 bytes; o instalador
  troca pelo IP digitado (`192.168.1.2` = `c0a80102` no HTML publicado);
- o `gx_bind.rld` é compilado com `HOST_IP`; o binário publicado usa
  `255.255.255.255` e recebe o IP na hora (`/api/payload?...&ip=` ou
  `start_servers.sh`).

IP errado = o `uid=500` tenta conectar num endereço que não existe e a
instalação para em “não confirmei o root”.

## 2. No PC — preparar (uma vez)

```sh
git clone <seu-repo> miragem && cd miragem

# toolchain ARM (musl) — ajuste o caminho do seu
TC=/caminho/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf
PC=192.168.1.2          # IP do PC na LAN (o mesmo que vai no instalador)

# agente uid=500 (conecta na 47140) e agente root (bindsh 2222)
$TC-gcc -O2 -static -no-pie -DHOST_IP="\"$PC\"" -DHOST_PORT=47140 \
  -Wl,-Ttext-segment=0x20000000 -o agent8 exploit/rootagent.c
cp agent8 rootagent7
python3 exploit/pack.py agent8 agent8.rld

# GhostLock (opcional: o publicado já vem pronto)
$TC-gcc -O2 -static -no-pie -DHOST_IP="\"$PC\"" \
  -Wl,-Ttext-segment=0x10000 -o gx_bind exploit/gx_bind.c
python3 exploit/pack.py gx_bind gx_bind.rld

# gerenciador (o repo já traz tools/miragemd.arm pronto)
$TC-gcc -O2 -static -no-pie -o miragemd tools/miragemd.c
```

Coloque os artefatos no diretório de estado (padrão `/tmp/opencode`; use
`ROKU_STATE_DIR` para outro lugar):

```sh
mkdir -p ~/miragem-state && cp agent8.rld gx_bind.rld rootagent7 ~/miragem-state/
```

> Se você **já** tem um build que funcionou nesta TV, reuse esses arquivos —
> eles são o que importa. O `gx_bind` pode vir do site, personalizado pelo IP.

Descubra o IP do PC (se não souber):

```sh
ip route get 1.1.1.1 | sed -n 's/.* src \([0-9.]*\).*/\1/p'
```

## 3. Subir os servidores

```sh
export ROKU_STATE_DIR=~/miragem-state
ROKU_TARGET=h536x-15.0.4.5537 \
ROKU_PC_IP=192.168.1.2 \
ROKU_HOST=192.168.1.100 \
  bash tools/start_servers.sh --force
```

O que deve aparecer:

```
=== Roku root: servidores (/home/vc/miragem-state) ===
alvo: h536x-15.0.4.5537 (.../payload/h536x-15.0.4.5537)
gx_bind: IP do PC = 192.168.1.2
porta 47127 -> .../agent8.rld (pid ...)
porta 47190 -> .../gx_bind.served.rld (pid ...)
porta 47192 -> .../rootagent7 (pid ...)
porta 47193 -> .../tools/miragemd.arm (pid ...)
porta 47140 -> auto_root.py (pid ...)
PRONTO: servidores no ar. Na TV: abra o instalador...
```

- `ROKU_PC_IP` é o IP que vai dentro do `gx_bind`; se omitir, o script detecta
  pela rota até a TV.
- `ROKU_TARGET` faz o script usar `payload/<id>/gx_bind.rld` (já com o
  placeholder) e substituir o IP na hora.
- Se a 2222 já estiver aberta, o script não reinicia o `auto_root`.

## 4. Na TV

**Caminho A — HTTPS** (se a TV abre a internet normalmente): no navegador da
TV, abra `https://<seu-dominio>/installer`. Dá para pré-preencher o IP:
`https://<seu-dominio>/installer?pc=192.168.1.2`.

**Caminho B — USB** (sempre funciona):
1. No PC, abra `/gerador` do site (ou `site/index.html`).
2. Na TV, com o pendrive, use **Carregamento rápido** em um canal com Ginga para
   a TV criar a pasta; leve o pendrive ao PC e envie o `MANIFEST` para o
   gerador; baixe o zip e extraia na raiz do pendrive.
3. Na TV: **Antena TV → app do Miragem**.

Na tela do instalador:
1. escolha o idioma;
2. digite o **IP do PC** (o mesmo da seção 2/3) — sem IPv4 válido o botão não
   instala;
3. aperte **Instalar** (OK no controle);
4. se aparecer a checagem de Chrome/firmware, “tentar mesmo assim” só se souber
   o que está fazendo;
5. se o kernel não for confirmado, escolha o **modelo/alvo** na lista (H536X)
   e **Usar este payload**;
6. espere a linha virar **“Root ativo · shell na 2222 · gerenciador pronto”**.

## 5. Conferir

```sh
ROKU_HOST=192.168.1.100 ./tools/tvsh        # shell root (Ctrl-] sai)
curl -s http://192.168.1.100:8060/query/device-info | grep software-version
```

Gerenciador: na TV, abra `/manager` (ou `127.0.0.1:8090`); no PC, use o
`tvsh`/`tvcmd.py` para o que precisar. Para instalar/atualizar o gerenciador e
o **srpatch** (superroot) de uma vez:

```sh
ROKU_HOST=192.168.1.100 bash tools/install_manager.sh
```

Ele deixa o srpatch em `/nvram/miragem/srpatch` (persistente); o gerenciador
restaura `/tmp/srpatch` no boot e o botão Superroot (Ajustes) fica disponível.

## 6. Depois de reiniciar a TV

O root **não persiste**. A TV volta ao normal; o PC pode continuar com os
servidores no ar. Basta abrir o instalador e repetir o passo 4 (não precisa
recompilar nada).

## 7. Problemas comuns

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| botão não instala / “Informe o IP do PC” | campo vazio ou IPv4 inválido | digite `192.168.1.2` (sem espaços) |
| “não confirmei o root” | IP errado, porta fechada, firewall, `agent8`/`47140` errado | confira `ROKU_PC_IP`, `ss -lnt \| grep 471`, logs `srv_*.log`/`auto_root.log` |
| 47127 recebe conexão mas nada sobe | `agent8` não conecta na 47140 | compile com `-DHOST_PORT=47140` e confira o `auto_root.log` (“agent from…”) |
| root sobe mas o gerenciador não | `miragemd` não servido/instalado | veja a 47193 e o log; o repo já tem `tools/miragemd.arm` |
| TV não abre o site | DNS/HTTPS da TV | use o caminho USB |
| quer testar sem TV | — | o instalador abre no PC, mas o exploit só funciona no navegador da TV |

## 8. Por que o PC no meio (e o que falta)

O estágio do navegador roda dentro do Chromium da TV com `uid=500`, mas o
binário nativo (GhostLock) ainda precisa ser entregue e controlado: o PC é
quem serve os arquivos e dispara `fetch`/`run`. Tirar o PC da jogada exige o
`gx_bind` buscar o agente root por HTTPS (sem IP embutido) — está documentado
como próximo passo em `docs/SITE.md` e `payload/README.md`.

---

## Quick guide (EN, short)

1. **PC** on the same LAN: build `agent8`/`rootagent7` (and optionally
   `gx_bind`), then `ROKU_TARGET=h536x-15.0.4.5537 ROKU_PC_IP=<pc-ip>
   ROKU_HOST=<tv-ip> bash tools/start_servers.sh`.
2. **TV**: open `/installer` (HTTPS) or the USB Ginga app, type the PC IP and
   press Install.
3. The TV's `uid=500` agent pulls `gx_bind.rld` from the PC on **47190**;
   `gx_bind` pulls `rootagent7` on **47192**; the root shell listens on
   **2222** and the manager on **8090**. Root does not survive a reboot.
