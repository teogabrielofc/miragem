#!/usr/bin/env node
/*
 * check_site.mjs - conferencias do build do site (roda no CI e antes de PR).
 *
 *   node tools/build.mjs && node tools/check_site.mjs
 *
 * Verifica: scripts inline compilam, JSON injetado e valido, tags balanceadas,
 * placeholders substituidos, chaves de i18n usadas existem, links internos
 * apontam para arquivos/rotas que existem.
 */
import { readFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(root, 'dist');
const errors = [];
const ok = [];

function fail(m) { errors.push(m); }
function pass(m) { ok.push(m); }

const pages = ['index.html', 'installer.html', 'manager.html', 'gerador.html'];
for (const page of pages) {
  if (!existsSync(join(dist, page))) { fail(page + ': nao foi gerado (rode tools/build.mjs)'); continue; }
  const html = readFileSync(join(dist, page), 'utf8');

  /* placeholders */
  const left = html.match(/(?<![.A-Za-z0-9_])__[A-Z][A-Z0-9_]*__(?![A-Za-z0-9_])/g);
  if (left) fail(page + ': placeholder nao substituido: ' + [...new Set(left)].join(', '));

  /* scripts inline */
  const re = /<script(\s[^>]*)?>([\s\S]*?)<\/script>/gi;
  let m, n = 0;
  while ((m = re.exec(html))) {
    const attrs = m[1] || '';
    if (/type\s*=\s*["']text\/plain["']/i.test(attrs)) continue;
    const code = m[2];
    if (!code.trim()) continue;
    n++;
    try { new vm.Script(code, { filename: page + '#script' + n }); }
    catch (e) { fail(page + ' script ' + n + ': ' + e.message); }
  }
  if (n) pass(page + ': ' + n + ' script(s) validos');

  /* JSON injetado */
  const j = /window\.__TARGETS__\s*=\s*(\{[\s\S]*?\})\s*;/.exec(html);
  if (j) {
    try {
      const cat = JSON.parse(j[1]);
      if (!Array.isArray(cat.targets) || !cat.targets.length) fail(page + ': __TARGETS__ sem targets');
      else pass(page + ': catalogo com ' + cat.targets.length + ' alvo(s)');
    } catch (e) { fail(page + ': __TARGETS__ invalido: ' + e.message); }
  }

  /* estagio 1: marcador de IP (o IP real entra na hora de instalar) */
  const pay = /<script type="text\/plain" id="payload">([\s\S]*?)<\/script>/i.exec(html);
  if (pay) {
    const marker = (pay[1].match(/c0a80102/g) || []).length;
    if (marker !== 1) fail(page + ': marcador de IP do estagio 1 aparece ' + marker + 'x (esperado 1)');
    else pass(page + ': estagio 1 com marcador de IP');
  }

  /* tags balanceadas (ignora conteudo de script/style) */
  const stripped = html.replace(/<script[\s\S]*?<\/script>/gi, '').replace(/<style[\s\S]*?<\/style>/gi, '');
  const VOID = new Set(['area','base','br','col','embed','hr','img','input','link','meta','param','source','track','wbr']);
  const stack = [];
  const tagRe = /<(\/?)([a-zA-Z][a-zA-Z0-9-]*)([^>]*)>/g;
  let t;
  while ((t = tagRe.exec(stripped))) {
    const close = t[1] === '/';
    const name = t[2].toLowerCase();
    if (VOID.has(name) || /\/$/.test(t[3])) continue;
    if (!close) stack.push(name);
    else {
      const i = stack.lastIndexOf(name);
      if (i < 0) fail(page + ': </' + name + '> sem abertura');
      else if (i !== stack.length - 1) fail(page + ': </' + name + '> fecha fora de ordem (' + stack.slice(i + 1).join(',') + ')');
      else stack.pop();
    }
  }
  if (stack.length) fail(page + ': tags nao fechadas: ' + stack.join(', '));
}

/* landing: chaves de i18n existem nos 3 idiomas + links internos */
if (existsSync(join(dist, 'index.html'))) {
  const html = readFileSync(join(dist, 'index.html'), 'utf8');
  const keys = [...new Set([...html.matchAll(/data-i18n="([^"]+)"/g)].map((x) => x[1]))];
  for (const lang of ['pt', 'en', 'es']) {
    const block = new RegExp('\\b' + lang + ':\\{([\\s\\S]*?)\\n    \\}', 'm').exec(html);
    if (!block) { fail('landing: bloco i18n ' + lang + ' nao encontrado'); continue; }
    const missing = keys.filter((k) => !new RegExp('(^|[,{\\s])' + k + ':').test(block[1]));
    if (missing.length) fail('landing: ' + lang + ' sem chave(s): ' + missing.join(', '));
  }
  if (!errors.some((e) => e.startsWith('landing:') && e.includes('sem chave'))) pass('landing: i18n completo pt/en/es');

  for (const mm of html.matchAll(/href="(\/[^"#?]*)/g)) {
    const p = mm[1].replace(/\/$/, '/index.html');
    const candidates = [p, p + '.html', p + '/index.html'];
    if (candidates.some((c) => existsSync(join(dist, c)))) continue;
    if (/^\/api\//.test(p)) continue;
    fail('landing: link interno sem destino: ' + mm[1]);
  }
}

/* links relativos da documentacao */
for (const f of ['README.md', 'CONTRIBUTING.md',
                 'docs/QUICKSTART.md', 'docs/INSTALL.md', 'docs/SITE.md',
                 'docs/PORTING.md', 'docs/KERNELS.md', 'docs/SUPERROOT.md',
                 'docs/RMP-DLNA.md']) {
  if (!existsSync(join(root, f))) continue;
  const txt = readFileSync(join(root, f), 'utf8');
  for (const m of txt.matchAll(/\]\((?!https?:|mailto:|#)([^)#]+)/g)) {
    const target = join(root, dirname(f), decodeURIComponent(m[1].trim()));
    if (!existsSync(target)) fail(f + ': link relativo quebrado: ' + m[1]);
  }
}

if (errors.length) {
  console.error('check_site: ' + errors.length + ' problema(s)');
  for (const e of errors) console.error('  - ' + e);
  process.exit(1);
}
console.log('check_site: ok');
for (const o of ok) console.log('  - ' + o);
