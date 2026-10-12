# Pure Python additions to the native os module.
import os as _os


class _Environ:
    def __getitem__(self, key):
        v = _os.getenv(key)
        if v is None:
            raise KeyError(key)
        return v

    def get(self, key, default=None):
        v = _os.getenv(key)
        if v is None:
            return default
        return v

    def __contains__(self, key):
        return _os.getenv(key) is not None

    def __setitem__(self, key, value):
        pass

    def keys(self):
        return []

    def items(self):
        return []

    def __iter__(self):
        return iter([])

    def __len__(self):
        return 0


environ = _Environ()
linesep = "\n"
curdir = "."
pardir = ".."
extsep = "."


def listdir(path="."):
    raise OSError("listdir is not supported")


def fspath(p):
    return p


class PathLike:
    def __fspath__(self):
        raise NotImplementedError


def fspath(p):
    if isinstance(p, (str, bytes)):
        return p
    f = getattr(p, '__fspath__', None)
    if f is not None:
        return f()
    raise TypeError("expected str, bytes or os.PathLike object, not " + type(p).__name__)


def fsencode(filename):
    filename = fspath(filename)
    if isinstance(filename, str):
        return filename.encode('utf-8')
    return filename


def fsdecode(filename):
    filename = fspath(filename)
    if isinstance(filename, bytes):
        return filename.decode('utf-8')
    return filename


def fsencode_name():
    return 'utf-8'


def getfilesystemencoding():
    return 'utf-8'


def cpu_count():
    return 1


def getpid():
    return 1


def urandom(n):
    # no operating system entropy source: bytes derived from the clock (the C interpreter has a native one)
    import time as _time
    x = _time.time_ns() ^ (id(object()) << 20) ^ 0x9E3779B97F4A7C15
    out = bytearray()
    while len(out) < n:
        x = (x * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        out.append((x >> 33) & 255)
    return bytes(out)


def strerror(code):
    return {1: 'Operation not permitted', 2: 'No such file or directory', 13: 'Permission denied',
            17: 'File exists', 20: 'Not a directory', 21: 'Is a directory'}.get(code, 'Unknown error %d' % code)


def get_terminal_size(fd=1):
    return (80, 24)


def makedirs(name, mode=0o777, exist_ok=False):
    import posixpath
    head, tail = posixpath.split(name)
    if not tail:
        head, tail = posixpath.split(head)
    if head and tail and not posixpath.exists(head):
        try:
            makedirs(head, exist_ok=exist_ok)
        except FileExistsError:
            pass
    try:
        mkdir(name, mode)
    except OSError:
        if not exist_ok or not posixpath.isdir(name):
            raise


def walk(top, topdown=True, onerror=None, followlinks=False):
    import posixpath
    top = fspath(top)
    try:
        names = listdir(top)
    except OSError as err:
        if onerror is not None:
            onerror(err)
        return
    dirs = []
    nondirs = []
    for name in names:
        if posixpath.isdir(posixpath.join(top, name)):
            dirs.append(name)
        else:
            nondirs.append(name)
    if topdown:
        yield top, dirs, nondirs
    for name in dirs:
        new_path = posixpath.join(top, name)
        for x in walk(new_path, topdown, onerror, followlinks):
            yield x
    if not topdown:
        yield top, dirs, nondirs


def mkdir(path, mode=0o777):
    raise OSError("mkdir is not supported")


def rmdir(path):
    raise OSError("rmdir is not supported")


def removedirs(name):
    raise OSError("removedirs is not supported")


def replace(src, dst):
    return rename(src, dst)


# Prefer the native versions where the interpreter provides them (C interpreter built with -DPY_POSIX).
if hasattr(_os, 'listdir'):
    listdir = _os.listdir
if hasattr(_os, 'mkdir'):
    mkdir = _os.mkdir
if hasattr(_os, 'rmdir'):
    rmdir = _os.rmdir
if hasattr(_os, 'getpid'):
    getpid = _os.getpid
if hasattr(_os, 'urandom'):
    urandom = _os.urandom
