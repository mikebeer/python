# Pure-Python additions to the native time module (used where the native module lacks these names).
# Where the native module has the C library calendar functions (_gmtime _localtime _mktime _tzinfo,
# C interpreter built with -DPY_POSIX) the local time zone is real; otherwise _pytime treats the
# local zone as UTC.
import time as _t
import _pytime as _p
from _pytime import struct_time, strptime, timegm

_ng = getattr(_t, '_gmtime', None)
_nl = getattr(_t, '_localtime', None)
_nm = getattr(_t, '_mktime', None)
_nz = getattr(_t, '_tzinfo', None)
try:
    # a host without the C library calendar raises here: then _pytime (UTC) is used
    _ng(0)
    _nl(0)
    _nm(_nl(0))
    _nz()
except Exception:
    _ng = _nl = _nm = _nz = None

if _ng is not None and _nl is not None:
    def gmtime(*args):
        secs = args[-1] if args else None
        if secs is None:
            secs = _t.time()
        return struct_time(_ng(secs))

    def localtime(*args):
        secs = args[-1] if args else None
        if secs is None:
            secs = _t.time()
        return struct_time(_nl(secs))
else:
    from _pytime import gmtime, localtime

if _nm is not None:
    def mktime(t):
        return _nm(t)
else:
    from _pytime import mktime

if _nz is not None:
    timezone, altzone, daylight, _std, _dst = _nz()
    tzname = (_std, _dst)
else:
    from _pytime import timezone, altzone, daylight, tzname


def strftime(format, t=None):
    if t is None:
        t = localtime()
    return _p.strftime(format, t)


def asctime(t=None):
    if t is None:
        t = localtime()
    return _p.asctime(t)


def ctime(secs=None):
    return _p.asctime(localtime(secs))


def tzset():
    pass


thread_time = _t.process_time
thread_time_ns = _t.process_time_ns
CLOCK_REALTIME = 0
CLOCK_MONOTONIC = 1


def clock_gettime(clk_id):
    return _t.time() if clk_id == 0 else _t.monotonic()


def clock_gettime_ns(clk_id):
    return _t.time_ns() if clk_id == 0 else _t.monotonic_ns()
