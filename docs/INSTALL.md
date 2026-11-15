# Instalação — fluxo USB, seletor de idioma e checagem de compatibilidade

Para o caminho mais curto (com o diagrama de quem puxa o quê), veja
**[`QUICKSTART.md`](QUICKSTART.md)**.

Este documento descreve o caminho **USB**: o pacote vira dois apps interativos
(Ginga/DTV Play) que a TV já sabe carregar, sem developer mode e sem trocar de
canal. O instalador precisa de um **PC na mesma rede** rodando os servidores do
Miragem; o gerenciador funciona depois que o root sobe.

> **Por que só no Brasil?** O entrypoint é o app interativo **Ginga/DTV Play**
> da TV digital **ISDB-Tb** brasileira: a TV guarda apps de pendrive em
> `/ginga/installed/<id>/` e os carrega pela entrada **Antena TV**. É esse
> caminho que entrega o Chromium 104 rodando como `uid=500`. Rokus vendidos
> fora do Brasil normalmente não expõem Ginga; esses aparelhos precisam de
> **outro entrypoint** (trabalho futuro), não só de offsets novos. Veja
> `docs/PORTING.md`.

Endereços de exemplo (troque pelos seus): host Linux `192.168.1.2`, TV
`192.168.1.100`, Windows `192.168.1.3`.

## 0. Dois caminhos para abrir o instalador

| Caminho | Como | Quando usar |
|---|---|---|
| **HTTPS (site)** | abra `https://<dominio>/installer` **no navegador da TV** | TV com rede/DNS normal; instalador e payload V8 vêm por HTTPS |
| **USB** | pacote `ginga/installed/<id>/` + app na Antena TV (seções 2–6) | TV sem acesso ao domínio, ou para guardar o pacote offline |

O site e o instalador são os mesmos nos dois caminhos; o zip do USB leva o
`installer.html` com o payload embutido. Em **ambos** o estágio nativo
(`gx_bind.rld`) ainda precisa do PC na mesma rede — veja a seção 8.

### IP do PC

O instalador pede o **IP do PC** antes de instalar, e isso não é decorativo:

- o `gx_bind.rld` é compilado com `HOST_IP` e, depois do GhostLock, **conecta
  de volta** no PC (47190 para baixar o agente root; 47192 para o agente root);
- o binário publicado traz `255.255.255.255` no lugar do IP; o site troca pelo
  IP informado (`/api/payload?...&ip=`) e o `start_servers.sh` faz a mesma
  troca no PC (`ROKU_PC_IP=...`, ou detecta pela rota até a TV);
- IP errado = GhostLock roda, mas o root não sobe (o `gx_bind` tenta conectar
  num endereço que não existe).

Informe o IP da interface do PC que alcança a TV (o mesmo que vai no
`ROKU_HOST` da seção 4). Dá para pré-preencher com `?pc=192.168.1.2`.

## 1. Requisitos

| Item | Requisito |
|---|---|
| TV | **TV Roku com Ginga** no OS **15.0.4** — verificado no **H536X** / Philco **PTV24G5YR2CP** (kernel 4.9.118 ARMv7) |
| Região | **Brasil** — o entrypoint é Ginga/DTV Play (ISDB-Tb); fora do Brasil, outro entrypoint é trabalho futuro |
| Entrada | **Antena TV** (DTV) com um canal que tenha Ginga/DTV Play |
| Pendrive | FAT32, com a pasta `ginga/` que a TV já cria ao guardar um app interativo |
| PC | Linux (ou Windows + SSH para um Linux) com Python 3, na mesma LAN |
| Rede | TV e PC na mesma sub-rede, sem isolamento de clientes (AP isolation) |

> Não aceite atualização de sistema. O Roku OS **16.0** remove a cadeia de
> certificados de que o foothold depende. Veja `docs/RE.md`.

## 2. Montar o pacote USB

### 2a. Pelo site (recomendado)

1. Na TV, com o pendrive conectado, use **Carregamento rápido** (ou
   "aplicativos interativos") em um canal com Ginga. A TV grava o app em
   `ginga/installed/<id>/`.
2. Leve o pendrive ao PC e pegue o arquivo `MANIFEST` dessa pasta.
3. Abra `site/index.html` (local) ou o site publicado, arraste/selecione o
   `MANIFEST` e clique em **Gerar pacote**.
4. O navegador monta o zip e baixa `miragem-usb.zip`:

```
miragem-usb.zip
├─ ginga/installed/<id1>/MANIFEST
├─ ginga/installed/<id1>/_carousel_metadata.json
├─ ginga/installed/<id1>/index.html        (Miragem — gerenciador)
├─ ginga/installed/<id2>/MANIFEST
├─ ginga/installed/<id2>/_carousel_metadata.json
├─ ginga/installed/<id2>/index.html        (Instalador do Miragem)
├─ ferramentas/tvsh
├─ ferramentas/tvcmd.py
└─ LEIA-ME.txt
```

