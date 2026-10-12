# http.client (minimal): HTTPConnection/HTTPSConnection-less (plain HTTP) with HTTPResponse parsing.
import socket
from http import HTTPStatus

HTTP_PORT = 80
HTTPS_PORT = 443
from http import _TABLE
responses = {}
for _k in _TABLE:
    responses[_k] = _TABLE[_k][0]
for _n in ('CONTINUE', 'OK', 'CREATED', 'ACCEPTED', 'NO_CONTENT', 'MOVED_PERMANENTLY', 'FOUND', 'SEE_OTHER',
           'NOT_MODIFIED', 'TEMPORARY_REDIRECT', 'PERMANENT_REDIRECT', 'BAD_REQUEST', 'UNAUTHORIZED', 'FORBIDDEN',
           'NOT_FOUND', 'METHOD_NOT_ALLOWED', 'REQUEST_TIMEOUT', 'CONFLICT', 'GONE', 'IM_A_TEAPOT',
           'TOO_MANY_REQUESTS', 'INTERNAL_SERVER_ERROR', 'NOT_IMPLEMENTED', 'BAD_GATEWAY', 'SERVICE_UNAVAILABLE',
           'GATEWAY_TIMEOUT'):
    globals()[_n] = getattr(HTTPStatus, _n)


class HTTPException(Exception):
    pass


class NotConnected(HTTPException):
    pass


class InvalidURL(HTTPException):
    pass


class BadStatusLine(HTTPException):
    def __init__(self, line):
        if not line:
            line = repr(line)
        self.args = (line,)
        self.line = line


class RemoteDisconnected(ConnectionResetError, BadStatusLine):
    def __init__(self, *pos, **kw):
        BadStatusLine.__init__(self, "")


class IncompleteRead(HTTPException):
    def __init__(self, partial, expected=None):
        self.partial = partial
        self.expected = expected
        self.args = (partial, expected)


class ResponseNotReady(HTTPException):
    pass


class _Headers:
    def __init__(self):
        self._items = []

    def add(self, k, v):
        self._items.append((k, v))

    def get(self, name, default=None):
        n = name.lower()
        for k, v in self._items:
            if k.lower() == n:
                return v
        return default

    def get_all(self, name, default=None):
        n = name.lower()
        r = [v for k, v in self._items if k.lower() == n]
        return r if r else default

    def items(self):
        return list(self._items)

    def keys(self):
        return [k for k, v in self._items]

    def values(self):
        return [v for k, v in self._items]

    def __getitem__(self, name):
        return self.get(name)

    def __contains__(self, name):
        return self.get(name) is not None

    def __len__(self):
        return len(self._items)

    def __iter__(self):
        return iter(self.keys())


class _Reader:
    """Buffered reader over a socket-like object with recv()."""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b''
        self.eof = False

    def _fill(self):
        if self.eof:
            return False
        d = self.sock.recv(8192)
        if not d:
            self.eof = True
            return False
        self.buf += d
        return True

    def readline(self):
        while True:
            i = self.buf.find(b'\n')
            if i >= 0:
                line = self.buf[:i + 1]
                self.buf = self.buf[i + 1:]
                return line
            if not self._fill():
                line = self.buf
                self.buf = b''
                return line

    def read(self, n=None):
        if n is None:
            while self._fill():
                pass
            d = self.buf
            self.buf = b''
            return d
        while len(self.buf) < n and self._fill():
            pass
        d = self.buf[:n]
        self.buf = self.buf[n:]
        return d


