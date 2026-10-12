# pandas subset: DatetimeIndex, TimedeltaIndex, to_datetime, to_timedelta, date_range, the .dt accessors and
# partial string indexing
import datetime as _dtm
import numpy as np
from .base import nan, NA, NaT, Index, infer, norm_dtype, _isser, dtype_of
from . import tslib as T
from .tslib import Timestamp, Timedelta, to_offset, as_timestamp, Tick, BaseOffset

_NO = ['__no__']
_DAY = 86400000000


def _lst(x):
    from .core import _lst as f
    return f(x)


def _isna(x):
    return x is None or x is NA or x is NaT or (type(x) is float and x != x)


def _unit_of(dt):
    k = dt.find('[')
    if k < 0:
        return 'us'
    return dt[k + 1:dt.find(']')]


def _mkdt(vals, unit='us'):
    return 'datetime64[%s]' % unit


# ------------------------------------------------------------------------------------------ conversion

def _guess_format(s, dayfirst):
    """strptime format of an ISO-like / numeric date string, or None"""
    s = s.strip()
    n = len(s)
    k = 0
    date_s = s
    time_s = ''
    tsep = ' '
    for q in range(n):
        if s[q] == 'T' and q > 0 and s[q - 1].isdigit():
            date_s = s[:q]
            time_s = s[q + 1:]
            tsep = 'T'
            break
        if s[q] == ' ':
            date_s = s[:q]
            time_s = s[q + 1:].strip()
            break
    if time_s.endswith('Z'):
        return None
    # date
    runs = []
    seps = []
    cur = ''
    for ch in date_s:
        if ch.isdigit():
            cur += ch
        else:
            if cur == '':
                return None
            runs.append(cur)
            seps.append(ch)
            cur = ''
            if ch not in '-/.':
                return None
    if cur == '':
        return None
    runs.append(cur)
    if len(set(seps)) > 1:
        return None
    sep = seps[0] if seps else ''
    lens = [len(r) for r in runs]
    if len(runs) == 1:
        if lens[0] == 8:
            df = '%Y%m%d'
        elif lens[0] == 4:
            df = '%Y'
        elif lens[0] == 6:
            df = '%Y%m'
        else:
            return None
    elif len(runs) == 2:
        if lens == [4, 2]:
            df = '%Y' + sep + '%m'
        else:
            return None
    elif len(runs) == 3:
        if lens[0] == 4 and lens[1] <= 2 and lens[2] <= 2:
            df = '%Y' + sep + '%m' + sep + '%d'
        elif lens[2] == 4 and lens[0] <= 2 and lens[1] <= 2:
            a = int(runs[0])
            b = int(runs[1])
            if dayfirst or a > 12:
                df = '%d' + sep + '%m' + sep + '%Y'
            else:
                df = '%m' + sep + '%d' + sep + '%Y'
        else:
            return None
    else:
        return None
    if time_s == '':
        return df
    low = time_s.lower()
    if low.endswith('am') or low.endswith('pm'):
        return None
    parts = time_s.split(':')
    if len(parts) == 1:
        if not parts[0].isdigit():
            return None
        tf = '%H'
    elif len(parts) == 2:
        tf = '%H:%M'
    elif len(parts) == 3:
        tf = '%H:%M:%S'
        if '.' in parts[2]:
            tf += '.%f'
    else:
        return None
    return df + tsep + tf


_HINT = ('. You might want to try:\n    - passing `format` if your strings have a consistent format;\n'
         "    - passing `format='ISO8601'` if your strings are all ISO8601 but not necessarily in exactly the same "
         "format;\n    - passing `format='mixed'`, and the format will be inferred for each element individually. "
         'You might want to use `dayfirst` alongside this.')


def _from_ts_fields(f):
    return Timestamp(f[0], f[1], f[2], f[3], f[4], f[5], f[6])


def _to_ts(x, dayfirst=False, yearfirst=False, unit=None, fmt=None, errors='raise', guessed=None, pos=0):
    """one value -> Timestamp / NaT"""
    if x is None or x is NA or x is NaT:
        return NaT
    if type(x) is Timestamp:
        return x
    if isinstance(x, _dtm.datetime):
        return Timestamp(x)
    if isinstance(x, _dtm.date):
        return Timestamp(x)
    if isinstance(x, str):
        t = x.strip()
        if t == '' or t.lower() in ('nat', 'nan', 'none', 'null'):
            return NaT
        try:
            if fmt is not None and fmt != 'mixed' and fmt != 'ISO8601':
                try:
                    return Timestamp(_dtm.datetime.strptime(t, fmt))
                except ValueError:
                    raise ValueError('time data "%s" doesn\'t match format "%s"' % (x, fmt) + _HINT)
            if guessed is not None:
                try:
                    return Timestamp(_dtm.datetime.strptime(t, guessed))
                except ValueError:
                    raise ValueError('time data "%s" doesn\'t match format "%s"' % (x, guessed) + _HINT)
            f = T.parse_datetime_string(t, dayfirst, yearfirst)
            return _from_ts_fields(f)
        except ValueError:
            if errors == 'coerce':
                return NaT
            raise
    if isinstance(x, bool):
        raise TypeError('<class \'bool\'> is not convertible to datetime')
    if isinstance(x, (int, float)):
        if x != x:
            return NaT
        if fmt is not None and fmt != 'mixed' and fmt != 'ISO8601':
            return Timestamp(_dtm.datetime.strptime(str(x), fmt))
        return Timestamp(x, unit=unit)
    raise TypeError('<class \'%s\'> is not convertible to datetime' % type(x).__name__)


def _unit_label(unit, vals_unit='us'):
    if unit is None:
        return vals_unit
    k = T._unit_key(unit)
    if k in ('D', 'h', 'min', 's', 'W'):
        return 's'
    if k == 'ms':
        return 'ms'
    return 'us'


def convert_list(vals, errors='raise', dayfirst=False, yearfirst=False, format=None, unit=None, origin='unix',
                 exact=True):
    """list -> list of Timestamp / NaT"""
    guessed = None
    if format is None:
        for x in vals:
            if isinstance(x, str):
                t = x.strip()
                if t == '' or t.lower() in ('nat', 'nan', 'none', 'null'):
                    continue
                guessed = _guess_format(t, dayfirst)
                break
            elif _isna(x):
                continue
            else:
                break
        if guessed is not None:
            # a guessed format is only enforced when every element is a string
            for x in vals:
                if not (isinstance(x, str) or _isna(x)):
                    guessed = None
                    break
    out = []
    off = None
    if origin != 'unix' and origin is not None:
        o = as_timestamp(origin) if not isinstance(origin, (int, float)) else Timestamp(origin, unit='D')
        off = o._epoch_us()
    i = 0
    for x in vals:
        if off is not None and isinstance(x, (int, float)) and not isinstance(x, bool) and x == x:
            us = T._num_to_us(x, unit if unit is not None else 'ns')
            out.append(T.ts_from_us(off + us))
        else:
            out.append(_to_ts(x, dayfirst, yearfirst, unit, format, errors, guessed, i))
        i += 1
    return out


