"""scipy.special subset: gamma/beta family, error functions, incomplete gamma/beta, logistic, softmax ..."""
import math
import numpy as np
from ._util import vmap, vmap2, vmap3, tolist, inf, nan, reduce_axis

_SQRT2 = math.sqrt(2.0)
_SQRT2PI = math.sqrt(2.0 * math.pi)


# ---- gamma family ---------------------------------------------------------

def _gamma(x):
    x = float(x)
    if x != x:
        return nan
    if x == 0.0:
        return inf
    if x == inf:
        return inf
    if x == -inf:
        return nan
    if x < 0 and x == math.floor(x):
        return nan
    if x > 171.7:
        return inf
    return math.gamma(x)


def gamma(z):
    return vmap(_gamma, z)


def _gammaln(x):
    x = float(x)
    if x != x:
        return nan
    if x <= 0 and x == math.floor(x):
        return inf
    if x == inf or x == -inf:
        return inf
    return math.lgamma(x)


def gammaln(x):
    return vmap(_gammaln, x)


def _gammasgn(x):
    x = float(x)
    if x > 0 or x == 0:
        return 1.0
    if x == math.floor(x):
        return 0.0
    return 1.0 if int(math.floor(x)) % 2 == 0 else -1.0


def gammasgn(x):
    return vmap(_gammasgn, x)


def loggamma(z):
    return gammaln(z)


def rgamma(z):
    return vmap(lambda v: 0.0 if (v <= 0 and v == math.floor(v)) else 1.0 / _gamma(v), z)


def _digamma(x):
    x = float(x)
    if x != x:
        return nan
    if x == 0.0:
        return -inf
    if x < 0:
        if x == math.floor(x):
            return nan
        return _digamma(1.0 - x) - math.pi / math.tan(math.pi * x)
    r = 0.0
    while x < 10.0:
        r -= 1.0 / x
        x += 1.0
    f = 1.0 / (x * x)
    t = f * (-1.0 / 12.0 + f * (1.0 / 120.0 + f * (-1.0 / 252.0 + f * (1.0 / 240.0 + f * (-1.0 / 132.0 + f * (
        691.0 / 32760.0 + f * (-1.0 / 12.0 + f * 3617.0 / 8160.0)))))))
    return r + math.log(x) - 0.5 / x + t


def digamma(x):
    return vmap(_digamma, x)


psi = digamma


def _betaln(a, b):
    return _gammaln(a) + _gammaln(b) - _gammaln(a + b)


def betaln(a, b):
    return vmap2(_betaln, a, b)


def _beta(a, b):
    a = float(a)
    b = float(b)
    if a + b < 170.0 and a > 0 and b > 0:
        return math.gamma(a) * math.gamma(b) / math.gamma(a + b)
    return math.exp(_betaln(a, b))


def beta(a, b):
    return vmap2(_beta, a, b)


def _comb(n, k, rep):
    if rep:
        return _comb(n + k - 1, k, False)
    n = float(n)
    k = float(k)
    if n != n or k != k:
        return nan
    if k < 0 or n < 0:
        return 0.0
    if n == math.floor(n) and k == math.floor(k):
        if k > n:
            return 0.0
        return float(math.comb(int(n), int(k)))
    if k > n:
        return 0.0
    return math.exp(_gammaln(n + 1) - _gammaln(k + 1) - _gammaln(n - k + 1))


def comb(N, k, exact=False, repetition=False):
    if exact:
        n = int(N)
        k = int(k)
        if repetition:
            n = n + k - 1
        if k < 0 or n < 0 or k > n:
            return 0
        return math.comb(n, k)
    return vmap2(lambda a, b: _comb(a, b, repetition), N, k)


def _perm(n, k):
    n = float(n)
    k = float(k)
    if k < 0 or n < 0 or k > n:
        return 0.0
    if n == math.floor(n) and k == math.floor(k):
        return float(math.perm(int(n), int(k)))
    return math.exp(_gammaln(n + 1) - _gammaln(n - k + 1))


