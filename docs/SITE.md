# Site — landing, instalador HTTPS, API e gerador USB

O site é servido **pelo próprio repositório** no Vercel: qualquer merge na
`main` roda `node tools/build.mjs` e publica. Não há backend próprio — só
funções serverless em `api/` para o catálogo de kernels e os payloads.

## 1. Rotas publicadas

| Rota | Arquivo | O que é |
|---|---|---|
| `/` | `site/landing.html` → `dist/index.html` | landing PT/EN/ES (o que é, compatibilidade, aviso, CTA) |
| `/installer` | `app/installer.html` → `dist/installer.html` | instalador com o estágio V8 embutido (servido por **HTTPS**) |
| `/manager` | `app/manager.html` → `dist/manager.html` | gerenciador (fala com `127.0.0.1:8090` na TV) |
| `/gerador` | `site/index.html` → `dist/gerador.html` | gerador do pacote USB (offline, um arquivo) |
| `/kernels.json` | `kernels.json` | catálogo cru |
| `/api/targets` | `api/targets.js` | catálogo + estado do payload por alvo |
| `/api/payload` | `api/payload.js` | metadados do alvo / binário do alvo |
| `/api/manager` | `api/manager.js` | `miragemd.arm`, `tvsh`, `tvcmd.py`… por HTTPS |

O build é a única fonte de `dist/`. Nada é editado à mão lá.

## 2. Build (`tools/build.mjs`)

1. lê `kernels.json` e varre `payload/<id>/` (tamanho + sha256);
2. injeta no `installer.html` um `<script>window.__TARGETS__={...}</script>`
   para a escolha de alvo funcionar **offline** (USB) e atualiza por
   `/api/targets` quando há rede;
3. injeta no landing a fonte Parisienne (reaproveitada de `site/index.html`),
   o catálogo e, se `MIRAGEM_REPO_URL` estiver definido, o link do GitHub;
4. copia `assets/`, `kernels.json`, e escreve `robots.txt`.

```
node tools/build.mjs        # gera dist/
node tools/check_site.mjs   # valida scripts, tags, i18n, links
```

## 3. API

### `GET /api/targets`

```json
{
  "schema": 1,
  "updated": "2026-11-15",
  "targets": [{
    "id": "h536x-15.0.4.5537",
    "model": "H536X / PTV24G5YR2CP",
    "os": "15.0.4.5537", "kernel": "4.9.118", "arch": "armv7",
    "verified": ["read", "root", "chroot-escape"],
    "offsets": { "...": "..." },
    "payload": { "available": false, "files": [] }
  }]
}
```

`CORS: *` (a página pode rodar na TV, em `file://`, ou no domínio).

### `GET /api/payload?target=<id>`

Metadados do alvo + `usage` (`ROKU_TARGET=<id> bash tools/start_servers.sh`).

### `GET /api/payload?target=<id>&file=<nome>[&ip=<IPv4>]`

Stream do binário em `payload/<id>/<nome>` (quando publicado). Nome validado
contra `[A-Za-z0-9._-]+` e contra path traversal.

Com `ip` (IPv4 válido), se o arquivo trouxer o placeholder
`255.255.255.255`, a resposta sai **personalizada** — o `gx_bind.rld` é
compilado com o IP do PC e conecta de volta nele. Headers: `X-Miragem-IP` e
`X-Miragem-Sha256`; `400` para IP inválido, `409` para arquivo sem placeholder.
A troca é só de string (mesmo tamanho), então o resto do binário é idêntico.

### `GET /api/manager?file=<nome>`

Serve `tools/miragemd.arm`, `tools/tvsh`, `tools/tvcmd.py`, `tools/tvget.py`,
`tools/tvput.py` por HTTPS (allowlist fechada).

## 4. Instalador em etapas

1. **Idioma** (PT/EN/ES) e checagem de Chrome (espera 104, aceita 100–109) e
   firmware (`TV-...-15.0.4`); "tentar mesmo assim" libera.
2. **Payload V8** (`#payload`, embutido) → `uid=500` e shells de apoio.
3. **Kernel** — o estágio 3 tem três desfechos:

| Situação | UI |
|---|---|
| `GET /status` do host diz `supported:true` | segue direto |
| `supported:false` | modal "Kernel não suportado" + **lista de alvos** |
| endpoint ausente/offline | modal "Kernel não confirmado" + lista |
| `?target=<id>` ou `?pick=1` | abre a lista já com o alvo pré-selecionado |

