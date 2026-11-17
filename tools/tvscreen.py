#!/usr/bin/env python3
"""tvscreen.py - Miragem: espelha a tela da TV (fb1) no PC.

Conecta no `fbserve` que roda na TV (tools/fbserve.arm) e mostra os frames
numa janela (ffplay ou Tk), ou salva um PNG com --shot. Sem audio.

Protocolo: veja tools/fbserve.c (cabecalho "FBS2"; tiles 64x64 ou frame cru).

Exemplos:
  python3 tools/tvscreen.py --host 192.168.1.100            # janela
  python3 tools/tvscreen.py --host 192.168.1.100 --shot tela.png
  python3 tools/tvscreen.py --host 192.168.1.100 --tk --scale 2
"""
import argparse
import os
import shutil
import socket
import struct
import subprocess
import sys
import time
import zlib

SESSION = struct.Struct("<4s7I")   # magic, w, h, stride, bpp, fps, format, tile
FRAME = struct.Struct("<3I")       # payload_len, seq, flags


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("conexao encerrada pela TV")
        buf += chunk
    return bytes(buf)


def open_stream(host, port, wait=8.0):
    """Conecta com retry: o fbserve pode levar alguns instantes para subir."""
    deadline = time.time() + wait
    last = None
    while True:
        s = None
        try:
            s = socket.create_connection((host, port), timeout=2.0)
            s.settimeout(5.0)
            head = recv_exact(s, SESSION.size)
            magic, w, h, stride, bpp, fps, fmt, tile = SESSION.unpack(head)
            if magic != b"FBS2":
                raise ConnectionError("protocolo inesperado (magic %r)" % (magic,))
            if bpp not in (16, 32) or w <= 0 or h <= 0 or tile <= 0:
                raise ConnectionError("formato nao suportado: %dx%d %dbpp" % (w, h, bpp))
            s.settimeout(None)
            return s, w, h, stride, bpp, fps, fmt, tile
        except (ConnectionError, OSError) as e:
            last = e
            if s is not None:
                try:
                    s.close()
                except Exception:
                    pass
            if time.time() > deadline:
                raise ConnectionError("nao consegui conectar em %s:%d (%s)" % (host, port, last))
            time.sleep(0.25)


def read_frame(sock):
    plen, seq, flags = FRAME.unpack(recv_exact(sock, FRAME.size))
    payload = recv_exact(sock, plen)
    return payload, seq, flags


