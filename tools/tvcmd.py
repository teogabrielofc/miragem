#!/usr/bin/env python3
import os,socket,sys,time
HOST=os.environ.get('ROKU_HOST','192.168.1.100')
PORT=int(os.environ.get('ROKU_PORT','2222'))
cmd=' '.join(sys.argv[1:])
s=socket.create_connection((HOST,PORT),timeout=6); s.settimeout(1.0)
time.sleep(0.9)
try: s.recv(4096)
except Exception: pass
mark='__M%d__'%int(time.time()*1000)
s.sendall((cmd+'; echo '+mark+'\n').encode())
out=b''; t0=time.time()
while time.time()-t0<25:
    try:
        d=s.recv(65536)
        if not d: break
        out+=d
        if out.count(mark.encode()) >= 2: break
    except socket.timeout: continue
    except Exception: break
s.close()
txt=out.decode('utf-8','replace'); i=txt.rfind(mark)
print(txt[:i] if i>=0 else txt)
