# Minimal struct module in pure Python: x c b B ? h H i I l L q Q n N f d s p P, prefixes @ = < > !
# A native struct module (C interpreter) takes precedence.
import math as _m


class error(Exception):
    pass


_STD = {"b": 1, "B": 1, "?": 1, "h": 2, "H": 2, "i": 4, "I": 4, "l": 4, "L": 4,
        "q": 8, "Q": 8, "f": 4, "d": 8, "c": 1, "n": 8, "N": 8, "P": 8, "x": 1, "s": 1, "p": 1}
_NAT = {"b": 1, "B": 1, "?": 1, "h": 2, "H": 2, "i": 4, "I": 4, "l": 8, "L": 8,
        "q": 8, "Q": 8, "f": 4, "d": 8, "c": 1, "n": 8, "N": 8, "P": 8, "x": 1, "s": 1, "p": 1}
_SIGNED = "bhilqn"
_INTS = "bBhHiIlLqQnNP"


def _parse(fmt):
    if isinstance(fmt, (bytes, bytearray)):
        fmt = fmt.decode("ascii")
    order = "<"
    native = True
    i = 0
    if fmt and fmt[0] in "@=<>!":
        c = fmt[0]
        i = 1
        if c == "@":
            native = True
        else:
            native = False
            order = "<" if c in "=<" else ">"
    sizes = _NAT if native else _STD
    items = []  # (code, count, offset)
    off = 0
    n = len(fmt)
    while i < n:
        c = fmt[i]
        if c.isspace():
            i += 1
            continue
        cnt = ""
        while i < n and fmt[i].isdigit():
            cnt += fmt[i]
            i += 1
        if i >= n:
            raise error("repeat count given without format specifier")
        c = fmt[i]
        i += 1
        if c not in sizes:
            raise error("bad char in struct format")
        cnt = int(cnt) if cnt else 1
        sz = sizes[c]
        if native and c not in "xspc?" and sz > 1:
            off = (off + sz - 1) // sz * sz
        elif native and c in "?":
            pass
        items.append((c, cnt, off))
        if c in "sp":
            off += cnt
        else:
            off += sz * cnt
    return order, sizes, items, off


def calcsize(fmt):
    return _parse(fmt)[3]


def _ld(x, e):
    h = e // 2
    return _m.ldexp(_m.ldexp(x, h), e - h)


def _decompose(x, mbits, ebits):
    """(sign, biased exponent, mantissa) of the float x for an IEEE format"""
    bias = (1 << (ebits - 1)) - 1
    emax = (1 << ebits) - 1
    sign = 1 if _m.copysign(1.0, x) < 0 else 0
    if x != x:
        return (0, emax, 1 << (mbits - 1))
    x = abs(x)
    if x == float("inf"):
        return (sign, emax, 0)
    if x == 0.0:
        return (sign, 0, 0)
    m, e = _m.frexp(x)  # x = m * 2**e, 0.5 <= m < 1
    e -= 1
    biased = e + bias
    if biased <= 0:
        mant = round(_ld(x, bias - 1 + mbits))
        biased = 0
        if mant >> mbits:
            biased = 1
            mant &= (1 << mbits) - 1
    else:
        mant = round(_m.ldexp(m * 2.0 - 1.0, mbits))
        if mant >> mbits:
            mant = 0
            biased += 1
        if biased >= emax:
            raise OverflowError("float too large to pack with %s format" % ("f" if mbits == 23 else "d"))
    return (sign, biased, mant)


def _fbytes(x, code):
    """big endian bytes of the float x as 'f' or 'd'"""
    if code == "f":
        s, e, mt = _decompose(x, 23, 8)
        return ((s << 31) | (e << 23) | mt).to_bytes(4, "big")
    s, e, mt = _decompose(x, 52, 11)
    hi = (s << 31) | (e << 20) | (mt >> 32)
    lo = mt & 0xFFFFFFFF
    return hi.to_bytes(4, "big") + lo.to_bytes(4, "big")


def _frombytes(b, code):
    """float from big endian bytes"""
    if code == "f":
        v = int.from_bytes(b, "big")
        sign = v >> 31
        biased = (v >> 23) & 255
        mant = v & 8388607
        mbits = 23
        ebits = 8
    else:
        hi = int.from_bytes(b[:4], "big")
        lo = int.from_bytes(b[4:], "big")
        sign = hi >> 31
        biased = (hi >> 20) & 2047
        mant = ((hi & 1048575) << 32) | lo
        mbits = 52
        ebits = 11
    bias = (1 << (ebits - 1)) - 1
    emax = (1 << ebits) - 1
    if biased == emax:
        r = float("inf") if mant == 0 else float("nan")
    elif biased == 0:
        r = _ld(float(mant), 1 - bias - mbits)
    else:
        r = _m.ldexp(float(mant | (1 << mbits)), biased - bias - mbits)
    return -r if sign else r


def _range_msg(code, lo, hi, size):
    if size >= 8:
        return "argument out of range"
    return "'%s' format requires %d <= number <= %d" % (code, lo, hi)


