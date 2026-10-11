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
