# Pure Python urllib.parse (common API)
import re as _re

uses_relative = ['', 'ftp', 'http', 'gopher', 'nntp', 'imap', 'wais', 'file', 'https', 'shttp', 'mms',
                 'prospero', 'rtsp', 'rtsps', 'rtspu', 'sftp', 'svn', 'svn+ssh', 'ws', 'wss']
uses_netloc = ['', 'ftp', 'http', 'gopher', 'nntp', 'telnet', 'imap', 'wais', 'file', 'mms', 'https', 'shttp',
               'snews', 'prospero', 'rtsp', 'rtsps', 'rtspu', 'rsync', 'svn', 'svn+ssh', 'sftp', 'nfs', 'git',
               'git+ssh', 'ws', 'wss']
uses_params = ['', 'ftp', 'hdl', 'prospero', 'http', 'imap', 'https', 'shttp', 'rtsp', 'rtsps', 'rtspu', 'sip',
               'sips', 'mms', 'sftp', 'tel']
non_hierarchical = ['gopher', 'hdl', 'mailto', 'news', 'telnet', 'wais', 'imap', 'snews', 'sip', 'sips']
uses_query = ['', 'http', 'wais', 'imap', 'https', 'shttp', 'mms', 'gopher', 'rtsp', 'rtsps', 'rtspu', 'sip', 'sips']
uses_fragment = ['', 'ftp', 'hdl', 'http', 'gopher', 'news', 'nntp', 'wais', 'https', 'shttp', 'snews', 'file',
                 'prospero']
