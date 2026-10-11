"""json: dumps/dump/loads/load in pure Python."""

# strings are bytes (UTF-8) in one interpreter and code points in the other
_BYTES = len(chr(233)) != 1
_HEX = "0123456789abcdef"
_CTLS = [chr(i) for i in range(32) if i not in (8, 9, 10, 12, 13)]


_chr = chr


class JSONDecodeError(ValueError):
    def __init__(self, msg, doc, pos):
        lineno = doc.count("\n", 0, pos) + 1
        colno = pos - doc.rfind("\n", 0, pos)
        ValueError.__init__(self, "%s: line %d column %d (char %d)" % (msg, lineno, colno, pos))
        self.msg = msg
        self.doc = doc
        self.pos = pos
        self.lineno = lineno
        self.colno = colno


def _u4(n):
    return "\\u" + _HEX[(n >> 12) & 15] + _HEX[(n >> 8) & 15] + _HEX[(n >> 4) & 15] + _HEX[n & 15]


def _escape_slow(s, ascii_only):
    out = []
    n = len(s)
    i = 0
    while i < n:
        c = s[i]
        o = ord(c)
        i += 1
        if o < 128:
            if o < 32:
                out.append(_u4(o))
            else:
                out.append(c)
            continue
        if not ascii_only:
            out.append(c)
            continue
        if _BYTES:
            # decode one UTF-8 sequence from byte values
            if o >= 240 and i + 2 < n:
                cp = ((o & 7) << 18) | ((ord(s[i]) & 63) << 12) | ((ord(s[i + 1]) & 63) << 6) | (ord(s[i + 2]) & 63)
                i += 3
            elif o >= 224 and i + 1 < n:
                cp = ((o & 15) << 12) | ((ord(s[i]) & 63) << 6) | (ord(s[i + 1]) & 63)
                i += 2
            elif o >= 192 and i < n:
                cp = ((o & 31) << 6) | (ord(s[i]) & 63)
                i += 1
            else:
                cp = o
        else:
            cp = o
        if cp >= 65536:
            cp -= 65536
            out.append(_u4(55296 | (cp >> 10)))
            out.append(_u4(56320 | (cp & 1023)))
        else:
            out.append(_u4(cp))
    return "".join(out)


def _quote(s, ascii_only=True):
    if s.isalnum() and s.isascii():
        return '"' + s + '"'
    if s.isascii():
        s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r")
        s = s.replace("\t", "\\t").replace("\x08", "\\b").replace("\x0c", "\\f")
        for c in _CTLS:
            if c in s:
                return '"' + _escape_slow(s, ascii_only) + '"'
        return '"' + s + '"'
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r")
    s = s.replace("\t", "\\t").replace("\x08", "\\b").replace("\x0c", "\\f")
    return '"' + _escape_slow(s, ascii_only) + '"'


def _float_repr(f, allow_nan):
    if f != f:
        t = "NaN"
    elif f == float("inf"):
        t = "Infinity"
    elif f == float("-inf"):
        t = "-Infinity"
    else:
        return repr(f)
    if not allow_nan:
        raise ValueError("Out of range float values are not JSON compliant: " + repr(f))
    return t


