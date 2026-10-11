"""Class-body introspection helpers (annotation names/order) for dataclasses, typing.NamedTuple, ..."""
import sys as _sys


def _source_lines(cls):
    mod = _sys.modules.get(cls.__module__)
    path = getattr(mod, "__file__", None)
    if not path and cls.__module__ == "__main__" and len(_sys.argv) > 0 and _sys.argv[0] != "-c":
        path = _sys.argv[0]
    if not path:
        return None
    try:
        f = open(path)
        text = f.read()
        f.close()
    except Exception:
        return None
    return text.split("\n")


def _indent_of(line):
    return len(line) - len(line.lstrip(" \t"))


def _split_ann(text):
    k = text.find(":")
    if k <= 0:
        return None
    name = text[:k].strip()
    if not name.isidentifier():
        return None
    rest = text[k + 1:]
    depth = 0
    cut = len(rest)
    for i in range(len(rest)):
        ch = rest[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "=" and depth == 0:
            cut = i
            break
    ann = rest[:cut].strip()
    if not ann:
        return None
    return name, ann


def annotations(cls):
    """[(name, annotation)] of the class body in definition order (annotation as text if scanned)."""
    ann = getattr(cls, "__annotations__", None)
    if ann is not None:
        return list(ann.items())
    lines = _source_lines(cls)
    if lines is None:
        return []
    key = "class " + cls.__name__
    start = -1
    for i in range(len(lines)):
        s = lines[i].lstrip(" \t")
        if s.startswith(key) and len(s) > len(key) and s[len(key)] in "(: ":
            start = i
            break
    if start < 0:
        return []
    ind = _indent_of(lines[start])
    out = []
    body = -1
    i = start + 1
    while i < len(lines):
        ln = lines[i]
        i += 1
        s = ln.strip()
        if not s or s[0] == "#":
            continue
        li = _indent_of(ln)
        if li <= ind:
            break
        if body < 0:
            body = li
        if li != body:
            continue
        r = _split_ann(s)
        if r is not None:
            out.append(r)
    return out
