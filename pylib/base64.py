# Pure Python base64
import binascii as _bin

__all__ = ['encode', 'decode', 'encodebytes', 'decodebytes', 'b64encode', 'b64decode', 'b32encode', 'b32decode',
           'b32hexencode', 'b32hexdecode', 'b16encode', 'b16decode', 'b85encode', 'b85decode', 'a85encode',
           'a85decode', 'standard_b64encode', 'standard_b64decode', 'urlsafe_b64encode', 'urlsafe_b64decode']


def _bytes_from_decode_data(s):
    if isinstance(s, str):
        try:
            return s.encode('ascii')
        except UnicodeEncodeError:
            raise ValueError('string argument should contain only ASCII characters')
    if isinstance(s, (bytes, bytearray)):
        return bytes(s)
    raise TypeError("argument should be a bytes-like object or ASCII string, not %r" % type(s).__name__)


def _bin_in(s):
    if isinstance(s, str):
        raise TypeError("a bytes-like object is required, not 'str'")
    return bytes(s)


def b64encode(s, altchars=None):
    encoded = _bin.b2a_base64(_bin_in(s), False)
    if altchars is not None:
        altchars = bytes(altchars)
        if len(altchars) != 2:
            raise AssertionError(repr(altchars))
        encoded = encoded.replace(b'+', altchars[0:1]).replace(b'/', altchars[1:2])
    return encoded


def b64decode(s, altchars=None, validate=False):
    s = _bytes_from_decode_data(s)
    if altchars is not None:
        altchars = _bytes_from_decode_data(altchars)
        s = s.replace(altchars[0:1], b'+').replace(altchars[1:2], b'/')
    return _bin.a2b_base64(s, validate)


def standard_b64encode(s):
    return b64encode(s)


def standard_b64decode(s):
    return b64decode(s)


def urlsafe_b64encode(s):
    return b64encode(s).replace(b'+', b'-').replace(b'/', b'_')


def urlsafe_b64decode(s):
    s = _bytes_from_decode_data(s)
    s = s.replace(b'-', b'+').replace(b'_', b'/')
    return b64decode(s)


_b32alphabet = b'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567'
_b32hexalphabet = b'0123456789ABCDEFGHIJKLMNOPQRSTUV'


def _b32encode(alphabet, s):
    s = _bin_in(s)
    out = []
    n = len(s)
    for i in range(0, n, 5):
        chunk = s[i:i + 5]
        l = len(chunk)
        chunk = chunk + b'\x00' * (5 - l)
        v = int.from_bytes(chunk, 'big')
        chars = [alphabet[(v >> (35 - 5 * j)) & 31] for j in range(8)]
        keep = {5: 8, 4: 7, 3: 5, 2: 4, 1: 2}[l]
        for j in range(keep):
            out.append(chars[j])
        for j in range(8 - keep):
            out.append(61)
    return bytes(out)


def _b32decode(alphabet, s, casefold=False, map01=None):
    s = _bytes_from_decode_data(s)
    if len(s) % 8:
        raise _bin.Error('Incorrect padding')
    if map01 is not None:
        map01 = _bytes_from_decode_data(map01)
        s = s.replace(b'0', b'O').replace(b'1', map01)
    if casefold:
        s = s.upper()
    lookup = {}
    for i in range(32):
        lookup[alphabet[i]] = i
    out = bytearray()
    for i in range(0, len(s), 8):
        chunk = s[i:i + 8]
        core = chunk.rstrip(b'=')
        padlen = len(chunk) - len(core)
        if padlen not in (0, 1, 3, 4, 6):
            raise _bin.Error('Incorrect padding')
        if padlen and i + 8 != len(s):
            raise _bin.Error('Incorrect padding')
        v = 0
        for c in core:
            if c not in lookup:
                raise _bin.Error('Non-base32 digit found')
            v = (v << 5) | lookup[c]
        v <<= 5 * padlen
        raw = v.to_bytes(5, 'big')
        keep = {0: 5, 1: 4, 3: 3, 4: 2, 6: 1}[padlen]
        out += raw[:keep]
    return bytes(out)


def b32encode(s):
    return _b32encode(_b32alphabet, s)


def b32decode(s, casefold=False, map01=None):
    return _b32decode(_b32alphabet, s, casefold, map01)


def b32hexencode(s):
    return _b32encode(_b32hexalphabet, s)


def b32hexdecode(s, casefold=False):
    return _b32decode(_b32hexalphabet, s, casefold)


def b16encode(s):
    return _bin.hexlify(_bin_in(s)).upper()


