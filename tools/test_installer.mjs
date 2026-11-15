#!/usr/bin/env node
/*
 * test_installer.mjs - teste de fluxo da UI do instalador (sem navegador).
 *
 * Usa linkedom para montar o DOM e roda os <script> inline do installer com um
 * fetch simulado. Cobre: escolha de alvo (?pick), "tentar mesmo assim" e o gate
 * de kernel nao suportado.
 *
 *   npm i --no-save linkedom   (em /tmp, nao vai pro repo)
 *   node tools/test_installer.mjs [caminho/para/installer.html]
 */
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const linkedom = await import('linkedom');
const file = process.argv[2] || 'dist/installer.html';
const html = readFileSync(file, 'utf8');

/* catalogo igual ao que o build injeta */
const cat = JSON.parse(/window\.__TARGETS__\s*=\s*(\{[\s\S]*?\})\s*;/.exec(html)[1]);

let fails = 0;
function check(name, cond, extra) {
  if (cond) console.log('  ok   ' + name);
  else { fails++; console.log('  FAIL ' + name + (extra ? ' :: ' + extra : '')); }
}

function makePage(query, statusJson) {
  const { window, document } = linkedom.parseHTML(html);
  const store = new Map();
  store.set('miragem.pc', '192.168.1.50');
  const waits = [];
  const fetchCalls = [];

  const fetchStub = (url) => {
    fetchCalls.push(String(url));
    const u = String(url);
    if (u.startsWith('http://x/status')) return Promise.resolve({ ok: true, json: () => Promise.resolve(statusJson) });
    if (u.includes('127.0.0.1:8090')) return Promise.resolve({ ok: false, json: () => Promise.resolve(null) });
    if (u.includes('/api/targets')) return Promise.resolve({ ok: true, json: () => Promise.resolve(cat) });
    if (u.includes('/api/payload')) return Promise.resolve({ ok: true, json: () => Promise.resolve({
      payload: { available: true, files: [{ name: 'gx_bind.rld', bytes: 53312, ipPlaceholder: true }] },
    }) });
    return Promise.resolve({ ok: true, json: () => Promise.resolve({}) });
  };

  /* stub que devolve o marcador: prova que o IP digitado entra no estagio 1 */
  document.getElementById('payload').textContent = 'return "c0a80102";';

  const ctx = {
    window, document,
    location: { search: query, protocol: 'https:', href: 'https://miragem.test/installer' + query },
    navigator: { userAgent: 'Mozilla/5.0 RokuBrowser/104.11.35 (TV-H000X-15.0.4.5537) Chrome/104.0.5112.70 Safari/537.36', language: 'pt-BR' },
    localStorage: {
      getItem: (k) => (store.has(k) ? store.get(k) : null),
      setItem: (k, v) => store.set(k, String(v)),
      removeItem: (k) => store.delete(k),
    },
    performance: { now: () => Date.now() },
    fetch: fetchStub,
    setTimeout: (fn, ms) => { const t = setTimeout(fn, ms); waits.push(t); return t; },
    clearTimeout,
    setInterval: () => 0,
    clearInterval: () => {},
    console,
    Promise, Date, Math, JSON, Array, Object, String, Number, BigInt, Uint8Array, Error,
  };
  ctx.globalThis = ctx;
  vm.createContext(ctx);

  const scripts = [...html.matchAll(/<script(\s[^>]*)?>([\s\S]*?)<\/script>/g)]
    .filter((m) => !/type\s*=\s*["']text\/plain["']/i.test(m[1] || ''))
    .map((m) => m[2]);
  for (const code of scripts) {
    try { vm.runInContext(code, ctx, { filename: 'installer#inline' }); }
    catch (e) { console.log('  FAIL script: ' + e.message); fails++; return null; }
  }
  return { window, document, ctx, store, waits, fetchCalls };
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const $ = (p, s) => p.document.querySelector(s);
function chooseLang(p, l) {
  const b = p.document.querySelector('#gatebtns button[data-lang="' + (l || 'pt') + '"]');
  if (b) b.dispatchEvent(new p.window.Event('click'));
}

/* 1) ?pick=1 abre a lista e usa o alvo */
{
  console.log('1) ?pick=1 + escolher alvo');
  const p = makePage('?pick=1', null);
  await sleep(40); chooseLang(p); await sleep(80);
  const gate = $(p, '#kgate');
  check('gate aberto', gate.classList.contains('on'));
  const opts = p.document.querySelectorAll('#kgate-list .topt');
  check('lista tem 1 alvo', opts.length === 1, 'n=' + opts.length);
  check('alvo é o h536x', opts[0] && opts[0].getAttribute('data-target') === 'h536x-15.0.4.5537');
  check('alvo único já vem pré-selecionado', $(p, '#kgate-use').disabled === false);
  if (opts[0]) opts[0].dispatchEvent(new p.window.Event('click'));
  await sleep(40);
  check('botão usar habilitado', $(p, '#kgate-use').disabled === false);
  check('nota mostra o alvo', /h536x-15\.0\.4\.5537/.test($(p, '#kgate-note').textContent), $(p, '#kgate-note').textContent);
  check('nota mostra payload para o IP', /payload pronto para 192\.168\.1\.50/.test($(p, '#kgate-note').textContent), $(p, '#kgate-note').textContent);
  $(p, '#kgate-use').dispatchEvent(new p.window.Event('click'));
  await sleep(120);
  check('gate fechou', !gate.classList.contains('on'));
  check('linha do kernel ok', $(p, '#r-kernel').className.includes('ok'), $(p, '#r-kernel').className);
  check('linha do kernel cita o alvo', /h536x-15\.0\.4\.5537/.test($(p, '#r-kernel').textContent));
  check('localStorage gravou o alvo', p.store.get('miragem.target') === 'h536x-15.0.4.5537');
  check('payload stub rodou', $(p, '#r-exec').className.includes('ok'), $(p, '#r-exec').textContent);
  check('IP do PC entrou no estagio 1', /c0a80132/.test($(p, '#r-exec').textContent), $(p, '#r-exec').textContent);
}

/* 2) sem status: instalar abre o gate "não confirmado" e "tentar mesmo assim" segue */
{
  console.log('2) kernel desconhecido + tentar mesmo assim');
  const p = makePage('', null);
  await sleep(40); chooseLang(p); await sleep(80);
  $(p, '#install').dispatchEvent(new p.window.Event('click'));
  await sleep(150);
  check('gate aberto', $(p, '#kgate').classList.contains('on'));
  check('título de não confirmado', /confirmado/i.test($(p, '#kgate-title').textContent), $(p, '#kgate-title').textContent);
  check('lista visível', p.document.querySelectorAll('#kgate-list .topt').length === 1);
  $(p, '#kgate-skip').dispatchEvent(new p.window.Event('click'));
  await sleep(120);
  check('gate fechou', !$(p, '#kgate').classList.contains('on'));
  check('linha kernel em aviso', $(p, '#r-kernel').className.includes('warn'), $(p, '#r-kernel').className);
  check('texto de seguir sem confirmar', /sem confirmar|forced/i.test($(p, '#r-kernel').textContent), $(p, '#r-kernel').textContent);
}

/* 3) status diz supported:false */
{
  console.log('3) status supported:false');
  const p = makePage('?status=http://x/status', { uid500: true, os: '15.0.4', kernel: '4.9.118', supported: false });
  await sleep(40); chooseLang(p); await sleep(80);
  $(p, '#install').dispatchEvent(new p.window.Event('click'));
  await sleep(150);
  check('gate bloqueado aberto', $(p, '#kgate').classList.contains('on'));
  check('título de não suportado', /suportado/i.test($(p, '#kgate-title').textContent), $(p, '#kgate-title').textContent);
  check('detalhe com kernel', /4\.9\.118/.test($(p, '#kgate-body').textContent), $(p, '#kgate-body').textContent.slice(0, 120));
  check('ainda oferece tentar', !$(p, '#kgate-skip').disabled);
}

/* 4) IP do PC obrigatorio e validado */
{
  console.log('4) IP do PC');
  const p = makePage('', null);
  await sleep(40); chooseLang(p); await sleep(80);
  const pc = $(p, '#pc');
  check('IP carregado do localStorage', pc.value === '192.168.1.50', pc.value);
  check('estado ok', $(p, '#pc-state').className.includes('ok'), $(p, '#pc-state').className);
  pc.value = '999.1.1.1';
  pc.dispatchEvent(new p.window.Event('input'));
  check('IP invalido marcado', pc.className.includes('bad'), pc.className);
  check('estado invalido', $(p, '#pc-state').className.includes('bad'));
  pc.value = '';
  pc.dispatchEvent(new p.window.Event('input'));
  $(p, '#install').dispatchEvent(new p.window.Event('click'));
  await sleep(60);
  check('sem IP bloqueia o install', /IP do PC/.test($(p, '#hint').textContent), $(p, '#hint').textContent);
  check('gate nao abriu', !$(p, '#kgate').classList.contains('on'));
  pc.value = '10.0.0.7';
  pc.dispatchEvent(new p.window.Event('input'));
  $(p, '#install').dispatchEvent(new p.window.Event('click'));
  await sleep(150);
  check('com IP segue para o kernel', $(p, '#kgate').classList.contains('on') || $(p, '#r-kernel').className.includes('ok'));
  check('IP salvo', p.store.get('miragem.pc') === '10.0.0.7');
}

process.exit(fails ? 1 : 0);
