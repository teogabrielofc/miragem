#!/usr/bin/env node
/*
 * build.mjs - monta o site do Miragem para o Vercel (e para preview local).
 *
 * Entrada (versionada):
 *   site/landing.html     landing PT/EN/ES (placeholders __FONT__, __TARGETS__, __REPO__)
 *   app/installer.html    instalador (payload V8 embutido)
 *   app/manager.html      gerenciador (fala com 127.0.0.1:8090 na TV)
 *   site/index.html       gerador de pacote USB (offline, um arquivo)
 *   kernels.json          registro de kernels (vira /api/targets tambem)
 *   assets/*              logo/banner
 *
 * Saida: dist/
 *   /            landing
 *   /installer   instalador servido por HTTPS
 *   /manager     gerenciador
 *   /gerador     gerador do pacote USB
 *   /kernels.json
 *   /assets/*
 *
 * A fonte de verdade continua sendo app/ e site/: nada e editado a mao em dist/.
 */
import { readFileSync, writeFileSync, mkdirSync, rmSync, copyFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(root, 'dist');
const read = (p) => readFileSync(join(root, p), 'utf8');
const exists = (p) => existsSync(join(root, p));

function die(msg) { console.error('build: ' + msg); process.exit(1); }

/* ------------------------------------------------------------------ inputs */
const kernels = JSON.parse(read('kernels.json'));
if (!Array.isArray(kernels.targets) || !kernels.targets.length) die('kernels.json sem targets');

const installer = read('app/installer.html');
const manager = read('app/manager.html');
const generator = read('site/index.html');
const landingTpl = read('site/landing.html');

/* Fonte Parisienne: assets/parisienne.woff2 (fallback: a que esta no gerador). */
let font = '';
if (existsSync(join(root, 'assets', 'parisienne.woff2'))) {
  font = readFileSync(join(root, 'assets', 'parisienne.woff2')).toString('base64');
} else {
  const fontMatch = /base64,([A-Za-z0-9+/=]{1000,})\)/.exec(generator);
  font = fontMatch ? fontMatch[1] : '';
}

/* Repo publico (opcional): define MIRAGEM_REPO_URL no Vercel. */
const repo = (process.env.MIRAGEM_REPO_URL || '').replace(/\/+$/, '');

/* ------------------------------------------------------------------ payload */
function payloadInfo(id) {
  const dir = join(root, 'payload', id);
  if (!existsSync(dir)) return { available: false, files: [] };
  const out = [];
  for (const name of readdirSync(dir)) {
    if (name.startsWith('.')) continue;
    const p = join(dir, name);
    const st = statSync(p);
    if (!st.isFile()) continue;
    out.push({ name, bytes: st.size, sha256: createHash('sha256').update(readFileSync(p)).digest('hex') });
  }
  return { available: out.length > 0, files: out };
}

const payload = {};
for (const t of kernels.targets) payload[t.id] = payloadInfo(t.id);

const targetsJson = JSON.stringify({
  schema: kernels.schema || 1,
  updated: kernels.updated || '',
  targets: kernels.targets.map((t) => Object.assign({}, t, { payload: payload[t.id] })),
  payload,
}).replace(/</g, '\\u003c');

/* ------------------------------------------------------------------ dist */
rmSync(dist, { recursive: true, force: true });
mkdirSync(join(dist, 'assets'), { recursive: true });

/* landing: injeta fonte, catalogo e repo */
let landing = landingTpl
  .split('__FONT__').join(font)
  .split('__TARGETS_JSON__').join(targetsJson)
  .split('__REPO__').join(repo);
writeFileSync(join(dist, 'index.html'), landing);

/* instalador: mesmo arquivo do USB + catalogo inline (funciona offline) */
const inject =
  '<script>window.__TARGETS__=' + targetsJson + ';window.__MIRAGEM_BUILD__="' +
  new Date().toISOString().slice(0, 10) + '";</script>\n';
if (!installer.includes('</head>')) die('app/installer.html sem </head>');
writeFileSync(join(dist, 'installer.html'), installer.replace('</head>', inject + '</head>'));

writeFileSync(join(dist, 'manager.html'), manager);
writeFileSync(join(dist, 'gerador.html'), generator);
writeFileSync(join(dist, 'kernels.json'), JSON.stringify(kernels, null, 2) + '\n');

for (const f of ['logo.png', 'banner.png']) {
  if (exists('assets/' + f)) copyFileSync(join(root, 'assets', f), join(dist, 'assets', f));
}
if (exists('assets/logo.png')) copyFileSync(join(root, 'assets/logo.png'), join(dist, 'favicon.png'));

writeFileSync(join(dist, 'robots.txt'), 'User-agent: *\nAllow: /\n');
writeFileSync(join(dist, '_routes.json'), JSON.stringify({ version: 1 }, null, 2) + '\n');

/* ------------------------------------------------------------------ report */
const sizes = {};
for (const f of ['index.html', 'installer.html', 'manager.html', 'gerador.html']) {
  sizes[f] = statSync(join(dist, f)).size;
}
const have = kernels.targets.filter((t) => payload[t.id].available).map((t) => t.id);
console.log('build: dist/ ok');
console.log('  landing   ' + sizes['index.html'] + ' B');
console.log('  installer ' + sizes['installer.html'] + ' B');
console.log('  manager   ' + sizes['manager.html'] + ' B');
console.log('  gerador   ' + sizes['gerador.html'] + ' B');
console.log('  targets   ' + kernels.targets.length + ' (' + kernels.targets.map((t) => t.id).join(', ') + ')');
console.log('  payload   ' + (have.length ? have.join(', ') : 'nenhum binario publicado (payload/<id>/)'));
console.log('  repo      ' + (repo || '(MIRAGEM_REPO_URL nao definido: links do GitHub ocultos)'));