def to_datetime(arg, errors='raise', dayfirst=False, yearfirst=False, utc=False, format=None, exact=True,
                unit=None, origin='unix', cache=True):
    from .core import Series, DataFrame, _is_listlike
    if isinstance(arg, DataFrame):
        cols = {}
        for c in arg.columns._v:
            cols[str(c).lower()] = c
        names = {'year': 'year', 'years': 'year', 'month': 'month', 'months': 'month', 'day': 'day', 'days': 'day',
                 'hour': 'hour', 'hours': 'hour', 'minute': 'minute', 'minutes': 'minute', 'second': 'second',
                 'seconds': 'second', 'ms': 'ms', 'us': 'us'}
        got = {}
        for k, c in cols.items():
            if k in names:
                got[names[k]] = arg[c]._v
        for need in ('year', 'month', 'day'):
            if need not in got:
                raise ValueError('to assemble mappings requires at least that [year, month, day] be specified: '
                                 '[%s] is missing' % need)
        n = len(arg.index)
        out = []
        for i in range(n):
            vals = [got['year'][i], got['month'][i], got['day'][i]]
            if vals[0] != vals[0] or vals[1] != vals[1] or vals[2] != vals[2]:
                out.append(NaT)
                continue
            try:
                out.append(Timestamp(int(vals[0]), int(vals[1]), int(vals[2]),
                                     int(got['hour'][i]) if 'hour' in got else 0,
                                     int(got['minute'][i]) if 'minute' in got else 0,
                                     int(got['second'][i]) if 'second' in got else 0))
            except ValueError:
                if errors == 'coerce':
                    out.append(NaT)
                else:
                    raise
        return Series(out, arg.index, _dt='datetime64[us]')
    if isinstance(arg, (Series, Index)) or _is_listlike(arg):
        vals = _lst(arg)
        if isinstance(arg, Series) or isinstance(arg, Index):
            if arg._dt.startswith('datetime') and format is None and unit is None:
                if isinstance(arg, Series):
                    return arg
                return arg
        conv = convert_list(list(vals), errors, dayfirst, yearfirst, format, unit, origin, exact)
        un = _unit_label(unit) if _numeric_input(vals) else _inferred_unit(conv, arg)
        if isinstance(arg, Series):
            return Series(conv, arg._idx, name=arg.name, _dt='datetime64[%s]' % un)
        name = getattr(arg, 'name', None)
        return DatetimeIndex(conv, name=name, _dt='datetime64[%s]' % un, _raw=True)
    if isinstance(arg, dict):
        raise TypeError('dict input is not supported')
    r = convert_list([arg], errors, dayfirst, yearfirst, format, unit, origin, exact)[0]
    return r


def _numeric_input(vals):
    for x in vals:
        if _isna(x):
            continue
        return isinstance(x, (int, float)) and not isinstance(x, bool)
    return False


def _inferred_unit(conv, arg):
    u = None
    for x in conv:
        if x is NaT:
            continue
        xu = x._unit
        u = xu if u is None else T._finer(u, xu)
    if u is None:
        return 'us'
    return u


def _td_us(x, unit=None):
    """value -> microseconds int or None (NaT)"""
    if x is None or x is NA or x is NaT or (type(x) is float and x != x):
        return None
    if isinstance(x, _dtm.timedelta):
        return x._us
    if isinstance(x, str):
        s = x.strip()
        if unit is not None and s.replace('.', '', 1).lstrip('+-').isdigit():
            return T._num_to_us(float(s), unit)
        return T._parse_td_string(s)
    if isinstance(x, (int, float)) and not isinstance(x, bool):
        return T._num_to_us(x, unit)
    raise ValueError('Could not convert %r to Timedelta' % (x,))


def to_timedelta(arg, unit=None, errors='raise'):
    from .core import Series, _is_listlike
    if _is_listlike(arg) or isinstance(arg, (Series, Index)):
        vals = _lst(arg)
        out = []
        numeric = False
        for x in vals:
            try:
                us = _td_us(x, unit)
            except ValueError:
                if errors == 'coerce':
                    us = None
                else:
                    raise
            if us is None:
                out.append(NaT)
            else:
                out.append(Timedelta._of(us))
            if isinstance(x, (int, float)) and not isinstance(x, bool):
                numeric = True
        un = 'us'
        if numeric and unit is not None:
            un = _unit_label(unit)
            for i in range(len(out)):
                if out[i] is not NaT:
                    out[i]._unit = un
        if isinstance(arg, Series):
            return Series(out, arg._idx, name=arg.name, _dt='timedelta64[%s]' % un)
        return TimedeltaIndex(out, name=getattr(arg, 'name', None), _dt='timedelta64[%s]' % un, _raw=True)
    try:
        us = _td_us(arg, unit)
    except ValueError:
        if errors == 'coerce':
            return NaT
        raise
    if us is None:
        return NaT
    r = Timedelta._of(us)
    if isinstance(arg, (int, float)) and unit is not None:
        r._unit = _unit_label(unit)
    return r


# ------------------------------------------------------------------------------------------ text layout

def _fmt_dt_index_item(x, dates_only):
    if x is NaT or x != x:
        return 'NaT'
    if dates_only:
        return x._date_repr
    return str(x)


def _is_dates_only(vals, freq):
    if freq is not None and isinstance(freq, Tick):
        if freq._tickus * freq.n % _DAY != 0:
            return False
    for x in vals:
        if x is not NaT and x == x:
            if x.hour or x.minute or x.second or x.microsecond:
                return False
    return True


