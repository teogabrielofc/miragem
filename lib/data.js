'use strict';
/*
 * lib/data.js - catalogo de kernels e payloads (usado pelas funcoes de /api).
 *
 * kernels.json e a fonte de verdade dos alvos (modelo, OS, kernel, offsets).
 * payload/<id>/ guarda os binarios publicados para aquele alvo. Os binarios
 * sao opcionais: quem so tem os offsets portados ainda consegue escolher o
 * alvo no instalador e compilar o GhostLock localmente (docs/PORTING.md).
 */
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const kernels = require('../kernels.json');
const PAYLOAD_ROOT = path.join(process.cwd(), 'payload');
const SAFE = /^[A-Za-z0-9._-]+$/;

/* gx_bind.rld e outros binarios sao compilados com HOST_IP. Publicamos com
 * 255.255.255.255 no lugar e trocamos pelo IP do PC na hora de servir. */
const IP_PLACEHOLDER = '255.255.255.255';

const hashCache = new Map();

function isIPv4(s) {
  return typeof s === 'string' && /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/.test(s);
}

function patchIp(buf, ip) {
  if (!isIPv4(ip)) return null;
  const ph = Buffer.from(IP_PLACEHOLDER, 'ascii');
  if (buf.indexOf(ph) < 0) return null;
  const out = Buffer.from(buf);
  const ipb = Buffer.from(ip, 'ascii');
  let i = out.indexOf(ph);
  while (i >= 0) {
    ipb.copy(out, i);
    out.fill(0, i + ipb.length, i + ph.length);
    i = out.indexOf(ph, i + ph.length);
  }
  return out;
}

function fileInfo(dir, name) {
  const p = path.join(dir, name);
  const st = fs.statSync(p);
  if (!st.isFile()) return null;
  const key = p + ':' + st.size + ':' + st.mtimeMs;
  let cached = hashCache.get(key);
  if (!cached) {
    const buf = fs.readFileSync(p);
    cached = {
      sha256: crypto.createHash('sha256').update(buf).digest('hex'),
      ipPlaceholder: buf.indexOf(Buffer.from(IP_PLACEHOLDER, 'ascii')) >= 0,
    };
    hashCache.set(key, cached);
  }
  return {
    name,
    bytes: st.size,
    sha256: cached.sha256,
    ipPlaceholder: cached.ipPlaceholder,
    url: '/api/payload?target=' + encodeURIComponent(path.basename(dir)) + '&file=' + encodeURIComponent(name),
  };
}

function targetDir(id) {
  if (!SAFE.test(id)) return null;
  const dir = path.join(PAYLOAD_ROOT, id);
  if (!dir.startsWith(PAYLOAD_ROOT + path.sep)) return null;
  return fs.existsSync(dir) ? dir : null;
}

function payloadInfo(id) {
  const dir = targetDir(id);
  if (!dir) return { available: false, files: [] };
  const files = [];
  for (const name of fs.readdirSync(dir).sort()) {
    if (name.startsWith('.') || name === 'README.md') continue;
    const info = fileInfo(dir, name);
    if (info) files.push(info);
  }
  return { available: files.length > 0, files };
}

function listTargets() {
  return (kernels.targets || []).map(function (t) {
    return {
      id: t.id,
      model: t.model,
      os: t.os,
      build: t.build,
      kernel: t.kernel,
      arch: t.arch,
      kimage: t.kimage,
      verified: t.verified || [],
      offsets: t.offsets || {},
      superroot: t.superroot || null,
      payload: payloadInfo(t.id),
    };
  });
}

function findFile(id, name) {
  const dir = targetDir(id);
  if (!dir || !SAFE.test(name)) return null;
  const p = path.join(dir, name);
  if (!p.startsWith(dir + path.sep) || !fs.existsSync(p)) return null;
  if (!fs.statSync(p).isFile()) return null;
  return p;
}

module.exports = {
  kernels,
  schema: kernels.schema || 1,
  updated: kernels.updated || '',
  listTargets,
  payloadInfo,
  findFile,
  IP_PLACEHOLDER,
  isIPv4,
  patchIp,
};
