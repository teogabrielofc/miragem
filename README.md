<p align="center">
  <img src="assets/logo.png" alt="miragem" width="320">
</p>

<p align="center">
  <strong>Root e gerenciador homebrew para TVs Roku com Ginga (Roku OS 15.0.4)</strong><br>
  <em>Verificado no H536X / Philco PTV24G5YR2CP · kernel 4.9.118 · Brasil (Ginga / ISDB-Tb)</em>
</p>

<p align="center">
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-6a556f.svg"></a>
  <img alt="Status: research" src="https://img.shields.io/badge/status-research-8a6a2f.svg">
  <img alt="Target: Roku 15.0.4 + Ginga" src="https://img.shields.io/badge/target-Roku%2015.0.4%20%2B%20Ginga-a3505f.svg">
  <img alt="Region: Brazil" src="https://img.shields.io/badge/region-Brazil-2f7d5b.svg">
</p>

Miragem é um **instalador/gerenciador homebrew** para **TVs Roku com Ginga**
(Roku OS **15.0.4**). Verificado no **H536X / Philco PTV24G5YR2CP** (kernel
**Linux 4.9.118 ARMv7**, MStar **mt5867**); outros modelos com Ginga e o mesmo
OS precisam apenas do **GhostLock portado** para o kernel deles. O pacote vai num pendrive, aparece como app
interativo (Ginga/DTV Play) na entrada **Antena TV** e instala root + um
gerenciador local na própria TV. O site gera o pacote no navegador; o Vercel
publica site e apps.

Pesquisa feita **no próprio aparelho**. Não é um produto, não tem garantia, e
não redistribui firmware nem código proprietário da Roku.

> ## ⚠️ Requer Roku OS 15.0.4 + Ginga; kernel precisa ser suportado
>
> **O Miragem funciona em TVs Roku com Ginga no Roku OS 15.0.4.** O que muda de
> modelo para modelo é o **kernel**: o GhostLock escreve em memória do kernel e
> um offset errado pode dar panic. Hoje os offsets publicados são do
> **H536X / PTV24G5YR2CP (15.0.4.5537, Linux 4.9.118)**; outras TVs com Ginga e
> 15.0.4 só precisam **portar o GhostLock** para o kernel delas:
> **[`docs/PORTING.md`](docs/PORTING.md)**, [`kernels.json`](kernels.json) e
> [`docs/KERNELS.md`](docs/KERNELS.md).
>
> **EN:** works on Roku TVs with Ginga on OS 15.0.4. Only the kernel offsets are
> model-specific; other 15.0.4 Ginga models just need the GhostLock ported
> (`docs/PORTING.md`).

> **EN (short):** Miragem is a homebrew installer/manager for Roku TVs with
> Ginga on OS 15.0.4 (verified on the H536X/PTV24G5YR2CP, Linux 4.9.118). A USB
> package shows up as a Ginga interactive app; the installer escalates via
> GhostLock (CVE-2026-43499) from a Chromium 104 / CVE-2023-3079 foothold, then
> the manager (local HTTP on 8090) drives root, the PTY shell and dev-mode
> flags. Own-device research only; no firmware, keys or credentials.
>
> **ES (breve):** Miragem es un instalador/gestor homebrew para TVs Roku con
> Ginga en OS 15.0.4 (verificado en el H536X/PTV24G5YR2CP, Linux 4.9.118). El paquete
> USB se abre como app interactiva Ginga; el instalador obtiene root con
> GhostLock (CVE-2026-43499) desde un foothold Chromium 104 / CVE-2023-3079, y
> el gestor (HTTP local en 8090) controla root, la shell PTY y las flags de
> modo desarrollador. Solo para investigación en tu propio equipo.

## Índice