def format_object_summary(strs, name, display_width=None, max_seq_items=100):
    """port of pandas.io.formats.printing.format_object_summary for pre-formatted strings"""
    if display_width is None:
        from . import fmt as _fmt
        display_width = _fmt.WIDTH
    n = len(strs)
    space1 = '\n' + ' ' * (len(name) + 1)
    space2 = '\n' + ' ' * (len(name) + 2)
    sep = ','
    is_truncated = n > max_seq_items
    close = ', '
    if n == 0:
        return '[]' + close
    if n == 1:
        return '[' + strs[0] + ']' + close
    if n == 2:
        return '[' + strs[0] + ', ' + strs[1] + ']' + close
    if is_truncated:
        k = min(max_seq_items // 2, 10)
        head = strs[:k]
        tail = strs[n - k:]
    else:
        head = []
        tail = list(strs)
    if is_truncated or not (len(', '.join(head)) < display_width and len(', '.join(tail)) < display_width):
        ml = 0
        for x in head + tail:
            if len(x) > ml:
                ml = len(x)
        head = [x.rjust(ml) for x in head]
        tail = [x.rjust(ml) for x in tail]
    summary = ''
    line = space2
    for hv in head:
        word = hv + sep + ' '
        if len(line.rstrip()) + len(word.rstrip()) >= display_width:
            summary += line.rstrip()
            line = space2
        line += word
    if is_truncated:
        summary += line.rstrip() + space2 + '...'
        line = space2
    for tv in tail[:-1]:
        word = tv + sep + ' '
        if len(line.rstrip()) + len(word.rstrip()) >= display_width:
            summary += line.rstrip()
            line = space2
        line += word
    word = tail[-1]
    if len(line.rstrip()) + len(word.rstrip()) >= display_width - 2:
        summary += line.rstrip()
        line = space2
    line += word
    summary += line
    close = ']' + close.rstrip(' ')
    summary += close
    if len(summary) > display_width:
        summary += space1
    else:
        summary += ' '
    return '[' + summary[len(space2):]


# ------------------------------------------------------------------------------------------ DatetimeIndex

_DT_INT_FIELDS = {
    'year': lambda x: x.year, 'month': lambda x: x.month, 'day': lambda x: x.day, 'hour': lambda x: x.hour,
    'minute': lambda x: x.minute, 'second': lambda x: x.second, 'microsecond': lambda x: x.microsecond,
    'nanosecond': lambda x: 0, 'dayofweek': lambda x: x.weekday(), 'day_of_week': lambda x: x.weekday(),
    'weekday': lambda x: x.weekday(), 'dayofyear': lambda x: x.dayofyear, 'day_of_year': lambda x: x.dayofyear,
    'quarter': lambda x: x.quarter, 'days_in_month': lambda x: x.days_in_month,
    'daysinmonth': lambda x: x.days_in_month,
}
_DT_BOOL_FIELDS = {
    'is_month_start': lambda x: x.is_month_start, 'is_month_end': lambda x: x.is_month_end,
    'is_quarter_start': lambda x: x.is_quarter_start, 'is_quarter_end': lambda x: x.is_quarter_end,
    'is_year_start': lambda x: x.is_year_start, 'is_year_end': lambda x: x.is_year_end,
    'is_leap_year': lambda x: x.is_leap_year,
}
_TD_INT_FIELDS = {
    'days': lambda x: x.days, 'seconds': lambda x: x.seconds, 'microseconds': lambda x: x.microseconds,
    'nanoseconds': lambda x: 0,
}


def dt_field(vals, name):
    """datetime values -> (list, dtype name)"""
    if name in _DT_INT_FIELDS:
        f = _DT_INT_FIELDS[name]
        miss = False
        for x in vals:
            if x is NaT or x != x:
                miss = True
                break
        if miss:
            return [nan if (x is NaT or x != x) else float(f(x)) for x in vals], 'float64'
        return [f(x) for x in vals], 'int32'
    if name in _DT_BOOL_FIELDS:
        f = _DT_BOOL_FIELDS[name]
        return [(False if (x is NaT or x != x) else f(x)) for x in vals], 'bool'
    if name == 'date':
        return [(NaT if (x is NaT or x != x) else x.date()) for x in vals], 'object'
    if name == 'time':
        return [(NaT if (x is NaT or x != x) else x.time()) for x in vals], 'object'
    if name == 'timetz':
        return [(NaT if (x is NaT or x != x) else x.time()) for x in vals], 'object'
    raise AttributeError(name)


def td_field(vals, name):
    f = _TD_INT_FIELDS[name]
    miss = False
    for x in vals:
        if x is NaT or x != x:
            miss = True
            break
    if miss:
        return [nan if (x is NaT or x != x) else float(f(x)) for x in vals], 'float64'
    return [f(x) for x in vals], 'int64'


def _strf(vals, fmt):
    return [(nan if (x is NaT or x != x) else x.strftime(fmt)) for x in vals]


def _floor_ceil(vals, freq, how):
    out = []
    for x in vals:
        if x is NaT or x != x:
            out.append(NaT)
        else:
            out.append(x._round(freq, how))
    return out


class _Acc:
    """common machinery of .dt on Series and the methods of the datetime-like indexes"""

    def __init__(self, vals, dt, ret):
        self.vals = vals
        self.dt = dt
        self.ret = ret          # ret(list, dtype) -> container


class DatetimeMethods:
    def __init__(self, s):
        self.s = s
        self._isidx = isinstance(s, Index)

    def _ret(self, vals, dt):
        s = self.s
        if self._isidx:
            r = Index(vals, name=s.name) if dt in ('object', 'str') or dt.startswith('datetime') else Index(vals, name=s.name)
            if dt == 'int32' or dt == 'str' or dt.startswith('datetime') or dt.startswith('timedelta'):
                r._dt = dt
            if dt.startswith('datetime'):
                return DatetimeIndex(vals, name=s.name, _dt=dt, _raw=True)
            return r
        from .core import Series
        return Series(vals, s._idx, name=s.name, _dt=dt)

    def _vals(self):
        return self.s._v

    def _field(self, name):
        v, dt = dt_field(self.s._v, name)
        if self._isidx and (dt == 'bool' or dt == 'object'):
            if dt == 'bool':
                return np.array(v)
            from .core import ObjArray
            return ObjArray(v)
        return self._ret(v, dt)

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)
        if name in _DT_INT_FIELDS or name in _DT_BOOL_FIELDS or name in ('date', 'time', 'timetz'):
            return self._field(name)
        if name == 'tz' or name == 'tzinfo':
            return None
        if name == 'freq':
            return getattr(self.s, 'freq', None) if self._isidx else None
        if name == 'unit':
            return _unit_of(self.s._dt)
        raise AttributeError("'DatetimeProperties' object has no attribute '%s'" % name)

    def strftime(self, date_format):
        return self._ret(_strf(self.s._v, date_format), 'str')

    def day_name(self, locale=None):
        return self._ret([(nan if (x is NaT or x != x) else T._DAYS[x.weekday()]) for x in self.s._v], 'str')

    def month_name(self, locale=None):
        return self._ret([(nan if (x is NaT or x != x) else T._MONTHS[x.month - 1]) for x in self.s._v], 'str')

    def normalize(self):
        return self._ret([(NaT if (x is NaT or x != x) else x.normalize()) for x in self.s._v], self.s._dt)

    def floor(self, freq, ambiguous='raise', nonexistent='raise'):
        return self._ret(_floor_ceil(self.s._v, freq, 'floor'), self.s._dt)

    def ceil(self, freq, ambiguous='raise', nonexistent='raise'):
        return self._ret(_floor_ceil(self.s._v, freq, 'ceil'), self.s._dt)

    def round(self, freq, ambiguous='raise', nonexistent='raise'):
        return self._ret(_floor_ceil(self.s._v, freq, 'round'), self.s._dt)

    def to_pydatetime(self):
        from .core import ObjArray
        return ObjArray([(NaT if x is NaT else x.to_pydatetime()) for x in self.s._v])

    def tz_localize(self, tz, ambiguous='raise', nonexistent='raise'):
        if tz is None:
            return self.s
        raise NotImplementedError('time zones are not supported')

    def tz_convert(self, tz):
        raise NotImplementedError('time zones are not supported')

    def as_unit(self, unit):
        return self._ret(list(self.s._v), 'datetime64[%s]' % unit)

    def isocalendar(self):
        from .core import DataFrame
        y = []
        w = []
        d = []
        for x in self.s._v:
            if x is NaT or x != x:
                y.append(nan)
                w.append(nan)
                d.append(nan)
            else:
                c = _dtm._isocal(x.year, x.month, x.day)
                y.append(c[0])
                w.append(c[1])
                d.append(c[2])
        idx = self.s if self._isidx else self.s._idx
        return DataFrame({'year': y, 'week': w, 'day': d}, index=idx)

    def to_period(self, freq=None):
        raise NotImplementedError('Period is not supported')


class TimedeltaMethods:
    def __init__(self, s):
        self.s = s
        self._isidx = isinstance(s, Index)

    def _ret(self, vals, dt):
        s = self.s
        if self._isidx:
            if dt.startswith('timedelta'):
                return TimedeltaIndex(vals, name=s.name, _dt=dt, _raw=True)
            r = Index(vals, name=s.name)
            r._dt = dt
            return r
        from .core import Series
        return Series(vals, s._idx, name=s.name, _dt=dt)

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)
        if name in _TD_INT_FIELDS:
            v, dt = td_field(self.s._v, name)
            return self._ret(v, dt)
        if name == 'components':
            from .core import DataFrame
            cols = ['days', 'hours', 'minutes', 'seconds', 'milliseconds', 'microseconds', 'nanoseconds']
            data = {}
            for k in range(7):
                data[cols[k]] = [(nan if (x is NaT or x != x) else x.components[k]) for x in self.s._v]
            idx = self.s if self._isidx else self.s._idx
            return DataFrame(data, index=idx)
        if name == 'unit':
            return _unit_of(self.s._dt)
        raise AttributeError("'TimedeltaProperties' object has no attribute '%s'" % name)

    def total_seconds(self):
        return self._ret([(nan if (x is NaT or x != x) else x._us / 1000000.0) for x in self.s._v], 'float64')

    def to_pytimedelta(self):
        from .core import ObjArray
        return ObjArray([(NaT if x is NaT else x.to_pytimedelta()) for x in self.s._v])

    def floor(self, freq):
        return self._ret([(NaT if (x is NaT or x != x) else x.floor(freq)) for x in self.s._v], self.s._dt)

    def ceil(self, freq):
        return self._ret([(NaT if (x is NaT or x != x) else x.ceil(freq)) for x in self.s._v], self.s._dt)

    def round(self, freq):
        return self._ret([(NaT if (x is NaT or x != x) else x.round(freq)) for x in self.s._v], self.s._dt)

    def as_unit(self, unit):
        return self._ret(list(self.s._v), 'timedelta64[%s]' % unit)


def make_dt_accessor(s):
    d = s._dt
    if d.startswith('datetime'):
        return DatetimeMethods(s)
    if d.startswith('timedelta'):
        return TimedeltaMethods(s)
    raise AttributeError('Can only use .dt accessor with datetimelike values')


# ------------------------------------------------------------------------------------------ partial strings

def _reso_bounds(y, m, d, hh, mi, ss, us, reso):
    """period [lo, hi] (epoch microseconds, inclusive) addressed by a partially specified string"""
    lo = T.Timestamp(y, m, d, hh, mi, ss, us)._epoch_us()
    if reso == 'year':
        hi = Timestamp(y + 1, 1, 1)._epoch_us() - 1
    elif reso == 'quarter':
        y2, m2 = T._add_months(y, m, 3)
        hi = Timestamp(y2, m2, 1)._epoch_us() - 1
    elif reso == 'month':
        y2, m2 = T._add_months(y, m, 1)
        hi = Timestamp(y2, m2, 1)._epoch_us() - 1
    elif reso == 'day':
        hi = lo + _DAY - 1
    elif reso == 'hour':
        hi = lo + 3600000000 - 1
    elif reso == 'minute':
        hi = lo + 60000000 - 1
    elif reso == 'second':
        hi = lo + 1000000 - 1
    else:
        hi = lo
    return lo, hi


