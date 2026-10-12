# Pure Python pathlib (POSIX flavour).  Directory operations need os.listdir / os.mkdir / os.stat etc.; when the
# interpreter's os module lacks them they raise OSError (NotImplementedError-like message).
import os as _os
import posixpath as _pp
import fnmatch as _fnmatch
import _fileio

__all__ = ["PurePath", "PurePosixPath", "PureWindowsPath", "Path", "PosixPath", "WindowsPath"]


def _need(name):
    f = getattr(_os, name, None)
    if f is None:
        raise OSError("os.%s is not available in this interpreter" % name)
    return f


class PurePath:
    def __new__(cls, *args, **kwargs):
        if cls is PurePath:
            cls = PurePosixPath
        elif cls is Path:
            cls = PosixPath
        return object.__new__(cls)

    def __init__(self, *args):
        paths = []
        for a in args:
            if isinstance(a, PurePath):
                paths.append(a._str)
            else:
                if not isinstance(a, str):
                    if hasattr(a, '__fspath__'):
                        a = a.__fspath__()
                    if not isinstance(a, str):
                        raise TypeError("argument should be a str or an os.PathLike object where __fspath__ returns a str, not %r" % type(a).__name__)
                paths.append(a)
        self._init_from(paths)

    def _init_from(self, paths):
        p = ''
        for a in paths:
            if a.startswith('/'):
                p = a
            elif p == '' or p.endswith('/'):
                p = p + a
            else:
                p = p + '/' + a
        drive, root, rest = _pp.splitroot(p)
        parts = [x for x in rest.split('/') if x != '' and x != '.']
        self._drive = ''
        self._root = root
        self._tail = parts
        s = root + '/'.join(parts)
        if s == '':
            s = '.'
        self._str = s

    def with_segments(self, *pathsegments):
        return type(self)(*pathsegments)

    def _new(self, root, tail):
        o = object.__new__(type(self))
        o._drive = ''
        o._root = root
        o._tail = tail
        s = root + '/'.join(tail)
        o._str = s if s != '' else '.'
        return o

    def __str__(self):
        return self._str

    def __fspath__(self):
        return self._str

    def as_posix(self):
        return self._str

    def __repr__(self):
        return "%s(%r)" % (type(self).__name__, self._str)

    def __bytes__(self):
        return self._str.encode()

    def as_uri(self):
        if not self.is_absolute():
            raise ValueError("relative path can't be expressed as a file URI")
        from urllib.parse import quote_from_bytes
        return 'file://' + quote_from_bytes(self._str.encode())

    def __eq__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return self._str == other._str

    def __ne__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return self._str != other._str

    def __hash__(self):
        return hash(self._str)

    def _key(self):
        return (self._root, tuple(self._tail))

    def __lt__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return self._key() < other._key()

    def __le__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return self._key() <= other._key()

    def __gt__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return self._key() > other._key()

    def __ge__(self, other):
        if not isinstance(other, PurePath):
            return NotImplemented
        return self._key() >= other._key()

    @property
    def drive(self):
        return ''

    @property
    def root(self):
        return self._root

    @property
    def anchor(self):
        return self._root

    @property
    def name(self):
        if not self._tail:
            return ''
        return self._tail[-1]

    @property
    def suffix(self):
        name = self.name
        i = name.rfind('.')
        if 0 < i < len(name) - 1:
            return name[i:]
        return ''

    @property
    def suffixes(self):
        name = self.name
        if name.endswith('.'):
            return []
        name = name.lstrip('.')
        return ['.' + s for s in name.split('.')[1:]]

    @property
    def stem(self):
        name = self.name
        i = name.rfind('.')
        if 0 < i < len(name) - 1:
            return name[:i]
        return name

    @property
    def parts(self):
        if self._root:
            return tuple([self._root] + self._tail)
        return tuple(self._tail)

    @property
    def parent(self):
        if not self._tail:
            return self
        return self._new(self._root, self._tail[:-1])

    @property
    def parents(self):
        return _PathParents(self)

    def is_absolute(self):
        return self._root != ''

    def is_reserved(self):
        return False

    def joinpath(self, *args):
        return type(self)(self._str, *args)

    def __truediv__(self, key):
        try:
            return type(self)(self._str, key)
        except TypeError:
            return NotImplemented

    def __rtruediv__(self, key):
        try:
            return type(self)(key, self._str)
        except TypeError:
            return NotImplemented

    def with_name(self, name):
        if not self.name:
            raise ValueError("%r has an empty name" % (self,))
        if not name or '/' in name or name == '.':
            raise ValueError("Invalid name %r" % (name))
        return self._new(self._root, self._tail[:-1] + [name])

    def with_stem(self, stem):
        return self.with_name(stem + self.suffix)

    def with_suffix(self, suffix):
        if '/' in suffix:
            raise ValueError("Invalid suffix %r" % (suffix,))
        if suffix and not suffix.startswith('.') or suffix == '.':
            raise ValueError("Invalid suffix %r" % (suffix))
        name = self.name
        if not name:
            raise ValueError("%r has an empty name" % (self,))
        old = self.suffix
        if not old:
            name = name + suffix
        else:
            name = name[:-len(old)] + suffix
        return self._new(self._root, self._tail[:-1] + [name])

    def relative_to(self, other, *more, walk_up=False):
        other = type(self)(other, *more)
        if self._root != other._root:
            raise ValueError("%r and %r have different anchors" % (str(self), str(other)))
        n = len(other._tail)
        if self._tail[:n] == other._tail:
            return self._new('', self._tail[n:])
        if not walk_up:
            raise ValueError("%r is not in the subpath of %r" % (str(self), str(other)))
        i = 0
        while i < n and i < len(self._tail) and self._tail[i] == other._tail[i]:
            i += 1
        ups = n - i
        for x in other._tail[i:]:
            if x == '..':
                raise ValueError("'..' segment in %r cannot be walked" % str(other))
        return self._new('', ['..'] * ups + self._tail[i:])

    def is_relative_to(self, other, *more):
        other = type(self)(other, *more)
        if self._root != other._root:
            return False
        return self._tail[:len(other._tail)] == other._tail

    def match(self, path_pattern, *, case_sensitive=None):
        pat = type(self)(path_pattern)
        if not pat._tail and not pat._root:
            raise ValueError("empty pattern")
        pparts = list(pat.parts)
        sparts = list(self.parts)
        if pat._root:
            if len(pparts) != len(sparts):
                return False
        elif len(pparts) > len(sparts):
            return False
        sparts = sparts[len(sparts) - len(pparts):]
        for i in range(len(pparts)):
            if not _fnmatch.fnmatchcase(sparts[i], pparts[i]):
                return False
        return True

    def full_match(self, pattern):
        pat = type(self)(pattern)
        return _glob_match(list(self.parts), list(pat.parts))

    def __reduce__(self):
        return (type(self), (self._str,))

    def __copy__(self):
        return self

    def __deepcopy__(self, memo):
        return self


