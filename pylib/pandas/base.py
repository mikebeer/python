# pandas subset: dtypes, missing values, Index classes (pure Python, runs on both interpreters)
import numpy as np

nan = float('nan')


class NAType:
    def __repr__(self):
        return '<NA>'

    def __bool__(self):
        raise TypeError('boolean value of NA is ambiguous')

    def __eq__(self, other):
        return self

    def __ne__(self, other):
        return self

    def __hash__(self):
        return 12345


NA = NAType()


_NAT_NAN = ('year month day hour minute second microsecond nanosecond dayofweek day_of_week dayofyear day_of_year '
            'quarter days_in_month daysinmonth week weekofyear days seconds microseconds nanoseconds value '
            'is_month_start is_month_end is_quarter_start is_quarter_end is_year_start is_year_end '
            'is_leap_year').split()
_NAT_NANF = 'weekday isoweekday toordinal timestamp total_seconds'.split()
_NAT_SELF = ('normalize floor ceil round replace tz_localize tz_convert to_pydatetime to_pytimedelta date time '
             'combine as_unit astimezone fromisoformat').split()
_NAT_STR = {'day_name': 'NaT', 'month_name': 'NaT', 'isoformat': 'NaT', 'strftime': 'NaT', 'ctime': 'NaT'}


class NaTType:
    def __repr__(self):
        return 'NaT'

    def __str__(self):
        return 'NaT'

    def __hash__(self):
        return 54321

    def __eq__(self, o):
        return False

    def __ne__(self, o):
        return True

    def __lt__(self, o):
        return False

    def __le__(self, o):
        return False

    def __gt__(self, o):
        return False

    def __ge__(self, o):
        return False

    def __bool__(self):
        return True

    def __add__(self, o):
        return self

    __radd__ = __add__

    def __sub__(self, o):
        return self

    __rsub__ = __sub__

    def __mul__(self, o):
        return self

    __rmul__ = __mul__

    def __truediv__(self, o):
        if isinstance(o, (int, float)):
            return self
        return nan

    def __neg__(self):
        return self

    def __abs__(self):
        return self

    def __getattr__(self, name):
        if name in _NAT_NAN:
            return nan
        if name in _NAT_NANF:
            return lambda *a, **k: nan
        if name in _NAT_SELF:
            return lambda *a, **k: self
        if name in _NAT_STR:
            return lambda *a, **k: self if name != 'isoformat' else 'NaT'
        if name == 'tz' or name == 'tzinfo':
            return None
        raise AttributeError("'NaTType' object has no attribute '%s'" % name)


NaT = NaTType()


def _isser(x):
    return type(x).__name__ == 'Series'


def isna_scalar(x):
    return x is None or x != x or x is NA or x is NaT


def notna_scalar(x):
    return not (x is None or x != x or x is NA or x is NaT)


class Dtype:
    def __init__(self, name):
        self.name = name

    @property
    def kind(self):
        n = self.name
        if n == 'int64':
            return 'i'
        if n == 'float64':
            return 'f'
        if n == 'bool':
            return 'b'
        if n.startswith('datetime'):
            return 'M'
        return 'O'

    def __str__(self):
        return self.name

    def __repr__(self):
        if self.name == 'str':
            return "<StringDtype(storage='python', na_value=nan)>"
        if self.name == 'object':
            return "dtype('O')"
        return "dtype('%s')" % self.name

    def __eq__(self, other):
        if isinstance(other, Dtype):
            return other.name == self.name
        if isinstance(other, str):
            if other == self.name:
                return True
            if other in ('O', 'object'):
                return self.name == 'object'
            if other in ('int', 'i8', 'int64'):
                return self.name == 'int64'
            if other in ('float', 'f8', 'float64', 'double'):
                return self.name == 'float64'
            if other in ('string', 'str'):
                return self.name == 'str'
            return False
        if other is int:
            return self.name == 'int64'
        if other is float:
            return self.name == 'float64'
        if other is bool:
            return self.name == 'bool'
        if other is str:
            return self.name == 'str'
        if other is object:
            return self.name == 'object'
        nm = getattr(other, '__name__', None)
        if nm is not None:
            return nm == self.name or (nm == 'float' and self.name == 'float64')
        return False

    def __ne__(self, other):
        return not self.__eq__(other)

    def __hash__(self):
        return hash(self.name)


_dts = {}


def dtype_of(name):
    d = _dts.get(name)
    if d is None:
        d = Dtype(name)
        _dts[name] = d
    return d


