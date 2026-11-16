# Superroot — desligar as travas do grsecurity (por boot)

O **superroot** é o root (GhostLock) + um conjunto de escritas de 4 bytes em
`.data` que desligam a maioria das restrições de `chroot` do **grsecurity**
embutido no kernel da Roku. Com ele, coisas que davam `EPERM`/`EACCES` passam a
funcionar: `mount`, `dmesg`, `chmod +s`, `mknod`, `chroot`, `/proc` completo
(todos os processos), etc.

> Vale **por boot**. Um reboot restaura tudo — é o rollback natural.
> Continua sendo **GhostLock** por baixo: a mesma corrida de futex do root.
> Se ela falhar feio, a TV reinicia (root cai; reaplique o Miragem).

## 1. Pré-requisitos

1. Root ativo (uid=0) pelo GhostLock — veja `docs/INSTALL.md` / `docs/QUICKSTART.md`.
2. O kernel da TV **portado** (offsets do GhostLock **e** do bloco grsec).
   Para o H536X / 15.0.4.5537 já está em [`kernels.json`](../kernels.json).

## 2. O que é desligado (H536X / 15.0.4.5537)

Bloco de flags do grsec: base **`0x8091e518`** (tabela `ctl_table` em
`0x80946264`, 27 entradas de `0x24`, handler `proc_dointvec_secure`).
Cada flag é um `int` (4 bytes); escrever **0** desliga.

| Variável | Endereço | O que libera |
|---|---|---|
| `chroot_findtask` | `0x8091e530` | `/proc` completo (todos os pids, kernel threads) |
| `chroot_deny_unix` | `0x8091e534` | sockets unix fora do chroot |
| `chroot_deny_mount` | `0x8091e538` | `mount`/`umount` |
| `chroot_deny_shmat` | `0x8091e540` | SysV `shmat` |
| `chroot_deny_chroot` | `0x8091e544` | `chroot()` |
| `chroot_enforce_chdir` | `0x8091e54c` | `chdir` para fora do root |
| `chroot_deny_chmod` | `0x8091e550` | `chmod +s` |
| `chroot_deny_mknod` | `0x8091e554` | `mknod` |
| `chroot_restrict_nice` | `0x8091e558` | `setpriority`/nice |
| `chroot_caps` | `0x8091e55c` | capacidades (`CAP_SYS_ADMIN`, `CAP_SYSLOG`…) — `dmesg` volta |
| `chroot_deny_bad_rename` | `0x8091e560` | `rename` para fora |
| `chroot_deny_sysctl` | `0x8091e564` | libera o gate de sysctl (o write em si continua bloqueado por `grsec_lock=1`) |

Ficam **ligados** por padrão: `chroot_deny_fchdir` (`0x8091e53c`),
`chroot_deny_pivot` (`0x8091e548`), `tpe`/`tpe_restrict_all`, `grsec_lock`
(`0x8091e574`), `dmesg`, `linking_restrictions`, `deter_bruteforce`,
`fifo_restrictions`, `rwxmap_logging`.

### 2.1 `grsec_lock` é opcional (e vale a pena)

`grsec_lock=1` não impede as escritas de memória — só bloqueia o caminho
**sysctl** (`echo` em `/proc/sys/kernel/grsecurity/*`). Desligá-lo custa **uma**
escrita (`0x8091e574 ← 0`) e libera todo o resto por sysctl, **sem mais
corridas**:

```sh
/tmp/srpatch w 0x8091e574 0
cat /proc/sys/kernel/grsecurity/grsec_lock          # 0
echo 0 > /proc/sys/kernel/grsecurity/chroot_deny_fchdir
echo 0 > /proc/sys/kernel/grsecurity/chroot_deny_pivot
```

Testado no H536X em 2026-11-16: a primeira corrida pode não completar (sem
crash, valor fica 1); a segunda aplica. Depois do lock=0, `echo` num flag
(com `chroot_deny_sysctl=0` e `chroot_caps=0`) retorna 0 — dá para terminar o
conjunto inteiro com **1 corrida** em vez de N. Custo: grsec destravado até o
reboot (qualquer root pode mexer) e a Roku pode reafirmar o lock em algum boot
(não observado).

## 3. Como aplicar

O binário é [`exploit/srpatch.c`](../exploit/srpatch.c) (estático ARMv7):

```sh
TC=/caminho/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf
$TC-gcc -O2 -static -no-pie -o srpatch exploit/srpatch.c
```

Leve para a TV (o Miragem já tem esse caminho) e use **um passo por vez**:

