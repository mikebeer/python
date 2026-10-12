# urllib.request (minimal): Request and urlopen for plain http:// URLs via http.client
import socket
from urllib.parse import urlparse, urlsplit, urljoin, quote, unquote, urlencode
from urllib.error import URLError, HTTPError
import http.client

__all__ = ['Request', 'urlopen', 'URLError', 'HTTPError', 'quote', 'unquote', 'urlencode']


class Request:
    def __init__(self, url, data=None, headers={}, origin_req_host=None, unverifiable=False, method=None):
        self.full_url = url
        self.data = data
        self.headers = {}
        self._method = method
        for k in headers:
            self.add_header(k, headers[k])
        p = urlsplit(url)
        self.type = p.scheme
        self.host = p.netloc
        self.selector = p.path or '/'
        if p.query:
            self.selector += '?' + p.query

    def add_header(self, key, val):
        self.headers[key.capitalize()] = val

    def has_header(self, name):
        return name.capitalize() in self.headers

    def get_header(self, name, default=None):
        return self.headers.get(name.capitalize(), default)

    def header_items(self):
        return list(self.headers.items())

    def get_method(self):
        if self._method is not None:
            return self._method
        return 'POST' if self.data is not None else 'GET'

    def get_full_url(self):
        return self.full_url


class _Resp:
    def __init__(self, r, url):
        self._r = r
        self.status = r.status
        self.reason = r.reason
        self.headers = r.headers
        self.url = url
        self.msg = r.reason
        self.code = r.status

    def read(self, amt=None):
        return self._r.read(amt)

    def getcode(self):
        return self.status

    def geturl(self):
        return self.url

    def info(self):
        return self.headers

    def getheader(self, name, default=None):
        return self._r.getheader(name, default)

    def close(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *a):
        pass


def urlopen(url, data=None, timeout=None, *, context=None):
    req = url if isinstance(url, Request) else Request(url, data)
    if data is not None and req.data is None:
        req.data = data
    if req.type != 'http':
        raise URLError('unknown url type: %s' % req.type)
    conn = http.client.HTTPConnection(req.host, timeout=timeout)
    hdrs = {'Connection': 'close', 'User-agent': 'Python-urllib/3.13'}
    for k in req.headers:
        hdrs[k] = req.headers[k]
    body = req.data
    if body is not None and 'Content-type' not in hdrs:
        hdrs['Content-type'] = 'application/x-www-form-urlencoded'
    try:
        conn.request(req.get_method(), req.selector, body, hdrs)
        r = conn.getresponse()
    except OSError as e:
        raise URLError(e)
    resp = _Resp(r, req.full_url)
    if r.status >= 400:
        raise HTTPError(req.full_url, r.status, r.reason, r.headers, resp)
    return resp