O `MANIFEST` enviado só serve para copiar os dados do **seu** canal (`host`,
`serviceID`, `originalNetworkID`, etc.). O zip é gerado no navegador: nada é
enviado para servidor nenhum. Detalhes do gerador em `docs/SITE.md`.

### 2b. Manualmente

Use `app/manager.html` e `app/installer.html` como `index.html` de duas pastas
em `ginga/installed/`, cada uma com um `MANIFEST` e um
`_carousel_metadata.json` no mesmo formato do app que a TV gravou (troque o
`appName`, `appIds`, `appSize` e o `id`). O `id` é hexadecimal
(`host << 16 | appId`).

## 3. Copiar para o pendrive

Extraia o zip **na raiz** do pendrive, juntando com a pasta `ginga/` que já
existe (não substitua a pasta inteira). O resultado:

```
<USB>/ginga/installed/<id1>/...
<USB>/ginga/installed/<id2>/...
<USB>/ferramentas/tvsh
<USB>/ferramentas/tvcmd.py
```

`ferramentas/` é para o PC; a TV não usa.

## 4. Preparar o PC

Coloque os artefatos compilados no diretório de estado (padrão `/tmp/opencode`)
e suba os servidores:

```sh
export ROKU_STATE_DIR=/tmp/opencode
# artefatos esperados pelo start_servers.sh:
#   agent8.rld  gx_bind.rld  rootagent7  miragemd.arm
ROKU_TARGET=h536x-15.0.4.5537 ROKU_PC_IP=192.168.1.2 \
  ROKU_HOST=192.168.1.100 bash tools/start_servers.sh
```

| Porta | Serviço | Arquivo |
|---|---|---|
| 47127 | agente uid=500 carregado pela página Ginga | `agent8.rld` |
| 47140 | driver do auto-root (recebe o agente) | `auto_root.py` |
| 47190 | exploit GhostLock + bindsh 2222 | `gx_bind.rld` |
| 47192 | agente root (PTY) | `rootagent7` |
| 47193 | servidor local do gerenciador (8090) | `miragemd.arm` |
| 2222 | shell root (PTY) | `tvsh` |
| 2323 | agente de comando/transferência (opcional) | `tvget`/`tvput` |

`start_servers.sh` confere a porta contra `/proc/<pid>/cmdline` e reinicia quem
estiver servindo o arquivo errado; `--force` reinicia tudo, inclusive o
`auto_root.py` (que tem um flag de "já rodei").

Build dos artefatos: veja `README.md` e `exploit/pack.py`.

## 5. Na TV — abrir o instalador

1. Conecte o pendrive e entre em **Antena TV**.
2. Abra **Carregamento rápido** / "aplicativos interativos" (menu de opções do
   canal, quando o controle tiver o botão).
3. Escolha **Instalador do Miragem** (ou "Miragem Installer").

### Seletor de idioma (gate)

Na primeira abertura aparece um cartão em tela cheia:

```
miragem
Escolha o idioma · Choose your language · Elige tu idioma
[ Português ]  [ English ]  [ Español ]
```

- Navegação pelo controle: setas + OK/Enter.
- A escolha é gravada em `localStorage` (`miragem.lang`) e aplicada na hora
  (`data-i18n` no HTML, `data-i18n-html` quando o texto tem markup).
- O gate é só a primeira dobra; o idioma também pode ser trocado depois no
  cabeçalho (`PT · EN · ES`).
- Se o `localStorage` estiver bloqueado, o app cai em PT e não abre o gate.

### Checagem de compatibilidade (dois estágios)

**Estágio 1 — antes de instalar.** O app detecta o ambiente pelo
`navigator.userAgent`:

| Checagem | Regra | Mensagem se falhar |
|---|---|---|
| Motor | `Chrome/<n>` entre **100 e 109** (o alvo é 104.0.5112.70) | "Chrome {v} detectado (esperado 104). O exploit V8 pode não funcionar." |
| Firmware | `TV-<modelo>-<versão>` começando com **15.0.4** | "TV {v} detectada (esperado 15.0.4.5537 · kernel 4.9.118). Os offsets do GhostLock podem não bater." |

Se qualquer regra falhar, abre o modal **Compatibilidade** com a lista de
problemas e duas saídas:

- **Tentar mesmo assim** — fecha o modal e entra em "modo compatibilidade";
  a linha de status passa a mostrar `Chrome {c} · TV {f}`.

**Estágio 3 — depois do payload V8 (kernel).** O instalador pergunta ao host
(`GET http://HOST:PORT/status`) se o kernel é suportado. Três desfechos:

| Resposta | O que acontece |
|---|---|
| `supported: true` | segue para o root |
| `supported: false` | modal **Kernel não suportado** com a lista de modelos/kernels publicados |
| sem resposta | modal **Kernel não confirmado** com a mesma lista |

