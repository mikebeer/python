# zoneinfo (minimal): UTC, GMT and fixed-offset "Etc/GMT+N" zones; no tz database
from datetime import tzinfo, timedelta

__all__ = ["ZoneInfo", "reset_tzpath", "available_timezones", "TZPATH", "ZoneInfoNotFoundError", "InvalidTZPathWarning"]
TZPATH = ()


class ZoneInfoNotFoundError(KeyError):
    pass


class InvalidTZPathWarning(RuntimeWarning):
    pass


_cache = {}


def _fixed(key):
    if key in ('UTC', 'Etc/UTC', 'GMT', 'Etc/GMT', 'Zulu', 'Etc/Zulu', 'Universal', 'Etc/Universal', 'UCT',
               'Etc/UCT', 'Greenwich', 'Etc/Greenwich', 'Etc/GMT0', 'Etc/GMT+0', 'Etc/GMT-0', 'GMT0'):
        return (0, 'UTC' if 'UTC' in key or key in ('Zulu', 'Etc/Zulu', 'UCT', 'Etc/UCT', 'Universal', 'Etc/Universal') else 'GMT')
    if key.startswith('Etc/GMT') and len(key) > 8 and key[7] in '+-' and key[8:].isdigit():
        n = int(key[8:])
        sign = -1 if key[7] == '+' else 1
        if n <= 14:
            return (sign * n * 3600, '%s%02d' % (key[7], n))
    return None


# Zones with hard-coded rules (current rules; the EU rule is used back to 1980, the US rule since 2007 with
# the 1987-2006 rule before that).  key -> (standard offset in seconds, standard name, DST name, rule)
_RULES = {}


def _def(rule, std, sn, dn, keys):
    for k in keys.split():
        _RULES[k] = (std, sn, dn, rule)


_def('EU', 0, 'GMT', 'BST', 'Europe/London GB Europe/Belfast Europe/Jersey Europe/Guernsey Europe/Isle_of_Man')
_def('EU', 0, 'WET', 'WEST', 'Europe/Lisbon Atlantic/Canary Atlantic/Madeira')
_def('EU', 0, 'GMT', 'IST', 'Europe/Dublin Eire')
_def('EU', 3600, 'CET', 'CEST', 'Europe/Zurich Europe/Berlin Europe/Paris Europe/Rome Europe/Madrid Europe/Vienna '
     'Europe/Amsterdam Europe/Brussels Europe/Stockholm Europe/Oslo Europe/Copenhagen Europe/Prague '
     'Europe/Warsaw Europe/Budapest Europe/Belgrade Europe/Luxembourg Europe/Monaco Europe/Vaduz '
     'Europe/Malta Europe/Zagreb Europe/Ljubljana Europe/Bratislava Europe/Andorra Europe/Tirane '
     'Europe/Sarajevo Europe/Skopje Europe/Podgorica Europe/San_Marino Europe/Vatican Europe/Gibraltar '
     'Arctic/Longyearbyen Poland CET MET')
_def('EU', 7200, 'EET', 'EEST', 'Europe/Athens Europe/Helsinki Europe/Kiev Europe/Kyiv Europe/Riga Europe/Tallinn '
     'Europe/Vilnius Europe/Sofia Europe/Bucharest Asia/Nicosia Europe/Chisinau EET')
_def('US', -18000, 'EST', 'EDT', 'America/New_York US/Eastern America/Toronto America/Detroit EST5EDT '
     'America/Montreal Canada/Eastern')
_def('US', -21600, 'CST', 'CDT', 'America/Chicago US/Central America/Winnipeg CST6CDT Canada/Central')
_def('US', -25200, 'MST', 'MDT', 'America/Denver US/Mountain America/Edmonton MST7MDT Canada/Mountain')
_def('US', -28800, 'PST', 'PDT', 'America/Los_Angeles US/Pacific America/Vancouver PST8PDT Canada/Pacific')
_def('US', -32400, 'AKST', 'AKDT', 'America/Anchorage US/Alaska')
_def('AU', 36000, 'AEST', 'AEDT', 'Australia/Sydney Australia/Melbourne Australia/Canberra Australia/ACT '
     'Australia/NSW Australia/Victoria')