```sh
/tmp/srpatch status                 # lê os valores (sysctl; sem escrita)
/tmp/srpatch step chroot_caps       # 0x8091e55c: 1 -> 0  (dmesg volta)
/tmp/srpatch step chroot_deny_mount # 0x8091e538: 1 -> 0  (mount funciona)
/tmp/srpatch step chroot_deny_sysctl
/tmp/srpatch step chroot_deny_chmod
/tmp/srpatch step chroot_deny_chroot
/tmp/srpatch step chroot_deny_mknod --no-prim
/tmp/srpatch w 0x8091e534 0         # unix
/tmp/srpatch w 0x8091e540 0         # shmat
/tmp/srpatch w 0x8091e530 0         # /proc completo
/tmp/srpatch restore                # volta o que foi salvo (ou reboot)
```

Regras aprendidas na prática:

- **uma variável por corrida**; nunca em lote;
- **não use leitura pela primitiva** (`check`/`step` com pré-leitura) para
  validar: a primitiva escreve **duas** palavras — `*(alvo)=valor` **e**
  `*(valor)=alvo-4`. A “leitura” acaba corrompendo o vizinho (`alvo-4`).
  Valide pelo **sysctl** (`/proc/sys/kernel/grsecurity/<nome>`) e pelo efeito;
- escrita com `valor=0` faz só a primeira palavra (segura);
- `grsec_lock=1` impede `echo` em `/proc/sys/kernel/grsecurity/*`, mas as
  escritas de memória continuam valendo. Se quiser o caminho sysctl, desligue
  o lock primeiro (uma escrita) — veja §2.1;
- se a TV reiniciar, pare e reporte o último passo; não repita o endereço.

## 4. O que passa a funcionar (medido)

```
mount -t tmpfs ...   0        umount 0
mount -t proc ...    0        /proc = 337 pids, /proc/1/comm = init
dmesg / klogctl      ok       (CAP_SYSLOG restaurado)
chmod 4755           0
mknod c 1 3          0
chroot               ok
```

## 5. Portar para outro kernel (obrigatório junto com o root)

O `srpatch` usa os mesmos endereços absolutos do GhostLock — então **portar o
GhostLock agora é duas coisas**:

1. os offsets do `task_struct`/`cred`/`fs` (root);
2. os endereços das flags do **grsec** (superroot), que mudam por build.

Como achar no kernel novo (sem KASLR):

- localize a `ctl_table` do grsecurity (entradas de `0x24`, nomes
  `chroot_*`, handler `proc_dointvec_secure`);
- confirme com `grsecurity_init` (as stores de `1`/`100` no boot);
- confirme os xrefs: `gr_handle_chroot_mount`, `gr_handle_chroot_sysctl`,
  `gr_chroot_is_capable` (máscara `GR_CHROOT_CAPS`), etc.;
- preencha o bloco `superroot` em [`kernels.json`](../kernels.json) com os
  endereços/offsets e teste **um por um** numa TV própria.

Sem os endereços do bloco grsec, o root funciona e o superroot não — e um
endereço errado pode derrubar a TV (a corrida já é instável por si).

## 6. Integração no gerenciador (implementada)

O `miragemd` (porta 8090) expõe:

| Rota | O que faz |
|---|---|
| `GET /api/superroot` | `{ok, active, findtask, lock, pids, pid1, binary, path}` |
| `POST /api/superroot/apply/lock/<addr>` | **rápido**: 3 corridas (lock, caps, gate de sysctl) e o resto por `echo`; `<addr>` é o `grsec_lock` do alvo (vem do `kernels.json`) |
| `POST /api/superroot/apply/races` | **uma corrida por flag** (não usa o lock) |
| `POST /api/superroot/restore` | volta as flags via sysctl (ordem: caps/sysctl por último) |

Todos são idempotentes (pulam flags que já estão em 0) e devolvem o status
atualizado. O app só habilita o modo rápido se o alvo declarar
`superroot.lock_optional: true` em `kernels.json` (e usa o endereço
`superroot.grsec_lock`); sem isso, ele oferece só “por corridas”. O app tem a seção **Superroot** em *Ajustes* (`app/manager.html`,
PT/EN/ES) com os três botões e o aviso de reboot.

Pré-requisito: `/tmp/srpatch` executável na TV (66 KB; o instalador pode
entregá-lo junto com o resto — ele vive em `/tmp` e some no reboot). Sem ele a
API responde `binary:false` e o app mostra “srpatch ausente”.

## 6.1 Persistir o estado (importante)

A patch e o `srpatch` vivem em `/tmp` (some no reboot). Guarde **fora de
`/tmp`** (no PC, em disco):

- o binário testado e o `srpatch.c` (com sha256);
- um `STATE.json` com flag → endereço → valor atual (e o default de boot);
- um script de reaplicação em ordem, uma escrita por linha, com `--check`
  (só lê os sysctls e compara) antes de reaplicar depois de re-rootar.

## 7. Aviso

Desligar o grsecurity reduz a proteção do aparelho — use só na **sua** TV,
para pesquisa. Nada disso persiste no boot e nada é redistribuído.
