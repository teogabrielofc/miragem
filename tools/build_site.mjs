#!/usr/bin/env node
/*
 * build_site.mjs - gera site/index.html (gerador do pacote USB) a partir de
 * site/template.html + app/ + tools/.
 *
 *   node tools/build_site.mjs            # reescreve site/index.html
 *   node tools/build_site.mjs --check    # so confere se esta em dia (CI)
 *
 * O zip do USB leva o instalador e o gerenciador embutidos, entao este build
 * precisa rodar sempre que app/installer.html ou app/manager.html mudarem.
 */
import { readFileSync, writeFileSync, existsSync, statSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const check = process.argv.includes('--check');
const read = (p) => readFileSync(join(root, p));
const b64 = (p) => read(p).toString('base64');

const FONT = 'assets/parisienne.woff2';
const OUT = 'site/index.html';

let font;
if (existsSync(join(root, FONT))) {
  font = readFileSync(join(root, FONT)).toString('base64');
} else {
  /* primeira vez: extrai a fonte que ja esta embutida no index.html atual */
  const cur = read(OUT).toString('utf8');
  const m = /base64,([A-Za-z0-9+/=]{1000,})\)/.exec(cur);
  if (!m) { console.error('build_site: fonte Parisienne nao encontrada'); process.exit(1); }
  font = m[1];
  writeFileSync(join(root, FONT), Buffer.from(font, 'base64'));
  console.log('build_site: extraida ' + FONT + ' (' + statSync(join(root, FONT)).size + ' B)');
}

/* O instalador do USB roda em file://: leva o catalogo de kernels embutido
 * (sem a parte de payload, que so o site publica). */
const kernels = JSON.parse(read('kernels.json').toString('utf8'));
const catalog = JSON.stringify({ schema: kernels.schema || 1, updated: kernels.updated || '', targets: kernels.targets })
  .replace(/</g, '\\u003c');
let installer = read('app/installer.html').toString('utf8');
if (!installer.includes('</head>')) { console.error('build_site: installer sem </head>'); process.exit(1); }
installer = installer.replace('</head>',
  '<script>window.__TARGETS__=' + catalog + ';</script>\n</head>');

let html = read('site/template.html').toString('utf8');
const subs = {
  __PARISIENNE__: font,
  __MANAGER_B64__: b64('app/manager.html'),
  __INSTALLER_B64__: Buffer.from(installer, 'utf8').toString('base64'),
  __TVSH_B64__: b64('tools/tvsh'),
  __TVCMD_B64__: b64('tools/tvcmd.py'),
};
for (const [k, v] of Object.entries(subs)) {
  if (!html.includes(k)) { console.error('build_site: template sem ' + k); process.exit(1); }
  html = html.split(k).join(v);
}

if (check) {
  const cur = existsSync(join(root, OUT)) ? read(OUT).toString('utf8') : '';
  if (cur !== html) {
    console.error('build_site: ' + OUT + ' desatualizado (rode node tools/build_site.mjs)');
    process.exit(1);
  }
  console.log('build_site: ' + OUT + ' em dia (' + html.length + ' B)');
} else {
  writeFileSync(join(root, OUT), html);
  console.log('build_site: ' + OUT + ' (' + html.length + ' B)');
}