def norm_dtype(dt):
    """user supplied dtype -> internal name"""
    if dt is None:
        return None
    if isinstance(dt, Dtype):
        return dt.name
    if dt is int:
        return 'int64'
    if dt is float:
        return 'float64'
    if dt is bool:
        return 'bool'
    if dt is str:
        return 'str'
    if dt is object:
        return 'object'
    if isinstance(dt, str):
        d = dt.lower()
        if d.startswith('int') or d.startswith('uint'):
            return 'int64'
        if d.startswith('float') or d == 'double':
            return 'float64'
        if d in ('bool', 'boolean'):
            return 'bool'
        if d in ('str', 'string', 'unicode'):
            return 'str'
        if d in ('object', 'o'):
            return 'object'
        if d.startswith('datetime64'):
            return d if '[' in d else 'datetime64[us]'
        if d.startswith('timedelta64'):
            return d if '[' in d else 'timedelta64[us]'
        if d == 'category':
            return 'object'
        return dt
    nm = getattr(dt, '__name__', None)
    if nm is not None:
        return norm_dtype(nm)
    return str(dt)


def _is_datelike_type(t):
    if t is float or t is int or t is str or t is bool:
        return False
    n = getattr(t, '__name__', '')
    if n in ('datetime', 'Timestamp', 'timedelta', 'Timedelta'):
        return True
    for b in ('datetime', 'timedelta'):
        try:
            import datetime as _d
            if issubclass(t, getattr(_d, b)):
                return True
        except TypeError:
            pass
    return False


def infer(vals):
    """returns (values, dtype name); values may be a converted copy"""
    if len(vals) == 0:
        return vals, 'object'
    ts = set(map(type, vals))
    has_none = False
    if type(None) in ts:
        has_none = True
        ts.discard(type(None))
    if NAType in ts:
        has_none = True
        ts.discard(NAType)
    if NaTType in ts:
        ts.discard(NaTType)
        if len(ts) == 0:
            return [NaT if (x is None or x is NA) else x for x in vals], 'datetime64[s]'
        has_none = True
    if len(ts) == 0:
        return vals, 'object'
    for t in ts:
        if _is_datelike_type(t):
            from . import tslib
            r = tslib.infer_datelike(vals)
            if r is not None:
                return r
            return vals, 'object'
    if len(ts) == 1:
        t = list(ts)[0]
        if t is int and not has_none:
            return vals, 'int64'
        if t is bool and not has_none:
            return vals, 'bool'
        if t is float:
            if has_none:
                vals = [nan if (x is None or x is NA) else x for x in vals]
            return vals, 'float64'
        if t is int:
            return [nan if (x is None or x is NA) else float(x) for x in vals], 'float64'
        if t is str:
            if has_none:
                vals = [nan if (x is None or x is NA) else x for x in vals]
            return vals, 'str'
    elif len(ts) == 2 and int in ts and float in ts:
        return [nan if (x is None or x is NA) else float(x) for x in vals], 'float64'
    elif len(ts) == 2 and str in ts and float in ts:
        ok = True
        for x in vals:
            if type(x) is float and x == x:
                ok = False
                break
        if ok:
            if has_none:
                vals = [nan if (x is None or x is NA) else x for x in vals]
            return vals, 'str'
    return vals, 'object'


def is_numeric_dt(dt):
    return dt == 'int64' or dt == 'float64' or dt == 'bool'


def sortkey_list(vals):
    """sorted copy of non-missing labels, falling back to original order if unorderable"""
    try:
        return sorted(vals)
    except Exception:
        return list(vals)