def perm(N, k, exact=False):
    if exact:
        n = int(N)
        k = int(k)
        if k < 0 or k > n:
            return 0
        return math.perm(n, k)
    return vmap2(_perm, N, k)


def _fact(n):
    n = float(n)
    if n != n:
        return nan
    if n < 0:
        return 0.0
    if n != math.floor(n):
        return _gamma(n + 1.0)
    return float(math.factorial(int(n)))


def factorial(n, exact=False):
    if exact:
        if isinstance(n, (np.ndarray, list, tuple)):
            return vmap(lambda v: math.factorial(int(v)), n)
        n = int(n)
        if n < 0:
            return 0
        return math.factorial(n)
    return vmap(_fact, n)


def factorial2(n, exact=False):
    def f(v):
        v = int(v)
        if v < 0:
            return 0
        r = 1
        while v > 1:
            r *= v
            v -= 2
        return r if exact else float(r)
    return vmap(f, n)


def binom(n, k):
    def f(a, b):
        a = float(a)
        b = float(b)
        if a == math.floor(a) and b == math.floor(b):
            if a >= 0:
                return _comb(a, b, False)
            kk = int(b)
            if kk < 0:
                return 0.0
            r = 1.0
            for i in range(kk):
                r = r * (a - i) / (i + 1)
            return r
        if b < 0:
            return 0.0
        if b == math.floor(b) and b < 200:
            r = 1.0
            for i in range(int(b)):
                r = r * (a - i) / (i + 1)
            return r
        return 1.0 / ((a + 1.0) * _beta(a - b + 1.0, b + 1.0))
    return vmap2(f, n, k)


# ---- error function family ------------------------------------------------

def _erf(x):
    x = float(x)
    if x != x:
        return nan
    return math.erf(x)


def erf(x):
    return vmap(_erf, x)


def _erfc(x):
    x = float(x)
    if x != x:
        return nan
    return math.erfc(x)


def erfc(x):
    return vmap(_erfc, x)


def _erfcx_cf(x):
    tiny = 1e-300
    f = x
    c = x
    d = 0.0
    for n in range(1, 300):
        an = n / 2.0
        d = x + an * d
        if d == 0.0:
            d = tiny
        c = x + an / c
        if c == 0.0:
            c = tiny
        d = 1.0 / d
        delta = c * d
        f = f * delta
        if abs(delta - 1.0) < 1e-16:
            break
    return 1.0 / (math.sqrt(math.pi) * f)


def _erfcx(x):
    x = float(x)
    if x < 5.0:
        return math.exp(x * x) * math.erfc(x)
    return _erfcx_cf(x)


def erfcx(x):
    return vmap(_erfcx, x)


def _ndtri(p):
    p = float(p)
    if p != p or p < 0.0 or p > 1.0:
        return nan
    if p == 0.0:
        return -inf
    if p == 1.0:
        return inf
    if p > 0.5:
        return -_ndtri(1.0 - p)
    a = [-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
         1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00]
    b = [-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
         6.680131188771972e+01, -1.328068155288572e+01]
    c = [-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
         -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00]
    d = [7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
         3.754408661907416e+00]
    if p < 0.02425:
        q = math.sqrt(-2.0 * math.log(p))
        x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / \
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0)
    else:
        q = p - 0.5
        r = q * q
        x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q / \
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0)
    for i in range(2):
        e = 0.5 * math.erfc(-x / _SQRT2) - p
        u = e * _SQRT2PI * math.exp(x * x / 2.0)
        x = x - u / (1.0 + x * u / 2.0)
    return x


def ndtri(y):
    return vmap(_ndtri, y)


def _ndtr(x):
    x = float(x)
    if x != x:
        return nan
    return 0.5 * math.erfc(-x / _SQRT2)


def ndtr(x):
    return vmap(_ndtr, x)


