# Kernels suportados

Registro das builds de kernel em que o **GhostLock** tem offsets confirmados.
O arquivo de dados é **`kernels.json`** (raiz do repositório).

O Miragem roda em **qualquer TV Roku com Ginga no Roku OS 15.0.4**. O registro
começa com uma única entrada verificada: **15.0.4.5537** no **H536X / Philco
PTV24G5YR2CP**, kernel **4.9.118 ARMv7** sem KASLR. Outras TVs com Ginga no
15.0.4 entram como novas entradas em `kernels.json` (basta portar o GhostLock —
`docs/PORTING.md`); até lá, o instalador deve recusar o GhostLock em kernel
desconhecido.

> **EN (short):** `kernels.json` is the supported-kernel registry. Miragem runs
> on any Ginga Roku TV on OS 15.0.4; it starts with one verified entry,
> `h536x-15.0.4.5537` (Roku OS 15.0.4.5537, Linux 4.9.118, armv7, no KASLR).
> Other 15.0.4 Ginga models only need the GhostLock ported. The installer/exploit must select the entry that matches
> the device's reported OS/build (and, when available, the extracted kernel)
> and **refuse to run GhostLock on an unknown kernel**.

## 1. Schema (`schema: 1`)

```json
{
  "schema": 1,
  "updated": "AAAA-MM-DD",
  "targets": [
    {
      "id": "h536x-15.0.4.5537",
      "model": "H536X / PTV24G5YR2CP",
      "os": "15.0.4.5537",
      "build": "G1G.04E05537A",
      "kernel": "4.9.118",
      "arch": "armv7",
      "kimage": "no KASLR",
      "verified": ["read", "root", "chroot-escape"],
      "offsets": { "...": "0x..." }
    }
  ]
}
```

| Campo | Tipo | Uso |
|---|---|---|
| `id` | string | chave estável; use `<modelo>-<os>` em minúsculas (ex.: `h536x-15.0.4.5537`) |
| `model` | string | modelo comercial / codinome que a TV reporta |
| `os` | string | versão do Roku OS, como em `/etc/BUILD_PLID` ou no update |
| `build` | string | build interno do OS (opcional, mas recomendado) |
| `kernel` | string | `uname -r` / versão do Linux |
| `arch` | string | arquitetura (`armv7`, `aarch64`, ...) |
| `kimage` | string | propriedade do kernel: `no KASLR`, `KASLR`, ... |
| `verified` | array | o que foi testado nessa build: `read`, `root`, `chroot-escape` |
| `offsets` | object | tabela abaixo; valores hex em string para não perder formatação |

### 1.1 `offsets`

Todos os valores são hexadecimais de 32 bits (ARMv7). Endereços absolutos são
do espaço do kernel (`0x80000000`+); os demais são offsets de campo.

| Chave | Símbolo / campo | Onde é usado |
|---|---|---|
| `_stext` | início do kernel | sanidade / base |
| `init_cred` | `init_cred` | escrita `task->cred` / `real_cred` |
| `tcp_wmem_data` | campo `data` do sysctl estático `tcp_wmem` | leitura 4+4 via `/proc/sys/net/ipv4/tcp_wmem` |
| `__per_cpu_offset` | `__per_cpu_offset[0]` | achar a `runqueue` da CPU 0 |
| `runqueues` | `runqueues` (link) | base da `rq` |
| `rq_curr` | `rq->curr` | task atual (alvo da escalada) |
| `rq_idle` | `rq->idle` | `init_task`; de onde sai `init_fs` |
| `task_fs` | `task->fs` | escape do chroot (`task->fs = init_fs`) |
| `task_nsproxy` | `task->nsproxy` | validação de `mnt_ns->root` |
| `task_real_cred` | `task->real_cred` | escalada |
| `task_cred` | `task->cred` | escalada |
| `fs_root` | `fs->root` (path: mnt, dentry) | validação do chroot |
| `mnt_namespace_root` | `mnt_namespace->root` | `current_chrooted()` |
| `rb_erase` | `rb_erase` | alvo do primitivo de escrita |
| `current_chrooted` | `current_chrooted` | lógica do escape |
| `page_offset` | `PAGE_OFFSET` | validação de ponteiros |
| `task_size` | `TASK_SIZE` | validação de ponteiros |
| `thread_size` | `THREAD_SIZE` | validação de stack |

### 1.2 `superroot`

