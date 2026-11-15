# Foothold — Ginga/DTV Play + MITM + Chromium 104 (CVE-2023-3079)

This directory preserves the research page and the supporting code for the uid=500
foothold. It is not a one-click exploit: the deployment scripts that copy files to the
author's Windows mitmproxy machine are private and are not included.

## Chain

1. The TV is on the **Antena TV** input tuned to an ISDB DTV channel with Ginga/DTV
   Play. The TV's native Ginga app loads an iframe:
   `https://interatividade-tv.globo.com/ems/`.
2. A mitmproxy reverse proxy on the LAN intercepts the request. The TV accepts the
   forged certificate chain because the test unit trusts **eMudhra/AAA**; mitmproxy
   12.3's native `--emudhra` option signs the certificate with that chain. Revocation/
   CT are not enforced by this RokuBrowser build.
3. `ginga_serve.py` serves `ginga_fingerprint.html` at the iframe path, logs
   `/beacon`, and serves `/cmd` (`cmd.js`) from the same directory.
4. The page auto-runs `/cmd` after 2.5 s and on every keydown. `cmd.js` is the
   **CVE-2023-3079** V8 exploit against **Chromium 104.0.5112.70**, the RokuBrowser
   engine. The renderer runs with `--no-sandbox --single-process`; seccomp is 0.
5. The V8 arbitrary read/write bootstraps a manual ELF/RLD loader (`sc38.bin`): it
   downloads an RLD container from the host over TCP, maps it at its link address, and
   jumps. The RLD is the uid=500 agent (or the auto-root bootstrap).
6. The uid=500 agent connects to the host (47140). The host driver runs the GhostLock
   child and the root PTY shell (see `docs/SHELL.md`).

## Files

| File | Purpose |
|---|---|
| `ginga_fingerprint.html` | research page; `/cmd` fetch, `/beacon` logging, keydown/AUTORUN |
| `ginga_serve.py` | mitmproxy addon: serves the page at `interatividade-tv.globo.com/ems/`, `/cmd`, `/beacon` |
| `proxy_subdominios.py` | reverse-proxy addon: host allow-list, host/SNI selection, optional firmware `.bin` substitution, creds capture |
| `start_mitm.bat`, `run_ginga.bat` | example mitmproxy commands (placeholders) |
| `cmd_nat46.js` | V8 exploit base (training, leak, fake object, native call) |
| `sc38.bin` | RLD loader shellcode (host IP sanitized to 192.168.1.2; port 47127) |

The final `cmd.js` used on the TV is built by combining the V8 base with the RLD
loader and the deployment URL (the author's `runrld`/`cmd_runrld` builder is private).
The page itself only fetches whatever is in `cmd.js`.

## Setup (placeholders)

- `ROKU_TV`: TV LAN address (default `192.168.1.100`).
- The mitmproxy machine: set `--listen-host` to its LAN address; the TV's DNS or
  router must resolve `interatividade-tv.globo.com` to it, and the TV must trust the
  eMudhra/AAA chain (`--emudhra`).
- `ROKU_FIRMWARE`: optional path to an AImage firmware `.bin` for the substitution
  test (empty disables it).
- `ROKU_CREDS`: directory for the `apps-creds` capture (default `creds`).
- `dominio_alvo` must include the iframe host (`interatividade-tv.globo.com` /
  `globo.com`). The example `.bat` uses `roku.com,globo.com`.
- The page's `/cmd` is `cmd.js`; the `/beacon` log records page loads and exploit
  stages.

The `.bat` files are examples; adjust the mitmproxy path and the network addresses.
The real deployment (copying `cmd.js` to the Windows machine, the RLD servers, the
auto-root driver) is kept private because it contains machine-specific paths and
credentials.

## Roku OS 16.0

Roku OS 16.0 removes the eMudhra certificate chain from the trust store. The MITM
page then fails to load (the TV rejects the forged certificate), so the Ginga
foothold — and therefore root — stops working. Staying on 15.0.4 is required for
this chain. The TV's `SoftwareUpdate/Package` was changed from `beta` to `release`.

## Legal

The page and code here are for the author's own TV. The eMudhra/AAA certificate
material, Roku firmware, and the channel content are third-party property. Do not use
the MITM against third-party devices or networks.

## Repository note (Miragem)

- The loader shellcode `sc38.bin` is a compiled payload and is **not** committed
  (see `.gitignore`); the same bytes are embedded in the installer page as the
  `SCTXT` hex string. Build or extract it locally before running `cmd.js`.
- Host and TV addresses in this directory are placeholders
  (`192.168.1.2` host, `192.168.1.100` TV, `192.168.1.3` Windows) and the
  deployment user is `user`. See `docs/INSTALL.md` and `docs/RE.md`.
