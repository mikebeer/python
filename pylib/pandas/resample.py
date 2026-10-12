# pandas subset: resample / asfreq / truncate on datetime indexes
import datetime as _dtm
from .base import nan, NaT, Index
from .core import Series, DataFrame, _lst
from . import tslib as T
from .tslib import Timestamp, Timedelta, to_offset, Tick, as_timestamp
from .dtindex import DatetimeIndex, dt_slice_positions

_DAY = 86400000000
_RIGHT = ('Week', 'MonthEnd', 'YearEnd', 'QuarterEnd')


def assign_labels(tv, off, closed):
    """bin label (Timestamp) of every timestamp in tv (None for NaT)"""
    first = None
    for x in tv:
        if x == x and (first is None or x < first):
            first = x
    out = []
    if first is None:
        return [None for x in tv]
    if isinstance(off, Tick):
        step = off.n * off._tickus
        origin = first.normalize()._epoch_us()
    for x in tv:
        if x != x:
            out.append(None)
            continue
        if isinstance(off, Tick):
            u = x._epoch_us()
            lu = origin + ((u - origin) // step) * step
            if closed == 'right' and (u - origin) % step == 0 and u != origin:
                lu = u - step
            lab = T.ts_from_us(lu)
        else:
            nd = x.normalize()
            one = off.__class__(1, **off._kw()) if off.n == 1 else off
            if closed == 'right':
                lab = one.rollforward(nd)
            else:
                lab = one.rollback(nd)
        out.append(lab)
    return out


def default_closed(off):
    return 'right' if off.__class__.__name__ in _RIGHT else 'left'


def _label_next(off, lab):
    if isinstance(off, Tick):
        return T.ts_from_us(lab._epoch_us() + off.n * off._tickus)
    return off._add_ts(lab)


class Resampler:
    def __init__(self, obj, rule, closed, label, on, origin):
        self.obj = obj
        self.on = on
        self.off = to_offset(rule)
        self._isdf = isinstance(obj, DataFrame)
        if on is not None:
            tv = list(obj[on]._v)
        else:
            if not obj._idx._dt.startswith('datetime'):
                raise TypeError("Only valid with DatetimeIndex, TimedeltaIndex or PeriodIndex, but got an instance of '%s'" % type(obj._idx).__name__)
            tv = list(obj._idx._v)
        self._tv = tv
        cn = self.off.__class__.__name__
        self.closed = closed if closed is not None else ('right' if cn in _RIGHT else 'left')
        self.label = label if label is not None else ('right' if cn in _RIGHT else 'left')
        self._build()

    def _build(self):
        off = self.off
        tv = self._tv
        labels_of = []
        first = None
        for x in tv:
            if x == x and (first is None or x < first):
                first = x
        bins = {}
        order_min = None
        order_max = None
        if first is None:
            self._labels = []
            self._groups = []
            self._pos = []
            return
        lab_of = assign_labels(tv, off, self.closed)
        for lab in lab_of:
            if lab is None:
                continue
            lu = lab._epoch_us()
            if order_min is None or lu < order_min:
                order_min = lu
            if order_max is None or lu > order_max:
                order_max = lu
        if isinstance(off, Tick):
            step = off.n * off._tickus
        # continuous label sequence
        labels = []
        cur = T.ts_from_us(order_min)
        guard = 0
        while cur._epoch_us() <= order_max:
            labels.append(cur)
            cur = _label_next(off, cur)
            guard += 1
            if guard > 2000000:
                break
        pos = {}
        for i in range(len(labels)):
            pos[labels[i]._epoch_us()] = i
        groups = [[] for _ in labels]
        for i in range(len(tv)):
            lab = lab_of[i]
            if lab is None:
                continue
            k = pos.get(lab._epoch_us())
            if k is not None:
                groups[k].append(i)
        self._labels = labels
        self._groups = groups
        if isinstance(off, Tick) and self.label == 'right':
            self._outlabels = [T.ts_from_us(l._epoch_us() + step) for l in labels]
        elif isinstance(off, Tick):
            self._outlabels = labels
        else:
            self._outlabels = labels

    def _index(self):
        return DatetimeIndex(list(self._outlabels), name=self._idxname(), _raw=True, _dt='datetime64[us]',
                             freq=self.off)

    def _idxname(self):
        if self.on is not None:
            return self.on
        return self.obj._idx.name

    # -- aggregation
    def _cols(self):
        o = self.obj
        if not self._isdf:
            return [(o.name, o)]
        out = []
        for j in range(len(o._vals)):
            nm = o._cols._v[j]
            if self.on is not None and nm == self.on:
                continue
            out.append((nm, o._col(j)))
        return out

    def _sub(self, s, g):
        return Series([s._v[i] for i in g], Index([s._idx._v[i] for i in g]), name=s.name, _dt=s._dt)

    def _one(self, s, how, *a, **kw):
        res = []
        for g in self._groups:
            if len(g) == 0:
                if how in ('sum', 'count', 'size', 'nunique'):
                    res.append(0 if how != 'sum' or s._dt != 'float64' else 0.0)
                elif how == 'prod':
                    res.append(1)
                else:
                    res.append(nan)
                continue
            sub = self._sub(s, g)
            if how == 'size':
                res.append(len(g))
            elif how == 'first':
                nn = [x for x in sub._v if x == x]
                res.append(nn[0] if nn else nan)
            elif how == 'last':
                nn = [x for x in sub._v if x == x]
                res.append(nn[-1] if nn else nan)
            elif callable(how):
                res.append(how(sub))
            else:
                res.append(getattr(sub, how)(*a, **kw))
        return res

    def _run(self, how, *a, **kw):
        cols = self._cols()
        ix = self._index()
        if not self._isdf:
            s = cols[0][1]
            vals = self._one(s, how, *a, **kw)
            return Series(vals, ix, name=s.name)
        data = {}
        keep = []
        for nm, s in cols:
            if how in ('mean', 'median', 'std', 'var', 'sem') and s._dt in ('str', 'object'):
                continue
            data[nm] = self._one(s, how, *a, **kw)
            keep.append(nm)
        if how == 'size':
            return Series(self._one(cols[0][1], 'size'), ix)
        return DataFrame(data, index=ix, columns=keep) if keep else DataFrame({}, index=ix)

    def sum(self, *a, **kw):
        return self._run('sum')

    def mean(self, *a, **kw):
        return self._run('mean')

    def median(self, *a, **kw):
        return self._run('median')

    def min(self, *a, **kw):
        return self._run('min')

    def max(self, *a, **kw):
        return self._run('max')

    def count(self):
        return self._run('count')

    def size(self):
        return self._run('size')

    def first(self, *a, **kw):
        return self._run('first')

    def last(self, *a, **kw):
        return self._run('last')

    def std(self, ddof=1, *a, **kw):
        return self._run('std', ddof=ddof)

    def var(self, ddof=1, *a, **kw):
        return self._run('var', ddof=ddof)

    def prod(self, *a, **kw):
        return self._run('prod')

    def nunique(self):
        return self._run('nunique')

    def sem(self, *a, **kw):
        return self._run('sem')

    def ohlc(self):
        cols = self._cols()
        s = cols[0][1]
        o = []
        h = []
        l = []
        c = []
        for g in self._groups:
            nn = [s._v[i] for i in g if s._v[i] == s._v[i]]
            if nn:
                o.append(nn[0])
                h.append(max(nn))
                l.append(min(nn))
                c.append(nn[-1])
            else:
                o.append(nan)
                h.append(nan)
                l.append(nan)
                c.append(nan)
        return DataFrame({'open': o, 'high': h, 'low': l, 'close': c}, index=self._index())

    def apply(self, func, *a, **kw):
        return self.agg(func)

    def agg(self, func=None, *args, **kwargs):
        if isinstance(func, str):
            return getattr(self, func)(*args, **kwargs)
        if isinstance(func, (list, tuple)):
            parts = {}
            for f in func:
                nm = f if isinstance(f, str) else getattr(f, '__name__', str(f))
                parts[nm] = self.agg(f)
            if not self._isdf:
                return DataFrame({k: v._v for k, v in parts.items()}, index=self._index())
            from .reshape import concat
            return concat(list(parts.values()), axis=1, keys=list(parts.keys())) if False else DataFrame(
                {(c, k): parts[k][c]._v for k in parts for c in parts[k].columns._v}, index=self._index())
        if isinstance(func, dict):
            data = {}
            for k, f in func.items():
                s = self.obj[k]
                sub = Resampler(Series(s._v, s._idx, name=k, _dt=s._dt) if self.on is None else self.obj, self.off, self.closed, self.label, self.on, 'start_day')
                data[k] = sub._one(s, f) if not isinstance(f, str) else sub._one(s, f)
            return DataFrame(data, index=self._index())
        if func is None:
            for k, f in kwargs.items():
                pass
            data = {}
            cols = self._cols()
            for name, spec in kwargs.items():
                col, f = spec
                s = self.obj[col] if self._isdf else self.obj
                data[name] = self._one(s, f)
            return DataFrame(data, index=self._index())
        return self._run(func)

    aggregate = agg

    # -- filling
    def asfreq(self, fill_value=None):
        o = self.obj
        ix = self._index()
        cols = self._cols()
        pos = {}
        for i in range(len(self._tv)):
            pos[self._tv[i]] = i
        out = []
        for nm, s in cols:
            vals = []
            for lab in self._outlabels:
                k = pos.get(lab)
                vals.append(s._v[k] if k is not None else (nan if fill_value is None else fill_value))
            out.append(vals)
        if not self._isdf:
            return Series(out[0], ix, name=cols[0][0])
        return DataFrame({cols[j][0]: out[j] for j in range(len(cols))}, index=ix)

    def _fill(self, how):
        cols = self._cols()
        ix = self._index()
        tv = self._tv
        order = sorted([i for i in range(len(tv)) if tv[i] == tv[i]], key=lambda i: tv[i])
        outs = []
        for nm, s in cols:
            vals = []
            for lab in self._outlabels:
                pick = None
                if how == 'ffill':
                    for i in order:
                        if tv[i] <= lab:
                            pick = i
                        else:
                            break
                else:
                    for i in order:
                        if tv[i] >= lab:
                            pick = i
                            break
                vals.append(s._v[pick] if pick is not None else nan)
            outs.append(vals)
        if not self._isdf:
            return Series(outs[0], ix, name=cols[0][0])
        return DataFrame({cols[j][0]: outs[j] for j in range(len(cols))}, index=ix)

    def ffill(self, limit=None):
        return self._fill('ffill')

    pad = ffill

    def bfill(self, limit=None):
        return self._fill('bfill')

    backfill = bfill

    def __getitem__(self, key):
        if isinstance(key, list):
            sub = self.obj[key] if self.on is None else self.obj[key + [self.on]]
        else:
            sub = self.obj[key]
        r = Resampler(sub, self.off, self.closed, self.label, self.on, 'start_day')
        return r

    def interpolate(self, method='linear', **kw):
        return self.asfreq().interpolate(method=method)

    def __iter__(self):
        o = self.obj
        for lab, g in zip(self._outlabels, self._groups):
            if self._isdf:
                yield lab, o.iloc[g]
            else:
                yield lab, o.iloc[g]


def asfreq(obj, freq, method=None, normalize=False, fill_value=None):
    off = to_offset(freq)
    iv = [x for x in obj._idx._v if x == x]
    if not iv:
        return obj
    first = min(iv)
    last = max(iv)
    from .dtindex import date_range
    rng = date_range(first.normalize() if normalize else first, last, freq=off)
    if isinstance(obj, Series):
        r = obj.reindex(rng) if fill_value is None else obj.reindex(rng, fill_value=fill_value)
        if method in ('ffill', 'pad'):
            r = r.ffill()
        elif method == 'bfill':
            r = r.bfill()
        r._idx = rng
        return r
    r = obj.reindex(rng)
    if method in ('ffill', 'pad'):
        r = r.ffill()
    elif method == 'bfill':
        r = r.bfill()
    r._idx = rng
    return r


def truncate(obj, before=None, after=None):
    ix = obj._idx
    if ix._dt.startswith('datetime'):
        ps = dt_slice_positions(ix, before, after, len(ix._v))
        return obj.iloc[ps]
    ps = [i for i in range(len(ix._v)) if (before is None or ix._v[i] >= before) and (after is None or ix._v[i] <= after)]
    return obj.iloc[ps]
