# Names of os.path that the native os.path module lacks (isabs, getsize, relpath, ...): posixpath's.
from posixpath import *
import posixpath as _pp


def getsize(filename):
    f = open(_pp._fspath(filename), 'rb')
    try:
        return len(f.read())
    finally:
        f.close()
