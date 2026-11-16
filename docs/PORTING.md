# Porting do GhostLock para outro kernel

Este documento explica como portar o **GhostLock (CVE-2026-43499)** do Miragem
para outro modelo/build de Roku. Ele foi escrito para ser entregue a um agente
de IA junto com o `update.roku` da **sua** TV: copie o bloco
[PROMPT](#3-prompt-para-o-agente-copie-o-bloco) e preencha os `<...>`.

> **EN (short):** Miragem targets **Roku TVs with Ginga on OS 15.0.4**; the
> verified reference build is the **H536X / Philco PTV24G5YR2CP** (15.0.4.5537,
> Linux **4.9.118** ARMv7, **no KASLR**). Only the GhostLock kernel offsets are
> model-specific. To port to another kernel: get that TV's own `update.roku`,
> extract the kernel, find the offsets listed in `exploit/README.md`, port the
> trigger in `exploit/gx.c`, validate read/write on the device, and open a PR
> adding a `kernels.json` entry. A different build needs different offsets; a
> wrong write can panic the TV.

## 1. O que é específico da build

O Miragem é feito para **qualquer TV Roku com Ginga no Roku OS 15.0.4**. O que
é compartilhado entre esses aparelhos:

| Parte | Compartilhado? |
|---|---|
| Entrypoint (app Ginga/DTV Play em `ginga/installed/<id>/`) | ✅ sim, em TVs com Ginga |
| Estágio do navegador (Chromium 104) até **uid 500** | ✅ em builds 15.0.4 com o mesmo navegador |
| Agente/gerenciador (`miragemd`, `tvsh`) | ✅ sim |
| **Offsets do GhostLock** (`init_cred`, `tcp_wmem`, `runqueues`…) | ❌ **por kernel/build** |

O kernel **não** é genérico: os offsets abaixo valem só para o build de
referência (o único verificado até agora):

| Item | Valor |
|---|---|
| Modelo | Roku **H536X** / Philco **PTV24G5YR2CP** |
| Roku OS | **15.0.4.5537** (build `G1G.04E05537A`) |
| Kernel | **Linux 4.9.118** `#2 SMP PREEMPT armv7l` (MStar mt5867) |
| Arquitetura | ARMv7 (32 bits) |
| KASLR | **não** (imagem carregada em endereço fixo) |
| `_stext` | `0x80100000` |
| vmlinux | privado, **não** versionado (o build do update é a fonte) |

O GhostLock usa endereços absolutos (`init_cred`, `tcp_wmem`, `runqueues`) e
offsets de `task_struct`/`fs_struct` (`task->cred`, `task->fs`, ...). Mudou o
kernel, mudou o layout: os offsets antigos escrevem em memória arbitrária e
podem dar **panic** ou corromper a TV. A tabela completa está em
`exploit/README.md` e em `kernels.json` (entrada `h536x-15.0.4.5537`).

## 1.1 Portar é **duas** coisas: root e superroot

O mesmo GhostLock serve para o root e para o **superroot** (desligar as travas
do grsecurity: `mount`, `/proc` completo, `dmesg`, `chmod +s`, `mknod`…). Se o
kernel é novo, você precisa dos dois conjuntos de endereços:

| Conjunto | O que é | Onde vai |
|---|---|---|
| **root** | `init_cred`, `tcp_wmem`, `runqueues`, offsets de `task_struct`/`cred`/`fs` | `kernels.json` → `offsets` |
| **superroot** | bloco de flags do grsecurity (`chroot_findtask`, `chroot_deny_mount`, `chroot_caps`, …) | `kernels.json` → `superroot` |

Sem o bloco do grsec o root funciona, mas o superroot não. Um endereço errado
pode derrubar a TV (a corrida já é instável por si) — por isso o guia pede os
dois e a validação é passo a passo. Detalhes e a lista do H536X:
[`SUPERROOT.md`](SUPERROOT.md).

Como achar o bloco no kernel novo: localize a `ctl_table` do grsecurity
(entradas de `0x24`, nomes `chroot_*`, handler `proc_dointvec_secure`),
confirme com as stores de `grsecurity_init` no boot e com os xrefs
(`gr_handle_chroot_mount`, `chroot_caps`/`GR_CHROOT_CAPS`,
`gr_handle_chroot_sysctl`). Anote base + offset de cada flag.

## 2. Obtenha o kernel da sua TV

1. Peça o update da **sua própria TV** em <https://tvupdate.roku.com/> (o site
   pede modelo/serial e entrega o `update.roku` correspondente). Se o fluxo da
   TV baixar o arquivo, ele também pode ser capturado no PC/proxy durante a
   checagem de atualização.
2. Guarde o `update.roku` original; **não** altere nem faça flash dele.
3. Opcional: extraia o kernel antes de entregar ao agente (`binwalk`,
   `unsquashfs`, `ubi*`, `dd`). O container varia por geração; o agente deve
   identificar o formato e dizer onde parou se não achar.
4. Confira a versão **reportada pela TV** (`/etc/BUILD_PLID`,
   `/etc/build-info`, `uname -a`). `SoftwareUpdate/CurrVersion` no LinkDB não
   serve: é um valor gravado, não a versão real.

