# pandas subset: Timestamp, Timedelta, date offsets and string parsing (pure Python on top of pylib/datetime.py)
# Resolution is microseconds (pandas' sub-microsecond digits are not supported).
import datetime as _dtm
from .base import NaT, nan

_EPOCH_ORD = 719163
_EPOCH_US = 719163 * 86400 * 1000000
_DAY_US = 86400000000
_UNITS = ['s', 'ms', 'us', 'ns']
_MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October',
           'November', 'December']
_DAYS = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']
_WKDAY = ['MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT', 'SUN']
_MON3 = ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC']
_NO = ['__no_input__']


def _finer(a, b):
    ia = _UNITS.index(a) if a in _UNITS else 2
    ib = _UNITS.index(b) if b in _UNITS else 2
    return a if ia >= ib else b


def _dim(y, m):
    return _dtm._days_in_month(y, m)


def _isnum(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool)


# ------------------------------------------------------------------------------------------ Timedelta

class Components:
    def __init__(self, days, hours, minutes, seconds, milliseconds, microseconds, nanoseconds):
        self.days = days
        self.hours = hours
        self.minutes = minutes
        self.seconds = seconds
        self.milliseconds = milliseconds
        self.microseconds = microseconds
        self.nanoseconds = nanoseconds

    def _t(self):
        return (self.days, self.hours, self.minutes, self.seconds, self.milliseconds, self.microseconds,
                self.nanoseconds)

    def __iter__(self):
        return iter(self._t())

    def __getitem__(self, i):
        return self._t()[i]

    def __len__(self):
        return 7

    def __eq__(self, o):
        if isinstance(o, Components):
            return self._t() == o._t()
        return self._t() == o

    def __hash__(self):
        return hash(self._t())

    def __repr__(self):
        return ('Components(days=%d, hours=%d, minutes=%d, seconds=%d, milliseconds=%d, microseconds=%d, '
                'nanoseconds=%d)' % self._t())


_TD_UNIT_US = {'ns': 0.001, 'us': 1, 'ms': 1000, 's': 1000000, 'min': 60000000, 'h': 3600000000, 'D': _DAY_US,
               'W': 7 * _DAY_US}
_TD_ALIASES = {
    'nanoseconds': 'ns', 'nanosecond': 'ns', 'nanos': 'ns', 'nano': 'ns', 'ns': 'ns', 'n': 'ns',
    'microseconds': 'us', 'microsecond': 'us', 'micros': 'us', 'micro': 'us', 'us': 'us', 'u': 'us', 'µs': 'us',
    'milliseconds': 'ms', 'millisecond': 'ms', 'millis': 'ms', 'milli': 'ms', 'ms': 'ms', 'l': 'ms',
    'seconds': 's', 'second': 's', 'secs': 's', 'sec': 's', 's': 's',
    'minutes': 'min', 'minute': 'min', 'mins': 'min', 'min': 'min', 'm': 'min', 't': 'min',
    'hours': 'h', 'hour': 'h', 'hrs': 'h', 'hr': 'h', 'h': 'h',
    'days': 'D', 'day': 'D', 'd': 'D',
    'weeks': 'W', 'week': 'W', 'w': 'W',
}


def _unit_key(u):
    if u is None:
        return 'ns'
    k = _TD_ALIASES.get(u)
    if k is None:
        k = _TD_ALIASES.get(u.lower())
    if k is None:
        raise ValueError('invalid unit abbreviation: %s' % u)
    return k


def _round_half_even(x):
    return int(round(x))


def _num_to_us(v, unit):
    """number in the given unit -> microseconds (int)"""
    k = _unit_key(unit)
    f = _TD_UNIT_US[k]
    if isinstance(v, int):
        if k == 'ns':
            return _round_half_even(v / 1000.0) if abs(v) < 2 ** 52 else v // 1000
        return v * f
    return _round_half_even(v * f)


def _parse_td_string(s):
    """string -> microseconds (int) or None for NaT"""
    t = s.strip()
    if t == '':
        raise ValueError('Value must be Timedelta, string, integer, float, timedelta or convertible, not %s' % 'str')
    low = t.lower()
    if low in ('nat', 'nan', 'none', 'n/a'):
        return None
    neg = False
    i = 0
    n = len(low)
    if low[0] in '+-' and len(low) > 1 and low[1] == 'p':
        neg = low[0] == '-'
        i = 1
    if low.startswith('p', i):
        return _parse_iso_td(low[i:], neg)
    total = 0.0
    totint = 0
    isint = True
    found = False
    while i < n:
        while i < n and low[i] == ' ':
            i += 1
        if i >= n:
            break
        # number (possibly hh:mm:ss[.fff])
        j = i
        if low[j] in '+-':
            j += 1
        while j < n and (low[j].isdigit() or low[j] == '.'):
            j += 1
        if j == i or (j == i + 1 and low[i] in '+-'):
            raise ValueError("unit abbreviation w/o a number")
        numtxt = low[i:j]
        if j < n and low[j] == ':':
            # clock time h:mm[:ss[.fff]]
            k = j
            while k < n and (low[k].isdigit() or low[k] in ':.'):
                k += 1
            parts = low[i:k].split(':')
            sign = 1
            h = parts[0]
            if h[:1] == '-':
                sign = -1
                h = h[1:]
            elif h[:1] == '+':
                h = h[1:]
            hh = int(h)
            mm = int(parts[1]) if len(parts) > 1 else 0
            ss = parts[2] if len(parts) > 2 else '0'
            if '.' in ss:
                a, b = ss.split('.')
                us = int(a or '0') * 1000000 + int((b + '000000')[:6])
            else:
                us = int(ss) * 1000000
            totint += sign * (hh * 3600000000 + mm * 60000000 + us)
            i = k
            found = True
            continue
        # optional unit
        k = j
        while k < n and low[k] == ' ':
            k += 1
        u0 = k
        while k < n and (low[k].isalpha() or low[k] == 'µ'):
            k += 1
        unit = low[u0:k]
        if unit == '':
            if found or i != 0:
                raise ValueError('no units specified')
            # bare number: nanoseconds
            unit = 'ns'
        key = _TD_ALIASES.get(unit)
        if key is None:
            raise ValueError('invalid unit abbreviation: %s' % unit)
        if '.' in numtxt:
            isint = False
            total += float(numtxt) * _TD_UNIT_US[key]
        else:
            if key == 'ns':
                total += int(numtxt) / 1000.0
                isint = False
            else:
                totint += int(numtxt) * int(_TD_UNIT_US[key])
        found = True
        i = k
    if not found:
        raise ValueError('Invalid Timedelta string: %s' % s)
    return totint + (_round_half_even(total) if not isint else 0)


def _parse_iso_td(s, neg):
    # s starts with 'p': p[n]d[t[n]h[n]m[n]s]
    i = 1
    n = len(s)
    us = 0.0
    intime = False
    while i < n:
        if s[i] == 't':
            intime = True
            i += 1
            continue
        j = i
        while j < n and (s[j].isdigit() or s[j] in '.-'):
            j += 1
        v = float(s[i:j])
        c = s[j] if j < n else ''
        if c == 'd':
            us += v * _DAY_US
        elif c == 'w':
            us += v * 7 * _DAY_US
        elif c == 'h':
            us += v * 3600000000
        elif c == 'm':
            us += v * (60000000 if intime else 30 * _DAY_US)
        elif c == 's':
            us += v * 1000000
        else:
            raise ValueError('Invalid ISO 8601 Duration format')
        i = j + 1
    r = _round_half_even(us)
    return -r if neg else r


