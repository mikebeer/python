# Pure Python tomllib (loads / load)
import datetime as _dt

__all__ = ("loads", "load", "TOMLDecodeError")


class TOMLDecodeError(ValueError):
    pass


_WS = ' \t'
_BARE = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'
_ESC = {'b': '\b', 't': '\t', 'n': '\n', 'f': '\f', 'r': '\r', '"': '"', '\\': '\\'}


def load(fp, *, parse_float=float):
    b = fp.read()
    if isinstance(b, bytes):
        try:
            s = b.decode()
        except UnicodeDecodeError:
            raise TypeError("File must be opened in binary mode, e.g. use `open('foo.toml', 'rb')`")
    else:
        raise TypeError("File must be opened in binary mode, e.g. use `open('foo.toml', 'rb')`")
    return loads(s, parse_float=parse_float)


class _Flags:
    pass


class _P:
    def __init__(self, s, parse_float):
        self.s = s.replace('\r\n', '\n')
        self.i = 0
        self.n = len(self.s)
        self.pf = parse_float
        self.explicit = set()   # paths of explicitly defined tables
        self.frozen = set()     # paths of inline tables / static arrays (cannot be extended)
        self.implicit = set()

    def err(self, msg, pos=None):
        if pos is None:
            pos = self.i
        if pos >= self.n:
            raise TOMLDecodeError("%s (at end of document)" % msg)
        line = self.s.count('\n', 0, pos) + 1
        col = pos - (self.s.rfind('\n', 0, pos) + 1) + 1
        raise TOMLDecodeError("%s (at line %d, column %d)" % (msg, line, col))

    def skip_ws(self):
        s = self.s
        while self.i < self.n and s[self.i] in _WS:
            self.i += 1

    def skip_comment(self):
        s = self.s
        if self.i < self.n and s[self.i] == '#':
            while self.i < self.n and s[self.i] != '\n':
                c = s[self.i]
                if c < ' ' and c != '\t' or c == '\x7f':
                    self.err("Illegal character %r" % c)
                self.i += 1

    def skip_ws_comment_nl(self):
        s = self.s
        while self.i < self.n:
            c = s[self.i]
            if c in _WS or c == '\n':
                self.i += 1
            elif c == '#':
                self.skip_comment()
            else:
                break

    def parse(self):
        out = {}
        cur = out
        curpath = ()
        s = self.s
        while True:
            self.skip_ws_comment_nl()
            if self.i >= self.n:
                break
            c = s[self.i]
            if c == '[':
                if s[self.i + 1:self.i + 2] == '[':
                    self.i += 2
                    path = self.parse_key()
                    self.skip_ws()
                    if s[self.i:self.i + 2] != ']]':
                        self.err("Expected ']]' at the end of an array declaration")
                    self.i += 2
                    cur = self.open_aot(out, path)
                    curpath = path
                else:
                    self.i += 1
                    path = self.parse_key()
                    self.skip_ws()
                    if s[self.i:self.i + 1] != ']':
                        self.err("Expected ']' at the end of a table declaration")
                    self.i += 1
                    cur = self.open_table(out, path)
                    curpath = path
                self.end_of_line()
            else:
                keys = self.parse_key()
                self.skip_ws()
                if s[self.i:self.i + 1] != '=':
                    self.err("Expected '=' after a key in a key/value pair")
                self.i += 1
                self.skip_ws()
                val = self.parse_value()
                self.set_dotted(cur, curpath, keys, val)
                self.end_of_line()
        return out

    def end_of_line(self):
        self.skip_ws()
        self.skip_comment()
        if self.i < self.n and self.s[self.i] != '\n':
            self.err("Expected newline or end of document after a statement")

    def parse_key(self):
        self.skip_ws()
        keys = []
        while True:
            self.skip_ws()
            c = self.s[self.i:self.i + 1]
            if c == '"':
                k = self.parse_basic_string()
            elif c == "'":
                k = self.parse_literal_string()
            else:
                j = self.i
                while j < self.n and self.s[j] in _BARE:
                    j += 1
                if j == self.i:
                    self.err("Invalid initial character for a key part")
                k = self.s[self.i:j]
                self.i = j
            keys.append(k)
            self.skip_ws()
            if self.s[self.i:self.i + 1] == '.':
                self.i += 1
                continue
            break
        return tuple(keys)

    def open_table(self, root, path):
        cur = root
        for idx in range(len(path)):
            k = path[idx]
            sub = path[:idx + 1]
            last = idx == len(path) - 1
            if k in cur:
                nxt = cur[k]
                if isinstance(nxt, list):
                    if sub in self.frozen:
                        self.err("Cannot mutate immutable namespace %s" % '.'.join(sub))
                    nxt = nxt[-1]
                elif not isinstance(nxt, dict):
                    self.err("Cannot declare %s twice" % '.'.join(sub) if last else "Cannot access %s" % '.'.join(sub))
                if sub in self.frozen:
                    self.err("Cannot mutate immutable namespace %s" % '.'.join(sub))
                if last:
                    if sub in self.explicit:
                        self.err("Cannot declare %s twice" % '.'.join(sub))
                    if sub not in self.implicit:
                        self.err("Cannot declare %s twice" % '.'.join(sub))
                    self.implicit.discard(sub)
                    self.explicit.add(sub)
                cur = nxt
            else:
                nxt = {}
                cur[k] = nxt
                if last:
                    self.explicit.add(sub)
                else:
                    self.implicit.add(sub)
                cur = nxt
        return cur

    def open_aot(self, root, path):
        cur = root
        for idx in range(len(path)):
            k = path[idx]
            sub = path[:idx + 1]
            last = idx == len(path) - 1
            if last:
                if k in cur:
                    lst = cur[k]
                    if not isinstance(lst, list) or sub in self.frozen:
                        self.err("Cannot append to %s" % '.'.join(sub))
                    nt = {}
                    lst.append(nt)
                    self._clear_sub(sub)
                    return nt
                nt = {}
                cur[k] = [nt]
                self.explicit.add(sub)
                return nt
            if k in cur:
                nxt = cur[k]
                if isinstance(nxt, list):
                    nxt = nxt[-1]
                elif not isinstance(nxt, dict):
                    self.err("Cannot access %s" % '.'.join(sub))
                if sub in self.frozen:
                    self.err("Cannot mutate immutable namespace %s" % '.'.join(sub))
                cur = nxt
            else:
                nxt = {}
                cur[k] = nxt
                self.implicit.add(sub)
                cur = nxt
        return cur

    def _clear_sub(self, sub):
        n = len(sub)
        for st in (self.explicit, self.implicit, self.frozen):
            for p in list(st):
                if len(p) > n and p[:n] == sub:
                    st.discard(p)

    def set_dotted(self, cur, curpath, keys, val):
        for idx in range(len(keys) - 1):
            k = keys[idx]
            sub = curpath + keys[:idx + 1]
            if k in cur:
                nxt = cur[k]
                if not isinstance(nxt, dict) or sub in self.frozen or sub in self.explicit:
                    self.err("Cannot mutate immutable namespace %s" % '.'.join(keys[:idx + 1]) if False else "Cannot overwrite a value")
                cur = nxt
            else:
                nxt = {}
                cur[k] = nxt
                self.implicit.add(sub)
                self.explicit.add(sub) if False else None
                cur = nxt
                self.frozen_dotted = True
        last = keys[-1]
        if last in cur:
            self.err("Cannot overwrite a value")
        cur[last] = val
        full = curpath + keys
        if isinstance(val, (dict, list)):
            self.frozen.add(full)

    def parse_value(self):
        s = self.s
        c = s[self.i:self.i + 1]
        if c == '"':
            if s[self.i:self.i + 3] == '"""':
                return self.parse_ml_basic()
            return self.parse_basic_string()
        if c == "'":
            if s[self.i:self.i + 3] == "'''":
                return self.parse_ml_literal()
            return self.parse_literal_string()
        if c == '[':
            return self.parse_array()
        if c == '{':
            return self.parse_inline_table()
        if s[self.i:self.i + 4] == 'true':
            self.i += 4
            return True
        if s[self.i:self.i + 5] == 'false':
            self.i += 5
            return False
        j = self.i
        if s[j:j + 4].isdigit() and s[j + 4:j + 5] == '-' and s[j + 7:j + 8] == '-':
            j += 10
            if s[j:j + 1] in ('T', 't') or (s[j:j + 1] == ' ' and s[j + 1:j + 3].isdigit() and s[j + 3:j + 4] == ':'):
                j += 1
                while j < self.n and s[j] in '0123456789:.+-Zz':
                    j += 1
        else:
            while j < self.n and s[j] not in ',]}\n#' and s[j] not in _WS:
                j += 1
        tok = s[self.i:j].strip()
        if not tok:
            self.err("Invalid value")
        r = self.parse_scalar(tok)
        self.i = j
        return r

    def parse_scalar(self, tok):
        t = tok
        v = self.try_datetime(t)
        if v is not None:
            return v
        if t in ('inf', '+inf'):
            return float('inf')
        if t == '-inf':
            return float('-inf')
        if t in ('nan', '+nan', '-nan'):
            return float('nan')
        if len(t) > 2 and t[0] == '0' and t[1] in 'xob':
            base = {'x': 16, 'o': 8, 'b': 2}[t[1]]
            try:
                if '__' in t or t.endswith('_') or t[2] == '_':
                    raise ValueError()
                return int(t[2:].replace('_', ''), base)
            except ValueError:
                self.err("Invalid hex/octal/binary value")
        u = t
        if '_' in u:
            if '__' in u or u.startswith('_') or u.endswith('_') or '_.' in u or '._' in u or '_e' in u.lower() or 'e_' in u.lower():
                self.err("Invalid number")
            u = u.replace('_', '')
        body = u.lstrip('+-')
        if body.isdigit() and not (len(body) > 1 and body[0] == '0'):
            return int(u)
        if body and (body[0].isdigit()):
            if '.' in body:
                a, b2 = body.split('.', 1)
                if not a.isdigit() or not b2 or not (b2[0].isdigit()):
                    self.err("Invalid number")
            if len(body.split('.')[0].split('e')[0].split('E')[0]) > 1 and body[0] == '0':
                self.err("Invalid number")
            try:
                return self.pf(u)
            except ValueError:
                pass
        self.err("Invalid value")

    def try_datetime(self, t):
        try:
            if len(t) >= 10 and t[4:5] == '-' and t[7:8] == '-':
                y, m, d = int(t[:4]), int(t[5:7]), int(t[8:10])
                if len(t) == 10:
                    return _dt.date(y, m, d)
                if t[10] not in 'Tt ':
                    return None
                tm = self._time(t[11:])
                if tm is None:
                    return None
                hh, mm, ss, us, off = tm
                dtv = _dt.datetime(y, m, d, hh, mm, ss, us)
                if off is not None:
                    dtv = dtv.replace(tzinfo=off)
                return dtv
            if len(t) >= 8 and t[2:3] == ':' and t[5:6] == ':':
                tm = self._time(t)
                if tm is None or tm[4] is not None:
                    return None
                return _dt.time(tm[0], tm[1], tm[2], tm[3])
        except ValueError:
            self.err("Invalid date or datetime")
        return None

    def _time(self, t):
        off = None
        if t.endswith('Z') or t.endswith('z'):
            off = _dt.timezone.utc
            t = t[:-1]
        else:
            for sg in '+-':
                k = t.rfind(sg)
                if k > 5:
                    o = t[k + 1:]
                    if len(o) == 5 and o[2] == ':':
                        delta = _dt.timedelta(hours=int(o[:2]), minutes=int(o[3:]))
                        if sg == '-':
                            delta = -delta
                        off = _dt.timezone(delta)
                        t = t[:k]
                        break
        if len(t) < 8 or t[2] != ':' or t[5] != ':':
            return None
        hh, mm, ss = int(t[:2]), int(t[3:5]), int(t[6:8])
        us = 0
        if len(t) > 8:
            if t[8] != '.' or not t[9:].isdigit():
                return None
            us = int((t[9:] + '000000')[:6])
        return hh, mm, ss, us, off

    def parse_array(self):
        self.i += 1
        arr = []
        s = self.s
        while True:
            self.skip_ws_comment_nl()
            if s[self.i:self.i + 1] == ']':
                self.i += 1
                return arr
            arr.append(self.parse_value())
            self.skip_ws_comment_nl()
            c = s[self.i:self.i + 1]
            if c == ',':
                self.i += 1
            elif c == ']':
                self.i += 1
                return arr
            else:
                self.err("Unclosed array")

    def parse_inline_table(self):
        self.i += 1
        d = {}
        s = self.s
        self.skip_ws()
        if s[self.i:self.i + 1] == '}':
            self.i += 1
            return d
        while True:
            self.skip_ws()
            keys = self.parse_key()
            self.skip_ws()
            if s[self.i:self.i + 1] != '=':
                self.err("Expected '=' after a key in a key/value pair")
            self.i += 1
            self.skip_ws()
            val = self.parse_value()
            cur = d
            for k in keys[:-1]:
                if k in cur:
                    cur = cur[k]
                    if not isinstance(cur, dict):
                        self.err("Cannot overwrite a value")
                else:
                    cur[k] = {}
                    cur = cur[k]
            if keys[-1] in cur:
                self.err("Duplicate inline table key %r" % keys[-1])
            cur[keys[-1]] = val
            self.skip_ws()
            c = s[self.i:self.i + 1]
            if c == ',':
                self.i += 1
                continue
            if c == '}':
                self.i += 1
                return d
            self.err("Unclosed inline table")

    def parse_literal_string(self):
        j = self.s.find("'", self.i + 1)
        k = self.s.find('\n', self.i + 1)
        if j < 0 or (0 <= k < j):
            self.err("Unterminated string")
        r = self.s[self.i + 1:j]
        self.i = j + 1
        return r

    def parse_ml_literal(self):
        self.i += 3
        if self.s[self.i:self.i + 1] == '\n':
            self.i += 1
        j = self.s.find("'''", self.i)
        if j < 0:
            self.err("Unterminated multiline string")
        while self.s[j + 3:j + 4] == "'" and j + 3 < self.n:
            j += 1
        r = self.s[self.i:j]
        self.i = j + 3
        return r

    def parse_basic_string(self):
        s = self.s
        i = self.i + 1
        out = []
        while True:
            if i >= self.n:
                self.err("Unterminated string", self.i)
            c = s[i]
            if c == '"':
                self.i = i + 1
                return ''.join(out)
            if c == '\n':
                self.err("Unterminated string", self.i)
            if c == '\\':
                i, ch = self.escape(i)
                out.append(ch)
                continue
            out.append(c)
            i += 1

    def escape(self, i):
        s = self.s
        e = s[i + 1:i + 2]
        if e in _ESC:
            return i + 2, _ESC[e]
        if e == 'u' or e == 'U':
            ln = 4 if e == 'u' else 8
            h = s[i + 2:i + 2 + ln]
            try:
                if len(h) != ln:
                    raise ValueError()
                return i + 2 + ln, chr(int(h, 16))
            except ValueError:
                self.err("Invalid hex value", i)
        self.err("Unescaped '\\' in a string", i)

    def parse_ml_basic(self):
        s = self.s
        i = self.i + 3
        if s[i:i + 1] == '\n':
            i += 1
        out = []
        while True:
            if i >= self.n:
                self.err("Unterminated multiline string", self.i)
            c = s[i]
            if c == '"' and s[i:i + 3] == '"""':
                j = i + 3
                while s[j:j + 1] == '"' and j < i + 5:
                    out.append('"')
                    j += 1
                self.i = j
                return ''.join(out)
            if c == '\\':
                e = s[i + 1:i + 2]
                if e in (' ', '\t', '\n'):
                    j = i + 1
                    while j < self.n and s[j] in ' \t':
                        j += 1
                    if s[j:j + 1] != '\n':
                        self.err("Unescaped '\\' in a string", i)
                    while j < self.n and s[j] in ' \t\n':
                        j += 1
                    i = j
                    continue
                i, ch = self.escape(i)
                out.append(ch)
                continue
            out.append(c)
            i += 1


def loads(s, *, parse_float=float):
    if not isinstance(s, str):
        raise TypeError("Expected str object, not '%s'" % type(s).__name__)
    return _P(s, parse_float).parse()
