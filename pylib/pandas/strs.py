# pandas subset: .str accessor, .dt accessor stub, DataFrame.query evaluation
from .base import nan, infer, Index
from .core import Series, DataFrame, _lst

_SPECIAL = '.^$*+?{}[]\\|()'


def _isre(p):
    for ch in p:
        if ch in _SPECIAL:
            return True
    return False


def _re():
    import re
    return re


class StringMethods:
    def __init__(self, s):
        if s._dt not in ('str', 'object'):
            raise AttributeError('Can only use .str accessor with string values!')
        self.s = s

    def _map(self, f, dt=None):
        res = []
        for x in self.s._v:
            if isinstance(x, str):
                res.append(f(x))
            else:
                res.append(nan)
        return self.s._new(res)

    def _mapb(self, f, na=nan):
        if self.s._dt == 'str' and na != na:
            na = False
        res = []
        for x in self.s._v:
            if isinstance(x, str):
                res.append(f(x))
            else:
                res.append(na)
        return self.s._new(res)

    def lower(self):
        return self._map(lambda x: x.lower())

    def upper(self):
        return self._map(lambda x: x.upper())

    def title(self):
        return self._map(lambda x: x.title())

    def capitalize(self):
        return self._map(lambda x: x.capitalize())

    def swapcase(self):
        return self._map(lambda x: x.swapcase())

    def casefold(self):
        return self._map(lambda x: x.lower())

    def strip(self, to_strip=None):
        return self._map(lambda x: x.strip(to_strip))

    def lstrip(self, to_strip=None):
        return self._map(lambda x: x.lstrip(to_strip))

    def rstrip(self, to_strip=None):
        return self._map(lambda x: x.rstrip(to_strip))

    def len(self):
        res = []
        for x in self.s._v:
            res.append(len(x) if isinstance(x, str) else nan)
        return self.s._new(res)

    def contains(self, pat, case=True, flags=0, na=nan, regex=True):
        if regex and _isre(pat):
            re = _re()
            rx = re.compile(pat, flags | (0 if case else re.IGNORECASE))
            return self._mapb(lambda x: rx.search(x) is not None, na)
        if not case:
            p = pat.lower()
            return self._mapb(lambda x: p in x.lower(), na)
        return self._mapb(lambda x: pat in x, na)

    def startswith(self, pat, na=nan):
        return self._mapb(lambda x: x.startswith(pat if not isinstance(pat, list) else tuple(pat)), na)

    def endswith(self, pat, na=nan):
        return self._mapb(lambda x: x.endswith(pat if not isinstance(pat, list) else tuple(pat)), na)

    def match(self, pat, case=True, flags=0, na=nan):
        re = _re()
        rx = re.compile(pat, flags | (0 if case else re.IGNORECASE))
        return self._mapb(lambda x: rx.match(x) is not None, na)

    def fullmatch(self, pat, case=True, flags=0, na=nan):
        re = _re()
        rx = re.compile(pat, flags | (0 if case else re.IGNORECASE))
        return self._mapb(lambda x: rx.fullmatch(x) is not None, na)

    def replace(self, pat, repl, n=-1, case=None, flags=0, regex=False):
        if callable(pat) or isinstance(pat, dict):
            raise NotImplementedError
        if regex or (case is False):
            re = _re()
            rx = re.compile(pat, flags | (0 if case is not False else re.IGNORECASE))
            cnt = 0 if n < 0 else n
            return self._map(lambda x: rx.sub(repl, x, cnt))
        if n < 0:
            return self._map(lambda x: x.replace(pat, repl))
        return self._map(lambda x: x.replace(pat, repl, n))

    def count(self, pat, flags=0):
        if _isre(pat):
            re = _re()
            rx = re.compile(pat, flags)
            return self._map(lambda x: len(rx.findall(x)))
        return self._map(lambda x: x.count(pat))

    def find(self, sub, start=0, end=None):
        return self._map(lambda x: x.find(sub, start, len(x) if end is None else end))

    def rfind(self, sub, start=0, end=None):
        return self._map(lambda x: x.rfind(sub, start, len(x) if end is None else end))

    def index(self, sub, start=0, end=None):
        return self._map(lambda x: x.index(sub, start, len(x) if end is None else end))

    def repeat(self, repeats):
        return self._map(lambda x: x * repeats)

    def get(self, i):
        res = []
        for x in self.s._v:
            if isinstance(x, (str, list, tuple)) and -len(x) <= i < len(x):
                res.append(x[i])
            else:
                res.append(nan)
        if self.s._dt == 'object':
            return self.s._new(res, 'object')
        return self.s._new(res)

    def slice(self, start=None, stop=None, step=None):
        return self._map(lambda x: x[start:stop:step])

    def slice_replace(self, start=None, stop=None, repl=None):
        return self._map(lambda x: x[:start] + (repl or '') + (x[stop:] if stop is not None else ''))

    def zfill(self, width):
        return self._map(lambda x: x.zfill(width))

    def pad(self, width, side='left', fillchar=' '):
        if side == 'left':
            return self._map(lambda x: x.rjust(width, fillchar))
        if side == 'right':
            return self._map(lambda x: x.ljust(width, fillchar))
        return self._map(lambda x: x.center(width, fillchar))

    def center(self, width, fillchar=' '):
        return self._map(lambda x: x.center(width, fillchar))

    def ljust(self, width, fillchar=' '):
        return self._map(lambda x: x.ljust(width, fillchar))

    def rjust(self, width, fillchar=' '):
        return self._map(lambda x: x.rjust(width, fillchar))

    def removeprefix(self, p):
        return self._map(lambda x: x[len(p):] if x.startswith(p) else x)

    def removesuffix(self, p):
        return self._map(lambda x: x[:len(x) - len(p)] if (p and x.endswith(p)) else x)

    def isdigit(self):
        return self._mapb(lambda x: x.isdigit())

    def isnumeric(self):
        return self._mapb(lambda x: x.isdigit())

    def isdecimal(self):
        return self._mapb(lambda x: x.isdigit())

    def isalpha(self):
        return self._mapb(lambda x: x.isalpha())

    def isalnum(self):
        return self._mapb(lambda x: x.isalnum())

    def isspace(self):
        return self._mapb(lambda x: x.isspace())

    def islower(self):
        return self._mapb(lambda x: x.islower())

    def isupper(self):
        return self._mapb(lambda x: x.isupper())

    def istitle(self):
        return self._mapb(lambda x: x.istitle())

    def split(self, pat=None, n=-1, expand=False, regex=None):
        if pat is not None and len(pat) > 1 and (regex or regex is None) and _isre(pat):
            re = _re()
            rx = re.compile(pat)
            parts = [rx.split(x, 0 if n < 0 else n) if isinstance(x, str) else nan for x in self.s._v]
        else:
            parts = [x.split(pat, n) if isinstance(x, str) else nan for x in self.s._v]
        return self._split_out(parts, expand)

    def rsplit(self, pat=None, n=-1, expand=False):
        parts = [x.rsplit(pat, n) if isinstance(x, str) else nan for x in self.s._v]
        return self._split_out(parts, expand)

    def _split_out(self, parts, expand):
        if not expand:
            return Series(parts, self.s._idx, name=self.s.name, dtype='object')
        mx = 0
        for p in parts:
            if isinstance(p, list) and len(p) > mx:
                mx = len(p)
        cols = {}
        for k in range(mx):
            cols[k] = [p[k] if isinstance(p, list) and k < len(p) else nan for p in parts]
        return DataFrame(cols, index=self.s._idx)

    def partition(self, sep=' ', expand=True):
        cols = {0: [], 1: [], 2: []}
        for x in self.s._v:
            if isinstance(x, str):
                a, b, c = x.partition(sep)
            else:
                a = b = c = nan
            cols[0].append(a)
            cols[1].append(b)
            cols[2].append(c)
        return DataFrame(cols, index=self.s._idx)

    def cat(self, others=None, sep=None, na_rep=None, join='left'):
        sp = sep if sep is not None else ''
        if others is None:
            vals = [x for x in self.s._v if isinstance(x, str)] if na_rep is None else [x if isinstance(x, str) else na_rep for x in self.s._v]
            return sp.join(vals)
        if isinstance(others, (Series, list)):
            ol = [others] if isinstance(others, Series) else others
            if isinstance(others, list) and others and not isinstance(others[0], (Series, list)):
                ol = [others]
            res = []
            for i in range(len(self.s._v)):
                parts = [self.s._v[i]] + [(o._v[i] if isinstance(o, Series) else o[i]) for o in ol]
                if na_rep is None and any([not isinstance(p, str) for p in parts]):
                    res.append(nan)
                else:
                    res.append(sp.join([p if isinstance(p, str) else na_rep for p in parts]))
            return self.s._new(res)
        return self._map(lambda x: x + sp + others)

    def join(self, sep):
        res = []
        for x in self.s._v:
            res.append(sep.join(x) if isinstance(x, (list, tuple)) else nan)
        return self.s._new(res)

    def findall(self, pat, flags=0):
        re = _re()
        rx = re.compile(pat, flags)
        res = [rx.findall(x) if isinstance(x, str) else nan for x in self.s._v]
        return Series(res, self.s._idx, name=self.s.name, dtype='object')

    def extract(self, pat, flags=0, expand=True):
        re = _re()
        rx = re.compile(pat, flags)
        gi = rx.groupindex
        ng = rx.groups
        names = list(range(ng))
        for k, v in gi.items():
            names[v - 1] = k
        cols = [[] for _ in range(ng)]
        for x in self.s._v:
            m = rx.search(x) if isinstance(x, str) else None
            for g in range(ng):
                cols[g].append(m.group(g + 1) if m is not None and m.group(g + 1) is not None else nan)
        if ng == 1 and not expand:
            return Series(cols[0], self.s._idx, name=self.s.name)
        return DataFrame({names[g]: cols[g] for g in range(ng)}, index=self.s._idx)

    def get_dummies(self, sep='|'):
        toks = [x.split(sep) if isinstance(x, str) else [] for x in self.s._v]
        cats = sorted(set([t for r in toks for t in r]))
        return DataFrame({c: [1 if c in r else 0 for r in toks] for c in cats}, index=self.s._idx)

    def encode(self, enc='utf-8'):
        return self._map(lambda x: x)

    def decode(self, enc='utf-8'):
        return self._map(lambda x: x)

    def normalize(self, form):
        return self._map(lambda x: x)

    def wrap(self, width):
        import textwrap
        return self._map(lambda x: '\n'.join(textwrap.wrap(x, width)))

    def __getitem__(self, key):
        if isinstance(key, slice):
            return self.slice(key.start, key.stop, key.step)
        return self.get(key)


