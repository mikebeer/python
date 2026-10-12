import socket
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 0))
srv.listen(5)
host, port = srv.getsockname()
print(host, port > 0)
cli = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
cli.connect(("127.0.0.1", port))
conn, addr = srv.accept()
print(addr[0], addr[1] > 0)
cli.sendall(b"hello server")
data = conn.recv(100)
print(data, len(data))
conn.sendall(b"echo: " + data)
print(cli.recv(100).decode())
cli.send(b"line1\nline2\n")
f = conn.makefile("r") if False else None
buf = b""
while b"line2\n" not in buf:
    buf += conn.recv(4)
print(buf.split(b"\n"))
conn.close()
print(cli.recv(10))
cli.close(); srv.close()
cli = socket.socket()
cli.settimeout(0.5)
try:
    cli.connect(("127.0.0.1", 1))
except OSError as e:
    print("OSError", isinstance(e, ConnectionRefusedError))
print(socket.gethostname() != "", socket.htons(1) == 256 or socket.htons(1) == 1, socket.inet_aton("1.2.3.4") == b"\x01\x02\x03\x04", socket.inet_ntoa(b"\x7f\x00\x00\x01"))
print(socket.gethostbyname("localhost"), socket.AF_INET == 2, socket.SOCK_STREAM == 1)
