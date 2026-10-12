# pandas subset: groupby
import math
import numpy as np
from .base import nan, infer, Index, RangeIndex, MultiIndex, norm_dtype, dtype_of, mkindex
from .core import Series, DataFrame, _lred, _lst, _is_listlike, _NUMDT, _quant

_AGGS = ('sum', 'mean', 'min', 'max', 'count', 'size', 'std', 'var', 'median', 'first', 'last', 'nunique', 'prod',
         'sem', 'any', 'all', 'mode')
_NUMONLY = ('mean', 'std', 'var', 'median', 'sem', 'prod')
_ALIAS = {'amax': 'max', 'amin': 'min', 'len': 'size', 'average': 'mean', 'nanmean': 'mean', 'nansum': 'sum',
          'nanmax': 'max', 'nanmin': 'min', 'nanstd': 'std', 'nanmedian': 'median', 'nanvar': 'var'}


class NamedAgg:
    def __init__(self, column, aggfunc):
        self.column = column
        self.aggfunc = aggfunc

    def __iter__(self):
        return iter((self.column, self.aggfunc))

    def __getitem__(self, i):
        return (self.column, self.aggfunc)[i]


def _fname(f):
    if isinstance(f, str):
        return f
    n = getattr(f, '__name__', None)
    if n is None:
        n = str(f)
    return n


def _std_name(f):
    """map a function spec to a builtin aggregation name or None"""
    if isinstance(f, str):
        return f if f in _AGGS else None
    n = getattr(f, '__name__', None)
    if n is None:
        return None
    n = _ALIAS.get(n, n)
    if n in _AGGS and (n != 'sum' or True):
        if n in ('mean', 'median', 'std', 'var', 'amax', 'amin') and getattr(f, '__module__', '') == '__main__':
            return None
        if n in ('first', 'last', 'size', 'count', 'nunique', 'mode', 'sem') and getattr(f, '__module__', '') == '__main__':
            return None
        return n
    return None