_RESO_RANK = {'microsecond': 0, 'second': 1, 'minute': 2, 'hour': 3, 'day': 4, 'month': 5, 'quarter': 6, 'year': 7}


def index_resolution(vals):
    """finest non-zero time component present: microsecond .. day"""
    r = 'day'
    rank = 4
    for x in vals:
        if x is NaT or x != x:
            continue
        if x.microsecond:
            return 'microsecond'
        if x.second:
            if rank > 1:
                r, rank = 'second', 1
        elif x.minute:
            if rank > 2:
                r, rank = 'minute', 2
        elif x.hour:
            if rank > 3:
                r, rank = 'hour', 3
    return r


def dt_string_bounds(key, dayfirst=False):
    f = T.parse_datetime_string(key, dayfirst)
    reso = f[7]
    lo, hi = _reso_bounds(f[0], f[1], f[2], f[3], f[4], f[5], f[6], reso)
    return lo, hi, reso


def dt_positions(idx, key, kind='loc'):
    """string / datetime key on a datetime index -> (positions, is_scalar)"""
    vals = idx._v
    orig = key
    if isinstance(key, str):
        try:
            lo, hi, reso = dt_string_bounds(key)
        except ValueError:
            raise KeyError(key)
        ireso = index_resolution(vals)
        if _RESO_RANK[reso] > _RESO_RANK[ireso]:
            ps = []
            i = 0
            for x in vals:
                if x is not NaT and x == x:
                    u = x._epoch_us()
                    if lo <= u <= hi:
                        ps.append(i)
                i += 1
            if not ps:
                raise KeyError(key)
            return ps, False
        key = Timestamp(key)
    if isinstance(key, _dtm.date) and not isinstance(key, _dtm.datetime):
        key = Timestamp(key)
    if isinstance(key, _dtm.datetime):
        ps = idx._locs(key if type(key) is Timestamp else Timestamp(key))
        if not ps:
            raise KeyError(orig if isinstance(orig, str) else (key if type(key) is Timestamp else Timestamp(key)))
        return ps, len(ps) == 1
    raise KeyError(key)


def dt_slice_positions(idx, start, stop, n):
    """label slice with string / datetime bounds -> list of positions"""
    vals = idx._v
    lo = None
    hi = None
    if start is not None:
        lo = _bound_us(start, False)
    if stop is not None:
        hi = _bound_us(stop, True)
    ps = []
    i = 0
    for x in vals:
        if x is NaT or x != x:
            i += 1
            continue
        u = x._epoch_us()
        if (lo is None or u >= lo) and (hi is None or u <= hi):
            ps.append(i)
        i += 1
    return ps


def _bound_us(b, upper):
    if isinstance(b, str):
        lo, hi, reso = dt_string_bounds(b)
        return hi if upper else lo
    if isinstance(b, _dtm.datetime):
        return as_timestamp(b)._epoch_us()
    if isinstance(b, _dtm.date):
        return Timestamp(b)._epoch_us() + (_DAY - 1 if upper else 0)
    if isinstance(b, (int, float)):
        raise TypeError('cannot do slice indexing on DatetimeIndex with these indexers [%s]' % b)
    raise TypeError('cannot do slice indexing on DatetimeIndex with these indexers [%s]' % (b,))


# ------------------------------------------------------------------------------------------ index classes

def _clean_name(arg, name):
    if name is None:
        return getattr(arg, 'name', None)
    return name


