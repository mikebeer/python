# Pure Python additions to the native sys module.
version_info = (3, 13, 0, "final", 0)
version = "3.13.0 (crexx-py)"
byteorder = "little"
maxunicode = 1114111
try:
    import os as _os
    executable = _os.environ.get("PY_EXECUTABLE") or "py"
    del _os
except Exception:
    executable = "py"
hexversion = 0x030D00F0
implementation = None


class _FloatInfo:
    max = 1.7976931348623157e+308
    min = 2.2250738585072014e-308
    epsilon = 2.220446049250313e-16
    dig = 15
    mant_dig = 53
    radix = 2
    max_exp = 1024
    min_exp = -1021
    max_10_exp = 308
    min_10_exp = -307


float_info = _FloatInfo()


def getsizeof(obj, default=0):
    return 64


def setrecursionlimit(n):
    return None


def getdefaultencoding():
    return "utf-8"
