# Pure Python glob (needs os.listdir for wildcard patterns)
import os as _os
import posixpath as _pp
import re as _re
import fnmatch as _fnmatch

__all__ = ["glob", "iglob", "escape", "glob0", "glob1", "has_magic"]
magic_check = _re.compile('([*?[])')


def has_magic(s):
    return magic_check.search(s) is not None


def _listdir(d):
    f = getattr(_os, 'listdir', None)
    if f is None:
        raise OSError("os.listdir is not available in this interpreter")
    return f(d or '.')


def glob(pathname, *, root_dir=None, dir_fd=None, recursive=False, include_hidden=False):
    return list(iglob(pathname, root_dir=root_dir, recursive=recursive, include_hidden=include_hidden))


def iglob(pathname, *, root_dir=None, dir_fd=None, recursive=False, include_hidden=False):
    pathname = str(pathname)
    if root_dir is not None:
        root_dir = str(root_dir)
    for p in _iglob(pathname, root_dir, recursive, include_hidden, False):
        yield p


def _join(root, rel):
    if root is None:
        return rel
    if not rel:
        return root
    return _pp.join(root, rel)


def _iglob(pathname, root_dir, recursive, include_hidden, dironly):
    dirname, basename = _pp.split(pathname)
    if not has_magic(pathname):
        if basename:
            if _pp.exists(_join(root_dir, pathname)):
                yield pathname
        else:
            if _pp.isdir(_join(root_dir, dirname)):
                yield pathname
        return
    if not dirname:
        if recursive and basename == '**':
            for x in _glob2(root_dir, basename, dironly, include_hidden):
                yield x
        else:
            for x in _glob1(root_dir, basename, dironly, include_hidden):
                yield x
        return
    if dirname != pathname and has_magic(dirname):
        dirs = _iglob(dirname, root_dir, recursive, include_hidden, True)
    else:
        dirs = [dirname]
    if has_magic(basename):
        if recursive and basename == '**':
            glob_in_dir = _glob2
        else:
            glob_in_dir = _glob1
    else:
        glob_in_dir = _glob0
    for dirname in dirs:
        for name in glob_in_dir(_join(root_dir, dirname), basename, dironly, include_hidden):
            yield _pp.join(dirname, name)


def _glob1(dirname, pattern, dironly, include_hidden):
    names = _listdir(dirname)
    if dironly:
        names = [x for x in names if _pp.isdir(_pp.join(dirname or '.', x))]
    if not include_hidden and not _is_hidden(pattern):
        names = [x for x in names if not _is_hidden(x)]
    return _fnmatch.filter(sorted(names) if False else names, pattern)


def _glob0(dirname, basename, dironly, include_hidden):
    if basename:
        if _pp.exists(_pp.join(dirname, basename)):
            return [basename]
    else:
        if _pp.isdir(dirname):
            return [basename]
    return []


def glob0(dirname, pattern):
    return _glob0(dirname, pattern, False, True)


def glob1(dirname, pattern):
    return _glob1(dirname, pattern, False, True)


def _glob2(dirname, pattern, dironly, include_hidden):
    if dirname is None or _pp.isdir(dirname or '.'):
        yield pattern[:0]
    for x in _rlistdir(dirname, dironly, include_hidden):
        yield x


def _rlistdir(dirname, dironly, include_hidden):
    names = _listdir(dirname)
    for x in names:
        if not include_hidden and _is_hidden(x):
            continue
        path = _pp.join(dirname, x) if dirname else x
        if not dironly or _pp.isdir(path):
            yield x
        if _pp.isdir(path):
            for y in _rlistdir(path, dironly, include_hidden):
                yield _pp.join(x, y)


def _is_hidden(path):
    return path[0] == '.'


def escape(pathname):
    drive, pathname = _pp.splitdrive(pathname)
    pathname = magic_check.sub(r'[\1]', pathname)
    return drive + pathname