def _pack_int(v, size, order, signed, code):
    if isinstance(v, float) or not hasattr(v, "__index__") and not isinstance(v, int):
        raise error("required argument is not an integer")
    v = int(v)
    if signed:
        lim = 2 ** (size * 8 - 1)
        if v < -lim or v >= lim:
            raise error(_range_msg(code, -lim, lim - 1, size))
    else:
        if v < 0 or v >= 2 ** (size * 8):
            raise error(_range_msg(code, 0, 2 ** (size * 8) - 1, size))
    return v.to_bytes(size, "little" if order == "<" else "big", signed=signed)


def _pack_one(code, v, size, order):
    if code in _INTS:
        return _pack_int(v, size, order, code in _SIGNED, code)
    if code == "?":
        return b"\x01" if v else b"\x00"
    if code == "c":
        if not isinstance(v, (bytes, bytearray)) or len(v) != 1:
            raise error("char format requires a bytes object of length 1")
        return bytes(v)
    if code == "f" or code == "d":
        if not isinstance(v, (int, float)):
            raise error("required argument is not a float")
        b = _fbytes(float(v), code)
        if order == "<":
            b = bytes(list(b)[::-1])
        return b
    raise error("bad char in struct format")


def pack(fmt, *vals):
    order, sizes, items, total = _parse(fmt)
    need = 0
    for c, cnt, off in items:
        if c == "x":
            continue
        need += 1 if c in "sp" else cnt
    if need != len(vals):
        raise error("pack expected %d items for packing (got %d)" % (need, len(vals)))
    out = bytearray()
    vi = 0
    for c, cnt, off in items:
        while len(out) < off:
            out.append(0)
        if c == "x":
            out.extend(b"\x00" * cnt)
        elif c == "s" or c == "p":
            v = vals[vi]
            vi += 1
            if not isinstance(v, (bytes, bytearray)):
                raise error("argument for 's' must be a bytes object")
            v = bytes(v)[:cnt]
            out.extend(v + b"\x00" * (cnt - len(v)))
        else:
            for _ in range(cnt):
                out.extend(_pack_one(c, vals[vi], sizes[c], order))
                vi += 1
    while len(out) < total:
        out.append(0)
    return bytes(out)


def _unpack_at(order, sizes, items, buf, base):
    res = []
    for c, cnt, off in items:
        p = base + off
        if c == "x":
            continue
        if c == "s" or c == "p":
            res.append(bytes(buf[p:p + cnt]))
            continue
        sz = sizes[c]
        for k in range(cnt):
            chunk = bytes(buf[p:p + sz])
            p += sz
            if c in _INTS:
                res.append(int.from_bytes(chunk, "little" if order == "<" else "big", signed=c in _SIGNED))
            elif c == "?":
                res.append(chunk != b"\x00")
            elif c == "c":
                res.append(chunk)
            elif c == "f" or c == "d":
                if order == "<":
                    chunk = bytes(list(chunk)[::-1])
                res.append(_frombytes(chunk, c))
    return tuple(res)


def unpack(fmt, buf):
    order, sizes, items, total = _parse(fmt)
    if len(buf) != total:
        raise error("unpack requires a buffer of %d bytes" % total)
    return _unpack_at(order, sizes, items, buf, 0)


def unpack_from(fmt, buf, offset=0):
    order, sizes, items, total = _parse(fmt)
    if offset < 0:
        offset += len(buf)
    if len(buf) - offset < total:
        raise error("unpack_from requires a buffer of at least %d bytes for unpacking %d bytes at offset %d (actual buffer size is %d)" % (total + offset, total, offset, len(buf)))
    return _unpack_at(order, sizes, items, buf, offset)


def pack_into(fmt, buf, offset, *vals):
    data = pack(fmt, *vals)
    if offset < 0:
        offset += len(buf)
    if offset < 0 or len(buf) - offset < len(data):
        raise error("pack_into requires a buffer of at least %d bytes for packing %d bytes at offset %d (actual buffer size is %d)" % (len(data) + offset, len(data), offset, len(buf)))
    for i in range(len(data)):
        buf[offset + i] = data[i]


def iter_unpack(fmt, buf):
    order, sizes, items, total = _parse(fmt)
    if total == 0 or len(buf) % total != 0:
        raise error("iterative unpacking requires a buffer of a multiple of %d bytes" % total)
    res = []
    for p in range(0, len(buf), total):
        res.append(_unpack_at(order, sizes, items, buf, p))
    return iter(res)


class Struct:
    def __init__(self, format):
        self.format = format
        self.size = calcsize(format)

    def pack(self, *vals):
        return pack(self.format, *vals)

    def unpack(self, buf):
        return unpack(self.format, buf)

    def unpack_from(self, buf, offset=0):
        return unpack_from(self.format, buf, offset)

    def pack_into(self, buf, offset, *vals):
        return pack_into(self.format, buf, offset, *vals)

    def iter_unpack(self, buf):
        return iter_unpack(self.format, buf)
