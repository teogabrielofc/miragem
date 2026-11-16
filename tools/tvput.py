#!/usr/bin/env python3
import socket,sys,time,os
HOST=os.environ.get('ROKU_HOST','192.168.1.100')
PORT=int(os.environ.get('ROKU_PORT','2323'))
path=sys.argv[1]; fn=sys.argv[2]
data=open(fn,'rb').read()
s=socket.create_connection((HOST,PORT),timeout=6); s.settimeout(2)
time.sleep(0.4)
try: print('banner',repr(s.recv(200)))
except Exception: pass
s.sendall(('put %s %d\n'%(path,len(data))).encode())
time.sleep(0.2)
s.sendall(data)
out=b''; t0=time.time()
while time.time()-t0<30:
    try:
        d=s.recv(4096)
        if not d: break
        out+=d
        if b'put:' in out: break
    except socket.timeout: continue
    except Exception: break
print(out.decode('utf-8','replace')[:500])
s.close()