class _Base:
    def __init__(self, obj, by, level, as_index, sort, dropna, selection=None):
        self.obj = obj
        self.as_index = as_index
        self.sort = sort
        self.dropna = dropna
        self._sel = selection
        self._isdf = isinstance(obj, DataFrame)
        arrays = []
        names = []
        self._keycols = []
        n = len(obj.index)
        if level is not None:
            lv = level if isinstance(level, list) else [level]
            for L in lv:
                ix = obj.index
                if ix.nlevels > 1:
                    k = L if isinstance(L, int) else ix.names.index(L)
                    arrays.append([t[k] for t in ix._v])
                    names.append(ix.names[k])
                else:
                    arrays.append(list(ix._v))
                    names.append(ix.name)
        else:
            bl = by if isinstance(by, list) else [by]
            for b in bl:
                if type(b).__name__ == 'Grouper':
                    from .resample import assign_labels, default_closed
                    from .tslib import to_offset
                    if b.key is not None:
                        tv = list((obj[b.key] if self._isdf else obj)._v)
                        if self._isdf:
                            self._keycols.append(b.key)
                    else:
                        tv = list(obj.index._v)
                    off = to_offset(b.freq)
                    arrays.append(assign_labels(tv, off, b.closed if b.closed is not None else default_closed(off)))
                    names.append(b.key)
                    self._gfreq = off
                elif self._isdf and not isinstance(b, (Series, np.ndarray, list, Index)) and not callable(b) and not isinstance(b, dict) and b in obj.columns:
                    j = obj._colpos(b)
                    arrays.append(obj._vals[j])
                    names.append(b)
                    self._keycols.append(b)
                elif isinstance(b, Series):
                    arrays.append(list(b._v))
                    names.append(b.name)
                elif isinstance(b, (list, np.ndarray, Index)):
                    arrays.append(list(_lst(b)))
                    names.append(getattr(b, 'name', None))
                elif callable(b):
                    arrays.append([b(x) for x in obj.index._v])
                    names.append(None)
                elif isinstance(b, dict):
                    arrays.append([b.get(x, nan) for x in obj.index._v])
                    names.append(None)
                else:
                    if not self._isdf and False:
                        pass
                    raise KeyError(b)
        self._arrays = arrays
        self._names = names
        self._build_groups(n)

    def _build_groups(self, n):
        arrays = self._arrays
        groups = {}
        order = []
        if len(arrays) == 1:
            a = arrays[0]
            for i in range(n):
                k = a[i]
                if k is None or k != k:
                    if self.dropna:
                        continue
                if k in groups:
                    groups[k].append(i)
                else:
                    groups[k] = [i]
                    order.append(k)
        else:
            for i in range(n):
                k = tuple([a[i] for a in arrays])
                if self.dropna:
                    bad = False
                    for x in k:
                        if x is None or x != x:
                            bad = True
                            break
                    if bad:
                        continue
                if k in groups:
                    groups[k].append(i)
                else:
                    groups[k] = [i]
                    order.append(k)
        if self.sort:
            try:
                order = sorted(order)
            except Exception:
                pass
        self._groups = groups
        self._order = order

    # ---- keys / index
    def _gindex(self, keys=None):
        keys = self._order if keys is None else keys
        if len(self._arrays) == 1:
            r = mkindex(list(keys), name=self._names[0])
            gf = getattr(self, '_gfreq', None)
            if gf is not None and hasattr(r, '_freq'):
                from .dtindex import _regular
                if _regular(r._v, gf):
                    r._freq = gf
            return r
        return MultiIndex(_tuples=list(keys), names=self._names)

    @property
    def ngroups(self):
        return len(self._order)

    @property
    def groups(self):
        ix = self.obj.index._v
        return {k: Index([ix[i] for i in self._groups[k]]) for k in self._order}

    @property
    def indices(self):
        return {k: np.array(self._groups[k]) for k in self._order}

    def __len__(self):
        return len(self._order)

    def _value_cols(self):
        if not self._isdf:
            return [None]
        obj = self.obj
        if self._sel is not None:
            return [obj._colpos(c) for c in self._sel]
        skip = set(self._keycols)
        return [j for j in range(len(obj._vals)) if obj._cols._v[j] not in skip]

    def _subvals(self, j, pos):
        v = self.obj._vals[j] if self._isdf else self.obj._v
        return [v[i] for i in pos]

    def _dtof(self, j):
        return self.obj._dts[j] if self._isdf else self.obj._dt

    def _finish(self, names, cols, dts=None, series_name=None):
        """assemble result from per-column result lists (one value per group)"""
        gi = self._gindex()
        if not self._isdf or isinstance(self._sel_single(), bool) and self._sel_single():
            pass
        return None

    def _sel_single(self):
        return False

    def get_group(self, name, obj=None):
        pos = self._groups[name]
        return self.obj._take_rows(pos) if self._isdf else self.obj._take(pos)

    def __iter__(self):
        out = []
        for k in self._order:
            pos = self._groups[k]
            out.append((k, self.obj._take_rows(pos) if self._isdf else self.obj._take(pos)))
        return iter(out)

    # ---- aggregation core
    def _agg_one(self, j, spec):
        """aggregate column j with spec (name or callable) -> list of per-group values"""
        nm = _std_name(spec)
        out = []
        v = self.obj._vals[j] if self._isdf else self.obj._v
        if nm is not None:
            if nm == 'size':
                nm = 'size'
            for k in self._order:
                pos = self._groups[k]
                sub = [v[i] for i in pos]
                out.append(_lred(nm, sub))
            return out
        nmv = self.obj._cols._v[j] if self._isdf else self.obj.name
        dt = self._dtof(j)
        ix = self.obj.index._v
        for k in self._order:
            pos = self._groups[k]
            s = Series([v[i] for i in pos], Index([ix[i] for i in pos]), name=nmv, _dt=dt) if dt != 'object' else Series([v[i] for i in pos], Index([ix[i] for i in pos]), name=nmv)
            out.append(spec(s))
        return out

    def _result_frame(self, cols, names):
        """cols: list of lists (per group); names: column labels -> DataFrame / reset"""
        gi = self._gindex()
        vals = []
        dts = []
        for c in cols:
            v, dt = infer(list(c))
            vals.append(v)
            dts.append(dt)
        cx = Index(names) if not (names and isinstance(names[0], tuple)) else MultiIndex(_tuples=names, names=[None] * len(names[0]))
        df = DataFrame._fast(vals, dts, cx, gi)
        if not self.as_index:
            df = df.reset_index()
        return df

    def _named(self, name):
        js = self._value_cols()
        if not self._isdf:
            cols = [self._agg_one(None, name)]
            v, dt = infer(cols[0])
            r = Series(v, self._gindex(), name=self.obj.name, _dt=dt)
            if not self.as_index:
                return r.reset_index()
            return r
        names = []
        cols = []
        for j in js:
            if name in _NUMONLY and self.obj._dts[j] not in _NUMDT:
                continue
            names.append(self.obj._cols._v[j])
            cols.append(self._agg_one(j, name))
        return self._result_frame(cols, names)

    def sum(self, numeric_only=False, min_count=0):
        return self._named('sum')

    def mean(self, numeric_only=False):
        return self._named('mean')

    def median(self, numeric_only=False):
        return self._named('median')

    def min(self, numeric_only=False):
        return self._named('min')

    def max(self, numeric_only=False):
        return self._named('max')

    def count(self):
        return self._named('count')

    def first(self, numeric_only=False):
        return self._named('first')

    def last(self, numeric_only=False):
        return self._named('last')

    def nunique(self, dropna=True):
        return self._named('nunique')

    def prod(self, numeric_only=False):
        return self._named('prod')

    def any(self):
        return self._named('any')

    def all(self):
        return self._named('all')

    def std(self, ddof=1, numeric_only=False):
        if ddof == 1:
            return self._named('std')
        return self._agg_ddof('std', ddof)

    def var(self, ddof=1, numeric_only=False):
        if ddof == 1:
            return self._named('var')
        return self._agg_ddof('var', ddof)

    def _agg_ddof(self, name, ddof):
        js = self._value_cols()
        cols = []
        names = []
        for j in js:
            if self.obj._dts[j] not in _NUMDT if self._isdf else self.obj._dt not in _NUMDT:
                continue
            v = self.obj._vals[j] if self._isdf else self.obj._v
            cols.append([_lred(name, [v[i] for i in self._groups[k]], ddof) for k in self._order])
            names.append(self.obj._cols._v[j] if self._isdf else self.obj.name)
        if not self._isdf:
            r = Series(cols[0], self._gindex(), name=self.obj.name)
            return r
        return self._result_frame(cols, names)

    def sem(self):
        return self._named('sem')

    def mode(self):
        return self._named('mode')

    def size(self):
        gi = self._gindex()
        vals = [len(self._groups[k]) for k in self._order]
        if self.as_index:
            return Series(vals, gi, _dt='int64')
        df = DataFrame._fast([vals], ['int64'], Index(['size']), gi)
        return df.reset_index()

    def ngroup(self):
        n = len(self.obj.index)
        res = [-1] * n
        for g, k in enumerate(self._order):
            for i in self._groups[k]:
                res[i] = g
        return Series(res, self.obj.index, _dt='int64')

    def cumcount(self, ascending=True):
        n = len(self.obj.index)
        res = [0] * n
        for k in self._order:
            pos = self._groups[k]
            m = len(pos)
            for r, i in enumerate(pos):
                res[i] = r if ascending else m - 1 - r
        return Series(res, self.obj.index, _dt='int64')

    def quantile(self, q=0.5, numeric_only=False):
        js = self._value_cols()
        cols = []
        names = []
        for j in js:
            dt = self._dtof(j)
            if dt not in _NUMDT:
                continue
            v = self.obj._vals[j] if self._isdf else self.obj._v
            cols.append([_quant(sorted([x for x in [v[i] for i in self._groups[k]] if x == x]), q) for k in self._order])
            names.append(self.obj._cols._v[j] if self._isdf else self.obj.name)
        if not self._isdf:
            return Series(cols[0], self._gindex(), name=self.obj.name)
        return self._result_frame(cols, names)

    def describe(self):
        parts = {}
        js = self._value_cols()
        frames = []
        for k in self._order:
            pos = self._groups[k]
            frames.append(self.obj._take_rows(pos) if self._isdf else self.obj._take(pos))
        from .reshape import concat
        res = []
        for k, f in zip(self._order, frames):
            d = f.describe() if self._isdf else f.describe().to_frame().T
            res.append(d)
        out = {}
        labs = list(res[0].columns._v) if res else []
        return None

    # ---- agg
    def agg(self, func=None, *args, **kwargs):
        if func is None and kwargs:
            return self._named_agg(kwargs)
        if not self._isdf:
            return self._agg_series(func)
        js = self._value_cols()
        obj = self.obj
        if isinstance(func, dict):
            anylist = False
            for f in func.values():
                if isinstance(f, (list, tuple)):
                    anylist = True
            cols = []
            names = []
            for c, f in func.items():
                j = obj._colpos(c)
                if isinstance(f, (list, tuple)):
                    for g in f:
                        cols.append(self._agg_one(j, g))
                        names.append((c, _fname(g)))
                elif anylist:
                    cols.append(self._agg_one(j, f))
                    names.append((c, _fname(f)))
                else:
                    cols.append(self._agg_one(j, f))
                    names.append(c)
            return self._result_frame(cols, names)
        if isinstance(func, (list, tuple)):
            cols = []
            names = []
            for j in js:
                for g in func:
                    nm = _std_name(g)
                    if nm in _NUMONLY and obj._dts[j] not in _NUMDT:
                        continue
                    cols.append(self._agg_one(j, g))
                    names.append((obj._cols._v[j], _fname(g)))
            return self._result_frame(cols, names)
        cols = []
        names = []
        nm = _std_name(func)
        for j in js:
            if nm in _NUMONLY and obj._dts[j] not in _NUMDT:
                continue
            cols.append(self._agg_one(j, func))
            names.append(obj._cols._v[j])
        return self._result_frame(cols, names)

    aggregate = agg

    def _agg_series(self, func):
        if isinstance(func, (list, tuple)):
            cols = []
            names = []
            for g in func:
                cols.append(self._agg_one(None, g))
                names.append(_fname(g))
            return self._result_frame(cols, names)
        if isinstance(func, dict):
            cols = []
            names = []
            for k, g in func.items():
                cols.append(self._agg_one(None, g))
                names.append(k)
            return self._result_frame(cols, names)
        col = self._agg_one(None, func)
        v, dt = infer(col)
        r = Series(v, self._gindex(), name=self.obj.name, _dt=dt)
        if not self.as_index:
            return r.reset_index()
        return r

    def _named_agg(self, spec):
        cols = []
        names = []
        for out, ca in spec.items():
            if self._isdf:
                c, f = ca[0], ca[1]
                cols.append(self._agg_one(self.obj._colpos(c), f))
            else:
                cols.append(self._agg_one(None, ca))
            names.append(out)
        return self._result_frame(cols, names)

    # ---- transform-like
    def _per_group_series(self, j, f):
        """apply f(Series)->list-like of same length per group; returns full-length list"""
        obj = self.obj
        v = obj._vals[j] if self._isdf else obj._v
        dt = self._dtof(j)
        n = len(obj.index)
        res = [nan] * n
        ix = obj.index._v
        nmv = obj._cols._v[j] if self._isdf else obj.name
        for k in self._order:
            pos = self._groups[k]
            s = Series([v[i] for i in pos], Index([ix[i] for i in pos]), name=nmv, _dt=dt)
            r = f(s)
            rl = r._v if isinstance(r, Series) else (list(_lst(r)) if _is_listlike(r) else [r] * len(pos))
            for i, x in zip(pos, rl):
                res[i] = x
        return res

    def _xform(self, f, numeric=False):
        obj = self.obj
        if not self._isdf:
            res = self._per_group_series(None, f)
            v, dt = infer(res)
            return Series(v, obj.index, name=obj.name, _dt=dt)
        cols = []
        dts = []
        names = []
        for j in self._value_cols():
            if numeric and obj._dts[j] not in _NUMDT:
                continue
            v, dt = infer(self._per_group_series(j, f))
            cols.append(v)
            dts.append(dt)
            names.append(obj._cols._v[j])
        return DataFrame._fast(cols, dts, Index(names), obj.index)

    def transform(self, func, *args, **kwargs):
        if isinstance(func, str) or _std_name(func) is not None:
            nm = func if isinstance(func, str) else _std_name(func)
            if nm in _AGGS:
                return self._xform(lambda s: _lred(nm, s._v), nm in _NUMONLY)
            return self._xform(lambda s: getattr(s, nm)(*args, **kwargs))
        return self._xform(lambda s: func(s, *args, **kwargs))

    def cumsum(self):
        return self._xform(lambda s: s.cumsum(), True)

    def cumprod(self):
        return self._xform(lambda s: s.cumprod(), True)

    def cummax(self):
        return self._xform(lambda s: s.cummax(), True)

    def cummin(self):
        return self._xform(lambda s: s.cummin(), True)

    def shift(self, periods=1, fill_value=nan):
        return self._xform(lambda s: s.shift(periods, fill_value=fill_value))

    def diff(self, periods=1):
        return self._xform(lambda s: s.diff(periods), True)

    def pct_change(self, periods=1):
        return self._xform(lambda s: s.pct_change(periods), True)

    def rank(self, method='average', ascending=True, pct=False):
        return self._xform(lambda s: s.rank(method=method, ascending=ascending, pct=pct), True)

    def ffill(self):
        return self._xform(lambda s: s.ffill())

    def bfill(self):
        return self._xform(lambda s: s.bfill())

    def fillna(self, value=None):
        return self._xform(lambda s: s.fillna(value))

    def head(self, n=5):
        pos = []
        for k in self._order:
            pos.extend(self._groups[k][:n])
        pos.sort()
        return self.obj._take_rows(pos) if self._isdf else self.obj._take(pos)

    def tail(self, n=5):
        pos = []
        for k in self._order:
            pos.extend(self._groups[k][-n:] if n > 0 else [])
        pos.sort()
        return self.obj._take_rows(pos) if self._isdf else self.obj._take(pos)

    def nth(self, n):
        pos = []
        for k in self._order:
            g = self._groups[k]
            if -len(g) <= n < len(g):
                pos.append(g[n])
        pos.sort()
        return self.obj._take_rows(pos) if self._isdf else self.obj._take(pos)

    def filter(self, func, dropna=True):
        pos = []
        for k in self._order:
            g = self._groups[k]
            sub = self.obj._take_rows(g) if self._isdf else self.obj._take(g)
            if func(sub):
                pos.extend(g)
        pos.sort()
        return self.obj._take_rows(pos) if self._isdf else self.obj._take(pos)

    def apply(self, func, *args, **kwargs):
        obj = self.obj
        results = []
        keycols = set(self._keycols)
        for k in self._order:
            pos = self._groups[k]
            if self._isdf:
                sub = obj._take_rows(pos)
                if keycols and self._sel is None:
                    sub = sub._take_cols([j for j in range(len(sub._vals)) if sub._cols._v[j] not in keycols])
                elif self._sel is not None:
                    sub = sub._take_cols([obj._colpos(c) for c in self._sel])
            else:
                sub = obj._take(pos)
            results.append(func(sub, *args, **kwargs))
        if not results:
            return Series([], dtype='float64')
        first = results[0]
        keys = list(self._order)
        if isinstance(first, DataFrame) or isinstance(first, Series) and False:
            from .reshape import concat
            return concat(results, keys=keys, names=self._names)
        if isinstance(first, Series):
            same = True
            for r in results:
                if r._idx._v != first._idx._v:
                    same = False
            ingroup = False
            gpos = self._groups[self._order[0]]
            if first._idx._v == [obj.index._v[i] for i in gpos]:
                ingroup = True
            if same and not ingroup:
                cols = {}
                for c in first._idx._v:
                    cols[c] = [r._v[r._idx._mkpos()[c]] for r in results]
                df = DataFrame(cols, index=self._gindex())
                return df
            from .reshape import concat
            return concat(results, keys=keys, names=self._names)
        v, dt = infer(results)
        r = Series(v, self._gindex(), name=(None if self._isdf else obj.name), _dt=dt)
        if not self.as_index:
            return r.reset_index()
        return r

    def pipe(self, func, *a, **k):
        return func(self, *a, **k)

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)
        if self._isdf and name in self.obj.columns:
            return self[name]
        raise AttributeError(name)

    def __getitem__(self, key):
        obj = self.obj
        if isinstance(key, (list, tuple)):
            return DataFrameGroupBy(obj, None, None, self.as_index, self.sort, self.dropna, list(key), self)
        s = obj[key]
        return SeriesGroupBy(s, None, None, self.as_index, self.sort, self.dropna, None, self, key)

    def rolling(self, window, **kw):
        raise NotImplementedError('groupby.rolling')


