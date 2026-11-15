import socket,sys,threading
port=int(sys.argv[1]); data=open(sys.argv[2],'rb').read()
s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
s.bind(('0.0.0.0',port)); s.listen(8)
print('srvrld',port,len(data),flush=True)
def h(c,a):
    print('conn',a,flush=True)
    try:
        c.sendall(data); c.shutdown(socket.SHUT_WR)
    except Exception as e: print('send',e,flush=True)
    try: c.close()
    except: pass
while True:
    try: c,a=s.accept()
    except Exception as e: continue
    threading.Thread(target=h,args=(c,a),daemon=True).start()