## 3. PROMPT para o agente (copie o bloco)

Copie o bloco abaixo, preencha os `<...>` e entregue junto com o `update.roku`.
Ele assume um agente com acesso ao repositório e a uma TV na mesma rede.

```text
Você é um agente de engenharia reversa e desenvolvimento C/ARM. Trabalhe em um
fork do repositório Miragem e porte o GhostLock (CVE-2026-43499) para o kernel
da MINHA TV. É pesquisa no próprio aparelho: nada de firmware ou dados de
terceiros no repositório.

CONTEXTO DO PROJETO
- Build de referência (verificada): Roku H536X / Philco PTV24G5YR2CP, Roku OS
  15.0.4.5537 (build G1G.04E05537A), Linux 4.9.118 ARMv7 (MStar mt5867), sem
  KASLR, _stext 0x80100000.
- O Miragem roda em qualquer TV Roku com Ginga no OS 15.0.4; só os offsets do
  GhostLock mudam. Meu kernel está em anexo/à mão: extraia dele e porte.
- Cadeia: foothold uid=500 (Chromium 104 + CVE-2023-3079) -> GhostLock
  (corrida de futex PI/rt_mutex) -> task->cred = init_cred e
  task->fs = init_fs (escape do chroot) -> agente root/PTY.
- Fontes: exploit/gx.c, exploit/gx_bind.c, exploit/rootagent.c,
  exploit/README.md, docs/RE.md e kernels.json.
- A tabela de offsets do alvo atual está em kernels.json
  (id h536x-15.0.4.5537). NÃO reuse esses valores em outro kernel.

ENTRADA
- update.roku da MINHA TV: <caminho do arquivo>.
- Modelo / OS / build reportado pela TV: <modelo> / <versao> / <build>.
- (Opcional) vmlinux/AImage já extraído: <caminho>.

TAREFAS
1. Identifique a build. Extraia o update.roku (procure squashfs, UBI, cpio;
   ferramentas: binwalk, unsquashfs, ubidump, dd). De /etc/BUILD_PLID e
   /etc/build-info tire modelo, versão do OS e build. Confirme arquitetura e
   versão do kernel. Se o kernel não estiver no container, diga exatamente
   onde parou e o que falta.
2. Extraia o kernel (vmlinux ELF, AImage ou zImage). Se for zImage, localize o
   payload descomprimido e reconstrua uma imagem endereçável para análise.
   NÃO faça flash de nada.
3. Ache os símbolos/offsets usados pelo exploit:
   _stext, init_cred, tcp_wmem (campo data do sysctl estático),
   __per_cpu_offset, runqueues, rq->curr, rq->idle, task->fs,
   task->nsproxy, task->real_cred, task->cred, fs->root,
   mnt_namespace->root, rb_erase, current_chrooted, PAGE_OFFSET,
   TASK_SIZE, THREAD_SIZE.
   Use nm/readelf/objdump/gdb + DWARF/BTF quando houver; senão, assinatura de
   código e strings. Explique o método de cada offset.
4. Confirme que o bug existe nesse kernel. Compare kernel/futex.c e
   kernel/locking/rtmutex.c (linhagem 4.9.x) com o alvo: FUTEX_CMP_REQUEUE_PI,
   FUTEX_WAIT_REQUEUE_PI, pi_blocked_on e a cadeia
   rt_mutex_dequeue -> rb_erase. Se o kernel já tiver o fix, PARE e reporte.
5. Porte o gatilho. Ajuste syscalls/ABI ARM, offsets de task_struct, endereços
   estáticos e validações em exploit/gx.c (e gx_bind.c). Mantenha o fluxo:
   leitura 4+4 -> root -> escape do chroot -> fork -> agente. Deixe os offsets
   numa tabela (C ou JSON), não espalhados no meio do código.
6. Valide na TV, nesta ordem:
   a) leitura: `run /tmp/gx_new.rld read <addr>` e confira o 4+4;
   b) root: `run /tmp/gx_new.rld root`, confira uid=0 e o shell em 2222
      (tools/tvsh);
   c) escape: `run /tmp/gx_new.rld esc`, confira o root real e o agente.
   Observe a sessão root já aberta (tvsh). NÃO reinicie a TV para testar: cada
   dança deixa futex presos e o reboot perde o root.
7. Porte o **superroot** também (mesmo GhostLock, outra tabela). Localize a
   ctl_table do grsecurity (entradas de 0x24, nomes chroot_*, handler
   proc_dointvec_secure), confirme com as stores de grsecurity_init no boot e
   com os xrefs gr_handle_chroot_mount / gr_handle_chroot_sysctl /
   chroot_caps (GR_CHROOT_CAPS). Anote base + offset de cada flag
   (chroot_findtask, chroot_deny_mount, chroot_caps, chroot_deny_chmod,
   chroot_deny_mknod, chroot_deny_unix, chroot_deny_shmat, ...) e teste uma
   por vez com exploit/srpatch.c. Veja docs/SUPERROOT.md.
8. Entregue patch + evidência:
   - versão/build do kernel e como você a obteve;
   - tabela de offsets preenchida no formato de kernels.json, com o método,
     incluindo o bloco `superroot` (base do grsec + offset de cada flag);
   - log de teste (comandos + saída de read/root/esc, sem dados sensíveis);
   - diff de exploit/gx.c, exploit/gx_bind.c e uma entrada nova em
     kernels.json;
   - PR seguindo .github/PULL_REQUEST_TEMPLATE.md.

SAÍDAS OBRIGATÓRIAS (não pule)
- Kernel: versão, build, arquitetura, KASLR (sim/não) e origem (update.roku).
- Offsets: tabela completa (root) e bloco `superroot`; cada valor com
  evidência (símbolo/assinatura/xref).
- Teste: log de leitura e de root/escape; diga também o que NÃO funcionou.
- Riscos: o que pode dar panic e o que fica corrompido até o reboot.

SEGURANÇA E LIMITES
- Só na MINHA TV. Não rode em aparelho de terceiros.
- Não faça flash/atualização de firmware, não escreva em partição, não apague
  NVRAM e não reinicie a TV durante o teste.
- Não publique firmware, .pkg/.jwt, chaves, certificados, serial/MAC ou dumps.
  O PR leva só offsets, patch e log.
- Se não tiver certeza de um offset, PARE e reporte. Offset errado = panic ou
  corrupção. O primitivo de escrita também grava target-4 em `value`; escolha
  alvos com vizinho descartável.
```

