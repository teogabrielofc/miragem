# RE — gate do modo desenvolvedor, limites do kernel e do GhostLock

Notas de engenharia reversa do aparelho de referência **Roku H536X / Philco
PTV24G5YR2CP**, Roku OS **15.0.4.5537** (build `G1G.04E05537A`), kernel
**Linux 4.9.118** ARMv7 (MStar mt5867). Tudo foi observado no próprio aparelho;
onde a conclusão é parcial, está marcado.

> O entrypoint (Ginga/DTV Play) e o estágio do Chromium 104 são os mesmos em
> outras TVs Roku com Ginga no 15.0.4; o que este documento descreve de kernel
> (seções de GhostLock/offsets) é específico do build acima. Para outro modelo,
> veja `docs/PORTING.md`.

## 1. Alvo

| Item | Valor |
|---|---|
| Modelo | Roku H536X / Philco PTV24G5YR2CP |
| Roku OS | 15.0.4, build 5537 (`G1G.04E05537A`) |
| Kernel | 4.9.118 `#2 SMP PREEMPT armv7l`, sem KASLR |
| `_stext` | `0x80100000` |
| LinkDB | `/nvram/system.conf.db` (editor `/bin/linkdbedit`) |
| Navegador | RokuBrowser / Chromium **104.0.5112.70**, `--no-sandbox --single-process`, seccomp 0 |
| Update | `SoftwareUpdate/Package` em `beta` → trocado para `release` |

## 2. Cadeia usada pelo Miragem

1. **Foothold uid=500** — Ginga/DTV Play carrega um iframe de um canal ISDB;
   um proxy reverso com a cadeia eMudhra/AAA entrega a página de pesquisa.
2. **CVE-2023-3079** — exploit V8 no Chromium 104: leak, fake object, chamada
   nativa, loader de RLD. O renderer não tem seccomp e roda como uid=500.
3. **GhostLock (CVE-2026-43499)** — corrida em `rt_mutex`/futex PI no 4.9.118.
   Um `FUTEX_CMP_REQUEUE_PI` deixa `pi_blocked_on` apontando para um `rt_waiter`
   que já morreu; a cadeia de prioridade chega em `rt_mutex_dequeue()` →
   `rb_erase()` com ponteiros controlados.
4. **Root + escape** — `task->cred = init_cred`, `task->fs = init_fs`, fork,
   `execve` do agente root, PTY em 2222.

Detalhes de endereços e geometria: `docs/INSTALL.md` e `exploit/*.c`.

## 3. Gate do modo desenvolvedor

O modo desenvolvedor "de verdade" (instalador 8080, sideload, dev channel) é
um gate **nativo** com estado no NVRAM/LinkDB, não um simples arquivo de
configuração. As flags ficam em `/nvram/system.conf.db` e são lidas/gravadas
com `linkdbedit`:

```sh
linkdbedit /nvram/system.conf.db -g Developer/Agree
linkdbedit /nvram/system.conf.db -s Developer/Agree true
linkdbedit /nvram/system.conf.db -s Developer/Installer true
```

O gerenciador expõe isso em `POST /api/devmode`, que grava
`Developer/Agree=true` e `Developer/Installer=true` e responde
"flags gravadas; reinicie a TV para o instalador subir".

### 3.1 Flags e chaves observadas

| Chave / sinal | O que faz | Estado |
|---|---|---|
| `Developer/Agree` | aceite do EULA do modo dev; libera o fluxo do instalador | **gravada com root**; ECP ainda reporta `developer-enabled=false` até o app/OS reler |
| `Developer/Installer` | habilita o instalador de desenvolvimento | **gravada**; efeito só depois de reboot/releitura |
| `developer-enabled` (ECP) | `<developer-enabled>` em `/query/device-info` | **cacheado**: não muda na hora em que o LinkDB muda |
| `skipeula` | chave consultada pelo fluxo do instalador de dev para **pular a tela de EULA** | **observada em sondagem**: suprime a tela, mas **não** substitui `Developer/Agree`/gate nativo |
| `enable_dev_installer` | nome registrado para o gatilho que **abre o instalador de dev** | **parcial**: sozinho não abre o gate; o EULA e o estado do NVRAM continuam valendo |
| `channel_development_unlinked` | token/estado que o aparelho calcula para **desenvolvimento de canal enquanto está desvinculado** de conta | **observado**; muda quando a conta é vinculada — é o que bloqueia dev channel/loja em aparelho `user-profile-type=none` |

