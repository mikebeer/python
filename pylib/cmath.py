# cmath in pure Python (follows CPython's Modules/cmathmodule.c for the finite cases).
# Works on both interpreters; a native cmath module (C interpreter) takes precedence.
import math as _m

pi = _m.pi
e = _m.e
tau = 2 * _m.pi
inf = float("inf")
nan = float("nan")
infj = complex(0.0, inf)
nanj = complex(0.0, nan)

_hypot = _m.hypot
_copysign = _m.copysign
_ln10 = _m.log(10.0)
_DBL_MIN = 2.2250738585072014e-308


def _c(z):
    if isinstance(z, complex):
        return z
    return complex(z)


def phase(z):
    z = _c(z)
    return _m.atan2(z.imag, z.real)


def polar(z):
    z = _c(z)
    return (_hypot(z.real, z.imag), _m.atan2(z.imag, z.real))


def rect(r, phi):
    r = float(r)
    phi = float(phi)
    if phi == 0.0:
        return complex(r, phi * r if r != 0.0 else 0.0 * phi)
    return complex(r * _m.cos(phi), r * _m.sin(phi))


def isnan(z):
    z = _c(z)
    return z.real != z.real or z.imag != z.imag


def isinf(z):
    z = _c(z)
    return _m.isinf(z.real) or _m.isinf(z.imag)


def isfinite(z):
    z = _c(z)
    return _m.isfinite(z.real) and _m.isfinite(z.imag)


def isclose(a, b, *, rel_tol=1e-09, abs_tol=0.0):
    a = _c(a)
    b = _c(b)
    if rel_tol < 0.0 or abs_tol < 0.0:
        raise ValueError("tolerances must be non-negative")
    if a == b:
        return True
    if isinf(a) or isinf(b):
        return False
    diff = abs(a - b)
    return diff <= rel_tol * abs(b) or diff <= rel_tol * abs(a) or diff <= abs_tol


def sqrt(z):
    z = _c(z)
    re = z.real
    im = z.imag
    if re == 0.0 and im == 0.0:
        return complex(0.0, im)
    ax = abs(re)
    ay = abs(im)
    if ax < _DBL_MIN and ay < _DBL_MIN:
        ax = _m.ldexp(ax, 53)
        ay = _m.ldexp(ay, 53)
        s = _m.ldexp(_m.sqrt(_m.ldexp(ax + _hypot(ax, ay), -1)), -26)
    else:
        ax1 = ax / 8.0
        s = 2.0 * _m.sqrt(ax1 + _hypot(ax1, ay / 8.0))
    d = ay / (2.0 * s)
    if re >= 0.0:
        return complex(s, _copysign(d, im))
    return complex(d, _copysign(s, im))


def exp(z):
    z = _c(z)
    l = _m.exp(z.real)
    if z.imag == 0.0:
        return complex(l, z.imag)
    return complex(l * _m.cos(z.imag), l * _m.sin(z.imag))


def _log(z):
    re = z.real
    im = z.imag
    if re == 0.0 and im == 0.0:
        raise ValueError("math domain error")
    ax = abs(re)
    ay = abs(im)
    h = _hypot(ax, ay)
    if 0.71 <= h and h <= 1.73:
        am = ax if ax > ay else ay
        an = ay if ax > ay else ax
        rr = _m.log1p((am - 1.0) * (am + 1.0) + an * an) / 2.0
    else:
        rr = _m.log(h)
    return complex(rr, _m.atan2(im, re))


def log(z, base=None):
    z = _c(z)
    r = _log(z)
    if base is None:
        return r
    b = _log(_c(base))
    return r / b


def log10(z):
    r = _log(_c(z))
    return complex(r.real / _ln10, r.imag / _ln10)


def cosh(z):
    z = _c(z)
    x = z.real
    y = z.imag
    return complex(_m.cosh(x) * _m.cos(y), _m.sinh(x) * _m.sin(y))


def sinh(z):
    z = _c(z)
    x = z.real
    y = z.imag
    return complex(_m.sinh(x) * _m.cos(y), _m.cosh(x) * _m.sin(y))


def tanh(z):
    z = _c(z)
    x = z.real
    y = z.imag
    tx = _m.tanh(x)
    ty = _m.tan(y)
    cx = 1.0 / _m.cosh(x)
    txty = tx * ty
    denom = 1.0 + txty * txty
    return complex(tx * (1.0 + ty * ty) / denom, ((ty / denom) * cx) * cx)


def cos(z):
    z = _c(z)
    return cosh(complex(-z.imag, z.real))


def sin(z):
    z = _c(z)
    s = sinh(complex(-z.imag, z.real))
    return complex(s.imag, -s.real)


def tan(z):
    z = _c(z)
    s = tanh(complex(-z.imag, z.real))
    return complex(s.imag, -s.real)


def asinh(z):
    z = _c(z)
    s1 = sqrt(complex(1.0 + z.imag, -z.real))
    s2 = sqrt(complex(1.0 - z.imag, z.real))
    return complex(_m.asinh(s1.real * s2.imag - s2.real * s1.imag),
                   _m.atan2(z.imag, s1.real * s2.real - s1.imag * s2.imag))


def acosh(z):
    z = _c(z)
    s1 = sqrt(complex(z.real - 1.0, z.imag))
    s2 = sqrt(complex(z.real + 1.0, z.imag))
    return complex(_m.asinh(s1.real * s2.real + s1.imag * s2.imag),
                   2.0 * _m.atan2(s1.imag, s2.real))


def asin(z):
    z = _c(z)
    s = asinh(complex(-z.imag, z.real))
    return complex(s.imag, -s.real)


def acos(z):
    z = _c(z)
    s1 = sqrt(complex(1.0 - z.real, -z.imag))
    s2 = sqrt(complex(1.0 + z.real, z.imag))
    return complex(2.0 * _m.atan2(s1.real, s2.real),
                   _m.asinh(s2.real * s1.imag - s2.imag * s1.real))


def atanh(z):
    z = _c(z)
    re = z.real
    im = z.imag
    if re < 0.0:
        r = atanh(complex(-re, -im))
        return complex(-r.real, -r.imag)
    ay = abs(im)
    if re == 1.0 and ay == 0.0:
        raise ValueError("math domain error")
    rr = _m.log1p(4.0 * re / ((1.0 - re) * (1.0 - re) + ay * ay)) / 4.0
    ii = -_m.atan2(-2.0 * im, (1.0 - re) * (1.0 + re) - ay * ay) / 2.0
    return complex(rr, ii)


def atan(z):
    z = _c(z)
    s = atanh(complex(-z.imag, z.real))
    return complex(s.imag, -s.real)