def _log_ndtr(x):
    x = float(x)
    if x > 6.0:
        return -_ndtr(-x)
    if x > -20.0:
        return math.log(0.5 * math.erfc(-x / _SQRT2))
    x2 = x * x
    s = 1.0 - 1.0 / x2 + 3.0 / (x2 * x2) - 15.0 / (x2 * x2 * x2) + 105.0 / (x2 * x2 * x2 * x2)
    return -0.5 * x2 - math.log(-x) - 0.5 * math.log(2.0 * math.pi) + math.log(s)


def log_ndtr(x):
    return vmap(_log_ndtr, x)


def _erfinv(y):
    y = float(y)
    if y != y or y < -1.0 or y > 1.0:
        return nan
    if y == 1.0:
        return inf
    if y == -1.0:
        return -inf
    if y == 0.0:
        return y
    if y < 0:
        return -_erfinv(-y)
    if y < 0.5:
        x = math.sqrt(math.pi) / 2.0 * (y + math.pi * y ** 3 / 12.0 + 7.0 * math.pi ** 2 * y ** 5 / 480.0)
        for i in range(8):
            dx = (math.erf(x) - y) / (2.0 / math.sqrt(math.pi) * math.exp(-x * x))
            x = x - dx
            if abs(dx) < 1e-16 * abs(x):
                break
        return x
    return _erfcinv(1.0 - y)


def _erfcinv(q):
    q = float(q)
    if q != q or q < 0.0 or q > 2.0:
        return nan
    if q == 0.0:
        return inf
    if q == 2.0:
        return -inf
    if q > 1.0:
        return -_erfcinv(2.0 - q)
    if q > 0.5:
        return _erfinv(1.0 - q)
    x = -_ndtri(q / 2.0) / _SQRT2
    for i in range(4):
        dx = (math.erfc(x) - q) / (-2.0 / math.sqrt(math.pi) * math.exp(-x * x))
        x = x - dx
        if abs(dx) < 1e-16 * abs(x):
            break
    return x


def erfinv(y):
    return vmap(_erfinv, y)


def erfcinv(y):
    return vmap(_erfcinv, y)


# ---- incomplete gamma / beta ----------------------------------------------

def _gser(a, x):
    ap = a
    s = 1.0 / a
    de = s
    for n in range(1, 5000):
        ap += 1.0
        de = de * x / ap
        s += de
        if abs(de) < abs(s) * 1e-17:
            break
    return s * math.exp(-x + a * math.log(x) - _gammaln(a))


def _gcf(a, x):
    tiny = 1e-300
    b = x + 1.0 - a
    c = 1.0 / tiny
    d = 1.0 / b
    h = d
    for i in range(1, 5000):
        an = -i * (i - a)
        b += 2.0
        d = an * d + b
        if abs(d) < tiny:
            d = tiny
        c = b + an / c
        if abs(c) < tiny:
            c = tiny
        d = 1.0 / d
        de = d * c
        h = h * de
        if abs(de - 1.0) < 1e-16:
            break
    return math.exp(-x + a * math.log(x) - _gammaln(a)) * h


def _gammainc(a, x):
    a = float(a)
    x = float(x)
    if a != a or x != x or a < 0 or x < 0:
        return nan
    if a == 0.0:
        return 1.0 if x > 0 else nan
    if x == 0.0:
        return 0.0
    if x == inf:
        return 1.0
    if x < a + 1.0:
        return _gser(a, x)
    return 1.0 - _gcf(a, x)


def _gammaincc(a, x):
    a = float(a)
    x = float(x)
    if a != a or x != x or a < 0 or x < 0:
        return nan
    if a == 0.0:
        return 0.0 if x > 0 else nan
    if x == 0.0:
        return 1.0
    if x == inf:
        return 0.0
    if x < a + 1.0:
        return 1.0 - _gser(a, x)
    return _gcf(a, x)


def gammainc(a, x):
    return vmap2(_gammainc, a, x)


def gammaincc(a, x):
    return vmap2(_gammaincc, a, x)


