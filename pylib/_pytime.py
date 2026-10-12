# Pure-Python pieces of the time module: struct_time, gmtime/localtime (local zone is UTC), mktime,
# strftime, strptime, asctime, ctime.  Used by the logging/datetime code and as a fallback overlay (pylib/time.py).
import re as _re

_DAYS = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']
_MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October',
           'November', 'December']
_FIELDS = ('tm_year', 'tm_mon', 'tm_mday', 'tm_hour', 'tm_min', 'tm_sec', 'tm_wday', 'tm_yday', 'tm_isdst')

timezone = 0
altzone = 0
daylight = 0
tzname = ('UTC', 'UTC')


class struct_time(tuple):
    def __new__(cls, seq, dict=None):
        t = tuple(seq)
        if len(t) < 9:
            raise TypeError("time.struct_time() takes an at least 9-sequence (%d-sequence given)" % len(t))
        return tuple.__new__(cls, t[:9])

    tm_year = property(lambda self: self[0])
    tm_mon = property(lambda self: self[1])
    tm_mday = property(lambda self: self[2])
    tm_hour = property(lambda self: self[3])
    tm_min = property(lambda self: self[4])
    tm_sec = property(lambda self: self[5])
    tm_wday = property(lambda self: self[6])
    tm_yday = property(lambda self: self[7])
    tm_isdst = property(lambda self: self[8])
    tm_zone = property(lambda self: 'UTC')
    tm_gmtoff = property(lambda self: 0)

    def __repr__(self):
        return "time.struct_time(" + ", ".join("%s=%r" % (_FIELDS[i], self[i]) for i in range(9)) + ")"


def _days_from_civil(y, m, d):
    y -= m <= 2
    era = (y if y >= 0 else y - 399) // 400
    yoe = y - era * 400
    doy = (153 * (m + (-3 if m > 2 else 9)) + 2) // 5 + d - 1
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468


def _civil_from_days(z):
    z += 719468
    era = (z if z >= 0 else z - 146096) // 146097
    doe = z - era * 146097
    yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
    y = yoe + era * 400
    doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
    mp = (5 * doy + 2) // 153
    d = doy - (153 * mp + 2) // 5 + 1
    m = mp + (3 if mp < 10 else -9)
    return (y + (m <= 2), m, d)


def _isleap(y):
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)