_def('NZ', 43200, 'NZST', 'NZDT', 'Pacific/Auckland NZ')
for _k, _o, _n in (('Asia/Tokyo', 32400, 'JST'), ('Japan', 32400, 'JST'), ('Asia/Seoul', 32400, 'KST'),
                   ('Asia/Shanghai', 28800, 'CST'), ('PRC', 28800, 'CST'), ('Asia/Hong_Kong', 28800, 'HKT'),
                   ('Asia/Singapore', 28800, '+08'), ('Asia/Kolkata', 19800, 'IST'), ('Asia/Calcutta', 19800, 'IST'),
                   ('Asia/Dubai', 14400, '+04'), ('Asia/Karachi', 18000, 'PKT'), ('Asia/Dhaka', 21600, '+06'),
                   ('Asia/Bangkok', 25200, '+07'), ('Asia/Jakarta', 25200, 'WIB'), ('Asia/Manila', 28800, 'PST'),
                   ('Europe/Moscow', 10800, 'MSK'), ('Europe/Istanbul', 10800, '+03'), ('Asia/Riyadh', 10800, '+03'),
                   ('Africa/Cairo', 7200, 'EET'), ('Africa/Johannesburg', 7200, 'SAST'), ('Africa/Lagos', 3600, 'WAT'),
                   ('Africa/Nairobi', 10800, 'EAT'), ('America/Phoenix', -25200, 'MST'),
                   ('America/Bogota', -18000, '-05'), ('America/Lima', -18000, '-05'),
                   ('America/Sao_Paulo', -10800, '-03'), ('America/Argentina/Buenos_Aires', -10800, '-03'),
                   ('America/Mexico_City', -21600, 'CST'), ('Pacific/Honolulu', -36000, 'HST'),
                   ('Australia/Brisbane', 36000, 'AEST'), ('Australia/Perth', 28800, 'AWST')):
    _RULES[_k] = (_o, _n, None, None)


def _dfc(y, m, d):
    # days since 1970-01-01 of a proleptic Gregorian date
    if m <= 2:
        y -= 1
    era = y // 400
    yoe = y - era * 400
    doy = (153 * (m + (-3 if m > 2 else 9)) + 2) // 5 + d - 1
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468


def _nth_sun(y, m, n):
    # day number of the n-th Sunday (n = -1: last) of a month
    if n > 0:
        d0 = _dfc(y, m, 1)
        dow = (d0 + 4) % 7          # 0 = Sunday (1970-01-01 was a Thursday)
        return d0 + (7 - dow) % 7 + 7 * (n - 1)
    ny, nm = (y + 1, 1) if m == 12 else (y, m + 1)
    d1 = _dfc(ny, nm, 1) - 1
    return d1 - (d1 + 4) % 7


def _transitions(rule, y, std):
    # (DST start, DST end) as UTC seconds for year y
    if rule == 'EU':
        return (_nth_sun(y, 3, -1) * 86400 + 3600, _nth_sun(y, 10, -1) * 86400 + 3600)
    if rule == 'US':
        if y >= 2007:
            return (_nth_sun(y, 3, 2) * 86400 + 7200 - std, _nth_sun(y, 11, 1) * 86400 + 7200 - std - 3600)
        return (_nth_sun(y, 4, 1) * 86400 + 7200 - std, _nth_sun(y, 10, -1) * 86400 + 7200 - std - 3600)
    if rule == 'AU':
        return (_nth_sun(y, 10, 1) * 86400 + 7200 - std, _nth_sun(y, 4, 1) * 86400 + 10800 - std - 3600)
    # NZ: last Sunday of September 02:00 std -> first Sunday of April 03:00 dst
    return (_nth_sun(y, 9, -1) * 86400 + 7200 - std, _nth_sun(y, 4, 1) * 86400 + 10800 - std - 3600)