class HTTPResponse:
    def __init__(self, sock, debuglevel=0, method=None, url=None):
        self.fp = _Reader(sock)
        self._method = method
        self.headers = self.msg = None
        self.version = None
        self.status = None
        self.reason = None
        self.chunked = False
        self.length = None
        self.will_close = True
        self.url = url
        self._left = None
        self._done = False

    def begin(self):
        line = self.fp.readline().decode('iso-8859-1')
        if not line:
            raise RemoteDisconnected("Remote end closed connection without response")
        parts = line.split(None, 2)
        if len(parts) < 2:
            raise BadStatusLine(line)
        version = parts[0]
        if not version.startswith('HTTP/'):
            raise BadStatusLine(line)
        try:
            status = int(parts[1])
        except ValueError:
            raise BadStatusLine(line)
        reason = parts[2].strip() if len(parts) > 2 else ''
        self.version = 11 if version == 'HTTP/1.1' else 10
        self.status = status
        self.reason = reason
        self.headers = self.msg = _Headers()
        while True:
            h = self.fp.readline().decode('iso-8859-1')
            if h in ('\r\n', '\n', ''):
                break
            if ':' in h:
                k, v = h.split(':', 1)
                self.headers.add(k.strip(), v.strip())
        te = self.headers.get('transfer-encoding')
        self.chunked = bool(te and te.lower() == 'chunked')
        cl = self.headers.get('content-length')
        if cl and not self.chunked:
            self.length = int(cl)
        conn = (self.headers.get('connection') or '').lower()
        self.will_close = conn == 'close' or (self.version == 10 and conn != 'keep-alive')
        if status in (204, 304) or 100 <= status < 200 or self._method == 'HEAD':
            self.length = 0

    def _read_chunked(self):
        out = b''
        while True:
            line = self.fp.readline().strip()
            if b';' in line:
                line = line.split(b';')[0]
            n = int(line.decode('ascii'), 16) if line else 0
            if n == 0:
                while True:
                    t = self.fp.readline()
                    if t in (b'\r\n', b'\n', b''):
                        break
                break
            out += self.fp.read(n)
            self.fp.readline()
        return out

    def read(self, amt=None):
        if self._done:
            return b''
        if self.chunked:
            data = self._read_chunked()
        elif self.length is not None:
            data = self.fp.read(self.length)
            if len(data) < self.length:
                self._done = True
                raise IncompleteRead(data, self.length - len(data))
        else:
            data = self.fp.read()
        self._done = True
        return data

    def getheader(self, name, default=None):
        if self.headers is None:
            raise ResponseNotReady()
        v = self.headers.get_all(name)
        if v is None:
            return default
        return ', '.join(v)

    def getheaders(self):
        return self.headers.items()

    def getcode(self):
        return self.status

    def geturl(self):
        return self.url

    def info(self):
        return self.headers

    def close(self):
        pass

    def isclosed(self):
        return self._done

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


class HTTPConnection:
    default_port = HTTP_PORT
    response_class = HTTPResponse

    def __init__(self, host, port=None, timeout=None, source_address=None, blocksize=8192):
        self.timeout = timeout
        self.sock = None
        self._buffer = []
        self._method = None
        self.__response = None
        self._set_hostport(host, port)

    def _set_hostport(self, host, port):
        if port is None:
            i = host.rfind(':')
            j = host.rfind(']')
            if i > j:
                try:
                    port = int(host[i + 1:])
                except ValueError:
                    raise InvalidURL("nonnumeric port: '%s'" % host[i + 1:])
                host = host[:i]
            else:
                port = self.default_port
            if host and host[0] == '[' and host[-1] == ']':
                host = host[1:-1]
        self.host = host
        self.port = port

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), self.timeout)

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def putrequest(self, method, url, skip_host=False, skip_accept_encoding=False):
        self._method = method
        if not url:
            url = '/'
        self._buffer = ['%s %s HTTP/1.1' % (method, url)]
        if not skip_host:
            host = self.host
            if ':' in host:
                host = '[' + host + ']'
            self.putheader('Host', host if self.port == self.default_port else '%s:%d' % (host, self.port))
        if not skip_accept_encoding:
            self.putheader('Accept-Encoding', 'identity')

    def putheader(self, header, *values):
        self._buffer.append('%s: %s' % (header, '\r\n\t'.join([str(v) for v in values])))

    def endheaders(self, message_body=None):
        self._buffer.extend(['', ''])
        msg = '\r\n'.join(self._buffer).encode('latin-1')
        self._buffer = []
        if self.sock is None:
            self.connect()
        self.sock.sendall(msg)
        if message_body is not None:
            if isinstance(message_body, str):
                message_body = message_body.encode('iso-8859-1')
            self.sock.sendall(message_body)

    def request(self, method, url, body=None, headers={}, *, encode_chunked=False):
        hdrs = dict(headers)
        lower = [k.lower() for k in hdrs]
        self.putrequest(method, url, skip_host='host' in lower, skip_accept_encoding='accept-encoding' in lower)
        if body is not None and 'content-length' not in lower:
            if isinstance(body, str):
                body = body.encode('iso-8859-1')
            self.putheader('Content-Length', str(len(body)))
        elif body is None and method in ('POST', 'PUT', 'PATCH') and 'content-length' not in lower:
            self.putheader('Content-Length', '0')
        for k in hdrs:
            self.putheader(k, hdrs[k])
        self.endheaders(body)

    def getresponse(self):
        resp = self.response_class(self.sock, method=self._method)
        resp.begin()
        return resp
