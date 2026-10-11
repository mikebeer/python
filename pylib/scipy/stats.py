"""scipy.stats subset: distributions (norm t chi2 f uniform expon gamma beta lognorm laplace cauchy logistic
weibull_min binom poisson bernoulli geom randint) and descriptive / hypothesis test functions."""
import math
import numpy as np
from ._util import (vmap, tolist, inf, nan, reduce_axis, map_axis, slices, Result)
from . import special as _sp

_ndtr = _sp._ndtr
_ndtri = _sp._ndtri
_betainc = _sp._betainc
_gammainc = _sp._gammainc
_gammaincc = _sp._gammaincc
_gammaln = _sp._gammaln
_LOG2PI = math.log(2.0 * math.pi)


# ---------------------------------------------------------------------------
# distributions
# ---------------------------------------------------------------------------

def _get_rng(rs):
    if rs is None:
        return np.random
    if isinstance(rs, int):
        return np.random.RandomState(rs)
    return rs


def _uniform_draw(rng, n):
    if hasattr(rng, "random_sample"):
        v = rng.random_sample(n)
    else:
        v = rng.random(n)
    return tolist(v)


def _shape_of(size):
    if size is None:
        return None
    if isinstance(size, int):
        return (size,)
    return tuple(size)


def _count(shape):
    n = 1
    for d in shape:
        n *= d
    return n


class _Frozen:
    def __init__(self, dist, args, kw):
        self.dist = dist
        self.args = args
        self.kwds = kw

    def pdf(self, x):
        return self.dist.pdf(x, *self.args, **self.kwds)

    def logpdf(self, x):
        return self.dist.logpdf(x, *self.args, **self.kwds)

    def pmf(self, x):
        return self.dist.pmf(x, *self.args, **self.kwds)

    def logpmf(self, x):
        return self.dist.logpmf(x, *self.args, **self.kwds)

    def cdf(self, x):
        return self.dist.cdf(x, *self.args, **self.kwds)

    def logcdf(self, x):
        return self.dist.logcdf(x, *self.args, **self.kwds)

    def sf(self, x):
        return self.dist.sf(x, *self.args, **self.kwds)

    def logsf(self, x):
        return self.dist.logsf(x, *self.args, **self.kwds)

    def ppf(self, q):
        return self.dist.ppf(q, *self.args, **self.kwds)

    def isf(self, q):
        return self.dist.isf(q, *self.args, **self.kwds)

    def rvs(self, size=None, random_state=None):
        return self.dist.rvs(*self.args, size=size, random_state=random_state, **self.kwds)

    def mean(self):
        return self.dist.mean(*self.args, **self.kwds)

    def var(self):
        return self.dist.var(*self.args, **self.kwds)

    def std(self):
        return self.dist.std(*self.args, **self.kwds)

    def median(self):
        return self.dist.median(*self.args, **self.kwds)

    def stats(self, moments="mv"):
        return self.dist.stats(*self.args, moments=moments, **self.kwds)

    def interval(self, confidence):
        return self.dist.interval(confidence, *self.args, **self.kwds)

    def support(self):
        return self.dist.support(*self.args, **self.kwds)

    def entropy(self):
        return self.dist.entropy(*self.args, **self.kwds)


