# Pure Python binascii (subset: hex, base64, crc32, crc_hqx, uu)


class Error(ValueError):
    pass


class Incomplete(Exception):
    pass


def _b(data):
    if isinstance(data, str):
        try:
            return data.encode('ascii')
        except UnicodeEncodeError:
            raise ValueError('string argument should contain only ASCII characters')
    return bytes(data)


_HEX = '0123456789abcdef'


def hexlify(data, sep=None, bytes_per_sep=1):
    data = bytes(data)
    if sep is None:
        return data.hex().encode()
    return data.hex(sep, bytes_per_sep).encode()


b2a_hex = hexlify


def unhexlify(hexstr):
    h = _b(hexstr)
    if len(h) % 2:
        raise Error('Odd-length string')
    out = bytearray()
    for i in range(0, len(h), 2):
        try:
            out.append(int(h[i:i + 2].decode(), 16))
            if h[i] in b'+- ' or h[i + 1] in b'+- ':
                raise ValueError()
        except ValueError:
            raise Error('Non-hexadecimal digit found')
    return bytes(out)


a2b_hex = unhexlify

_B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
_B64D = {}
for _i in range(64):
    _B64D[ord(_B64[_i])] = _i


def b2a_base64(data, newline=True):
    data = bytes(data)
    out = []
    n = len(data)
    i = 0
    while i + 2 < n:
        v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2]
        out.append(_B64[v >> 18] + _B64[(v >> 12) & 63] + _B64[(v >> 6) & 63] + _B64[v & 63])
        i += 3
    r = n - i
    if r == 1:
        v = data[i] << 16
        out.append(_B64[v >> 18] + _B64[(v >> 12) & 63] + '==')
    elif r == 2:
        v = (data[i] << 16) | (data[i + 1] << 8)
        out.append(_B64[v >> 18] + _B64[(v >> 12) & 63] + _B64[(v >> 6) & 63] + '=')
    s = ''.join(out)
    if newline:
        s += '\n'
    return s.encode()


def a2b_base64(data, strict_mode=False):
    d = _b(data)
    out = bytearray()
    acc = 0
    nb = 0
    pad = 0
    quad = 0
    for c in d:
        if c == 61:
            pad += 1
            if quad >= 2 and pad + quad == 4:
                quad = 0
                pad = 0
                nb = 0
                acc = 0
            continue
        v = _B64D.get(c)
        if v is None:
            if strict_mode:
                raise Error('Only base64 data is allowed')
            continue
        if pad and strict_mode:
            raise Error('Excess data after padding')
        pad = 0
        acc = (acc << 6) | v
        nb += 6
        quad = (quad + 1) % 4
        if nb >= 8:
            nb -= 8
            out.append((acc >> nb) & 255)
            acc &= (1 << nb) - 1
    if quad != 0:
        if quad == 1:
            raise Error('Invalid base64-encoded string: number of data characters (%d) cannot be 1 more than a multiple of 4' % (len(out) // 3 * 4 + 1))
        raise Error('Incorrect padding')
    return bytes(out)


_CRC = None


def _crctab():
    global _CRC
    if _CRC is None:
        t = []
        for n in range(256):
            c = n
            for _ in range(8):
                if c & 1:
                    c = 0xEDB88320 ^ (c >> 1)
                else:
                    c = c >> 1
            t.append(c)
        _CRC = t
    return _CRC


def crc32(data, crc=0):
    t = _crctab()
    c = (crc & 0xFFFFFFFF) ^ 0xFFFFFFFF
    for b in bytes(data):
        c = t[(c ^ b) & 255] ^ (c >> 8)
    return c ^ 0xFFFFFFFF


def crc_hqx(data, crc):
    for b in bytes(data):
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def b2a_uu(data, *, backtick=False):
    data = bytes(data)
    if len(data) > 45:
        raise Error('At most 45 bytes at once')
    def enc(v):
        if v == 0 and backtick:
            return '`'
        return chr(32 + v)
    out = [enc(len(data))]
    for i in range(0, len(data), 3):
        ch = data[i:i + 3]
        ch = ch + b'\x00' * (3 - len(ch))
        v = (ch[0] << 16) | (ch[1] << 8) | ch[2]
        out.append(enc(v >> 18) + enc((v >> 12) & 63) + enc((v >> 6) & 63) + enc(v & 63))
    return (''.join(out) + '\n').encode()


def a2b_uu(data):
    d = _b(data)
    if not d:
        return b'\x00' * 32
    n = (d[0] - 32) & 63
    out = bytearray()
    body = d[1:].rstrip(b'\r\n')
    for i in range(0, len(body), 4):
        q = body[i:i + 4]
        q = q + b' ' * (4 - len(q))
        v = 0
        for c in q:
            v = (v << 6) | ((c - 32) & 63)
        out.append((v >> 16) & 255)
        out.append((v >> 8) & 255)
        out.append(v & 255)
    return bytes(out[:n])
