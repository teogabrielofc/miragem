'use strict';
/*
 * GET /api/targets
 *
 * Catalogo usado pelo instalador e pela landing:
 *   { schema, updated, targets: [{ id, model, os, build, kernel, arch,
 *     verified[], offsets{}, payload:{ available, files[] } }] }
 *
 * CORS liberado: a pagina do instalador pode rodar na TV (origem file:// ou
 * o dominio), e o mesmo catalogo serve o painel local e ferramentas do PC.
 */
const data = require('../lib/data');

module.exports = function handler(req, res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,HEAD,OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
  res.setHeader('Cache-Control', 'public, max-age=60, stale-while-revalidate=300');
  if (req.method === 'OPTIONS') { res.statusCode = 204; return res.end(); }
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.statusCode = 405;
    res.setHeader('Allow', 'GET,HEAD,OPTIONS');
    return res.end(JSON.stringify({ error: 'method not allowed' }));
  }
  const body = JSON.stringify({
    schema: data.schema,
    updated: data.updated,
    targets: data.listTargets(),
  });
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.statusCode = 200;
  if (req.method === 'HEAD') return res.end();
  res.end(body);
};