class Index:
    def __init__(self, data=None, dtype=None, copy=False, name=None, tupleize_cols=True, _norm=True):
        if data is None:
            data = []
        if isinstance(data, Index):
            if name is None:
                name = data.name
            data = data._v
        elif _isser(data):
            data = data._v
        elif isinstance(data, np.ndarray):
            data = data.tolist()
        elif not isinstance(data, list):
            data = list(data)
        if _norm:
            data, dt = infer(data)
            if dtype is not None:
                dn = norm_dtype(dtype)
                if dn != dt:
                    data = _astype_list(data, dt, dn)
                    dt = dn
            elif len(data) == 0:
                dt = 'object'
            self._dt = dt
        else:
            self._dt = 'object'
        self._v = data
        self.name = name
        self._pos = None

    @property
    def dtype(self):
        return dtype_of(self._dt)

    @property
    def names(self):
        return [self.name]

    @property
    def nlevels(self):
        return 1

    @property
    def values(self):
        return to_np(self._v, self._dt)

    @property
    def shape(self):
        return (len(self._v),)

    @property
    def size(self):
        return len(self._v)

    @property
    def ndim(self):
        return 1

    @property
    def empty(self):
        return len(self._v) == 0

    @property
    def is_unique(self):
        return len(self._mkpos()) == len(self._v)

    @property
    def hasnans(self):
        for x in self._v:
            if x != x:
                return True
        return False

    @property
    def is_monotonic_increasing(self):
        v = self._v
        for i in range(1, len(v)):
            if v[i] < v[i - 1]:
                return False
        return True

    @property
    def is_monotonic_decreasing(self):
        v = self._v
        for i in range(1, len(v)):
            if v[i] > v[i - 1]:
                return False
        return True

    def _mkpos(self):
        p = self._pos
        if p is None:
            p = {}
            i = 0
            for x in self._v:
                if x not in p:
                    p[x] = i
                i += 1
            self._pos = p
        return p

    def _new(self, v, name=None):
        r = Index(v, name=name)
        if r._dt.startswith('datetime') or r._dt.startswith('timedelta'):
            return r._as_dtindex()
        return r

    def _as_dtindex(self):
        from .dtindex import DatetimeIndex, TimedeltaIndex
        if self._dt.startswith('datetime'):
            return DatetimeIndex(self._v, name=self.name, _dt=self._dt, _raw=True)
        return TimedeltaIndex(self._v, name=self.name, _dt=self._dt, _raw=True)

    def __len__(self):
        return len(self._v)

    def __iter__(self):
        return iter(self._v)

    def __contains__(self, x):
        return x in self._mkpos()

    def __bool__(self):
        raise ValueError('The truth value of a Index is ambiguous. Use a.empty, a.bool(), a.item(), a.any() or a.all().')

    def tolist(self):
        return list(self._v)

    def to_list(self):
        return list(self._v)

    def to_numpy(self, dtype=None, copy=False):
        return self.values

    def to_series(self, index=None, name=None):
        from .core import Series
        return Series(list(self._v), index=self if index is None else index, name=name if name is not None else self.name)

    def to_frame(self, index=True, name=None):
        from .core import DataFrame
        nm = name if name is not None else (self.name if self.name is not None else 0)
        return DataFrame({nm: list(self._v)}, index=self if index else None)

    def copy(self, name=None, deep=False):
        r = self._new(list(self._v), self.name if name is None else name)
        return r

    def __getitem__(self, key):
        v = self._v
        if isinstance(key, slice):
            return self._new(v[key], self.name)
        if isinstance(key, int):
            return v[key]
        if isinstance(key, np.ndarray):
            key = key.tolist()
        if isinstance(key, list):
            if len(key) > 0 and isinstance(key[0], bool):
                return self._new([v[i] for i in range(len(v)) if key[i]], self.name)
            return self._new([v[i] for i in key], self.name)
        return v[key]

    def get_loc(self, key):
        p = self._mkpos()
        if key in p:
            if len(p) == len(self._v):
                return p[key]
            locs = [i for i in range(len(self._v)) if self._v[i] == key]
            if len(locs) == 1:
                return locs[0]
            return locs
        raise KeyError(key)

    def get_indexer(self, target):
        p = self._mkpos()
        if isinstance(target, Index):
            target = target._v
        return np.array([p.get(x, -1) for x in target])

    def _locs(self, key):
        """list of positions of a label"""
        p = self._mkpos()
        if key not in p:
            return []
        if len(p) == len(self._v):
            return [p[key]]
        return [i for i in range(len(self._v)) if self._v[i] == key]

    def equals(self, other):
        if not isinstance(other, Index):
            return False
        return self._v == other._v

    def identical(self, other):
        return self.equals(other) and self.name == other.name

    def __eq__(self, other):
        if isinstance(other, Index):
            other = other._v
        if isinstance(other, (list, tuple, np.ndarray)):
            ov = list(other)
            return np.array([a == b for a, b in zip(self._v, ov)])
        return np.array([a == other for a in self._v])

    def __ne__(self, other):
        r = self.__eq__(other)
        return ~r

    def __hash__(self):
        return id(self)

    def _cmp(self, other, f):
        if isinstance(other, Index):
            other = other._v
        if isinstance(other, list):
            return np.array([f(a, b) for a, b in zip(self._v, other)])
        return np.array([f(a, other) for a in self._v])

    def __lt__(self, o):
        return self._cmp(o, lambda a, b: a < b)

    def __le__(self, o):
        return self._cmp(o, lambda a, b: a <= b)

    def __gt__(self, o):
        return self._cmp(o, lambda a, b: a > b)

    def __ge__(self, o):
        return self._cmp(o, lambda a, b: a >= b)

    def __add__(self, other):
        if isinstance(other, Index):
            other = other._v
        if isinstance(other, list):
            return Index([a + b for a, b in zip(self._v, other)], name=self.name)
        return Index([a + other for a in self._v], name=self.name)

    def __radd__(self, other):
        return Index([other + a for a in self._v], name=self.name)

    def __sub__(self, other):
        return Index([a - other for a in self._v], name=self.name)

    def __mul__(self, other):
        return Index([a * other for a in self._v], name=self.name)

    def astype(self, dtype, copy=True):
        dn = norm_dtype(dtype)
        return Index(_astype_list(self._v, self._dt, dn), name=self.name)

    def map(self, f):
        if isinstance(f, dict):
            return Index([f.get(x, nan) for x in self._v], name=self.name)
        if _isser(f):
            d = dict(zip(f.index._v, f._v))
            return Index([d.get(x, nan) for x in self._v], name=self.name)
        return Index([f(x) for x in self._v], name=self.name)

    def rename(self, name, inplace=False):
        if inplace:
            self.name = name
            return None
        r = self.copy()
        r.name = name
        return r

    def set_names(self, names, level=None, inplace=False):
        if isinstance(names, (list, tuple)):
            names = names[0]
        return self.rename(names, inplace=inplace)

    def isin(self, values):
        if _isser(values) or isinstance(values, Index):
            values = values._v
        s = set(values)
        return np.array([x in s for x in self._v])

    def isna(self):
        return np.array([x != x for x in self._v])

    def notna(self):
        return np.array([x == x for x in self._v])

    def unique(self, level=None):
        seen = {}
        out = []
        for x in self._v:
            if x not in seen:
                seen[x] = 1
                out.append(x)
        return self._new(out, self.name)

    def nunique(self, dropna=True):
        return len(set([x for x in self._v if x == x]))

    def drop_duplicates(self, keep='first'):
        return self.unique()

    def duplicated(self, keep='first'):
        seen = {}
        out = []
        for x in self._v:
            out.append(x in seen)
            seen[x] = 1
        return np.array(out)

    def sort_values(self, ascending=True, return_indexer=False, key=None):
        order = sorted(range(len(self._v)), key=lambda i: self._v[i], reverse=not ascending)
        if not ascending:
            # keep stable order for ties like pandas
            order = sorted(range(len(self._v)), key=lambda i: self._v[i])
            order = order[::-1]
        r = self._new([self._v[i] for i in order], self.name)
        if return_indexer:
            return r, np.array(order)
        return r

    def argsort(self):
        return np.array(sorted(range(len(self._v)), key=lambda i: self._v[i]))

    def min(self):
        return min(self._v)

    def max(self):
        return max(self._v)

    def argmax(self):
        v = self._v
        return v.index(max(v))

    def argmin(self):
        v = self._v
        return v.index(min(v))

    def union(self, other, sort=None):
        if isinstance(other, Index):
            ov = other._v
        else:
            ov = list(other)
        seen = {}
        out = []
        for x in self._v + ov:
            if x not in seen:
                seen[x] = 1
                out.append(x)
        if sort is not False and self._v != ov:
            try:
                out = sorted(out)
            except Exception:
                pass
        return Index(out, name=self.name if (not isinstance(other, Index) or other.name == self.name) else None)

    def intersection(self, other, sort=False):
        ov = other._v if isinstance(other, Index) else list(other)
        s = set(ov)
        out = []
        seen = {}
        for x in self._v:
            if x in s and x not in seen:
                seen[x] = 1
                out.append(x)
        return Index(out, name=self.name)

    def difference(self, other, sort=None):
        ov = other._v if isinstance(other, Index) else list(other)
        s = set(ov)
        out = []
        seen = {}
        for x in self._v:
            if x not in s and x not in seen:
                seen[x] = 1
                out.append(x)
        return Index(out, name=self.name)

    def append(self, other):
        if isinstance(other, (list, tuple)):
            r = list(self._v)
            for o in other:
                r = r + list(o._v)
            return Index(r)
        return Index(self._v + other._v)

    def delete(self, loc):
        v = list(self._v)
        if isinstance(loc, int):
            del v[loc]
            return self._new(v, self.name)
        loc = set(loc)
        return self._new([x for i, x in enumerate(v) if i not in loc], self.name)

    def insert(self, loc, item):
        v = list(self._v)
        v.insert(loc, item)
        return Index(v, name=self.name)

    def take(self, idx):
        return self._new([self._v[i] for i in idx], self.name)

    def repeat(self, n):
        return self._new([x for x in self._v for _ in range(n)], self.name)

    def fillna(self, value=None):
        return Index([value if x != x else x for x in self._v], name=self.name)

    def dropna(self, how='any'):
        return Index([x for x in self._v if x == x], name=self.name)

    def where(self, cond, other=nan):
        c = cond.tolist() if isinstance(cond, np.ndarray) else (cond._v if _isser(cond) else list(cond))
        return Index([x if k else other for x, k in zip(self._v, c)], name=self.name)

    def value_counts(self):
        from .core import Series
        return Series(list(self._v)).value_counts()

    def slice_indexer(self, start=None, end=None):
        return slice(self._bound(start, False), self._bound(end, True))

    def _bound(self, lab, upper):
        if lab is None:
            return None
        v = self._v
        if lab in self._mkpos():
            locs = self._locs(lab)
            return locs[-1] + 1 if upper else locs[0]
        # not present: need sorted index
        if upper:
            n = 0
            for x in v:
                if x <= lab:
                    n += 1
            return n
        n = 0
        for x in v:
            if x < lab:
                n += 1
        return n

    def _format_labels(self):
        """list of label strings (as pandas displays an index)"""
        from .fmt import format_labels
        return format_labels(self._v, self._dt)

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)
        if self._dt.startswith('datetime') or self._dt.startswith('timedelta'):
            return getattr(self._as_dtindex(), name)
        raise AttributeError("'Index' object has no attribute '%s'" % name)

    @staticmethod
    def _ser(ix):
        from .core import Series
        return Series(list(ix._v), ix, name=ix.name, _dt=ix._dt)

    def __repr__(self):
        v = self._v
        if self._dt.startswith('datetime') or self._dt.startswith('timedelta'):
            return repr(self._as_dtindex())
        if len(v) > 100:
            body = ', '.join([repr(x) for x in v[:10]]) + ', ..., ' + ', '.join([repr(x) for x in v[-10:]])
        else:
            body = ', '.join([_irepr(x) for x in v])
        s = 'Index([' + body + '], dtype=' + repr(self._dt if self._dt != 'datetime64[us]' else 'datetime64[us]')
        if self.name is not None:
            s += ', name=' + repr(self.name)
        return s + ')'

    def __str__(self):
        return self.__repr__()

    def item(self):
        return self._v[0]

    def memory_usage(self, deep=False):
        return 8 * len(self._v)

    @property
    def T(self):
        return self