def new_frame(w, h, bpp):
    return bytearray(w * h * (bpp // 8))


def apply_payload(frame, payload, flags, w, h, bpp, tile):
    """Atualiza o frame local: tiles (flags bit0) ou frame cru."""
    psize = bpp // 8
    if not (flags & 1):
        if len(payload) != w * h * psize:
            raise ValueError("frame cru com tamanho errado")
        frame[:] = payload
        return frame
    if len(payload) < 4:
        raise ValueError("payload de tiles curto")
    nt = struct.unpack_from("<I", payload, 0)[0]
    off = 4
    for _ in range(nt):
        if off + 4 > len(payload):
            raise ValueError("cabecalho de tile curto")
        tx, ty = struct.unpack_from("<HH", payload, off)
        off += 4
        tw = min(tile, w - tx * tile)
        th = min(tile, h - ty * tile)
        if tw <= 0 or th <= 0:
            raise ValueError("tile fora da tela")
        n = tw * th * psize
        data = payload[off:off + n]
        if len(data) != n:
            raise ValueError("tile incompleto")
        off += n
        rowbytes = tw * psize
        for y in range(th):
            dst = ((ty * tile + y) * w + tx * tile) * psize
            frame[dst:dst + rowbytes] = data[y * rowbytes:(y + 1) * rowbytes]
    return frame


def bgra_to_rgb(data, w, h):
    rgb = bytearray(w * h * 3)
    rgb[0::3] = data[2::4]
    rgb[1::3] = data[1::4]
    rgb[2::3] = data[0::4]
    return rgb


def rgb565_to_rgb(data, w, h):
    out = bytearray(w * h * 3)
    for i in range(w * h):
        v = data[2 * i] | (data[2 * i + 1] << 8)
        out[3 * i] = ((v >> 11) & 0x1F) * 255 // 31
        out[3 * i + 1] = ((v >> 5) & 0x3F) * 255 // 63
        out[3 * i + 2] = (v & 0x1F) * 255 // 31
    return out


def to_rgb(data, w, h, bpp):
    return bgra_to_rgb(data, w, h) if bpp == 32 else rgb565_to_rgb(data, w, h)


def chunk(tag, blob):
    return (struct.pack(">I", len(blob)) + tag + blob +
            struct.pack(">I", zlib.crc32(tag + blob) & 0xFFFFFFFF))


def write_png(path, w, h, rgb):
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 6)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def ffplay_cmd(w, h, bpp, scale):
    pix = "bgra" if bpp == 32 else "rgb565le"
    cmd = ["ffplay", "-loglevel", "error", "-f", "rawvideo",
           "-pixel_format", pix, "-video_size", "%dx%d" % (w, h),
           "-framerate", "30", "-infbuf", "-window_title", "Miragem - TV"]
    if scale and scale != 1:
        cmd += ["-vf", "scale=%d:%d" % (w * scale, h * scale)]
    cmd += ["-i", "-"]
    return cmd


def run_ffplay(host, port, scale):
    s, w, h, stride, bpp, fps, fmt, tile = open_stream(host, port)
    cmd = ffplay_cmd(w, h, bpp, scale)
    if not shutil.which(cmd[0]):
        s.close()
        raise FileNotFoundError(cmd[0])
    print("tvscreen: %dx%d %dbpp, tiles %d, fbserve %d fps -> ffplay" %
          (w, h, bpp, tile, fps))
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    frame = new_frame(w, h, bpp)
    time.sleep(0.25)
    if proc.poll() is not None:
        s.close()
        raise RuntimeError("ffplay nao abriu a janela (sem DISPLAY?); use --tk ou rode no desktop")
    try:
        while proc.poll() is None:
            payload, seq, flags = read_frame(s)
            apply_payload(frame, payload, flags, w, h, bpp, tile)
            proc.stdin.write(frame)
            proc.stdin.flush()
    except (BrokenPipeError, ConnectionError, KeyboardInterrupt):
        pass
    finally:
        s.close()
        try:
            proc.stdin.close()
        except Exception:
            pass
        try:
            proc.terminate()
        except Exception:
            pass


def run_tk(host, port, scale):
    try:
        import tkinter as tk
    except Exception as e:
        raise RuntimeError("tkinter indisponivel (%s); instale python3-tk ou use ffplay" % e)
    try:
        from PIL import Image, ImageTk
        use_pil = True
    except Exception:
        use_pil = False

    s, w, h, stride, bpp, fps, fmt, tile = open_stream(host, port)
    print("tvscreen: %dx%d %dbpp, tiles %d, fbserve %d fps -> tkinter%s" %
          (w, h, bpp, tile, fps, " + PIL" if use_pil else " (PPM)"))
    try:
        root = tk.Tk()
    except Exception as e:
        s.close()
        raise RuntimeError("nao consegui abrir a janela Tk (%s); use ffplay" % e)
    root.title("Miragem - TV")
    label = tk.Label(root, bd=0, highlightthickness=0)
    label.pack()
    state = {"stop": False, "img": None}

    def on_close():
        state["stop"] = True
        try:
            root.destroy()
        except Exception:
            pass

    root.protocol("WM_DELETE_WINDOW", on_close)
    frame = new_frame(w, h, bpp)

    try:
        while not state["stop"]:
            payload, seq, flags = read_frame(s)
            apply_payload(frame, payload, flags, w, h, bpp, tile)
            rgb = to_rgb(frame, w, h, bpp)
            if use_pil:
                img = Image.frombytes("RGB", (w, h), bytes(rgb))
                if scale and scale != 1:
                    img = img.resize((w * scale, h * scale), Image.NEAREST)
                photo = ImageTk.PhotoImage(img)
            else:
                ppm = b"P6\n%d %d\n255\n" % (w, h) + bytes(rgb)
                photo = tk.PhotoImage(data=ppm, format="PPM")
                if scale and scale != 1:
                    photo = photo.zoom(scale, scale)
            state["img"] = photo
            label.configure(image=photo)
            root.update()
    except (ConnectionError, KeyboardInterrupt):
        pass
    finally:
        s.close()
        try:
            root.destroy()
        except Exception:
            pass


def run_shot(host, port, path):
    s, w, h, stride, bpp, fps, fmt, tile = open_stream(host, port)
    try:
        payload, seq, flags = read_frame(s)
    finally:
        s.close()
    frame = new_frame(w, h, bpp)
    apply_payload(frame, payload, flags, w, h, bpp, tile)
    write_png(path, w, h, to_rgb(frame, w, h, bpp))
    print("tvscreen: %dx%d salvo em %s" % (w, h, path))


def main():
    ap = argparse.ArgumentParser(description="espelha a tela da TV (fb1)")
    ap.add_argument("--host", default=os.environ.get("ROKU_HOST"),
                    help="IP da TV (ou ROKU_HOST)")
    ap.add_argument("--port", type=int, default=int(os.environ.get("SCREEN_PORT", "2331")))
    ap.add_argument("--shot", metavar="PNG", help="salva um frame e sai")
    ap.add_argument("--scale", type=int, default=1, help="amplia a janela (1..4)")
    ap.add_argument("--tk", action="store_true", help="forca a janela Tk (sem ffplay)")
    ap.add_argument("--ffplay", action="store_true", help="forca ffplay")
    args = ap.parse_args()

    if not args.host:
        ap.error("defina --host ou ROKU_HOST")
    if args.scale < 1 or args.scale > 4:
        ap.error("--scale deve ser 1..4")

    try:
        if args.shot:
            run_shot(args.host, args.port, args.shot)
        elif args.tk or not shutil.which("ffplay"):
            run_tk(args.host, args.port, args.scale)
        else:
            run_ffplay(args.host, args.port, args.scale)
    except FileNotFoundError as e:
        print("tvscreen: %s nao encontrado; use --tk ou instale ffmpeg" % e, file=sys.stderr)
        return 1
    except RuntimeError as e:
        print("tvscreen: %s" % e, file=sys.stderr)
        return 1
    except (ConnectionError, OSError) as e:
        print("tvscreen: %s" % e, file=sys.stderr)
        return 1
    except ValueError as e:
        print("tvscreen: frame invalido: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