- [Compatibilidade](#compatibilidade)
- [Por que só no Brasil?](#por-que-só-no-brasil)
- [Status](#status-2026-11-15)
- [Versão, downgrade e backup](#versão-downgrade-e-backup)
- [Como funciona](#como-funciona-resumo)
- [Estrutura](#estrutura)
- [Guia rápido](#guia-rápido-do-zero-ao-root)
- [Início rápido](#início-rápido)
- [HTTPS, domínio e o instalador USB](#https-domínio-e-o-instalador-usb)
- [Portar para outro kernel](#portar-para-outro-kernel)
- [Aviso](#aviso)
- [Licença](#licença)

## Compatibilidade

| Item | Estado |
|---|---|
| Modelo | Qualquer **TV Roku com Ginga** no OS 15.0.4 — verificado no **H536X / Philco PTV24G5YR2CP** |
| Roku OS | **15.0.4** (build verificado: `15.0.4.5537`, `G1G.04E05537A`) |
| Kernel | **Linux 4.9.118 ARMv7**, sem KASLR (no H536X) |
| Região | **Brasil** — entrypoint Ginga/DTV Play (ISDB-Tb) |
| Root (GhostLock) | ✅ funciona no build do H536X |
| Outros modelos com Ginga/15.0.4 | 🧩 só portar o GhostLock para o kernel — `docs/PORTING.md` |
| Roku OS 16.0 | ❌ remove a cadeia de certificados usada no MITM |
| Instalação sem PC | 🧪 em pesquisa — o loader ainda abre TCP para o host |

## Por que só no Brasil?

O entrypoint é o app interativo HTML5 **Ginga/DTV Play** da TV digital
**ISDB-Tb** brasileira. A TV guarda apps de pendrive em `/ginga/installed/<id>/`
e os carrega pela entrada **Antena TV** — é esse caminho que entrega o
Chromium 104 rodando como `uid=500`, de onde parte o foothold. Rokus vendidos
fora do Brasil normalmente não expõem Ginga (usam outro middleware/entrada),
então o mesmo fluxo não existe. Portar para esses aparelhos exige **outro
entrypoint** (trabalho futuro), não só offsets novos: veja
`docs/PORTING.md`.

## Status (2026-11-15)

| Item | Estado |
|---|---|
| Root (uid=0) na TV | **Funciona** — GhostLock em `rt_mutex`, kernel 4.9.118 |
| Superroot (grsec off) | **Funciona por boot** — `/proc` completo, `mount`, `dmesg`, `chmod +s`, `mknod` (12 flags; `docs/SUPERROOT.md`) |
| DLNA no USB Media Player | **Por sessão** — bind do manifesto (`usbonly=0`) + cold start (`docs/RMP-DLNA.md`) |
| Shell root | **Funciona** — PTY em TCP 2222, cliente `tools/tvsh` |
| Instalador USB (app Ginga) | **Funciona** com o PC na mesma rede rodando os servidores |
| Gerenciador (8090) | **Funciona** — status, tvsh on/off, plugins, flags de dev mode |
| Site gerador de pacote | **Funciona** — zip montado 100% no navegador, PT/EN/ES |
| Site + instalador por HTTPS | **Funciona** — landing, `/installer`, `/manager`, `/api/*` no Vercel |
| Catálogo de kernels | **Funciona** — `/api/targets`, escolha de alvo no instalador |
| Root persistente após reboot | **Não resolvido** — ver `docs/RE.md` |
| Instalação sem PC | **Em pesquisa** — o loader nativo ainda abre TCP para o host |
| Roku OS 16.0 | **Bloqueia a cadeia** — remove a cadeia de certificados usada no MITM |

## Versão, downgrade e backup

- **Alvo**: **Roku OS 15.0.4.5537** (build `G1G.04E05537A`), kernel **Linux
  4.9.118 ARMv7**, no **H536X / PTV24G5YR2CP**. É o build verificado com
  offsets em [`kernels.json`](kernels.json); outros kernels entram como novas
  entradas (veja `docs/KERNELS.md` e `docs/PORTING.md`).
- **Downgrade**: o update oficial fica em <https://tvupdate.roku.com/>. Se ele
  ainda oferecer a imagem **15.0.4** para o seu modelo/serial, instale essa
  imagem. Existe a chance de a 15.0.4 continuar sendo servida mesmo com a 16.0
  já existente, mas **não é garantido** e depende da Roku.
- **Riscos**: um downgrade pode falhar, ser recusado pelo aparelho ou deixar a
  TV num estado inconsistente. Faça por sua conta e risco, só na **sua** TV.
  O **16.0** remove a cadeia de certificados usada no MITM (eMudhra) e pode
  quebrar o foothold de vez.
- **Backup (pedido)**: se você conseguir o `update.roku` da 15.0.4, espelhe o
  arquivo em um item público do **archive.org** (respeitando os termos da Roku;
  este repositório não hospeda nem linka o binário) e abra uma issue aqui
  dizendo que ele está espelhado. Isso deixa um backup caso a Roku remova a
  imagem.

## Como funciona (resumo)

1. **Pacote USB** — `site/` gera um zip com dois apps interativos
   (`ginga/installed/<id>/`) e ferramentas de PC (`ferramentas/`). Extraia na
   raiz do pendrive.
2. **Foothold uid=500** — a TV carrega o app Ginga (ou abre `/installer` no
   navegador); o instalador roda o estágio V8 (**CVE-2023-3079**) contra o
   Chromium 104 do RokuBrowser (sem seccomp). A página checa em **etapas**:
   Chrome (esperado 104) antes de instalar; depois do payload V8, o kernel —
   se o host não confirmar, ela abre a **lista de modelos/kernels** de
   `kernels.json` para escolher o alvo (ou "tentar mesmo assim").
3. **Root** — o agente uid=500 baixa `gx_bind.rld` do PC e executa o
   **GhostLock (CVE-2026-43499)** num processo filho: `task->cred = init_cred`,
   `task->fs = init_fs` (escape do chroot) e `bindsh 2222`.
4. **Gerenciador** — `miragemd` serve a UI em `127.0.0.1:8090` e a API JSON que o
   app usa para status, ligar/desligar o shell, listar plugins e gravar as flags
   de desenvolvedor no LinkDB.

## Estrutura

```
app/       manager.html (gerenciador) · installer.html (instalador)
site/      landing.html (site) · index.html (gerador USB) · template.html
api/       targets.js (catálogo) · payload.js (binários por alvo) · manager.js
lib/       data.js (kernels.json + scans de payload/)
payload/   <target-id>/ com binários publicados (opcional; veja o README)
assets/    logo.png · banner.png (wordmark do Miragem)
tools/     tvsh · tvcmd/tvget/tvput · auto_root · srvrld_multi · start_servers
           build.mjs (site) · dev_server.mjs · check_site.mjs · test_installer.mjs
           install_manager.sh (miragemd + srpatch na TV, persiste em /nvram)
           rmp_dlna.sh (DLNA no 2213 por sessão: bind do manifesto + cold start)
           miragemd.c (servidor local) · miragemd.arm (binário ARM)
exploit/   gx.c · gx_bind.c · rootagent.c · rootboot.c · mtdtool.c · pack.py
           srpatch.c (superroot: desliga as travas do grsecurity, por boot)
foothold/  página Ginga, addons mitmproxy, base do exploit V8, README
windows/   roku_root.bat (lançador Windows: mitmweb + servidores + tvsh)
vercel.json  build + output + rotas + headers do projeto na Vercel
docs/      QUICKSTART.md · INSTALL.md · SITE.md · RE.md · PORTING.md · KERNELS.md
           SUPERROOT.md · RMP-DLNA.md · LEGAL.md
kernels.json  tabela de kernels/offsets suportados (fonte de verdade)
CONTRIBUTING.md  regras para PR (kernel, payload, docs)
```

## Guia rápido (do zero ao root)

Se é a primeira vez, siga **[`docs/QUICKSTART.md`](docs/QUICKSTART.md)**: ele
mostra **quem puxa o quê** (o `gx_bind.rld` é buscado pelo agente da TV no PC,
porta 47190), o que compilar/rodar no PC, o que digitar na TV e os problemas
comuns. O resumo abaixo é para quem já conhece o fluxo.

## Início rápido

Pré-requisitos: TV no **15.0.4.5537** (não atualize para 16.0; veja
[Versão, downgrade e backup](#versão-downgrade-e-backup)), PC Linux na mesma
rede com Python 3, pendrive FAT32. Endereços usados nos exemplos:
host Linux `192.168.1.2`, TV `192.168.1.100`, Windows `192.168.1.3`.
Ajuste os placeholders para a sua rede.

```sh
# 0. preview do site (landing + instalador + API) sem Vercel
node tools/dev_server.mjs 3000     # http://localhost:3000

# 1. toolchain ARM (musl) e artefatos (PC = IP do PC na LAN)
TC=/caminho/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf
PC=192.168.1.2
$TC-gcc -O2 -static -no-pie -DHOST_IP="\"$PC\"" -DHOST_PORT=47140 \
  -Wl,-Ttext-segment=0x20000000 -o agent8 exploit/rootagent.c
cp agent8 rootagent7
$TC-gcc -O2 -static -no-pie -DHOST_IP="\"$PC\"" \
  -Wl,-Ttext-segment=0x10000 -o gx_bind exploit/gx_bind.c
$TC-gcc -O2 -static -no-pie -o miragemd tools/miragemd.c
python3 exploit/pack.py agent8 agent8.rld
python3 exploit/pack.py gx_bind gx_bind.rld

# alternativa: use o gx_bind publicado (com placeholder de IP) direto do site:
#   curl -fLO "https://<dominio>/api/payload?target=h536x-15.0.4.5537&file=gx_bind.rld&ip=$PC"

# 2. servidores no PC (agente uid=500, exploit, agente root, miragemd)
#    o IP do PC (ROKU_PC_IP) vai embutido no gx_bind; sem ele, o script
#    detecta pela rota até a TV
ROKU_TARGET=h536x-15.0.4.5537 ROKU_PC_IP=192.168.1.2 \
  ROKU_HOST=192.168.1.100 bash tools/start_servers.sh

# 3. shell root, quando o instalador terminar
ROKU_HOST=192.168.1.100 ./tools/tvsh      # Ctrl-] sai
```

No Windows: edite os placeholders de `windows/roku_root.bat` (usuário/IP do
Linux, IP da TV, IP do Windows) e dê duplo clique. No navegador, abra
`site/index.html` (ou o site publicado), envie o `MANIFEST` da TV e baixe
`miragem-usb.zip`.

Passo a passo completo: **`docs/QUICKSTART.md`** (quem puxa o quê, portas e
problemas comuns) e **`docs/INSTALL.md`**. Gerador e deploy:
**`docs/SITE.md`**. Superroot: **`docs/SUPERROOT.md`**. Achados de engenharia reversa e limites:
**`docs/RE.md`**. Portar para outro kernel: **`docs/PORTING.md`** e
**`docs/KERNELS.md`**.

## HTTPS, site e API

O site é publicado **a partir deste repositório** (Vercel: build
`node tools/build.mjs`, output `dist/`) e expõe:

| Rota | O que entrega |
|---|---|
| `/` | landing PT/EN/ES com compatibilidade, aviso e CTA |
| `/installer` | instalador completo **por HTTPS** (`?pick=1`, `?target=<id>`) |
| `/manager` | gerenciador (conversa com `127.0.0.1:8090` na TV) |
| `/gerador` | gerador do pacote USB, offline |
| `/api/targets` | catálogo de kernels + estado do payload (CORS `*`) |
| `/api/payload` | metadados do alvo / binário de `payload/<id>/` |
| `/api/manager` | `miragemd.arm`, `tvsh`, `tvcmd.py`… por HTTPS |

Merge na `main` → produção atualiza; PR → preview. Detalhes e deploy em
**`docs/SITE.md`**.

- **IP do PC**: o instalador pede o IPv4 do PC porque o `gx_bind.rld` é
  compilado com `HOST_IP` e conecta de volta nele (47190/47192) para buscar o
  agente root. O payload publicado em `payload/<id>/gx_bind.rld` usa o
  placeholder `255.255.255.255`; o site troca pelo IP
  (`/api/payload?...&ip=`) e o `start_servers.sh` faz o mesmo no PC. Sem IP
  certo o GhostLock roda e o root não sobe.
- **Limite atual**: o payload V8 vai embutido no instalador (nada de HTTP) e
  o estágio nativo (`gx_bind.rld`) ainda abre **TCP direto** para o PC. O que
  falta para instalar 100% remoto é o PC sair da jogada (o `gx_bind` buscar o
  agente root por HTTPS em vez de voltar na LAN).
- O endpoint de status do kernel (`GET http://HOST:PORT/status`,
  `?status=` no instalador) ainda não existe; sem ele o instalador abre a
  lista de alvos. Veja `docs/KERNELS.md`.

## Portar para outro kernel

O kernel é **específico de cada build**: `init_cred`, `tcp_wmem`, `runqueues` e
os offsets de `task_struct` mudam. Para outro modelo/build:

1. Obtenha o `update.roku` da **sua** TV em <https://tvupdate.roku.com/>.
2. Siga o prompt de **`docs/PORTING.md`** (extrair kernel, achar offsets,
   portar o gatilho, validar leitura/root/escape).
3. Abra um PR preenchendo `.github/PULL_REQUEST_TEMPLATE.md` e adicionando a
   entrada em [`kernels.json`](kernels.json). O schema está em
   `docs/KERNELS.md`.
4. Sem port, o instalador deve **recusar** o GhostLock em kernel desconhecido —
   não tente "forçar" offsets de outro build.

## Aviso

- Use **somente na sua TV**. A cadeia usa MITM de hosts de terceiros e um
  exploit de kernel: pode violar termos de uso e leis se usada em aparelho de
  outra pessoa. Hoje o fluxo é **Brasil-only** (Ginga/ISDB-Tb).
- As portas 2222/2323 ficam **sem autenticação** enquanto o agente roda. Rede
  confiável, e desligue quando não estiver usando.
- Offsets de kernel são **exclusivos** do build 15.0.4.5537 (veja
  `kernels.json`). Outro build = panic/corrupção; use `docs/PORTING.md`.
- Nada de firmware, `.pkg`/`.jwt`, certificados, chaves, serial/MAC ou
  credenciais neste repositório.

## Licença

**MIT** — veja [`LICENSE`](LICENSE). Política de segurança:
[`SECURITY.md`](SECURITY.md). Termos e responsabilidade:
[`docs/LEGAL.md`](docs/LEGAL.md).