scheme_chars = ('abcdefghijklmnopqrstuvwxyz' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ' '0123456789' '+-.')
_WHATWG_C0_CONTROL_OR_SPACE = '\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f '
_UNSAFE_URL_BYTES_TO_REMOVE = ['\t', '\r', '\n']


def _noop(obj):
    return obj


def _coerce_args(*args):
    str_input = isinstance(args[0], str)
    for arg in args[1:]:
        if arg and isinstance(arg, str) != str_input:
            raise TypeError("Cannot mix str and non-str arguments")
    if str_input:
        return args + (_noop,)
    return _decode_args(args) + (_encode_result,)


def _decode_args(args, encoding='ascii', errors='strict'):
    return tuple([x.decode(encoding, errors) if x else '' for x in args])


def _encode_result(obj, encoding='ascii', errors='strict'):
    return obj.encode(encoding, errors)


class _ResultMixinStr:
    def encode(self, encoding='ascii', errors='strict'):
        return self._encoded_counterpart(*(x.encode(encoding, errors) for x in self))


class _ResultMixinBytes:
    def decode(self, encoding='ascii', errors='strict'):
        return self._decoded_counterpart(*(x.decode(encoding, errors) for x in self))


class _NetlocResultMixinBase:
    @property
    def username(self):
        return self._userinfo[0]

    @property
    def password(self):
        return self._userinfo[1]

    @property
    def hostname(self):
        hostname = self._hostinfo[0]
        if not hostname:
            return None
        separator = '%' if isinstance(hostname, str) else b'%'
        hostname, percent, zone = hostname.partition(separator)
        return hostname.lower() + percent + zone

    @property
    def port(self):
        port = self._hostinfo[1]
        if port is not None:
            if port.isdigit() and port.isascii():
                port = int(port)
            else:
                raise ValueError("Port could not be cast to integer value as %r" % port)
            if not (0 <= port <= 65535):
                raise ValueError("Port out of range 0-65535")
        return port


class _NetlocResultMixinStr(_NetlocResultMixinBase, _ResultMixinStr):
    @property
    def _userinfo(self):
        netloc = self.netloc
        userinfo, have_info, hostinfo = netloc.rpartition('@')
        if have_info:
            username, have_password, password = userinfo.partition(':')
            if not have_password:
                password = None
        else:
            username = password = None
        return username, password

    @property
    def _hostinfo(self):
        netloc = self.netloc
        _, _, hostinfo = netloc.rpartition('@')
        _, have_open_br, bracketed = hostinfo.partition('[')
        if have_open_br:
            hostname, _, port = bracketed.partition(']')
            _, _, port = port.partition(':')
        else:
            hostname, _, port = hostinfo.partition(':')
        if not port:
            port = None
        return hostname, port


class _NetlocResultMixinBytes(_NetlocResultMixinBase, _ResultMixinBytes):
    @property
    def _userinfo(self):
        netloc = self.netloc
        userinfo, have_info, hostinfo = netloc.rpartition(b'@')
        if have_info:
            username, have_password, password = userinfo.partition(b':')
            if not have_password:
                password = None
        else:
            username = password = None
        return username, password

    @property
    def _hostinfo(self):
        netloc = self.netloc
        _, _, hostinfo = netloc.rpartition(b'@')
        _, have_open_br, bracketed = hostinfo.partition(b'[')
        if have_open_br:
            hostname, _, port = bracketed.partition(b']')
            _, _, port = port.partition(b':')
        else:
            hostname, _, port = hostinfo.partition(b':')
        if not port:
            port = None
        return hostname, port


class _Result(tuple):
    _fields = ()

    def __new__(cls, *args):
        return tuple.__new__(cls, args)

    def _replace(self, **kw):
        vals = list(self)
        f = type(self)._fields
        for k in kw:
            vals[f.index(k)] = kw[k]
        return type(self)(*vals)

    def _asdict(self):
        f = type(self)._fields
        d = {}
        for i in range(len(f)):
            d[f[i]] = self[i]
        return d

    def __repr__(self):
        f = type(self)._fields
        return type(self).__name__ + '(' + ', '.join([f[i] + '=' + repr(self[i]) for i in range(len(f))]) + ')'


class DefragResult(_Result, _ResultMixinStr):
    _fields = ('url', 'fragment')
    url = property(lambda self: self[0])
    fragment = property(lambda self: self[1])

    def geturl(self):
        if self.fragment:
            return self.url + '#' + self.fragment
        return self.url


class SplitResult(_Result, _NetlocResultMixinStr):
    _fields = ('scheme', 'netloc', 'path', 'query', 'fragment')
    scheme = property(lambda self: self[0])
    netloc = property(lambda self: self[1])
    path = property(lambda self: self[2])
    query = property(lambda self: self[3])
    fragment = property(lambda self: self[4])

    def geturl(self):
        return urlunsplit(self)


class ParseResult(_Result, _NetlocResultMixinStr):
    _fields = ('scheme', 'netloc', 'path', 'params', 'query', 'fragment')
    scheme = property(lambda self: self[0])
    netloc = property(lambda self: self[1])
    path = property(lambda self: self[2])
    params = property(lambda self: self[3])
    query = property(lambda self: self[4])
    fragment = property(lambda self: self[5])

    def geturl(self):
        return urlunparse(self)


class DefragResultBytes(_Result, _ResultMixinBytes):
    _fields = ('url', 'fragment')
    url = property(lambda self: self[0])
    fragment = property(lambda self: self[1])

    def geturl(self):
        if self.fragment:
            return self.url + b'#' + self.fragment
        return self.url


class SplitResultBytes(_Result, _NetlocResultMixinBytes):
    _fields = ('scheme', 'netloc', 'path', 'query', 'fragment')
    scheme = property(lambda self: self[0])
    netloc = property(lambda self: self[1])
    path = property(lambda self: self[2])
    query = property(lambda self: self[3])
    fragment = property(lambda self: self[4])

    def geturl(self):
        return urlunsplit(self)


class ParseResultBytes(_Result, _NetlocResultMixinBytes):
    _fields = ('scheme', 'netloc', 'path', 'params', 'query', 'fragment')
    scheme = property(lambda self: self[0])
    netloc = property(lambda self: self[1])
    path = property(lambda self: self[2])
    params = property(lambda self: self[3])
    query = property(lambda self: self[4])
    fragment = property(lambda self: self[5])

    def geturl(self):
        return urlunparse(self)


DefragResult._encoded_counterpart = DefragResultBytes
SplitResult._encoded_counterpart = SplitResultBytes
ParseResult._encoded_counterpart = ParseResultBytes
DefragResultBytes._decoded_counterpart = DefragResult
SplitResultBytes._decoded_counterpart = SplitResult
ParseResultBytes._decoded_counterpart = ParseResult


def _splitnetloc(url, start=0):
    delim = len(url)
    for c in '/?#':
        wdelim = url.find(c, start)
        if wdelim >= 0:
            delim = min(delim, wdelim)
    return url[start:delim], url[delim:]


def _checknetloc(netloc):
    pass


def urlsplit(url, scheme='', allow_fragments=True):
    url, scheme, _coerce_result = _coerce_args(url, scheme)
    url = url.lstrip(_WHATWG_C0_CONTROL_OR_SPACE)
    scheme = scheme.strip(_WHATWG_C0_CONTROL_OR_SPACE)
    for b in _UNSAFE_URL_BYTES_TO_REMOVE:
        url = url.replace(b, '')
        scheme = scheme.replace(b, '')
    allow_fragments = bool(allow_fragments)
    netloc = query = fragment = ''
    i = url.find(':')
    if i > 0 and url[0].isascii() and url[0].isalpha():
        for c in url[:i]:
            if c not in scheme_chars:
                break
        else:
            scheme, url = url[:i].lower(), url[i + 1:]
    if url[:2] == '//':
        netloc, url = _splitnetloc(url, 2)
        if (('[' in netloc and ']' not in netloc) or (']' in netloc and '[' not in netloc)):
            raise ValueError("Invalid IPv6 URL")
    if allow_fragments and '#' in url:
        url, fragment = url.split('#', 1)
    if '?' in url:
        url, query = url.split('?', 1)
    v = SplitResult(scheme, netloc, url, query, fragment)
    return _coerce_result(v)


def urlunsplit(components):
    scheme, netloc, url, query, fragment, _coerce_result = _coerce_args(*components)
    if netloc or (scheme and scheme in uses_netloc and url[:2] != '//'):
        if url and url[:1] != '/':
            url = '/' + url
        url = '//' + (netloc or '') + url
    if scheme:
        url = scheme + ':' + url
    if query:
        url = url + '?' + query
    if fragment:
        url = url + '#' + fragment
    return _coerce_result(url)


def _splitparams(url):
    if '/' in url:
        i = url.find(';', url.rfind('/'))
        if i < 0:
            return url, ''
    else:
        i = url.find(';')
    return url[:i], url[i + 1:]


def urlparse(url, scheme='', allow_fragments=True):
    url, scheme, _coerce_result = _coerce_args(url, scheme)
    splitresult = urlsplit(url, scheme, allow_fragments)
    scheme, netloc, url, query, fragment = splitresult
    if scheme in uses_params and ';' in url:
        url, params = _splitparams(url)
    else:
        params = ''
    result = ParseResult(scheme, netloc, url, params, query, fragment)
    return _coerce_result(result)


def urlunparse(components):
    scheme, netloc, url, params, query, fragment, _coerce_result = _coerce_args(*components)
    if params:
        url = "%s;%s" % (url, params)
    return _coerce_result(urlunsplit((scheme, netloc, url, query, fragment)))


def urljoin(base, url, allow_fragments=True):
    if not base:
        return url
    if not url:
        return base
    base, url, _coerce_result = _coerce_args(base, url)
    bscheme, bnetloc, bpath, bparams, bquery, bfragment = urlparse(base, '', allow_fragments)
    scheme, netloc, path, params, query, fragment = urlparse(url, bscheme, allow_fragments)
    if scheme != bscheme or scheme not in uses_relative:
        return _coerce_result(url)
    if scheme in uses_netloc:
        if netloc:
            return _coerce_result(urlunparse((scheme, netloc, path, params, query, fragment)))
        netloc = bnetloc
    if not path and not params:
        path = bpath
        params = bparams
        if not query:
            query = bquery
        return _coerce_result(urlunparse((scheme, netloc, path, params, query, fragment)))
    base_parts = bpath.split('/')
    if base_parts[-1] != '':
        del base_parts[-1]
    if path[:1] == '/':
        segments = path.split('/')
    else:
        segments = base_parts + path.split('/')
        segments[1:-1] = filter(None, segments[1:-1])
    resolved_path = []
    for seg in segments:
        if seg == '..':
            try:
                resolved_path.pop()
            except IndexError:
                pass
        elif seg == '.':
            continue
        else:
            resolved_path.append(seg)
    if segments[-1] in ('.', '..'):
        resolved_path.append('')
    return _coerce_result(urlunparse((scheme, netloc, '/'.join(resolved_path) or '/', params, query, fragment)))


def urldefrag(url):
    url, _coerce_result = _coerce_args(url)[0:1] + (None,) if False else (url, None)
    if isinstance(url, bytes):
        s = url.decode('ascii')
        if '#' in s:
            u, f = s.split('#', 1)
        else:
            u, f = s, ''
        return DefragResultBytes(u.encode('ascii'), f.encode('ascii'))
    if '#' in url:
        s, n, p, a, q, frag = urlparse(url)
        defrag = urlunparse((s, n, p, a, q, ''))
    else:
        frag = ''
        defrag = url
    return DefragResult(defrag, frag)


_hexdig = '0123456789ABCDEFabcdef'
_hextobyte = None


def unquote_to_bytes(string):
    if not string:
        string.split
        return b''
    if isinstance(string, str):
        string = string.encode('utf-8')
    bits = string.split(b'%')
    if len(bits) == 1:
        return string
    res = [bits[0]]
    for item in bits[1:]:
        try:
            if len(item) < 2:
                raise ValueError()
            res.append(bytes([int(item[:2].decode('ascii'), 16)]))
            if item[0] in b'+- ' or item[1] in b'+- ':
                raise ValueError()
            res.append(item[2:])
        except ValueError:
            res.append(b'%')
            res.append(item)
    return b''.join(res)


def unquote(string, encoding='utf-8', errors='replace'):
    if isinstance(string, bytes):
        return unquote_to_bytes(string).decode(encoding, errors)
    if '%' not in string:
        return string
    return unquote_to_bytes(string).decode(encoding, errors)


def unquote_plus(string, encoding='utf-8', errors='replace'):
    string = string.replace('+', ' ')
    return unquote(string, encoding, errors)


_ALWAYS_SAFE = frozenset(b'ABCDEFGHIJKLMNOPQRSTUVWXYZ' b'abcdefghijklmnopqrstuvwxyz' b'0123456789' b'_.-~')


def quote_from_bytes(bs, safe='/'):
    if not isinstance(bs, (bytes, bytearray)):
        raise TypeError("quote_from_bytes() expected bytes")
    if not bs:
        return ''
    if isinstance(safe, str):
        safe = safe.encode('ascii', 'ignore')
    else:
        safe = bytes([c for c in safe if c < 128])
    out = []
    for c in bs:
        if c in _ALWAYS_SAFE or c in safe:
            out.append(chr(c))
        else:
            out.append('%' + '0123456789ABCDEF'[c >> 4] + '0123456789ABCDEF'[c & 15])
    return ''.join(out)


def quote(string, safe='/', encoding=None, errors=None):
    if isinstance(string, str):
        if not string:
            return string
        if encoding is None:
            encoding = 'utf-8'
        if errors is None:
            errors = 'strict'
        string = string.encode(encoding, errors)
    else:
        if encoding is not None:
            raise TypeError("quote() doesn't support 'encoding' for bytes")
        if errors is not None:
            raise TypeError("quote() doesn't support 'errors' for bytes")
    return quote_from_bytes(string, safe)


def quote_plus(string, safe='', encoding=None, errors=None):
    if ((isinstance(string, str) and ' ' not in string) or (isinstance(string, bytes) and b' ' not in string)):
        return quote(string, safe, encoding, errors)
    if isinstance(safe, str):
        space = ' '
    else:
        space = b' '
    string = quote(string, safe + space, encoding, errors)
    return string.replace(' ', '+')


def parse_qsl(qs, keep_blank_values=False, strict_parsing=False, encoding='utf-8', errors='replace',
              max_num_fields=None, separator='&'):
    if not separator or not isinstance(separator, (str, bytes)):
        raise ValueError("Separator must be of type string or bytes.")
    if isinstance(qs, str):
        if not isinstance(separator, str):
            separator = str(separator, 'ascii')
        eq = '='
    else:
        qs = qs.decode('latin-1')
        separator = separator if isinstance(separator, str) else separator.decode('latin-1')
        eq = '='
    r = []
    if max_num_fields is not None:
        num_fields = 1 + qs.count(separator) if qs else 0
        if max_num_fields < num_fields:
            raise ValueError('Max number of fields exceeded')
    for name_value in qs.split(separator):
        if not name_value and not strict_parsing:
            continue
        nv = name_value.split('=', 1)
        if len(nv) != 2:
            if strict_parsing:
                raise ValueError("bad query field: %r" % (name_value,))
            if keep_blank_values:
                nv.append('')
            else:
                continue
        if len(nv[1]) or keep_blank_values:
            name = nv[0].replace('+', ' ')
            name = unquote(name, encoding=encoding, errors=errors)
            value = nv[1].replace('+', ' ')
            value = unquote(value, encoding=encoding, errors=errors)
            r.append((name, value))
    return r


def parse_qs(qs, keep_blank_values=False, strict_parsing=False, encoding='utf-8', errors='replace',
             max_num_fields=None, separator='&'):
    parsed_result = {}
    pairs = parse_qsl(qs, keep_blank_values, strict_parsing, encoding=encoding, errors=errors,
                      max_num_fields=max_num_fields, separator=separator)
    for name, value in pairs:
        if name in parsed_result:
            parsed_result[name].append(value)
        else:
            parsed_result[name] = [value]
    return parsed_result


def urlencode(query, doseq=False, safe='', encoding=None, errors=None, quote_via=quote_plus):
    if hasattr(query, "items"):
        query = query.items()
    else:
        query = list(query)
        if len(query) and not isinstance(query[0], tuple):
            raise TypeError("not a valid non-string sequence or mapping object")
    l = []
    if not doseq:
        for k, v in query:
            if isinstance(k, bytes):
                k = quote_via(k, safe)
            else:
                k = quote_via(str(k), safe, encoding, errors)
            if isinstance(v, bytes):
                v = quote_via(v, safe)
            else:
                v = quote_via(str(v), safe, encoding, errors)
            l.append(k + '=' + v)
    else:
        for k, v in query:
            if isinstance(k, bytes):
                k = quote_via(k, safe)
            else:
                k = quote_via(str(k), safe, encoding, errors)
            if isinstance(v, bytes):
                v = quote_via(v, safe)
                l.append(k + '=' + v)
            elif isinstance(v, str):
                v = quote_via(v, safe, encoding, errors)
                l.append(k + '=' + v)
            else:
                try:
                    x = len(v)
                except TypeError:
                    v = quote_via(str(v), safe, encoding, errors)
                    l.append(k + '=' + v)
                else:
                    for elt in v:
                        if isinstance(elt, bytes):
                            elt = quote_via(elt, safe)
                        else:
                            elt = quote_via(str(elt), safe, encoding, errors)
                        l.append(k + '=' + elt)
    return '&'.join(l)


def splittype(url):
    m = _re.match('([^/:]+):(.*)', url, _re.DOTALL)
    if m:
        scheme, data = m.groups()
        return scheme.lower(), data
    return None, url


def splithost(url):
    m = _re.match('//([^/#?]*)(.*)', url, _re.DOTALL)
    if m:
        host_port, path = m.groups()
        if path and path[0] != '/':
            path = '/' + path
        return host_port, path
    return None, url


def to_bytes(url):
    return str(url)
