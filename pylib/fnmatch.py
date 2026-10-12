# Pure Python fnmatch
import re as _re

__all__ = ["filter", "fnmatch", "fnmatchcase", "translate"]
_cache = {}


def fnmatch(name, pat):
    return fnmatchcase(name, pat)


def _compile(pat):
    r = _cache.get(pat)
    if r is None:
        r = _re.compile(translate(pat).replace('(?>', '(?:')).match
        _cache[pat] = r
    return r


def filter(names, pat):
    match = _compile(pat)
    return [n for n in names if match(n)]


def fnmatchcase(name, pat):
    return _compile(pat)(name) is not None


def translate(pat):
    STAR = object()
    res = []
    add = res.append
    i, n = 0, len(pat)
    while i < n:
        c = pat[i]
        i = i + 1
        if c == '*':
            if not res or res[-1] is not STAR:
                add(STAR)
        elif c == '?':
            add('.')
        elif c == '[':
            j = i
            if j < n and pat[j] == '!':
                j = j + 1
            if j < n and pat[j] == ']':
                j = j + 1
            while j < n and pat[j] != ']':
                j = j + 1
            if j >= n:
                add('\\[')
            else:
                stuff = pat[i:j]
                if '-' not in stuff:
                    stuff = stuff.replace('\\', r'\\')
                else:
                    chunks = []
                    k = i + 2 if pat[i] == '!' else i + 1
                    while True:
                        k = pat.find('-', k, j)
                        if k < 0:
                            break
                        chunks.append(pat[i:k])
                        i = k + 1
                        k = k + 3
                    chunk = pat[i:j]
                    if chunk:
                        chunks.append(chunk)
                    else:
                        chunks[-1] += '-'
                    for k in range(len(chunks) - 1, 0, -1):
                        if chunks[k - 1][-1] > chunks[k][0]:
                            chunks[k - 1] = chunks[k - 1][:-1] + chunks[k][1:]
                            del chunks[k]
                    stuff = '-'.join(s.replace('\\', r'\\').replace('-', r'\-') for s in chunks)
                stuff = _re.sub(r'([&~|])', r'\\\1', stuff)
                i = j + 1
                if not stuff:
                    add('(?!)')
                elif stuff == '!':
                    add('.')
                else:
                    if stuff[0] == '!':
                        stuff = '^' + stuff[1:]
                    elif stuff[0] in ('^', '['):
                        stuff = '\\' + stuff
                    add('[' + stuff + ']')
        else:
            add(_re.escape(c))
    inp = res
    res = []
    add = res.append
    i, n = 0, len(inp)
    while i < n and inp[i] is not STAR:
        add(inp[i])
        i += 1
    while i < n:
        i += 1
        if i == n:
            add('.*')
            break
        fixed = []
        while i < n and inp[i] is not STAR:
            fixed.append(inp[i])
            i += 1
        fixed = ''.join(fixed)
        if i == n:
            add('.*')
            add(fixed)
        else:
            add('(?>.*?' + fixed + ')')
    res = ''.join(res)
    return '(?s:' + res + ')\\Z'
