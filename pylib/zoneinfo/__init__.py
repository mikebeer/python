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


class ZoneInfo(tzinfo):
    def __init__(self, key):
        f = _fixed(key)
        if f is None:
            raise ZoneInfoNotFoundError("'No time zone found with key %s'" % key)
        self._key = key
        self._off = f[0]
        self._name = f[1]

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
        return timedelta(seconds=self._off)

    def dst(self, dt):
        return timedelta(0)

    def tzname(self, dt):
        return self._name

    def fromutc(self, dt):
        return dt + timedelta(seconds=self._off)

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
    return {'UTC', 'Etc/UTC', 'GMT', 'Etc/GMT'}
