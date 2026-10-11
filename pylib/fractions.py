"""fractions: a minimal Fraction."""
import math as _m


def _gcd(a, b):
    while b:
        a, b = b, a % b
    return a


class Fraction:
    def __init__(self, numerator=0, denominator=None):
        if denominator is None:
            if isinstance(numerator, Fraction):
                n = numerator.numerator
                d = numerator.denominator
            elif isinstance(numerator, int):
                n = numerator
                d = 1
            elif isinstance(numerator, float):
                n, d = _float_ratio(numerator)
            elif isinstance(numerator, str):
                n, d = _parse(numerator)
            elif hasattr(numerator, "as_integer_ratio"):
                n, d = numerator.as_integer_ratio()
            else:
                raise TypeError("argument should be a string or a Rational instance")
        else:
            if isinstance(numerator, Fraction):
                n1, d1 = numerator.numerator, numerator.denominator
            else:
                n1, d1 = numerator, 1
            if isinstance(denominator, Fraction):
                n2, d2 = denominator.numerator, denominator.denominator
            else:
                n2, d2 = denominator, 1
            n = n1 * d2
            d = d1 * n2
        if d == 0:
            raise ZeroDivisionError("Fraction(%d, 0)" % n)
        g = _gcd(abs(n), abs(d))
        if d < 0:
            g = -g
        self.numerator = n // g
        self.denominator = d // g

    @classmethod
    def from_float(cls, f):
        return cls(f)

    @classmethod
    def from_decimal(cls, d):
        return cls(d)

    def as_integer_ratio(self):
        return (self.numerator, self.denominator)

    def limit_denominator(self, max_denominator=1000000):
        if self.denominator <= max_denominator:
            return Fraction(self.numerator, self.denominator)
        p0, q0, p1, q1 = 0, 1, 1, 0
        n, d = self.numerator, self.denominator
        while True:
            a = n // d
            q2 = q0 + a * q1
            if q2 > max_denominator:
                break
            p0, q0, p1, q1 = p1, q1, p0 + a * p1, q2
            n, d = d, n - a * d
        k = (max_denominator - q0) // q1
        b1 = Fraction(p0 + k * p1, q0 + k * q1)
        b2 = Fraction(p1, q1)
        if abs(b2 - self) <= abs(b1 - self):
            return b2
        return b1

    @staticmethod
    def _coerce(o):
        if isinstance(o, Fraction):
            return o
        if isinstance(o, int):
            return Fraction(o)
        return None

    def __add__(self, o):
        if isinstance(o, float):
            return float(self) + o
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        return Fraction(self.numerator * b.denominator + b.numerator * self.denominator,
                        self.denominator * b.denominator)

    __radd__ = __add__

    def __sub__(self, o):
        if isinstance(o, float):
            return float(self) - o
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        return Fraction(self.numerator * b.denominator - b.numerator * self.denominator,
                        self.denominator * b.denominator)

    def __rsub__(self, o):
        if isinstance(o, float):
            return o - float(self)
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        return b - self

    def __mul__(self, o):
        if isinstance(o, float):
            return float(self) * o
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        return Fraction(self.numerator * b.numerator, self.denominator * b.denominator)

    __rmul__ = __mul__

    def __truediv__(self, o):
        if isinstance(o, float):
            return float(self) / o
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        if b.numerator == 0:
            raise ZeroDivisionError("Fraction(%d, 0)" % self.numerator)
        return Fraction(self.numerator * b.denominator, self.denominator * b.numerator)

    def __rtruediv__(self, o):
        if isinstance(o, float):
            return o / float(self)
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        return b / self

    def __floordiv__(self, o):
        if isinstance(o, float):
            return float(self) // o
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        return (self.numerator * b.denominator) // (self.denominator * b.numerator)

    def __rfloordiv__(self, o):
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        return b // self

    def __mod__(self, o):
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        return self - b * (self // b)

    def __rmod__(self, o):
        b = Fraction._coerce(o)
        if b is None:
            return NotImplemented
        return b % self

    def __divmod__(self, o):
        return (self // o, self % o)

    def __pow__(self, e):
        if isinstance(e, int):
            if e >= 0:
                return Fraction(self.numerator ** e, self.denominator ** e)
            if self.numerator == 0:
                raise ZeroDivisionError("Fraction(%d, 0)" % (self.denominator ** -e))
            return Fraction(self.denominator ** -e, self.numerator ** -e)
        if isinstance(e, Fraction) and e.denominator == 1:
            return self ** e.numerator
        return float(self) ** float(e)

    def __rpow__(self, b):
        if self.denominator == 1 and self.numerator >= 0:
            return b ** self.numerator
        return b ** float(self)

    def __neg__(self):
        return Fraction(-self.numerator, self.denominator)

    def __pos__(self):
        return self

    def __abs__(self):
        return Fraction(abs(self.numerator), self.denominator)

    def __bool__(self):
        return self.numerator != 0

    def __int__(self):
        if self.numerator < 0:
            return -(-self.numerator // self.denominator)
        return self.numerator // self.denominator

    def __trunc__(self):
        return self.__int__()

    def __floor__(self):
        return self.numerator // self.denominator

    def __ceil__(self):
        return -(-self.numerator // self.denominator)

    def __float__(self):
        return self.numerator / self.denominator

    def __round__(self, ndigits=None):
        if ndigits is None:
            fl, rem = divmod(self.numerator, self.denominator)
            c = rem * 2 - self.denominator
            if c < 0:
                return fl
            if c > 0:
                return fl + 1
            return fl if fl % 2 == 0 else fl + 1
        shift = 10 ** abs(ndigits)
        if ndigits > 0:
            return Fraction(round(self * shift), shift)
        return Fraction(round(self / shift) * shift)

    def _cmp(self, o):
        if isinstance(o, float):
            a = float(self)
            return -1 if a < o else (1 if a > o else 0)
        b = Fraction._coerce(o)
        if b is None:
            return None
        x = self.numerator * b.denominator
        y = b.numerator * self.denominator
        return -1 if x < y else (1 if x > y else 0)

    def __eq__(self, o):
        c = self._cmp(o)
        return c == 0

    def __ne__(self, o):
        c = self._cmp(o)
        return c != 0

    def __lt__(self, o):
        c = self._cmp(o)
        if c is None:
            return NotImplemented
        return c < 0

    def __le__(self, o):
        c = self._cmp(o)
        if c is None:
            return NotImplemented
        return c <= 0

    def __gt__(self, o):
        c = self._cmp(o)
        if c is None:
            return NotImplemented
        return c > 0

    def __ge__(self, o):
        c = self._cmp(o)
        if c is None:
            return NotImplemented
        return c >= 0

    def __hash__(self):
        if self.denominator == 1:
            return hash(self.numerator)
        return hash((self.numerator, self.denominator))

    def __str__(self):
        if self.denominator == 1:
            return str(self.numerator)
        return "%d/%d" % (self.numerator, self.denominator)

    def __repr__(self):
        return "Fraction(%d, %d)" % (self.numerator, self.denominator)

    def __format__(self, spec):
        return format(str(self), spec)

    def __copy__(self):
        return self

    def __deepcopy__(self, memo):
        return self


def _float_ratio(f):
    if f != f or f in (float("inf"), float("-inf")):
        raise OverflowError("cannot convert Infinity or NaN to integer ratio") if f == f else \
            ValueError("cannot convert NaN to integer ratio")
    m, e = _m.frexp(f)
    # f = m * 2**e with 0.5 <= |m| < 1; scale m to an integer of 53 bits
    n = int(_m.ldexp(m, 53))
    e -= 53
    if e >= 0:
        return n * (2 ** e), 1
    return n, 2 ** (-e)


def _parse(s):
    t = s.strip().replace("_", "")
    if not t:
        raise ValueError("Invalid literal for Fraction: %r" % s)
    try:
        if "/" in t:
            a, b = t.split("/")
            n = int(a)
            d = int(b)
            if d == 0:
                raise ZeroDivisionError("Fraction(%d, 0)" % n)
            return n, d
        exp = 0
        low = t.lower()
        if "e" in low:
            k = low.index("e")
            exp = int(low[k + 1:])
            t = t[:k]
        sign = 1
        if t[0] in "+-":
            if t[0] == "-":
                sign = -1
            t = t[1:]
        if "." in t:
            ip, fp = t.split(".")
            if not (ip + fp).isdigit():
                raise ValueError("x")
            n = int(ip + fp) if (ip + fp) else 0
            exp -= len(fp)
        else:
            if not t.isdigit():
                raise ValueError("x")
            n = int(t)
        if exp >= 0:
            return sign * n * 10 ** exp, 1
        return sign * n, 10 ** (-exp)
    except ValueError:
        raise ValueError("Invalid literal for Fraction: %r" % s)