class _Dist:
    name = "dist"
    shapes = ()
    discrete = False

    def __call__(self, *args, **kw):
        return _Frozen(self, args, kw)

    # ---- parameter handling ----
    def _prep(self, args, kw):
        n = len(self.shapes)
        args = list(args)
        s = []
        for i in range(n):
            if i < len(args):
                s.append(args[i])
            else:
                s.append(kw[self.shapes[i]])
        rest = args[n:]
        loc = rest[0] if len(rest) > 0 else kw.get("loc", 0)
        scale = rest[1] if len(rest) > 1 else kw.get("scale", 1)
        if self.discrete:
            scale = 1
        return s, float(loc), float(scale)

    def _argcheck(self, *s):
        return True

    def _support(self, *s):
        return (-inf, inf)

    def support(self, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        a, b = self._support(*s)
        return (a * scale + loc, b * scale + loc)

    # ---- generic defaults built from _cdf/_sf/_pdf/_ppf ----
    def _sf(self, x, *s):
        return 1.0 - self._cdf(x, *s)

    def _logpdf(self, x, *s):
        p = self._pdf(x, *s)
        return math.log(p) if p > 0 else -inf

    def _isf(self, q, *s):
        return self._ppf(1.0 - q, *s)

    def _pmf(self, k, *s):
        return self._pdf(k, *s)

    # ---- public API ----
    def pdf(self, x, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        ok = self._argcheck(*s) and scale > 0
        a, b = self._support(*s)

        def f(v):
            v = float(v)
            if v != v or not ok:
                return nan
            z = (v - loc) / scale
            if z < a or z > b:
                return 0.0
            return self._pdf(z, *s) / scale
        return vmap(f, x)

    def logpdf(self, x, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        ok = self._argcheck(*s) and scale > 0
        a, b = self._support(*s)

        def f(v):
            v = float(v)
            if v != v or not ok:
                return nan
            z = (v - loc) / scale
            if z < a or z > b:
                return -inf
            return self._logpdf(z, *s) - math.log(scale)
        return vmap(f, x)

    def pmf(self, x, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        ok = self._argcheck(*s)
        a, b = self._support(*s)

        def f(v):
            v = float(v)
            if v != v or not ok:
                return nan
            k = v - loc
            if k != math.floor(k) or k < a or k > b:
                return 0.0
            return self._pmf(k, *s)
        return vmap(f, x)

    def logpmf(self, x, *args, **kw):
        r = self.pmf(x, *args, **kw)
        return vmap(lambda p: math.log(p) if p > 0 else (nan if p != p else -inf), r)

    def cdf(self, x, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        ok = self._argcheck(*s) and scale > 0
        a, b = self._support(*s)
        disc = self.discrete

        def f(v):
            v = float(v)
            if v != v or not ok:
                return nan
            z = (v - loc) / scale
            if disc:
                z = math.floor(z) if z != inf and z != -inf else z
            if z < a:
                return 0.0
            if z >= b:
                return 1.0
            return self._cdf(z, *s)
        return vmap(f, x)

    def logcdf(self, x, *args, **kw):
        r = self.cdf(x, *args, **kw)
        return vmap(lambda p: math.log(p) if p > 0 else (nan if p != p else -inf), r)

    def sf(self, x, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        ok = self._argcheck(*s) and scale > 0
        a, b = self._support(*s)
        disc = self.discrete

        def f(v):
            v = float(v)
            if v != v or not ok:
                return nan
            z = (v - loc) / scale
            if disc:
                z = math.floor(z) if z != inf and z != -inf else z
            if z < a:
                return 1.0
            if z >= b:
                return 0.0
            return self._sf(z, *s)
        return vmap(f, x)

    def logsf(self, x, *args, **kw):
        r = self.sf(x, *args, **kw)
        return vmap(lambda p: math.log(p) if p > 0 else (nan if p != p else -inf), r)

    def _ppf_discrete(self, q, s):
        a, b = self._support(*s)
        lo = a - 1.0
        if b < inf:
            hi = b
        else:
            m = self._stats(*s)
            hi = max(a + 1.0, math.floor(m[0] + 10.0 * math.sqrt(m[1]) + 10.0))
            while self._cdf(hi, *s) < q:
                hi = hi * 2.0
        # smallest k in (lo, hi] with cdf(k) >= q
        while hi - lo > 1.0:
            mid = math.floor((lo + hi) / 2.0)
            if self._cdf(mid, *s) >= q:
                hi = mid
            else:
                lo = mid
        return hi

    def ppf(self, q, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        ok = self._argcheck(*s) and scale > 0
        a, b = self._support(*s)
        disc = self.discrete

        def f(v):
            v = float(v)
            if v != v or not ok or v < 0 or v > 1:
                return nan
            if disc:
                if v == 0.0:
                    return a - 1.0 + loc
                if v == 1.0:
                    return b + loc
                return self._ppf_discrete(v, s) + loc
            if v == 0.0:
                return a * scale + loc
            if v == 1.0:
                return b * scale + loc
            return self._ppf(v, *s) * scale + loc
        return vmap(f, q)

    def isf(self, q, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        ok = self._argcheck(*s) and scale > 0
        a, b = self._support(*s)
        disc = self.discrete

        def f(v):
            v = float(v)
            if v != v or not ok or v < 0 or v > 1:
                return nan
            if disc:
                if v == 1.0:
                    return a - 1.0 + loc
                if v == 0.0:
                    return b + loc
                return self._ppf_discrete(1.0 - v, s) + loc
            if v == 1.0:
                return a * scale + loc
            if v == 0.0:
                return b * scale + loc
            return self._isf(v, *s) * scale + loc
        return vmap(f, q)

    def stats(self, *args, **kw):
        moments = kw.pop("moments", "mv")
        s, loc, scale = self._prep(args, kw)
        if not self._argcheck(*s) or not scale > 0:
            return tuple([nan] * len(moments)) if len(moments) > 1 else nan
        m, v, sk, ku = self._stats(*s)
        out = []
        for c in moments:
            if c == "m":
                out.append(m * scale + loc)
            elif c == "v":
                out.append(v * scale * scale)
            elif c == "s":
                out.append(sk)
            elif c == "k":
                out.append(ku)
        return out[0] if len(out) == 1 else tuple(out)

    def mean(self, *args, **kw):
        return self.stats(*args, moments="m", **kw)

    def var(self, *args, **kw):
        return self.stats(*args, moments="v", **kw)

    def std(self, *args, **kw):
        v = self.stats(*args, moments="v", **kw)
        return math.sqrt(v) if v == v else nan

    def median(self, *args, **kw):
        return self.ppf(0.5, *args, **kw)

    def interval(self, confidence, *args, **kw):
        a = (1.0 - confidence) / 2.0
        return (self.ppf(a, *args, **kw), self.ppf(1.0 - a, *args, **kw))

    def entropy(self, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        return self._entropy(*s) + math.log(scale)

    def _std_draws(self, rng, n, s):
        """n standard variates (list); default is inverse transform sampling"""
        out = []
        for u in _uniform_draw(rng, n):
            if self.discrete:
                out.append(self._ppf_discrete(min(max(u, 1e-300), 1.0 - 1e-16), s))
            else:
                out.append(self._ppf(min(max(u, 1e-300), 1.0 - 1e-16), *s))
        return out

    def rvs(self, *args, **kw):
        size = kw.pop("size", None)
        rs = kw.pop("random_state", None)
        s, loc, scale = self._prep(args, kw)
        if not self._argcheck(*s) or not scale > 0:
            raise ValueError("Domain error in arguments.")
        shape = _shape_of(size)
        n = 1 if shape is None else _count(shape)
        d = self._std_draws(_get_rng(rs), n, s)
        out = [v * scale + loc for v in d]
        if shape is None:
            return out[0]
        return np.array(out).reshape(shape)

    def fit(self, data, *args, **kw):
        raise NotImplementedError(self.name + ".fit is not implemented")


class _norm(_Dist):
    name = "norm"

    def _pdf(self, x):
        return math.exp(-x * x / 2.0) / math.sqrt(2.0 * math.pi)

    def _logpdf(self, x):
        return -x * x / 2.0 - 0.5 * _LOG2PI

    def _cdf(self, x):
        return _ndtr(x)

    def _sf(self, x):
        return _ndtr(-x)

    def _ppf(self, q):
        return _ndtri(q)

    def _isf(self, q):
        return -_ndtri(q)

    def _stats(self):
        return (0.0, 1.0, 0.0, 0.0)

    def _entropy(self):
        return 0.5 * (math.log(2.0 * math.pi) + 1.0)

    def _std_draws(self, rng, n, s):
        return tolist(rng.standard_normal(n)) if hasattr(rng, "standard_normal") else \
            tolist(rng.randn(n))

    def logcdf(self, x, *args, **kw):
        s, loc, scale = self._prep(args, kw)
        return vmap(lambda v: _sp._log_ndtr((float(v) - loc) / scale), x)

    def fit(self, data, *args, **kw):
        d = tolist(data)
        n = len(d)
        m = sum(d) / n
        v = sum([(t - m) ** 2 for t in d]) / n
        return (m, math.sqrt(v))


class _t(_Dist):
    name = "t"
    shapes = ("df",)

    def _argcheck(self, df):
        return df > 0

    def _pdf(self, x, df):
        return math.exp(self._logpdf(x, df))

    def _logpdf(self, x, df):
        return (_gammaln((df + 1.0) / 2.0) - _gammaln(df / 2.0) - 0.5 * math.log(df * math.pi)
                - (df + 1.0) / 2.0 * math.log1p(x * x / df))

    def _cdf(self, x, df):
        if x == 0.0:
            return 0.5
        if x == inf:
            return 1.0
        if x == -inf:
            return 0.0
        z = df / (df + x * x)
        p = 0.5 * _betainc(df / 2.0, 0.5, z)
        if df > 1e15:
            return _ndtr(x)
        return 1.0 - p if x > 0 else p

    def _sf(self, x, df):
        return self._cdf(-x, df)

    def _ppf(self, q, df):
        if q == 0.5:
            return 0.0
        if q > 0.5:
            return -self._ppf(1.0 - q, df)
        if df == 1.0:
            return math.tan(math.pi * (q - 0.5))
        z = _ndtri(q)
        g1 = (z ** 3 + z) / 4.0
        g2 = (5.0 * z ** 5 + 16.0 * z ** 3 + 3.0 * z) / 96.0
        x0 = z + g1 / df + g2 / (df * df)
        if x0 >= 0:
            x0 = -1.0
        return _sp.solve_monotone(lambda v: self._cdf(v, df), lambda v: self._pdf(v, df), q,
                                  -inf, 0.0, x0)

    def _stats(self, df):
        m = 0.0 if df > 1 else inf
        v = df / (df - 2.0) if df > 2 else (inf if df > 1 else nan)
        sk = 0.0 if df > 3 else nan
        ku = 6.0 / (df - 4.0) if df > 4 else (inf if df > 2 else nan)
        return (m, v, sk, ku)

    def _entropy(self, df):
        h = (df + 1.0) / 2.0 * (_sp._digamma((df + 1.0) / 2.0) - _sp._digamma(df / 2.0)) + \
            0.5 * math.log(df) + _sp._betaln(df / 2.0, 0.5)
        return h


class _chi2(_Dist):
    name = "chi2"
    shapes = ("df",)

    def _argcheck(self, df):
        return df > 0

    def _support(self, df):
        return (0.0, inf)

    def _pdf(self, x, df):
        if x == 0.0:
            return inf if df < 2 else (0.5 if df == 2 else 0.0)
        return math.exp((df / 2.0 - 1.0) * math.log(x) - x / 2.0 - (df / 2.0) * math.log(2.0)
                        - _gammaln(df / 2.0))

    def _cdf(self, x, df):
        return _gammainc(df / 2.0, x / 2.0)

    def _sf(self, x, df):
        return _gammaincc(df / 2.0, x / 2.0)

    def _ppf(self, q, df):
        return 2.0 * _sp._gammaincinv(df / 2.0, q)

    def _isf(self, q, df):
        return 2.0 * _sp._gammaincinv(df / 2.0, 1.0 - q)

    def _stats(self, df):
        return (df, 2.0 * df, math.sqrt(8.0 / df), 12.0 / df)

    def _std_draws(self, rng, n, s):
        if hasattr(rng, "chisquare"):
            return tolist(rng.chisquare(s[0], n))
        return _Dist._std_draws(self, rng, n, s)


class _f(_Dist):
    name = "f"
    shapes = ("dfn", "dfd")

    def _argcheck(self, dfn, dfd):
        return dfn > 0 and dfd > 0

    def _support(self, dfn, dfd):
        return (0.0, inf)

    def _pdf(self, x, dfn, dfd):
        if x == 0.0:
            return inf if dfn < 2 else (1.0 if dfn == 2 else 0.0)
        return math.exp(0.5 * (dfn * math.log(dfn) + dfd * math.log(dfd)) + (dfn / 2.0 - 1.0) * math.log(x)
                        - (dfn + dfd) / 2.0 * math.log(dfd + dfn * x) - _sp._betaln(dfn / 2.0, dfd / 2.0))

    def _cdf(self, x, dfn, dfd):
        return _betainc(dfn / 2.0, dfd / 2.0, dfn * x / (dfn * x + dfd))

    def _sf(self, x, dfn, dfd):
        return _betainc(dfd / 2.0, dfn / 2.0, dfd / (dfd + dfn * x))

    def _ppf(self, q, dfn, dfd):
        y = _sp._betaincinv(dfn / 2.0, dfd / 2.0, q)
        return dfd * y / (dfn * (1.0 - y))

    def _isf(self, q, dfn, dfd):
        y = _sp._betaincinv(dfd / 2.0, dfn / 2.0, q)
        return dfd * (1.0 - y) / (dfn * y)

    def _stats(self, dfn, dfd):
        m = dfd / (dfd - 2.0) if dfd > 2 else nan
        v = 2.0 * dfd * dfd * (dfn + dfd - 2.0) / (dfn * (dfd - 2.0) ** 2 * (dfd - 4.0)) if dfd > 4 else nan
        sk = nan
        if dfd > 6:
            sk = (2.0 * dfn + dfd - 2.0) * math.sqrt(8.0 * (dfd - 4.0)) / \
                ((dfd - 6.0) * math.sqrt(dfn * (dfn + dfd - 2.0)))
        ku = nan
        if dfd > 8:
            ku = 12.0 * (dfn * (5.0 * dfd - 22.0) * (dfn + dfd - 2.0) + (dfd - 4.0) * (dfd - 2.0) ** 2) / \
                (dfn * (dfd - 6.0) * (dfd - 8.0) * (dfn + dfd - 2.0))
        return (m, v, sk, ku)


class _uniform(_Dist):
    name = "uniform"

    def _support(self):
        return (0.0, 1.0)

    def _pdf(self, x):
        return 1.0

    def _cdf(self, x):
        return x

    def _sf(self, x):
        return 1.0 - x

    def _ppf(self, q):
        return q

    def _stats(self):
        return (0.5, 1.0 / 12.0, 0.0, -1.2)

    def _entropy(self):
        return 0.0

    def _std_draws(self, rng, n, s):
        return _uniform_draw(rng, n)

    def fit(self, data, *args, **kw):
        d = tolist(data)
        return (min(d), max(d) - min(d))


class _expon(_Dist):
    name = "expon"

    def _support(self):
        return (0.0, inf)

    def _pdf(self, x):
        return math.exp(-x)

    def _logpdf(self, x):
        return -x

    def _cdf(self, x):
        return -math.expm1(-x)

    def _sf(self, x):
        return math.exp(-x)

    def _ppf(self, q):
        return -math.log1p(-q)

    def _isf(self, q):
        return -math.log(q)

    def _stats(self):
        return (1.0, 1.0, 2.0, 6.0)

    def _entropy(self):
        return 1.0

    def _std_draws(self, rng, n, s):
        return tolist(rng.standard_exponential(n)) if hasattr(rng, "standard_exponential") else \
            _Dist._std_draws(self, rng, n, s)

    def fit(self, data, *args, **kw):
        d = tolist(data)
        lo = min(d)
        return (lo, sum(d) / len(d) - lo)


class _gamma(_Dist):
    name = "gamma"
    shapes = ("a",)

    def _argcheck(self, a):
        return a > 0

    def _support(self, a):
        return (0.0, inf)

    def _pdf(self, x, a):
        if x == 0.0:
            return inf if a < 1 else (1.0 if a == 1 else 0.0)
        return math.exp((a - 1.0) * math.log(x) - x - _gammaln(a))

    def _cdf(self, x, a):
        return _gammainc(a, x)

    def _sf(self, x, a):
        return _gammaincc(a, x)

    def _ppf(self, q, a):
        return _sp._gammaincinv(a, q)

    def _stats(self, a):
        return (a, a, 2.0 / math.sqrt(a), 6.0 / a)


class _beta(_Dist):
    name = "beta"
    shapes = ("a", "b")

    def _argcheck(self, a, b):
        return a > 0 and b > 0

    def _support(self, a, b):
        return (0.0, 1.0)

    def _pdf(self, x, a, b):
        if x == 0.0 or x == 1.0:
            if (x == 0.0 and a < 1) or (x == 1.0 and b < 1):
                return inf
            if (x == 0.0 and a > 1) or (x == 1.0 and b > 1):
                return 0.0
        return math.exp((a - 1.0) * math.log(x) + (b - 1.0) * math.log1p(-x) - _sp._betaln(a, b))

    def _cdf(self, x, a, b):
        return _betainc(a, b, x)

    def _ppf(self, q, a, b):
        return _sp._betaincinv(a, b, q)

    def _stats(self, a, b):
        s = a + b
        m = a / s
        v = a * b / (s * s * (s + 1.0))
        sk = 2.0 * (b - a) * math.sqrt(s + 1.0) / ((s + 2.0) * math.sqrt(a * b))
        ku = 6.0 * ((a - b) ** 2 * (s + 1.0) - a * b * (s + 2.0)) / (a * b * (s + 2.0) * (s + 3.0))
        return (m, v, sk, ku)


class _lognorm(_Dist):
    name = "lognorm"
    shapes = ("s",)

    def _argcheck(self, s):
        return s > 0

    def _support(self, s):
        return (0.0, inf)

    def _pdf(self, x, s):
        if x == 0.0:
            return 0.0
        lx = math.log(x)
        return math.exp(-lx * lx / (2.0 * s * s)) / (x * s * math.sqrt(2.0 * math.pi))

    def _cdf(self, x, s):
        if x == 0.0:
            return 0.0
        return _ndtr(math.log(x) / s)

    def _sf(self, x, s):
        if x == 0.0:
            return 1.0
        return _ndtr(-math.log(x) / s)

    def _ppf(self, q, s):
        return math.exp(s * _ndtri(q))

    def _stats(self, s):
        p = math.exp(s * s)
        return (math.sqrt(p), (p - 1.0) * p, (p + 2.0) * math.sqrt(p - 1.0),
                p ** 4 + 2.0 * p ** 3 + 3.0 * p ** 2 - 6.0)


class _laplace(_Dist):
    name = "laplace"

    def _pdf(self, x):
        return 0.5 * math.exp(-abs(x))

    def _cdf(self, x):
        return 0.5 * math.exp(x) if x < 0 else 1.0 - 0.5 * math.exp(-x)

    def _sf(self, x):
        return 1.0 - 0.5 * math.exp(x) if x < 0 else 0.5 * math.exp(-x)

    def _ppf(self, q):
        return math.log(2.0 * q) if q < 0.5 else -math.log(2.0 * (1.0 - q))

    def _stats(self):
        return (0.0, 2.0, 0.0, 3.0)


class _cauchy(_Dist):
    name = "cauchy"

    def _pdf(self, x):
        return 1.0 / (math.pi * (1.0 + x * x))

    def _cdf(self, x):
        return 0.5 + math.atan(x) / math.pi

    def _sf(self, x):
        return 0.5 - math.atan(x) / math.pi

    def _ppf(self, q):
        return math.tan(math.pi * (q - 0.5))

    def _stats(self):
        return (nan, nan, nan, nan)


class _logistic(_Dist):
    name = "logistic"

    def _pdf(self, x):
        e = math.exp(-abs(x))
        return e / (1.0 + e) ** 2

    def _cdf(self, x):
        return _sp._expit(x)

    def _sf(self, x):
        return _sp._expit(-x)

    def _ppf(self, q):
        return math.log(q / (1.0 - q))

    def _stats(self):
        return (0.0, math.pi * math.pi / 3.0, 0.0, 1.2)


class _weibull_min(_Dist):
    name = "weibull_min"
    shapes = ("c",)

    def _argcheck(self, c):
        return c > 0

    def _support(self, c):
        return (0.0, inf)

    def _pdf(self, x, c):
        if x == 0.0:
            return inf if c < 1 else (c if c == 1 else 0.0)
        return c * x ** (c - 1.0) * math.exp(-(x ** c))

    def _cdf(self, x, c):
        return -math.expm1(-(x ** c))

    def _sf(self, x, c):
        return math.exp(-(x ** c))

    def _ppf(self, q, c):
        return (-math.log1p(-q)) ** (1.0 / c)

    def _stats(self, c):
        g1 = math.gamma(1.0 + 1.0 / c)
        g2 = math.gamma(1.0 + 2.0 / c)
        v = g2 - g1 * g1
        g3 = math.gamma(1.0 + 3.0 / c)
        g4 = math.gamma(1.0 + 4.0 / c)
        sk = (g3 - 3.0 * g1 * g2 + 2.0 * g1 ** 3) / v ** 1.5
        ku = (g4 - 4.0 * g1 * g3 + 6.0 * g1 * g1 * g2 - 3.0 * g1 ** 4) / (v * v) - 3.0
        return (g1, v, sk, ku)


# ---- discrete ----

class _binom(_Dist):
    name = "binom"
    shapes = ("n", "p")
    discrete = True

    def _argcheck(self, n, p):
        return n >= 0 and p >= 0 and p <= 1 and n == math.floor(n)

    def _support(self, n, p):
        return (0.0, float(n))

    def _pmf(self, k, n, p):
        if p == 0.0:
            return 1.0 if k == 0 else 0.0
        if p == 1.0:
            return 1.0 if k == n else 0.0
        lp = (_gammaln(n + 1.0) - _gammaln(k + 1.0) - _gammaln(n - k + 1.0) + k * math.log(p)
              + (n - k) * math.log1p(-p))
        return math.exp(lp)

    def _cdf(self, k, n, p):
        if k >= n:
            return 1.0
        return _betainc(n - k, k + 1.0, 1.0 - p)

    def _sf(self, k, n, p):
        if k >= n:
            return 0.0
        return _betainc(k + 1.0, n - k, p)

    def _stats(self, n, p):
        q = 1.0 - p
        v = n * p * q
        if v == 0:
            return (n * p, v, nan, nan)
        return (n * p, v, (q - p) / math.sqrt(v), (1.0 - 6.0 * p * q) / v)

    def _std_draws(self, rng, size, s):
        return [float(v) for v in tolist(rng.binomial(int(s[0]), s[1], size))]


class _poisson(_Dist):
    name = "poisson"
    shapes = ("mu",)
    discrete = True

    def _argcheck(self, mu):
        return mu >= 0

    def _support(self, mu):
        return (0.0, inf)

    def _pmf(self, k, mu):
        if mu == 0.0:
            return 1.0 if k == 0 else 0.0
        return math.exp(-mu + k * math.log(mu) - _gammaln(k + 1.0))

    def _cdf(self, k, mu):
        if mu == 0.0:
            return 1.0
        return _gammaincc(k + 1.0, mu)

    def _sf(self, k, mu):
        if mu == 0.0:
            return 0.0
        return _gammainc(k + 1.0, mu)

    def _stats(self, mu):
        return (mu, mu, 1.0 / math.sqrt(mu) if mu > 0 else nan, 1.0 / mu if mu > 0 else nan)

    def _std_draws(self, rng, size, s):
        return [float(v) for v in tolist(rng.poisson(s[0], size))]


class _bernoulli(_Dist):
    name = "bernoulli"
    shapes = ("p",)
    discrete = True

    def _argcheck(self, p):
        return p >= 0 and p <= 1

    def _support(self, p):
        return (0.0, 1.0)

    def _pmf(self, k, p):
        return p if k == 1 else 1.0 - p

    def _cdf(self, k, p):
        return 1.0 - p if k < 1 else 1.0

    def _sf(self, k, p):
        return p if k < 1 else 0.0

    def _stats(self, p):
        q = 1.0 - p
        v = p * q
        if v == 0:
            return (p, v, nan, nan)
        return (p, v, (q - p) / math.sqrt(v), (1.0 - 6.0 * p * q) / v)


class _geom(_Dist):
    name = "geom"
    shapes = ("p",)
    discrete = True

    def _argcheck(self, p):
        return p > 0 and p <= 1

    def _support(self, p):
        return (1.0, inf)

    def _pmf(self, k, p):
        return (1.0 - p) ** (k - 1.0) * p

    def _cdf(self, k, p):
        return -math.expm1(k * math.log1p(-p)) if p < 1 else 1.0

    def _sf(self, k, p):
        return (1.0 - p) ** k

    def _stats(self, p):
        q = 1.0 - p
        return (1.0 / p, q / (p * p), (2.0 - p) / math.sqrt(q) if q > 0 else nan, 6.0 + p * p / q if q > 0 else nan)


class _randint(_Dist):
    name = "randint"
    shapes = ("low", "high")
    discrete = True

    def _argcheck(self, low, high):
        return high > low

    def _support(self, low, high):
        return (float(low), float(high) - 1.0)

    def _pmf(self, k, low, high):
        return 1.0 / (high - low)

    def _cdf(self, k, low, high):
        return (k - low + 1.0) / (high - low)

    def _sf(self, k, low, high):
        return (high - 1.0 - k) / (high - low)

    def _stats(self, low, high):
        n = high - low
        return ((low + high - 1.0) / 2.0, (n * n - 1.0) / 12.0, 0.0, -1.2 * (n * n + 1.0) / (n * n - 1.0))


norm = _norm()
t = _t()
chi2 = _chi2()
f = _f()
uniform = _uniform()
expon = _expon()
gamma = _gamma()
beta = _beta()
lognorm = _lognorm()
laplace = _laplace()
cauchy = _cauchy()
logistic = _logistic()
weibull_min = _weibull_min()
binom = _binom()
poisson = _poisson()
bernoulli = _bernoulli()
geom = _geom()
randint = _randint()


# ---------------------------------------------------------------------------
# list helpers
# ---------------------------------------------------------------------------

def _mean(v):
    return sum(v) / len(v)


def _var(v, ddof=0):
    n = len(v)
    m = sum(v) / n
    s = 0.0
    for x in v:
        s += (x - m) * (x - m)
    if n - ddof <= 0:
        return nan
    return s / (n - ddof)


def _lerp(a, b, tt):
    d = b - a
    if tt >= 0.5:
        return b - d * (1.0 - tt)
    return a + d * tt


def _pct_sorted(sv, q):
    n = len(sv)
    pos = q / 100.0 * (n - 1)
    lo = int(math.floor(pos))
    hi = lo + 1
    if hi > n - 1:
        hi = n - 1
    return _lerp(sv[lo], sv[hi], pos - lo)


def _rawlist(a):
    if isinstance(a, np.ndarray):
        return a.flatten().tolist() if a.ndim > 0 else [a.tolist()]
    if isinstance(a, (list, tuple)):
        out = []
        for v in a:
            if isinstance(v, (list, tuple, np.ndarray)):
                out.extend(_rawlist(v))
            else:
                out.append(v)
        return out
    return [a]


def _rank_list(v, method="average"):
    n = len(v)
    order = sorted(range(n), key=lambda i: v[i])
    ranks = [0.0] * n
    i = 0
    dense = 0
    while i < n:
        j = i
        while j + 1 < n and v[order[j + 1]] == v[order[i]]:
            j += 1
        dense += 1
        for k in range(i, j + 1):
            if method == "average":
                ranks[order[k]] = (i + j) / 2.0 + 1.0
            elif method == "min":
                ranks[order[k]] = i + 1
            elif method == "max":
                ranks[order[k]] = j + 1
            elif method == "dense":
                ranks[order[k]] = dense
            else:
                ranks[order[k]] = k + 1
        i = j + 1
    return ranks


def _ax(a, axis, fn):
    """apply a list -> scalar function along axis (None flattens)"""
    return reduce_axis(fn, a, axis)


# ---------------------------------------------------------------------------
# descriptive statistics
# ---------------------------------------------------------------------------

def _moment_list(v, k):
    m = sum(v) / len(v)
    s = 0.0
    for x in v:
        s += (x - m) ** k
    return s / len(v)


def moment(a, moment=1, axis=0, center=None, nan_policy="propagate"):
    def f(v):
        if center is None:
            return _moment_list(v, moment) if moment != 1 else 0.0
        s = 0.0
        for x in v:
            s += (x - center) ** moment
        return s / len(v)
    return _ax(a, axis, f)


def skew(a, axis=0, bias=True, nan_policy="propagate"):
    def f(v):
        n = len(v)
        m2 = _moment_list(v, 2)
        m3 = _moment_list(v, 3)
        if m2 <= (1e-15 * (sum(v) / n)) ** 2:
            return nan
        g1 = m3 / m2 ** 1.5
        if not bias:
            if n > 2:
                g1 = math.sqrt((n - 1.0) * n) / (n - 2.0) * g1
            else:
                return nan
        return g1
    return _ax(a, axis, f)


def kurtosis(a, axis=0, fisher=True, bias=True, nan_policy="propagate"):
    def f(v):
        n = len(v)
        m2 = _moment_list(v, 2)
        m4 = _moment_list(v, 4)
        if m2 <= (1e-15 * (sum(v) / n)) ** 2:
            return nan
        if bias:
            val = m4 / m2 ** 2
            return val - 3.0 if fisher else val
        if n < 4:
            return nan
        val = 1.0 / (n - 2.0) / (n - 3.0) * ((n * n - 1.0) * m4 / m2 ** 2 - 3.0 * (n - 1.0) ** 2)
        return val if fisher else val + 3.0
    return _ax(a, axis, f)


def zscore(a, axis=0, ddof=0, nan_policy="propagate"):
    def f(v):
        m = sum(v) / len(v)
        sd = math.sqrt(_var(v, ddof))
        return [(x - m) / sd for x in v]
    return map_axis(f, a, axis)


def sem(a, axis=0, ddof=1, nan_policy="propagate"):
    return _ax(a, axis, lambda v: math.sqrt(_var(v, ddof)) / math.sqrt(len(v)))


def variation(a, axis=0, nan_policy="propagate", ddof=0):
    return _ax(a, axis, lambda v: math.sqrt(_var(v, ddof)) / _mean(v))


def gmean(a, axis=0, dtype=None, weights=None):
    return _ax(a, axis, lambda v: math.exp(sum([math.log(x) for x in v]) / len(v)))


def hmean(a, axis=0, dtype=None, weights=None):
    def f(v):
        for x in v:
            if x < 0:
                raise ValueError("Harmonic mean only defined if all elements greater than or equal to zero")
            if x == 0:
                return 0.0
        return len(v) / sum([1.0 / x for x in v])
    return _ax(a, axis, f)


def pmean(a, p, axis=0, dtype=None, weights=None):
    return _ax(a, axis, lambda v: (sum([x ** p for x in v]) / len(v)) ** (1.0 / p))


def _trim_bounds(v, limits, inclusive):
    if limits is None:
        return v
    lo, hi = limits
    out = []
    for x in v:
        if lo is not None:
            if inclusive[0]:
                if x < lo:
                    continue
            elif x <= lo:
                continue
        if hi is not None:
            if inclusive[1]:
                if x > hi:
                    continue
            elif x >= hi:
                continue
        out.append(x)
    return out


def tmean(a, limits=None, inclusive=(True, True), axis=None):
    return _ax(a, axis, lambda v: _mean(_trim_bounds(v, limits, inclusive)))


def tvar(a, limits=None, inclusive=(True, True), axis=0, ddof=1):
    return _ax(a, axis, lambda v: _var(_trim_bounds(v, limits, inclusive), ddof))


def tstd(a, limits=None, inclusive=(True, True), axis=0, ddof=1):
    return _ax(a, axis, lambda v: math.sqrt(_var(_trim_bounds(v, limits, inclusive), ddof)))


def tsem(a, limits=None, inclusive=(True, True), axis=0, ddof=1):
    def f(v):
        w = _trim_bounds(v, limits, inclusive)
        return math.sqrt(_var(w, ddof)) / math.sqrt(len(w))
    return _ax(a, axis, f)


def tmin(a, lowerlimit=None, axis=0, inclusive=True, nan_policy="propagate"):
    return _ax(a, axis, lambda v: min(_trim_bounds(v, (lowerlimit, None), (inclusive, True))))


def tmax(a, upperlimit=None, axis=0, inclusive=True, nan_policy="propagate"):
    return _ax(a, axis, lambda v: max(_trim_bounds(v, (None, upperlimit), (True, inclusive))))


def trim_mean(a, proportiontocut, axis=0):
    def f(v):
        n = len(v)
        lo = int(proportiontocut * n)
        hi = n - lo
        if lo >= hi:
            raise ValueError("Proportion too big.")
        s = sorted(v)[lo:hi]
        return sum(s) / len(s)
    return _ax(a, axis, f)


def trimboth(a, proportiontocut, axis=0):
    v = sorted(tolist(a))
    lo = int(proportiontocut * len(v))
    return np.array(v[lo:len(v) - lo])


def iqr(x, axis=None, rng=(25, 75), scale=1.0, nan_policy="propagate", interpolation="linear", keepdims=False):
    if scale == "normal":
        scale = 1.3489795003921634
    lo, hi = rng

    def f(v):
        s = sorted(v)
        return (_pct_sorted(s, hi) - _pct_sorted(s, lo)) / scale
    return _ax(x, axis, f)


def median_abs_deviation(x, axis=0, center=None, scale=1.0, nan_policy="propagate"):
    if scale == "normal":
        scale = 0.6744897501960817

    def med(s):
        n = len(s)
        return s[n // 2] if n % 2 == 1 else (s[n // 2 - 1] + s[n // 2]) / 2.0

    def f(v):
        s = sorted(v)
        c = med(s) if center is None else center
        d = sorted([abs(t - c) for t in v])
        return med(d) / scale
    return _ax(x, axis, f)


def scoreatpercentile(a, per, limit=(), interpolation_method="fraction", axis=None):
    v = sorted(tolist(a))
    if limit:
        v = [t for t in v if limit[0] <= t <= limit[1]]
    return vmap(lambda p: _pct_sorted(v, float(p)), per)


def percentileofscore(a, score, kind="rank", nan_policy="propagate"):
    v = tolist(a)
    n = len(v)

    def one(sc):
        left = 0
        right = 0
        for x in v:
            if x < sc:
                left += 1
            if x <= sc:
                right += 1
        if kind == "rank":
            plus1 = 1 if right > left else 0
            return (left + right + plus1) * (50.0 / n)
        if kind == "strict":
            return left / n * 100.0
        if kind == "weak":
            return right / n * 100.0
        if kind == "mean":
            return (left + right) / 2.0 * 100.0 / n
        raise ValueError("kind can only be 'rank', 'strict', 'weak' or 'mean'")
    return vmap(one, score)


def rankdata(a, method="average", axis=None, nan_policy="propagate"):
    a = np.asarray(a)
    if axis is None or a.ndim <= 1:
        r = _rank_list(_rawlist(a), method)
        out = np.array(r).reshape(a.shape) if a.ndim > 0 else np.array(r)
    else:
        out = map_axis(lambda v: _rank_list(v, method), a, axis)
    if method != "average":
        out = out.astype(int)
    return out


def mode(a, axis=0, nan_policy="propagate", keepdims=False):
    a = np.asarray(a)

    def f(v):
        s = sorted(v)
        best = s[0]
        bc = 0
        i = 0
        n = len(s)
        while i < n:
            j = i
            while j + 1 < n and s[j + 1] == s[i]:
                j += 1
            if j - i + 1 > bc:
                bc = j - i + 1
                best = s[i]
            i = j + 1
        return (best, bc)
    if axis is None or a.ndim <= 1:
        m, c = f(_rawlist(a))
        if keepdims:
            return Result(["mode", "count"], [np.array([m]), np.array([c])], name="ModeResult")
        return Result(["mode", "count"], [m, c], name="ModeResult")
    rows = a.tolist()
    if axis < 0:
        axis += 2
    if axis == 0:
        sl = [[rows[i][j] for i in range(len(rows))] for j in range(len(rows[0]))]
    else:
        sl = rows
    res = [f(s) for s in sl]
    return Result(["mode", "count"], [np.array([r[0] for r in res]), np.array([r[1] for r in res])],
                  name="ModeResult")


def describe(a, axis=0, ddof=1, bias=True, nan_policy="propagate"):
    a = np.asarray(a)
    if axis is None or a.ndim <= 1:
        v = tolist(a)
        n = len(v)
        return Result(["nobs", "minmax", "mean", "variance", "skewness", "kurtosis"],
                      [n, (min(v), max(v)), _mean(v), _var(v, ddof), skew(a, axis, bias), kurtosis(a, axis, True, bias)],
                      name="DescribeResult")
    n = a.shape[axis]
    return Result(["nobs", "minmax", "mean", "variance", "skewness", "kurtosis"],
                  [n, (np.min(a, axis=axis), np.max(a, axis=axis)), np.mean(a, axis=axis),
                   reduce_axis(lambda v: _var(v, ddof), a, axis), skew(a, axis, bias), kurtosis(a, axis, True, bias)],
                  name="DescribeResult")


def entropy(pk, qk=None, base=None, axis=0):
    def f(v, w):
        s = sum(v)
        p = [x / s for x in v]
        if w is None:
            r = 0.0
            for x in p:
                if x > 0:
                    r -= x * math.log(x)
        else:
            s2 = sum(w)
            r = 0.0
            for x, y in zip(p, [t / s2 for t in w]):
                if x > 0:
                    r += x * math.log(x / y) if y > 0 else inf
        if base is not None:
            r = r / math.log(base)
        return r
    if np.asarray(pk).ndim <= 1:
        return f(tolist(pk), None if qk is None else tolist(qk))
    raise NotImplementedError("entropy: only 1-d input")


def jarque_bera(x):
    v = tolist(x)
    n = len(v)
    s = skew(v)
    k = kurtosis(v)
    stat = n / 6.0 * (s * s + k * k / 4.0)
    return Result(["statistic", "pvalue"], [stat, chi2.sf(stat, 2)], name="SignificanceResult")


# ---------------------------------------------------------------------------
# correlation
# ---------------------------------------------------------------------------

def _pearson_r(x, y):
    n = len(x)
    mx = sum(x) / n
    my = sum(y) / n
    sxy = 0.0
    sxx = 0.0
    syy = 0.0
    for i in range(n):
        dx = x[i] - mx
        dy = y[i] - my
        sxy += dx * dy
        sxx += dx * dx
        syy += dy * dy
    den = math.sqrt(sxx) * math.sqrt(syy)
    if den == 0.0:
        return nan
    r = sxy / den
    return max(min(r, 1.0), -1.0)


def _pearson_p(r, n, alternative="two-sided"):
    if r != r:
        return nan
    if n == 2:
        return 1.0 if alternative == "two-sided" else (0.5 if abs(r) == 1 else None)
    ab = n / 2.0 - 1.0
    if alternative == "two-sided":
        return 2.0 * _betainc(ab, ab, (1.0 - abs(r)) / 2.0)
    cdf = _betainc(ab, ab, (r + 1.0) / 2.0)
    if alternative == "less":
        return cdf
    return 1.0 - cdf if cdf > 0.5 else _betainc(ab, ab, (1.0 - r) / 2.0)


class _PearsonResult(Result):
    def __init__(self, r, p, n):
        Result.__init__(self, ["statistic", "pvalue"], [r, p], name="PearsonRResult")
        self._n = n

    def confidence_interval(self, confidence_level=0.95, method=None):
        r = self.statistic
        n = self._n
        if r == 1.0 or r == -1.0 or n <= 3:
            return Result(["low", "high"], [-1.0, 1.0] if abs(r) != 1.0 else [r, r], name="ConfidenceInterval")
        z = math.atanh(r)
        se = 1.0 / math.sqrt(n - 3.0)
        zc = _ndtri(1.0 - (1.0 - confidence_level) / 2.0)
        return Result(["low", "high"], [math.tanh(z - zc * se), math.tanh(z + zc * se)], name="ConfidenceInterval")


def pearsonr(x, y, alternative="two-sided", axis=0, method=None):
    xv = tolist(x)
    yv = tolist(y)
    if len(xv) != len(yv):
        raise ValueError("x and y must have the same length.")
    if len(xv) < 2:
        raise ValueError("x and y must have length at least 2.")
    r = _pearson_r(xv, yv)
    return _PearsonResult(r, _pearson_p(r, len(xv), alternative), len(xv))


def _t_p(tv, df, alternative="two-sided"):
    if alternative == "two-sided":
        return 2.0 * t.sf(abs(tv), df)
    if alternative == "less":
        return t.cdf(tv, df)
    return t.sf(tv, df)


def _spearman_pair(xv, yv, alternative):
    n = len(xv)
    r = _pearson_r(_rank_list(xv), _rank_list(yv))
    if n == 2 or r != r:
        return r, nan if r != r else 1.0
    dof = n - 2
    if abs(r) >= 1.0:
        tv = inf if r > 0 else -inf
    else:
        tv = r * math.sqrt(dof / ((r + 1.0) * (1.0 - r)))
    return r, _t_p(tv, dof, alternative)


def spearmanr(a, b=None, axis=0, nan_policy="propagate", alternative="two-sided"):
    a = np.asarray(a)
    if b is None:
        if a.ndim != 2:
            raise ValueError("spearmanr needs two variables")
        cols = slices(a, axis)[0]
    else:
        if a.ndim == 2:
            cols = slices(a, axis)[0] + slices(np.asarray(b), axis)[0]
        else:
            r, p = _spearman_pair(_rawlist(a), _rawlist(b), alternative)
            return Result(["statistic", "pvalue"], [r, p], extra={"correlation": r}, name="SignificanceResult")
    k = len(cols)
    if k == 2:
        r, p = _spearman_pair(cols[0], cols[1], alternative)
        return Result(["statistic", "pvalue"], [r, p], extra={"correlation": r}, name="SignificanceResult")
    rm = []
    pm = []
    for i in range(k):
        rr = []
        pp = []
        for j in range(k):
            if i == j:
                rr.append(1.0)
                pp.append(0.0)
            else:
                r, p = _spearman_pair(cols[i], cols[j], alternative)
                rr.append(r)
                pp.append(p)
        rm.append(rr)
        pm.append(pp)
    return Result(["statistic", "pvalue"], [np.array(rm), np.array(pm)], name="SignificanceResult")


def pointbiserialr(x, y):
    return pearsonr(x, y)


def _kendall_p_exact(n, c, alternative):
    tot = n * (n - 1) // 2
    in_right = c >= tot - c
    greater = alternative == "greater"
    c = min(c, tot - c)
    if n == 1:
        prob = 1.0
        pm = 1.0
    elif n == 2:
        prob = 1.0
        pm = 0.5
    elif c == 0:
        prob = 2.0 / math.factorial(n) if n < 171 else 0.0
        pm = prob / 2.0
    elif c == 1:
        prob = 2.0 / math.factorial(n - 1) if n < 171 else 0.0
        pm = (n - 1.0) / math.factorial(n)
    elif 4 * c == n * (n - 1) and alternative == "two-sided":
        prob = 1.0
        pm = 1.0
    else:
        new = [0] * (c + 1)
        new[0] = 1
        for j in range(2, n + 1):
            cur = new
            new = [0] * (c + 1)
            run = 0
            for k in range(c + 1):
                run += cur[k]
                if k - j >= 0:
                    run -= cur[k - j]
                new[k] = run
        fact = float(math.factorial(n))
        pm = new[c] / fact
        prob = 2.0 * float(sum(new)) / fact
    if alternative == "two-sided":
        return min(1.0, prob)
    if in_right:
        return min(1.0, prob / 2.0)
    return min(1.0, 1.0 - prob / 2.0 + pm)


def kendalltau(x, y, nan_policy="propagate", method="auto", variant="b", alternative="two-sided"):
    xv = _rawlist(x)
    yv = _rawlist(y)
    n = len(xv)
    if n != len(yv):
        raise ValueError("All inputs to `kendalltau` must be of the same size")
    con = 0
    dis = 0
    tx = 0
    ty = 0
    txy = 0
    for i in range(n):
        xi = xv[i]
        yi = yv[i]
        for j in range(i + 1, n):
            dx = xv[j] - xi
            dy = yv[j] - yi
            if dx == 0:
                tx += 1
            if dy == 0:
                ty += 1
            if dx == 0 and dy == 0:
                txy += 1
            elif dx != 0 and dy != 0:
                if (dx > 0) == (dy > 0):
                    con += 1
                else:
                    dis += 1
    tot = n * (n - 1) // 2

    def tiecounts(v):
        s = sorted(v)
        out = []
        i = 0
        while i < n:
            j = i
            while j + 1 < n and s[j + 1] == s[i]:
                j += 1
            out.append(j - i + 1)
            i = j + 1
        return out
    xt = tiecounts(xv)
    yt = tiecounts(yv)
    xtie = sum([c * (c - 1) // 2 for c in xt])
    ytie = sum([c * (c - 1) // 2 for c in yt])
    cmd = con - dis
    if variant == "b":
        if tot == xtie or tot == ytie:
            return Result(["statistic", "pvalue"], [nan, nan], extra={"correlation": nan}, name="SignificanceResult")
        tau = cmd / math.sqrt(tot - xtie) / math.sqrt(tot - ytie)
    else:
        m = min(len(xt), len(yt))
        tau = 2.0 * cmd / (n * n * (m - 1.0) / m)
    if method == "auto":
        if xtie == 0 and ytie == 0 and (n <= 33 or min(dis, tot - dis) <= 1):
            method = "exact"
        else:
            method = "asymptotic"
    if xtie == 0 and ytie == 0 and method == "exact":
        cc = tot - dis if alternative == "greater" else dis
        p = _kendall_p_exact(n, cc, alternative)
        return Result(["statistic", "pvalue"], [tau, p], extra={"correlation": tau}, name="SignificanceResult")
    # asymptotic
    def tsum(c, f):
        return sum([f(k) for k in c])
    v0 = n * (n - 1.0) * (2.0 * n + 5.0)
    vt = tsum(xt, lambda k: k * (k - 1.0) * (2.0 * k + 5.0))
    vu = tsum(yt, lambda k: k * (k - 1.0) * (2.0 * k + 5.0))
    v1 = tsum(xt, lambda k: k * (k - 1.0)) * tsum(yt, lambda k: k * (k - 1.0))
    v2 = tsum(xt, lambda k: k * (k - 1.0) * (k - 2.0)) * tsum(yt, lambda k: k * (k - 1.0) * (k - 2.0))
    var = (v0 - vt - vu) / 18.0 + v1 / (2.0 * n * (n - 1.0))
    if n > 2:
        var += v2 / (9.0 * n * (n - 1.0) * (n - 2.0))
    z = cmd / math.sqrt(var)
    if alternative == "two-sided":
        p = 2.0 * norm.sf(abs(z))
    elif alternative == "less":
        p = norm.cdf(z)
    else:
        p = norm.sf(z)
    return Result(["statistic", "pvalue"], [tau, p], extra={"correlation": tau}, name="SignificanceResult")


def linregress(x, y=None, alternative="two-sided"):
    if y is None:
        a = np.asarray(x)
        if a.shape[0] == 2:
            xv = tolist(a[0])
            yv = tolist(a[1])
        else:
            xv = tolist(a[:, 0])
            yv = tolist(a[:, 1])
    else:
        xv = tolist(x)
        yv = tolist(y)
    n = len(xv)
    if n != len(yv):
        raise ValueError("Inputs must not be empty.")
    if n < 2:
        raise ValueError("Inputs must not be empty.")
    xm = sum(xv) / n
    ym = sum(yv) / n
    ssxm = 0.0
    ssym = 0.0
    ssxym = 0.0
    for i in range(n):
        dx = xv[i] - xm
        dy = yv[i] - ym
        ssxm += dx * dx
        ssym += dy * dy
        ssxym += dx * dy
    ssxm /= n
    ssym /= n
    ssxym /= n
    if ssxm == 0.0:
        raise ValueError("Cannot calculate a linear regression if all x values are identical")
    r_den = math.sqrt(ssxm * ssym)
    r = 0.0 if r_den == 0.0 else ssxym / r_den
    r = max(min(r, 1.0), -1.0)
    df = n - 2
    slope = ssxym / ssxm
    intercept = ym - slope * xm
    if n == 2:
        p = 1.0 if yv[0] == yv[1] else 0.0
        return Result(["slope", "intercept", "rvalue", "pvalue", "stderr"], [slope, intercept, r, p, 0.0],
                      extra={"intercept_stderr": 0.0}, name="LinregressResult")
    tiny = 1e-20
    tv = r * math.sqrt(df / ((1.0 - r + tiny) * (1.0 + r + tiny)))
    p = _t_p(tv, df, alternative)
    slope_stderr = math.sqrt((1.0 - r * r) * ssym / ssxm / df)
    intercept_stderr = slope_stderr * math.sqrt(ssxm + xm * xm)
    return Result(["slope", "intercept", "rvalue", "pvalue", "stderr"],
                  [slope, intercept, r, p, slope_stderr], extra={"intercept_stderr": intercept_stderr},
                  name="LinregressResult")


# ---------------------------------------------------------------------------
# hypothesis tests
# ---------------------------------------------------------------------------

class _TtestResult(Result):
    def __init__(self, stat, p, df, est=None, se=None, alt="two-sided"):
        Result.__init__(self, ["statistic", "pvalue"], [stat, p], extra={"df": df}, name="TtestResult")
        self._est = est
        self._se = se
        self._alt = alt

    def __repr__(self):
        return "TtestResult(statistic=" + repr(self.statistic) + ", pvalue=" + repr(self.pvalue) + \
            ", df=" + repr(self.df) + ")"

    def confidence_interval(self, confidence_level=0.95):
        alt = self._alt
        df = self.df
        if alt == "less":
            lo = -inf
            hi = self._est + t.ppf(confidence_level, df) * self._se
        elif alt == "greater":
            lo = self._est + t.ppf(1.0 - confidence_level, df) * self._se
            hi = inf
        else:
            q = t.ppf((1.0 + confidence_level) / 2.0, df)
            lo = self._est - q * self._se
            hi = self._est + q * self._se
        return Result(["low", "high"], [lo, hi], name="ConfidenceInterval")


def _tt_combine(results):
    if len(results) == 1:
        return results[0]
    stat = np.array([r.statistic for r in results])
    p = np.array([r.pvalue for r in results])
    return _TtestResult(stat, p, results[0].df)


def _t1(v, popmean, alternative):
    n = len(v)
    m = sum(v) / n
    var = _var(v, 1)
    df = n - 1
    se = math.sqrt(var / n)
    if se == 0.0:
        tv = nan if m == popmean else (inf if m > popmean else -inf)
    else:
        tv = (m - popmean) / se
    return _TtestResult(tv, _t_p(tv, df, alternative), df, m, se, alternative)


def ttest_1samp(a, popmean, axis=0, nan_policy="propagate", alternative="two-sided"):
    a = np.asarray(a)
    if axis is None or a.ndim <= 1:
        return _t1(tolist(a), popmean, alternative)
    sl = slices(a, axis)[0]
    return _tt_combine([_t1(v, popmean, alternative) for v in sl])


def _t2(v1, v2, equal_var, alternative):
    n1 = len(v1)
    n2 = len(v2)
    m1 = sum(v1) / n1
    m2 = sum(v2) / n2
    s1 = _var(v1, 1)
    s2 = _var(v2, 1)
    if equal_var:
        df = n1 + n2 - 2.0
        sv = ((n1 - 1.0) * s1 + (n2 - 1.0) * s2) / df
        se = math.sqrt(sv * (1.0 / n1 + 1.0 / n2))
    else:
        a1 = s1 / n1
        a2 = s2 / n2
        df = (a1 + a2) ** 2 / (a1 * a1 / (n1 - 1.0) + a2 * a2 / (n2 - 1.0))
        se = math.sqrt(a1 + a2)
    d = m1 - m2
    if se == 0.0:
        tv = nan if d == 0 else (inf if d > 0 else -inf)
    else:
        tv = d / se
    return _TtestResult(tv, _t_p(tv, df, alternative), df, d, se, alternative)


def ttest_ind(a, b, axis=0, equal_var=True, nan_policy="propagate", permutations=None, random_state=None,
              alternative="two-sided", trim=0, keepdims=False):
    a = np.asarray(a)
    b = np.asarray(b)
    if axis is None or a.ndim <= 1:
        va = tolist(a)
        vb = tolist(b)
        if trim > 0:
            va = _trimmed(va, trim)
            vb = _trimmed(vb, trim)
        return _t2(va, vb, equal_var, alternative)
    sa = slices(a, axis)[0]
    sb = slices(b, axis)[0]
    return _tt_combine([_t2(sa[i], sb[i], equal_var, alternative) for i in range(len(sa))])


def _trimmed(v, p):
    s = sorted(v)
    k = int(p * len(s))
    return s[k:len(s) - k]


def ttest_rel(a, b, axis=0, nan_policy="propagate", alternative="two-sided"):
    a = np.asarray(a)
    b = np.asarray(b)
    if axis is None or a.ndim <= 1:
        d = [p - q for p, q in zip(tolist(a), tolist(b))]
        return _t1(d, 0.0, alternative)
    sa = slices(a, axis)[0]
    sb = slices(b, axis)[0]
    return _tt_combine([_t1([p - q for p, q in zip(sa[i], sb[i])], 0.0, alternative) for i in range(len(sa))])


def ttest_ind_from_stats(mean1, std1, nobs1, mean2, std2, nobs2, equal_var=True, alternative="two-sided"):
    s1 = std1 * std1
    s2 = std2 * std2
    if equal_var:
        df = nobs1 + nobs2 - 2.0
        sv = ((nobs1 - 1.0) * s1 + (nobs2 - 1.0) * s2) / df
        se = math.sqrt(sv * (1.0 / nobs1 + 1.0 / nobs2))
    else:
        a1 = s1 / nobs1
        a2 = s2 / nobs2
        df = (a1 + a2) ** 2 / (a1 * a1 / (nobs1 - 1.0) + a2 * a2 / (nobs2 - 1.0))
        se = math.sqrt(a1 + a2)
    tv = (mean1 - mean2) / se
    return Result(["statistic", "pvalue"], [tv, _t_p(tv, df, alternative)], name="Ttest_indResult")


def chisquare(f_obs, f_exp=None, ddof=0, axis=0):
    o = tolist(f_obs)
    k = len(o)
    if f_exp is None:
        e = [sum(o) / k] * k
    else:
        e = tolist(f_exp)
    stat = 0.0
    for i in range(k):
        stat += (o[i] - e[i]) ** 2 / e[i]
    return Result(["statistic", "pvalue"], [stat, chi2.sf(stat, k - 1 - ddof)], name="Power_divergenceResult")


def chi2_contingency(observed, correction=True, lambda_=None):
    rows = [[float(v) for v in r] for r in np.asarray(observed).tolist()]
    nr = len(rows)
    nc = len(rows[0])
    rs = [sum(r) for r in rows]
    cs = [sum([rows[i][j] for i in range(nr)]) for j in range(nc)]
    tot = sum(rs)
    dof = (nr - 1) * (nc - 1)
    exp = [[rs[i] * cs[j] / tot for j in range(nc)] for i in range(nr)]
    if dof == 0:
        stat = 0.0
        p = 1.0
    else:
        stat = 0.0
        for i in range(nr):
            for j in range(nc):
                d = rows[i][j] - exp[i][j]
                if correction and dof == 1:
                    mag = min(0.5, abs(d))
                    d = d + mag if d < 0 else d - mag
                stat += d * d / exp[i][j]
        p = chi2.sf(stat, dof)
    return Result(["statistic", "pvalue", "dof", "expected_freq"], [stat, p, dof, np.array(exp)],
                  name="Chi2ContingencyResult")


def f_oneway(*samples, **kw):
    groups = [tolist(s) for s in samples]
    k = len(groups)
    n = sum([len(g) for g in groups])
    allm = sum([sum(g) for g in groups]) / n
    ssb = 0.0
    ssw = 0.0
    for g in groups:
        m = sum(g) / len(g)
        ssb += len(g) * (m - allm) ** 2
        for x in g:
            ssw += (x - m) ** 2
    dfb = k - 1
    dfw = n - k
    if ssw == 0.0:
        fv = inf if ssb > 0 else nan
    else:
        fv = (ssb / dfb) / (ssw / dfw)
    return Result(["statistic", "pvalue"], [fv, f.sf(fv, dfb, dfw)], name="F_onewayResult")


def fisher_exact(table, alternative="two-sided"):
    tb = np.asarray(table).tolist()
    a = int(tb[0][0])
    b = int(tb[0][1])
    c = int(tb[1][0])
    d = int(tb[1][1])
    odds = (a * d) / (b * c) if b * c > 0 else (inf if a * d > 0 else nan)
    n1 = a + b
    n2 = c + d
    k = a + c
    lo = max(0, k - n2)
    hi = min(k, n1)

    def pmf(x):
        return math.comb(n1, x) * math.comb(n2, k - x) / math.comb(n1 + n2, k)
    pexact = pmf(a)
    if alternative == "less":
        p = sum([pmf(x) for x in range(lo, a + 1)])
    elif alternative == "greater":
        p = sum([pmf(x) for x in range(a, hi + 1)])
    else:
        p = 0.0
        for x in range(lo, hi + 1):
            px = pmf(x)
            if px <= pexact * (1.0 + 1e-7):
                p += px
    return Result(["statistic", "pvalue"], [odds, min(p, 1.0)], name="SignificanceResult")


def binomtest(k, n, p=0.5, alternative="two-sided"):
    if alternative == "less":
        pv = binom.cdf(k, n, p)
    elif alternative == "greater":
        pv = binom.sf(k - 1, n, p)
    else:
        d = binom.pmf(k, n, p)
        rerr = 1.0 + 1e-7
        if k == p * n:
            pv = 1.0
        elif k < p * n:
            y = 0
            for i in range(int(math.ceil(p * n)), n + 1):
                if binom.pmf(i, n, p) <= d * rerr:
                    y += 1
            pv = binom.cdf(k, n, p) + binom.sf(n - y, n, p)
        else:
            y = 0
            for i in range(0, int(math.floor(p * n)) + 1):
                if binom.pmf(i, n, p) <= d * rerr:
                    y += 1
            pv = binom.cdf(y - 1, n, p) + binom.sf(k - 1, n, p)
        pv = min(1.0, pv)
    return Result(["k", "n", "proportion_estimate", "pvalue"], [k, n, k / n, pv], extra={"statistic": k / n},
                  name="BinomTestResult")


def _ks_exact_p(n1, n2, d):
    g = _gcd(n1, n2)
    a = n2 // g
    b = n1 // g
    h = int(round(d * (n1 * n2 // g)))
    # count lattice paths from (0,0) to (n1,n2) with |i*a - j*b| < h
    prev = None
    for i in range(n1 + 1):
        cur = [0] * (n2 + 1)
        for j in range(n2 + 1):
            if abs(i * a - j * b) >= h:
                continue
            if i == 0 and j == 0:
                cur[j] = 1
            else:
                v = 0
                if i > 0:
                    v += prev[j]
                if j > 0:
                    v += cur[j - 1]
                cur[j] = v
        prev = cur
    tot = 1
    for q in range(1, n1 + 1):
        tot = tot * (n2 + q) // q
    p = 1.0 - prev[n2] / float(tot)
    return min(1.0, max(0.0, p))


def _gcd(a, b):
    while b:
        a, b = b, a % b
    return a


def ks_2samp(data1, data2, alternative="two-sided", method="auto"):
    a = sorted(tolist(data1))
    b = sorted(tolist(data2))
    n1 = len(a)
    n2 = len(b)
    allv = sorted(a + b)
    d = 0.0
    i = 0
    j = 0
    for x in allv:
        while i < n1 and a[i] <= x:
            i += 1
        while j < n2 and b[j] <= x:
            j += 1
        dd = i / n1 - j / n2
        if abs(dd) > d:
            d = abs(dd)
    if alternative == "two-sided" and method in ("auto", "exact") and max(n1, n2) <= 10000:
        return Result(["statistic", "pvalue"], [d, _ks_exact_p(n1, n2, d)], name="KstestResult")
    en = math.sqrt(n1 * n2 / float(n1 + n2))
    lam = (en + 0.12 + 0.11 / en) * d
    s = 0.0
    sign = 1.0
    for k in range(1, 101):
        term = sign * math.exp(-2.0 * k * k * lam * lam)
        s += term
        if abs(term) < 1e-16:
            break
        sign = -sign
    p = min(1.0, max(0.0, 2.0 * s))
    return Result(["statistic", "pvalue"], [d, p], name="KstestResult")


def _mwu_counts(m, n):
    """number of arrangements for each value of U (list indexed by U)"""
    # prev[j] = counts for (i-1 x-values, j y-values); standard recurrence
    # c(m, n, u) = c(m - 1, n, u - n) + c(m, n - 1, u)
    table = {}
    for i in range(m + 1):
        for j in range(n + 1):
            if i == 0 or j == 0:
                table[(i, j)] = [1]
                continue
            a = table[(i - 1, j)]
            b = table[(i, j - 1)]
            size = i * j + 1
            c = [0] * size
            for u in range(len(b)):
                c[u] += b[u]
            for u in range(len(a)):
                if u + j < size:
                    c[u + j] += a[u]
            table[(i, j)] = c
    return table[(m, n)]


def mannwhitneyu(x, y, use_continuity=True, alternative="two-sided", axis=0, method="auto"):
    xv = tolist(x)
    yv = tolist(y)
    n1 = len(xv)
    n2 = len(yv)
    r = _rank_list(xv + yv)
    u1 = sum(r[:n1]) - n1 * (n1 + 1) / 2.0
    u2 = n1 * n2 - u1
    # tie correction
    allv = sorted(xv + yv)
    n = n1 + n2
    ts = 0.0
    i = 0
    while i < n:
        j = i
        while j + 1 < n and allv[j + 1] == allv[i]:
            j += 1
        c = j - i + 1
        ts += c ** 3 - c
        i = j + 1
    if method in ("auto", "exact") and ts == 0.0 and (method == "exact" or (n1 <= 8 and n2 <= 8)):
        cnt = _mwu_counts(n1, n2)
        tot = float(sum(cnt))
        if alternative == "greater":
            p = sum(cnt[int(round(u1)):]) / tot
        elif alternative == "less":
            p = sum(cnt[int(round(u2)):]) / tot
        else:
            p = min(1.0, 2.0 * sum(cnt[int(round(max(u1, u2))):]) / tot)
        return Result(["statistic", "pvalue"], [u1, p], name="MannwhitneyuResult")
    mu = n1 * n2 / 2.0
    s = math.sqrt(n1 * n2 / 12.0 * ((n + 1.0) - ts / (n * (n - 1.0))))
    if alternative == "greater":
        u = u1
    elif alternative == "less":
        u = u2
    else:
        u = max(u1, u2)
    num = u - mu - (0.5 if use_continuity else 0.0)
    z = num / s
    p = norm.sf(z)
    if alternative == "two-sided":
        p = min(2.0 * p, 1.0)
    return Result(["statistic", "pvalue"], [u1, p], name="MannwhitneyuResult")