def _betacf(a, b, x):
    tiny = 1e-300
    qab = a + b
    qap = a + 1.0
    qam = a - 1.0
    c = 1.0
    d = 1.0 - qab * x / qap
    if abs(d) < tiny:
        d = tiny
    d = 1.0 / d
    h = d
    for m in range(1, 5000):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d
        if abs(d) < tiny:
            d = tiny
        c = 1.0 + aa / c
        if abs(c) < tiny:
            c = tiny
        d = 1.0 / d
        h = h * d * c
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d
        if abs(d) < tiny:
            d = tiny
        c = 1.0 + aa / c
        if abs(c) < tiny:
            c = tiny
        d = 1.0 / d
        de = d * c
        h = h * de
        if abs(de - 1.0) < 1e-16:
            break
    return h


def _betainc(a, b, x):
    a = float(a)
    b = float(b)
    x = float(x)
    if a != a or b != b or x != x or a < 0 or b < 0 or x < 0 or x > 1:
        return nan
    if x == 0.0:
        return 0.0
    if x == 1.0:
        return 1.0
    if a == 0.0:
        return 1.0
    if b == 0.0:
        return 0.0
    lbt = _gammaln(a + b) - _gammaln(a) - _gammaln(b) + a * math.log(x) + b * math.log1p(-x)
    bt = math.exp(lbt)
    if x < (a + 1.0) / (a + b + 2.0):
        return bt * _betacf(a, b, x) / a
    return 1.0 - bt * _betacf(b, a, 1.0 - x) / b


def betainc(a, b, x):
    return vmap3(_betainc, a, b, x)


def solve_monotone(cdf, pdf, p, lo, hi, x0):
    """x with cdf(x) = p for an increasing function on [lo, hi] (bounds may be infinite)."""
    a = lo
    b = hi
    x = x0
    if not (x > a and x < b):
        if a > -inf and b < inf:
            x = 0.5 * (a + b)
        elif a > -inf:
            x = a + 1.0
        elif b < inf:
            x = b - 1.0
        else:
            x = 0.0
    step = max(1.0, abs(x)) * 0.5
    if cdf(x) < p:
        a = x
        while True:
            nx = x + step
            if nx >= b:
                if b < inf:
                    break
            elif cdf(nx) >= p:
                b = nx
                break
            else:
                a = nx
                x = nx
            step = step * 2.0
            if step > 1e300:
                return nan
    else:
        b = x
        while True:
            nx = x - step
            if nx <= a:
                if a > -inf:
                    break
            elif cdf(nx) < p:
                a = nx
                break
            else:
                b = nx
                x = nx
            step = step * 2.0
            if step > 1e300:
                return nan
    x = x0 if (x0 > a and x0 < b) else 0.5 * (a + b)
    for it in range(300):
        fx = cdf(x) - p
        if fx == 0.0:
            return x
        if fx > 0:
            b = x
        else:
            a = x
        d = pdf(x)
        xn = nan
        if d > 0 and d < inf:
            xn = x - fx / d
        if not (xn > a and xn < b):
            xn = 0.5 * (a + b)
        if abs(xn - x) <= 4e-16 * abs(xn) + 1e-300:
            return xn
        x = xn
        if b - a <= 4e-16 * abs(x):
            return x
    return x


def _gammaincinv(a, y):
    a = float(a)
    y = float(y)
    if y != y or a <= 0 or y < 0 or y > 1:
        return nan
    if y == 0.0:
        return 0.0
    if y == 1.0:
        return inf
    z = _ndtri(y)
    t = 1.0 - 1.0 / (9.0 * a) + z / (3.0 * math.sqrt(a))
    x0 = a * t * t * t
    if x0 <= 0 or x0 != x0:
        x0 = math.exp((math.log(y) + _gammaln(a + 1.0)) / a)
    lg = _gammaln(a)
    return solve_monotone(lambda v: _gammainc(a, v),
                          lambda v: math.exp((a - 1.0) * math.log(v) - v - lg) if v > 0 else 0.0,
                          y, 0.0, inf, x0)


def gammaincinv(a, y):
    return vmap2(_gammaincinv, a, y)


