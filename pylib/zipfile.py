# Pure Python zipfile: stored and deflated members (read/write/append-less), in memory or via _fileio
import io as _io
import zlib as _zlib
import _fileio
import posixpath as _pp

ZIP_STORED = 0
ZIP_DEFLATED = 8
ZIP_BZIP2 = 12
ZIP_LZMA = 14
ZIP64_LIMIT = (1 << 31) - 1
_SIG_LOCAL = b'PK\x03\x04'
_SIG_CEN = b'PK\x01\x02'
_SIG_END = b'PK\x05\x06'


class BadZipFile(Exception):
    pass


BadZipfile = BadZipFile


class LargeZipFile(Exception):
    pass


def _le(n, v):
    return (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')


def _dosdate(t):
    return ((t[0] - 1980) << 9) | (t[1] << 5) | t[2]


def _dostime(t):
    return (t[3] << 11) | (t[4] << 5) | (t[5] // 2)


class ZipInfo:
    def __init__(self, filename="NoName", date_time=(1980, 1, 1, 0, 0, 0)):
        self.orig_filename = filename
        null_byte = filename.find(chr(0))
        if null_byte >= 0:
            filename = filename[0:null_byte]
        self.filename = filename
        self.date_time = date_time
        self.compress_type = ZIP_STORED
        self.comment = b""
        self.extra = b""
        self.create_system = 3
        self.create_version = 20
        self.extract_version = 20
        self.reserved = 0
        self.flag_bits = 0
        self.volume = 0
        self.internal_attr = 0
        self.external_attr = 0
        self.header_offset = 0
        self.CRC = 0
        self.compress_size = 0
        self.file_size = 0

    def is_dir(self):
        return self.filename[-1:] == '/'

    def __repr__(self):
        r = '<%s filename=%r' % (type(self).__name__, self.filename)
        if self.compress_type != ZIP_STORED:
            r += ' compress_type=deflate'
        hs = self.file_size
        r += ' file_size=%r' % hs
        if self.compress_type != ZIP_STORED:
            r += ' compress_size=%r' % self.compress_size
        return r + '>'


class ZipFile:
    def __init__(self, file, mode="r", compression=ZIP_STORED, allowZip64=True, compresslevel=None, *,
                 strict_timestamps=True):
        if mode not in ('r', 'w', 'x', 'a'):
            raise ValueError("ZipFile requires mode 'r', 'w', 'x', or 'a'")
        self.mode = mode
        self.compression = compression
        self.compresslevel = compresslevel
        self.filelist = []
        self.NameToInfo = {}
        self.comment = b''
        self._data = {}
        self.debug = 0
        self.closed = False
        self._path = None
        self._fp = None
        if isinstance(file, str) or hasattr(file, '__fspath__'):
            self._path = str(file)
            self.filename = self._path
        else:
            self._fp = file
            self.filename = getattr(file, 'name', None)
        if mode == 'r' or mode == 'a':
            if self._path is not None:
                raw = _fileio.read_bytes(self._path) if (_pp.exists(self._path) or mode == 'r') else b''
            else:
                self._fp.seek(0)
                raw = self._fp.read()
            if raw or mode == 'r':
                self._read_central(raw)
        self._dirty = mode != 'r'
        if mode == 'w' or mode == 'x':
            self._touch = True

    def _read_central(self, raw):
        i = raw.rfind(_SIG_END)
        if i < 0:
            raise BadZipFile("File is not a zip file")
        total = int.from_bytes(raw[i + 10:i + 12], 'little')
        cdsize = int.from_bytes(raw[i + 12:i + 16], 'little')
        cdoff = int.from_bytes(raw[i + 16:i + 20], 'little')
        clen = int.from_bytes(raw[i + 20:i + 22], 'little')
        self.comment = raw[i + 22:i + 22 + clen]
        p = cdoff
        for _ in range(total):
            if raw[p:p + 4] != _SIG_CEN:
                raise BadZipFile("Bad magic number for central directory")
            zi = ZipInfo()
            (vmade, vneed, flags, method, mtime, mdate, crc, csize, usize, nlen, elen, cmlen, dsk, iattr, eattr,
             hoff) = (int.from_bytes(raw[p + 4:p + 6], 'little'), int.from_bytes(raw[p + 6:p + 8], 'little'),
                      int.from_bytes(raw[p + 8:p + 10], 'little'), int.from_bytes(raw[p + 10:p + 12], 'little'),
                      int.from_bytes(raw[p + 12:p + 14], 'little'), int.from_bytes(raw[p + 14:p + 16], 'little'),
                      int.from_bytes(raw[p + 16:p + 20], 'little'), int.from_bytes(raw[p + 20:p + 24], 'little'),
                      int.from_bytes(raw[p + 24:p + 28], 'little'), int.from_bytes(raw[p + 28:p + 30], 'little'),
                      int.from_bytes(raw[p + 30:p + 32], 'little'), int.from_bytes(raw[p + 32:p + 34], 'little'),
                      int.from_bytes(raw[p + 34:p + 36], 'little'), int.from_bytes(raw[p + 36:p + 38], 'little'),
                      int.from_bytes(raw[p + 38:p + 42], 'little'), int.from_bytes(raw[p + 42:p + 46], 'little'))
            name = raw[p + 46:p + 46 + nlen].decode('utf-8')
            zi.orig_filename = name
            zi.filename = name
            zi.extra = raw[p + 46 + nlen:p + 46 + nlen + elen]
            zi.comment = raw[p + 46 + nlen + elen:p + 46 + nlen + elen + cmlen]
            zi.date_time = (((mdate >> 9) & 127) + 1980, (mdate >> 5) & 15, mdate & 31, (mtime >> 11) & 31,
                            (mtime >> 5) & 63, (mtime & 31) * 2)
            zi.compress_type = method
            zi.create_system = vmade >> 8
            zi.create_version = vmade & 255
            zi.extract_version = vneed & 255
            zi.reserved = 0
            zi.flag_bits = flags
            zi.volume = dsk
            zi.internal_attr = iattr
            zi.external_attr = eattr
            zi.header_offset = hoff
            zi.CRC = crc
            zi.compress_size = csize
            zi.file_size = usize
            lnlen = int.from_bytes(raw[hoff + 26:hoff + 28], 'little')
            lelen = int.from_bytes(raw[hoff + 28:hoff + 30], 'little')
            start = hoff + 30 + lnlen + lelen
            self._data[name] = raw[start:start + csize]
            self.filelist.append(zi)
            self.NameToInfo[name] = zi
            p += 46 + nlen + elen + cmlen

    def namelist(self):
        return [data.filename for data in self.filelist]

    def infolist(self):
        return self.filelist

    def getinfo(self, name):
        info = self.NameToInfo.get(name)
        if info is None:
            raise KeyError('There is no item named %r in the archive' % name)
        return info

    def printdir(self, file=None):
        import sys
        out = file or sys.stdout
        out.write("%-46s %19s %12s\n" % ("File Name", "Modified    ", "Size"))
        for zinfo in self.filelist:
            date = "%d-%02d-%02d %02d:%02d:%02d" % zinfo.date_time[:6]
            out.write("%-46s %s %12d\n" % (zinfo.filename, date, zinfo.file_size))

    def read(self, name, pwd=None):
        zi = name if isinstance(name, ZipInfo) else self.getinfo(name)
        data = self._data[zi.filename]
        if zi.compress_type == ZIP_STORED:
            out = data
        elif zi.compress_type == ZIP_DEFLATED:
            out = _zlib.decompress(data, -15)
        else:
            raise NotImplementedError("compression type %d is not supported" % zi.compress_type)
        if _zlib.crc32(out) != zi.CRC:
            raise BadZipFile("Bad CRC-32 for file %r" % zi.orig_filename)
        return out

    def open(self, name, mode="r", pwd=None, *, force_zip64=False):
        if mode == 'r':
            return _io.BytesIO(self.read(name))
        if mode == 'w':
            zf = self
            fname = name.filename if isinstance(name, ZipInfo) else name

            class _W(_io.BytesIO):
                def close(s):
                    if not s.closed:
                        zf.writestr(name, s.getvalue())
                    _io.BytesIO.close(s)

                def __exit__(s, a, b, c):
                    s.close()
                    return False
            return _W()
        raise ValueError('open() requires mode "r" or "w"')

    def testzip(self):
        for zi in self.filelist:
            try:
                self.read(zi.filename)
            except BadZipFile:
                return zi.filename
        return None

    def setpassword(self, pwd):
        pass

    def writestr(self, zinfo_or_arcname, data, compress_type=None, compresslevel=None):
        if isinstance(data, str):
            data = data.encode('utf-8')
        data = bytes(data)
        if self.mode == 'r':
            raise ValueError('write() requires mode "w", "x", or "a"')
        if not isinstance(zinfo_or_arcname, ZipInfo):
            zinfo = ZipInfo(filename=zinfo_or_arcname, date_time=(1980, 1, 1, 0, 0, 0))
            zinfo.compress_type = self.compression
            zinfo.external_attr = 0o600 << 16
            if zinfo.filename[-1:] == '/':
                zinfo.external_attr = 0o40775 << 16
                zinfo.external_attr |= 0x10
        else:
            zinfo = zinfo_or_arcname
        if compress_type is not None:
            zinfo.compress_type = compress_type
        zinfo.file_size = len(data)
        zinfo.CRC = _zlib.crc32(data)
        if zinfo.compress_type == ZIP_DEFLATED and data:
            cdata = _zlib.compress(data, 6 if compresslevel is None else compresslevel, -15)
        else:
            cdata = data
            if not data:
                zinfo.compress_type = ZIP_STORED if zinfo.compress_type != ZIP_DEFLATED else ZIP_DEFLATED
                if zinfo.compress_type == ZIP_DEFLATED:
                    cdata = _zlib.compress(b'', 6, -15)
        zinfo.compress_size = len(cdata)
        if zinfo.filename in self.NameToInfo:
            import warnings
            self.filelist = [z for z in self.filelist if z.filename != zinfo.filename]
        self._data[zinfo.filename] = cdata
        self.filelist.append(zinfo)
        self.NameToInfo[zinfo.filename] = zinfo

    def write(self, filename, arcname=None, compress_type=None, compresslevel=None):
        filename = str(filename)
        if arcname is None:
            arcname = filename
        arcname = _pp.normpath(arcname)
        while arcname[:1] == '/':
            arcname = arcname[1:]
        if _pp.isdir(filename):
            self.writestr(ZipInfo(arcname + '/'), b'')
            return
        self.writestr(arcname, _fileio.read_bytes(filename), compress_type, compresslevel)

    def mkdir(self, zinfo_or_directory_name, mode=511):
        n = zinfo_or_directory_name
        if not isinstance(n, ZipInfo):
            if n[-1:] != '/':
                n += '/'
            self.writestr(n, b'')
        else:
            self.writestr(n, b'')

    def extractall(self, path=None, members=None, pwd=None):
        if members is None:
            members = self.namelist()
        for m in members:
            self.extract(m, path)

    def extract(self, member, path=None, pwd=None):
        name = member.filename if isinstance(member, ZipInfo) else member
        if path is None:
            path = '.'
        target = _pp.join(str(path), name)
        if name[-1:] == '/':
            import os
            if not _pp.isdir(target):
                os.mkdir(target)
            return target
        d = _pp.dirname(target)
        if d and not _pp.isdir(d):
            import os
            os.makedirs(d, exist_ok=True)
        _fileio.write_bytes(target, self.read(name))
        return target

    def _build(self):
        out = bytearray()
        cen = bytearray()
        for zi in self.filelist:
            name = zi.filename.encode('utf-8')
            zi.header_offset = len(out)
            flag = zi.flag_bits | (0x800 if len(name) != len(zi.filename) else 0)
            data = self._data[zi.filename]
            out += _SIG_LOCAL + _le(2, 20) + _le(2, flag) + _le(2, zi.compress_type) + _le(2, _dostime(zi.date_time)) + \
                _le(2, _dosdate(zi.date_time)) + _le(4, zi.CRC) + _le(4, zi.compress_size) + _le(4, zi.file_size) + \
                _le(2, len(name)) + _le(2, len(zi.extra)) + name + zi.extra + data
            cen += _SIG_CEN + _le(1, zi.create_version) + _le(1, zi.create_system) + _le(2, 20) + _le(2, flag) + \
                _le(2, zi.compress_type) + _le(2, _dostime(zi.date_time)) + _le(2, _dosdate(zi.date_time)) + \
                _le(4, zi.CRC) + _le(4, zi.compress_size) + _le(4, zi.file_size) + _le(2, len(name)) + \
                _le(2, len(zi.extra)) + _le(2, len(zi.comment)) + _le(2, 0) + _le(2, zi.internal_attr) + \
                _le(4, zi.external_attr) + _le(4, zi.header_offset) + name + zi.extra + zi.comment
        cdoff = len(out)
        out += cen
        out += _SIG_END + _le(2, 0) + _le(2, 0) + _le(2, len(self.filelist)) + _le(2, len(self.filelist)) + \
            _le(4, len(cen)) + _le(4, cdoff) + _le(2, len(self.comment)) + self.comment
        return bytes(out)

    def close(self):
        if self.closed:
            return
        self.closed = True
        if self.mode != 'r':
            blob = self._build()
            if self._path is not None:
                _fileio.write_bytes(self._path, blob)
            else:
                self._fp.seek(0)
                self._fp.write(blob)

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self.close()
        return False


def is_zipfile(filename):
    try:
        if hasattr(filename, 'read'):
            filename.seek(0)
            raw = filename.read()
        else:
            raw = _fileio.read_bytes(str(filename))
        return raw.rfind(_SIG_END) >= 0
    except OSError:
        return False
