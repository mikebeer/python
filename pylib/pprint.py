"""pprint: pformat / pprint following CPython's layout rules for builtin containers."""
import sys as _sys


def _safe_key_sort(items):
    try:
        return sorted(items, key=lambda kv: kv[0])
    except TypeError:
        return sorted(items, key=lambda kv: (type(kv[0]).__name__, repr(kv[0])))


class PrettyPrinter:
    def __init__(self, indent=1, width=80, depth=None, stream=None, compact=False, sort_dicts=True,
                 underscore_numbers=False):
        self._indent = indent
        self._width = width
        self._depth = depth
        self._stream = stream
        self._compact = compact
        self._sort = sort_dicts

    def pformat(self, obj):
        out = []
        self._format(obj, out, 0, 0, 0)
        return "".join(out)

    def pprint(self, obj):
        s = self.pformat(obj) + "\n"
        if self._stream is not None:
            self._stream.write(s)
        else:
            _sys.stdout.write(s)

    def isreadable(self, obj):
        return True

    def isrecursive(self, obj):
        return False

    def _repr(self, obj, level):
        return self._r(obj, level)

    def _r(self, o, level):
        t = type(o)
        if t is dict:
            if not o:
                return "{}"
            if self._depth is not None and level >= self._depth:
                return "{...}"
            items = list(o.items())
            if self._sort:
                items = _safe_key_sort(items)
            return "{" + ", ".join([self._r(k, level + 1) + ": " + self._r(v, level + 1) for k, v in items]) + "}"
        if t is list:
            if not o:
                return "[]"
            if self._depth is not None and level >= self._depth:
                return "[...]"
            return "[" + ", ".join([self._r(v, level + 1) for v in o]) + "]"
        if t is tuple:
            if not o:
                return "()"
            if self._depth is not None and level >= self._depth:
                return "(...)"
            if len(o) == 1:
                return "(" + self._r(o[0], level + 1) + ",)"
            return "(" + ", ".join([self._r(v, level + 1) for v in o]) + ")"
        if t is set or t is frozenset:
            if not o:
                return "set()" if t is set else "frozenset()"
            items = self._sorted_set(o)
            body = "{" + ", ".join([self._r(v, level + 1) for v in items]) + "}"
            return body if t is set else "frozenset(" + body + ")"
        return repr(o)

    def _sorted_set(self, o):
        try:
            return sorted(o)
        except TypeError:
            return sorted(o, key=lambda v: (type(v).__name__, repr(v)))

    def _format(self, o, out, indent, allowance, level):
        rep = self._r(o, level)
        t = type(o)
        if len(rep) <= self._width - indent - allowance:
            out.append(rep)
            return
        ind = self._indent
        if t is dict and o:
            out.append("{")
            if ind > 1:
                out.append(" " * (ind - 1))
            items = list(o.items())
            if self._sort:
                items = _safe_key_sort(items)
            self._items_dict(items, out, indent + ind, allowance + 1, level + 1)
            out.append("}")
        elif t is list and o:
            out.append("[")
            if ind > 1:
                out.append(" " * (ind - 1))
            self._items(o, out, indent + ind, allowance + 1, level + 1)
            out.append("]")
        elif t is tuple and o:
            out.append("(")
            if ind > 1:
                out.append(" " * (ind - 1))
            self._items(o, out, indent + ind, allowance + (1 if len(o) > 1 else 2), level + 1)
            if len(o) == 1:
                out.append(",")
            out.append(")")
        elif (t is set or t is frozenset) and o:
            items = self._sorted_set(o)
            if t is frozenset:
                out.append("frozenset({")
                indent += 11
            else:
                out.append("{")
            if ind > 1:
                out.append(" " * (ind - 1))
            self._items(items, out, indent + ind, allowance + (2 if t is frozenset else 1), level + 1)
            out.append("})" if t is frozenset else "}")
        elif t is str and len(o) > 0:
            self._fmt_str(o, out, indent, allowance, level)
        else:
            out.append(rep)

    def _fmt_str(self, o, out, indent, allowance, level):
        # split into lines (keeping line ends), then into words, as CPython does
        top = level == 0
        if top:
            indent += 1
            allowance += 1
        lines = o.splitlines(True)
        if len(lines) > 1:
            parts = lines
        else:
            parts = [o]
        chunks = []
        max_width1 = max_width = self._width - indent
        for i in range(len(parts)):
            rep = repr(parts[i])
            if i == len(parts) - 1:
                max_width1 -= allowance
            if len(rep) <= max_width1:
                chunks.append(rep)
            else:
                parts2 = _split_words(parts[i])
                cur = ""
                for j in range(len(parts2)):
                    candidate = cur + parts2[j]
                    if j == len(parts2) - 1 and i == len(parts) - 1:
                        max_width -= allowance
                    if len(repr(candidate)) > max_width:
                        if cur:
                            chunks.append(repr(cur))
                        cur = parts2[j]
                    else:
                        cur = candidate
                if cur:
                    chunks.append(repr(cur))
        if len(chunks) == 1:
            out.append(chunks[0])
            return
        if top:
            out.append("(")
        for i in range(len(chunks)):
            if i > 0:
                out.append("\n" + " " * indent)
            out.append(chunks[i])
        if top:
            out.append(")")

    def _items(self, items, out, indent, allowance, level):
        items = list(items)
        n = len(items)
        sp = "\n" + " " * indent
        for i in range(n):
            last = i == n - 1
            if i > 0:
                out.append("," + sp)
            self._format(items[i], out, indent, allowance if last else 1, level)

    def _items_dict(self, items, out, indent, allowance, level):
        n = len(items)
        sp = "\n" + " " * indent
        for i in range(n):
            k, v = items[i]
            last = i == n - 1
            if i > 0:
                out.append("," + sp)
            kr = self._r(k, level)
            out.append(kr)
            out.append(": ")
            self._format(v, out, indent + len(kr) + 2, allowance if last else 1, level)


def _split_words(s):
    # re.findall(r'\S*\s*', s) without the regex module
    parts = []
    i = 0
    n = len(s)
    while i < n:
        j = i
        while j < n and not s[j].isspace():
            j += 1
        while j < n and s[j].isspace():
            j += 1
        parts.append(s[i:j])
        i = j
    return parts


def pformat(obj, indent=1, width=80, depth=None, *, compact=False, sort_dicts=True, underscore_numbers=False):
    return PrettyPrinter(indent, width, depth, None, compact, sort_dicts).pformat(obj)


def pprint(obj, stream=None, indent=1, width=80, depth=None, *, compact=False, sort_dicts=True,
           underscore_numbers=False):
    PrettyPrinter(indent, width, depth, stream, compact, sort_dicts).pprint(obj)


def pp(obj, *args, sort_dicts=False, **kw):
    pprint(obj, *args, sort_dicts=sort_dicts, **kw)


def saferepr(obj):
    return repr(obj)


def isreadable(obj):
    return True


def isrecursive(obj):
    return False