class _DTLike(Index):
    """shared behaviour of DatetimeIndex and TimedeltaIndex"""
    _kind = 'M'

    def _setup(self, vals, dt, name, freq):
        self._v = vals
        self._dt = dt
        self.name = name
        self._pos = None
        self._freq = freq

    @property
    def freq(self):
        return self._freq

    @freq.setter
    def freq(self, value):
        self._freq = to_offset(value)

    @property
    def freqstr(self):
        if self._freq is None:
            return None
        return self._freq.freqstr

    @property
    def unit(self):
        return _unit_of(self._dt)

    @property
    def tz(self):
        return None

    @property
    def tzinfo(self):
        return None

    @property
    def inferred_freq(self):
        return _infer_freq(self._v)

    def _clone(self, vals, name=-1, freq=None):
        raise NotImplementedError

    def _new(self, v, name=None):
        return self._clone(v, name, None)

    def copy(self, name=None, deep=False):
        return self._clone(list(self._v), self.name if name is None else name, self._freq)

    def __getitem__(self, key):
        v = self._v
        if isinstance(key, slice):
            fr = self._freq
            if fr is not None and key.step is not None and key.step != 1:
                fr = fr * key.step
            if fr is not None and key.step is not None and key.step < 0 and False:
                pass
            return self._clone(v[key], self.name, fr)
        if isinstance(key, int):
            return v[key]
        if isinstance(key, np.ndarray):
            key = key.tolist()
        if isinstance(key, list):
            if len(key) > 0 and isinstance(key[0], bool):
                pos = [i for i in range(len(v)) if key[i]]
                keep = self._freq is not None and _contig(pos)
                return self._clone([v[i] for i in pos], self.name, self._freq if keep else None)
            return self._clone([v[i] for i in key], self.name, None)
        return v[key]

    def _take_pos(self, pos):
        v = self._v
        fr = None
        if self._freq is not None and len(pos) > 0:
            if len(pos) == 1:
                fr = self._freq
            else:
                st = pos[1] - pos[0]
                ok = st != 0
                for i in range(2, len(pos)):
                    if pos[i] - pos[i - 1] != st:
                        ok = False
                        break
                if ok:
                    fr = self._freq * st if st != 1 else self._freq
        return self._clone([v[i] for i in pos], self.name, fr)

    def take(self, idx):
        return self._take_pos(list(idx))

    def _wrap(self, r):
        if isinstance(r, Index) and not isinstance(r, _DTLike):
            return self._clone(list(r._v), r.name, None)
        return r

    def _setop_freq(self, r):
        r = self._wrap(r)
        if self._freq is not None and isinstance(r, _DTLike) and _regular(r._v, self._freq):
            r._freq = self._freq
        return r

    def union(self, other, sort=None):
        return self._setop_freq(Index.union(self, other, sort))

    def intersection(self, other, sort=False):
        return self._setop_freq(Index.intersection(self, other, sort))

    def difference(self, other, sort=None):
        return self._setop_freq(Index.difference(self, other, sort))

    def append(self, other):
        return self._wrap(Index.append(self, other))

    def insert(self, loc, item):
        return self._setop_freq(Index.insert(self, loc, item))

    def delete(self, loc):
        r = self._clone(Index.delete(self, loc)._v, self.name, None)
        if self._freq is not None and (loc == 0 or loc == -1 or loc == len(self._v) - 1):
            r._freq = self._freq
        return r

    def repeat(self, n):
        return self._clone([x for x in self._v for _ in range(n)], self.name, None)

    def unique(self, level=None):
        r = self._clone(Index.unique(self)._v, self.name, None)
        if len(r._v) == len(self._v):
            r._freq = self._freq
        return r

    def drop_duplicates(self, keep='first'):
        return self.unique()

    def sort_values(self, ascending=True, return_indexer=False, key=None):
        nn = [i for i in range(len(self._v)) if self._v[i] == self._v[i]]
        nan_pos = [i for i in range(len(self._v)) if not (self._v[i] == self._v[i])]
        order = sorted(nn, key=lambda i: self._v[i])
        if not ascending:
            order = order[::-1]
        order = order + nan_pos
        fr = None
        if self._freq is not None:
            if order == sorted(order):
                fr = self._freq
            elif order == sorted(order)[::-1] and len(order) > 1:
                fr = self._freq * -1
        r = self._clone([self._v[i] for i in order], self.name, fr)
        if return_indexer:
            return r, np.array(order)
        return r

    def argsort(self):
        return np.array(sorted(range(len(self._v)), key=lambda i: self._v[i]))

    def min(self, axis=None, skipna=True):
        nn = [x for x in self._v if x == x]
        if not nn:
            return NaT
        return min(nn)

    def max(self, axis=None, skipna=True):
        nn = [x for x in self._v if x == x]
        if not nn:
            return NaT
        return max(nn)

    def argmin(self, axis=None, skipna=True):
        best = -1
        for i in range(len(self._v)):
            if self._v[i] == self._v[i] and (best < 0 or self._v[i] < self._v[best]):
                best = i
        return best

    def argmax(self, axis=None, skipna=True):
        best = -1
        for i in range(len(self._v)):
            if self._v[i] == self._v[i] and (best < 0 or self._v[i] > self._v[best]):
                best = i
        return best

    def isna(self):
        return np.array([x != x for x in self._v])

    def fillna(self, value=None):
        return self._clone([value if x != x else x for x in self._v], self.name, None)

    def dropna(self, how='any'):
        return self._clone([x for x in self._v if x == x], self.name, None)

    def map(self, f):
        r = Index.map(self, f)
        return r

    def to_series(self, index=None, name=None):
        from .core import Series
        return Series(list(self._v), self if index is None else index, name=name if name is not None else self.name,
                      _dt=self._dt)

    def to_frame(self, index=True, name=None):
        from .core import DataFrame
        nm = name if name is not None else (self.name if self.name is not None else 0)
        return DataFrame({nm: list(self._v)}, index=self if index else None)

    def where(self, cond, other=nan):
        c = cond.tolist() if isinstance(cond, np.ndarray) else (cond._v if _isser(cond) else list(cond))
        return self._clone([x if k else (NaT if other != other else other) for x, k in zip(self._v, c)], self.name, None)

    def astype(self, dtype, copy=True):
        dn = norm_dtype(dtype)
        if dn == 'str':
            labs = self._fmt_labels(False)
            r = Index([(nan if s == 'NaT' else s) for s in labs], name=self.name)
            r._dt = 'str'
            return r
        if dn == 'object':
            r = Index(list(self._v), name=self.name)
            r._dt = 'object'
            return r
        if dn == 'int64':
            r = Index([x._epoch_us() if self._kind == 'M' else x._us for x in self._v], name=self.name)
            return r
        if dn.startswith('datetime64') and self._kind == 'M':
            return self._clone(list(self._v), self.name, self._freq, dn)
        if dn.startswith('timedelta64') and self._kind == 'm':
            return self._clone(list(self._v), self.name, self._freq, dn)
        return Index.astype(self, dtype)

    def get_loc(self, key, method=None):
        ps, scalar = self._key_positions(key)
        if scalar:
            return ps[0]
        if len(ps) > 1 and ps == list(range(ps[0], ps[-1] + 1)):
            return slice(ps[0], ps[-1] + 1)
        return ps if len(ps) > 1 else ps[0]

    def __contains__(self, key):
        try:
            self._key_positions(key)
            return True
        except (KeyError, TypeError, ValueError):
            return False

    def get_indexer(self, target):
        p = self._mkpos()
        if isinstance(target, Index):
            target = target._v
        return np.array([p.get(x, -1) for x in target])

    def searchsorted(self, value, side='left'):
        if isinstance(value, str):
            value = Timestamp(value)
        v = self._v
        n = 0
        for x in v:
            if (x < value) if side == 'left' else (x <= value):
                n += 1
        return n

    def _format_labels(self):
        return self._fmt_labels()

    def _conv_other(self, o):
        return o

    def _cmp(self, other, f):
        if isinstance(other, Index):
            other = other._v
        if isinstance(other, (list, tuple, np.ndarray)):
            ol = [self._conv_other(x) for x in (other.tolist() if isinstance(other, np.ndarray) else other)]
            return np.array([(a == a and b == b and f(a, b)) for a, b in zip(self._v, ol)])
        o = self._conv_other(other)
        if o != o:
            return np.array([False] * len(self._v))
        return np.array([(a == a and f(a, o)) for a in self._v])

    def __eq__(self, other):
        if isinstance(other, Index):
            other = other._v
        if isinstance(other, (list, tuple, np.ndarray)):
            ol = [self._conv_other(x) for x in (other.tolist() if isinstance(other, np.ndarray) else other)]
            return np.array([a == b for a, b in zip(self._v, ol)])
        o = self._conv_other(other)
        return np.array([a == o for a in self._v])

    def __ne__(self, other):
        return ~self.__eq__(other)

    def __hash__(self):
        return id(self)

    def __iter__(self):
        return iter(self._v)

    def equals(self, other):
        if not isinstance(other, Index):
            return False
        if len(other._v) != len(self._v):
            return False
        for a, b in zip(self._v, other._v):
            if not (a == b or (a != a and b != b)):
                return False
        return True

    def __repr__(self):
        nm = self.__class__.__name__
        strs = [(s if (s == 'NaT' and self._kind == 'm') else "'" + s + "'") for s in self._fmt_labels(True)]
        data = format_object_summary(strs, nm)
        attrs = ["dtype='%s'" % self._dt]
        if self.name is not None:
            attrs.append('name=%r' % (self.name,))
        if len(self._v) > 100:
            attrs.append('length=%d' % len(self._v))
        fs = self.freqstr
        attrs.append('freq=%s' % ("'%s'" % fs if fs is not None else 'None'))
        return nm + '(' + data + ', '.join(attrs) + ')'

    def __str__(self):
        return self.__repr__()

    def memory_usage(self, deep=False):
        return 8 * len(self._v)

    @property
    def values(self):
        from .core import ObjArray
        return ObjArray(list(self._v))

    def to_list(self):
        return list(self._v)

    def tolist(self):
        return list(self._v)

    def to_numpy(self, dtype=None, copy=False, na_value=None):
        return self.values

    @property
    def asi8(self):
        return np.array([x._epoch_us() * 1000 if self._kind == 'M' else x._us * 1000 for x in self._v])

    def is_(self, other):
        return self is other

    @property
    def is_monotonic_increasing(self):
        v = self._v
        for i in range(1, len(v)):
            if not (v[i] >= v[i - 1]):
                return False
        return True

    @property
    def is_monotonic_decreasing(self):
        v = self._v
        for i in range(1, len(v)):
            if not (v[i] <= v[i - 1]):
                return False
        return True

    @property
    def hasnans(self):
        for x in self._v:
            if x != x:
                return True
        return False

    @property
    def is_unique(self):
        return len(self._mkpos()) == len(self._v)

    def nunique(self, dropna=True):
        return len(set([x for x in self._v if x == x]))

    def value_counts(self, normalize=False, sort=True, ascending=False, bins=None, dropna=True):
        from .core import Series
        return Series(list(self._v), _dt=self._dt).value_counts(normalize=normalize, sort=sort, ascending=ascending,
                                                                  dropna=dropna)

    def slice_indexer(self, start=None, end=None, step=None):
        ps = dt_slice_positions(self, start, end, len(self._v)) if self._kind == 'M' else self._td_slice(start, end)
        if not ps:
            return slice(0, 0)
        return slice(ps[0], ps[-1] + 1)

    def _td_slice(self, start, end):
        lo = _as_td(start) if start is not None else None
        hi = _as_td(end) if end is not None else None
        return [i for i in range(len(self._v)) if self._v[i] == self._v[i] and (lo is None or self._v[i] >= lo)
                and (hi is None or self._v[i] <= hi)]


def _as_td(x):
    return T._as_td(x)


def _contig(pos):
    for i in range(1, len(pos)):
        if pos[i] != pos[i - 1] + 1:
            return False
    return True


def _infer_freq(vals):
    n = len(vals)
    if n < 3:
        return None
    for x in vals:
        if x is NaT or x != x:
            return None
    if isinstance(vals[0], Timedelta):
        d = vals[1] - vals[0]
        for i in range(2, n):
            if vals[i] - vals[i - 1] != d:
                return None
        return _td_freq(d._us)
    us = [x._epoch_us() for x in vals]
    d = us[1] - us[0]
    ok = True
    for i in range(2, n):
        if us[i] - us[i - 1] != d:
            ok = False
            break
    if ok and d > 0:
        r = _td_freq(d)
        if r is not None and d % _DAY == 0 and d % (7 * _DAY) == 0 and False:
            pass
        if d % (7 * _DAY) == 0 and vals[0].weekday() is not None:
            return 'W-' + T._WKDAY[vals[0].weekday()] if d == 7 * _DAY else r
        return r
    # month / quarter / year ends and starts
    allend = True
    allbeg = True
    for x in vals:
        if not x.is_month_end:
            allend = False
        if not x.is_month_start:
            allbeg = False
    if not (allend or allbeg):
        wd = True
        for x in vals:
            if x.hour or x.minute or x.second or x.microsecond or x.weekday() >= 5:
                wd = False
        if wd:
            ok = True
            for i in range(1, n):
                nxt = T.BusinessDay(1)._add_ts(vals[i - 1])
                if nxt != vals[i]:
                    ok = False
                    break
            if ok:
                return 'B'
        return None
    steps = []
    for i in range(1, n):
        a, b = vals[i - 1], vals[i]
        steps.append((b.year - a.year) * 12 + b.month - a.month)
    for s in steps:
        if s != steps[0]:
            return None
    k = steps[0]
    if k <= 0:
        return None
    suffix = 'E' if allend else 'S'
    if k % 12 == 0:
        c = k // 12
        base = ('Y' + suffix) + '-' + T._MON3[vals[0].month - 1]
        return base if c == 1 else '%d%s' % (c, base)
    if k % 3 == 0:
        c = k // 3
        base = ('Q' + suffix) + '-' + T._MON3[vals[0].month - 1]
        return base if c == 1 else '%d%s' % (c, base)
    return 'M' + suffix if k == 1 else '%dM%s' % (k, suffix)


