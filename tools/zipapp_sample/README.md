# zipapp_sample

App BrightScript de exemplo para o `tools/zipapp.sh`. Ele **não** tem
`manifest` próprio: o `push` usa o pacote original do slot (ex.: 2213) e
sobrepõe estes arquivos, preservando o `manifest`, o token e as permissões do
slot.

O que ele faz quando roda:

- imprime `ZIPAPP_PROOF` no console BrightScript;
- escreve `tmp:/zipapp_proof.txt` com o marcador;
- escreve `tmp:/zipapp_steps.txt` com o passo a passo (prova/beacon);
- mostra uma cena com o marcador;
- opcionalmente faz um GET HTTP para o `--beacon` informado no push.

Placeholders trocados no push:

- `__ZIPAPP_MARKER__` → `--marker` (ou `ZIPAPP_OK_<slot>_<data>`);
- `__ZIPAPP_BEACON__` → `--beacon` (ou vazio).

Uso:

```sh
ROKU_HOST=192.168.1.100 bash tools/zipapp.sh push 2213 tools/zipapp_sample \
  --beacon "http://192.168.1.2:8901/zipapp/beacon?slot=2213" \
  --marker "ZIPAPP_OK"
ROKU_HOST=192.168.1.100 bash tools/zipapp.sh apply 2213
ROKU_HOST=192.168.1.100 bash tools/zipapp.sh restore 2213
```

`source/main.brs` funciona nos dois estilos: canal comum (`Main`) e canal
SGDEX como o 2213 (`GetSceneName`/`MainInit`). O beacon é síncrono de
propósito (o objeto `roUrlTransfer` local seria coletado antes de um envio
assíncrono); sem `--beacon` ele é pulado. Em um app real, guarde a referência
do `roUrlTransfer` e use `AsyncGetToString`.