class DatetimeMethods:
    def __init__(self, s):
        self.s = s

    def _f(self, name):
        res = []
        for x in self.s._v:
            if x != x:
                res.append(nan)
            else:
                v = getattr(x, name)
                res.append(v() if callable(v) else v)
        return self.s._new(res)

    @property
    def year(self):
        return self._f('year')

    @property
    def month(self):
        return self._f('month')

    @property
    def day(self):
        return self._f('day')

    @property
    def hour(self):
        return self._f('hour')

    @property
    def minute(self):
        return self._f('minute')

    @property
    def second(self):
        return self._f('second')

    @property
    def dayofweek(self):
        return self._f('weekday')

    weekday = dayofweek

    @property
    def dayofyear(self):
        res = []
        import datetime
        for x in self.s._v:
            res.append((x - datetime.datetime(x.year, 1, 1)).days + 1 if x == x else nan)
        return self.s._new(res)

    @property
    def date(self):
        return self._f('date')

    def strftime(self, fmt):
        return self.s._new([x.strftime(fmt) if x == x else nan for x in self.s._v])

    def day_name(self):
        names = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']
        return self.s._new([names[x.weekday()] if x == x else nan for x in self.s._v])

    def month_name(self):
        names = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December']
        return self.s._new([names[x.month - 1] if x == x else nan for x in self.s._v])