def mkindex(vals, name=None):
    """Index of the right class (DatetimeIndex / TimedeltaIndex for datetime-like values)"""
    r = Index(vals, name=name)
    if r._dt.startswith('datetime') or r._dt.startswith('timedelta'):
        return r._as_dtindex()
    return r


def _irepr(x):
    if isinstance(x, float) and x != x:
        return 'nan'
    return repr(x)


class RangeIndex(Index):
    def __init__(self, start=0, stop=None, step=1, name=None, dtype=None, copy=False):
        if stop is None:
            stop = start
            start = 0
        self.start = start
        self.stop = stop
        self.step = step
        self._v = list(range(start, stop, step))
        self._dt = 'int64'
        self.name = name
        self._pos = None

    def _new(self, v, name=None):
        n = len(v)
        if n == 0:
            return RangeIndex(0, 0, 1, name=name)
        if n == 1:
            return RangeIndex(v[0], v[0] + 1, 1, name=name)
        st = v[1] - v[0]
        if st != 0 and v[-1] - v[0] == st * (n - 1):
            ok = True
            if n > 2:
                for i in range(2, n):
                    if v[i] - v[i - 1] != st:
                        ok = False
                        break
            if ok:
                return RangeIndex(v[0], v[-1] + (1 if st > 0 else -1), st, name=name)
        return Index(v, name=name)

    def copy(self, name=None, deep=False):
        return RangeIndex(self.start, self.stop, self.step, name=self.name if name is None else name)

    def __repr__(self):
        s = 'RangeIndex(start=%d, stop=%d, step=%d' % (self.start, self.stop, self.step)
        if self.name is not None:
            s += ', name=' + repr(self.name)
        return s + ')'

    def _mkpos(self):
        p = self._pos
        if p is None:
            p = {}
            i = 0
            for x in self._v:
                p[x] = i
                i += 1
            self._pos = p
        return p