class Timedelta(_dtm.timedelta):
    _unit = 'us'

    def __init__(self, value=_NO, unit=None, days=0, seconds=0, microseconds=0, milliseconds=0, minutes=0, hours=0,
                 weeks=0, nanoseconds=0):
        us = None
        un = 'us'
        kw = (days, seconds, microseconds, milliseconds, minutes, hours, weeks, nanoseconds)
        if value is _NO:
            f = 1
            tot = 0.0
            allint = True
            for v in kw:
                if not isinstance(v, int):
                    allint = False
            if allint:
                us = ((weeks * 7 + days) * _DAY_US + hours * 3600000000 + minutes * 60000000 + seconds * 1000000
                      + milliseconds * 1000 + microseconds + _round_half_even(nanoseconds / 1000.0))
            else:
                us = _round_half_even((weeks * 7 + days) * float(_DAY_US) + hours * 3600000000.0
                                      + minutes * 60000000.0 + seconds * 1000000.0 + milliseconds * 1000.0
                                      + microseconds + nanoseconds / 1000.0)
        elif isinstance(value, str):
            r = _parse_td_string(value) if unit is None else None
            if unit is not None:
                r = _num_to_us(float(value), unit)
            if r is None:
                raise ValueError('NaT is not a valid Timedelta')
            us = r
        elif isinstance(value, _dtm.timedelta):
            us = value._us
            if isinstance(value, Timedelta):
                un = value._unit
        elif isinstance(value, bool):
            raise ValueError('Value must be Timedelta, string, integer, float, timedelta or convertible, not bool')
        elif _isnum(value):
            if value != value:
                raise ValueError('NaN is not a valid Timedelta')
            us = _num_to_us(value, unit)
            k = _unit_key(unit) if unit is not None else 'ns'
            if k in ('D', 'W', 'h', 'min', 's'):
                un = 's'
            elif k == 'ms':
                un = 'ms'
            elif k == 'ns':
                un = 'us'
        else:
            raise ValueError('Value must be Timedelta, string, integer, float, timedelta or convertible, not %s'
                             % type(value).__name__)
        _dtm.timedelta.__init__(self, 0, 0, us)
        self._unit = un

    # -- construction helpers
    @classmethod
    def _of(cls, us, unit='us'):
        r = Timedelta(microseconds=us)
        r._unit = unit
        return r

    # -- properties
    @property
    def unit(self):
        return self._unit

    @property
    def value(self):
        return self._us * 1000

    @property
    def _value(self):
        return self._us * 1000

    @property
    def nanoseconds(self):
        return 0

    @property
    def components(self):
        us = self._us
        d, rem = divmod(us, _DAY_US)
        secs, frac = divmod(rem, 1000000)
        return Components(d, secs // 3600, (secs // 60) % 60, secs % 60, frac // 1000, frac % 1000, 0)

    @property
    def resolution_string(self):
        us = self._us
        if us % _DAY_US == 0:
            return 'D'
        if us % 3600000000 == 0:
            return 'h'
        if us % 60000000 == 0:
            return 'min'
        if us % 1000000 == 0:
            return 's'
        if us % 1000 == 0:
            return 'ms'
        return 'us'

    @property
    def asm8(self):
        return self._us * 1000

    def total_seconds(self):
        return self._us / 1000000.0

    def to_pytimedelta(self):
        return _dtm.timedelta(microseconds=self._us)

    def to_timedelta64(self):
        return self._us * 1000

    def as_unit(self, unit, round_ok=True):
        r = Timedelta._of(self._us, unit)
        return r

    def isoformat(self):
        c = self.components
        neg = self._us < 0
        if neg:
            c = Timedelta._of(-self._us).components
        s = 'P%dDT%dH%dM%d' % (c.days, c.hours, c.minutes, c.seconds)
        frac = c.milliseconds * 1000 + c.microseconds
        if frac:
            s += ('.%06d' % frac).rstrip('0')
        s += 'S'
        return ('-' if neg else '') + s

    def _repr_base(self, format=None):
        us = self._us
        d, rem = divmod(us, _DAY_US)
        secs, frac = divmod(rem, 1000000)
        hh = secs // 3600
        mm = (secs // 60) % 60
        ss = secs % 60
        sign = ' +' if d < 0 else ' '
        subs = rem != 0
        sec = '%02d' % ss
        if frac:
            sec = '%02d.%06d' % (ss, frac)
        if format == 'sub_day' and d == 0:
            return '%02d:%02d:%s' % (hh, mm, sec)
        if subs or format == 'long':
            return '%d days%s%02d:%02d:%s' % (d, sign, hh, mm, sec)
        return '%d days' % d

    def __repr__(self):
        return "Timedelta('%s')" % self._repr_base('long')

    def __str__(self):
        return self._repr_base('long')

    def _round(self, freq, how):
        step = _tick_us(freq)
        us = self._us
        if how == 'floor':
            r = (us // step) * step
        elif how == 'ceil':
            r = -((-us) // step) * step
        else:
            q, m = divmod(us, step)
            if m * 2 > step or (m * 2 == step and q % 2 == 1):
                q += 1
            r = q * step
        return Timedelta._of(r, self._unit)

    def floor(self, freq):
        return self._round(freq, 'floor')

    def ceil(self, freq):
        return self._round(freq, 'ceil')

    def round(self, freq):
        return self._round(freq, 'round')

    # -- arithmetic
    def __add__(self, o):
        if isinstance(o, _dtm.timedelta):
            u = o._us
            return Timedelta._of(self._us + u, _finer(self._unit, getattr(o, '_unit', 'us')))
        if o is NaT:
            return NaT
        if isinstance(o, _dtm.datetime):
            return Timestamp(o) + self
        if isinstance(o, BaseOffset):
            return NotImplemented
        return NotImplemented

    def __radd__(self, o):
        return self.__add__(o)

    def __sub__(self, o):
        if isinstance(o, _dtm.timedelta):
            return Timedelta._of(self._us - o._us, _finer(self._unit, getattr(o, '_unit', 'us')))
        if o is NaT:
            return NaT
        return NotImplemented

    def __rsub__(self, o):
        if isinstance(o, _dtm.timedelta):
            return Timedelta._of(o._us - self._us, _finer(self._unit, getattr(o, '_unit', 'us')))
        if o is NaT:
            return NaT
        if isinstance(o, _dtm.datetime):
            return Timestamp(o) - self
        return NotImplemented

    def __neg__(self):
        return Timedelta._of(-self._us, self._unit)

    def __pos__(self):
        return self

    def __abs__(self):
        return Timedelta._of(abs(self._us), self._unit)

    def __mul__(self, k):
        if isinstance(k, bool):
            return NotImplemented
        if isinstance(k, int):
            return Timedelta._of(self._us * k, self._unit)
        if isinstance(k, float):
            if k != k:
                return NaT
            return Timedelta._of(_round_half_even(self._us * k), self._unit)
        return NotImplemented

    __rmul__ = __mul__

    def __truediv__(self, o):
        if isinstance(o, _dtm.timedelta):
            if o._us == 0:
                return nan
            return self._us / o._us
        if o is NaT:
            return nan
        if _isnum(o):
            if o == 0:
                raise ZeroDivisionError('division by zero')
            return Timedelta._of(_round_half_even(self._us / o), self._unit)
        return NotImplemented

    def __rtruediv__(self, o):
        if isinstance(o, _dtm.timedelta):
            return o._us / self._us
        return NotImplemented

    def __floordiv__(self, o):
        if isinstance(o, _dtm.timedelta):
            if o._us == 0:
                return nan
            return self._us // o._us
        if o is NaT:
            return nan
        if isinstance(o, int):
            return Timedelta._of(self._us // o, self._unit)
        if isinstance(o, float):
            return Timedelta._of(int(self._us // o), self._unit)
        return NotImplemented

    def __rfloordiv__(self, o):
        if isinstance(o, _dtm.timedelta):
            return o._us // self._us
        return NotImplemented

    def __mod__(self, o):
        if isinstance(o, _dtm.timedelta):
            return Timedelta._of(self._us % o._us, self._unit)
        if isinstance(o, int):
            return Timedelta._of(self._us % o, self._unit)
        return NotImplemented

    def __rmod__(self, o):
        if isinstance(o, _dtm.timedelta):
            return Timedelta._of(o._us % self._us, self._unit)
        return NotImplemented

    def __divmod__(self, o):
        if isinstance(o, _dtm.timedelta):
            q, r = divmod(self._us, o._us)
            return q, Timedelta._of(r, self._unit)
        return NotImplemented

    # -- comparisons
    def __eq__(self, o):
        if isinstance(o, _dtm.timedelta):
            return self._us == o._us
        return False

    def __ne__(self, o):
        return not self.__eq__(o)

    def _cmpv(self, o, op):
        if isinstance(o, _dtm.timedelta):
            return o._us
        if o is NaT:
            return None
        raise TypeError("'%s' not supported between instances of 'Timedelta' and '%s'" % (op, type(o).__name__))

    def __lt__(self, o):
        v = self._cmpv(o, '<')
        return v is not None and self._us < v

    def __le__(self, o):
        v = self._cmpv(o, '<=')
        return v is not None and self._us <= v

    def __gt__(self, o):
        v = self._cmpv(o, '>')
        return v is not None and self._us > v

    def __ge__(self, o):
        v = self._cmpv(o, '>=')
        return v is not None and self._us >= v

    def __hash__(self):
        return hash(self._us)

    def __bool__(self):
        return self._us != 0


Timedelta.min = Timedelta._of(-9223372036854775)
Timedelta.max = Timedelta._of(9223372036854775)
Timedelta.resolution = Timedelta._of(1)


def _as_td(x):
    """number / string / timedelta -> Timedelta (or NaT)"""
    if x is NaT:
        return NaT
    if isinstance(x, Timedelta):
        return x
    if isinstance(x, _dtm.timedelta):
        return Timedelta._of(x._us)
    return Timedelta(x)


# ------------------------------------------------------------------------------------------ parsing

def _split_tz(s):
    """strip a trailing Z / +hh:mm offset (ignored: values stay wall time)"""
    t = s
    if t.endswith('Z') or t.endswith('z'):
        return t[:-1], True
    k = len(t) - 1
    # +hh:mm / -hh:mm / +hhmm after a time part
    if ':' in t:
        for cand in (6, 5, 3):
            if len(t) > cand and t[-cand] in '+-' and t[-cand + 1:].replace(':', '').isdigit():
                return t[:-cand], True
    return t, False


def _mon_index(w):
    lw = w.lower()
    for k in range(12):
        nm = _MONTHS[k].lower()
        if lw == nm or lw == nm[:3] or (lw == 'sept' and k == 8):
            return k + 1
    return 0


def _wd_index(w):
    lw = w.lower()
    for k in range(7):
        nm = _DAYS[k].lower()
        if lw == nm or lw == nm[:3] or (lw == 'tues' and k == 1) or (lw == 'thur' and k == 3) or (lw == 'thurs' and k == 3):
            return k
    return -1


def _parse_time_part(t):
    """'10:30:15.123456 PM' -> (hh, mi, ss, us, reso)"""
    t = t.strip()
    ampm = None
    low = t.lower()
    if low.endswith('pm') or low.endswith('am'):
        ampm = low[-2:]
        t = t[:-2].strip()
    elif low.endswith('p.m.') or low.endswith('a.m.'):
        ampm = low[0 - 4] + 'm'
        t = t[:-4].strip()
    parts = t.split(':')
    if len(parts) > 3 or parts[0] == '':
        raise ValueError(t)
    for p in parts[:2]:
        if not p.isdigit():
            raise ValueError(t)
    hh = int(parts[0])
    mi = 0
    ss = 0
    us = 0
    reso = 'hour'
    if len(parts) > 1:
        mi = int(parts[1])
        reso = 'minute'
    if len(parts) > 2:
        sp = parts[2]
        if '.' in sp or ',' in sp:
            a, b = sp.replace(',', '.').split('.')
            if not (a.isdigit() or a == '') or not b.isdigit():
                raise ValueError(t)
            ss = int(a or '0')
            us = int((b + '000000')[:6])
            reso = 'microsecond'
        else:
            if not sp.isdigit():
                raise ValueError(t)
            ss = int(sp)
            reso = 'second'
    if ampm is not None:
        if hh < 1 or hh > 12:
            raise ValueError(t)
        if ampm == 'pm' and hh < 12:
            hh += 12
        elif ampm == 'am' and hh == 12:
            hh = 0
    if hh > 23 or mi > 59 or ss > 59:
        raise ValueError(t)
    return hh, mi, ss, us, reso


class DateParseError(ValueError):
    pass


def parse_datetime_string(s, dayfirst=False, yearfirst=False):
    """flexible date string parser -> (y, m, d, hh, mi, ss, us, reso); raises ValueError"""
    orig = s
    s = s.strip()
    if s == '':
        raise DateParseError('Unknown datetime string format, unable to parse: %s' % orig)
    s, _ = _split_tz(s)
    s = s.strip()
    # split date and time on 'T' or ' ' (time contains ':' or am/pm)
    date_s = s
    time_s = None
    k = s.find('T')
    if k > 0 and s[k - 1].isdigit() and k + 1 < len(s) and s[k + 1].isdigit():
        date_s = s[:k]
        time_s = s[k + 1:]
    elif ':' in s:
        # the time token: from the first token containing ':' backwards by one token
        toks = s.split(' ')
        ti = -1
        for q in range(len(toks)):
            if ':' in toks[q]:
                ti = q
                break
        date_s = ' '.join(toks[:ti]).strip()
        time_s = ' '.join(toks[ti:]).strip()
        if date_s == '':
            raise DateParseError('Unknown datetime string format, unable to parse: %s' % orig)
    else:
        low = s.lower()
        # '2024-01-01 10 PM' style: trailing am/pm with an hour
        toks = s.split(' ')
        if len(toks) >= 2 and toks[-1].lower() in ('am', 'pm') and toks[-2].isdigit() and len(toks) > 1:
            date_s = ' '.join(toks[:-2])
            time_s = toks[-2] + ' ' + toks[-1]
    hh = mi = ss = us = 0
    reso = None
    if time_s is not None:
        hh, mi, ss, us, reso = _parse_time_part(time_s)
    try:
        y, m, d, dreso = _parse_date_part(date_s, dayfirst, yearfirst)
    except ValueError:
        raise DateParseError('Unknown datetime string format, unable to parse: %s' % orig)
    if reso is None:
        reso = dreso
    if m < 1 or m > 12 or d < 1 or d > _dim(y, m):
        raise DateParseError('day is out of range for month: %s' % orig if m >= 1 and m <= 12 else 'month must be in 1..12: %s' % orig)
    return y, m, d, hh, mi, ss, us, reso


def _parse_date_part(s, dayfirst, yearfirst):
    s = s.strip()
    n = len(s)
    if n == 0:
        raise ValueError(s)
    # quarter: 2024Q1 / 2024-Q1
    ql = s.upper().replace('-', '')
    if len(ql) == 6 and ql[4] == 'Q' and ql[:4].isdigit() and ql[5] in '1234':
        return int(ql[:4]), (int(ql[5]) - 1) * 3 + 1, 1, 'quarter'
    if s.isdigit():
        if n == 4:
            return int(s), 1, 1, 'year'
        if n == 6:
            return int(s[:4]), int(s[4:]), 1, 'month'
        if n == 8:
            return int(s[:4]), int(s[4:6]), int(s[6:]), 'day'
        raise ValueError(s)
    # separators
    sep = None
    for c in '-/.':
        if c in s:
            sep = c
            break
    has_alpha = False
    for c in s:
        if c.isalpha():
            has_alpha = True
            break
    if not has_alpha and sep is not None:
        parts = s.split(sep)
        for p in parts:
            if not p.isdigit():
                raise ValueError(s)
        if len(parts) == 2:
            if len(parts[0]) == 4:
                return int(parts[0]), int(parts[1]), 1, 'month'
            if len(parts[1]) == 4:
                return int(parts[1]), int(parts[0]), 1, 'month'
            raise ValueError(s)
        if len(parts) == 3:
            a, b, c = parts
            if len(a) == 4:
                if yearfirst or True:
                    return int(a), int(b), int(c), 'day'
            if len(c) == 4:
                x, yv = int(a), int(b)
                if dayfirst or x > 12:
                    return int(c), yv, x, 'day'
                return int(c), x, yv, 'day'
            # two digit year
            x, yv, z = int(a), int(b), int(c)
            if yearfirst:
                return _y2(x), yv, z, 'day'
            if dayfirst:
                return _y2(z), yv, x, 'day'
            return _y2(z), x, yv, 'day'
        raise ValueError(s)
    # textual month
    toks = []
    cur = ''
    kind = ''
    for ch in s:
        if ch.isdigit():
            k = 'n'
        elif ch.isalpha():
            k = 'a'
        else:
            k = ' '
        if k != kind and cur:
            toks.append((kind, cur))
            cur = ''
        kind = k
        if k != ' ':
            cur += ch
    if cur:
        toks.append((kind, cur))
    mon = 0
    nums = []
    for kd, tx in toks:
        if kd == 'a':
            mi = _mon_index(tx)
            if mi:
                if mon:
                    raise ValueError(s)
                mon = mi
            elif _wd_index(tx) >= 0 or tx.lower() in ('st', 'nd', 'rd', 'th', 'of', 'the', 'at', 'on'):
                pass
            else:
                raise ValueError(s)
        else:
            nums.append(tx)
    if not mon:
        raise ValueError(s)
    year = None
    day = None
    rest = []
    for tx in nums:
        if len(tx) == 4 and year is None:
            year = int(tx)
        else:
            rest.append(int(tx))
    if len(rest) == 2 and year is None:
        day = rest[0]
        year = _y2(rest[1])
    elif len(rest) == 1:
        day = rest[0]
    elif len(rest) == 0:
        day = 1
    else:
        raise ValueError(s)
    if year is None:
        year = 1970
        raise ValueError(s)
    return year, mon, day, 'day'


def _y2(y):
    if y >= 100:
        return y
    return 2000 + y if y < 69 else 1900 + y


# ------------------------------------------------------------------------------------------ Timestamp

def _us_to_fields(us):
    """microseconds since 1970 -> (y, m, d, hh, mi, ss, micro)"""
    secs, micro = divmod(us, 1000000)
    days, rem = divmod(secs, 86400)
    y, m, d = _dtm._ord2ymd(days + _EPOCH_ORD)
    return y, m, d, rem // 3600, (rem % 3600) // 60, rem % 60, micro


def ts_from_us(us, unit='us'):
    f = _us_to_fields(us)
    r = Timestamp(f[0], f[1], f[2], f[3], f[4], f[5], f[6])
    r._unit = unit
    return r


def _tick_us(freq):
    """freq (str / offset / Timedelta) -> microseconds of a fixed frequency"""
    if isinstance(freq, _dtm.timedelta):
        return freq._us
    off = to_offset(freq)
    if not isinstance(off, Tick):
        raise ValueError('%s is a non-fixed frequency' % repr(off))
    return off.n * off._tickus


class Timestamp(_dtm.datetime):
    _unit = 'us'

    def __init__(self, ts_input=_NO, year=None, month=None, day=None, hour=None, minute=None, second=None,
                 microsecond=None, tzinfo=None, nanosecond=None, tz=None, unit=None, fold=None):
        y = mo = d = hh = mi = ss = us = None
        un = 'us'
        if ts_input is _NO:
            y, mo, d, hh, mi, ss, us = year, month, day, hour, minute, second, microsecond
            if y is None or mo is None or d is None:
                raise TypeError("Timestamp() requires year, month and day")
        elif isinstance(ts_input, int) and not isinstance(ts_input, bool) and year is not None and unit is None:
            # positional form Timestamp(year, month, day, hour, minute, second, microsecond)
            vals = [ts_input, year, month, day, hour, minute, second, microsecond]
            y, mo, d, hh, mi, ss, us = vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6]
            if d is None:
                raise TypeError("Timestamp() requires year, month and day")
        elif isinstance(ts_input, str):
            low = ts_input.strip().lower()
            if low in ('nat', 'nan', 'none', ''):
                raise ValueError('NaT is not a valid Timestamp')
            if low == 'now' or low == 'today':
                t = _dtm.datetime.now()
                y, mo, d, hh, mi, ss, us = t.year, t.month, t.day, t.hour, t.minute, t.second, t.microsecond
                if low == 'today':
                    hh = mi = ss = us = 0
            else:
                y, mo, d, hh, mi, ss, us, reso = parse_datetime_string(ts_input)
        elif isinstance(ts_input, _dtm.datetime):
            y, mo, d, hh, mi, ss, us = (ts_input.year, ts_input.month, ts_input.day, ts_input.hour,
                                        ts_input.minute, ts_input.second, ts_input.microsecond)
            if isinstance(ts_input, Timestamp):
                un = ts_input._unit
        elif isinstance(ts_input, _dtm.date):
            y, mo, d = ts_input.year, ts_input.month, ts_input.day
        elif _isnum(ts_input):
            if ts_input != ts_input:
                raise ValueError('NaN is not a valid Timestamp')
            k = _unit_key(unit) if unit is not None else 'ns'
            if k == 'ns':
                v = int(ts_input) // 1000 if isinstance(ts_input, int) else _round_half_even(ts_input / 1000.0)
            else:
                v = _num_to_us(ts_input, k)
            y, mo, d, hh, mi, ss, us = _us_to_fields(v)
            if unit is not None and k in ('D', 'h', 'min', 's'):
                un = 's'
            elif unit is not None and k == 'ms':
                un = 'ms'
        else:
            raise TypeError('Cannot convert input [%s] of type %s to Timestamp' % (ts_input, type(ts_input).__name__))
        if hh is None:
            hh = 0
        if mi is None:
            mi = 0
        if ss is None:
            ss = 0
        if us is None:
            us = 0
        if nanosecond:
            us += _round_half_even(nanosecond / 1000.0)
        _dtm.datetime.__init__(self, y, mo, d, hh, mi, ss, us, None)
        self._unit = un

    # -- construction
    @classmethod
    def _of(cls, us, unit='us'):
        return ts_from_us(us, unit)

    @classmethod
    def now(cls, tz=None):
        t = _dtm.datetime.now()
        return Timestamp(t)

    @classmethod
    def today(cls):
        return Timestamp(_dtm.datetime.now())

    @classmethod
    def utcnow(cls):
        return Timestamp(_dtm.datetime.utcnow())

    @classmethod
    def fromtimestamp(cls, t, tz=None):
        return ts_from_us(_round_half_even(t * 1000000.0))

    @classmethod
    def utcfromtimestamp(cls, t):
        return ts_from_us(_round_half_even(t * 1000000.0))

    @classmethod
    def fromordinal(cls, n, tz=None):
        y, m, d = _dtm._ord2ymd(n)
        return Timestamp(y, m, d)

    @classmethod
    def fromisoformat(cls, s):
        return Timestamp(_dtm.datetime.fromisoformat(s))

    @classmethod
    def combine(cls, date, time):
        return Timestamp(date.year, date.month, date.day, time.hour, time.minute, time.second, time.microsecond)

    @classmethod
    def strptime(cls, s, fmt):
        return Timestamp(_dtm.datetime.strptime(s, fmt))

    # -- properties
    @property
    def unit(self):
        return self._unit

    @property
    def value(self):
        return self._epoch_us() * 1000

    @property
    def _value(self):
        return self._epoch_us() * 1000

    def _epoch_us(self):
        return _dtm.datetime._us(self) - _EPOCH_US

    @property
    def nanosecond(self):
        return 0

    @property
    def dayofweek(self):
        return self.weekday()

    day_of_week = dayofweek

    @property
    def dayofyear(self):
        return _dtm._days_before_month(self.year, self.month) + self.day

    day_of_year = dayofyear

    @property
    def quarter(self):
        return (self.month - 1) // 3 + 1

    @property
    def week(self):
        return _dtm._isocal(self.year, self.month, self.day)[1]

    weekofyear = week

    @property
    def days_in_month(self):
        return _dim(self.year, self.month)

    daysinmonth = days_in_month

    @property
    def is_leap_year(self):
        return _dtm._is_leap(self.year)

    @property
    def is_month_start(self):
        return self.day == 1

    @property
    def is_month_end(self):
        return self.day == _dim(self.year, self.month)

    @property
    def is_quarter_start(self):
        return self.day == 1 and self.month % 3 == 1

    @property
    def is_quarter_end(self):
        return self.month % 3 == 0 and self.day == _dim(self.year, self.month)

    @property
    def is_year_start(self):
        return self.day == 1 and self.month == 1

    @property
    def is_year_end(self):
        return self.month == 12 and self.day == 31

    @property
    def tz(self):
        return None

    @property
    def freq(self):
        return None

    @property
    def asm8(self):
        return self._epoch_us() * 1000

    def day_name(self, locale=None):
        return _DAYS[self.weekday()]

    def month_name(self, locale=None):
        return _MONTHS[self.month - 1]

    def to_pydatetime(self, warn=True):
        return _dtm.datetime(self.year, self.month, self.day, self.hour, self.minute, self.second, self.microsecond)

    def to_datetime64(self):
        return self._epoch_us() * 1000

    def to_period(self, freq=None):
        raise NotImplementedError('Period is not supported')

    def tz_localize(self, tz, ambiguous='raise', nonexistent='raise'):
        if tz is None:
            return self
        raise NotImplementedError('time zones are not supported')

    def tz_convert(self, tz):
        raise NotImplementedError('time zones are not supported')

    def as_unit(self, unit, round_ok=True):
        r = ts_from_us(self._epoch_us(), unit)
        return r

    def normalize(self):
        r = Timestamp(self.year, self.month, self.day)
        r._unit = self._unit
        return r

    def replace(self, year=None, month=None, day=None, hour=None, minute=None, second=None, microsecond=None,
                nanosecond=None, tzinfo=True, fold=None):
        r = Timestamp(self.year if year is None else year, self.month if month is None else month,
                      self.day if day is None else day, self.hour if hour is None else hour,
                      self.minute if minute is None else minute, self.second if second is None else second,
                      self.microsecond if microsecond is None else microsecond)
        r._unit = self._unit
        return r

    def date(self):
        return _dtm.date(self.year, self.month, self.day)

    def time(self):
        return _dtm.time(self.hour, self.minute, self.second, self.microsecond)

    def isoformat(self, sep='T', timespec='auto'):
        return _dtm.datetime.isoformat(self, sep, timespec)

    def _round(self, freq, how):
        step = _tick_us(freq)
        us = self._epoch_us()
        if how == 'floor':
            r = (us // step) * step
        elif how == 'ceil':
            r = -((-us) // step) * step
        else:
            q, m = divmod(us, step)
            if m * 2 > step or (m * 2 == step and q % 2 == 1):
                q += 1
            r = q * step
        return ts_from_us(r, self._unit)

    def floor(self, freq, ambiguous='raise', nonexistent='raise'):
        return self._round(freq, 'floor')

    def ceil(self, freq, ambiguous='raise', nonexistent='raise'):
        return self._round(freq, 'ceil')

    def round(self, freq='us', ambiguous='raise', nonexistent='raise'):
        return self._round(freq, 'round')

    def timestamp(self):
        return self._epoch_us() / 1000000.0

    # -- text
    def _text(self):
        s = '%04d-%02d-%02d %02d:%02d:%02d' % (self.year, self.month, self.day, self.hour, self.minute, self.second)
        if self.microsecond:
            s += '.%06d' % self.microsecond
        return s

    @property
    def _date_repr(self):
        return '%04d-%02d-%02d' % (self.year, self.month, self.day)

    def __repr__(self):
        return "Timestamp('%s')" % self._text()

    def __str__(self):
        return self._text()

    def __format__(self, spec):
        if spec:
            return self.strftime(spec)
        return self._text()

    # -- arithmetic
    def __add__(self, o):
        if isinstance(o, _dtm.timedelta):
            return ts_from_us(self._epoch_us() + o._us, _finer(self._unit, getattr(o, '_unit', 'us')))
        if o is NaT:
            return NaT
        if isinstance(o, BaseOffset):
            return o._add_ts(self)
        if isinstance(o, (int, float)):
            raise TypeError('Addition/subtraction of integers and integer-arrays with Timestamp is no longer supported.  Instead of adding/subtracting `n`, use `n * obj.freq`')
        return NotImplemented

    def __radd__(self, o):
        return self.__add__(o)

    def __sub__(self, o):
        if isinstance(o, _dtm.datetime):
            ou = o._epoch_us() if isinstance(o, Timestamp) else o._us() - _EPOCH_US
            return Timedelta._of(self._epoch_us() - ou, _finer(self._unit, getattr(o, '_unit', 'us')))
        if isinstance(o, _dtm.timedelta):
            return ts_from_us(self._epoch_us() - o._us, _finer(self._unit, getattr(o, '_unit', 'us')))
        if o is NaT:
            return NaT
        if isinstance(o, BaseOffset):
            return (-o)._add_ts(self)
        return NotImplemented

    def __rsub__(self, o):
        if isinstance(o, _dtm.datetime):
            return Timedelta._of(o._us() - _EPOCH_US - self._epoch_us())
        if o is NaT:
            return NaT
        return NotImplemented

    # -- comparison
    def _ckey(self, o, op):
        if isinstance(o, _dtm.datetime):
            if isinstance(o, Timestamp):
                return o._epoch_us()
            return o._us() - _EPOCH_US
        if o is NaT:
            return None
        raise TypeError("'%s' not supported between instances of 'Timestamp' and '%s'" % (op, type(o).__name__))

    def __eq__(self, o):
        if isinstance(o, _dtm.datetime):
            return self._epoch_us() == (o._epoch_us() if isinstance(o, Timestamp) else o._us() - _EPOCH_US)
        return False

    def __ne__(self, o):
        return not self.__eq__(o)

    def __lt__(self, o):
        k = self._ckey(o, '<')
        return k is not None and self._epoch_us() < k

    def __le__(self, o):
        k = self._ckey(o, '<=')
        return k is not None and self._epoch_us() <= k

    def __gt__(self, o):
        k = self._ckey(o, '>')
        return k is not None and self._epoch_us() > k

    def __ge__(self, o):
        k = self._ckey(o, '>=')
        return k is not None and self._epoch_us() >= k

    def __hash__(self):
        return hash(_dtm.datetime._us(self))


Timestamp.min = ts_from_us(-9223372036854775)
Timestamp.max = ts_from_us(9223372036854775)
Timestamp.resolution = Timedelta._of(1)


def as_timestamp(x):
    """datetime-like -> Timestamp (NaT passes)"""
    if x is NaT:
        return NaT
    if type(x) is Timestamp:
        return x
    if isinstance(x, _dtm.datetime):
        return Timestamp(x)
    if isinstance(x, _dtm.date):
        return Timestamp(x)
    return Timestamp(x)


# ------------------------------------------------------------------------------------------ offsets

def _add_months(y, m, k):
    t = y * 12 + (m - 1) + k
    return t // 12, t % 12 + 1


def _month_end_ts(ts, y, m):
    r = Timestamp(y, m, _dim(y, m), ts.hour, ts.minute, ts.second, ts.microsecond)
    r._unit = ts._unit
    return r


def _month_begin_ts(ts, y, m):
    r = Timestamp(y, m, 1, ts.hour, ts.minute, ts.second, ts.microsecond)
    r._unit = ts._unit
    return r


class BaseOffset:
    _prefix = ''
    _cls_name = 'BaseOffset'
    _tickus = None

    def __init__(self, n=1, normalize=False):
        self.n = n
        self.normalize = normalize

    # -- naming
    def _suffix(self):
        return ''

    @property
    def rule_code(self):
        return self._prefix + self._suffix()

    @property
    def freqstr(self):
        b = self.rule_code
        if self.n == 1:
            return b
        return '%d%s' % (self.n, b)

    name = rule_code

    @property
    def base(self):
        return self.__class__(1)

    def _attrs_repr(self):
        return ''

    def __repr__(self):
        nm = self.__class__.__name__
        a = self._attrs_repr()
        if self.n == 1:
            return '<%s%s>' % (nm, a)
        return '<%d * %ss%s>' % (self.n, nm, a)

    def _key(self):
        return (self.__class__.__name__, self.n, self._suffix())

    def __eq__(self, o):
        if isinstance(o, str):
            try:
                o = to_offset(o)
            except ValueError:
                return False
        if not isinstance(o, BaseOffset):
            return False
        return self._key() == o._key()

    def __ne__(self, o):
        return not self.__eq__(o)

    def __hash__(self):
        return hash(self._key())

    def _copy(self, n):
        raise NotImplementedError

    def __mul__(self, k):
        if isinstance(k, int):
            return self._copy(self.n * k)
        return NotImplemented

    __rmul__ = __mul__

    def __neg__(self):
        return self._copy(-self.n)

    # -- application
    def _add_ts(self, ts):
        r = self._apply(ts)
        if self.normalize:
            r = r.normalize()
        return r

    def __add__(self, o):
        if isinstance(o, _dtm.datetime):
            return self._add_ts(as_timestamp(o))
        if isinstance(o, BaseOffset):
            return NotImplemented
        if o is NaT:
            return NaT
        return NotImplemented

    def __radd__(self, o):
        return self.__add__(o)

    def __rsub__(self, o):
        return (-self).__add__(o)

    def __call__(self, o):
        return self.__add__(o)

    def rollforward(self, ts):
        ts = as_timestamp(ts)
        if self.is_on_offset(ts):
            return ts
        return ts + self.__class__(1, **self._kw())

    def rollback(self, ts):
        ts = as_timestamp(ts)
        if self.is_on_offset(ts):
            return ts
        return ts - self.__class__(1, **self._kw())

    def _kw(self):
        return {}

    def is_on_offset(self, ts):
        return True


class Tick(BaseOffset):
    def _suffix(self):
        return ''

    def _copy(self, n):
        return self.__class__(n)

    @property
    def nanos(self):
        return self.n * self._tickus * 1000

    @property
    def delta(self):
        return Timedelta._of(self.n * self._tickus)

    def _apply(self, ts):
        return ts_from_us(ts._epoch_us() + self.n * self._tickus, ts._unit)

    def __add__(self, o):
        if isinstance(o, Tick):
            if o._tickus == self._tickus:
                return self._copy(self.n + o.n)
            return Timedelta._of(self.n * self._tickus + o.n * o._tickus)
        if isinstance(o, _dtm.timedelta):
            return Timedelta._of(self.n * self._tickus + o._us)
        return BaseOffset.__add__(self, o)

    def is_on_offset(self, ts):
        return True

    def rollforward(self, ts):
        return as_timestamp(ts)

    rollback = rollforward


class Day(Tick):
    _prefix = 'D'
    _tickus = _DAY_US


class Hour(Tick):
    _prefix = 'h'
    _tickus = 3600000000


class Minute(Tick):
    _prefix = 'min'
    _tickus = 60000000


class Second(Tick):
    _prefix = 's'
    _tickus = 1000000


class Milli(Tick):
    _prefix = 'ms'
    _tickus = 1000


class Micro(Tick):
    _prefix = 'us'
    _tickus = 1


class Nano(Tick):
    _prefix = 'ns'
    _tickus = 0.001


class Week(BaseOffset):
    _prefix = 'W'

    def __init__(self, n=1, normalize=False, weekday=None):
        BaseOffset.__init__(self, n, normalize)
        self.weekday = weekday

    def _suffix(self):
        if self.weekday is None:
            return ''
        return '-' + _WKDAY[self.weekday]

    def _attrs_repr(self):
        if self.weekday is None:
            return ''
        return ': weekday=%d' % self.weekday

    def _key(self):
        return ('Week', self.n, self.weekday)

    def _copy(self, n):
        return Week(n, self.normalize, self.weekday)

    def _kw(self):
        return {'weekday': self.weekday}

    def is_on_offset(self, ts):
        return self.weekday is None or ts.weekday() == self.weekday

    def _apply(self, ts):
        n = self.n
        if self.weekday is None:
            return ts + Timedelta._of(7 * n * _DAY_US)
        w = self.weekday
        if n > 0:
            d = (w - ts.weekday()) % 7
            days = d + 7 * (n - 1) if d != 0 else 7 * n
        else:
            d = (ts.weekday() - w) % 7
            days = -(d + 7 * (-n - 1)) if d != 0 else 7 * n
        return ts + Timedelta._of(days * _DAY_US)


class MonthEnd(BaseOffset):
    _prefix = 'ME'

    def _copy(self, n):
        return MonthEnd(n, self.normalize)

    def is_on_offset(self, ts):
        return ts.day == _dim(ts.year, ts.month)

    def _apply(self, ts):
        n = self.n
        y, m = ts.year, ts.month
        if n > 0:
            k = n if ts.day == _dim(y, m) else n - 1
        else:
            k = n
        y2, m2 = _add_months(y, m, k)
        return _month_end_ts(ts, y2, m2)


class MonthBegin(BaseOffset):
    _prefix = 'MS'

    def _copy(self, n):
        return MonthBegin(n, self.normalize)

    def is_on_offset(self, ts):
        return ts.day == 1

    def _apply(self, ts):
        n = self.n
        y, m = ts.year, ts.month
        if n > 0:
            k = n
        else:
            k = n + 1 if ts.day > 1 else n
        y2, m2 = _add_months(y, m, k)
        return _month_begin_ts(ts, y2, m2)


class _AnchoredMonth(BaseOffset):
    _kind = ''

    def __init__(self, n=1, normalize=False, month=None):
        BaseOffset.__init__(self, n, normalize)
        self.month = month if month is not None else self._defmonth

    def _suffix(self):
        return '-' + _MON3[self.month - 1]

    def _kw(self):
        return {'month': self.month}

    def _key(self):
        return (self.__class__.__name__, self.n, self.month)


class YearEnd(_AnchoredMonth):
    _prefix = 'YE'
    _defmonth = 12

    def _attrs_repr(self):
        return ': month=%d' % self.month

    def _copy(self, n):
        return YearEnd(n, self.normalize, self.month)

    def is_on_offset(self, ts):
        return ts.month == self.month and ts.day == _dim(ts.year, ts.month)

    def _apply(self, ts):
        n = self.n
        y = ts.year
        m = self.month
        cur = (ts.month, ts.day)
        anchor = (m, _dim(y, m))
        if n > 0:
            if cur == anchor:
                yy = y + n
            elif cur < anchor:
                yy = y + n - 1
            else:
                yy = y + n
        else:
            if cur > anchor:
                yy = y + n + 1
            else:
                yy = y + n
        return _month_end_ts(ts, yy, m)


class YearBegin(_AnchoredMonth):
    _prefix = 'YS'
    _defmonth = 1

    def _attrs_repr(self):
        return ': month=%d' % self.month

    def _copy(self, n):
        return YearBegin(n, self.normalize, self.month)

    def is_on_offset(self, ts):
        return ts.month == self.month and ts.day == 1

    def _apply(self, ts):
        n = self.n
        y = ts.year
        m = self.month
        cur = (ts.month, ts.day)
        anchor = (m, 1)
        if n > 0:
            if cur == anchor:
                yy = y + n
            elif cur < anchor:
                yy = y + n - 1
            else:
                yy = y + n
        else:
            if cur > anchor:
                yy = y + n + 1
            else:
                yy = y + n
        return _month_begin_ts(ts, yy, m)


class QuarterEnd(_AnchoredMonth):
    _prefix = 'QE'
    _defmonth = 12

    def _attrs_repr(self):
        return ': startingMonth=%d' % self.month

    def _copy(self, n):
        return QuarterEnd(n, self.normalize, self.month)

    def is_on_offset(self, ts):
        return (ts.month - self.month) % 3 == 0 and ts.day == _dim(ts.year, ts.month)

    def _apply(self, ts):
        n = self.n
        y, m = ts.year, ts.month
        if n > 0:
            r = (self.month - m) % 3
            if r == 0:
                first = 0 if ts.day < _dim(y, m) else 3
            else:
                first = r
            k = first + 3 * (n - 1)
        else:
            r2 = (m - self.month) % 3
            first = -3 if r2 == 0 else -r2
            k = first + 3 * (n + 1)
        y2, m2 = _add_months(y, m, k)
        return _month_end_ts(ts, y2, m2)


class QuarterBegin(_AnchoredMonth):
    _prefix = 'QS'
    _defmonth = 1

    def _attrs_repr(self):
        return ': startingMonth=%d' % self.month

    def _copy(self, n):
        return QuarterBegin(n, self.normalize, self.month)

    def is_on_offset(self, ts):
        return (ts.month - self.month) % 3 == 0 and ts.day == 1

    def _apply(self, ts):
        n = self.n
        y, m = ts.year, ts.month
        if n > 0:
            r = (self.month - m) % 3
            if r == 0:
                first = 3
            else:
                first = r
            k = first + 3 * (n - 1)
        else:
            r2 = (m - self.month) % 3
            if r2 == 0:
                first = 0 if ts.day > 1 else -3
            else:
                first = -r2
            k = first + 3 * (n + 1)
        y2, m2 = _add_months(y, m, k)
        return _month_begin_ts(ts, y2, m2)


class BusinessDay(BaseOffset):
    _prefix = 'B'

    def _copy(self, n):
        return BusinessDay(n, self.normalize)

    def is_on_offset(self, ts):
        return ts.weekday() < 5

    def _apply(self, ts):
        n = self.n
        step = 1 if n > 0 else -1
        cnt = abs(n)
        us = ts._epoch_us()
        wd = ts.weekday()
        days = 0
        if n > 0 and wd >= 5:
            # from a weekend the first business day is the following Monday
            days = 7 - wd
            cnt -= 1
        elif n < 0 and wd >= 5:
            days = -(wd - 4)
            cnt -= 1
        wd = (wd + days) % 7
        while cnt > 0:
            days += step
            wd = (wd + step) % 7
            if wd < 5:
                cnt -= 1
        return ts + Timedelta._of(days * _DAY_US)

    def rollforward(self, ts):
        ts = as_timestamp(ts)
        if ts.weekday() < 5:
            return ts
        return ts + Timedelta._of((7 - ts.weekday()) * _DAY_US)

    def rollback(self, ts):
        ts = as_timestamp(ts)
        if ts.weekday() < 5:
            return ts
        return ts - Timedelta._of((ts.weekday() - 4) * _DAY_US)


class DateOffset(BaseOffset):
    _prefix = 'DateOffset'

    def __init__(self, n=1, normalize=False, years=0, months=0, weeks=0, days=0, hours=0, minutes=0, seconds=0,
                 milliseconds=0, microseconds=0):
        BaseOffset.__init__(self, n, normalize)
        self.years = years
        self.months = months
        self.weeks = weeks
        self.days = days
        self.hours = hours
        self.minutes = minutes
        self.seconds = seconds
        self.milliseconds = milliseconds
        self.microseconds = microseconds

    def _fields(self):
        out = []
        for k in ('years', 'months', 'weeks', 'days', 'hours', 'minutes', 'seconds', 'milliseconds', 'microseconds'):
            v = getattr(self, k)
            if v:
                out.append((k, v))
        return out

    @property
    def kwds(self):
        return dict(self._fields())

    def _suffix(self):
        return ''

    @property
    def rule_code(self):
        return 'DateOffset'

    def _attrs_repr(self):
        f = self._fields()
        if not f:
            return ''
        return ': ' + ', '.join(['%s=%s' % (k, v) for k, v in f])

    def __repr__(self):
        a = self._attrs_repr()
        if self.n == 1:
            return '<DateOffset%s>' % a
        return '<%d * DateOffsets%s>' % (self.n, a)

    def _key(self):
        return ('DateOffset', self.n) + tuple(self._fields())

    def _copy(self, n):
        return DateOffset(n, self.normalize, self.years, self.months, self.weeks, self.days, self.hours,
                          self.minutes, self.seconds, self.milliseconds, self.microseconds)

    def _apply(self, ts):
        n = self.n
        y, m, d = ts.year, ts.month, ts.day
        tm = n * (self.years * 12 + self.months)
        if tm:
            y, m = _add_months(y, m, tm)
            if d > _dim(y, m):
                d = _dim(y, m)
        r = Timestamp(y, m, d, ts.hour, ts.minute, ts.second, ts.microsecond)
        r._unit = ts._unit
        us = ((self.weeks * 7 + self.days) * _DAY_US + self.hours * 3600000000 + self.minutes * 60000000
              + self.seconds * 1000000 + self.milliseconds * 1000 + self.microseconds)
        if us:
            r = r + Timedelta._of(n * us)
        return r

    def is_on_offset(self, ts):
        return True

    def rollforward(self, ts):
        return as_timestamp(ts)

    rollback = rollforward


_OFFSETS = {
    'D': Day, 'h': Hour, 'min': Minute, 's': Second, 'ms': Milli, 'us': Micro, 'ns': Nano,
    'B': BusinessDay, 'MS': MonthBegin, 'ME': MonthEnd,
}
_LEGACY = {'H': 'h', 'T': 'min', 'S': 's', 'L': 'ms', 'U': 'us', 'N': 'ns', 'M': 'ME', 'BM': 'BME',
           'A': 'YE', 'AS': 'YS', 'Y': 'YE', 'Q': 'QE', 'bh': 'bh', 'd': 'D', 'b': 'B', 'min': 'min',
           'MIN': 'min', 'ME': 'ME', 'H': 'h', 'W': 'W'}


def _mon_num(s):
    k = _MON3.index(s.upper()) if s.upper() in _MON3 else -1
    if k < 0:
        raise ValueError('Invalid frequency: %s' % s)
    return k + 1


def to_offset(freq):
    if freq is None:
        return None
    if isinstance(freq, BaseOffset):
        return freq
    if isinstance(freq, _dtm.timedelta):
        us = freq._us
        for cls in (Day, Hour, Minute, Second, Milli, Micro):
            if us % cls._tickus == 0:
                return cls(us // cls._tickus)
        return Micro(us)
    if isinstance(freq, tuple):
        return to_offset(freq[0]) * freq[1]
    if not isinstance(freq, str):
        raise ValueError('Invalid frequency: %s' % (freq,))
    s = freq.strip()
    # compound like '1h30min' : sum of ticks
    i = 0
    n = len(s)
    if n == 0:
        raise ValueError('Invalid frequency: %s' % freq)
    j = 0
    if s[0] in '+-':
        j = 1
    while j < n and s[j].isdigit():
        j += 1
    numtxt = s[:j]
    rest = s[j:]
    if numtxt in ('', '+'):
        mult = 1
    elif numtxt == '-':
        mult = -1
    else:
        mult = int(numtxt)
    if rest == '':
        raise ValueError('Invalid frequency: %s' % freq)
    code = rest
    suffix = None
    if '-' in rest:
        code, suffix = rest.split('-', 1)
    if code in _LEGACY and code not in _OFFSETS and code not in ('ME', 'W'):
        if code in ('M', 'A', 'Y', 'Q', 'AS', 'BM') and False:
            pass
        code = _LEGACY[code]
    elif code == 'ME' or code == 'W':
        pass
    # weeks
    if code == 'W':
        wd = 6
        if suffix is not None:
            if suffix.upper() not in _WKDAY:
                raise ValueError('Invalid frequency: %s' % freq)
            wd = _WKDAY.index(suffix.upper())
        return Week(mult, False, wd)
    if code in ('YE', 'YS', 'QE', 'QS'):
        cls = {'YE': YearEnd, 'YS': YearBegin, 'QE': QuarterEnd, 'QS': QuarterBegin}[code]
        if suffix is None:
            return cls(mult)
        return cls(mult, False, _mon_num(suffix))
    if code in _OFFSETS:
        if suffix is not None:
            raise ValueError('Invalid frequency: %s' % freq)
        return _OFFSETS[code](mult)
    raise ValueError('Invalid frequency: %s, failed to parse with error message: ValueError("Invalid frequency: %s")'
                     % (freq, freq) if False else 'Invalid frequency: %s' % freq)


# ------------------------------------------------------------------------------------------ misc
def tick_us_of(freq):
    return _tick_us(freq)


def infer_datelike(vals):
    """list of datetime / timedelta objects (+ missing) -> (converted list, dtype name) or None"""
    from .base import NA
    kind = None
    unit = None
    for x in vals:
        if x is None or x is NA or x is NaT or (type(x) is float and x != x):
            continue
        if isinstance(x, _dtm.datetime):
            k = 'M'
        elif isinstance(x, _dtm.timedelta):
            k = 'm'
        else:
            return None
        if kind is None:
            kind = k
        elif kind != k:
            return None
        u = getattr(x, '_unit', 'us')
        unit = u if unit is None else _finer(unit, u)
    if kind is None:
        return None
    out = []
    for x in vals:
        if x is None or x is NA or x is NaT or (type(x) is float and x != x):
            out.append(NaT)
        elif kind == 'M':
            out.append(x if type(x) is Timestamp else Timestamp(x))
        else:
            out.append(x if type(x) is Timedelta else Timedelta._of(x._us))
    return out, ('datetime64[%s]' if kind == 'M' else 'timedelta64[%s]') % unit
