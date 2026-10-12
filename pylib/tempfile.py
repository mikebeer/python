# Pure Python tempfile (directory creation needs os.mkdir; files are created with open())
import os as _os
import random as _random
import posixpath as _pp

TMP_MAX = 10000
template = "tmp"
tempdir = None
_chars = "abcdefghijklmnopqrstuvwxyz0123456789_"


def gettempdir():
    global tempdir
    if tempdir is None:
        for e in ('TMPDIR', 'TEMP', 'TMP'):
            d = _os.getenv(e)
            if d:
                tempdir = d
                break
        else:
            tempdir = '/tmp'
    return tempdir


def gettempprefix():
    return template


def _name(prefix, suffix):
    return prefix + ''.join([_chars[int(_random.random() * len(_chars))] for _ in range(8)]) + suffix


def _candidate(suffix, prefix, dir):
    if dir is None:
        dir = gettempdir()
    if prefix is None:
        prefix = template
    if suffix is None:
        suffix = ''
    return dir, prefix, suffix


def mktemp(suffix="", prefix=template, dir=None):
    dir, prefix, suffix = _candidate(suffix, prefix, dir)
    for _ in range(TMP_MAX):
        file = _pp.join(dir, _name(prefix, suffix))
        if not _pp.exists(file):
            return file
    raise FileExistsError("No usable temporary filename found")


class _TemporaryFileWrapper:
    def __init__(self, file, name, delete=True):
        self.file = file
        self.name = name
        self.delete = delete
        self._closed = False

    def __getattr__(self, name):
        return getattr(self.file, name)

    def write(self, data):
        return self.file.write(data)

    def read(self, *a):
        return self.file.read(*a)

    def readline(self, *a):
        return self.file.readline(*a)

    def readlines(self, *a):
        return self.file.readlines(*a)

    def seek(self, *a):
        return self.file.seek(*a)

    def tell(self):
        return self.file.tell()

    def flush(self):
        return self.file.flush()

    def __iter__(self):
        return iter(self.file)

    def close(self):
        if not self._closed:
            self._closed = True
            self.file.close()
            if self.delete and _pp.exists(self.name):
                _os.remove(self.name)

    def __enter__(self):
        return self

    def __exit__(self, exc, value, tb):
        self.close()
        return False


def NamedTemporaryFile(mode='w+b', buffering=-1, encoding=None, newline=None, suffix=None, prefix=None, dir=None,
                       delete=True, *, errors=None, delete_on_close=True):
    dir, prefix, suffix = _candidate(suffix, prefix, dir)
    name = mktemp(suffix, prefix, dir)
    f = open(name, 'w')
    f.close()
    wmode = mode.replace('+', '')
    if 'b' in mode and 'r' not in mode:
        wmode = 'w' if False else wmode
    return _TemporaryFileWrapper(open(name, 'w' if 'w' in mode else mode.replace('b', '').replace('+', '') or 'w'), name, delete)


def TemporaryFile(mode='w+b', buffering=-1, encoding=None, newline=None, suffix=None, prefix=None, dir=None, *, errors=None):
    return NamedTemporaryFile(mode, suffix=suffix, prefix=prefix, dir=dir)


def mkstemp(suffix=None, prefix=None, dir=None, text=False):
    dir, prefix, suffix = _candidate(suffix, prefix, dir)
    name = mktemp(suffix, prefix, dir)
    f = open(name, 'w')
    f.close()
    return (3, _pp.abspath(name))


def mkdtemp(suffix=None, prefix=None, dir=None):
    dir, prefix, suffix = _candidate(suffix, prefix, dir)
    name = mktemp(suffix, prefix, dir)
    f = getattr(_os, 'mkdir', None)
    if f is None:
        raise OSError("os.mkdir is not available in this interpreter")
    f(name)
    return _pp.abspath(name)


class TemporaryDirectory:
    def __init__(self, suffix=None, prefix=None, dir=None, ignore_cleanup_errors=False, *, delete=True):
        self.name = mkdtemp(suffix, prefix, dir)
        self._delete = delete

    def __repr__(self):
        return "<{} {!r}>".format(self.__class__.__name__, self.name)

    def __enter__(self):
        return self.name

    def __exit__(self, exc, value, tb):
        if self._delete:
            self.cleanup()
        return False

    def cleanup(self):
        import shutil
        if _pp.exists(self.name):
            shutil.rmtree(self.name)


class SpooledTemporaryFile:
    def __init__(self, max_size=0, mode='w+b', **kw):
        import io
        self._f = io.BytesIO() if 'b' in mode else io.StringIO()

    def __getattr__(self, n):
        return getattr(self._f, n)

    def __enter__(self):
        return self

    def __exit__(self, a, b, c):
        self._f.close()
        return False
