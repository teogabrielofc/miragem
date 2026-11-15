#!/usr/bin/env node
/*
 * dev_server.mjs - preview local do site do Miragem (sem Vercel CLI).
 *
 *   node tools/dev_server.mjs [porta]
 *
 * Roda tools/build.mjs, serve dist/ com cleanUrls (/installer -> installer.html)
 * e monta /api/* com as mesmas funcoes que rodam no Vercel.
 */
import http from 'node:http';
import { createRequire } from 'node:module';
import { readFileSync, existsSync, statSync } from 'node:fs';
import { join, extname, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(root, 'dist');
const port = Number(process.argv[2] || process.env.PORT || 3000);

execFileSync(process.execPath, [join(root, 'tools', 'build.mjs')], { stdio: 'inherit', env: process.env });

const require = createRequire(import.meta.url);
const api = {
  '/api/targets': require(join(root, 'api', 'targets.js')),
  '/api/payload': require(join(root, 'api', 'payload.js')),
  '/api/manager': require(join(root, 'api', 'manager.js')),
};

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.txt': 'text/plain; charset=utf-8',
  '.md': 'text/plain; charset=utf-8',
  '.ico': 'image/x-icon',
};

function sendFile(res, file) {
  const st = statSync(file);
  res.setHeader('Content-Type', MIME[extname(file).toLowerCase()] || 'application/octet-stream');
  res.setHeader('Content-Length', String(st.size));
  res.statusCode = 200;
  res.end(readFileSync(file));
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://' + (req.headers.host || 'localhost'));
  const fn = api[url.pathname];
  if (fn) return fn(req, res);

  let p = decodeURIComponent(url.pathname);
  if (p === '/' || p === '') p = '/index.html';
  const tries = [join(dist, p), join(dist, p + '.html'), join(dist, p, 'index.html')];
  for (const f of tries) {
    if (f.startsWith(dist) && existsSync(f) && statSync(f).isFile()) return sendFile(res, f);
  }
  res.statusCode = 404;
  res.setHeader('Content-Type', 'text/plain; charset=utf-8');
  res.end('404');
});

server.listen(port, () => {
  console.log('dev: http://localhost:' + port + '  (/ /installer /manager /gerador /api/targets)');
});
