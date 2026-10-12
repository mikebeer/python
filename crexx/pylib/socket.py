# socket module for the CREXX interpreter: TCP over the CREXX rxsocket library (via the internal _sock builtin).
# Blocking IPv4 stream sockets only; no UDP, no IPv6, no real select().
import time as _time

AF_UNSPEC = 0
AF_INET = 2
AF_INET6 = 10
SOCK_STREAM = 1
SOCK_DGRAM = 2
SOL_SOCKET = 1
SO_REUSEADDR = 2
SO_KEEPALIVE = 9
SO_BROADCAST = 6
SO_RCVBUF = 8
SO_SNDBUF = 7
SO_LINGER = 13
SO_REUSEPORT = 15
IPPROTO_TCP = 6
IPPROTO_IP = 0
TCP_NODELAY = 1
SHUT_RD = 0
SHUT_WR = 1
SHUT_RDWR = 2
SOMAXCONN = 4096
INADDR_ANY = 0
has_ipv6 = False

error = OSError
timeout = TimeoutError


class gaierror(OSError):
    pass


class herror(OSError):
    pass


def _raise_status(st, text):
    low = text.lower()
    if "refused" in low:
        raise ConnectionRefusedError(111, "Connection refused")
    if "timed out" in low or "timeout" in low or st == 2:
        raise TimeoutError("timed out")
    if "reset" in low:
        raise ConnectionResetError(104, "Connection reset by peer")
    if "in use" in low:
        raise OSError(98, "Address already in use")
    if "pipe" in low:
        raise BrokenPipeError(32, "Broken pipe")
    if text == "":
        text = "socket operation failed"
    raise OSError(text)


def gethostname():
    return "localhost"


def gethostbyname(name):
    if name == "" or name == "0.0.0.0":
        return "0.0.0.0"
    if name == "localhost" or name == "localhost.localdomain":
        return "127.0.0.1"
    parts = name.split(".")
    if len(parts) == 4:
        ok = True
        for p in parts:
            if not p.isdigit() or int(p) > 255:
                ok = False
        if ok:
            return name
    raise gaierror(-2, "Name or service not known")


def gethostbyname_ex(name):
    ip = gethostbyname(name)
    return (name, [], [ip])


def getfqdn(name=""):
    return name or gethostname()


def inet_aton(s):
    parts = s.split(".")
    if len(parts) != 4:
        raise OSError("illegal IP address string passed to inet_aton")
    out = []
    for p in parts:
        if not p.isdigit() or int(p) > 255:
            raise OSError("illegal IP address string passed to inet_aton")
        out.append(int(p))
    return bytes(out)


def inet_ntoa(b):
    if len(b) != 4:
        raise OSError("packed IP wrong length for inet_ntoa")
    return "%d.%d.%d.%d" % (b[0], b[1], b[2], b[3])


def htons(x):
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF)


ntohs = htons


def htonl(x):
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF)


ntohl = htonl


def _split(ep):
    i = ep.rfind(":")
    if i < 0:
        return (ep, 0)
    return (ep[:i], int(ep[i + 1:]))