def _isdst_utc(rule, std, u):
    y = _gmyear(u)
    a, b = _transitions(rule, y, std)
    if rule == 'AU' or rule == 'NZ':
        return u >= a or u < b
    return a <= u < b


def _gmyear(u):
    d = u // 86400
    y = 1970 + int(d / 365.2425)
    while _dfc(y, 1, 1) > d:
        y -= 1
    while _dfc(y + 1, 1, 1) <= d:
        y += 1
    return y


class ZoneInfo(tzinfo):
    def __init__(self, key):
        f = _fixed(key)
        self._rule = None
        if f is None:
            r = _RULES.get(key)
            if r is None:
                raise ZoneInfoNotFoundError("'No time zone found with key %s'" % key)
            f = (r[0], r[1])
            self._dstname = r[2]
            self._rule = r[3]
        self._key = key
        self._off = f[0]
        self._name = f[1]

    # -- engine (integer seconds)
    def _off_utc(self, u):
        # (utc offset, dst flag) at the UTC instant u (seconds since the epoch)
        if self._rule is not None and _isdst_utc(self._rule, self._off, u):
            return (self._off + 3600, True)
        return (self._off, False)

    def _wall_offsets(self, w):
        # offsets (list) that can apply to the wall-clock seconds w: [] nonexistent, 2 entries ambiguous
        res = []
        o1 = self._off_utc(w - self._off)
        if o1[0] == self._off:
            res.append(o1)
        if self._rule is not None:
            o2 = self._off_utc(w - self._off - 3600)
            if o2[0] == self._off + 3600:
                res.append(o2)
        return res

    def _dst_start(self, w):
        # UTC seconds of the DST start (spring forward) in the year of the wall-clock seconds w
        return _transitions(self._rule, _gmyear(w), self._off)[0]

    def _fields_secs(self, dt):
        return _dfc(dt.year, dt.month, dt.day) * 86400 + dt.hour * 3600 + dt.minute * 60 + dt.second

    def _at(self, dt):
        w = self._fields_secs(dt)
        c = self._wall_offsets(w)
        if len(c) == 2:
            return c[0] if getattr(dt, 'fold', 0) else c[1]
        if len(c) == 1:
            return c[0]
        # nonexistent (spring forward gap): use the offset from before the gap
        return (self._off, False)

    @classmethod
    def no_cache(cls, key):
        return cls(key)

    @classmethod
    def from_file(cls, fobj, /, key=None):
        raise ValueError('from_file is not supported')

    @classmethod
    def clear_cache(cls, *, only_keys=None):
        _cache.clear()

    @property
    def key(self):
        return self._key

    def utcoffset(self, dt):
        if self._rule is None or dt is None:
            return timedelta(seconds=self._off)
        return timedelta(seconds=self._at(dt)[0])

    def dst(self, dt):
        if self._rule is None or dt is None:
            return timedelta(0)
        return timedelta(seconds=3600 if self._at(dt)[1] else 0)

    def tzname(self, dt):
        if self._rule is not None and dt is not None and self._at(dt)[1]:
            return self._dstname
        return self._name

    def fromutc(self, dt):
        if self._rule is None:
            return dt + timedelta(seconds=self._off)
        u = self._fields_secs(dt)
        return dt + timedelta(seconds=self._off_utc(u)[0])

    def __repr__(self):
        return 'zoneinfo.ZoneInfo(key=%r)' % (self._key,)

    def __str__(self):
        return self._key


def _get(key):
    z = _cache.get(key)
    if z is None:
        z = ZoneInfo(key)
        _cache[key] = z
    return z


def reset_tzpath(to=None):
    pass


def available_timezones():
    r = {'UTC', 'Etc/UTC', 'GMT', 'Etc/GMT'}
    for k in _RULES:
        r.add(k)
    return r