class JSONEncoder:
    def __init__(self, skipkeys=False, ensure_ascii=True, check_circular=True, allow_nan=True,
                 sort_keys=False, indent=None, separators=None, default=None):
        self.skipkeys = skipkeys
        self.ensure_ascii = ensure_ascii
        self.allow_nan = allow_nan
        self.sort_keys = sort_keys
        if isinstance(indent, int) and not isinstance(indent, bool):
            indent = " " * indent
        self.indent = indent
        if separators is not None:
            self.item_separator = separators[0]
            self.key_separator = separators[1]
        elif indent is not None:
            self.item_separator = ","
            self.key_separator = ": "
        else:
            self.item_separator = ", "
            self.key_separator = ": "
        if default is not None:
            self.default = default

    def default(self, o):
        raise TypeError("Object of type %s is not JSON serializable" % type(o).__name__)

    def encode(self, o):
        out = []
        self._enc(o, out, 0)
        return "".join(out)

    def iterencode(self, o):
        return iter([self.encode(o)])

    def _key(self, k):
        if isinstance(k, str):
            return k
        if k is True:
            return "true"
        if k is False:
            return "false"
        if k is None:
            return "null"
        if isinstance(k, int):
            return str(k)
        if isinstance(k, float):
            return _float_repr(k, self.allow_nan)
        return None

    def _enc(self, o, out, level):
        if isinstance(o, str):
            out.append(_quote(o, self.ensure_ascii))
        elif o is None:
            out.append("null")
        elif o is True:
            out.append("true")
        elif o is False:
            out.append("false")
        elif isinstance(o, int):
            out.append(str(o))
        elif isinstance(o, float):
            out.append(_float_repr(o, self.allow_nan))
        elif isinstance(o, (list, tuple)):
            if not o:
                out.append("[]")
                return
            ind = self.indent
            if ind is None:
                sep = self.item_separator
                nl = ""
            else:
                nl = "\n" + ind * (level + 1)
                sep = self.item_separator + nl
                out.append("[" + nl)
            if ind is None:
                out.append("[")
            first = True
            for v in o:
                if not first:
                    out.append(sep)
                first = False
                self._enc(v, out, level + 1)
            if ind is None:
                out.append("]")
            else:
                out.append("\n" + ind * level + "]")
        elif isinstance(o, dict):
            if not o:
                out.append("{}")
                return
            ind = self.indent
            if ind is None:
                sep = self.item_separator
                out.append("{")
            else:
                nl = "\n" + ind * (level + 1)
                sep = self.item_separator + nl
                out.append("{" + nl)
            items = list(o.items())
            if self.sort_keys:
                items = sorted(items, key=lambda kv: kv[0])
            first = True
            for k, v in items:
                ks = self._key(k)
                if ks is None:
                    if self.skipkeys:
                        continue
                    raise TypeError("keys must be str, int, float, bool or None, not %s" % type(k).__name__)
                if not first:
                    out.append(sep)
                first = False
                out.append(_quote(ks, self.ensure_ascii))
                out.append(self.key_separator)
                self._enc(v, out, level + 1)
            if ind is None:
                out.append("}")
            else:
                out.append("\n" + ind * level + "}")
        else:
            self._enc(self.default(o), out, level)


def dumps(obj, skipkeys=False, ensure_ascii=True, check_circular=True, allow_nan=True, cls=None,
          indent=None, separators=None, default=None, sort_keys=False, **kw):
    if cls is None:
        cls = JSONEncoder
    return cls(skipkeys=skipkeys, ensure_ascii=ensure_ascii, allow_nan=allow_nan, indent=indent,
               separators=separators, default=default, sort_keys=sort_keys).encode(obj)


def dump(obj, fp, **kw):
    fp.write(dumps(obj, **kw))


_WS = " \t\n\r"
_NUMCH = "0123456789+-.eE"
_ESC = {'"': '"', "\\": "\\", "/": "/", "b": "\x08", "f": "\x0c", "n": "\n", "r": "\r", "t": "\t"}


