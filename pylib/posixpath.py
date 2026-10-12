# Pure Python posixpath (port); file system queries go through the interpreter's native os / os.path
import os as _os
import os.path as _np

curdir = '.'
pardir = '..'
extsep = '.'
sep = '/'
pathsep = ':'
defpath = '/bin:/usr/bin'
altsep = None
devnull = '/dev/null'
supports_unicode_filenames = False


def _fspath(p):
    if isinstance(p, (str, bytes)):
        return p
    if hasattr(p, '__fspath__'):
        return p.__fspath__()
    raise TypeError("expected str, bytes or os.PathLike object, not " + type(p).__name__)


def _get_sep(path):
    if isinstance(path, bytes):
        return b'/'
    return '/'


def normcase(s):
    return _fspath(s)


def isabs(s):
    s = _fspath(s)
    sep = _get_sep(s)
    return s.startswith(sep)


def join(a, *p):
    a = _fspath(a)
    sep = _get_sep(a)
    path = a
    for b in p:
        b = _fspath(b)
        if b.startswith(sep):
            path = b
        elif not path or path.endswith(sep):
            path += b
        else:
            path += sep + b
    return path


def split(p):
    p = _fspath(p)
    sep = _get_sep(p)
    i = p.rfind(sep) + 1
    head, tail = p[:i], p[i:]
    if head and head != sep * len(head):
        head = head.rstrip(sep)
    return head, tail


def splitext(p):
    p = _fspath(p)
    if isinstance(p, bytes):
        sep = b'/'
        extsep = b'.'
    else:
        sep = '/'
        extsep = '.'
    sepIndex = p.rfind(sep)
    dotIndex = p.rfind(extsep)
    if dotIndex > sepIndex:
        filenameIndex = sepIndex + 1
        while filenameIndex < dotIndex:
            if p[filenameIndex:filenameIndex + 1] != extsep:
                return p[:dotIndex], p[dotIndex:]
            filenameIndex += 1
    return p, p[:0]


def splitdrive(p):
    p = _fspath(p)
    return p[:0], p


def splitroot(p):
    p = _fspath(p)
    if isinstance(p, bytes):
        sep = b'/'
        empty = b''
    else:
        sep = '/'
        empty = ''
    if not p or p[:1] != sep:
        return empty, empty, p
    elif p[1:2] != sep or p[2:3] == sep:
        return empty, sep, p[1:]
    else:
        return empty, p[:2], p[2:]


def basename(p):
    p = _fspath(p)
    sep = _get_sep(p)
    i = p.rfind(sep) + 1
    return p[i:]


def dirname(p):
    p = _fspath(p)
    sep = _get_sep(p)
    i = p.rfind(sep) + 1
    head = p[:i]
    if head and head != sep * len(head):
        head = head.rstrip(sep)
    return head


def commonprefix(m):
    if not m:
        return ''
    if not isinstance(m[0], (list, tuple)):
        m = tuple(map(_fspath, m))
    s1 = min(m)
    s2 = max(m)
    for i, c in enumerate(s1):
        if c != s2[i]:
            return s1[:i]
    return s1


def expanduser(path):
    path = _fspath(path)
    if isinstance(path, bytes):
        tilde = b'~'
    else:
        tilde = '~'
    if not path.startswith(tilde):
        return path
    sep = _get_sep(path)
    i = path.find(sep, 1)
    if i < 0:
        i = len(path)
    if i == 1:
        userhome = _os.getenv('HOME')
        if userhome is None:
            return path
    else:
        return path
    if isinstance(path, bytes):
        userhome = userhome.encode()
    userhome = userhome.rstrip(sep)
    return (userhome + path[i:]) or sep


