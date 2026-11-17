#!/usr/bin/env python3
import os,socket,sys,time
HOST=os.environ.get('ROKU_HOST','192.168.1.100')
PORT=int(os.environ.get('ROKU_PORT','2323'))
path=sys.argv[1]; out=sys.argv[2]
s=socket.create_connection((HOST,PORT),timeout=6); s.settimeout(2)
time.sleep(0.4)
banner=b''
try: banner=s.recv(4096)
except Exception: pass
s.sendall(('get %s\n'%path).encode())
buf=b''; t0=time.time()
while time.time()-t0<20:
    try:
        d=s.recv(65536)
        if not d: break
        buf+=d
        if b'DATA ' in buf:
            nl=buf.find(b'\n',buf.find(b'DATA '))
            if nl>=0:
                try: n=int(buf[buf.find(b'DATA ')+5:nl])
                except: n=0
                if n>0 and len(buf)>=nl+1+n:
                    open(out,'wb').write(buf[nl+1:nl+1+n]); print('saved',out,n); s.close(); sys.exit(0)
    except socket.timeout: continue
    except Exception: break
s.close(); print('FAILED',len(buf),repr(buf[:200])); sys.exit(1)