def _split_top(expr, word):
    """split a query string on ' and ' / ' or ' outside quotes / brackets"""
    parts = []
    depth = 0
    q = None
    cur = ''
    i = 0
    n = len(expr)
    w = ' ' + word + ' '
    while i < n:
        ch = expr[i]
        if q is not None:
            cur += ch
            if ch == q:
                q = None
        elif ch == '"' or ch == "'":
            q = ch
            cur += ch
        elif ch in '([{':
            depth += 1
            cur += ch
        elif ch in ')]}':
            depth -= 1
            cur += ch
        elif depth == 0 and expr[i:i + len(w)] == w:
            parts.append(cur)
            cur = ''
            i += len(w) - 1
        else:
            cur += ch
        i += 1
    parts.append(cur)
    return parts


def _qtransform(expr):
    ors = _split_top(expr, 'or')
    res = []
    for o in ors:
        ands = _split_top(o, 'and')
        if len(ands) > 1:
            res.append(' & '.join(['(' + _qtransform(a.strip()) + ')' for a in ands]))
        else:
            e = o.strip()
            if e.startswith('not '):
                e = '~(' + e[4:] + ')'
            res.append(e)
    if len(res) > 1:
        return ' | '.join(['(' + r + ')' for r in res])
    return res[0]


def eval_query(df, expr, local_dict):
    e = _qtransform(expr.strip())
    ns = {}
    for j, c in enumerate(df._cols._v):
        if isinstance(c, str):
            ns[c] = df._col(j)
    ns['index'] = df._idx
    if local_dict:
        for k, v in local_dict.items():
            ns['@' + k] = v
    # replace @name with name looked up in caller supplied dict
    out = ''
    i = 0
    while i < len(e):
        if e[i] == '@':
            j = i + 1
            while j < len(e) and (e[j].isalnum() or e[j] == '_'):
                j += 1
            nm = e[i + 1:j]
            ns['__at_' + nm] = (local_dict or {}).get(nm)
            out += '__at_' + nm
            i = j
        else:
            out += e[i]
            i += 1
    return eval(out, {}, ns)
