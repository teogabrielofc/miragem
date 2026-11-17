'use strict';
/*
 * GET /api/manager                 -> lista os utilitarios publicados
 * GET /api/manager?file=<nome>     -> baixa por HTTPS
 *
 * Arquivos servidos (versionados no repo, sem IP embutido):
 *   miragemd.arm  servidor do gerenciador na TV (porta 8090)
 *   mqfd.arm      fala com o ZipMountServer (ZIP app)
 *   ecp.arm       cliente HTTP minimo (ECP/API local, sem curl)
 *   zipapp.sh     push/apply/restore do app custom (ZIP) no PC
 *   ecp_relay.py  relay HTTP do ECP no PC (a TV recusa loopback)
 *   zipapp_apply.sh / dlna_apply.sh   helpers que rodam na TV
 *   tvsh          cliente de shell root (Python 3)
 *   tvcmd.py      um comando por vez pelo PTY
 *   tvget.py      baixar arquivo da TV
 *   tvput.py      enviar arquivo para a TV
 *
 * Assim o PC (ou a propria TV, depois do root) pega o binario por HTTPS em vez
 * de copiar do repositorio:
 *
 *   curl -fLO 'https://<dominio>/api/manager?file=miragemd.arm'
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.join(process.cwd(), 'tools');
const FILES = ['miragemd.arm', 'mqfd.arm', 'ecp.arm', 'zipapp.sh', 'ecp_relay.py',
  'zipapp_apply.sh', 'dlna_apply.sh', 'tvsh', 'tvcmd.py', 'tvget.py', 'tvput.py'];
const TYPES = {
  '.arm': 'application/octet-stream',
  '.py': 'text/x-python; charset=utf-8',
  '.sh': 'text/x-shellscript; charset=utf-8',
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
  const name = url.searchParams.get('file') || '';

  if (!name) {
    const files = FILES.filter(function (f) { return fs.existsSync(path.join(ROOT, f)); })
      .map(function (f) {
        const st = fs.statSync(path.join(ROOT, f));
        return { name: f, bytes: st.size, url: '/api/manager?file=' + encodeURIComponent(f) };
      });
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    res.statusCode = 200;
    return res.end(JSON.stringify({ files: files }));
  }

  if (FILES.indexOf(name) < 0) {
    res.statusCode = 404;
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    return res.end(JSON.stringify({ error: 'unknown file', file: name }));
  }

  const p = path.join(ROOT, name);
  if (!fs.existsSync(p)) {
    res.statusCode = 404;
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    return res.end(JSON.stringify({ error: 'not built', file: name }));
  }

  const st = fs.statSync(p);
  res.setHeader('Content-Type', TYPES[path.extname(name)] || 'application/octet-stream');
  res.setHeader('Content-Length', String(st.size));
  res.setHeader('Content-Disposition', 'attachment; filename="' + name + '"');
  res.setHeader('Cache-Control', 'public, max-age=300');
  res.statusCode = 200;
  if (req.method === 'HEAD') return res.end();
  fs.createReadStream(p).pipe(res);
};