## 4. O que o agente deve devolver

Antes de aceitar o port, confira:

1. **Kernel** — versão, build, `arch`, KASLR e de onde saiu (update.roku,
   vmlinux). Sem isso a tabela não é verificável.
2. **Offsets** — a tabela completa (`exploit/README.md`), cada linha com o
   método (DWARF, assinatura, leitura validada). Nada de "chute".
   **Superroot**: a base do bloco grsec e o offset de cada flag, com o
   `grsecurity_init`/ctl_table como evidência.
3. **Log de teste** — `read` com 4+4 coerente, `uid=0`, `pwd=/` e `ls /` depois
   do escape. O log não pode ter serial/MAC/IP público desnecessário.
4. **Patch** — offsets centralizados, `gx.c`/`gx_bind.c` compilando estático,
   e a entrada nova em `kernels.json` (schema em `docs/KERNELS.md`).

## 5. Testar na TV (sessão root, tvsh, sem reboot)

O teste usa o mesmo caminho do fluxo normal: o **agente uid=500** baixa e
executa o RLD; a sessão root (`tools/tvsh`, porta 2222) serve para observar.

```sh
# 1. suba o RLD novo para a TV (agente 2323; ajuste o IP no script)
python3 tools/tvput.py /tmp/gx_new.rld gx_new.rld

# 2. no agente uid=500 (loader RLD), como em tools/auto_root.py:
#    fetch 47190 /tmp/gx_new.rld
#    run /tmp/gx_new.rld read 0x8092f2b8
#    run /tmp/gx_new.rld root
#    run /tmp/gx_new.rld esc

# 3. observe pela sessão root
ROKU_HOST=192.168.1.100 ./tools/tvsh      # Ctrl-] sai
python3 tools/tvcmd.py "id; uname -a; pwd; ls /"
```

Regras do teste:

- **Não reinicie a TV.** O root não é persistente (veja `docs/RE.md`) e o reboot
  apaga o root e o estado da sessão. Teste tudo numa sessão só.
- Se o `read` falhar ou vier lixo, **pare** antes de tentar `root`/`esc`.
- Se o `root` funcionar mas o escape não, o problema é `task->fs`/`init_fs`
  (releia `rq->idle` e o offset `task_fs`).
- Anote o footprint: cada dança custa ~1 s e deixa futex presos.

## 6. O que NÃO fazer

- **Não** faça flash de firmware, `dd` em partição, erase de NVRAM nem
  `mtdtool write` durante o port. O experimento de U-Boot já mostrou que o
  bootloader reescreve o ambiente (veja `docs/RE.md`).
- **Não** aceite/instale o Roku OS **16.0** para "testar": ele bloqueia a
  cadeia de certificados usada no MITM/eMudhra. Se a TV oferecer 16.0, não
  atualize.
- **Não** rode o GhostLock em build desconhecida. O instalador deve recusar:
  veja `docs/KERNELS.md` e o estágio de kernel em `app/installer.html`.
- **Não** commite firmware, `.pkg`/`.jwt`, chaves, certificados, dumps ou
  credenciais. O `.gitignore` cobre os binários, mas revise o `git status`.

## 7. Abrir o PR

1. Preencha `.github/PULL_REQUEST_TEMPLATE.md` (fonte do kernel, offsets,
   testes, modelo/OS, sem binários).
2. Inclua a entrada em `kernels.json` com `id` no formato
   `<modelo>-<os>` e `verified` com o que passou.
3. Anexe/cole o log de teste no PR (sem dados sensíveis).
4. Se você só tem o `update.roku` e ainda não portou, abra uma issue com
   `.github/ISSUE_TEMPLATE/kernel-port.yml` para combinar o trabalho.
