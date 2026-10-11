"""datetime: date, time, datetime, timedelta, timezone (naive and fixed-offset aware values)."""
import time as _time

MINYEAR = 1
MAXYEAR = 9999

_DIM = [-1, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
_DBM = [-1, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334]
_MONTHS = ["", "January", "February", "March", "April", "May", "June", "July", "August", "September",
           "October", "November", "December"]
_DAYS = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]


def _is_leap(y):
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)


def _days_before_year(y):
    y -= 1
    return y * 365 + y // 4 - y // 100 + y // 400


def _days_in_month(y, m):
    if m == 2 and _is_leap(y):
        return 29
    return _DIM[m]


def _days_before_month(y, m):
    if m > 2 and _is_leap(y):
        return _DBM[m] + 1
    return _DBM[m]


def _ymd2ord(y, m, d):
    return _days_before_year(y) + _days_before_month(y, m) + d


def _ord2ymd(n):
    n -= 1
    n400, n = divmod(n, 146097)
    year = n400 * 400 + 1
    n100, n = divmod(n, 36524)
    n4, n = divmod(n, 1461)
    n1, n = divmod(n, 365)
    year += n100 * 100 + n4 * 4 + n1
    if n1 == 4 or n100 == 4:
        return year - 1, 12, 31
    leap = n1 == 3 and (n4 != 24 or n100 == 3)
    month = (n + 50) >> 5
    pre = _DBM[month] + (1 if (month > 2 and leap) else 0)
    if pre > n:
        month -= 1
        pre -= _DIM[month] + (1 if (month == 2 and leap) else 0)
    n -= pre
    return year, month, n + 1


def _check_date(y, m, d):
    if not (1 <= y <= 9999):
        raise ValueError("year %d is out of range" % y)
    if not (1 <= m <= 12):
        raise ValueError("month must be in 1..12")
    if not (1 <= d <= _days_in_month(y, m)):
        raise ValueError("day is out of range for month")


def _check_time(hh, mm, ss, us):
    if not (0 <= hh <= 23):
        raise ValueError("hour must be in 0..23")
    if not (0 <= mm <= 59):
        raise ValueError("minute must be in 0..59")
    if not (0 <= ss <= 59):
        raise ValueError("second must be in 0..59")
    if not (0 <= us <= 999999):
        raise ValueError("microsecond must be in 0..999999")


def _tn(o):
    n = type(o).__name__
    if n in ("date", "datetime", "timedelta", "time", "timezone"):
        return "datetime." + n
    return n


def _p2(n):
    return "%02d" % n


def _fmt_off(us_total, sep):
    sign = "+"
    if us_total < 0:
        sign = "-"
        us_total = -us_total
    secs = us_total // 1000000
    hh = secs // 3600
    mm = (secs % 3600) // 60
    ss = secs % 60
    r = "%s%02d%s%02d" % (sign, hh, sep, mm)
    if ss or us_total % 1000000:
        r += "%s%02d" % (sep, ss)
        if us_total % 1000000:
            r += ".%06d" % (us_total % 1000000)
    return r


