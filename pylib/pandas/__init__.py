# pandas subset in pure Python (runs on the C and CREXX interpreters, on top of the native numpy subset)
import numpy as np
from .base import (nan, NA, NaT, Index, RangeIndex, MultiIndex, Dtype, dtype_of, infer, isna_scalar, norm_dtype)
from .core import Series, DataFrame, ObjArray, _lst, _is_listlike, _quant
from .groupby import NamedAgg
from .reshape import concat, merge, pivot_table, pivot, melt, crosstab, get_dummies
from .pio import read_csv, read_table, read_json
from . import fmt as _fmt

__version__ = '3.0.5'
NaN = nan


def isna(obj):
    if isinstance(obj, (Series, DataFrame, Index)):
        return obj.isna()
    if isinstance(obj, (list, tuple, np.ndarray)):
        return np.array([isna_scalar(x) for x in _lst(obj)])
    return isna_scalar(obj)


isnull = isna


def notna(obj):
    r = isna(obj)
    if isinstance(r, bool):
        return not r
    if isinstance(r, np.ndarray):
        return ~r
    return ~r


notnull = notna


def to_numeric(arg, errors='raise', downcast=None, dtype_backend=None):
    scalar = not _is_listlike(arg)
    vals = [arg] if scalar else list(_lst(arg))
    out = []
    for x in vals:
        if isinstance(x, (int, float)) or x is None:
            out.append(nan if x is None else x)
            continue
        try:
            t = x.strip()
            try:
                out.append(int(t))
            except Exception:
                out.append(float(t))
        except Exception:
            if errors == 'coerce':
                out.append(nan)
            elif errors == 'ignore':
                return arg
            else:
                raise ValueError('Unable to parse string "%s" at position %d' % (x, len(out)))
    v, dt = infer(out)
    if scalar:
        return v[0]
    if isinstance(arg, Series):
        return Series(v, arg._idx, name=arg.name, _dt=dt)
    return Series(v, dtype=dt).values if not isinstance(arg, Series) else None


def unique(values):
    return Series(list(_lst(values))).unique()


def value_counts(values, **kw):
    return Series(list(_lst(values))).value_counts(**kw)


def factorize(values, sort=False):
    vals = list(_lst(values))
    seen = {}
    codes = []
    uniq = []
    for x in vals:
        if x != x:
            codes.append(-1)
            continue
        if x not in seen:
            seen[x] = len(uniq)
            uniq.append(x)
        codes.append(seen[x])
    if sort:
        order = sorted(range(len(uniq)), key=lambda i: uniq[i])
        remap = {old: new for new, old in enumerate(order)}
        uniq = [uniq[i] for i in order]
        codes = [remap[c] if c >= 0 else c for c in codes]
    return np.array(codes), Index(uniq)


def array(data, dtype=None):
    return Series(data, dtype=dtype).values


def cut(x, bins, right=True, labels=None, include_lowest=False, **kw):
    s = x if isinstance(x, Series) else Series(list(_lst(x)))
    if isinstance(bins, int):
        lo, hi = s.min(), s.max()
        w = (hi - lo) / bins
        edges = [lo + w * k for k in range(bins + 1)]
        edges[0] = lo - (hi - lo) * 0.001
        edges[-1] = hi
    else:
        edges = list(_lst(bins))
    n = len(edges) - 1
    if labels is None or labels is True:
        labs = []
        for k in range(n):
            a, b = edges[k], edges[k + 1]
            labs.append(('(%s, %s]' if right else '[%s, %s)') % (_cutnum(a), _cutnum(b)))
    elif labels is False:
        labs = list(range(n))
    else:
        labs = list(labels)
    res = []
    for v in s._v:
        r = nan
        if v == v:
            for k in range(n):
                a, b = edges[k], edges[k + 1]
                if right:
                    ok = (a < v <= b) or (include_lowest and k == 0 and v == a)
                else:
                    ok = (a <= v < b) or (include_lowest and k == n - 1 and v == b)
                if ok:
                    r = labs[k]
                    break
        res.append(r)
    return Series(res, s._idx, name=s.name)


def _cutnum(a):
    if a == int(a):
        return str(int(a))
    return '%g' % a


