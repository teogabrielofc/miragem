# payload/<target-id>/ — binários publicados por alvo

Cada subpasta é um `id` de `kernels.json`. O que estiver aqui é servido pelo
site por HTTPS em `/api/payload?target=<id>&file=<nome>` e o instalador mostra
na lista de alvos ("payload pronto" vs "só offsets").

```
payload/
└─ h536x-15.0.4.5537/
   ├─ gx_bind.rld     GhostLock + bindsh 2222 (compilado de exploit/gx_bind.c)
   └─ srpatch         superroot: flags do grsec (compilado de exploit/srpatch.c)
```

## O IP do PC

`gx_bind.rld` é compilado com `HOST_IP` (`-DHOST_IP=...`): depois do GhostLock
ele **conecta de volta** no PC (portas 47190/47192) para buscar o agente root.
Por isso o instalador pede o **IP do PC** antes de instalar.

O binário publicado traz `255.255.255.255` no lugar do IP (15 caracteres, o
tamanho máximo de um IPv4). O IP certo entra depois, sem recompilar:

- **pelo site**: `GET /api/payload?target=h536x-15.0.4.5537&file=gx_bind.rld&ip=192.168.1.2`
  devolve o binário já personalizado por HTTPS (`X-Miragem-IP`,
  `X-Miragem-Sha256`);
- **no PC**: `tools/start_servers.sh` detecta o IP da rota até a TV (ou usa
  `ROKU_PC_IP=...`), troca o placeholder e serve o arquivo na 47190.

A troca é só de string: o resto do binário fica idêntico. Prova: com o IP que
subiu root na TV de referência, o resultado é byte-a-byte o `gx_bind.rld`
testado (md5 `890887fd976bee9079b5d79043a27857`).

## O que ainda é build local

`rootagent7` (agente root na 47192) e `agent8.rld` (agente uid=500 na 47127)
também têm o IP embutido, mas são servidos pelo PC a partir do build local —
não são publicados aqui. Receita:

```sh
TC=/caminho/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf
$TC-gcc -O2 -static -no-pie -DHOST_IP='"192.168.1.2"' -Wl,-Ttext-segment=0x20000000 \
  -o rootagent exploit/rootagent.c
python3 exploit/pack.py rootagent agent8.rld
```

## Como habilitar um alvo com binário

1. Porte o GhostLock e adicione a entrada em `kernels.json` (veja
   `docs/PORTING.md`).
2. Compile o `gx_bind` do alvo **com `HOST_IP="255.255.255.255"`** e faça o
   pack:
   ```sh
   $TC-gcc -O2 -static -no-pie -DHOST_IP='"255.255.255.255"' \
     -Wl,-Ttext-segment=0x10000 -o gx_bind exploit/gx_bind.c
   python3 exploit/pack.py gx_bind payload/<id>/gx_bind.rld
   ```
3. Commit — `.gitignore` libera esta pasta, mas bloqueia firmware (`.pkg`,
   `.jwt`, `update.roku`). O build marca `ipPlaceholder: true` e o site passa a
   personalizar o binário por IP.

Nada de firmware da Roku aqui: só binários construídos a partir deste
repositório.