def _td_freq(us):
    for code, f in (('D', _DAY), ('h', 3600000000), ('min', 60000000), ('s', 1000000), ('ms', 1000), ('us', 1)):
        if us % f == 0:
            k = us // f
            return code if k == 1 else '%d%s' % (k, code)
    return None


class DatetimeIndex(_DTLike):
    _kind = 'M'

    def __init__(self, data=None, freq=_NO, tz=None, normalize=False, dayfirst=False, yearfirst=False, dtype=None,
                 copy=False, name=None, _dt=None, _raw=False, ambiguous='raise'):
        if data is None:
            data = []
        name = _clean_name(data, name)
        if _raw:
            vals = data
            dt = _dt or 'datetime64[us]'
        else:
            src = _lst(data) if not isinstance(data, (str, int, float)) else [data]
            vals = convert_list(list(src), 'raise', dayfirst, yearfirst)
            if normalize:
                vals = [(NaT if x is NaT else x.normalize()) for x in vals]
            un = _inferred_unit(vals, data)
            if isinstance(data, (Index,)) or hasattr(data, '_dt'):
                if getattr(data, '_dt', '').startswith('datetime'):
                    un = _unit_of(data._dt)
            dt = 'datetime64[%s]' % un
            if dtype is not None:
                dn = norm_dtype(dtype)
                if dn.startswith('datetime64'):
                    dt = dn
        if freq is _NO:
            fr = getattr(data, '_freq', None) if isinstance(data, DatetimeIndex) else None
        elif freq == 'infer':
            f = _infer_freq(vals)
            fr = to_offset(f) if f else None
        else:
            fr = to_offset(freq)
        self._setup(vals, dt, name, fr)

    def _clone(self, vals, name=-1, freq=None, dt=None):
        return DatetimeIndex(vals, name=self.name if name == -1 else name, _dt=dt or self._dt, _raw=True,
                             freq=freq)

    def _conv_other(self, o):
        if isinstance(o, str):
            return Timestamp(o)
        if o is None:
            return NaT
        if isinstance(o, _dtm.datetime) and type(o) is not Timestamp:
            return Timestamp(o)
        return o

    def _key_positions(self, key):
        if isinstance(key, str) or isinstance(key, _dtm.date):
            return dt_positions(self, key)
        raise KeyError(key)

    def _fmt_labels(self, boxed=False):
        dates_only = _is_dates_only(self._v, self._freq)
        if boxed:
            return [_fmt_dt_index_item(x, dates_only) for x in self._v]
        from .fmt import dt_strings
        return dt_strings(self._v)

    # -- fields
    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)
        if name in _DT_INT_FIELDS or name in _DT_BOOL_FIELDS or name in ('date', 'time', 'timetz'):
            return getattr(DatetimeMethods(self), name)
        raise AttributeError("'DatetimeIndex' object has no attribute '%s'" % name)

    def strftime(self, date_format):
        return DatetimeMethods(self).strftime(date_format)

    def day_name(self, locale=None):
        return DatetimeMethods(self).day_name()

    def month_name(self, locale=None):
        return DatetimeMethods(self).month_name()

    def normalize(self):
        r = DatetimeMethods(self).normalize()
        r._freq = self._freq
        return r

    def floor(self, freq, ambiguous='raise', nonexistent='raise'):
        return DatetimeMethods(self).floor(freq)

    def ceil(self, freq, ambiguous='raise', nonexistent='raise'):
        return DatetimeMethods(self).ceil(freq)

    def round(self, freq, ambiguous='raise', nonexistent='raise'):
        return DatetimeMethods(self).round(freq)

    def isocalendar(self):
        return DatetimeMethods(self).isocalendar()

    def to_pydatetime(self):
        return DatetimeMethods(self).to_pydatetime()

    def tz_localize(self, tz, ambiguous='raise', nonexistent='raise'):
        if tz is None:
            return self
        raise NotImplementedError('time zones are not supported')

    def tz_convert(self, tz):
        raise NotImplementedError('time zones are not supported')

    def as_unit(self, unit):
        return self._clone(list(self._v), self.name, self._freq, 'datetime64[%s]' % unit)

    def to_period(self, freq=None):
        raise NotImplementedError('Period is not supported')

    @property
    def is_normalized(self):
        for x in self._v:
            if x == x and (x.hour or x.minute or x.second or x.microsecond):
                return False
        return True

    @property
    def resolution(self):
        return index_resolution(self._v)

    def mean(self, axis=None, skipna=True):
        nn = [x._epoch_us() for x in self._v if x == x]
        if not nn:
            return NaT
        return T.ts_from_us(int(round(sum(nn) / len(nn))))

    # -- arithmetic
    def shift(self, periods=1, freq=None):
        fr = to_offset(freq) if freq is not None else self._freq
        if fr is None:
            raise NotImplementedError('Cannot shift with no freq')
        k = fr * periods
        return self._clone([(NaT if x is NaT else x + k) for x in self._v], self.name, self._freq)

    def __add__(self, other):
        if isinstance(other, BaseOffset):
            return self._clone([(NaT if x is NaT else other._add_ts(x)) for x in self._v], self.name,
                               self._freq if isinstance(other, Tick) else None)
        if isinstance(other, _dtm.timedelta):
            return self._clone([(NaT if x is NaT else x + other) for x in self._v], self.name, self._freq)
        if isinstance(other, TimedeltaIndex) or isinstance(other, (list, tuple, np.ndarray)):
            ol = _lst(other)
            if len(ol) != len(self._v):
                raise ValueError('Lengths must match')
            return self._clone([(NaT if (a is NaT or b is NaT) else a + b) for a, b in zip(self._v, ol)], self.name, None)
        if other is NaT:
            return self._clone([NaT] * len(self._v), self.name, None)
        if isinstance(other, str):
            return self.__add__(Timedelta(other))
        raise TypeError("cannot add DatetimeIndex and %s" % type(other).__name__)

    __radd__ = __add__

    def __sub__(self, other):
        if isinstance(other, (DatetimeIndex,)) or isinstance(other, (list, tuple, np.ndarray)):
            ol = _lst(other)
            if len(ol) != len(self._v):
                raise ValueError('cannot add indices of unequal length')
            if len(ol) > 0 and isinstance(ol[0], _dtm.timedelta):
                return self._clone([(NaT if (a is NaT or b is NaT) else a - b) for a, b in zip(self._v, ol)],
                                   self.name, None)
            return TimedeltaIndex([(NaT if (a is NaT or b is NaT) else a - b) for a, b in zip(self._v, ol)],
                                  name=self.name, _raw=True, _dt='timedelta64[%s]' % self.unit)
        if isinstance(other, BaseOffset):
            return self.__add__(-other)
        if isinstance(other, _dtm.timedelta):
            return self._clone([(NaT if x is NaT else x - other) for x in self._v], self.name, self._freq)
        if isinstance(other, (str, _dtm.datetime)):
            o = Timestamp(other) if not isinstance(other, Timestamp) else other
            return TimedeltaIndex([(NaT if x is NaT else x - o) for x in self._v], name=self.name, _raw=True,
                                  _dt='timedelta64[%s]' % self.unit, freq=self._freq)
        if other is NaT:
            return TimedeltaIndex([NaT] * len(self._v), name=self.name, _raw=True, _dt='timedelta64[%s]' % self.unit)
        raise TypeError("cannot subtract DatetimeIndex and %s" % type(other).__name__)

    def __rsub__(self, other):
        if isinstance(other, (str, _dtm.datetime)):
            o = Timestamp(other) if not isinstance(other, Timestamp) else other
            return TimedeltaIndex([(NaT if x is NaT else o - x) for x in self._v], name=self.name, _raw=True,
                                  _dt='timedelta64[%s]' % self.unit)
        raise TypeError('cannot subtract DatetimeIndex from %s' % type(other).__name__)

    def snap(self, freq='S'):
        raise NotImplementedError('snap')

    def diff(self, periods=1):
        n = len(self._v)
        out = [NaT] * n
        for i in range(n):
            j = i - periods
            if 0 <= j < n:
                out[i] = self._v[i] - self._v[j]
        return TimedeltaIndex(out, name=self.name, _raw=True, _dt='timedelta64[%s]' % self.unit)


