# PR: novo kernel / port do GhostLock

Obrigado por portar o Miragem. Preencha o que se aplica. **Não** anexe
firmware, `.pkg`/`.jwt`, chaves ou dumps: o PR leva só offsets, patch e log.

## Alvo

| Campo | Valor |
|---|---|
| Modelo | <!-- ex.: H536X / Philco PTV24G5YR2CP --> |
| Roku OS / build | <!-- ex.: 15.0.4.5537 / G1G.04E05537A --> |
| Kernel (`uname -r`) | <!-- ex.: 4.9.118 --> |
| Arquitetura | <!-- ex.: armv7 --> |
| KASLR | <!-- sim / não --> |
| `kernels.json` id | <!-- ex.: h536x-15.0.4.5537 --> |

## Fonte do kernel

<!-- Como você obteve o update.roku? tvupdate.roku.com? captura do próprio
     fluxo? Qual o hash do arquivo? NÃO cole um link direto de firmware. -->

## Offsets

<!-- Cole a tabela preenchida (init_cred, tcp_wmem data, __per_cpu_offset,
     runqueues, rq->curr/idle, task->fs/nsproxy/real_cred/cred, fs->root,
     mnt_namespace->root, rb_erase, current_chrooted, PAGE_OFFSET,
     TASK_SIZE, THREAD_SIZE). Diga o método de cada valor. -->

## Teste

<!-- Comandos + saída de read, root e escape (uid=0, pwd=/). Sem serial/MAC,
     IP público ou dados sensíveis. Diga também o que NÃO funcionou. -->

## Checklist

- [ ] `update.roku` obtido da **minha** TV; nenhum binário de firmware no PR
- [ ] kernel extraído (vmlinux/AImage/squashfs) e versão/build/arch/KASLR conferidos
- [ ] offsets preenchidos em `kernels.json` **e** aplicados em `exploit/gx.c` (e `gx_bind.c`)
- [ ] leitura 4+4 validada na TV
- [ ] `uid=0` validado na TV
- [ ] escape do chroot validado (`task->fs = init_fs`, `pwd=/`)
- [ ] modelo/OS/build informados acima
- [ ] log de teste incluído, sem dados sensíveis
- [ ] `git status` revisado: sem `.pkg`, `.jwt`, `.rld`, firmware, chaves ou dumps
- [ ] documentação atualizada (`docs/KERNELS.md` / `docs/PORTING.md` se necessário)
