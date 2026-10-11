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


# ---- datetime support (needs the datetime module) -------------------------------------------

def to_datetime(arg, format=None, errors='raise', dayfirst=False, utc=None, unit=None, origin=None, **kw):
    import datetime
    scalar = not _is_listlike(arg)
    vals = [arg] if scalar else list(_lst(arg))
    out = []
    for x in vals:
        if x is None or x != x:
            out.append(nan)
        elif isinstance(x, datetime.datetime):
            out.append(x)
        elif isinstance(x, datetime.date):
            out.append(datetime.datetime(x.year, x.month, x.day))
        elif isinstance(x, str):
            try:
                out.append(_parse_dt(x, format))
            except Exception:
                if errors == 'coerce':
                    out.append(nan)
                else:
                    raise ValueError('time data "%s" doesn\'t match format' % x)
        else:
            out.append(datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=x))
    if scalar:
        return out[0]
    if isinstance(arg, Series):
        return Series(out, arg._idx, name=arg.name, _dt='datetime64[us]')
    return Index(out)


def _parse_dt(s, fmt):
    import datetime
    s = s.strip()
    if fmt is not None:
        return datetime.datetime.strptime(s, fmt)
    t = s.replace('T', ' ')
    date, _, tm = t.partition(' ')
    sep = '-' if '-' in date else ('/' if '/' in date else None)
    if sep is None:
        if len(date) == 8 and date.isdigit():
            y, m, d = int(date[:4]), int(date[4:6]), int(date[6:])
        else:
            raise ValueError(s)
    else:
        parts = date.split(sep)
        if len(parts[0]) == 4:
            y, m, d = int(parts[0]), int(parts[1]), int(parts[2]) if len(parts) > 2 else 1
        else:
            m, d, y = int(parts[0]), int(parts[1]), int(parts[2])
    hh = mm = ss = 0
    if tm:
        tp = tm.split(':')
        hh = int(tp[0])
        mm = int(tp[1]) if len(tp) > 1 else 0
        ss = int(float(tp[2])) if len(tp) > 2 else 0
    return datetime.datetime(y, m, d, hh, mm, ss)


def date_range(start=None, end=None, periods=None, freq='D', name=None, **kw):
    import datetime
    s = to_datetime(start) if isinstance(start, str) else start
    e = to_datetime(end) if isinstance(end, str) else end
    step = datetime.timedelta(days=1)
    f = freq if freq is not None else 'D'
    mult = 1
    unit = f
    k = 0
    while k < len(f) and f[k].isdigit():
        k += 1
    if k > 0:
        mult = int(f[:k])
        unit = f[k:]
    out = []
    if unit in ('D', 'h', 'H', 'min', 'T', 's', 'S', 'W'):
        secs = {'D': 86400, 'h': 3600, 'H': 3600, 'min': 60, 'T': 60, 's': 1, 'S': 1, 'W': 604800}[unit] * mult
        step = datetime.timedelta(seconds=secs)
        cur = s
        if periods is not None:
            for i in range(periods):
                out.append(s + step * i)
        else:
            while cur <= e:
                out.append(cur)
                cur = cur + step
    elif unit in ('MS', 'M', 'ME', 'YS', 'Y', 'YE', 'A', 'AS'):
        cur = s
        i = 0
        while True:
            if periods is not None and i >= periods:
                break
            if periods is None and cur > e:
                break
            out.append(cur)
            i += 1
            months = mult if unit in ('MS', 'M', 'ME') else 12 * mult
            if unit in ('M', 'ME'):
                # month end
                y = s.year + (s.month - 1 + months * i) // 12
                m = (s.month - 1 + months * i) % 12 + 1
                ny, nm = (y + 1, 1) if m == 12 else (y, m + 1)
                cur = datetime.datetime(ny, nm, 1) - datetime.timedelta(days=1)
                if i == 1 and out[0] == s and s.day != 1 and False:
                    pass
            else:
                y = s.year + (s.month - 1 + months * i) // 12
                m = (s.month - 1 + months * i) % 12 + 1
                cur = datetime.datetime(y, m, 1)
        if unit in ('M', 'ME') and out:
            # first element is the month end on or after start
            first = out[0]
            y, m = s.year, s.month
            ny, nm = (y + 1, 1) if m == 12 else (y, m + 1)
            me = datetime.datetime(ny, nm, 1) - datetime.timedelta(days=1)
            out = []
            i = 0
            while True:
                if periods is not None and i >= periods:
                    break
                yy = y + (m - 1 + mult * i) // 12
                mm = (m - 1 + mult * i) % 12 + 1
                ny, nm = (yy + 1, 1) if mm == 12 else (yy, mm + 1)
                d = datetime.datetime(ny, nm, 1) - datetime.timedelta(days=1)
                if periods is None and d > e:
                    break
                out.append(d)
                i += 1
    else:
        raise ValueError('unsupported freq ' + f)
    ix = Index(out, name=name)
    ix._dt = 'datetime64[us]'
    return ix


class Timestamp:
    def __new_(cls):
        pass


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