def gammainccinv(a, y):
    return vmap2(lambda p, q: _gammaincinv(p, 1.0 - q), a, y)


def _betaincinv(a, b, y):
    a = float(a)
    b = float(b)
    y = float(y)
    if y != y or a <= 0 or b <= 0 or y < 0 or y > 1:
        return nan
    if y == 0.0:
        return 0.0
    if y == 1.0:
        return 1.0
    lb = _betaln(a, b)
    x0 = a / (a + b)
    return solve_monotone(lambda v: _betainc(a, b, v),
                          lambda v: math.exp((a - 1.0) * math.log(v) + (b - 1.0) * math.log1p(-v) - lb)
                          if 0 < v < 1 else 0.0,
                          y, 0.0, 1.0, x0)


def betaincinv(a, b, y):
    return vmap3(_betaincinv, a, b, y)


# ---- logistic, entropy, misc ----------------------------------------------

def _expit(x):
    x = float(x)
    if x >= 0:
        return 1.0 / (1.0 + math.exp(-x))
    e = math.exp(x)
    return e / (1.0 + e)


def expit(x):
    return vmap(_expit, x)


def _logit(p):
    p = float(p)
    if p < 0 or p > 1 or p != p:
        return nan
    if p == 0.0:
        return -inf
    if p == 1.0:
        return inf
    return math.log(p / (1.0 - p))


def logit(x):
    return vmap(_logit, x)


def _xlogy(x, y):
    if x == 0 and y == y:
        return 0.0
    if y > 0:
        return x * math.log(y)
    if y == 0:
        return -inf if x > 0 else inf
    return nan


def xlogy(x, y):
    return vmap2(lambda a, b: _xlogy(float(a), float(b)), x, y)


def xlog1py(x, y):
    return vmap2(lambda a, b: 0.0 if (a == 0 and b == b) else float(a) * math.log1p(float(b)), x, y)


def entr(x):
    def f(v):
        v = float(v)
        if v != v:
            return nan
        if v > 0:
            return -v * math.log(v)
        if v == 0:
            return 0.0
        return -inf
    return vmap(f, x)


def rel_entr(x, y):
    def f(a, b):
        a = float(a)
        b = float(b)
        if a > 0 and b > 0:
            return a * math.log(a / b)
        if a == 0 and b >= 0:
            return 0.0
        return inf
    return vmap2(f, x, y)


def kl_div(x, y):
    def f(a, b):
        a = float(a)
        b = float(b)
        if a > 0 and b > 0:
            return a * math.log(a / b) - a + b
        if a == 0 and b >= 0:
            return b
        return inf
    return vmap2(f, x, y)


def _lse(v):
    if not v:
        return -inf
    m = max(v)
    if m == inf or m == -inf:
        return m
    s = 0.0
    for t in v:
        s += math.exp(t - m)
    return m + math.log(s)


def logsumexp(a, axis=None, b=None, keepdims=False, return_sign=False):
    a = np.asarray(a)
    if b is not None:
        if axis is not None:
            raise NotImplementedError("logsumexp: b with axis")
        vals = tolist(a)
        ws = tolist(np.asarray(b) + a * 0)
        m = max(vals)
        s = 0.0
        for t, w in zip(vals, ws):
            s += w * math.exp(t - m)
        if return_sign:
            return (math.log(abs(s)) + m, 1.0 if s >= 0 else -1.0)
        return math.log(s) + m if s > 0 else nan
    if axis is None:
        r = _lse(tolist(a))
        if keepdims:
            r = np.array([r]).reshape([1] * a.ndim)
        return (r, 1.0) if return_sign else r
    r = reduce_axis(_lse, a, axis)
    if keepdims:
        sh = list(a.shape)
        sh[axis] = 1
        r = np.asarray(r).reshape(sh)
    if return_sign:
        return (r, np.ones_like(r) if hasattr(r, "shape") else 1.0)
    return r