def b16decode(s, casefold=False):
    s = _bytes_from_decode_data(s)
    if casefold:
        s = s.upper()
    for c in s:
        if not (48 <= c <= 57 or 65 <= c <= 70):
            raise _bin.Error('Non-base16 digit found')
    return _bin.unhexlify(s)


def a85encode(b, *, foldspaces=False, wrapcol=0, pad=False, adobe=False):
    b = _bin_in(b)
    padding = (-len(b)) % 4
    if padding:
        b = b + b'\x00' * padding
    res = []
    for i in range(0, len(b), 4):
        v = int.from_bytes(b[i:i + 4], 'big')
        if v == 0:
            res.append('z')
        elif foldspaces and v == 0x20202020:
            res.append('y')
        else:
            chars = []
            for _ in range(5):
                chars.append(chr(33 + v % 85))
                v //= 85
            chars.reverse()
            res.append(''.join(chars))
    result = ''.join(res)
    if padding and not pad:
        if result.endswith('z'):
            result = result[:-1] + '!!!!!'
        result = result[:-padding]
    result = result.encode('ascii')
    if adobe:
        result = b'<~' + result + b'~>'
    if wrapcol:
        wrapcol = max(2 if adobe else 1, wrapcol)
        chunks = [result[i:i + wrapcol] for i in range(0, len(result), wrapcol)]
        if adobe and len(chunks[-1]) + 2 > wrapcol:
            chunks.append(b'')
        result = b'\n'.join(chunks)
    return result


def a85decode(b, *, foldspaces=False, adobe=False, ignorechars=b' \t\n\r\x0b'):
    b = _bytes_from_decode_data(b)
    if adobe:
        if not b.endswith(b'~>'):
            raise ValueError('Ascii85 encoded byte sequences must end with b\'~>\'')
        if b.startswith(b'<~'):
            b = b[2:-2]
        else:
            b = b[:-2]
    out = bytearray()
    group = []
    for c in b:
        if 33 <= c <= 117:
            group.append(c - 33)
            if len(group) == 5:
                v = 0
                for x in group:
                    v = v * 85 + x
                if v > 0xFFFFFFFF:
                    raise ValueError('Ascii85 overflow')
                out += v.to_bytes(4, 'big')
                group = []
        elif c == 122 and not group:
            out += b'\x00\x00\x00\x00'
        elif foldspaces and c == 121 and not group:
            out += b'\x20\x20\x20\x20'
        elif c in ignorechars:
            continue
        else:
            raise ValueError('Non-Ascii85 digit found: %c' % c)
    if group:
        padding = 5 - len(group)
        group = group + [84] * padding
        v = 0
        for x in group:
            v = v * 85 + x
        raw = (v & 0xFFFFFFFF).to_bytes(4, 'big')
        out += raw[:4 - padding]
    return bytes(out)


_b85alphabet = (b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" b"abcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~")


def b85encode(b, pad=False):
    b = _bin_in(b)
    padding = (-len(b)) % 4
    if padding:
        b = b + b'\x00' * padding
    out = bytearray()
    for i in range(0, len(b), 4):
        v = int.from_bytes(b[i:i + 4], 'big')
        chars = []
        for _ in range(5):
            chars.append(_b85alphabet[v % 85])
            v //= 85
        chars.reverse()
        out += bytes(chars)
    if padding and not pad:
        out = out[:-padding]
    return bytes(out)


def b85decode(b):
    b = _bytes_from_decode_data(b)
    lookup = {}
    for i in range(85):
        lookup[_b85alphabet[i]] = i
    padding = (-len(b)) % 5
    b = b + b'~' * padding
    out = bytearray()
    for i in range(0, len(b), 5):
        v = 0
        for c in b[i:i + 5]:
            if c not in lookup:
                raise ValueError('bad base85 character at position %d' % i)
            v = v * 85 + lookup[c]
        if v > 0xFFFFFFFF:
            raise ValueError('base85 overflow in hunk starting at byte %d' % i)
        out += v.to_bytes(4, 'big')
    if padding:
        out = out[:-padding]
    return bytes(out)


MAXLINESIZE = 76
MAXBINSIZE = (MAXLINESIZE // 4) * 3


def encodebytes(s):
    s = _bin_in(s)
    pieces = []
    for i in range(0, len(s), MAXBINSIZE):
        pieces.append(_bin.b2a_base64(s[i:i + MAXBINSIZE]))
    return b''.join(pieces)


def decodebytes(s):
    return _bin.a2b_base64(_bytes_from_decode_data(s))


def encode(input, output):
    while True:
        s = input.read(MAXBINSIZE)
        if not s:
            break
        output.write(encodebytes(s))


def decode(input, output):
    while True:
        line = input.readline()
        if not line:
            break
        output.write(_bin.a2b_base64(line))