def qcut(x, q, labels=None, **kw):
    s = x if isinstance(x, Series) else Series(list(_lst(x)))
    qs = [k / q for k in range(q + 1)] if isinstance(q, int) else list(q)
    nn = sorted(s._nn().tolist())
    edges = [_quant(nn, p) for p in qs]
    edges[0] = edges[0] - 1e-9 * max(1, abs(edges[0]))
    r = cut(s, edges, labels=labels, right=True)
    return r


# ---- datetime support ------------------------------------------------------------------------
from .tslib import (Timestamp, Timedelta, to_offset, DateOffset, Day, Hour, Minute, Second, Milli, Micro, Nano, Week,
                    MonthEnd, MonthBegin, YearEnd, YearBegin, QuarterEnd, QuarterBegin, BusinessDay, BaseOffset)
from .dtindex import (DatetimeIndex, TimedeltaIndex, to_datetime, to_timedelta, date_range, bdate_range,
                      timedelta_range, infer_freq)


class _Offsets:
    pass


offsets = _Offsets()
for _n, _c in (('DateOffset', DateOffset), ('Day', Day), ('Hour', Hour), ('Minute', Minute), ('Second', Second),
               ('Milli', Milli), ('Micro', Micro), ('Nano', Nano), ('Week', Week), ('MonthEnd', MonthEnd),
               ('MonthBegin', MonthBegin), ('YearEnd', YearEnd), ('YearBegin', YearBegin),
               ('QuarterEnd', QuarterEnd), ('QuarterBegin', QuarterBegin), ('BusinessDay', BusinessDay),
               ('BDay', BusinessDay), ('BaseOffset', BaseOffset)):
    setattr(offsets, _n, _c)


class _Tseries:
    pass


tseries = _Tseries()
tseries.offsets = offsets
tseries.frequencies = _Tseries()
tseries.frequencies.to_offset = to_offset


class Grouper:
    def __init__(self, key=None, level=None, freq=None, sort=False, closed=None, label=None, **kw):
        self.key = key
        self.level = level
        self.freq = freq
        self.closed = closed
        self.label = label


# ---- options ----------------------------------------------------------------------------------

_opts = {'display.max_rows': 60, 'display.min_rows': 10, 'display.width': 80, 'display.max_colwidth': 50,
         'display.max_columns': 0, 'display.precision': 6}


def set_option(name, value):
    _opts[name] = value
    if name == 'display.max_rows':
        _fmt.MAX_ROWS = value if value is not None else 10 ** 9
    elif name == 'display.min_rows':
        _fmt.MIN_ROWS = value
    elif name == 'display.width':
        _fmt.WIDTH = value
    elif name == 'display.max_colwidth':
        _fmt.MAX_COLWIDTH = value if value is not None else 10 ** 9


def get_option(name):
    return _opts[name]


def reset_option(name):
    pass


class option_context:
    def __init__(self, *args):
        self.args = args
        self.old = []

    def __enter__(self):
        a = self.args
        for i in range(0, len(a), 2):
            self.old.append((a[i], _opts.get(a[i])))
            set_option(a[i], a[i + 1])
        return self

    def __exit__(self, *e):
        for k, v in self.old:
            set_option(k, v)
        return False


class _Options:
    pass


DataFrame.from_dict = staticmethod(lambda data, orient='columns', dtype=None, columns=None: _from_dict(data, orient, dtype, columns))
DataFrame.from_records = staticmethod(lambda data, columns=None, index=None, **kw: DataFrame(data, columns=columns, index=index))


def _from_dict(data, orient='columns', dtype=None, columns=None):
    if orient == 'index':
        keys = list(data.keys())
        first = data[keys[0]] if keys else {}
        if isinstance(first, dict):
            cols = []
            for k in keys:
                for c in data[k]:
                    if c not in cols:
                        cols.append(c)
            df = DataFrame({c: [data[k].get(c, nan) for k in keys] for c in cols}, index=Index(keys))
        else:
            df = DataFrame([list(data[k]) for k in keys], index=Index(keys), columns=columns)
        return df
    return DataFrame(data, dtype=dtype, columns=columns)


def _install_numpy_compat():
    from . import npcompat
    npcompat.install()


_install_numpy_compat()
