# Pure Python additions to the native math module (only names the native module lacks are used).
import math as _m

_sqrt = _m.sqrt
_log = _m.log
_exp = _m.exp
_pi = _m.pi
inf = float("inf")
nan = float("nan")


def isclose(a, b, rel_tol=1e-09, abs_tol=0.0):
    if rel_tol < 0.0 or abs_tol < 0.0:
        raise ValueError("tolerances must be non-negative")
    if a == b:
        return True
    if a in (inf, -inf) or b in (inf, -inf):
        return False
    d = abs(b - a)
    return d <= abs(rel_tol * b) or d <= abs(rel_tol * a) or d <= abs_tol


def log1p(x):
    if x <= -1.0:
        if x == -1.0:
            raise ValueError("math domain error")
        raise ValueError("math domain error")
    u = 1.0 + x
    if u == 1.0:
        return float(x)
    return _log(u) * x / (u - 1.0)


def expm1(x):
    if abs(x) < 1e-5:
        return x + x * x / 2.0 + x * x * x / 6.0
    return _exp(x) - 1.0


def asinh(x):
    if x == 0.0:
        return x
    a = abs(x)
    if a > 1e150:
        r = _log(a) + _log(2.0)
    else:
        r = log1p(a + a * a / (1.0 + _sqrt(1.0 + a * a)))
    return r if x > 0 else -r


def acosh(x):
    if x < 1.0:
        raise ValueError("math domain error")
    if x > 1e150:
        return _log(x) + _log(2.0)
    return _log(x + _sqrt(x * x - 1.0))


def atanh(x):
    if abs(x) >= 1.0:
        raise ValueError("math domain error")
    return 0.5 * log1p(2.0 * x / (1.0 - x))


def modf(x):
    if x in (inf, -inf):
        return (0.0 if x > 0 else -0.0, x)
    t = float(_m.trunc(x))
    return (x - t, t)


def frexp(x):
    if x == 0.0 or x != x or x in (inf, -inf):
        return (x, 0)
    e = 0
    m = abs(x)
    while m >= 1.0:
        m /= 2.0
        e += 1
    while m < 0.5:
        m *= 2.0
        e -= 1
    return (m if x > 0 else -m, e)


def ldexp(x, i):
    return x * (2.0 ** i)


def exp2(x):
    return 2.0 ** x


def dist(p, q):
    if len(p) != len(q):
        raise ValueError("both points must have the same number of dimensions")
    s = 0.0
    for a, b in zip(p, q):
        s += (a - b) * (a - b)
    return _sqrt(s)


def remainder(x, y):
    if y == 0.0:
        raise ValueError("math domain error")
    n = round(x / y)
    return float(x - n * y)


def _erf_series(x):
    # erf(x) = 2/sqrt(pi) * exp(-x^2) * sum 2^n x^(2n+1) / (1*3*...*(2n+1))   (all terms positive)
    term = x
    s = x
    n = 0
    x2 = x * x
    while True:
        n += 1
        term = term * 2.0 * x2 / (2 * n + 1)
        s += term
        if term < s * 1e-17:
            break
        if n > 500:
            break
    return 2.0 / _sqrt(_pi) * _exp(-x2) * s


def _erfc_cf(x):
    # continued fraction for x >= ~2, evaluated backwards
    t = 0.0
    k = 80
    while k >= 1:
        t = (k / 2.0) / (x + t)
        k -= 1
    return _exp(-x * x) / _sqrt(_pi) / (x + t)


def erf(x):
    if x != x:
        return x
    a = abs(x)
    if a >= 6.0:
        return 1.0 if x > 0 else -1.0
    if a < 2.5:
        r = _erf_series(a)
    else:
        r = 1.0 - _erfc_cf(a)
    return r if x >= 0 else -r


def erfc(x):
    if x != x:
        return x
    if x < 0:
        return 2.0 - erfc(-x)
    if x >= 27.0:
        return 0.0
    if x < 2.0:
        return 1.0 - _erf_series(x)
    return _erfc_cf(x)


_LG = [0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313,
       -176.61502916214059, 12.507343278686905, -0.13857109526572012,
       9.9843695780195716e-6, 1.5056327351493116e-7]


def gamma(x):
    if x != x:
        return x
    if x == inf:
        return inf
    if x == -inf:
        raise ValueError("math domain error")
    if x == int(x):
        if x <= 0:
            raise ValueError("math domain error")
        if x <= 171:
            return float(_m.factorial(int(x) - 1))
        raise OverflowError("math range error")
    if x < 0.5:
        return _pi / (_m.sin(_pi * x) * gamma(1.0 - x))
    xm = x - 1.0
    a = _LG[0]
    t = xm + 7.5
    for i in range(1, 9):
        a += _LG[i] / (xm + i)
    return _sqrt(2.0 * _pi) * (t ** (xm + 0.5)) * _exp(-t) * a


def lgamma(x):
    if x != x:
        return x
    if x in (inf, -inf):
        return inf
    if x == int(x) and x <= 0:
        return inf
    if x < 0.5:
        return _log(_pi / abs(_m.sin(_pi * x))) - lgamma(1.0 - x)
    if x == 1.0 or x == 2.0:
        return 0.0
    xm = x - 1.0
    a = _LG[0]
    t = xm + 7.5
    for i in range(1, 9):
        a += _LG[i] / (xm + i)
    return 0.5 * _log(2.0 * _pi) + (xm + 0.5) * _log(t) - t + _log(a)


def sumprod(p, q):
    p = list(p)
    q = list(q)
    if len(p) != len(q):
        raise ValueError("Inputs are not the same length")
    t = 0
    for i in range(len(p)):
        t += p[i] * q[i]
    return t


def ulp(x):
    if x != x or x in (inf, -inf):
        return abs(x)
    x = abs(x)
    if x == 0.0:
        return 5e-324
    m, e = frexp(x)
    if e - 53 < -1074:
        return 5e-324
    return ldexp(1.0, e - 53)


def nextafter(x, y, steps=1):
    for _ in range(steps):
        if x != x or y != y:
            return nan
        if x == y:
            return y
        if x == 0.0:
            x = 5e-324 if y > 0 else -5e-324
            continue
        m, e = frexp(abs(x))
        away = (y > x) == (x > 0)
        if away:
            d = ulp(x)
        elif m == 0.5 and e - 54 >= -1074:
            d = ldexp(1.0, e - 54)
        else:
            d = ulp(x)
        x = x + d if y > x else x - d
    return x


def fma(x, y, z):
    return x * y + z
