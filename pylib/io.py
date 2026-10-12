"""io: a small pure-Python StringIO (text only) -- runs on any interpreter of this project."""


class StringIO:
    def __init__(self, initial_value="", newline="\n"):
        self._buf = initial_value
        self._pos = 0
        self.closed = False

    def _check(self):
        if self.closed:
            raise ValueError("I/O operation on closed file.")

    def write(self, s):
        self._check()
        if not isinstance(s, str):
            raise TypeError("string argument expected, got '%s'" % type(s).__name__)
        n = len(s)
        if self._pos == len(self._buf):
            self._buf = self._buf + s
        else:
            if self._pos > len(self._buf):
                self._buf = self._buf + "\0" * (self._pos - len(self._buf))
            self._buf = self._buf[:self._pos] + s + self._buf[self._pos + n:]
        self._pos += n
        return n

    def writelines(self, lines):
        for line in lines:
            self.write(line)

    def getvalue(self):
        self._check()
        return self._buf

    def read(self, size=-1):
        self._check()
        if size is None or size < 0:
            r = self._buf[self._pos:]
        else:
            r = self._buf[self._pos:self._pos + size]
        self._pos += len(r)
        return r

    def readline(self, size=-1):
        self._check()
        if self._pos >= len(self._buf):
            return ""
        i = self._buf.find("\n", self._pos)
        end = len(self._buf) if i < 0 else i + 1
        if size is not None and size >= 0 and self._pos + size < end:
            end = self._pos + size
        r = self._buf[self._pos:end]
        self._pos = end
        return r

    def readlines(self, hint=-1):
        out = []
        while True:
            line = self.readline()
            if line == "":
                break
            out.append(line)
        return out

    def __iter__(self):
        return self

    def __next__(self):
        line = self.readline()
        if line == "":
            raise StopIteration
        return line

    def seek(self, pos, whence=0):
        self._check()
        if whence == 0:
            self._pos = pos
        elif whence == 1:
            self._pos = self._pos + pos
        else:
            self._pos = len(self._buf) + pos
        return self._pos

    def tell(self):
        self._check()
        return self._pos

    def truncate(self, size=None):
        self._check()
        if size is None:
            size = self._pos
        self._buf = self._buf[:size]
        return size

    def close(self):
        self.closed = True

    def readable(self):
        return True

    def writable(self):
        return True

    def seekable(self):
        return True

    def isatty(self):
        return False

    def flush(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False


SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2
DEFAULT_BUFFER_SIZE = 8192


class UnsupportedOperation(OSError, ValueError):
    pass


class BytesIO:
    def __init__(self, initial_bytes=b""):
        self._buf = bytearray(initial_bytes)
        self._pos = 0
        self.closed = False

    def _check(self):
        if self.closed:
            raise ValueError("I/O operation on closed file.")

    def write(self, b):
        self._check()
        if isinstance(b, str):
            raise TypeError("a bytes-like object is required, not 'str'")
        b = bytes(b)
        n = len(b)
        if self._pos == len(self._buf):
            self._buf.extend(b)
        else:
            if self._pos > len(self._buf):
                self._buf.extend(b"\x00" * (self._pos - len(self._buf)))
            self._buf[self._pos:self._pos + n] = b
        self._pos += n
        return n

    def writelines(self, lines):
        for line in lines:
            self.write(line)

    def getvalue(self):
        self._check()
        return bytes(self._buf)

    def getbuffer(self):
        return self._buf

    def read(self, size=-1):
        self._check()
        if size is None or size < 0:
            r = bytes(self._buf[self._pos:])
        else:
            r = bytes(self._buf[self._pos:self._pos + size])
        self._pos += len(r)
        return r

    read1 = read

    def readinto(self, b):
        d = self.read(len(b))
        b[:len(d)] = d
        return len(d)

    def readline(self, size=-1):
        self._check()
        if self._pos >= len(self._buf):
            return b""
        i = self._buf.find(b"\n", self._pos)
        end = len(self._buf) if i < 0 else i + 1
        if size is not None and size >= 0 and self._pos + size < end:
            end = self._pos + size
        r = bytes(self._buf[self._pos:end])
        self._pos = end
        return r

    def readlines(self, hint=-1):
        out = []
        while True:
            line = self.readline()
            if line == b"":
                break
            out.append(line)
        return out

    def __iter__(self):
        return self

    def __next__(self):
        line = self.readline()
        if line == b"":
            raise StopIteration
        return line

    def seek(self, pos, whence=0):
        self._check()
        if whence == 0:
            if pos < 0:
                raise ValueError("negative seek value %d" % pos)
            self._pos = pos
        elif whence == 1:
            self._pos = max(0, self._pos + pos)
        else:
            self._pos = max(0, len(self._buf) + pos)
        return self._pos

    def tell(self):
        self._check()
        return self._pos

    def truncate(self, size=None):
        self._check()
        if size is None:
            size = self._pos
        del self._buf[size:]
        return size

    def close(self):
        self.closed = True

    def readable(self):
        return True

    def writable(self):
        return True

    def seekable(self):
        return True

    def isatty(self):
        return False

    def flush(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False
