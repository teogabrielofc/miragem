# Security policy

## Scope

Miragem is security research tooling for a **Roku H536X (Philco PTV24G5YR2CP)**
television running Roku OS **15.0.4.5537** (Linux 4.9.118, ARMv7). It was written
and tested on a device owned by the researcher. It is published so that the
findings can be reviewed, reproduced on other owned devices, and fixed by the
vendor.

**It is not a product and comes with no warranty.** A wrong address in the
kernel write primitive, a bad flash write, or a partial update can brick the TV.

## Using it safely

- Run it **only** on hardware you own or are explicitly authorized to test.
- Keep the TV and the host PC on a trusted, isolated LAN.
- The root agent opens **unauthenticated** shells on TCP **2222** (PTY) and
  optionally **2323** (command/file transfer). Anyone on the same network can
  use them while they are up. Stop them when you are done (`/api/tvsh/stop` in
  the manager, or kill the agent) and never expose them to the internet.
- The mitmproxy foothold impersonates third-party hosts and uses a forged
  certificate chain. Do not point it at traffic that is not yours.
- Do not accept a Roku OS update while researching this chain: 16.0 removes the
  certificate chain the foothold depends on (and may fix GhostLock).

## What is not in this repository

- Roku firmware images, boot images, NAND dumps.
- Roku proprietary source (including `roku_mfg_lib` / the MFG app).
- Certificates, private keys, device keys, TV serial numbers or MAC addresses.
- `.pkg` / `.jwt` channel packages or decrypted plugin payloads.
- Account credentials, tokens, or the researcher's LAN addressing.

If you find any of the above here, treat it as a mistake and open an issue (or a
private report) so it can be removed.

## Reporting

- **Bugs in this repository**: open an issue with the model, Roku OS build,
  kernel version, and the exact step that failed. Do not attach firmware or
  proprietary files.
- **Vulnerabilities in Roku products**: report them through Roku's own
  responsible-disclosure channel. Do not send this repository's exploit at a
  device you do not own.
- **Leaked secrets in this repository**: contact the maintainers privately
  before opening a public issue.

## Supported versions

The kernel offsets are specific to the exact 15.0.4.5537 build. Other builds are
unsupported and will likely panic the device. See `docs/RE.md`.