def _glob_match(parts, pats):
    if not pats:
        return not parts
    if pats[0] == '**':
        if _glob_match(parts, pats[1:]):
            return True
        return bool(parts) and _glob_match(parts[1:], pats)
    if not parts:
        return False
    if not _fnmatch.fnmatchcase(parts[0], pats[0]):
        return False
    return _glob_match(parts[1:], pats[1:])


class _PathParents:
    def __init__(self, path):
        self._path = path
        self._n = len(path._tail)
        if not path._root and path._tail:
            pass

    def __len__(self):
        return self._n

    def __getitem__(self, idx):
        if isinstance(idx, slice):
            return tuple([self[i] for i in range(*idx.indices(len(self)))])
        if idx >= self._n or idx < -self._n:
            raise IndexError(idx)
        if idx < 0:
            idx += self._n
        p = self._path
        return p._new(p._root, p._tail[:self._n - idx - 1])

    def __iter__(self):
        for i in range(self._n):
            yield self[i]

    def __repr__(self):
        return "<%s.parents>" % type(self._path).__name__


class PurePosixPath(PurePath):
    pass


class PureWindowsPath(PurePath):
    pass


class Path(PurePath):
    @classmethod
    def cwd(cls):
        return cls(_os.getcwd())

    @classmethod
    def home(cls):
        return cls(_pp.expanduser('~'))

    def absolute(self):
        if self.is_absolute():
            return self
        return type(self)(_os.getcwd(), self._str)

    def resolve(self, strict=False):
        return type(self)(_pp.normpath(self.absolute()._str))

    def expanduser(self):
        if self._tail and self._tail[0][:1] == '~':
            return type(self)(_pp.expanduser(self._str))
        return self

    def exists(self, *, follow_symlinks=True):
        return _pp.exists(self._str)

    def is_dir(self):
        return _pp.isdir(self._str)

    def is_file(self):
        return _pp.isfile(self._str)

    def is_symlink(self):
        return False

    def is_mount(self):
        return self._str == '/'

    def is_socket(self):
        return False

    def is_fifo(self):
        return False

    def is_block_device(self):
        return False

    def is_char_device(self):
        return False

    def samefile(self, other_path):
        return self.absolute().resolve()._str == type(self)(other_path).absolute().resolve()._str

    def stat(self, *, follow_symlinks=True):
        return _need('stat')(self._str)

    def lstat(self):
        return self.stat()

    def owner(self):
        raise NotImplementedError("Path.owner() is unsupported")

    def group(self):
        raise NotImplementedError("Path.group() is unsupported")

    def open(self, mode='r', buffering=-1, encoding=None, errors=None, newline=None):
        return open(self._str, mode)

    def read_text(self, encoding=None, errors=None):
        f = open(self._str, 'r')
        try:
            return f.read()
        finally:
            f.close()

    def read_bytes(self):
        return _fileio.read_bytes(self._str)

    def write_text(self, data, encoding=None, errors=None, newline=None):
        if not isinstance(data, str):
            raise TypeError('data must be str, not %s' % type(data).__name__)
        f = open(self._str, 'w')
        try:
            return f.write(data)
        finally:
            f.close()

    def write_bytes(self, data):
        return _fileio.write_bytes(self._str, data)

    def touch(self, mode=0o666, exist_ok=True):
        if _pp.exists(self._str):
            if not exist_ok:
                raise FileExistsError("[Errno 17] File exists: %r" % self._str)
            return
        f = open(self._str, 'w')
        f.close()

    def mkdir(self, mode=0o777, parents=False, exist_ok=False):
        if _pp.isdir(self._str):
            if exist_ok:
                return
            raise FileExistsError("[Errno 17] File exists: %r" % self._str)
        if parents and self._tail:
            par = self.parent
            if par._str != self._str and not par.exists():
                par.mkdir(mode, True, True)
        _need('mkdir')(self._str)

    def rmdir(self):
        _need('rmdir')(self._str)

    def unlink(self, missing_ok=False):
        if not _pp.exists(self._str):
            if missing_ok:
                return
            raise FileNotFoundError("[Errno 2] No such file or directory: %r" % self._str)
        _os.remove(self._str)

    def rename(self, target):
        _os.rename(self._str, str(target))
        return type(self)(target)

    def replace(self, target):
        _os.rename(self._str, str(target))
        return type(self)(target)

    def chmod(self, mode, *, follow_symlinks=True):
        f = getattr(_os, 'chmod', None)
        if f is not None:
            f(self._str, mode)

    def iterdir(self):
        if not _pp.isdir(self._str):
            raise NotADirectoryError("[Errno 20] Not a directory: %r" % self._str)
        names = _need('listdir')(self._str)
        for name in names:
            yield self._new(self._root, self._tail + [name]) if self._str != '.' else self._new('', [name])

    def glob(self, pattern, *, case_sensitive=None, recurse_symlinks=False):
        pat = type(self)(pattern)
        if pat.is_absolute():
            raise NotImplementedError("Non-relative patterns are unsupported")
        pats = list(pat.parts)
        if not pats:
            raise ValueError("Unacceptable pattern: %r" % (pattern,))
        trailing_dir = pattern.endswith('/')
        res = self._glob(self, pats)
        seen = []
        for p in res:
            if p in seen:
                continue
            seen.append(p)
            if trailing_dir and not p.is_dir():
                continue
            yield p

    def _glob(self, base, pats):
        if not pats:
            return [base]
        first = pats[0]
        rest = pats[1:]
        out = []
        if first == '**':
            out.extend(self._glob(base, rest))
            if base.is_dir():
                for child in sorted(base.iterdir(), key=lambda c: c.name):
                    if child.is_dir():
                        out.extend(self._glob(child, pats))
            return out
        if first == '..':
            return self._glob(base.parent if base._str != '.' else type(self)('..'), rest)
        if not base.is_dir():
            return out
        if not ('*' in first or '?' in first or '[' in first):
            child = base / first
            if child.exists() and (rest == [] or child.is_dir()):
                out.extend(self._glob(child, rest))
            return out
        for child in base.iterdir():
            n = child.name
            if _fnmatch.fnmatchcase(n, first) and (first[0] == '.' or n[0] != '.' or True):
                if rest and not child.is_dir():
                    continue
                out.extend(self._glob(child, rest))
        return out

    def rglob(self, pattern, *, case_sensitive=None, recurse_symlinks=False):
        return self.glob('**/' + pattern)

    def walk(self, top_down=True, on_error=None, follow_symlinks=False):
        dirs = []
        files = []
        for c in self.iterdir():
            if c.is_dir():
                dirs.append(c.name)
            else:
                files.append(c.name)
        if top_down:
            yield self, dirs, files
        for d in list(dirs):
            for x in (self / d).walk(top_down):
                yield x
        if not top_down:
            yield self, dirs, files

    def symlink_to(self, target, target_is_directory=False):
        raise NotImplementedError("symlinks unsupported")

    def hardlink_to(self, target):
        raise NotImplementedError("hardlinks unsupported")

    def readlink(self):
        raise NotImplementedError("readlink unsupported")

    def __enter__(self):
        return self

    def __exit__(self, t, v, tb):
        return False


class PosixPath(Path, PurePosixPath):
    pass


class WindowsPath(Path, PureWindowsPath):
    def __new__(cls, *args, **kwargs):
        raise NotImplementedError("cannot instantiate 'WindowsPath' on your system")
