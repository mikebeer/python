# Pure Python shutil (best effort: copy/move/rmtree/copytree need os.listdir/os.mkdir/os.rmdir for directories)
import os as _os
import posixpath as _pp
import _fileio

__all__ = ["copyfileobj", "copyfile", "copy", "copy2", "copymode", "copystat", "copytree", "move", "rmtree",
           "which", "get_terminal_size", "disk_usage", "Error", "SameFileError", "ignore_patterns", "make_archive"]


class Error(OSError):
    pass


class SameFileError(Error):
    pass


def copyfileobj(fsrc, fdst, length=0):
    while True:
        buf = fsrc.read(65536)
        if not buf:
            break
        fdst.write(buf)


def copyfile(src, dst, *, follow_symlinks=True):
    src = str(src)
    dst = str(dst)
    if _pp.abspath(src) == _pp.abspath(dst):
        raise SameFileError("%r and %r are the same file" % (src, dst))
    if not _pp.exists(src):
        raise FileNotFoundError("[Errno 2] No such file or directory: %r" % src)
    _fileio.write_bytes(dst, _fileio.read_bytes(src))
    return dst


def copymode(src, dst, *, follow_symlinks=True):
    pass


def copystat(src, dst, *, follow_symlinks=True):
    pass


def copy(src, dst, *, follow_symlinks=True):
    src = str(src)
    dst = str(dst)
    if _pp.isdir(dst):
        dst = _pp.join(dst, _pp.basename(src))
    copyfile(src, dst)
    return dst


copy2 = copy


def ignore_patterns(*patterns):
    import fnmatch

    def _ignore_patterns(path, names):
        ignored = set()
        for pattern in patterns:
            ignored.update(fnmatch.filter(names, pattern))
        return ignored
    return _ignore_patterns


def _lsd(d):
    f = getattr(_os, 'listdir', None)
    if f is None:
        raise OSError("os.listdir is not available in this interpreter")
    return f(d)


def copytree(src, dst, symlinks=False, ignore=None, copy_function=copy2, ignore_dangling_symlinks=False,
             dirs_exist_ok=False):
    src = str(src)
    dst = str(dst)
    names = _lsd(src)
    ignored = ignore(src, names) if ignore is not None else set()
    if _pp.exists(dst):
        if not dirs_exist_ok:
            raise FileExistsError("[Errno 17] File exists: %r" % dst)
    else:
        _os.mkdir(dst)
    for name in names:
        if name in ignored:
            continue
        s = _pp.join(src, name)
        d = _pp.join(dst, name)
        if _pp.isdir(s):
            copytree(s, d, symlinks, ignore, copy_function, dirs_exist_ok=dirs_exist_ok)
        else:
            copy_function(s, d)
    return dst


def rmtree(path, ignore_errors=False, onerror=None, *, onexc=None, dir_fd=None):
    path = str(path)
    try:
        if not _pp.isdir(path):
            if not _pp.exists(path):
                raise FileNotFoundError("[Errno 2] No such file or directory: %r" % path)
            raise NotADirectoryError("[Errno 20] Not a directory: %r" % path)
        for name in _lsd(path):
            p = _pp.join(path, name)
            if _pp.isdir(p):
                rmtree(p)
            else:
                _os.remove(p)
        rd = getattr(_os, 'rmdir', None)
        if rd is None:
            raise OSError("os.rmdir is not available in this interpreter")
        rd(path)
    except OSError as e:
        if ignore_errors:
            return
        if onexc is not None:
            onexc(rmtree, path, e)
        elif onerror is not None:
            onerror(rmtree, path, None)
        else:
            raise


def move(src, dst, copy_function=copy2):
    src = str(src)
    dst = str(dst)
    real_dst = dst
    if _pp.isdir(dst):
        real_dst = _pp.join(dst, _pp.basename(src.rstrip('/')))
        if _pp.exists(real_dst):
            raise Error("Destination path '%s' already exists" % real_dst)
    try:
        _os.rename(src, real_dst)
    except OSError:
        if _pp.isdir(src):
            copytree(src, real_dst, copy_function=copy_function)
            rmtree(src)
        else:
            copy_function(src, real_dst)
            _os.remove(src)
    return real_dst


def which(cmd, mode=1, path=None):
    if _pp.dirname(cmd):
        return cmd if _pp.isfile(cmd) else None
    if path is None:
        path = _os.getenv('PATH') or '/bin:/usr/bin'
    for d in path.split(':'):
        p = _pp.join(d or '.', cmd)
        if _pp.isfile(p):
            return p
    return None


def get_terminal_size(fallback=(80, 24)):
    cols = _os.getenv('COLUMNS')
    lines = _os.getenv('LINES')
    try:
        c = int(cols) if cols else fallback[0]
        l = int(lines) if lines else fallback[1]
    except ValueError:
        c, l = fallback
    from collections import namedtuple
    T = namedtuple('terminal_size', 'columns lines')
    return T(c, l)


def disk_usage(path):
    from collections import namedtuple
    return namedtuple('usage', 'total used free')(0, 0, 0)


def make_archive(*a, **k):
    raise NotImplementedError("make_archive is not supported")
