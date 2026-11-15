"""mitmproxy reverse HTTP(S), selecionando o destino pelo Host/:authority.
Serve a AImage 15.3.4 local apenas para .bin de UPDATE; NAO mexe em apps-creds.
Captura cert/private-key do apps-creds para analise.
"""
from mitmproxy import ctx, http
from urllib.parse import urlsplit
from pathlib import Path
import os
import re

BIN = os.environ.get("ROKU_FIRMWARE", "")
SAVE = Path(os.environ.get("ROKU_CREDS", "creds"))


def host_of(flow):
    try:
        return (urlsplit("//" + (flow.request.host_header or "")).hostname
                or "").lower().rstrip(".")
    except ValueError:
        return ""


class Subdominios:
    def load(self, loader):
        loader.add_option("dominio_alvo", str, "example.com",
                          "Dominios base permitidos, separados por virgula")

    def requestheaders(self, flow: http.HTTPFlow):
        hostname = host_of(flow)
        raw = ctx.options.dominio_alvo.lower().strip().rstrip(".")
        bases = [b.strip() for b in raw.split(",") if b.strip()]
        peer = flow.client_conn.peername
        if not peer or peer[0] != os.environ.get("ROKU_TV", "192.168.1.100"):
            flow.response = http.Response.make(403, b"Cliente nao permitido")
            return
        allowed = any(hostname == b or hostname.endswith("." + b)
                      for b in bases)
        if not allowed:
            flow.response = http.Response.make(403, b"Dominio nao permitido")
            return
        path = flow.request.path.split("?", 1)[0]
        is_creds = "apps-creds" in hostname or hostname.startswith("apps-")
        # .bin substitution: update only, never credentials/private keys
        if path.lower().endswith(".bin") and not is_creds and BIN:
            try:
                data = Path(BIN).read_bytes()
            except OSError as exc:
                ctx.log.error(f"BIN read failed: {exc}")
            else:
                flow.response = http.Response.make(200, data, {
                    "Content-Type": "application/octet-stream",
                    "Cache-Control": "no-store",
                })
                ctx.log.info(
                    f"BIN served {len(data)} bytes for {hostname}{path}")
                return
        tls = flow.client_conn.tls
        flow.request.scheme = "https" if tls else "http"
        flow.request.host = hostname
        flow.request.port = 443 if tls else 80
        flow.server_conn.sni = hostname if tls else None

    def response(self, flow: http.HTTPFlow):
        hostname = host_of(flow)
        if "apps-creds" not in hostname or not flow.response:
            return
        path = flow.request.path.split("?", 1)[0]
        if not (path.lower().endswith(".bin") or path.lower().endswith(".pem")):
            return
        if flow.response.status_code != 200:
            ctx.log.info(f"CREDS {flow.response.status_code} {path} (not saved)")
            return
        try:
            SAVE.mkdir(parents=True, exist_ok=True)
            name = re.sub(r"[^A-Za-z0-9._-]", "_", path.rsplit("/", 1)[-1])
            (SAVE / name).write_bytes(flow.response.content)
            ctx.log.info(f"CREDS saved {name} ({len(flow.response.content)} B)")
        except OSError as exc:
            ctx.log.error(f"CREDS save failed: {exc}")


addons = [Subdominios()]