Leitura prática:

- `skipeula` e `enable_dev_installer` são atalhos de **UI** (pular EULA / abrir
  a tela do instalador). Eles não convertem o aparelho em "dev mode" e não
  sobrevivem como permissão: o gate real é o par
  `Developer/Agree` + `Developer/Installer` no LinkDB, com efeito após o
  runtime reler o estado.
- O `developer-enabled` do ECP é um **cache do runtime**. Escrever no LinkDB
  com root não invalida o cache; um reboot (ou o app relendo) é necessário.
  Por isso a automação não depende do dev mode: o root vem do GhostLock.
- `channel_development_unlinked` explica o sintoma "a TV tem rede, o ECP
  responde, mas a Channel Store/dev channel não funciona": o aparelho não está
  vinculado (`user-profile-type=none`). É um estado de conta, não de rede.
- O gate do MFG (`roManufacturing is not authorized for use.`) é **outro**
  gate, também nativo no `/bin/Application`, e não é satisfeito copiando
  `hidden`/`rom_only`/`dev_id` para um pacote de dev.

### 3.2 O que não funcionou

- Escrever `SoftwareUpdate/CurrVersion=16.0.0/6000` **não** muda a versão
  reportada: `/query/device-info` continua 15.0.4/5537. A versão real vem de
  `/etc/BUILD_PLID`, `/etc/build-info` e do `roDeviceInfo` do app (rootfs
  somente leitura, app em chroot).
- Forçar `Developer/*` e reiniciar não garantiu um dev-installer estável: o
  caminho de panic/beta do boot pode zerar a flag.

## 4. Limites de módulo: `insmod`/`module load` = EPERM

Mesmo como **uid 0** depois do GhostLock:

- `insmod`/`modprobe` de um `.ko` próprio falha com **EPERM**. O kernel tem
  grsecurity e a política de módulos não é afrouxada por `CAP_SYS_MODULE`
  efetivo; não há caminho de "carregar LKM" a partir do userspace.
- O autoload de módulos também fica quebrado: o exploit corrompe os primeiros
  bytes de `modprobe_path`. Módulos **já carregados** continuam funcionando;
  novos autoloads, não.
- `reboot(2)` retorna **EPERM** como root. `sysrq-trigger` aceita escrita, mas
  `b`/`s` não tiveram efeito; o watchdog é alimentado com frequência.
- Escritas em `/proc/sys` são negadas pelo grsecurity **mesmo como uid 0** —
  não dá para restaurar `randomize_va_space`, `console_loglevel` ou o
  `modprobe_path` pela userspace.

Consequência: o pós-exploit é "root de userspace", não "root de kernel
administrável". Nada de módulo, nada de desligar proteções, nada de reboot
limpo.

## 5. Limites de escrita do GhostLock

O primitivo de escrita é o `rb_erase` do `rt_mutex_dequeue()`:

```
escreve  value  em  target
e, se value != 0,
escreve  target-4  em  value
```

Isso impõe vários limites práticos:

| Limite | Efeito |
|---|---|
| **Efeito colateral** | toda escrita não-nula grava `target-4` no endereço `value`. `value` tem que ser um endereço válido e gravável, ou o kernel panica. |
| **`target-4` descartável** | a palavra em `target-4` é sobrescrita se você usar o valor como destino de leitura; escolha alvos com vizinho descartável. |
| **Uma "dança" ≈ 1 s** | cada escrita custa uma sequência de futex/prioridades. Não existe escrita em massa: creds + fs + leituras = várias danças. |
| **`val` do requeue tem que ser 1** | o `FUTEX_CMP_REQUEUE_PI` só entra na janela com `val=1`; o quarto argumento é o `nr_requeue`. |
| **Gate de prioridade** | `victim`/`owner` em `nice=19`, `K` em `nice=0`; o `sched_setattr` posterior propaga 139 e abre `waiter->prio == task->prio`. |
| **Top waiter vivo** | o `pi_mutex` real tem que continuar não-vazio, senão `BUG_ON` no `rt_mutex_top_waiter()`. |
| **Processos presos** | cada dança deixa alguns futex bloqueados; não dá para repetir indefinidamente sem sujar o scheduler. |

