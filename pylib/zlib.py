# Pure Python zlib: crc32, adler32, compress (LZ77 + fixed Huffman), decompress (full inflate), objects
import binascii as _bin

DEFLATED = 8
DEF_MEM_LEVEL = 8
DEF_BUF_SIZE = 16384
MAX_WBITS = 15
ZLIB_VERSION = '1.3.1'
ZLIB_RUNTIME_VERSION = '1.3.1'
Z_NO_COMPRESSION = 0
Z_BEST_SPEED = 1
Z_BEST_COMPRESSION = 9
Z_DEFAULT_COMPRESSION = -1
Z_DEFAULT_STRATEGY = 0
Z_FILTERED = 1
Z_HUFFMAN_ONLY = 2
Z_RLE = 3
Z_FIXED = 4
Z_NO_FLUSH = 0
Z_PARTIAL_FLUSH = 1
Z_SYNC_FLUSH = 2
Z_FULL_FLUSH = 3
Z_FINISH = 4
Z_BLOCK = 5
Z_TREES = 6


class error(Exception):
    pass


def crc32(data, value=0):
    return _bin.crc32(data, value)


def adler32(data, value=1):
    a = value & 0xFFFF
    b = (value >> 16) & 0xFFFF
    data = bytes(data)
    n = len(data)
    i = 0
    while i < n:
        chunk = data[i:i + 5552]
        for c in chunk:
            a += c
            b += a
        a %= 65521
        b %= 65521
        i += 5552
    return (b << 16) | a


_LBASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258]
_LEXT = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0]
_DBASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
          6145, 8193, 12289, 16385, 24577]
_DEXT = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13]
_CLORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]


class _Bits:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos
        self.bb = 0
        self.bn = 0

    def need(self, n):
        while self.bn < n:
            if self.p >= len(self.d):
                raise error("Error -5 while decompressing data: incomplete or truncated stream")
            self.bb |= self.d[self.p] << self.bn
            self.p += 1
            self.bn += 8

    def bits(self, n):
        if n == 0:
            return 0
        self.need(n)
        v = self.bb & ((1 << n) - 1)
        self.bb >>= n
        self.bn -= n
        return v

    def align(self):
        self.bb = 0
        self.bn = 0


def _build(lengths):
    # canonical Huffman: returns (counts, symbols) for decode-by-length
    maxl = 15
    count = [0] * (maxl + 1)
    for l in lengths:
        count[l] += 1
    count[0] = 0
    offs = [0] * (maxl + 2)
    for i in range(1, maxl + 1):
        offs[i + 1] = offs[i] + count[i]
    symbols = [0] * len(lengths)
    for s in range(len(lengths)):
        l = lengths[s]
        if l:
            symbols[offs[l]] = s
            offs[l] += 1
    return count, symbols


def _decode(br, h):
    count, symbols = h
    code = 0
    first = 0
    index = 0
    for l in range(1, 16):
        code |= br.bits(1)
        c = count[l]
        if code - c < first:
            return symbols[index + (code - first)]
        index += c
        first += c
        first <<= 1
        code <<= 1
    raise error("Error -3 while decompressing data: invalid code lengths set")


_FIXED = None


