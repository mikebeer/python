# Pure Python gzip (compress/decompress/GzipFile over io.BytesIO or a file path)
import zlib as _zlib
import io as _io
import _fileio

FTEXT, FHCRC, FEXTRA, FNAME, FCOMMENT = 1, 2, 4, 8, 16
READ, WRITE = 1, 2


class BadGzipFile(OSError):
    pass


def compress(data, compresslevel=9, *, mtime=None):
    data = bytes(data)
    raw = _zlib.compress(data, compresslevel, -15)
    mt = 0 if mtime is None else int(mtime)
    return (b'\x1f\x8b\x08\x00' + mt.to_bytes(4, 'little') + b'\x02\xff' + raw + _zlib.crc32(data).to_bytes(4, 'little') +
            (len(data) & 0xFFFFFFFF).to_bytes(4, 'little'))


def decompress(data):
    data = bytes(data)
    out = b''
    while data:
        if data[:2] != b'\x1f\x8b':
            raise BadGzipFile("Not a gzipped file (%r)" % data[:2])
        try:
            o, used = _zlib._decompress_any(data, 31)
        except _zlib.error as e:
            raise BadGzipFile(str(e))
        out += o
        data = data[used:]
    return out


class GzipFile:
    def __init__(self, filename=None, mode=None, compresslevel=9, fileobj=None, mtime=None):
        self.name = filename if filename is not None else ''
        self.mode = mode or 'rb'
        self._fileobj = fileobj
        self._level = compresslevel
        self._mtime = mtime
        self.closed = False
        self._own = fileobj is None
        if 'r' in self.mode:
            if fileobj is None:
                raw = _fileio.read_bytes(filename)
            else:
                raw = fileobj.read()
            self._buf = _io.BytesIO(decompress(raw))
        else:
            self._buf = _io.BytesIO()

    def write(self, data):
        return self._buf.write(data)

    def read(self, size=-1):
        return self._buf.read(size)

    def readline(self, size=-1):
        return self._buf.readline(size)

    def readlines(self):
        return self._buf.readlines()

    def __iter__(self):
        return iter(self._buf)

    def flush(self):
        pass

    def seek(self, *a):
        return self._buf.seek(*a)

    def tell(self):
        return self._buf.tell()

    def readable(self):
        return 'r' in self.mode

    def writable(self):
        return 'r' not in self.mode

    def close(self):
        if self.closed:
            return
        self.closed = True
        if 'r' not in self.mode:
            data = compress(self._buf.getvalue(), self._level, mtime=self._mtime)
            if self._fileobj is not None:
                self._fileobj.write(data)
            else:
                _fileio.write_bytes(self.name, data)

    def __enter__(self):
        return self

    def __exit__(self, a, b, c):
        self.close()
        return False


def open(filename, mode="rb", compresslevel=9, encoding=None, errors=None, newline=None):
    gz = GzipFile(filename if isinstance(filename, str) else None, mode.replace('t', ''), compresslevel,
                  None if isinstance(filename, str) else filename)
    if 't' in mode:
        return _TextWrap(gz, mode)
    return gz


class _TextWrap:
    def __init__(self, gz, mode):
        self._gz = gz
        self._mode = mode
        self._text = _io.StringIO(gz.read().decode('utf-8')) if 'r' in mode else _io.StringIO()

    def read(self, *a):
        return self._text.read(*a)

    def readline(self):
        return self._text.readline()

    def readlines(self):
        return self._text.readlines()

    def __iter__(self):
        return iter(self._text)

    def write(self, s):
        return self._text.write(s)

    def close(self):
        if 'r' not in self._mode:
            self._gz.write(self._text.getvalue().encode('utf-8'))
        self._gz.close()

    def __enter__(self):
        return self

    def __exit__(self, a, b, c):
        self.close()
        return False