### 5.1 Leitura 4+4

Não há leitura arbitrária direta. O truque é repontar o `data` do sysctl
estático `tcp_wmem` (`0x8098750c`) para `X-4` e ler
`/proc/sys/net/ipv4/tcp_wmem`:

```
[colateral 0x80987508] [*(X)] [*(X+4)]
```

A escrita que reponta grava uma constante em `X-4` (daí "escolha um X com
vizinho descartável"). As tabelas `net`, `random` e `keys` são estáticas e
servem; `kern_table` (printk/modprobe/panic) é copiada no boot
(`register_leaf_sysctl_tables`) e as entradas estáticas estão mortas.

### 5.2 Footprint até o reboot

- `randomize_va_space = 0`;
- `console_loglevel` corrompido;
- 4 primeiros bytes de `modprobe_path` corrompidos (autoload quebrado);
- ponteiro `data` do `tcp_wmem` repontado (só a visão `/proc`; a pilha TCP
  real está intacta);
- alguns futex bloqueados.

Um reboot restaura tudo — e perde o root.

## 6. Persistência (por que não fecha)

Três caminhos foram testados; nenhum dá root frio:

1. **U-Boot env (`mtd4`, `rokudevmode=dev=1`)** — o bloco é texto + CRC32 e foi
   gravado com `mtdtool` (erase + write + verify). O bootloader **reescreve** o
   ambiente no boot e o `dev=1` não entra na cmdline. `ubappedit` é rejeitado
   em unidade segura (`Commit failed: Invalid argument`, `is_secure`).
2. **Injeção uid=500 no boot** (`/nvram/SharedObjectNumPages` lido por
   `S20nvram_sharedobject`) — dá execução **uid=500 em todo boot**, mas:
   - PaX MPROTECT recusa mapear `PROT_EXEC` de arquivo gravável pelo app
     (`failed to map segment from shared object`);
   - `/nvram` é `noexec`;
   - não há filesystem gravável+exec persistente onde o app grave um binário
     root-owned, e TPE exige root para `execve` em `/tmp`.
3. **Developer mode** — dá 8080/sideload, não `dev=1`; e o flag pode ser
   zerado no caminho de panic/reboot.

Reboot controlado só foi obtido com oops deliberado (`panic_on_oops=1`,
`panic=0`) + watchdog — é crash boot, não persistência.

## 7. Em aberto

- Bypass do secure boot / roothash / TEE, ou um hook de boot root-context que
  não exista nos scripts de userland.
- Loader nativo que rode sem o PC (hoje o estágio nativo abre TCP para o host).
- Versão reportada / `roDeviceInfo` para a loja, se ela exigir 15.1+.
- Efeito real e persistência de `skipeula` / `enable_dev_installer` /
  `channel_development_unlinked` fora da sessão de root (medir com reboot
  limpo e ECP sem cache).

## 8. Referências no repositório

- `exploit/gx.c`, `exploit/gx_bind.c` — GhostLock (`esc`, `root`, `read`).
- `exploit/rootagent.c` — agente root (PTY + protocolo 2323).
- `exploit/mtdtool.c` — leitura/escrita de partição MTD (experimento U-Boot).
- `exploit/pack.py` — ELF → RLD (`RLD1`).
- `tools/miragemd.c` — status, tvsh on/off, `Developer/*`, plugins.
- `foothold/` — página Ginga, addons mitmproxy, base V8.
- `docs/INSTALL.md` — fluxo USB e checagem de compatibilidade.