def _fixed():
    global _FIXED
    if _FIXED is None:
        l = [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8
        _FIXED = (_build(l), _build([5] * 30))
    return _FIXED


def _inflate(data, pos=0):
    """returns (output bytes, end position)"""
    br = _Bits(data, pos)
    out = bytearray()
    while True:
        final = br.bits(1)
        typ = br.bits(2)
        if typ == 0:
            br.align()
            if br.p + 4 > len(data):
                raise error("Error -5 while decompressing data: incomplete or truncated stream")
            ln = data[br.p] | (data[br.p + 1] << 8)
            nln = data[br.p + 2] | (data[br.p + 3] << 8)
            if ln != (~nln & 0xFFFF):
                raise error("Error -3 while decompressing data: invalid stored block lengths")
            br.p += 4
            if br.p + ln > len(data):
                raise error("Error -5 while decompressing data: incomplete or truncated stream")
            out += data[br.p:br.p + ln]
            br.p += ln
        elif typ == 1 or typ == 2:
            if typ == 1:
                lh, dh = _fixed()
            else:
                hlit = br.bits(5) + 257
                hdist = br.bits(5) + 1
                hclen = br.bits(4) + 4
                cl = [0] * 19
                for i in range(hclen):
                    cl[_CLORDER[i]] = br.bits(3)
                ch = _build(cl)
                lens = []
                while len(lens) < hlit + hdist:
                    sym = _decode(br, ch)
                    if sym < 16:
                        lens.append(sym)
                    else:
                        if sym == 16:
                            if not lens:
                                raise error("Error -3 while decompressing data: invalid bit length repeat")
                            prev = lens[-1]
                            rep = 3 + br.bits(2)
                        elif sym == 17:
                            prev = 0
                            rep = 3 + br.bits(3)
                        else:
                            prev = 0
                            rep = 11 + br.bits(7)
                        lens.extend([prev] * rep)
                lh = _build(lens[:hlit])
                dh = _build(lens[hlit:hlit + hdist])
            while True:
                sym = _decode(br, lh)
                if sym < 256:
                    out.append(sym)
                elif sym == 256:
                    break
                else:
                    sym -= 257
                    if sym >= 29:
                        raise error("Error -3 while decompressing data: invalid literal/length code")
                    ln = _LBASE[sym] + br.bits(_LEXT[sym])
                    ds = _decode(br, dh)
                    if ds >= 30:
                        raise error("Error -3 while decompressing data: invalid distance code")
                    dist = _DBASE[ds] + br.bits(_DEXT[ds])
                    if dist > len(out):
                        raise error("Error -3 while decompressing data: invalid distance too far back")
                    st = len(out) - dist
                    if dist >= ln:
                        out += out[st:st + ln]
                    else:
                        for k in range(ln):
                            out.append(out[st + k])
        else:
            raise error("Error -3 while decompressing data: invalid block type")
        if final:
            break
    return bytes(out), br.p


class _BW:
    def __init__(self):
        self.out = bytearray()
        self.bb = 0
        self.bn = 0

    def put(self, v, n):
        self.bb |= v << self.bn
        self.bn += n
        while self.bn >= 8:
            self.out.append(self.bb & 255)
            self.bb >>= 8
            self.bn -= 8

    def put_rev(self, code, n):
        r = 0
        for _ in range(n):
            r = (r << 1) | (code & 1)
            code >>= 1
        self.put(r, n)

    def flush(self):
        if self.bn:
            self.out.append(self.bb & 255)
            self.bb = 0
            self.bn = 0


def _lcode(ln):
    i = 28
    while _LBASE[i] > ln:
        i -= 1
    return i


def _dcode(d):
    i = 29
    while _DBASE[i] > d:
        i -= 1
    return i


def _put_lit(bw, sym):
    if sym < 144:
        bw.put_rev(0x30 + sym, 8)
    elif sym < 256:
        bw.put_rev(0x190 + sym - 144, 9)
    elif sym < 280:
        bw.put_rev(sym - 256, 7)
    else:
        bw.put_rev(0xC0 + sym - 280, 8)


def _deflate_raw(data, level, final=True, bw=None):
    own = bw is None
    if own:
        bw = _BW()
    n = len(data)
    if level == 0:
        pos = 0
        if n == 0:
            bw.put(1 if final else 0, 1)
            bw.put(0, 2)
            bw.flush()
            bw.out += b'\x00\x00\xff\xff'
        while pos < n:
            chunk = data[pos:pos + 65535]
            pos += len(chunk)
            last = 1 if (final and pos >= n) else 0
            bw.put(last, 1)
            bw.put(0, 2)
            bw.flush()
            ln = len(chunk)
            bw.out.append(ln & 255)
            bw.out.append(ln >> 8)
            bw.out.append((~ln) & 255)
            bw.out.append(((~ln) >> 8) & 255)
            bw.out += chunk
        return bw.out if own else None
    bw.put(1 if final else 0, 1)
    bw.put(1, 2)
    head = {}
    i = 0
    maxchain = 8 if level < 6 else 32
    while i < n:
        best = 0
        bdist = 0
        if i + 2 < n:
            key = data[i:i + 3]
            cands = head.get(key)
            if cands:
                tries = 0
                k = len(cands) - 1
                while k >= 0 and tries < maxchain:
                    j = cands[k]
                    if i - j > 32768:
                        break
                    ln = 3
                    mx = min(258, n - i)
                    while ln < mx and data[j + ln] == data[i + ln]:
                        ln += 1
                    if ln > best:
                        best = ln
                        bdist = i - j
                        if ln == mx:
                            break
                    k -= 1
                    tries += 1
        if best >= 3:
            lc = _lcode(best)
            _put_lit(bw, 257 + lc)
            if _LEXT[lc]:
                bw.put(best - _LBASE[lc], _LEXT[lc])
            dc = _dcode(bdist)
            bw.put_rev(dc, 5)
            if _DEXT[dc]:
                bw.put(bdist - _DBASE[dc], _DEXT[dc])
            for t in range(i, i + best):
                if t + 2 < n:
                    kk = data[t:t + 3]
                    if kk in head:
                        head[kk].append(t)
                    else:
                        head[kk] = [t]
            i += best
        else:
            _put_lit(bw, data[i])
            if i + 2 < n:
                kk = data[i:i + 3]
                if kk in head:
                    head[kk].append(i)
                else:
                    head[kk] = [i]
            i += 1
    _put_lit(bw, 256)
    if own:
        bw.flush()
        return bw.out
    return None


def _check_level(level):
    if level < -1 or level > 9:
        raise error("Bad compression level")


def compress(data, /, level=-1, wbits=15):
    _check_level(level)
    data = bytes(data)
    if level == -1:
        level = 6
    if wbits < 0:
        return bytes(_deflate_raw(data, level))
    raw = bytes(_deflate_raw(data, level))
    if wbits > 15:
        return _gzip_wrap(raw, data)
    hdr = b'\x78\x9c'
    if level < 2:
        hdr = b'\x78\x01'
    elif level < 6:
        hdr = b'\x78\x5e'
    elif level > 6:
        hdr = b'\x78\xda'
    return hdr + raw + adler32(data).to_bytes(4, 'big')


def _gzip_wrap(raw, data):
    return (b'\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff' + raw + crc32(data).to_bytes(4, 'little') +
            (len(data) & 0xFFFFFFFF).to_bytes(4, 'little'))


def _parse_gzip_header(data):
    if len(data) < 10 or data[0] != 0x1f or data[1] != 0x8b:
        raise error("Error -3 while decompressing data: incorrect header check")
    flg = data[3]
    p = 10
    if flg & 4:
        xl = data[p] | (data[p + 1] << 8)
        p += 2 + xl
    if flg & 8:
        while data[p] != 0:
            p += 1
        p += 1
    if flg & 16:
        while data[p] != 0:
            p += 1
        p += 1
    if flg & 2:
        p += 2
    return p


def _decompress_any(data, wbits):
    # returns (output, consumed)
    if wbits < 0:
        out, p = _inflate(data, 0)
        return out, p
    if wbits > 15 + 16 - 1 and wbits >= 32 or wbits >= 16 and wbits < 32 or (wbits >= 32):
        if wbits >= 32 and len(data) >= 2 and not (data[0] == 0x1f and data[1] == 0x8b):
            return _decompress_any(data, wbits - 32 if wbits - 32 > 0 else 15)
        start = _parse_gzip_header(data)
        out, p = _inflate(data, start)
        if p + 8 > len(data):
            raise error("Error -5 while decompressing data: incomplete or truncated stream")
        if int.from_bytes(data[p:p + 4], 'little') != crc32(out):
            raise error("Error -3 while decompressing data: incorrect data check")
        return out, p + 8
    if len(data) < 2:
        raise error("Error -5 while decompressing data: incomplete or truncated stream")
    cmf = data[0]
    flg = data[1]
    if (cmf & 15) != 8 or ((cmf << 8) | flg) % 31 != 0:
        raise error("Error -3 while decompressing data: incorrect header check")
    p = 2
    if flg & 32:
        p += 4
    out, p = _inflate(data, p)
    if p + 4 > len(data):
        raise error("Error -5 while decompressing data: incomplete or truncated stream")
    if int.from_bytes(data[p:p + 4], 'big') != adler32(out):
        raise error("Error -3 while decompressing data: incorrect data check")
    return out, p + 4


def decompress(data, /, wbits=15, bufsize=16384):
    data = bytes(data)
    out, used = _decompress_any(data, wbits)
    return out


class _Compress:
    def __init__(self, level=-1, method=8, wbits=15, memLevel=8, strategy=0, zdict=None):
        _check_level(level)
        self.level = 6 if level == -1 else level
        self.wbits = wbits
        self.buf = bytearray()

    def compress(self, data):
        self.buf += bytes(data)
        return b''

    def flush(self, mode=Z_FINISH):
        data = bytes(self.buf)
        self.buf = bytearray()
        return compress(data, self.level, self.wbits)

    def copy(self):
        c = _Compress(self.level, 8, self.wbits)
        c.buf = bytearray(self.buf)
        return c


class _Decompress:
    def __init__(self, wbits=15, zdict=None):
        self.wbits = wbits
        self.buf = bytearray()
        self.eof = False
        self.unused_data = b''
        self.unconsumed_tail = b''
        self._out = b''
        self._emitted = 0

    def decompress(self, data, max_length=0):
        if self.eof:
            self.unused_data += bytes(data)
            return b''
        self.buf += bytes(data)
        try:
            out, used = _decompress_any(bytes(self.buf), self.wbits)
        except error as e:
            if 'incomplete' in str(e):
                return b''
            raise
        self.eof = True
        self.unused_data = bytes(self.buf[used:])
        res = out[self._emitted:]
        self._emitted = len(out)
        if max_length:
            self.unconsumed_tail = res[max_length:]
            res = res[:max_length]
        return res

    def flush(self, length=16384):
        return b''

    def copy(self):
        d = _Decompress(self.wbits)
        d.buf = bytearray(self.buf)
        return d


def compressobj(level=-1, method=8, wbits=15, memLevel=8, strategy=0, zdict=None):
    return _Compress(level, method, wbits, memLevel, strategy, zdict)


def decompressobj(wbits=15, zdict=None):
    return _Decompress(wbits, zdict)