class socket:
    def __init__(self, family=AF_INET, type=SOCK_STREAM, proto=0, fileno=None):
        if family != AF_INET or type != SOCK_STREAM:
            raise OSError(97, "Address family not supported by protocol")
        self.family = family
        self.type = type
        self.proto = proto
        self._h = _sock(1, 0) if fileno is None else fileno
        self._timeout = None
        self._closed = False
        self._listening = False
        self._bound = False
        self._pending = 0

    def _check(self):
        if self._closed:
            raise OSError(9, "Bad file descriptor")

    def fileno(self):
        return self._h if not self._closed else -1

    def settimeout(self, value):
        self._check()
        if value is None:
            self._timeout = None
            _sock(9, self._h, 0)
        else:
            if value < 0:
                raise ValueError("Timeout value out of range")
            self._timeout = float(value)
            ms = int(value * 1000)
            if ms < 1:
                ms = 1
            _sock(9, self._h, ms)

    def gettimeout(self):
        return self._timeout

    def setblocking(self, flag):
        if flag:
            self.settimeout(None)
        else:
            self.settimeout(0.0)

    def getblocking(self):
        return self._timeout is None or self._timeout > 0

    def setsockopt(self, level, opt, value, optlen=None):
        self._check()
        if level == IPPROTO_TCP and opt == TCP_NODELAY:
            _sock(16, self._h, 1 if value else 0)

    def getsockopt(self, level, opt, buflen=None):
        return 0

    def bind(self, addr):
        self._check()
        host, port = addr
        if host == "":
            host = "0.0.0.0"
        host = gethostbyname(host)
        st = _sock(4, self._h, host, port)
        if st != 0:
            if st < 0 and "use" in _sock(11, self._h).lower():
                raise OSError(98, "Address already in use")
            _raise_status(st, _sock(11, self._h))
        self._bound = True

    def listen(self, backlog=128):
        self._check()
        st = _sock(5, self._h, backlog)
        if st != 0:
            _raise_status(st, _sock(11, self._h))
        self._listening = True

    def _probe(self, ms):
        """select(): wait up to ms milliseconds for a connection on a listening socket and keep it for accept()"""
        if self._pending:
            return True
        _sock(9, self._h, ms if ms > 0 else 1)
        h = _sock(6, self._h)
        st = _sock(10, self._h)
        t = self._timeout
        _sock(9, self._h, 0 if t is None else max(1, int(t * 1000)))
        if h > 0 and st == 0:
            self._pending = h
            return True
        return False

    def accept(self):
        self._check()
        if self._pending:
            h = self._pending
            self._pending = 0
            s = socket(AF_INET, SOCK_STREAM, 0, h)
            s._timeout = None
            return (s, _split(_sock(12, h)))
        h = _sock(6, self._h)
        st = _sock(10, self._h)
        if h <= 0 or st != 0:
            _raise_status(st if st != 0 else h, _sock(11, self._h))
        s = socket(AF_INET, SOCK_STREAM, 0, h)
        s._timeout = None
        return (s, _split(_sock(12, h)))

    def connect_ex(self, addr):
        try:
            self.connect(addr)
        except ConnectionRefusedError:
            return 111
        except TimeoutError:
            return 11
        except OSError:
            return 1
        return 0

    def connect(self, addr):
        self._check()
        host, port = addr
        if host == "":
            host = "127.0.0.1"
        if host == "localhost":
            host = "127.0.0.1"
        st = _sock(3, self._h, host, port)
        if st != 0:
            _raise_status(st, _sock(11, self._h))

    def send(self, data, flags=0):
        self._check()
        if not isinstance(data, (bytes, bytearray)):
            raise TypeError("a bytes-like object is required, not '%s'" % type(data).__name__)
        n = _sock(7, self._h, bytes(data))
        if n < 0:
            _raise_status(n, _sock(11, self._h))
        return n

    def sendall(self, data, flags=0):
        self._check()
        data = bytes(data)
        total = len(data)
        sent = 0
        while sent < total:
            n = self.send(data[sent:])
            if n <= 0:
                raise BrokenPipeError(32, "Broken pipe")
            sent += n

    def recv(self, bufsize, flags=0):
        self._check()
        data = _sock(8, self._h, bufsize)
        if len(data) == 0:
            st = _sock(10, self._h)
            if st == 2:
                raise TimeoutError("timed out")
            if st < 0:
                _raise_status(st, _sock(11, self._h))
        return data

    def recv_into(self, buf, nbytes=0, flags=0):
        data = self.recv(nbytes or len(buf))
        for i in range(len(data)):
            buf[i] = data[i]
        return len(data)

    def shutdown(self, how):
        self._check()
        _sock(14, self._h, how)

    def getsockname(self):
        self._check()
        return _split(_sock(13, self._h))

    def getpeername(self):
        self._check()
        ep = _sock(12, self._h)
        if ep == "":
            raise OSError(107, "Transport endpoint is not connected")
        return _split(ep)

    def close(self):
        if not self._closed:
            self._closed = True
            if self._pending:
                _sock(2, self._pending)
                self._pending = 0
            _sock(2, self._h)

    def detach(self):
        self._closed = True
        return self._h

    def makefile(self, mode="r", buffering=None, encoding=None, errors=None, newline=None):
        for ch in mode:
            if ch not in "rwb":
                raise ValueError("invalid mode %r (only r, w, b allowed)" % (mode,))
        return _FileObj(self, mode, encoding or "utf-8")

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def __repr__(self):
        return "<socket.socket fd=%d, family=%d, type=%d, proto=%d>" % (self.fileno(), self.family, self.type, self.proto)


SocketType = socket


class _FileObj:
    def __init__(self, sock, mode, enc):
        self._s = sock
        self._binary = "b" in mode
        self._enc = enc
        self._buf = b""
        self.closed = False

    def _fill(self):
        d = self._s.recv(4096)
        if d == b"":
            return False
        self._buf += d
        return True

    def _out(self, b):
        if self._binary:
            return b
        return b.decode(self._enc)

    def readline(self, limit=-1):
        while b"\n" not in self._buf:
            if not self._fill():
                break
        i = self._buf.find(b"\n")
        if i < 0:
            line = self._buf
            self._buf = b""
        else:
            line = self._buf[:i + 1]
            self._buf = self._buf[i + 1:]
        return self._out(line)

    def read(self, n=-1):
        if n is None or n < 0:
            while self._fill():
                pass
            d = self._buf
            self._buf = b""
        else:
            while len(self._buf) < n:
                if not self._fill():
                    break
            d = self._buf[:n]
            self._buf = self._buf[n:]
        return self._out(d)

    def __iter__(self):
        return self

    def __next__(self):
        line = self.readline()
        if len(line) == 0:
            raise StopIteration
        return line

    def write(self, data):
        if not self._binary:
            data = data.encode(self._enc)
        self._s.sendall(data)
        return len(data)

    def flush(self):
        pass

    def close(self):
        self.closed = True

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


def create_connection(address, timeout=None, source_address=None):
    s = socket(AF_INET, SOCK_STREAM)
    if timeout is not None:
        s.settimeout(timeout)
    try:
        s.connect(address)
    except OSError:
        s.close()
        raise
    return s


def create_server(address, family=AF_INET, backlog=None, reuse_port=False, dualstack_ipv6=False):
    s = socket(family, SOCK_STREAM)
    s.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1)
    s.bind(address)
    s.listen(backlog if backlog else 128)
    return s


def socketpair(*a):
    srv = socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    c = socket()
    c.connect(srv.getsockname())
    p, _ = srv.accept()
    srv.close()
    return (c, p)


def getaddrinfo(host, port, family=0, type=0, proto=0, flags=0):
    ip = gethostbyname(host if host else "")
    return [(AF_INET, SOCK_STREAM, 6, "", (ip, int(port) if port else 0))]


def setdefaulttimeout(t):
    pass


def getdefaulttimeout():
    return None
