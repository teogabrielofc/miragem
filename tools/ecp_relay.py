#!/usr/bin/env python3
"""ecp_relay.py - relay HTTP minimo para o ECP da TV.

O ECP da Roku recusa pedidos vindos de endereco loopback/local:

    HTTP/1.1 403 Forbidden
    ECP is not allowed from loopback address.

O `miragemd` roda na propria TV, entao nao consegue chamar
`127.0.0.1:8060/launch/<id>` neste build. Este relay roda no PC e repassa o
pedido para o ECP da TV, que aceita a origem remota. O helper da TV tenta o
ECP local primeiro e, se receber 403/sem resposta, usa `/tmp/miragem/ecp_relay`
(arquivo `<ip-do-pc>:<porta>` escrito pelo `tools/zipapp.sh`).

    ROKU_HOST=192.168.1.100 python3 tools/ecp_relay.py [porta]

Rotas permitidas: /launch/*, /query/*, /keypress/*. Sem autenticacao: use so
na sua rede local e pare quando nao precisar.
"""
import os
import sys
import urllib.request
import urllib.error
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TV = os.environ.get("ROKU_HOST", "192.168.1.100")
TVPORT = int(os.environ.get("ROKU_PORT", "8060"))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else int(os.environ.get("RELAY_PORT", "8092"))
ALLOW = ("/launch/", "/query/", "/keypress/")
MAXBODY = 1 << 20


class Handler(BaseHTTPRequestHandler):
    server_version = "miragem-ecp-relay/1"
    protocol_version = "HTTP/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("[relay] %s %s\n" % (self.address_string(), fmt % args))

    def _proxy(self):
        path = self.path.split("?", 1)[0]
        if path == "/health":
            body = b"ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if not any(path.startswith(p) for p in ALLOW):
            self.send_error(404, "path not allowed")
            return
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAXBODY:
            self.send_error(413)
            return
        if length:
            self.rfile.read(length)
        url = "http://%s:%d%s" % (TV, TVPORT, self.path)
        req = urllib.request.Request(url, data=b"" if self.command == "POST" else None,
                                     method=self.command)
        req.add_header("User-Agent", "miragem-ecp-relay")
        try:
            with urllib.request.urlopen(req, timeout=8) as r:
                body = r.read()
                code = r.status
        except urllib.error.HTTPError as e:
            body = e.read()
            code = e.code
        except Exception as e:  # noqa: BLE001
            body = ("relay error: %s\n" % e).encode()
            code = 502
        self.send_response(code)
        self.send_header("Content-Type", "text/xml; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    do_GET = _proxy
    do_POST = _proxy


def main():
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print("ecp_relay: http://0.0.0.0:%d/ -> http://%s:%d (allow %s)" %
          (PORT, TV, TVPORT, ", ".join(ALLOW)), file=sys.stderr)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