class MultiIndex(Index):
    def __init__(self, levels=None, codes=None, names=None, _tuples=None):
        if _tuples is not None:
            self._v = _tuples
        else:
            self._v = [tuple([lv[c] for lv, c in zip(levels, row)]) for row in zip(*codes)]
        n = len(self._v[0]) if len(self._v) > 0 else (len(levels) if levels else 0)
        if names is None:
            names = [None] * n
        self._names = list(names)
        self._dt = 'object'
        self._pos = None

    @property
    def name(self):
        return None

    @name.setter
    def name(self, v):
        pass

    @property
    def names(self):
        return list(self._names)

    @names.setter
    def names(self, v):
        self._names = list(v)

    @property
    def nlevels(self):
        return len(self._names)

    @classmethod
    def from_tuples(cls, tuples, names=None, sortorder=None):
        return MultiIndex(_tuples=[tuple(t) for t in tuples], names=names if names is not None else [None] * len(tuples[0]))

    @classmethod
    def from_arrays(cls, arrays, names=None, sortorder=None):
        arrays = [a._v if (_isser(a) or isinstance(a, Index)) else list(a) for a in arrays]
        if names is None:
            names = [getattr(a, 'name', None) for a in arrays]
        return MultiIndex(_tuples=list(zip(*arrays)), names=names)

    @classmethod
    def from_product(cls, iterables, names=None):
        res = [()]
        for it in iterables:
            it = it._v if (_isser(it) or isinstance(it, Index)) else list(it)
            res = [r + (x,) for r in res for x in it]
        return MultiIndex(_tuples=res, names=names if names is not None else [None] * len(iterables))

    def _new(self, v, name=None):
        return MultiIndex(_tuples=v, names=self._names)

    def copy(self, name=None, deep=False):
        return MultiIndex(_tuples=list(self._v), names=self._names)

    def get_level_values(self, level):
        if isinstance(level, str):
            level = self._names.index(level)
        return Index([t[level] for t in self._v], name=self._names[level])

    def droplevel(self, level=0):
        if isinstance(level, list):
            lv = [self._names.index(x) if isinstance(x, str) else x for x in level]
        else:
            lv = [self._names.index(level) if isinstance(level, str) else level]
        keep = [i for i in range(len(self._names)) if i not in lv]
        if len(keep) == 1:
            return Index([t[keep[0]] for t in self._v], name=self._names[keep[0]])
        return MultiIndex(_tuples=[tuple([t[i] for i in keep]) for t in self._v], names=[self._names[i] for i in keep])

    @property
    def levels(self):
        out = []
        for k in range(len(self._names)):
            out.append(Index(sortkey_list(list(dict.fromkeys([t[k] for t in self._v]).keys())), name=self._names[k]))
        return out

    def rename(self, name, inplace=False):
        if isinstance(name, str):
            name = [name]
        if inplace:
            self._names = list(name)
            return None
        return MultiIndex(_tuples=self._v, names=name)

    def set_names(self, names, level=None, inplace=False):
        return self.rename(names, inplace)

    def union(self, other, sort=None):
        r = Index.union(self, other, sort)
        return MultiIndex(_tuples=r._v, names=self._names)

    def unique(self, level=None):
        r = Index.unique(self)
        return r

    def to_frame(self, index=True, name=None):
        from .core import DataFrame
        nm = [n if n is not None else i for i, n in enumerate(self._names)]
        return DataFrame({nm[k]: [t[k] for t in self._v] for k in range(len(nm))}, index=self if index else None)

    def __repr__(self):
        return 'MultiIndex(%r, names=%r)' % (self._v, self._names)

    def _format_labels(self):
        return [str(t) for t in self._v]


def _astype_list(vals, dt, dn):
    if dn == dt:
        return list(vals)
    if dn == 'float64':
        return [float(x) if x == x else x for x in vals] if dt != 'object' else [nan if x is None else float(x) for x in vals]
    if dn == 'int64':
        return [int(x) for x in vals]
    if dn == 'str':
        return [nan if (x is None or x != x) else (x if isinstance(x, str) else str(x)) for x in vals]
    if dn == 'bool':
        return [bool(x) for x in vals]
    if dn == 'object':
        return list(vals)
    return list(vals)


def to_np(vals, dt):
    if dt == 'int64' or dt == 'float64' or dt == 'bool':
        return np.array(vals)
    from .core import ObjArray
    return ObjArray(list(vals))