class timedelta:
    def __init__(self, days=0, seconds=0, microseconds=0, milliseconds=0, minutes=0, hours=0, weeks=0):
        us = 0
        isint = True
        for v in (days, seconds, microseconds, milliseconds, minutes, hours, weeks):
            if not isinstance(v, int):
                isint = False
        if isint:
            us = (((weeks * 7 + days) * 86400 + hours * 3600 + minutes * 60 + seconds) * 1000000
                  + milliseconds * 1000 + microseconds)
        else:
            us = int(round((weeks * 7 + days) * 86400000000.0 + hours * 3600000000.0 + minutes * 60000000.0
                           + seconds * 1000000.0 + milliseconds * 1000.0 + microseconds))
        self._us = us
        d, r = divmod(us, 86400000000)
        self.days = d
        self.seconds = r // 1000000
        self.microseconds = r % 1000000
        if abs(d) > 999999999:
            raise OverflowError("days=%d; must have magnitude <= 999999999" % d)

    @classmethod
    def _from_us(cls, us):
        return cls(microseconds=us)

    def total_seconds(self):
        return self._us / 1000000

    def __add__(self, o):
        if isinstance(o, timedelta):
            return timedelta(microseconds=self._us + o._us)
        return NotImplemented

    __radd__ = __add__

    def __sub__(self, o):
        if isinstance(o, timedelta):
            return timedelta(microseconds=self._us - o._us)
        return NotImplemented

    def __rsub__(self, o):
        if isinstance(o, timedelta):
            return timedelta(microseconds=o._us - self._us)
        return NotImplemented

    def __neg__(self):
        return timedelta(microseconds=-self._us)

    def __pos__(self):
        return self

    def __abs__(self):
        return timedelta(microseconds=abs(self._us))

    def __mul__(self, k):
        if isinstance(k, int):
            return timedelta(microseconds=self._us * k)
        if isinstance(k, float):
            return timedelta(microseconds=int(round(self._us * k)))
        return NotImplemented

    __rmul__ = __mul__

    def __truediv__(self, o):
        if isinstance(o, timedelta):
            return self._us / o._us
        if isinstance(o, int):
            return timedelta(microseconds=int(round(self._us / o)))
        if isinstance(o, float):
            return timedelta(microseconds=int(round(self._us / o)))
        return NotImplemented

    def __floordiv__(self, o):
        if isinstance(o, timedelta):
            return self._us // o._us
        if isinstance(o, int):
            return timedelta(microseconds=self._us // o)
        return NotImplemented

    def __mod__(self, o):
        if isinstance(o, timedelta):
            return timedelta(microseconds=self._us % o._us)
        return NotImplemented

    def __divmod__(self, o):
        q, r = divmod(self._us, o._us)
        return q, timedelta(microseconds=r)

    def __eq__(self, o):
        return isinstance(o, timedelta) and self._us == o._us

    def __ne__(self, o):
        return not (isinstance(o, timedelta) and self._us == o._us)

    def __lt__(self, o):
        if not isinstance(o, timedelta):
            raise TypeError("'<' not supported between instances of 'datetime.timedelta' and '%s'" % _tn(o))
        return self._us < o._us

    def __le__(self, o):
        if not isinstance(o, timedelta):
            raise TypeError("'<=' not supported between instances of 'datetime.timedelta' and '%s'" % _tn(o))
        return self._us <= o._us

    def __gt__(self, o):
        if not isinstance(o, timedelta):
            raise TypeError("'>' not supported between instances of 'datetime.timedelta' and '%s'" % _tn(o))
        return self._us > o._us

    def __ge__(self, o):
        if not isinstance(o, timedelta):
            raise TypeError("'>=' not supported between instances of 'datetime.timedelta' and '%s'" % _tn(o))
        return self._us >= o._us

    def __hash__(self):
        return hash(self._us)

    def __bool__(self):
        return self._us != 0

    def __str__(self):
        mm, ss = divmod(self.seconds, 60)
        hh, mm = divmod(mm, 60)
        s = "%d:%02d:%02d" % (hh, mm, ss)
        if self.days:
            s = ("%d day%s, " % (self.days, "" if abs(self.days) == 1 else "s")) + s
        if self.microseconds:
            s = s + ".%06d" % self.microseconds
        return s

    def __repr__(self):
        args = []
        if self.days:
            args.append("days=%d" % self.days)
        if self.seconds:
            args.append("seconds=%d" % self.seconds)
        if self.microseconds:
            args.append("microseconds=%d" % self.microseconds)
        if not args:
            args.append("0")
        return "datetime.timedelta(%s)" % ", ".join(args)


timedelta.min = timedelta(-999999999)
timedelta.max = timedelta(days=999999999, hours=23, minutes=59, seconds=59, microseconds=999999)
timedelta.resolution = timedelta(microseconds=1)


class tzinfo:
    def utcoffset(self, dt):
        return None

    def tzname(self, dt):
        return None

    def dst(self, dt):
        return None


class timezone(tzinfo):
    def __init__(self, offset, name=None):
        self._offset = offset
        self._name = name

    def utcoffset(self, dt):
        return self._offset

    def tzname(self, dt):
        if self._name is not None:
            return self._name
        if self._offset._us == 0:
            return "UTC"
        return "UTC" + _fmt_off(self._offset._us, ":")

    def dst(self, dt):
        return None

    def __eq__(self, o):
        return isinstance(o, timezone) and self._offset == o._offset

    def __hash__(self):
        return hash(self._offset)

    def __str__(self):
        return self.tzname(None)

    def __repr__(self):
        if self._offset._us == 0 and self._name is None:
            return "datetime.timezone.utc"
        if self._name is None:
            return "datetime.timezone(%r)" % (self._offset,)
        return "datetime.timezone(%r, %r)" % (self._offset, self._name)


timezone.utc = timezone(timedelta(0))
UTC = timezone.utc


def _tzoff(tz, dt):
    if tz is None:
        return None
    o = tz.utcoffset(dt)
    return o


def _strftime(fmt, y, m, d, hh, mm, ss, us, tz, dt):
    out = []
    i = 0
    n = len(fmt)
    wd = (_ymd2ord(y, m, d) + 6) % 7      # Monday == 0
    yday = _days_before_month(y, m) + d
    while i < n:
        c = fmt[i]
        i += 1
        if c != "%" or i >= n:
            out.append(c)
            continue
        c = fmt[i]
        i += 1
        pad = True
        if c == "-" and i < n:
            pad = False
            c = fmt[i]
            i += 1
        if c == "Y":
            out.append("%04d" % y if pad else str(y))
        elif c == "m":
            out.append("%02d" % m if pad else str(m))
        elif c == "d":
            out.append("%02d" % d if pad else str(d))
        elif c == "H":
            out.append("%02d" % hh if pad else str(hh))
        elif c == "M":
            out.append("%02d" % mm if pad else str(mm))
        elif c == "S":
            out.append("%02d" % ss if pad else str(ss))
        elif c == "y":
            out.append("%02d" % (y % 100) if pad else str(y % 100))
        elif c == "I":
            h = hh % 12
            if h == 0:
                h = 12
            out.append("%02d" % h if pad else str(h))
        elif c == "p":
            out.append("AM" if hh < 12 else "PM")
        elif c == "f":
            out.append("%06d" % us)
        elif c == "A":
            out.append(_DAYS[wd])
        elif c == "a":
            out.append(_DAYS[wd][:3])
        elif c == "B":
            out.append(_MONTHS[m])
        elif c == "b" or c == "h":
            out.append(_MONTHS[m][:3])
        elif c == "j":
            out.append("%03d" % yday if pad else str(yday))
        elif c == "e":
            out.append("%2d" % d)
        elif c == "w":
            out.append(str((wd + 1) % 7))
        elif c == "u":
            out.append(str(wd + 1))
        elif c == "U":
            out.append("%02d" % ((yday + 6 - (wd + 1) % 7) // 7))
        elif c == "W":
            out.append("%02d" % ((yday + 6 - wd) // 7))
        elif c == "G" or c == "V":
            iy, iw, idw = _isocal(y, m, d)
            out.append("%04d" % iy if c == "G" else "%02d" % iw)
        elif c == "z":
            off = _tzoff(tz, dt)
            if off is not None:
                out.append(_fmt_off(off._us, ""))
        elif c == "Z":
            if tz is not None:
                nm = tz.tzname(dt)
                if nm is not None:
                    out.append(nm)
        elif c == "c":
            out.append("%s %s %2d %02d:%02d:%02d %04d" % (_DAYS[wd][:3], _MONTHS[m][:3], d, hh, mm, ss, y))
        elif c == "x":
            out.append("%02d/%02d/%02d" % (m, d, y % 100))
        elif c == "X":
            out.append("%02d:%02d:%02d" % (hh, mm, ss))
        elif c == "D":
            out.append("%02d/%02d/%02d" % (m, d, y % 100))
        elif c == "F":
            out.append("%04d-%02d-%02d" % (y, m, d))
        elif c == "T":
            out.append("%02d:%02d:%02d" % (hh, mm, ss))
        elif c == "R":
            out.append("%02d:%02d" % (hh, mm))
        elif c == "n":
            out.append("\n")
        elif c == "t":
            out.append("\t")
        elif c == "%":
            out.append("%")
        else:
            out.append("%" + c)
    return "".join(out)


def _isocal(y, m, d):
    ordn = _ymd2ord(y, m, d)
    wd = (ordn + 6) % 7
    # Thursday of this week decides the ISO year
    thu = ordn - wd + 3
    ty, tm, td = _ord2ymd(thu)
    jan1 = _ymd2ord(ty, 1, 1)
    return ty, (thu - jan1) // 7 + 1, wd + 1


class date:
    def __init__(self, year, month, day):
        _check_date(year, month, day)
        self.year = year
        self.month = month
        self.day = day

    @classmethod
    def today(cls):
        return cls.fromtimestamp(_time.time())

    @classmethod
    def fromtimestamp(cls, t):
        days = int(t // 86400)
        y, m, d = _ord2ymd(719163 + days)
        return cls(y, m, d)

    @classmethod
    def fromordinal(cls, n):
        y, m, d = _ord2ymd(n)
        return cls(y, m, d)

    @classmethod
    def fromisoformat(cls, s):
        try:
            if len(s) == 10 and s[4] == "-" and s[7] == "-":
                return cls(int(s[0:4]), int(s[5:7]), int(s[8:10]))
            if len(s) == 8:
                return cls(int(s[0:4]), int(s[4:6]), int(s[6:8]))
        except ValueError:
            pass
        raise ValueError("Invalid isoformat string: %r" % (s,))

    @classmethod
    def fromisocalendar(cls, year, week, day):
        jan4 = _ymd2ord(year, 1, 4)
        w1 = jan4 - (jan4 + 6) % 7
        return cls.fromordinal(w1 + (week - 1) * 7 + day - 1)

    def toordinal(self):
        return _ymd2ord(self.year, self.month, self.day)

    def weekday(self):
        return (_ymd2ord(self.year, self.month, self.day) + 6) % 7

    def isoweekday(self):
        return _ymd2ord(self.year, self.month, self.day) % 7 or 7

    def isocalendar(self):
        return _IsoCal(*_isocal(self.year, self.month, self.day))

    def isoformat(self):
        return "%04d-%02d-%02d" % (self.year, self.month, self.day)

    __str__ = isoformat

    def ctime(self):
        return self.strftime("%a %b %e 00:00:00 %Y")

    def strftime(self, fmt):
        return _strftime(fmt, self.year, self.month, self.day, 0, 0, 0, 0, None, None)

    def __format__(self, spec):
        if spec:
            return self.strftime(spec)
        return str(self)

    def timetuple(self):
        return (self.year, self.month, self.day, 0, 0, 0, self.weekday(),
                _days_before_month(self.year, self.month) + self.day, -1)

    def replace(self, year=None, month=None, day=None):
        return date(self.year if year is None else year, self.month if month is None else month,
                    self.day if day is None else day)

    def _key(self):
        return (self.year, self.month, self.day)

    def __eq__(self, o):
        if isinstance(o, date) and not isinstance(o, datetime) and not isinstance(self, datetime):
            return self._key() == o._key()
        return False

    def __ne__(self, o):
        return not self.__eq__(o)

    def _cmpchk(self, o, op):
        if not isinstance(o, date) or isinstance(o, datetime):
            raise TypeError("'%s' not supported between instances of 'datetime.date' and '%s'" % (op, _tn(o)))

    def __lt__(self, o):
        self._cmpchk(o, "<")
        return self._key() < o._key()

    def __le__(self, o):
        self._cmpchk(o, "<=")
        return self._key() <= o._key()

    def __gt__(self, o):
        self._cmpchk(o, ">")
        return self._key() > o._key()

    def __ge__(self, o):
        self._cmpchk(o, ">=")
        return self._key() >= o._key()

    def __hash__(self):
        return hash(self._key())

    def __add__(self, o):
        if isinstance(o, timedelta):
            return date.fromordinal(self.toordinal() + o.days)
        return NotImplemented

    __radd__ = __add__

    def __sub__(self, o):
        if isinstance(o, timedelta):
            return date.fromordinal(self.toordinal() - o.days)
        if isinstance(o, date):
            return timedelta(days=self.toordinal() - o.toordinal())
        return NotImplemented

    def __repr__(self):
        return "datetime.date(%d, %d, %d)" % (self.year, self.month, self.day)


class _IsoCal:
    def __init__(self, year, week, weekday):
        self.year = year
        self.week = week
        self.weekday = weekday

    def __iter__(self):
        return iter((self.year, self.week, self.weekday))

    def __getitem__(self, i):
        return (self.year, self.week, self.weekday)[i]

    def __len__(self):
        return 3

    def __eq__(self, o):
        return tuple(self) == tuple(o)

    def __repr__(self):
        return "datetime.IsoCalendarDate(year=%d, week=%d, weekday=%d)" % (self.year, self.week, self.weekday)


date.min = date(1, 1, 1)
date.max = date(9999, 12, 31)
date.resolution = timedelta(days=1)


def _iso_time(hh, mm, ss, us, timespec):
    if timespec == "auto":
        timespec = "microseconds" if us else "seconds"
    if timespec == "hours":
        return "%02d" % hh
    if timespec == "minutes":
        return "%02d:%02d" % (hh, mm)
    if timespec == "seconds":
        return "%02d:%02d:%02d" % (hh, mm, ss)
    if timespec == "milliseconds":
        return "%02d:%02d:%02d.%03d" % (hh, mm, ss, us // 1000)
    if timespec == "microseconds":
        return "%02d:%02d:%02d.%06d" % (hh, mm, ss, us)
    raise ValueError("Unknown timespec value")


def _parse_time_iso(s):
    # HH[:MM[:SS[.ffffff]]][+HH:MM]  -> (h, m, s, us, tzoffset-us or None)
    off = None
    k = -1
    for j in range(len(s)):
        if s[j] in "+-":
            k = j
            break
    if len(s) > 0 and s[-1] in "Zz":
        off = 0
        s = s[:-1]
    elif k >= 0:
        t = s[k + 1:].replace(":", "")
        sign = -1 if s[k] == "-" else 1
        off = sign * ((int(t[0:2]) * 3600 + (int(t[2:4]) if len(t) >= 4 else 0) * 60
                       + (int(t[4:6]) if len(t) >= 6 else 0)) * 1000000)
        s = s[:k]
    us = 0
    if "." in s:
        s, frac = s.split(".")
        frac = (frac + "000000")[:6]
        us = int(frac)
    t = s.replace(":", "")
    if len(t) not in (2, 4, 6):
        raise ValueError("bad time")
    hh = int(t[0:2])
    mm = int(t[2:4]) if len(t) >= 4 else 0
    ss = int(t[4:6]) if len(t) >= 6 else 0
    return hh, mm, ss, us, off


class time:
    def __init__(self, hour=0, minute=0, second=0, microsecond=0, tzinfo=None):
        _check_time(hour, minute, second, microsecond)
        self.hour = hour
        self.minute = minute
        self.second = second
        self.microsecond = microsecond
        self.tzinfo = tzinfo

    @classmethod
    def fromisoformat(cls, s):
        try:
            hh, mm, ss, us, off = _parse_time_iso(s)
            tz = None
            if off is not None:
                tz = timezone(timedelta(microseconds=off))
            return cls(hh, mm, ss, us, tz)
        except (ValueError, IndexError):
            raise ValueError("Invalid isoformat string: %r" % (s,))

    def isoformat(self, timespec="auto"):
        r = _iso_time(self.hour, self.minute, self.second, self.microsecond, timespec)
        if self.tzinfo is not None:
            off = self.tzinfo.utcoffset(None)
            if off is not None:
                r += _fmt_off(off._us, ":")
        return r

    __str__ = isoformat

    def strftime(self, fmt):
        return _strftime(fmt, 1900, 1, 1, self.hour, self.minute, self.second, self.microsecond, self.tzinfo, None)

    def __format__(self, spec):
        if spec:
            return self.strftime(spec)
        return str(self)

    def replace(self, hour=None, minute=None, second=None, microsecond=None, tzinfo=True):
        return time(self.hour if hour is None else hour, self.minute if minute is None else minute,
                    self.second if second is None else second,
                    self.microsecond if microsecond is None else microsecond,
                    self.tzinfo if tzinfo is True else tzinfo)

    def _key(self):
        return (self.hour, self.minute, self.second, self.microsecond)

    def __eq__(self, o):
        return isinstance(o, time) and self._key() == o._key()

    def __ne__(self, o):
        return not self.__eq__(o)

    def __lt__(self, o):
        return self._key() < o._key()

    def __le__(self, o):
        return self._key() <= o._key()

    def __gt__(self, o):
        return self._key() > o._key()

    def __ge__(self, o):
        return self._key() >= o._key()

    def __hash__(self):
        return hash(self._key())

    def __bool__(self):
        return True

    def __repr__(self):
        if self.microsecond:
            s = "datetime.time(%d, %d, %d, %d" % (self.hour, self.minute, self.second, self.microsecond)
        elif self.second:
            s = "datetime.time(%d, %d, %d" % (self.hour, self.minute, self.second)
        else:
            s = "datetime.time(%d, %d" % (self.hour, self.minute)
        if self.tzinfo is not None:
            s += ", tzinfo=%r" % (self.tzinfo,)
        return s + ")"


time.min = time(0, 0, 0)
time.max = time(23, 59, 59, 999999)


class datetime(date):
    def __init__(self, year, month, day, hour=0, minute=0, second=0, microsecond=0, tzinfo=None):
        _check_date(year, month, day)
        _check_time(hour, minute, second, microsecond)
        self.year = year
        self.month = month
        self.day = day
        self.hour = hour
        self.minute = minute
        self.second = second
        self.microsecond = microsecond
        self.tzinfo = tzinfo

    @classmethod
    def fromtimestamp(cls, t, tz=None):
        secs = int(t // 1)
        us = int(round((t - secs) * 1000000))
        if us >= 1000000:
            secs += 1
            us -= 1000000
        if tz is not None:
            off = tz.utcoffset(None)
            if off is not None:
                secs += off.days * 86400 + off.seconds
        days, rem = divmod(secs, 86400)
        y, m, d = _ord2ymd(719163 + days)
        return cls(y, m, d, rem // 3600, (rem % 3600) // 60, rem % 60, us, tz)

    @classmethod
    def utcfromtimestamp(cls, t):
        return cls.fromtimestamp(t)

    @classmethod
    def now(cls, tz=None):
        return cls.fromtimestamp(_time.time(), tz)

    @classmethod
    def utcnow(cls):
        return cls.fromtimestamp(_time.time())

    @classmethod
    def today(cls):
        return cls.fromtimestamp(_time.time())

    @classmethod
    def combine(cls, d, t, tzinfo=True):
        tz = t.tzinfo if tzinfo is True else tzinfo
        return cls(d.year, d.month, d.day, t.hour, t.minute, t.second, t.microsecond, tz)

    @classmethod
    def fromordinal(cls, n):
        y, m, d = _ord2ymd(n)
        return cls(y, m, d)

    @classmethod
    def fromisoformat(cls, s):
        try:
            if len(s) < 8:
                raise ValueError("short")
            if s[4] == "-":
                dpart = s[:10]
                rest = s[11:]
            else:
                dpart = s[:8]
                rest = s[9:]
            d = date.fromisoformat(dpart)
            if len(s) > len(dpart):
                if len(rest) == 0 or s[len(dpart)] not in "T t":
                    pass
                hh, mm, ss, us, off = _parse_time_iso(rest)
                tz = None
                if off is not None:
                    tz = timezone(timedelta(microseconds=off))
                return cls(d.year, d.month, d.day, hh, mm, ss, us, tz)
            return cls(d.year, d.month, d.day)
        except (ValueError, IndexError):
            raise ValueError("Invalid isoformat string: %r" % (s,))

    @classmethod
    def strptime(cls, s, fmt):
        return _strptime(cls, s, fmt)

    def date(self):
        return date(self.year, self.month, self.day)

    def time(self):
        return time(self.hour, self.minute, self.second, self.microsecond)

    def timetz(self):
        return time(self.hour, self.minute, self.second, self.microsecond, self.tzinfo)

    def utcoffset(self):
        if self.tzinfo is None:
            return None
        return self.tzinfo.utcoffset(self)

    def tzname(self):
        if self.tzinfo is None:
            return None
        return self.tzinfo.tzname(self)

    def _secs(self):
        return (self.toordinal() * 86400 + self.hour * 3600 + self.minute * 60 + self.second)

    def _us(self):
        # microseconds since 0001-01-01, in UTC when aware
        v = self._secs() * 1000000 + self.microsecond
        off = self.utcoffset()
        if off is not None:
            v -= off._us
        return v

    def timestamp(self):
        off = self.utcoffset()
        v = (self._secs() - 719163 * 86400) * 1000000 + self.microsecond
        if off is not None:
            v -= off._us
        return v / 1000000

    def astimezone(self, tz=None):
        if tz is None:
            tz = timezone.utc
        if self.tzinfo is None:
            u = self._secs() * 1000000 + self.microsecond
        else:
            u = self._us()
        off = tz.utcoffset(None)
        if off is not None:
            u += off._us
        secs, us = divmod(u, 1000000)
        days, rem = divmod(secs, 86400)
        y, m, d = _ord2ymd(days)
        return datetime(y, m, d, rem // 3600, (rem % 3600) // 60, rem % 60, us, tz)

    def replace(self, year=None, month=None, day=None, hour=None, minute=None, second=None,
                microsecond=None, tzinfo=True):
        return datetime(self.year if year is None else year, self.month if month is None else month,
                        self.day if day is None else day, self.hour if hour is None else hour,
                        self.minute if minute is None else minute, self.second if second is None else second,
                        self.microsecond if microsecond is None else microsecond,
                        self.tzinfo if tzinfo is True else tzinfo)

    def isoformat(self, sep="T", timespec="auto"):
        r = "%04d-%02d-%02d%s%s" % (self.year, self.month, self.day, sep,
                                    _iso_time(self.hour, self.minute, self.second, self.microsecond, timespec))
        off = self.utcoffset()
        if off is not None:
            r += _fmt_off(off._us, ":")
        return r

    def __str__(self):
        return self.isoformat(" ")

    def ctime(self):
        return self.strftime("%a %b %e %H:%M:%S %Y")

    def strftime(self, fmt):
        return _strftime(fmt, self.year, self.month, self.day, self.hour, self.minute, self.second,
                         self.microsecond, self.tzinfo, self)

    def __format__(self, spec):
        if spec:
            return self.strftime(spec)
        return str(self)

    def timetuple(self):
        return (self.year, self.month, self.day, self.hour, self.minute, self.second, self.weekday(),
                _days_before_month(self.year, self.month) + self.day, -1)

    def _key(self):
        return (self.year, self.month, self.day, self.hour, self.minute, self.second, self.microsecond)

    def _cmp_key(self, o):
        if (self.tzinfo is None) != (o.tzinfo is None):
            return None
        if self.tzinfo is None:
            return (self._key(), o._key())
        return (self._us(), o._us())

    def __eq__(self, o):
        if not isinstance(o, datetime):
            return False
        k = self._cmp_key(o)
        if k is None:
            return False
        return k[0] == k[1]

    def __ne__(self, o):
        return not self.__eq__(o)

    def _chk(self, o, op):
        if not isinstance(o, datetime):
            raise TypeError("'%s' not supported between instances of 'datetime.datetime' and '%s'" % (op, _tn(o)))
        k = self._cmp_key(o)
        if k is None:
            raise TypeError("can't compare offset-naive and offset-aware datetimes")
        return k

    def __lt__(self, o):
        k = self._chk(o, "<")
        return k[0] < k[1]

    def __le__(self, o):
        k = self._chk(o, "<=")
        return k[0] <= k[1]

    def __gt__(self, o):
        k = self._chk(o, ">")
        return k[0] > k[1]

    def __ge__(self, o):
        k = self._chk(o, ">=")
        return k[0] >= k[1]

    def __hash__(self):
        return hash(self._us())

    def __add__(self, o):
        if isinstance(o, timedelta):
            u = self._secs() * 1000000 + self.microsecond + o._us
            secs, us = divmod(u, 1000000)
            days, rem = divmod(secs, 86400)
            if days < 1 or days > 3652059:
                raise OverflowError("date value out of range")
            y, m, d = _ord2ymd(days)
            return datetime(y, m, d, rem // 3600, (rem % 3600) // 60, rem % 60, us, self.tzinfo)
        return NotImplemented

    __radd__ = __add__

    def __sub__(self, o):
        if isinstance(o, timedelta):
            return self.__add__(timedelta(microseconds=-o._us))
        if isinstance(o, datetime):
            if (self.tzinfo is None) != (o.tzinfo is None):
                raise TypeError("can't subtract offset-naive and offset-aware datetimes")
            return timedelta(microseconds=self._us() - o._us())
        return NotImplemented

    def __repr__(self):
        if self.microsecond:
            s = "datetime.datetime(%d, %d, %d, %d, %d, %d, %d" % (self.year, self.month, self.day, self.hour,
                                                                  self.minute, self.second, self.microsecond)
        elif self.second:
            s = "datetime.datetime(%d, %d, %d, %d, %d, %d" % (self.year, self.month, self.day, self.hour,
                                                              self.minute, self.second)
        else:
            s = "datetime.datetime(%d, %d, %d, %d, %d" % (self.year, self.month, self.day, self.hour, self.minute)
        if self.tzinfo is not None:
            s += ", tzinfo=%r" % (self.tzinfo,)
        return s + ")"


datetime.min = datetime(1, 1, 1)
datetime.max = datetime(9999, 12, 31, 23, 59, 59, 999999)
datetime.resolution = timedelta(microseconds=1)


def _num(s, i, lo, hi):
    j = i
    n = len(s)
    while j < n and j - i < hi and s[j].isdigit():
        j += 1
    if j - i < lo:
        raise ValueError("bad number")
    return int(s[i:j]), j


def _strptime(cls, s, fmt):
    y = 1900
    mo = 1
    d = 1
    hh = 0
    mi = 0
    ss = 0
    us = 0
    pm = None
    yday = None
    tz = None
    i = 0
    j = 0
    n = len(fmt)
    sl = len(s)
    try:
        while j < n:
            c = fmt[j]
            j += 1
            if c != "%":
                if c in " \t\n":
                    while i < sl and s[i] in " \t\n":
                        i += 1
                else:
                    if i >= sl or s[i] != c:
                        raise ValueError("mismatch")
                    i += 1
                continue
            c = fmt[j]
            j += 1
            if c == "-" and j < n:
                c = fmt[j]
                j += 1
            if c == "Y":
                y, i = _num(s, i, 4, 4)
            elif c == "y":
                y, i = _num(s, i, 2, 2)
                y += 2000 if y < 69 else 1900
            elif c == "m":
                mo, i = _num(s, i, 1, 2)
            elif c == "d" or c == "e":
                while c == "e" and i < sl and s[i] == " ":
                    i += 1
                d, i = _num(s, i, 1, 2)
            elif c == "H":
                hh, i = _num(s, i, 1, 2)
            elif c == "I":
                hh, i = _num(s, i, 1, 2)
            elif c == "M":
                mi, i = _num(s, i, 1, 2)
            elif c == "S":
                ss, i = _num(s, i, 1, 2)
            elif c == "f":
                k = i
                while k < sl and k - i < 6 and s[k].isdigit():
                    k += 1
                if k == i:
                    raise ValueError("bad")
                us = int((s[i:k] + "000000")[:6])
                i = k
            elif c == "j":
                yday, i = _num(s, i, 1, 3)
            elif c == "p":
                t = s[i:i + 2].upper()
                if t == "AM":
                    pm = False
                elif t == "PM":
                    pm = True
                else:
                    raise ValueError("bad")
                i += 2
            elif c == "B" or c == "b" or c == "h":
                found = False
                for k in range(1, 13):
                    for nm in (_MONTHS[k], _MONTHS[k][:3]):
                        if s[i:i + len(nm)].lower() == nm.lower():
                            if not found or len(nm) > 3:
                                mo = k
                                ln = len(nm)
                                found = True
                    if found:
                        break
                if not found:
                    raise ValueError("bad month")
                i += ln
            elif c == "A" or c == "a":
                found = False
                for k in range(7):
                    for nm in (_DAYS[k], _DAYS[k][:3]):
                        if s[i:i + len(nm)].lower() == nm.lower() and (not found or len(nm) > 3):
                            ln = len(nm)
                            found = True
                    if found:
                        break
                if not found:
                    raise ValueError("bad weekday")
                i += ln
            elif c == "z":
                k = i
                if k < sl and s[k] in "Zz":
                    tz = timezone.utc
                    i = k + 1
                else:
                    sign = 1
                    if s[k] == "-":
                        sign = -1
                    elif s[k] != "+":
                        raise ValueError("bad tz")
                    k += 1
                    h2, k = _num(s, k, 2, 2)
                    if k < sl and s[k] == ":":
                        k += 1
                    m2, k = _num(s, k, 2, 2)
                    tz = timezone(timedelta(seconds=sign * (h2 * 3600 + m2 * 60)))
                    i = k
            elif c == "Z":
                k = i
                while k < sl and s[k].isalpha():
                    k += 1
                i = k
            elif c == "%":
                if i >= sl or s[i] != "%":
                    raise ValueError("mismatch")
                i += 1
            else:
                raise ValueError("'%s' is a bad directive in format '%s'" % (c, fmt))
        if i != sl:
            raise ValueError("unconverted data remains: " + s[i:])
    except (ValueError, IndexError):
        raise ValueError("time data %r does not match format %r" % (s, fmt))
    if pm is not None:
        if pm and hh < 12:
            hh += 12
        elif not pm and hh == 12:
            hh = 0
    if yday is not None:
        dd = date.fromordinal(_ymd2ord(y, 1, 1) + yday - 1)
        mo = dd.month
        d = dd.day
    return cls(y, mo, d, hh, mi, ss, us, tz)