class JSONDecoder:
    def __init__(self, object_hook=None, parse_float=None, parse_int=None, parse_constant=None,
                 strict=True, object_pairs_hook=None):
        self.object_hook = object_hook
        self.object_pairs_hook = object_pairs_hook
        self.parse_float = parse_float
        self.parse_int = parse_int
        self.parse_constant = parse_constant
        self.s = ""
        self.n = 0

    def decode(self, s):
        self.s = s
        self.n = len(s)
        i = self._ws(0)
        v, i = self._value(i)
        i = self._ws(i)
        if i != self.n:
            raise JSONDecodeError("Extra data", s, i)
        return v

    def raw_decode(self, s, idx=0):
        self.s = s
        self.n = len(s)
        return self._value(self._ws(idx))

    def _ws(self, i):
        s = self.s
        n = self.n
        while i < n and s[i] in _WS:
            i += 1
        return i

    def _string(self, i):
        # i is just after the opening quote
        s = self.s
        j = s.find('"', i)
        if j < 0:
            raise JSONDecodeError("Unterminated string starting at", s, i - 1)
        chunk = s[i:j]
        if "\\" not in chunk:
            return chunk, j + 1
        out = []
        n = self.n
        while True:
            j = i
            while j < n and s[j] != '"' and s[j] != "\\":
                j += 1
            if j >= n:
                raise JSONDecodeError("Unterminated string starting at", s, i - 1)
            out.append(s[i:j])
            if s[j] == '"':
                return "".join(out), j + 1
            j += 1
            if j >= n:
                raise JSONDecodeError("Unterminated string starting at", s, i - 1)
            c = s[j]
            if c == "u":
                h = s[j + 1:j + 5]
                try:
                    cp = int(h, 16)
                except ValueError:
                    raise JSONDecodeError("Invalid \\uXXXX escape", s, j - 1)
                j += 5
                if 55296 <= cp < 56320 and s[j:j + 2] == "\\u":
                    try:
                        lo = int(s[j + 2:j + 6], 16)
                    except ValueError:
                        lo = 0
                    if 56320 <= lo < 57344:
                        cp = 65536 + (((cp - 55296) << 10) | (lo - 56320))
                        j += 6
                out.append(_chr(cp))
                i = j
            elif c in _ESC:
                out.append(_ESC[c])
                i = j + 1
            else:
                raise JSONDecodeError("Invalid \\escape", s, j - 1)

    def _value(self, i):
        s = self.s
        if i >= self.n:
            raise JSONDecodeError("Expecting value", s, i)
        c = s[i]
        if c == '"':
            return self._string(i + 1)
        if c == "{":
            return self._object(i + 1)
        if c == "[":
            return self._array(i + 1)
        if c == "t" and s[i:i + 4] == "true":
            return True, i + 4
        if c == "f" and s[i:i + 5] == "false":
            return False, i + 5
        if c == "n" and s[i:i + 4] == "null":
            return None, i + 4
        if c == "N" and s[i:i + 3] == "NaN":
            return float("nan"), i + 3
        if c == "I" and s[i:i + 8] == "Infinity":
            return float("inf"), i + 8
        if c == "-" and s[i:i + 9] == "-Infinity":
            return float("-inf"), i + 9
        j = i
        n = self.n
        isf = False
        while j < n and s[j] in _NUMCH:
            if s[j] in ".eE":
                isf = True
            j += 1
        if j == i:
            raise JSONDecodeError("Expecting value", s, i)
        t = s[i:j]
        try:
            if isf:
                if self.parse_float is not None:
                    return self.parse_float(t), j
                return float(t), j
            if self.parse_int is not None:
                return self.parse_int(t), j
            return int(t), j
        except ValueError:
            raise JSONDecodeError("Expecting value", s, i)

    def _array(self, i):
        s = self.s
        res = []
        i = self._ws(i)
        if i < self.n and s[i] == "]":
            return res, i + 1
        while True:
            v, i = self._value(i)
            res.append(v)
            i = self._ws(i)
            if i >= self.n:
                raise JSONDecodeError("Expecting ',' delimiter", s, i)
            c = s[i]
            if c == "]":
                return res, i + 1
            if c != ",":
                raise JSONDecodeError("Expecting ',' delimiter", s, i)
            i = self._ws(i + 1)

    def _object(self, i):
        s = self.s
        pairs = []
        i = self._ws(i)
        if i < self.n and s[i] == "}":
            i += 1
        else:
            while True:
                if i >= self.n or s[i] != '"':
                    raise JSONDecodeError("Expecting property name enclosed in double quotes", s, i)
                k, i = self._string(i + 1)
                i = self._ws(i)
                if i >= self.n or s[i] != ":":
                    raise JSONDecodeError("Expecting ':' delimiter", s, i)
                i = self._ws(i + 1)
                v, i = self._value(i)
                pairs.append((k, v))
                i = self._ws(i)
                if i >= self.n:
                    raise JSONDecodeError("Expecting ',' delimiter", s, i)
                c = s[i]
                if c == "}":
                    i += 1
                    break
                if c != ",":
                    raise JSONDecodeError("Expecting ',' delimiter", s, i)
                i = self._ws(i + 1)
        if self.object_pairs_hook is not None:
            return self.object_pairs_hook(pairs), i
        d = {}
        for k, v in pairs:
            d[k] = v
        if self.object_hook is not None:
            return self.object_hook(d), i
        return d, i


def loads(s, cls=None, object_hook=None, parse_float=None, parse_int=None, parse_constant=None,
          object_pairs_hook=None, **kw):
    if not isinstance(s, str):
        if hasattr(s, "decode"):
            s = s.decode()
        else:
            raise TypeError("the JSON object must be str, bytes or bytearray, not %s" % type(s).__name__)
    if cls is None:
        cls = JSONDecoder
    return cls(object_hook=object_hook, parse_float=parse_float, parse_int=parse_int,
               parse_constant=parse_constant, object_pairs_hook=object_pairs_hook).decode(s)


def load(fp, **kw):
    return loads(fp.read(), **kw)