Na lista (vinda de `/api/targets` ou embutida no build), cada alvo mostra
modelo, OS, kernel, arch e se há payload publicado — e, quando o `gx_bind` do
alvo é personalizável por IP, a nota fica "payload pronto para <IP do PC>". **Usar este payload**
grava `miragem.target` e continua; **Tentar mesmo assim** segue sem confirmar;
**Cancelar** volta. Para abrir a lista direto: `?pick=1`; para pré-selecionar
um alvo: `?target=<id>`.
- **Cancelar** — fecha o modal, desabilita o botão e mostra "Instalação
  bloqueada. Atualize o Miragem para esta versão da TV."

**Estágio 2 — depois do payload V8.** A página **não** consegue ler o kernel
da TV (o processo roda como `uid=500` e dentro do chroot; `/proc/version` não
prova qual imagem de kernel bootou). Por isso ela consulta um endpoint de
status no host:

```
GET http://HOST:PORT/status
{ "uid500": true, "os": "15.0.4.5537", "kernel": "4.9.118", "supported": true }
```

- `supported:true` → linha "kernel suportado · 15.0.4.5537 · 4.9.118" e o
  fluxo continua normalmente.
- `supported:false` → "user 500 OK, kernel não suportado - GhostLock não vai
  rodar; veja docs/PORTING.md e envie o update.roku para portar", com
  **Tentar de novo**.
- endpoint ausente/offline → estado neutro "não foi possível confirmar o
  kernel" e o fluxo continua (a checagem é informativa, não bloqueante nesse
  caso).

O endpoint é configurável por `?status=http://HOST:PORT/status` ou
`localStorage['miragem.status']`. **No host ele ainda não existe**: falta o
`auto_root.py`/`miragemd` relatar `uid500`, OS/build e kernel. Enquanto isso,
o instalador cai no estado neutro. O contrato está documentado em
`docs/KERNELS.md` e no comentário de `app/installer.html`.

Observações:

- UA sem `Chrome/` (por exemplo, um navegador de teste no PC) conta como
  "desconhecido" e **não** bloqueia; o mesmo vale para UA sem `TV-...`.
- As duas checagens são heurísticas. A única prova real é a build reportada
  pelo aparelho + a tabela de `kernels.json`; offsets de outro build **vão**
  falhar. Sem port, use `docs/PORTING.md`.

## 6. Instalar

1. Aperte **OK** no controle (ou clique em **Instalar**).
2. O instalador injeta o estágio embutido (`<script type="text/plain"
   id="payload">`) via `eval`, executa o exploit V8 e carrega o estágio nativo
   (RLD). Os status aparecem em quatro linhas: **Página carregada** →
   **Executando o instalador** → **Conferindo o kernel** → **Aguardando o
   root**. A linha do kernel pode ficar neutra se o host não tiver o endpoint
   de status (veja a seção 5).
3. O app consulta `http://127.0.0.1:8090/api/status` a cada 3 s, por até 90 s:
   - `root && tvsh` → "Root ativo · shell na 2222 · gerenciador pronto";
   - só `root` → aviso "Root ativo, mas o shell não respondeu";
   - nada → "Não confirmei o root" e o botão volta para **Tentar de novo**.
4. Se o root já estava ativo, a tela mostra "Root já ativo · shell na 2222".

Se falhar: confira se o PC está ligado, se as cinco portas estão no ar
(`ss -lntp`), se a TV e o PC estão na mesma sub-rede, e se o `MANIFEST` usado
era daquele canal. O log do `auto_root.py` (`auto_root.log`) mostra o que o
agente uid=500 pediu.

## 7. Gerenciador

Com root no ar, `miragemd.arm` serve o gerenciador em `127.0.0.1:8090`
(embutido no app). Telas: **Início** (root/tvsh/dev), **Serviços** (ligar e
desligar o `tvsh`), **Aplicativos** (lista `/nvram/plugins`), **Ajustes**
(flags de dev mode).

Do PC:

```sh
ROKU_HOST=192.168.1.100 ./tools/tvsh          # shell root, Ctrl-] sai
ROKU_HOST=192.168.1.100 ./tools/tvcmd.py "id; uname -a"
```

> O shell e o agente 2323 são **sem autenticação**. Rede confiável e desligue
> quando terminar (`/api/tvsh/stop` no gerenciador).

## 8. Limites conhecidos

- Root **não sobrevive** a um reboot: o bootloader reescreve o ambiente
  U-Boot e o payload nativo não executa (PaX MPROTECT / mounts `noexec`).
  Reabra o instalador depois de ligar a TV. Ver `docs/RE.md`.
- A instalação depende do PC: o loader nativo abre TCP direto para o host e o
  `gx_bind` usa o IP do PC (informado no instalador). O site, o instalador, o catálogo e
  os utilitários do gerenciador já são servidos por **HTTPS** (`docs/SITE.md`);
  falta só tirar o IP do binário para a instalação ser 100% remota.
- `payload/<id>/` começa vazio: publique binários só se quiser (veja
  `payload/README.md`); o alvo funciona igual com os offsets de
  `kernels.json`.
- Roku OS 16.0 quebra o foothold (cadeia de certificados).