def expandvars(path):
    path = _fspath(path)
    if '$' not in path:
        return path
    out = []
    i = 0
    n = len(path)
    idc = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_'
    while i < n:
        c = path[i]
        if c == '$' and i + 1 < n:
            if path[i + 1] == '{':
                j = path.find('}', i + 2)
                if j > 0:
                    name = path[i + 2:j]
                    v = _os.getenv(name)
                    out.append(v if v is not None else path[i:j + 1])
                    i = j + 1
                    continue
            else:
                j = i + 1
                while j < n and path[j] in idc:
                    j += 1
                if j > i + 1:
                    name = path[i + 1:j]
                    v = _os.getenv(name)
                    out.append(v if v is not None else path[i:j])
                    i = j
                    continue
        out.append(c)
        i += 1
    return ''.join(out)


def normpath(path):
    path = _fspath(path)
    if isinstance(path, bytes):
        sep = b'/'
        empty = b''
        dot = b'.'
        dotdot = b'..'
    else:
        sep = '/'
        empty = ''
        dot = '.'
        dotdot = '..'
    if path == empty:
        return dot
    _, initial_slashes, path = splitroot(path)
    comps = path.split(sep)
    new_comps = []
    for comp in comps:
        if comp in (empty, dot):
            continue
        if (comp != dotdot or (not initial_slashes and not new_comps) or (new_comps and new_comps[-1] == dotdot)):
            new_comps.append(comp)
        elif new_comps:
            new_comps.pop()
    comps = new_comps
    path = initial_slashes + sep.join(comps)
    return path or dot


def abspath(path):
    path = _fspath(path)
    if not isabs(path):
        if isinstance(path, bytes):
            cwd = _os.getcwd().encode()
        else:
            cwd = _os.getcwd()
        path = join(cwd, path)
    return normpath(path)


def realpath(filename, *, strict=False):
    return abspath(filename)


def relpath(path, start=None):
    if not path:
        raise ValueError("no path specified")
    path = _fspath(path)
    if isinstance(path, bytes):
        curdir = b'.'
        sep = b'/'
        pardir = b'..'
    else:
        curdir = '.'
        sep = '/'
        pardir = '..'
    if start is None:
        start = curdir
    else:
        start = _fspath(start)
    start_list = [x for x in abspath(start).split(sep) if x]
    path_list = [x for x in abspath(path).split(sep) if x]
    i = len(commonprefix([start_list, path_list]))
    rel_list = [pardir] * (len(start_list) - i) + path_list[i:]
    if not rel_list:
        return curdir
    return join(*rel_list)


def commonpath(paths):
    if not paths:
        raise ValueError('commonpath() arg is an empty sequence')
    paths = tuple(map(_fspath, paths))
    if isinstance(paths[0], bytes):
        sep = b'/'
        curdir = b'.'
    else:
        sep = '/'
        curdir = '.'
    split_paths = [path.split(sep) for path in paths]
    isabs_ = set([p[:1] == sep for p in paths])
    if len(isabs_) > 1:
        raise ValueError("Can't mix absolute and relative paths")
    split_paths = [[c for c in s if c and c != curdir] for s in split_paths]
    s1 = min(split_paths)
    s2 = max(split_paths)
    common = s1
    for i, c in enumerate(s1):
        if c != s2[i]:
            common = s1[:i]
            break
    prefix = sep if paths[0][:1] == sep else paths[0][:0]
    return prefix + sep.join(common)


def exists(path):
    return _np.exists(_fspath(path))


def lexists(path):
    return _np.exists(_fspath(path))


def isdir(s):
    return _np.isdir(_fspath(s))


def isfile(path):
    return _np.isfile(_fspath(path))


def islink(path):
    return False


def ismount(path):
    return abspath(path) == '/'


def getsize(filename):
    filename = _fspath(filename)
    if hasattr(_np, 'getsize'):
        return _np.getsize(filename)
    f = open(filename, 'rb')
    try:
        return len(f.read())
    finally:
        f.close()


def samefile(f1, f2):
    return abspath(f1) == abspath(f2)


def isjunction(path):
    return False
