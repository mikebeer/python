"""decimal: a compact Decimal (sign, coefficient, exponent) with a context precision."""
import math as _m

ROUND_UP = "ROUND_UP"
ROUND_DOWN = "ROUND_DOWN"
ROUND_CEILING = "ROUND_CEILING"
ROUND_FLOOR = "ROUND_FLOOR"
ROUND_HALF_UP = "ROUND_HALF_UP"
ROUND_HALF_DOWN = "ROUND_HALF_DOWN"
ROUND_HALF_EVEN = "ROUND_HALF_EVEN"
ROUND_05UP = "ROUND_05UP"


class DecimalException(ArithmeticError):
    pass


class InvalidOperation(DecimalException):
    pass


class DivisionByZero(DecimalException, ZeroDivisionError):
    pass


class DivisionUndefined(InvalidOperation, ZeroDivisionError):
    pass


class Inexact(DecimalException):
    pass


class Rounded(DecimalException):
    pass


class Overflow(Inexact, Rounded):
    pass


class Context:
    def __init__(self, prec=28, rounding=ROUND_HALF_EVEN, Emin=-999999, Emax=999999, capitals=1,
                 clamp=0, flags=None, traps=None):
        self.prec = prec
        self.rounding = rounding
        self.Emin = Emin
        self.Emax = Emax
        self.capitals = capitals
        self.clamp = clamp

    def copy(self):
        return Context(self.prec, self.rounding, self.Emin, self.Emax)

    def create_decimal(self, v=0):
        return Decimal(v)._fix(self)

    def __repr__(self):
        return "Context(prec=%d, rounding=%s, Emin=%d, Emax=%d)" % (self.prec, self.rounding, self.Emin, self.Emax)

    def add(self, a, b):
        return _dec(a) + _dec(b)

    def subtract(self, a, b):
        return _dec(a) - _dec(b)

    def multiply(self, a, b):
        return _dec(a) * _dec(b)

    def divide(self, a, b):
        return _dec(a) / _dec(b)


_ctx = [Context()]
DefaultContext = Context()
BasicContext = Context(9, ROUND_HALF_UP)
ExtendedContext = Context(9, ROUND_HALF_EVEN)


def getcontext():
    return _ctx[0]


def setcontext(c):
    _ctx[0] = c


class _LocalCtx:
    def __init__(self, ctx, kw):
        self.ctx = ctx
        self.kw = kw

    def __enter__(self):
        self.saved = _ctx[0]
        c = (self.ctx if self.ctx is not None else self.saved).copy()
        for k in self.kw:
            setattr(c, k, self.kw[k])
        _ctx[0] = c
        return c

    def __exit__(self, a, b, c):
        _ctx[0] = self.saved
        return False


def localcontext(ctx=None, **kw):
    return _LocalCtx(ctx, kw)


def _ndig(n):
    return len(str(n))


def _divround(coeff, drop, sign, mode):
    # coeff / 10**drop rounded according to mode (drop >= 1)
    p = 10 ** drop
    q, r = divmod(coeff, p)
    if r == 0:
        return q
    half = p // 2
    inc = False
    if mode == ROUND_HALF_EVEN:
        inc = r > half or (r == half and q % 2 == 1)
    elif mode == ROUND_HALF_UP:
        inc = r >= half
    elif mode == ROUND_HALF_DOWN:
        inc = r > half
    elif mode == ROUND_UP:
        inc = True
    elif mode == ROUND_DOWN:
        inc = False
    elif mode == ROUND_CEILING:
        inc = sign == 0
    elif mode == ROUND_FLOOR:
        inc = sign == 1
    elif mode == ROUND_05UP:
        inc = q % 5 == 0
    if inc:
        q += 1
    return q


def _dec(v):
    if isinstance(v, Decimal):
        return v
    if isinstance(v, int):
        return Decimal(v)
    return None


def _float_ratio(f):
    m, e = _m.frexp(f)
    n = int(_m.ldexp(m, 53))
    e -= 53
    if e >= 0:
        return n * (2 ** e), 1
    return n, 2 ** (-e)


