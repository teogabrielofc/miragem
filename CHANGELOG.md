# Changelog

Formato inspirado no [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/).
O projeto usa versionamento semântico; cada tag `vX.Y.Z` deste repositório
corresponde a um estado testado no H536X (Roku OS 15.0.4.5537).

## v0.3.0 — 2026-11-16

### Adicionado

- **Tela no PC (mirror)**: `tools/fbserve.c`/`fbserve.arm` lê a UI (`/dev/fb1`)
  por `mmap`, respeita o double buffer (`yoffset`) e envia só os tiles 64×64
  alterados por TCP 2331 (protocolo `FBS2`, sem áudio). `tools/tvscreen.py`
  mostra no PC (ffplay ou Tk) e salva PNG (`--shot`); `tools/screen.sh` faz
  push/start/stop/shot. Limite: só a UI (fb1); vídeo (`fb0`) não aparece
  (`docs/SCREEN.md`).
- **App custom (ZIP)** por sessão: `tools/zipapp.sh` (`push`, `apply`,
  `status`, `restore`), helpers na TV (`zipapp_apply.sh`, `mqfd`, `ecp`) e
  `tools/zipapp_sample/`. Preserva o manifest/token do slot, monta o ZIP via
  ZipMountServer, invalida `mod_cache/<id>.*`, mata só o `SdkLauncher` do app
  e lança por ECP (relay `tools/ecp_relay.py` na 8092 quando o loopback dá 403).
- **DLNA no USB Media Player (2213)** por sessão: bind do manifest
  (`usbonly=0`) + cold start seletivo, `tools/rmp_dlna.sh` e endpoints no
  gerenciador (`/api/dlna`, `/api/dlna/apply|restore`).
- **Superroot no gerenciador**: botão em *Ajustes → Superroot → Sessão
  (runtime)* (`POST /api/superroot/apply/...`), `srpatch` persistido em
  `/nvram/miragem` e restaurado por `install_manager.sh`; suporte a
  `grsec_lock`/sysctl e a `lock_optional`/endereço do `kernels.json`.
- Gerenciador: endpoints `/api/zipapp` e `/api/zipapp/apply|restore/<slot>`;
  UI PT/EN/ES para ZIP, DLNA e superroot; verificação de `grsec_lock` por
  sysctl (evita falso negativo).
- Docs: `docs/SCREEN.md`, `docs/SUPERROOT.md`, `docs/RMP-DLNA.md`; README com
  status, estrutura e uso das ferramentas; `api/manager.js` publica
  `fbserve.arm`, `screen.sh`, `tvscreen.py` e os helpers.

### Corrigido

- Ordem do restore do superroot (gate antes de caps/lock).
- Superroot respeita `lock_optional` e o endereço do alvo no catálogo.
- `install_manager.sh` reinstala `miragemd`/UI a cada root e evita binário
  antigo servido pelo instalador (porta 47193).

## v0.2.0 — 2026-11-15

### Adicionado

- **Superroot** (`srpatch`): desliga as travas do grsecurity por boot
  (`chroot_findtask`, `grsec_lock`), com bloco `grsec` no `kernels.json`,
  documentação de portabilidade e `docs/PORTING.md`.
- Gerenciador: tela de superroot, botão de aplicar e persistência do
  `srpatch` em `/nvram/miragem`.

### Corrigido

- Ajustes de logo/banner e navegação do site/gerador.

## v0.1.0 — 2026-11-15

### Adicionado

- Publicação inicial: instalador USB (Ginga) + gerador de pacote no navegador,
  landing PT/EN/ES, rotas `/installer`, `/manager`, `/gerador`,
  `/kernels.json` e API de payload/catálogo no Vercel.
- Gerenciador local (`miragemd`, porta 8090) com status, `tvsh` on/off,
  plugins e modo desenvolvedor; shell root em TCP 2222 (`tools/tvsh`).
- Toolchain ARM, scripts de servidores no PC e documentação de instalação,
  portabilidade, kernels e aspectos legais.