def gmtime(*args):
    # *args: also callable as a bound method (Formatter.converter = gmtime)
    secs = args[-1] if args else None
    if secs is None:
        import time as _t
        secs = _t.time()
    secs = int(secs // 1)
    days = secs // 86400
    rem = secs - days * 86400
    y, m, d = _civil_from_days(days)
    yday = days - _days_from_civil(y, 1, 1) + 1
    return struct_time((y, m, d, rem // 3600, (rem % 3600) // 60, rem % 60, (days + 3) % 7, yday, 0))


localtime = gmtime


def mktime(t):
    y, m, d, hh, mm, ss = t[0], t[1], t[2], t[3], t[4], t[5]
    return float(_days_from_civil(y, 1, 1) * 86400 + 0) if False else float(
        _days_from_civil(y + (m - 1) // 12, (m - 1) % 12 + 1, 1) * 86400 + (d - 1) * 86400 + hh * 3600 + mm * 60 + ss)


def timegm(t):
    return int(mktime(t))


def asctime(t=None):
    if t is None:
        t = localtime()
    return "%s %s %2d %02d:%02d:%02d %d" % (_DAYS[t[6]][:3], _MONTHS[t[1] - 1][:3], t[2], t[3], t[4], t[5], t[0])


def ctime(secs=None):
    return asctime(localtime(secs))


def _week(t, first):
    # week number of the year with `first` (6 = Sunday, 0 = Monday) as first day of week
    wday = (t[6] - first) % 7
    return (t[7] + 6 - wday) // 7


def _iso(t):
    y, yday, wday = t[0], t[7], t[6]
    week = (yday - 1 - wday + 10) // 7
    if week < 1:
        y -= 1
        pdays = 366 if _isleap(y) else 365
        week = (yday + pdays - 1 - wday + 10) // 7
    elif week == 53:
        days = 366 if _isleap(y) else 365
        if yday - 1 - wday + 3 >= days:
            week = 1
            y += 1
    return y, week


def strftime(fmt, t=None):
    if t is None:
        t = localtime()
    t = tuple(t)
    out = []
    i = 0
    n = len(fmt)
    while i < n:
        c = fmt[i]
        if c != '%' or i + 1 >= n:
            out.append(c)
            i += 1
            continue
        i += 1
        c = fmt[i]
        if c == '-' and i + 1 < n:
            i += 1
            c = fmt[i]
            strip = True
        else:
            strip = False
        i += 1
        if c == 'Y': s = '%d' % t[0]
        elif c == 'y': s = '%02d' % (t[0] % 100)
        elif c == 'C': s = '%02d' % (t[0] // 100)
        elif c == 'm': s = '%02d' % t[1]
        elif c == 'd': s = '%02d' % t[2]
        elif c == 'e': s = '%2d' % t[2]
        elif c == 'H': s = '%02d' % t[3]
        elif c == 'I': s = '%02d' % ((t[3] % 12) or 12)
        elif c == 'M': s = '%02d' % t[4]
        elif c == 'S': s = '%02d' % t[5]
        elif c == 'p': s = 'AM' if t[3] < 12 else 'PM'
        elif c == 'a': s = _DAYS[t[6]][:3]
        elif c == 'A': s = _DAYS[t[6]]
        elif c == 'b' or c == 'h': s = _MONTHS[t[1] - 1][:3]
        elif c == 'B': s = _MONTHS[t[1] - 1]
        elif c == 'j': s = '%03d' % t[7]
        elif c == 'w': s = '%d' % ((t[6] + 1) % 7)
        elif c == 'u': s = '%d' % (t[6] + 1)
        elif c == 'U': s = '%02d' % _week(t, 6)
        elif c == 'W': s = '%02d' % _week(t, 0)
        elif c == 'V': s = '%02d' % _iso(t)[1]
        elif c == 'G': s = '%d' % _iso(t)[0]
        elif c == 'g': s = '%02d' % (_iso(t)[0] % 100)
        elif c == 'Z': s = 'UTC'
        elif c == 'z': s = '+0000'
        elif c == 'n': s = '\n'
        elif c == 't': s = '\t'
        elif c == '%': s = '%'
        elif c == 'D': s = strftime('%m/%d/%y', t)
        elif c == 'F': s = strftime('%Y-%m-%d', t)
        elif c == 'T': s = strftime('%H:%M:%S', t)
        elif c == 'R': s = strftime('%H:%M', t)
        elif c == 'r': s = strftime('%I:%M:%S %p', t)
        elif c == 'x': s = strftime('%m/%d/%y', t)
        elif c == 'X': s = strftime('%H:%M:%S', t)
        elif c == 'c': s = strftime('%a %b %e %H:%M:%S %Y', t)
        elif c == 's': s = '%d' % int(mktime(t))
        else: s = '%' + c
        if strip:
            s = s.lstrip('0 ') or '0'
        out.append(s)
    return ''.join(out)


def strptime(string, format="%a %b %d %H:%M:%S %Y"):
    pat = ''
    i = 0
    n = len(format)
    keys = []
    while i < n:
        c = format[i]
        if c == '%' and i + 1 < n:
            i += 1
            d = format[i]
            i += 1
            if d == 'Y': pat += '(\\d\\d\\d\\d)'; keys.append('Y')
            elif d == 'y': pat += '(\\d\\d)'; keys.append('y')
            elif d == 'm': pat += '(1[0-2]|0[1-9]|[1-9])'; keys.append('m')
            elif d == 'd': pat += '(3[01]|[12]\\d|0[1-9]|[1-9]| [1-9])'; keys.append('d')
            elif d == 'H': pat += '(2[0-3]|[0-1]\\d|\\d)'; keys.append('H')
            elif d == 'I': pat += '(1[0-2]|0[1-9]|[1-9])'; keys.append('I')
            elif d == 'M': pat += '([0-5]\\d|\\d)'; keys.append('M')
            elif d == 'S': pat += '(6[0-1]|[0-5]\\d|\\d)'; keys.append('S')
            elif d == 'j': pat += '(36[0-6]|3[0-5]\\d|[1-2]\\d\\d|0[1-9]\\d|00[1-9]|[1-9]\\d|0[1-9]|[1-9])'; keys.append('j')
            elif d == 'p': pat += '(AM|PM|am|pm)'; keys.append('p')
            elif d == 'a': pat += '(Mon|Tue|Wed|Thu|Fri|Sat|Sun|mon|tue|wed|thu|fri|sat|sun)'; keys.append('a')
            elif d == 'A': pat += '(Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday)'; keys.append('A')
            elif d == 'b': pat += '(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)'; keys.append('b')
            elif d == 'B': pat += '(January|February|March|April|May|June|July|August|September|October|November|December)'; keys.append('B')
            elif d == 'z': pat += '([+-]\\d\\d:?[0-5]\\d|Z)'; keys.append('z')
            elif d == 'Z': pat += '([A-Za-z]+)'; keys.append('Z')
            elif d == '%': pat += '%'
            elif d == 'F':
                pat += '(\\d\\d\\d\\d)-(1[0-2]|0[1-9]|[1-9])-(3[01]|[12]\\d|0[1-9]|[1-9])'; keys.extend(['Y', 'm', 'd'])
            elif d == 'T':
                pat += '(2[0-3]|[0-1]\\d|\\d):([0-5]\\d|\\d):(6[0-1]|[0-5]\\d|\\d)'; keys.extend(['H', 'M', 'S'])
            else:
                raise ValueError("'%s' is a bad directive in format '%s'" % (d, format))
        elif c == ' ' or c == '\t' or c == '\n':
            pat += '\\s+'
            i += 1
        else:
            pat += _re.escape(c)
            i += 1
    m = _re.match(pat + '$', string)
    if m is None:
        m2 = _re.match(pat, string)
        if m2 is not None:
            raise ValueError("unconverted data remains: " + string[m2.end():])
        raise ValueError("time data %r does not match format %r" % (string, format))
    year, mon, day, hour, minute, sec = 1900, 1, 1, 0, 0, 0
    pm = None
    yday = None
    wday = None
    for k, g in zip(keys, m.groups()):
        if k == 'Y': year = int(g)
        elif k == 'y':
            v = int(g)
            year = 2000 + v if v <= 68 else 1900 + v
        elif k == 'm': mon = int(g)
        elif k == 'd': day = int(g)
        elif k == 'H': hour = int(g)
        elif k == 'I': hour = int(g)
        elif k == 'M': minute = int(g)
        elif k == 'S': sec = int(g)
        elif k == 'j': yday = int(g)
        elif k == 'p': pm = g.lower() == 'pm'
        elif k == 'a': wday = [x[:3].lower() for x in _DAYS].index(g.lower())
        elif k == 'A': wday = _DAYS.index(g)
        elif k == 'b': mon = [x[:3] for x in _MONTHS].index(g) + 1
        elif k == 'B': mon = _MONTHS.index(g) + 1
    if pm is not None:
        if pm and hour != 12:
            hour += 12
        elif not pm and hour == 12:
            hour = 0
    if yday is not None:
        y, mon, day = _civil_from_days(_days_from_civil(year, 1, 1) + yday - 1)
        year = y
    days = _days_from_civil(year, mon, day)
    yd = days - _days_from_civil(year, 1, 1) + 1
    wd = (days + 3) % 7
    return struct_time((year, mon, day, hour, minute, sec, wd, yd, -1))