Opcional. Descreve como desligar as travas do **grsecurity** (superroot) nessa
build; sem esse bloco o root funciona e o superroot não. Os offsets são
**relativos a `grsec_block`** (o bloco de flags escrito no boot por
`grsecurity_init`).

```json
"superroot": {
  "method": "ghostlock-u32-write",
  "grsec_block": "0x8091e518",
  "grsec_lock": "0x8091e574",
  "apply": ["chroot_caps", "chroot_deny_mount", "chroot_deny_sysctl", "..."],
  "flags": { "chroot_findtask": "0x18", "chroot_deny_mount": "0x20", "...": "..." },
  "left_enabled": ["chroot_deny_fchdir", "chroot_deny_pivot", "tpe", "grsec_lock"],
  "verified": ["mount", "proc", "dmesg", "chmod", "mknod", "chroot", "unix", "shmat"],
  "note": "escrever 0 (u32) em grsec_block+offset, uma variavel por corrida"
}
```

| Campo | Uso |
|---|---|
| `method` | como aplicar (`ghostlock-u32-write`) |
| `grsec_block` | base do bloco de flags (`.data`) |
| `grsec_lock` | endereço de `grsec_lock` (fica ligado; bloqueia sysctl) |
| `apply` | lista de flags desligadas pelo `srpatch` |
| `flags` | nome → offset relativo à base |
| `left_enabled` | o que fica ligado de propósito |
| `verified` | o que já foi medido funcionando |

Regras de teste (aprendidas na prática): **uma flag por corrida**, escrever
**0**, validar pelo **sysctl** e pelo efeito (`mount`, `dmesg`, `/proc`) — não
use a leitura pela primitiva, porque ela escreve **duas** palavras
(`*(alvo)=valor` e `*(valor)=alvo-4`) e corrompe o vizinho. Veja
[`SUPERROOT.md`](SUPERROOT.md).

## 2. Estado atual (honesto)

- `kernels.json` é a **fonte de verdade** para novas builds.
- `exploit/gx.c` e `exploit/gx_bind.c` ainda compilam os offsets como `#define`
  (`K_INIT_CRED`, `K_TCPWMEM_DATA`, ...). Trocar a tabela exige recompilar o
  RLD; ainda **não** existe um passo de build que leia `kernels.json`.
- O instalador (`app/installer.html`) **não** lê o kernel da TV. Ele pergunta ao
  host (`GET http://HOST:PORT/status`), que deve responder
  `{"uid500":true,"os":"15.0.4.5537","kernel":"4.9.118","supported":true}`.
  Sem endpoint, o instalador mostra "não foi possível confirmar o kernel" e
  segue; com `supported:false`, bloqueia o GhostLock e pede o port.
- O endpoint no host ainda **não está implementado**; o comentário no instalador
  e a seção 7 de `docs/SITE.md` dizem o que falta (`tools/auto_root.py` /
  `miragemd` reportando OS/build/kernel).

## 3. Regra de seleção (para instalador e exploit)

1. Leia a build reportada pelo aparelho (ex.: `/etc/BUILD_PLID`,
   `/etc/build-info`, versão do OS no ECP). **Não** confie em
   `SoftwareUpdate/CurrVersion` no LinkDB: é só um valor gravado e não muda a
   versão real.
2. Procure em `kernels.json` a entrada cujo `os` (e `build`, quando existir)
   casa com o reportado. `model` sozinho **não** basta: o mesmo modelo pode
   receber builds diferentes.
3. Se o kernel extraído do update estiver disponível, confirme `kernel`,
   `build` e `arch` antes de usar.
4. Sem entrada que case: **não rode o GhostLock**. Mostre
   "kernel não suportado - veja docs/PORTING.md e envie o update.roku para
   portar". Um offset errado escreve em memória arbitrária e pode dar panic
   ou corromper a TV.
5. Uma build nova entra via PR: offsets preenchidos, leitura/escrita testadas,
   modelo/OS informados. Veja `.github/PULL_REQUEST_TEMPLATE.md`.

## 4. Adicionar uma build

O passo a passo (com o prompt para um agente) está em **`docs/PORTING.md`**.
Resumo:

1. Obtenha o `update.roku` da própria TV (https://tvupdate.roku.com/).
2. Extraia o kernel e ache os símbolos/offsets da tabela acima.
3. Porte o gatilho e valide `read`, `root` e o escape do chroot na TV.
4. Preencha a entrada em `kernels.json` e abra o PR.

Nada de firmware, `.pkg`/`.jwt`, chaves ou dumps no repositório. O PR leva só a
tabela e o log de teste.
