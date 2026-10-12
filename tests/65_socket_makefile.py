# socket.socketpair and socket.makefile (text and binary, over a pair and over TCP loopback)
import socket

a, b = socket.socketpair()
a.sendall(b"hello\nworld\n")
rb = b.makefile('rb')
print(rb.readline(), rb.readline())
wb = b.makefile('wb')
wb.write(b"reply\n")
wb.flush()
print(a.recv(100))
wb.close()
rb.close()

w = a.makefile('w')
r = b.makefile('r')
w.write("line1\nline2\n")
w.flush()
print(repr(r.readline()), repr(r.readline()))
w.write("tail")
w.close()
a.close()
print(repr(r.read()), repr(r.readline()))
r.close()
b.close()

# a closed makefile does not close the socket
x, y = socket.socketpair()
f = x.makefile('wb')
f.write(b'abc')
f.close()
x.sendall(b'def')
x.close()
print(y.recv(10), y.recv(10))
y.close()

# TCP loopback with a read/write file object on each side
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.bind(('127.0.0.1', 0))
srv.listen(1)
c = socket.create_connection(srv.getsockname())
s, _ = srv.accept()
cf = c.makefile('rw')
sf = s.makefile('rw')
cf.write("ping\n")
cf.flush()
print(sf.readline().strip())
sf.write("pong\n")
sf.flush()
print(cf.readline().strip())
for o in (cf, sf, c, s, srv):
    o.close()

try:
    socket.socketpair()[0].makefile('x')
except ValueError as e:
    print('ValueError', e)
