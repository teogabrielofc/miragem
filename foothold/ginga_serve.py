"""Serve a local fingerprint page for the Ginga/Globo iframe on the Roku TV.

Load AFTER proxy_subdominios.py, with --set dominio_alvo=interatividade-tv.globo.com
and --emudhra.  The page is served at https://interatividade-tv.globo.com/ems/
and the results are logged to ginga_beacon.log.
"""
from mitmproxy import ctx, http
from pathlib import Path
from urllib.parse import urlsplit
import time
import os

BASE = Path(__file__).resolve().parent
LOG = BASE / "ginga_beacon.log"
PAGE = BASE / "ginga_fingerprint.html"
TV = os.environ.get("ROKU_TV", "192.168.1.100")
HOST = "interatividade-tv.globo.com"


def _host(flow):
    try:
        return (urlsplit("//" + (flow.request.host_header or "")).hostname or "").lower().rstrip(".")
    except ValueError:
        return ""


def _log(text):
    try:
        with open(LOG, "a", encoding="utf-8", errors="replace") as f:
            f.write(f"\n===== {time.strftime('%Y-%m-%d %H:%M:%S')} =====\n{text}\n")
    except OSError as exc:
        ctx.log.error(f"ginga: log failed: {exc}")


class GingaServe:
    def load(self, loader):
        loader.add_option("ginga_host", str, HOST, "Host served locally")
        loader.add_option("ginga_page", str, str(PAGE), "HTML page to serve")

    def requestheaders(self, flow: http.HTTPFlow):
        peer = flow.client_conn.peername
        if not peer or peer[0] != TV:
            return
        if _host(flow) != ctx.options.ginga_host:
            return
        path = flow.request.path.split("?", 1)[0]
        if path in ("/", "/ems", "/ems/", "/index.html"):
            try:
                body = Path(ctx.options.ginga_page).read_bytes()
            except OSError as exc:
                ctx.log.error(f"ginga: page missing: {exc}")
                body = b"<h1>ginga page missing</h1>"
            flow.response = http.Response.make(200, body, {
                "Content-Type": "text/html; charset=utf-8",
                "Cache-Control": "no-store",
                "Access-Control-Allow-Origin": "*",
            })
            ctx.log.info("ginga: served fingerprint page")
        elif path == "/beacon" and flow.request.method == "GET":
            q = urlsplit(flow.request.path).query
            _log("GET " + q)
            flow.response = http.Response.make(200, b"ok", {
                "Access-Control-Allow-Origin": "*", "Cache-Control": "no-store"
            })
        elif path == "/cmd":
            try:
                code = Path(BASE / "cmd.js").read_text(encoding="utf-8", errors="replace")
            except OSError:
                code = ""
            flow.response = http.Response.make(200, code.encode("utf-8"), {
                "Content-Type": "text/plain; charset=utf-8",
                "Cache-Control": "no-store",
                "Access-Control-Allow-Origin": "*",
            })

    def request(self, flow: http.HTTPFlow):
        peer = flow.client_conn.peername
        if not peer or peer[0] != TV:
            return
        if _host(flow) != ctx.options.ginga_host:
            return
        if flow.request.path.split("?", 1)[0] != "/beacon":
            return
        if flow.request.method == "POST":
            try:
                body = flow.request.content.decode("utf-8", "replace")
            except Exception:
                body = "<binary>"
            _log("POST " + body)
            flow.response = http.Response.make(200, b"ok", {
                "Access-Control-Allow-Origin": "*", "Cache-Control": "no-store"
            })


addons = [GingaServe()]