# alias used by Index.shift in some pandas versions
class TimedeltaIndex(_DTLike):
    _kind = 'm'

    def __init__(self, data=None, unit=None, freq=_NO, dtype=None, copy=False, name=None, _dt=None, _raw=False):
        if data is None:
            data = []
        name = _clean_name(data, name)
        if _raw:
            vals = data
            dt = _dt or 'timedelta64[us]'
        else:
            src = _lst(data) if not isinstance(data, (str, int, float)) else [data]
            r = to_timedelta(list(src), unit=unit)
            vals = r._v
            dt = r._dt
        if freq is _NO:
            fr = getattr(data, '_freq', None) if isinstance(data, TimedeltaIndex) else None
        elif freq == 'infer':
            f = _infer_freq(vals)
            fr = to_offset(f) if f else None
        else:
            fr = to_offset(freq)
        self._setup(vals, dt, name, fr)

    def _clone(self, vals, name=-1, freq=None, dt=None):
        return TimedeltaIndex(vals, name=self.name if name == -1 else name, _dt=dt or self._dt, _raw=True,
                              freq=freq)

    def _conv_other(self, o):
        if isinstance(o, str):
            return Timedelta(o)
        if o is None:
            return NaT
        if isinstance(o, _dtm.timedelta) and type(o) is not Timedelta:
            return Timedelta._of(o._us)
        return o

    def _key_positions(self, key):
        k = self._conv_other(key)
        if isinstance(k, _dtm.timedelta):
            ps = self._locs(k)
            if not ps:
                raise KeyError(key)
            return ps, len(ps) == 1
        raise KeyError(key)

    def _fmt_labels(self, boxed=False):
        vals = self._v
        even = True
        for x in vals:
            if x == x and x._us % _DAY != 0:
                even = False
                break
        out = []
        for x in vals:
            if x is NaT or x != x:
                out.append('NaT')
            else:
                out.append(x._repr_base(None if even else 'long'))
        return out

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)
        if name in _TD_INT_FIELDS or name == 'components':
            return getattr(TimedeltaMethods(self), name)
        raise AttributeError("'TimedeltaIndex' object has no attribute '%s'" % name)

    def total_seconds(self):
        return TimedeltaMethods(self).total_seconds()

    def to_pytimedelta(self):
        return TimedeltaMethods(self).to_pytimedelta()

    def floor(self, freq):
        return TimedeltaMethods(self).floor(freq)

    def ceil(self, freq):
        return TimedeltaMethods(self).ceil(freq)

    def round(self, freq):
        return TimedeltaMethods(self).round(freq)

    def as_unit(self, unit):
        return self._clone(list(self._v), self.name, self._freq, 'timedelta64[%s]' % unit)

    def sum(self, axis=None, skipna=True):
        return Timedelta._of(sum([x._us for x in self._v if x == x]))

    def mean(self, axis=None, skipna=True):
        nn = [x._us for x in self._v if x == x]
        if not nn:
            return NaT
        return Timedelta._of(int(round(sum(nn) / len(nn))))

    def __add__(self, other):
        if isinstance(other, _dtm.datetime) or isinstance(other, str) and not _looks_td(other):
            o = as_timestamp(other)
            return DatetimeIndex([(NaT if x is NaT else o + x) for x in self._v], name=self.name, _raw=True,
                                 _dt='datetime64[%s]' % self.unit, freq=self._freq)
        if isinstance(other, _dtm.timedelta) or isinstance(other, str):
            o = _as_td(other)
            return self._clone([(NaT if x is NaT else x + o) for x in self._v], self.name, self._freq)
        if isinstance(other, DatetimeIndex):
            return other.__add__(self)
        if isinstance(other, (Index, list, tuple, np.ndarray)):
            ol = _lst(other)
            return self._clone([(NaT if (a is NaT or b is NaT) else a + b) for a, b in zip(self._v, ol)], self.name, None)
        if other is NaT:
            return self._clone([NaT] * len(self._v), self.name, None)
        if isinstance(other, BaseOffset) and isinstance(other, Tick):
            return self.__add__(other.delta)
        raise TypeError('cannot add TimedeltaIndex and %s' % type(other).__name__)

    __radd__ = __add__

    def __sub__(self, other):
        if isinstance(other, _dtm.timedelta) or isinstance(other, str):
            o = _as_td(other)
            return self._clone([(NaT if x is NaT else x - o) for x in self._v], self.name, self._freq)
        if isinstance(other, (Index, list, tuple, np.ndarray)):
            ol = _lst(other)
            return self._clone([(NaT if (a is NaT or b is NaT) else a - b) for a, b in zip(self._v, ol)], self.name, None)
        if other is NaT:
            return self._clone([NaT] * len(self._v), self.name, None)
        raise TypeError('cannot subtract TimedeltaIndex and %s' % type(other).__name__)

    def __rsub__(self, other):
        if isinstance(other, _dtm.datetime):
            o = as_timestamp(other)
            return DatetimeIndex([(NaT if x is NaT else o - x) for x in self._v], name=self.name, _raw=True,
                                 _dt='datetime64[%s]' % self.unit)
        if isinstance(other, _dtm.timedelta):
            o = _as_td(other)
            return self._clone([(NaT if x is NaT else o - x) for x in self._v], self.name, None)
        raise TypeError('cannot subtract TimedeltaIndex from %s' % type(other).__name__)

    def __mul__(self, k):
        if isinstance(k, (int, float)):
            fr = self._freq * k if (self._freq is not None and isinstance(k, int)) else None
            return self._clone([(NaT if x is NaT else x * k) for x in self._v], self.name, fr)
        ol = _lst(k)
        return self._clone([(NaT if x is NaT else x * y) for x, y in zip(self._v, ol)], self.name, None)

    __rmul__ = __mul__

    def __truediv__(self, o):
        if isinstance(o, (int, float)):
            fr = None
            if self._freq is not None and isinstance(self._freq, Tick):
                fr = to_offset(Timedelta._of(int(round(self._freq.n * self._freq._tickus / o))))
            return self._clone([(NaT if x is NaT else x / o) for x in self._v], self.name, fr)
        if isinstance(o, _dtm.timedelta) or isinstance(o, str):
            od = _as_td(o)
            r = Index([(nan if x is NaT else x / od) for x in self._v], name=self.name)
            return r
        ol = _lst(o)
        return Index([(nan if (x is NaT or y is NaT) else x / y) for x, y in zip(self._v, ol)], name=self.name)

    def __floordiv__(self, o):
        if isinstance(o, (int, float)):
            return self._clone([(NaT if x is NaT else x // o) for x in self._v], self.name, None)
        od = _as_td(o)
        return Index([(nan if x is NaT else x // od) for x in self._v], name=self.name)

    def __neg__(self):
        return self._clone([(NaT if x is NaT else -x) for x in self._v], self.name,
                           self._freq * -1 if self._freq is not None else None)

    def __abs__(self):
        return self._clone([(NaT if x is NaT else abs(x)) for x in self._v], self.name, None)

    def abs(self):
        return self.__abs__()

    def shift(self, periods=1, freq=None):
        raise NotImplementedError('shift')


def _looks_td(s):
    try:
        T._parse_td_string(s)
        return True
    except ValueError:
        return False


# ------------------------------------------------------------------------------------------ ranges

def _unit_arg(start, end, unit):
    if unit is not None:
        return unit
    return 'us'


def date_range(start=None, end=None, periods=None, freq=None, tz=None, normalize=False, name=None,
               inclusive='both', unit=None, **kw):
    if freq is None and (start is None or end is None or periods is None):
        freq = 'D'
    s = None if start is None else _to_ts(start)
    e = None if end is None else _to_ts(end)
    if (s is NaT) or (e is NaT):
        raise ValueError('Neither `start` nor `end` can be NaT')
    if normalize:
        if s is not None:
            s = s.normalize()
        if e is not None:
            e = e.normalize()
    out = []
    off = None
    if freq is None:
        # linear spacing
        if periods == 1:
            out = [s]
        else:
            a = s._epoch_us()
            b = e._epoch_us()
            step = (b - a) / (periods - 1)
            out = [T.ts_from_us(int(round(a + step * i))) for i in range(periods)]
    else:
        off = to_offset(freq)
        if s is not None and e is not None and periods is not None:
            raise ValueError('Of the four parameters: start, end, periods, and freq, exactly three must be specified')
        if s is None and e is None:
            raise ValueError('Of the four parameters: start, end, periods, and freq, exactly three must be specified')
        if s is not None and e is not None:
            cur = s if off.is_on_offset(s) else off.rollforward(s)
            guard = 0
            while cur <= e:
                out.append(cur)
                nxt = off._add_ts(cur) if not isinstance(off, Tick) else T.ts_from_us(cur._epoch_us() + off.n * off._tickus)
                cur = nxt
                guard += 1
                if guard > 2000000:
                    break
        elif s is not None:
            if periods is None:
                raise ValueError('Of the four parameters: start, end, periods, and freq, exactly three must be specified')
            cur = s if off.is_on_offset(s) else off.rollforward(s)
            for i in range(periods):
                out.append(cur)
                cur = off._add_ts(cur)
        else:
            if periods is None:
                raise ValueError('Of the four parameters: start, end, periods, and freq, exactly three must be specified')
            cur = e if off.is_on_offset(e) else off.rollback(e)
            noff = off * -1
            for i in range(periods):
                out.append(cur)
                cur = noff._add_ts(cur)
            out.reverse()
    if inclusive != 'both' and len(out) > 0 and s is not None and e is not None:
        if inclusive in ('left', 'neither') and out[-1] == e:
            out = out[:-1]
        if inclusive in ('right', 'neither') and len(out) > 0 and out[0] == s:
            out = out[1:]
    un = unit or 'us'
    if s is not None and unit is None:
        un = s._unit if s._unit != 'us' and False else 'us'
    res = DatetimeIndex(out, name=name, _raw=True, _dt='datetime64[%s]' % un, freq=off if freq is not None else None)
    if freq is not None and inclusive != 'both':
        res._freq = off
    return res


def bdate_range(start=None, end=None, periods=None, freq='B', tz=None, normalize=True, name=None, **kw):
    return date_range(start, end, periods, freq, tz, normalize, name)


def timedelta_range(start=None, end=None, periods=None, freq=None, name=None, closed=None, unit=None):
    if freq is None and (start is None or end is None or periods is None):
        freq = 'D'
    s = None if start is None else _as_td(start)
    e = None if end is None else _as_td(end)
    out = []
    off = None
    if freq is None:
        a, b = s._us, e._us
        step = (b - a) / (periods - 1) if periods > 1 else 0
        out = [Timedelta._of(int(round(a + step * i))) for i in range(periods)]
    else:
        off = to_offset(freq)
        if not isinstance(off, Tick):
            raise ValueError('freq must be a fixed frequency')
        stp = off.n * off._tickus
        if s is not None and e is not None:
            cur = s._us
            while cur <= e._us:
                out.append(Timedelta._of(cur))
                cur += stp
        elif s is not None:
            out = [Timedelta._of(s._us + stp * i) for i in range(periods)]
        else:
            out = [Timedelta._of(e._us - stp * (periods - 1 - i)) for i in range(periods)]
    return TimedeltaIndex(out, name=name, _raw=True, _dt='timedelta64[%s]' % (unit or 'us'), freq=off)


def infer_freq(index):
    vals = index._v if hasattr(index, '_v') else list(index)
    return _infer_freq(vals)


def is_dtkey(key):
    """scalar that addresses a datetime index (string or date-like)"""
    if isinstance(key, str):
        return True
    return isinstance(key, _dtm.date)


def td_reduce(vals, how):
    nn = [x for x in vals if x == x]
    if how == 'sum':
        return Timedelta._of(sum([x._us for x in nn]))
    return None


def dt_reduce(vals, how, dt, ddof=1):
    """mean / median / std of datetime-like values"""
    nn = [x for x in vals if x == x]
    isdt = dt.startswith('datetime')
    if len(nn) == 0:
        return NaT
    us = [(x._epoch_us() if isdt else x._us) for x in nn]
    n = len(us)
    if how == 'mean':
        r = int(round(sum(us) / n)) if n < 2 ** 20 else sum(us) // n
    elif how == 'median':
        a = sorted(us)
        r = a[n // 2] if n % 2 else int(round((a[n // 2 - 1] + a[n // 2]) / 2.0))
    else:
        if n - ddof <= 0:
            return NaT
        m = sum(us) / n
        v = sum([(u - m) * (u - m) for u in us]) / (n - ddof)
        r = int(v ** 0.5)
        return Timedelta._of(r)
    if isdt:
        return T.ts_from_us(r)
    return Timedelta._of(r)


def _regular(vals, off):
    """True when consecutive values are one step of the offset apart"""
    if len(vals) == 0:
        return False
    for i in range(1, len(vals)):
        a = vals[i - 1]
        if a is NaT or vals[i] is NaT:
            return False
        if isinstance(off, Tick):
            if isinstance(a, Timedelta):
                if vals[i]._us - a._us != off.n * off._tickus:
                    return False
            elif vals[i]._epoch_us() - a._epoch_us() != off.n * off._tickus:
                return False
        elif off._add_ts(a) != vals[i]:
            return False
    return True


def dt_describe(s, percentiles=None):
    from .core import Series, _pctlab
    isdt = s._dt.startswith('datetime')
    ps = percentiles if percentiles is not None else [0.25, 0.5, 0.75]
    nn = [x for x in s._v if x == x]
    us = sorted([(x._epoch_us() if isdt else x._us) for x in nn])
    n = len(us)

    def mk(u):
        if isdt:
            return T.ts_from_us(int(u))
        return Timedelta._of(int(u))

    def quant(q):
        if n == 0:
            return NaT
        pos = (n - 1) * q
        lo = int(pos)
        hi = lo + 1 if lo + 1 < n else lo
        fr = pos - lo
        return mk(us[lo] + (us[hi] - us[lo]) * fr)
    labs = ['count', 'mean']
    vals = [n, dt_reduce(s._v, 'mean', s._dt) if n else NaT]
    if not isdt:
        labs.append('std')
        vals.append(dt_reduce(s._v, 'std', s._dt) if n > 1 else NaT)
    labs.append('min')
    vals.append(mk(us[0]) if n else NaT)
    for p in ps:
        labs.append(_pctlab(p))
        vals.append(quant(p))
    labs.append('max')
    vals.append(mk(us[-1]) if n else NaT)
    return Series(vals, Index(labs), name=s.name, _dt='object')


def _json_scalar(x, fmt):
    if x is NaT:
        return None
    if isinstance(x, _dtm.datetime):
        if fmt == 'iso':
            t = '%04d-%02d-%02dT%02d:%02d:%02d' % (x.year, x.month, x.day, x.hour, x.minute, x.second)
            return t + '.%03d' % (x.microsecond // 1000) if True else t
        return as_timestamp(x)._epoch_us() // 1000
    if isinstance(x, _dtm.timedelta):
        if fmt == 'iso':
            return Timedelta._of(x._us).isoformat()
        return x._us // 1000
    return x


def json_prepare(o, fmt=None):
    if isinstance(o, dict):
        return {k: json_prepare(v, fmt) for k, v in o.items()}
    if isinstance(o, (list, tuple)):
        return [json_prepare(v, fmt) for v in o]
    return _json_scalar(o, fmt)


def json_key(k, fmt=None):
    if isinstance(k, (_dtm.datetime, _dtm.timedelta)):
        return str(_json_scalar(k, fmt))
    return str(k)