def _softmax_list(v):
    m = max(v)
    e = [math.exp(t - m) for t in v]
    s = 0.0
    for t in e:
        s += t
    return [t / s for t in e]


def softmax(x, axis=None):
    from ._util import map_axis
    a = np.asarray(x) * 1.0
    if axis is None or a.ndim <= 1:
        return np.array(_softmax_list(tolist(a))).reshape(a.shape)
    return map_axis(_softmax_list, a, axis)


def log_softmax(x, axis=None):
    from ._util import map_axis
    a = np.asarray(x) * 1.0

    def f(v):
        l = _lse(v)
        return [t - l for t in v]
    if axis is None or a.ndim <= 1:
        return np.array(f(tolist(a))).reshape(a.shape)
    return map_axis(f, a, axis)


def cbrt(x):
    return vmap(lambda v: math.copysign(abs(float(v)) ** (1.0 / 3.0), v), x)


def exp2(x):
    return vmap(lambda v: 2.0 ** float(v), x)


def exp10(x):
    return vmap(lambda v: 10.0 ** float(v), x)


def expm1(x):
    return vmap(lambda v: math.expm1(float(v)), x)


def log1p(x):
    return vmap(lambda v: math.log1p(float(v)), x)


def boxcox(x, lmbda):
    return vmap2(lambda v, l: math.log(v) if l == 0 else (v ** l - 1.0) / l, x, lmbda)


def inv_boxcox(y, lmbda):
    return vmap2(lambda v, l: math.exp(v) if l == 0 else (l * v + 1.0) ** (1.0 / l), y, lmbda)


def sindg(x):
    return vmap(lambda v: math.sin(math.radians(v)), x)


def cosdg(x):
    return vmap(lambda v: math.cos(math.radians(v)), x)


def _agm(a, b):
    for i in range(60):
        an = 0.5 * (a + b)
        b = math.sqrt(a * b)
        a = an
        if abs(a - b) < 1e-16 * a:
            break
    return a


def ellipk(m):
    def f(v):
        v = float(v)
        if v > 1:
            return nan
        if v == 1:
            return inf
        return math.pi / (2.0 * _agm(1.0, math.sqrt(1.0 - v)))
    return vmap(f, m)


def ellipe(m):
    def f(m):
        m = float(m)
        if m > 1:
            return nan
        if m == 1:
            return 1.0
        n = 400
        h = (math.pi / 2.0) / n
        t = 0.0
        for i in range(n + 1):
            v = math.sqrt(1.0 - m * math.sin(i * h) ** 2)
            w = 1.0 if (i == 0 or i == n) else (4.0 if i % 2 == 1 else 2.0)
            t += w * v
        return t * h / 3.0
    return vmap(f, m)


def roots_legendre(n):
    """nodes and weights of the n-point Gauss-Legendre rule on [-1, 1]"""
    n = int(n)
    xs = [0.0] * n
    ws = [0.0] * n
    for i in range((n + 1) // 2):
        x = math.cos(math.pi * (i + 0.75) / (n + 0.5))
        for it in range(100):
            p0 = 1.0
            p1 = x
            for k in range(2, n + 1):
                p2 = ((2.0 * k - 1.0) * x * p1 - (k - 1.0) * p0) / k
                p0 = p1
                p1 = p2
            dp = n * (x * p1 - p0) / (x * x - 1.0)
            dx = p1 / dp
            x = x - dx
            if abs(dx) < 1e-16:
                break
        p0 = 1.0
        p1 = x
        for k in range(2, n + 1):
            p2 = ((2.0 * k - 1.0) * x * p1 - (k - 1.0) * p0) / k
            p0 = p1
            p1 = p2
        dp = n * (x * p1 - p0) / (x * x - 1.0)
        w = 2.0 / ((1.0 - x * x) * dp * dp)
        xs[i] = -x
        xs[n - 1 - i] = x
        ws[i] = w
        ws[n - 1 - i] = w
    if n % 2 == 1:
        xs[n // 2] = 0.0
    return np.array(xs), np.array(ws)
