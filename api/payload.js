'use strict';
/*
 * GET /api/payload?target=<id>            -> metadados + lista de arquivos
 * GET /api/payload?target=<id>&file=<n>   -> binario publicado (HTTPS)
 *
 * O instalador usa o primeiro para mostrar o que existe para o alvo escolhido;
 * o PC pode usar o segundo para baixar o payload por HTTPS em vez de copiar
 * arquivos a mao:
 *
 *   curl -fLO 'https://<dominio>/api/payload?target=h536x-15.0.4.5537&file=gx_bind.rld'
 *
 * Sem binario publicado o alvo continua selecionavel (offsets em kernels.json)
 * e o GhostLock precisa ser compilado localmente - veja docs/PORTING.md.
 */
const fs = require('fs');
const crypto = require('crypto');
const path = require('path');
const data = require('../lib/data');

const TYPES = {
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8',
  '.md': 'text/plain; charset=utf-8',
};

function cors(res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,HEAD,OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
}

module.exports = function handler(req, res) {
  cors(res);
  if (req.method === 'OPTIONS') { res.statusCode = 204; return res.end(); }
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.statusCode = 405;
    res.setHeader('Allow', 'GET,HEAD,OPTIONS');
    return res.end(JSON.stringify({ error: 'method not allowed' }));
  }

  const url = new URL(req.url, 'http://x');
  const id = url.searchParams.get('target') || '';
  const file = url.searchParams.get('file') || '';
  const ip = url.searchParams.get('ip') || '';
  const target = (data.kernels.targets || []).filter(function (t) { return t.id === id; })[0];

  if (!target) {
    res.statusCode = 404;
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    return res.end(JSON.stringify({ error: 'unknown target', target: id }));
  }

  if (!file) {
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    res.setHeader('Cache-Control', 'public, max-age=60');
    res.statusCode = 200;
    return res.end(JSON.stringify({
      target: target,
      payload: data.payloadInfo(id),
      usage: {
        pc: 'ROKU_TARGET=' + id + (ip ? ' ROKU_PC_IP=' + ip : '') + ' bash tools/start_servers.sh',
        download: '/api/payload?target=' + encodeURIComponent(id) + '&file=<arquivo>',
        docs: 'docs/KERNELS.md',
      },
    }));
  }

  const p = data.findFile(id, file);
  if (!p) {
    res.statusCode = 404;
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    return res.end(JSON.stringify({ error: 'file not found', target: id, file: file }));
  }

  const st = fs.statSync(p);
  const type = TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream';
  const disposition = 'attachment; filename="' + file.replace(/"/g, '') + '"';

  /* IP do PC: binarios compilados com HOST_IP trazem 255.255.255.255 no lugar
   * e sao personalizados aqui (mesmo tamanho, so a string muda). */
  if (ip) {
    if (!data.isIPv4(ip)) {
      res.statusCode = 400;
      res.setHeader('Content-Type', 'application/json; charset=utf-8');
      return res.end(JSON.stringify({ error: 'invalid ip', ip: ip }));
    }
    const patched = data.patchIp(fs.readFileSync(p), ip);
    if (!patched) {
      res.statusCode = 409;
      res.setHeader('Content-Type', 'application/json; charset=utf-8');
      return res.end(JSON.stringify({ error: 'file is not ip-patchable', file: file }));
    }
    res.setHeader('Content-Type', type);
    res.setHeader('Content-Length', String(patched.length));
    res.setHeader('Content-Disposition', disposition);
    res.setHeader('X-Miragem-IP', ip);
    res.setHeader('X-Miragem-Sha256', crypto.createHash('sha256').update(patched).digest('hex'));
    res.setHeader('Cache-Control', 'no-store');
    res.statusCode = 200;
    if (req.method === 'HEAD') return res.end();
    return res.end(patched);
  }

  res.setHeader('Content-Type', type);
  res.setHeader('Content-Length', String(st.size));
  res.setHeader('Content-Disposition', disposition);
  res.setHeader('Cache-Control', 'public, max-age=300');
  res.statusCode = 200;
  if (req.method === 'HEAD') return res.end();
  fs.createReadStream(p).pipe(res);
};