class Decimal:
    def __init__(self, value="0", context=None):
        self._sp = ""            # "" finite, "F" infinity, "N" quiet NaN
        if isinstance(value, Decimal):
            self._s = value._s
            self._c = value._c
            self._e = value._e
            self._sp = value._sp
        elif isinstance(value, bool):
            self._s = 0
            self._c = 1 if value else 0
            self._e = 0
        elif isinstance(value, int):
            self._s = 1 if value < 0 else 0
            self._c = abs(value)
            self._e = 0
        elif isinstance(value, float):
            if value != value:
                self._s = 0
                self._c = 0
                self._e = 0
                self._sp = "N"
            elif value in (float("inf"), float("-inf")):
                self._s = 1 if value < 0 else 0
                self._c = 0
                self._e = 0
                self._sp = "F"
            else:
                n, d = _float_ratio(value)
                self._s = 1 if (n < 0 or (n == 0 and _m.copysign(1.0, value) < 0)) else 0
                n = abs(n)
                while n % 2 == 0 and d > 1:
                    n //= 2
                    d //= 2
                k = 0
                while d > 1:
                    d //= 2
                    k += 1
                self._c = n * 5 ** k
                self._e = -k
        elif isinstance(value, str):
            self._parse(value)
        elif isinstance(value, (tuple, list)):
            sign, digits, exp = value
            self._s = 1 if sign else 0
            c = 0
            for dg in digits:
                c = c * 10 + dg
            self._c = c
            if exp in ("F", "n", "N"):
                self._sp = "F" if exp == "F" else "N"
                self._e = 0
            else:
                self._e = exp
        else:
            raise TypeError("conversion from %s to Decimal is not supported" % type(value).__name__)

    def _parse(self, text):
        t = text.strip().replace("_", "")
        low = t.lower()
        sign = 0
        body = low
        if body[:1] in ("+", "-"):
            if body[0] == "-":
                sign = 1
            body = body[1:]
        self._s = sign
        self._c = 0
        self._e = 0
        if body in ("inf", "infinity"):
            self._sp = "F"
            return
        if body == "nan" or body[:3] == "nan" and body[3:].isdigit():
            self._sp = "N"
            return
        exp = 0
        if "e" in body:
            k = body.index("e")
            es = body[k + 1:]
            body = body[:k]
            if not es.lstrip("+-").isdigit():
                raise InvalidOperation("[<class 'decimal.ConversionSyntax'>]")
            exp = int(es)
        if "." in body:
            ip, fp = body.split(".", 1)
            exp -= len(fp)
            body = ip + fp
        if not body or not body.isdigit():
            raise InvalidOperation("[<class 'decimal.ConversionSyntax'>]")
        self._c = int(body)
        self._e = exp

    @classmethod
    def from_float(cls, f):
        return cls(f)

    def _make(self, s, c, e):
        d = Decimal.__new_raw()
        d._s = s
        d._c = c
        d._e = e
        return d

    @staticmethod
    def __new_raw():
        return Decimal(0)

    def _fix(self, ctx=None):
        # round to the context precision
        if self._sp:
            return self
        if ctx is None:
            ctx = _ctx[0]
        c = self._c
        n = _ndig(c)
        if c == 0 or n <= ctx.prec:
            return self
        drop = n - ctx.prec
        q = _divround(c, drop, self._s, ctx.rounding)
        e = self._e + drop
        if _ndig(q) > ctx.prec:
            q //= 10
            e += 1
        return self._make(self._s, q, e)

    # ---- inspection ----
    def is_nan(self):
        return self._sp == "N"

    def is_infinite(self):
        return self._sp == "F"

    def is_finite(self):
        return self._sp == ""

    def is_zero(self):
        return self._sp == "" and self._c == 0

    def is_signed(self):
        return self._s == 1

    def is_normal(self):
        return self._sp == "" and self._c != 0

    def adjusted(self):
        return self._e + _ndig(self._c) - 1

    def as_tuple(self):
        if self._sp == "F":
            return DecimalTuple(self._s, (0,), "F")
        if self._sp == "N":
            return DecimalTuple(self._s, (), "n")
        return DecimalTuple(self._s, tuple([int(ch) for ch in str(self._c)]), self._e)

    def as_integer_ratio(self):
        n = self._c * (10 ** self._e) if self._e >= 0 else self._c
        d = 1 if self._e >= 0 else 10 ** (-self._e)
        g = _m.gcd(n, d)
        n //= g
        d //= g
        return (-n if self._s else n, d)

    def _ratio(self):
        if self._e >= 0:
            n = self._c * 10 ** self._e
            d = 1
        else:
            n = self._c
            d = 10 ** (-self._e)
        return (-n if self._s else n), d

    # ---- text ----
    def __str__(self):
        if self._sp == "F":
            return "-Infinity" if self._s else "Infinity"
        if self._sp == "N":
            return "NaN"
        sg = "-" if self._s else ""
        ds = str(self._c)
        e = self._e
        adj = e + len(ds) - 1
        if e <= 0 and adj >= -6:
            if e == 0:
                return sg + ds
            if len(ds) > -e:
                return sg + ds[:len(ds) + e] + "." + ds[len(ds) + e:]
            return sg + "0." + "0" * (-e - len(ds)) + ds
        r = ds[0]
        if len(ds) > 1:
            r += "." + ds[1:]
        return sg + r + "E" + ("+" if adj >= 0 else "-") + str(abs(adj))

    def to_eng_string(self):
        return str(self)

    def __repr__(self):
        return "Decimal('%s')" % str(self)

    def __format__(self, spec):
        if not spec:
            return str(self)
        fill = " "
        align = ">"
        i = 0
        if len(spec) > 1 and spec[1] in "<>^=":
            fill = spec[0]
            align = spec[1]
            i = 2
        elif spec[0] in "<>^=":
            align = spec[0]
            i = 1
        sign = "-"
        if i < len(spec) and spec[i] in "+- ":
            sign = spec[i]
            i += 1
        if i < len(spec) and spec[i] == "0":
            fill = "0"
            align = "="
            i += 1
        w = ""
        while i < len(spec) and spec[i].isdigit():
            w += spec[i]
            i += 1
        comma = ""
        if i < len(spec) and spec[i] in ",_":
            comma = spec[i]
            i += 1
        prec = None
        if i < len(spec) and spec[i] == ".":
            i += 1
            p = ""
            while i < len(spec) and spec[i].isdigit():
                p += spec[i]
                i += 1
            prec = int(p)
        typ = spec[i:] if i < len(spec) else ""
        d = self
        pct = False
        if typ == "%":
            d = d * 100
            pct = True
            typ = "f"
        if self._sp:
            body = str(abs(self)) if self._sp == "F" else "NaN"
        else:
            if typ in ("f", "F") or (typ == "" and prec is not None and False):
                if prec is None:
                    prec = 6
                q = d._quantize_exp(-prec, _ctx[0].rounding)
                body = str(abs(q)) if q._e == -prec or True else str(abs(q))
                body = _plain(abs(q))
            elif typ in ("e", "E"):
                body = _sci(abs(d), prec, typ)
            else:
                if prec is not None:
                    q = abs(d)._fix_prec(prec)
                    body = str(q)
                else:
                    body = str(abs(d))
        if comma:
            ip = body
            fp = ""
            rest = ""
            for ch in "eE":
                if ch in ip:
                    k = ip.index(ch)
                    rest = ip[k:]
                    ip = ip[:k]
            if "." in ip:
                ip, fp = ip.split(".")
                fp = "." + fp
            if ip.isdigit():
                out = ""
                while len(ip) > 3:
                    out = comma + ip[-3:] + out
                    ip = ip[:-3]
                body = ip + out + fp + rest
        if pct:
            body += "%"
        sgn = ""
        if self._s:
            sgn = "-"
        elif sign == "+":
            sgn = "+"
        elif sign == " ":
            sgn = " "
        total = sgn + body
        wi = int(w) if w else 0
        if len(total) >= wi:
            return total
        pad = wi - len(total)
        if align == "<":
            return total + fill * pad
        if align == "^":
            return fill * (pad // 2) + total + fill * (pad - pad // 2)
        if align == "=":
            return sgn + fill * pad + body
        return fill * pad + total

    def _fix_prec(self, prec):
        c = Context(prec, _ctx[0].rounding)
        return self._fix(c)

    # ---- conversions ----
    def __int__(self):
        if self._sp:
            raise ValueError("cannot convert NaN to integer") if self._sp == "N" else \
                OverflowError("cannot convert Infinity to integer")
        v = self._c * 10 ** self._e if self._e >= 0 else self._c // 10 ** (-self._e)
        return -v if self._s else v

    def __trunc__(self):
        return self.__int__()

    def __float__(self):
        if self._sp == "N":
            return float("nan")
        if self._sp == "F":
            return float("-inf") if self._s else float("inf")
        return float(str(self))

    def __bool__(self):
        return self._sp != "" or self._c != 0

    def __hash__(self):
        if self._sp:
            return hash(str(self))
        n, d = self._ratio()
        if d == 1:
            return hash(n)
        return hash(float(self)) if float(Decimal(float(self))) == float(self) and Decimal(float(self)) == self \
            else hash((n, d))

    def __floor__(self):
        n, d = self._ratio()
        return n // d

    def __ceil__(self):
        n, d = self._ratio()
        return -((-n) // d)

    def __round__(self, ndigits=None):
        if ndigits is None:
            return int(self._quantize_exp(0, ROUND_HALF_EVEN))
        return self._quantize_exp(-ndigits, ROUND_HALF_EVEN)

    # ---- comparison ----
    def _cmpval(self, o):
        if isinstance(o, Decimal):
            return o
        if isinstance(o, int):
            return Decimal(o)
        if isinstance(o, float):
            return Decimal(o)
        if isinstance(o, (str, list, tuple, dict, set)) or o is None:
            return None
        if hasattr(o, "numerator") and hasattr(o, "denominator"):
            return o
        return None

    def _compare(self, o):
        # returns -1, 0, 1 or None (unordered / not comparable)
        b = self._cmpval(o)
        if b is None:
            return None
        if self._sp == "N":
            return None
        if isinstance(b, Decimal):
            if b._sp == "N":
                return None
            if self._sp == "F" or b._sp == "F":
                x = (0 if self._sp == "" else (-1 if self._s else 1))
                y = (0 if b._sp == "" else (-1 if b._s else 1))
                return -1 if x < y else (1 if x > y else 0)
            n1, d1 = self._ratio()
            n2, d2 = b._ratio()
        else:
            n1, d1 = self._ratio()
            n2, d2 = b.numerator, b.denominator
        x = n1 * d2
        y = n2 * d1
        return -1 if x < y else (1 if x > y else 0)

    def __eq__(self, o):
        return self._compare(o) == 0

    def __ne__(self, o):
        if self._cmpval(o) is None:
            return True
        return self._compare(o) != 0

    def __lt__(self, o):
        if self._cmpval(o) is None:
            return NotImplemented
        c = self._compare(o)
        return c is not None and c < 0

    def __le__(self, o):
        if self._cmpval(o) is None:
            return NotImplemented
        c = self._compare(o)
        return c is not None and c <= 0

    def __gt__(self, o):
        if self._cmpval(o) is None:
            return NotImplemented
        c = self._compare(o)
        return c is not None and c > 0

    def __ge__(self, o):
        if self._cmpval(o) is None:
            return NotImplemented
        c = self._compare(o)
        return c is not None and c >= 0

    def compare(self, o):
        b = _dec(o)
        c = self._compare(b)
        if c is None:
            return Decimal("NaN")
        return Decimal(c)

    def max(self, o):
        b = _dec(o)
        c = self._compare(b)
        if c is None or c >= 0:
            return self
        return b

    def min(self, o):
        b = _dec(o)
        c = self._compare(b)
        if c is None or c <= 0:
            return self
        return b

    # ---- arithmetic ----
    def __neg__(self):
        if self._sp == "N":
            return self
        r = self._make(0 if self._s else 1, self._c, self._e)
        r._sp = self._sp
        if self._sp == "" and self._c == 0:
            r._s = 0 if self._s else 0
        return r._fix()

    def __pos__(self):
        return self._fix()

    def __abs__(self):
        r = self._make(0, self._c, self._e)
        r._sp = self._sp
        return r._fix()

    def copy_abs(self):
        r = self._make(0, self._c, self._e)
        r._sp = self._sp
        return r

    def copy_negate(self):
        r = self._make(0 if self._s else 1, self._c, self._e)
        r._sp = self._sp
        return r

    def copy_sign(self, o):
        r = self._make(o._s, self._c, self._e)
        r._sp = self._sp
        return r

    def __add__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return self._add(b)

    def _add(self, b):
        if self._sp == "N":
            return self
        if b._sp == "N":
            return b
        if self._sp == "F" or b._sp == "F":
            if self._sp == "F" and b._sp == "F" and self._s != b._s:
                raise InvalidOperation("[<class 'decimal.InvalidOperation'>]")
            r = self if self._sp == "F" else b
            return r
        e = self._e if self._e < b._e else b._e
        x = self._c * 10 ** (self._e - e)
        y = b._c * 10 ** (b._e - e)
        if self._s:
            x = -x
        if b._s:
            y = -y
        t = x + y
        if t == 0:
            sign = 1 if (self._s and b._s) else 0
            if _ctx[0].rounding == ROUND_FLOOR and not (self._s == 0 and b._s == 0):
                sign = 1
            return self._make(sign, 0, e)._fix()
        return self._make(1 if t < 0 else 0, abs(t), e)._fix()

    __radd__ = __add__

    def __sub__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return self._add(b.copy_negate_raw())

    def copy_negate_raw(self):
        r = self._make(0 if self._s else 1, self._c, self._e)
        r._sp = self._sp
        return r

    def __rsub__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return b._add(self.copy_negate_raw())

    def __mul__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        if self._sp == "N":
            return self
        if b._sp == "N":
            return b
        s = self._s ^ b._s
        if self._sp == "F" or b._sp == "F":
            if (self._sp == "" and self._c == 0) or (b._sp == "" and b._c == 0):
                raise InvalidOperation("[<class 'decimal.InvalidOperation'>]")
            r = self._make(s, 0, 0)
            r._sp = "F"
            return r
        return self._make(s, self._c * b._c, self._e + b._e)._fix()

    __rmul__ = __mul__

    def __truediv__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return self._div(b)

    def __rtruediv__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return b._div(self)

    def _div(self, b):
        if self._sp == "N":
            return self
        if b._sp == "N":
            return b
        s = self._s ^ b._s
        if self._sp == "F":
            if b._sp == "F":
                raise InvalidOperation("[<class 'decimal.InvalidOperation'>]")
            r = self._make(s, 0, 0)
            r._sp = "F"
            return r
        if b._sp == "F":
            return self._make(s, 0, 0)
        if b._c == 0:
            if self._c == 0:
                raise InvalidOperation("[<class 'decimal.DivisionUndefined'>]")
            raise DivisionByZero("[<class 'decimal.DivisionByZero'>]")
        ctx = _ctx[0]
        ideal = self._e - b._e
        if self._c == 0:
            return self._make(s, 0, ideal)
        shift = ctx.prec + 1 + _ndig(b._c) - _ndig(self._c)
        if shift < 0:
            shift = 0
        q, r = divmod(self._c * 10 ** shift, b._c)
        e = ideal - shift
        if r != 0:
            q = q * 10 + 1
            e -= 1
            return self._make(s, q, e)._fix()
        while e < ideal and q % 10 == 0:
            q //= 10
            e += 1
        return self._make(s, q, e)._fix()

    def __floordiv__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return self._divmod(b)[0]

    def __rfloordiv__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return b._divmod(self)[0]

    def __mod__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return self._divmod(b)[1]

    def __rmod__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return b._divmod(self)[1]

    def __divmod__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return self._divmod(b)

    def _divmod(self, b):
        if self._sp or b._sp:
            raise InvalidOperation("[<class 'decimal.InvalidOperation'>]")
        if b._c == 0:
            if self._c == 0:
                raise InvalidOperation("[<class 'decimal.DivisionUndefined'>]")
            raise DivisionByZero("[<class 'decimal.DivisionByZero'>]")
        e = self._e if self._e < b._e else b._e
        x = self._c * 10 ** (self._e - e)
        y = b._c * 10 ** (b._e - e)
        q = x // y
        r = x - q * y
        s = self._s ^ b._s
        qd = self._make(s, q, 0)
        if _ndig(q) > _ctx[0].prec:
            raise InvalidOperation("[<class 'decimal.DivisionImpossible'>]")
        return qd, self._make(self._s, r, e)._fix()

    def remainder_near(self, o):
        b = _dec(o)
        q, r = self._divmod(b)
        twice = r.copy_abs() * 2
        if twice > b.copy_abs() or (twice == b.copy_abs() and int(q) % 2 == 1):
            if r._s == b._s or True:
                r = r - b if (self._s == b._s) else r + b
        return r

    def __pow__(self, o, mod=None):
        b = _dec(o)
        if b is None:
            return NotImplemented
        if mod is not None:
            return Decimal(pow(int(self), int(b), int(mod)))
        if self._sp or b._sp:
            raise InvalidOperation("[<class 'decimal.InvalidOperation'>]")
        if b._e >= 0 or (b._c % (10 ** (-b._e)) == 0):
            n = int(b)
            if n >= 0:
                if n == 0:
                    return Decimal(1)
                s = self._s if n % 2 == 1 else 0
                return self._make(s, self._c ** n, self._e * n)._fix()
            if self._c == 0:
                raise DivisionByZero("[<class 'decimal.DivisionByZero'>]")
            return Decimal(1)._div(self ** (-n))
        # non-integral exponent: go through floats (approximation)
        return Decimal(float(self) ** float(b))._fix()

    def __rpow__(self, o):
        b = _dec(o)
        if b is None:
            return NotImplemented
        return b ** self

    def sqrt(self):
        if self._sp == "N":
            return self
        if self._s and self._c != 0:
            raise InvalidOperation("[<class 'decimal.InvalidOperation'>]")
        if self._sp == "F":
            return self
        ctx = _ctx[0]
        if self._c == 0:
            return self._make(self._s, 0, self._e // 2)
        c = self._c
        e = self._e
        if e % 2:
            c *= 10
            e -= 1
        # want prec+1 digits of root
        want = ctx.prec + 1
        ds = _ndig(c)
        extra = 2 * want - ds
        if extra < 0:
            extra = 0
        if extra % 2:
            extra += 1
        n = c * 10 ** extra
        r = _isqrt(n)
        exact = r * r == n
        re = (e - extra) // 2
        if not exact:
            r = r * 10 + 1
            re -= 1
            return self._make(0, r, re)._fix()
        ideal = self._e // 2
        while re < ideal and r % 10 == 0:
            r //= 10
            re += 1
        return self._make(0, r, re)._fix()

    def exp(self):
        return Decimal(_m.exp(float(self)))._fix()

    def ln(self):
        return Decimal(_m.log(float(self)))._fix()

    def log10(self):
        return Decimal(_m.log10(float(self)))._fix()

    # ---- rounding ----
    def _quantize_exp(self, exp, mode):
        if self._sp:
            return self
        shift = self._e - exp
        if shift >= 0:
            return self._make(self._s, self._c * 10 ** shift, exp)
        q = _divround(self._c, -shift, self._s, mode)
        return self._make(self._s, q, exp)

    def quantize(self, exp, rounding=None, context=None):
        b = _dec(exp)
        if rounding is None:
            rounding = _ctx[0].rounding
        r = self._quantize_exp(b._e, rounding)
        if not r._sp and _ndig(r._c) > _ctx[0].prec:
            raise InvalidOperation("[<class 'decimal.InvalidOperation'>]")
        return r

    def to_integral_value(self, rounding=None, context=None):
        if rounding is None:
            rounding = _ctx[0].rounding
        if self._e >= 0:
            return self
        return self._quantize_exp(0, rounding)

    to_integral = to_integral_value
    to_integral_exact = to_integral_value

    def normalize(self, context=None):
        if self._sp:
            return self
        r = self._fix()
        if r._c == 0:
            return self._make(r._s, 0, 0)
        c = r._c
        e = r._e
        while c % 10 == 0:
            c //= 10
            e += 1
        return self._make(r._s, c, e)

    def scaleb(self, n):
        return self._make(self._s, self._c, self._e + int(n))

    def __copy__(self):
        return self

    def __deepcopy__(self, memo):
        return self

    def __reduce__(self):
        return (Decimal, (str(self),))

    def __complex__(self):
        raise TypeError("no complex")


class DecimalTuple:
    def __init__(self, sign, digits, exponent):
        self.sign = sign
        self.digits = digits
        self.exponent = exponent

    def __iter__(self):
        return iter((self.sign, self.digits, self.exponent))

    def __getitem__(self, i):
        return (self.sign, self.digits, self.exponent)[i]

    def __len__(self):
        return 3

    def __eq__(self, o):
        return tuple(self) == tuple(o)

    def __repr__(self):
        return "DecimalTuple(sign=%r, digits=%r, exponent=%r)" % (self.sign, self.digits, self.exponent)


def _isqrt(n):
    if n < 2:
        return n
    x = 1 << ((n.bit_length() + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def _plain(d):
    # plain (non-scientific) rendering of a non-negative finite Decimal
    ds = str(d._c)
    e = d._e
    if e >= 0:
        return ds + "0" * e
    if len(ds) > -e:
        return ds[:len(ds) + e] + "." + ds[len(ds) + e:]
    return "0." + "0" * (-e - len(ds)) + ds


def _sci(d, prec, typ):
    if d._c == 0:
        digits = "0"
        adj = d._e
    else:
        digits = str(d._c)
        adj = d._e + len(digits) - 1
    if prec is not None:
        if len(digits) > prec + 1:
            q = _divround(int(digits), len(digits) - prec - 1, 0, ROUND_HALF_EVEN)
            adj += len(digits) - len(str(q)) if False else 0
            if len(str(q)) > prec + 1:
                q //= 10
                adj += 1
            digits = str(q)
        else:
            digits = digits + "0" * (prec + 1 - len(digits))
    r = digits[0]
    if len(digits) > 1:
        r += "." + digits[1:]
    return r + ("E" if typ == "E" else "e") + ("+" if adj >= 0 else "-") + "%02d" % abs(adj)