class DataFrameGroupBy(_Base):
    def __init__(self, obj, by, level, as_index=True, sort=True, dropna=True, selection=None, parent=None):
        if parent is not None:
            self.obj = obj
            self.as_index = as_index
            self.sort = sort
            self.dropna = dropna
            self._sel = selection
            self._isdf = True
            self._arrays = parent._arrays
            self._names = parent._names
            self._keycols = parent._keycols
            self._gfreq = getattr(parent, '_gfreq', None)
            self._groups = parent._groups
            self._order = parent._order
        else:
            _Base.__init__(self, obj, by, level, as_index, sort, dropna, selection)


class SeriesGroupBy(_Base):
    def __init__(self, obj, by, level, as_index=True, sort=True, dropna=True, selection=None, parent=None, key=None):
        if parent is not None:
            self.obj = obj
            self.as_index = as_index
            self.sort = sort
            self.dropna = dropna
            self._sel = None
            self._isdf = False
            self._arrays = parent._arrays
            self._names = parent._names
            self._keycols = parent._keycols
            self._gfreq = getattr(parent, '_gfreq', None)
            self._groups = parent._groups
            self._order = parent._order
        else:
            _Base.__init__(self, obj, by, level, as_index, sort, dropna, selection)

    def unique(self):
        gi = self._gindex()
        return Series([list(dict.fromkeys(self._subvals(None, self._groups[k])).keys()) for k in self._order], gi, name=self.obj.name)

    def value_counts(self):
        pass
