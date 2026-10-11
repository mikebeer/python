# pandas subset: rolling / expanding / ewm
import math
from .base import nan, Index
from .core import Series, DataFrame, _lred, _NUMDT


def _wfun(name, w, ddof=1):
    nn = w
    return _lred(name, nn, ddof)


class Rolling:
    def __init__(self, obj, window, min_periods=None, center=False):
        self.obj = obj
        self.window = window
        self.min_periods = window if min_periods is None else min_periods
        self.center = center

    def _series(self, s, f):
        v = s._v
        n = len(v)
        w = self.window
        minp = self.min_periods
        res = [nan] * n
        for i in range(n):
            if self.center:
                lo = i - w // 2
                hi = i + (w - 1) // 2 + 1
            else:
                lo = i - w + 1
                hi = i + 1
            if lo < 0:
                lo = 0
            if hi > n:
                hi = n
            win = v[lo:hi]
            nn = [x for x in win if not (x is None or x != x)]
            if len(nn) < minp:
                continue
            res[i] = f(nn)
        return s._new(res, 'float64')

    def _apply_all(self, f, numeric=True):
        o = self.obj
        if isinstance(o, Series):
            return self._series(o, f)
        return o._mapcols(lambda s: self._series(s, f), True)

    def mean(self, **kw):
        return self._apply_all(lambda nn: sum(nn) / len(nn))

    def sum(self, **kw):
        return self._apply_all(lambda nn: float(sum(nn)))

    def min(self, **kw):
        return self._apply_all(lambda nn: float(min(nn)))

    def max(self, **kw):
        return self._apply_all(lambda nn: float(max(nn)))

    def median(self, **kw):
        return self._apply_all(lambda nn: _lred('median', nn))

    def std(self, ddof=1, **kw):
        return self._apply_all(lambda nn: _lred('std', nn, ddof))

    def var(self, ddof=1, **kw):
        return self._apply_all(lambda nn: _lred('var', nn, ddof))

    def count(self, **kw):
        o = self.obj

        def one(s):
            v = s._v
            n = len(v)
            w = self.window
            res = []
            for i in range(n):
                lo = max(0, i - w + 1)
                res.append(float(len([x for x in v[lo:i + 1] if x == x])))
            return s._new(res, 'float64')
        if isinstance(o, Series):
            return one(o)
        return o._mapcols(one, True)

    def apply(self, func, raw=False, **kw):
        return self._apply_all(lambda nn: func(nn if raw else Series(nn)))

    def quantile(self, q, **kw):
        from .core import _quant
        return self._apply_all(lambda nn: _quant(sorted(nn), q))

    def corr(self, other=None, **kw):
        raise NotImplementedError('rolling corr')

    def agg(self, func, **kw):
        if isinstance(func, str):
            return getattr(self, func)()
        if isinstance(func, list):
            from .reshape import concat
            return concat([getattr(self, f)().rename(f) for f in func], axis=1)
        return self.apply(func)

    aggregate = agg


class Expanding(Rolling):
    def __init__(self, obj, min_periods=1):
        self.obj = obj
        self.window = 10 ** 9
        self.min_periods = min_periods
        self.center = False

    def _series(self, s, f):
        v = s._v
        n = len(v)
        res = [nan] * n
        nn = []
        for i in range(n):
            x = v[i]
            if not (x is None or x != x):
                nn.append(x)
            if len(nn) >= self.min_periods and len(nn) > 0:
                res[i] = f(nn)
        return s._new(res, 'float64')


class EWM:
    def __init__(self, obj, com, span, halflife, alpha, adjust, min_periods):
        self.obj = obj
        if alpha is None:
            if com is not None:
                alpha = 1.0 / (1.0 + com)
            elif span is not None:
                alpha = 2.0 / (span + 1.0)
            elif halflife is not None:
                alpha = 1.0 - math.exp(math.log(0.5) / halflife)
        self.alpha = alpha
        self.adjust = adjust
        self.min_periods = min_periods

    def _series(self, s):
        v = s._v
        a = self.alpha
        res = []
        num = 0.0
        den = 0.0
        prev = None
        cnt = 0
        for x in v:
            if x != x:
                res.append(res[-1] if res else nan)
                continue
            cnt += 1
            if self.adjust:
                num = num * (1 - a) + x
                den = den * (1 - a) + 1.0
                val = num / den
            else:
                val = x if prev is None else (1 - a) * prev + a * x
                prev = val
            res.append(val if cnt >= max(self.min_periods, 1) else nan)
        return s._new(res, 'float64')

    def mean(self, **kw):
        if isinstance(self.obj, Series):
            return self._series(self.obj)
        return self.obj._mapcols(lambda s: self._series(s), True)