Antes disso, a tela principal pede o **IP do PC**: o `gx_bind.rld` é
compilado com esse IP e conecta de volta no PC (47190/47192) para buscar o
agente root. Sem um IPv4 válido o botão não inicia a instalação; o valor fica
em `localStorage['miragem.pc']` e pode vir por `?pc=`.

Na lista, cada item vem de `/api/targets` (ou do `window.__TARGETS__`
injetado): modelo, OS, kernel, arch e se há payload publicado — e, para
payloads com placeholder de IP, "payload pronto para <IP do PC>". Botões:

- **Usar este payload** — grava `miragem.target`, marca o kernel como ok e
  continua a instalação;
- **Tentar mesmo assim** — segue sem confirmar (usa o build do PC);
- **Cancelar**.

O contrato do endpoint de status do host continua pendente (ver
`docs/KERNELS.md`): `GET http://HOST:PORT/status` →
`{uid500, os, kernel, supported}`. Sem ele a lista cobre o caso.

## 5. Gerador do pacote USB (`/gerador`)

Continua igual: um arquivo só, sem backend. Lê o `MANIFEST` da TV no navegador,
monta o zip com `MANIFEST`, `_carousel_metadata.json`, `index.html` dos apps e
`ferramentas/` (zip writer próprio, CRC32, UTF-8, modo 0755 nas ferramentas).

| Variável | Conteúdo |
|---|---|
| `MANAGER_B64` | `app/manager.html` |
| `INSTALLER_B64` | `app/installer.html` |
| `TVSH_B64` | `tools/tvsh` |
| `TVCMD_B64` | `tools/tvcmd.py` |

## 6. Deploy no Vercel (a partir do repo)

1. **Add New → Project → Import** o repositório.
2. Framework preset: **Other**. Build e output vêm do `vercel.json` da raiz:
   `node tools/build.mjs` → `dist/`.
3. **Settings → Environment Variables**: `MIRAGEM_REPO_URL`
   (ex.: `https://github.com/<org>/miragem`) para os links do GitHub
   aparecerem; sem ela, os links ficam ocultos.
4. **Settings → Domains**: adicione o domínio (`miragem.facil.wtf` ou outro);
   TLS é automático.
5. PRs: a Vercel gera um **preview** por branch/PR; ao dar merge, produção
   atualiza sozinha. O workflow `.github/workflows/check.yml` roda build +
   `check_site` + teste de fluxo do instalador em cada PR.

Pela CLI, na raiz do repo:

```sh
npm i -g vercel
vercel --prod
```

## 7. Teste local

```sh
node tools/dev_server.mjs 3000
# http://localhost:3000/  /installer  /manager  /gerador  /api/targets
```

O `dev_server.mjs` roda o build e serve `dist/` com `cleanUrls`, chamando as
mesmas funções de `api/` (não precisa de Vercel CLI).

## 8. HTTPS: o que já é e o que falta

- landing, instalador, gerenciador, gerador, catálogo e binários do
  gerenciador: **tudo por HTTPS**, mesma origem, sem dependência de HTTP
  (exceto o `127.0.0.1:8090` da própria TV, que é loopback e não sai do
  aparelho).
- O payload V8 já vai **embutido** no instalador: nada é buscado em HTTP.
- **Limite atual:** o estágio nativo (`gx_bind.rld`) ainda conecta em **TCP**
  para o PC (portas 471xx) e é compilado com `HOST_IP` da sua LAN — por isso
  `payload/<id>/` começa vazio e cada pessoa compila o seu. Servir um
  `gx_bind` pronto pelo site exige tirar o IP do binário (argumento/descoberta
  na LAN) — é o próximo passo para uma instalação 100% remota.
- O endpoint `/status` do kernel continua sendo HTTP no host e não existe
  ainda; a lista de alvos em `/installer` cobre esse caso.

## 9. Observações

- O zip do USB é montado localmente; o servidor não recebe `MANIFEST` nenhum.
- Conteúdo de pesquisa no próprio aparelho. Não redistribua firmware da Roku
  nem use em TV de terceiros (veja `docs/LEGAL.md`).
- Blobs `TVSH_B64`/`TVCMD_B64` são gerados das versões sanitizadas de
  `tools/tvsh` e `tools/tvcmd.py`.
