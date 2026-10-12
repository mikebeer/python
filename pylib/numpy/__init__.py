# Pure Python additions to the native numpy subset (run by both interpreters).
# Everything here is only consulted for names the native module does not provide.
import math
import numpy as np

_range = range


def _arr(x):
    return np.asarray(x)


def _isarr(x):
    return isinstance(x, np.ndarray)


def _shape(a):
    return tuple(a.shape)


def _norm_axis(axis, nd):
    if axis < 0:
        axis += nd
    return axis


def _map(f, x):
    """apply the scalar function f element wise"""
    if not _isarr(x):
        if isinstance(x, (list, tuple)):
            x = np.array(x)
        else:
            return f(x)
    if x.ndim == 0:
        return f(x.tolist())
    flat = x.flatten().tolist()
    return np.array([f(v) for v in flat]).reshape(x.shape)


def _map2(f, a, b):
    a = _arr(a)
    b = _arr(b)
    if a.ndim == 0 and b.ndim == 0:
        return f(a.tolist(), b.tolist())
    if a.shape != b.shape:
        z = np.zeros(np.broadcast_shapes(a.shape, b.shape)) if hasattr(np, 'broadcast_shapes') else (a + b) * 0
        a = a + z
        b = b + z
    fa = a.flatten().tolist()
    fb = b.flatten().tolist()
    return np.array([f(p, q) for p, q in zip(fa, fb)]).reshape(a.shape)


# ---- element wise math -------------------------------------------------

def tanh(x):
    return _map(math.tanh, x)


def sinh(x):
    return _map(math.sinh, x)


def cosh(x):
    return _map(math.cosh, x)


def arctan(x):
    return _map(math.atan, x)


def arcsin(x):
    return _map(math.asin, x)


def arccos(x):
    return _map(math.acos, x)


def arctan2(y, x):
    return _map2(math.atan2, y, x)


def hypot(a, b):
    return _map2(math.hypot, a, b)


def log1p(x):
    return _map(math.log1p, x)


def expm1(x):
    return _map(math.expm1, x)


def cbrt(x):
    return _map(lambda v: math.copysign(abs(v) ** (1.0 / 3.0), v), x)


def exp2(x):
    return _map(lambda v: 2.0 ** v, x)


def _sign(v):
    if v > 0:
        return 1
    if v < 0:
        return -1
    return 0


def sign(x):
    a = _arr(x)
    r = _map(_sign, a)
    if a.ndim > 0 and str(a.dtype).startswith('float'):
        return r.astype(float)
    if a.ndim == 0 and isinstance(a.tolist(), float):
        return float(r)
    return r


def deg2rad(x):
    return _arr(x) * (math.pi / 180.0)


def rad2deg(x):
    return _arr(x) * (180.0 / math.pi)


radians = deg2rad
degrees = rad2deg


def trunc(x):
    return _map(lambda v: float(math.trunc(v)), x)


fix = trunc


def rint(x):
    return _map(lambda v: float(round(v)), x)


def isinf(x):
    return _map(lambda v: v == math.inf or v == -math.inf, x)


def isfinite(x):
    return _map(lambda v: not (v != v or v == math.inf or v == -math.inf), x)


def isscalar(x):
    return isinstance(x, (int, float, bool, str))


def fmax(a, b):
    return np.maximum(a, b)


def fmin(a, b):
    return np.minimum(a, b)


def add(a, b):
    return _arr(a) + _arr(b)


def subtract(a, b):
    return _arr(a) - _arr(b)


def multiply(a, b):
    return _arr(a) * _arr(b)


def divide(a, b):
    return _arr(a) / _arr(b)


def floor_divide(a, b):
    return _arr(a) // _arr(b)


def mod(a, b):
    return _arr(a) % _arr(b)


remainder = mod


def fmod(a, b):
    return _map2(math.fmod, a, b)


def negative(x):
    return -_arr(x)


def reciprocal(x):
    return 1.0 / _arr(x)


def isclose(a, b, rtol=1e-05, atol=1e-08, equal_nan=False):
    sa = not _isarr(a) and not isinstance(a, (list, tuple))
    sb = not _isarr(b) and not isinstance(b, (list, tuple))
    x = _arr(a)
    y = _arr(b)
    if sa and sb:
        if x.tolist() == y.tolist():
            return True
        d = abs(x.tolist() - y.tolist())
        return bool(d <= atol + rtol * abs(y.tolist()))
    return np.abs(x - y) <= atol + rtol * np.abs(y)


def logical_and(a, b):
    return _map2(lambda p, q: bool(p) and bool(q), a, b)


def logical_or(a, b):
    return _map2(lambda p, q: bool(p) or bool(q), a, b)


def logical_xor(a, b):
    return _map2(lambda p, q: bool(p) != bool(q), a, b)


def logical_not(a):
    return _map(lambda p: not p, a)


def nan_to_num(x, nan=0.0, posinf=None, neginf=None):
    big = 1.7976931348623157e308
    if posinf is None:
        posinf = big
    if neginf is None:
        neginf = -big

    def f(v):
        if v != v:
            return nan
        if v == math.inf:
            return posinf
        if v == -math.inf:
            return neginf
        return v
    a = _arr(x)
    if a.ndim == 0:
        return f(a.tolist())
    return _map(f, a.astype(float))


# ---- reductions ignoring nan --------------------------------------------

def _nan_filter(a, axis, f):
    a = _arr(a)
    if axis is None or a.ndim <= 1:
        vals = [v for v in a.flatten().tolist() if v == v]
        return f(vals)
    axis = _norm_axis(axis, a.ndim)
    if a.ndim == 2:
        rows = a.tolist()
        if axis == 0:
            rows = [list(c) for c in zip(*rows)]
        return np.array([f([v for v in r if v == v]) for r in rows])
    raise ValueError("nan reductions support up to 2 dimensions")


def nansum(a, axis=None):
    return _nan_filter(a, axis, lambda v: float(sum(v)))


def nanmean(a, axis=None):
    return _nan_filter(a, axis, lambda v: sum(v) / len(v) if v else math.nan)


def nanmax(a, axis=None):
    return _nan_filter(a, axis, lambda v: max(v) if v else math.nan)


def nanmin(a, axis=None):
    return _nan_filter(a, axis, lambda v: min(v) if v else math.nan)


def nanstd(a, axis=None):
    def f(v):
        if not v:
            return math.nan
        m = sum(v) / len(v)
        return math.sqrt(sum((x - m) ** 2 for x in v) / len(v))
    return _nan_filter(a, axis, f)


def nanvar(a, axis=None):
    def f(v):
        if not v:
            return math.nan
        m = sum(v) / len(v)
        return sum((x - m) ** 2 for x in v) / len(v)
    return _nan_filter(a, axis, f)


def nanmedian(a, axis=None):
    def f(v):
        if not v:
            return math.nan
        s = sorted(v)
        n = len(s)
        if n % 2:
            return s[n // 2]
        return (s[n // 2 - 1] + s[n // 2]) / 2.0
    return _nan_filter(a, axis, f)


def count_nonzero(a, axis=None):
    a = _arr(a)
    if axis is None:
        return sum(1 for v in a.flatten().tolist() if v)
    return (a != 0).sum(axis=axis)


def cumprod(a, axis=None):
    a = _arr(a)
    if a.ndim > 1 and axis is None:
        a = a.flatten()
    if a.ndim == 1:
        out = []
        p = 1
        for v in a.tolist():
            p = p * v
            out.append(p)
        return np.array(out)
    rows = a.tolist()
    axis = _norm_axis(axis, 2)
    if axis == 1:
        return np.array([cumprod(np.array(r)).tolist() for r in rows])
    cols = [cumprod(np.array(list(c))).tolist() for c in zip(*rows)]
    return np.array([list(r) for r in zip(*cols)])


def average(a, axis=None, weights=None):
    a = _arr(a)
    if weights is None:
        return a.mean(axis=axis) if axis is not None else a.mean()
    w = _arr(weights)
    if axis is None:
        return float((a * w).sum() / w.sum())
    return (a * w).sum(axis=axis) / w.sum()


def ptp(a, axis=None):
    a = _arr(a)
    if axis is None:
        return a.max() - a.min()
    return a.max(axis=axis) - a.min(axis=axis)


# ---- shape helpers ------------------------------------------------------

def ndim(a):
    return _arr(a).ndim


def shape(a):
    return _shape(_arr(a))


def size(a):
    return _arr(a).size


def copy(a):
    return np.array(a)


def asanyarray(a):
    return _arr(a)


def ascontiguousarray(a):
    return _arr(a)


def empty_like(a):
    return np.zeros_like(a)


def squeeze(a, axis=None):
    a = _arr(a)
    if axis is None:
        return a.reshape([d for d in a.shape if d != 1])
    axis = _norm_axis(axis, a.ndim)
    s = list(a.shape)
    del s[axis]
    return a.reshape(s)


def expand_dims(a, axis):
    a = _arr(a)
    s = list(a.shape)
    if axis < 0:
        axis += len(s) + 1
    s.insert(axis, 1)
    return a.reshape(s)


def atleast_1d(a):
    a = _arr(a)
    if a.ndim == 0:
        return a.reshape([1])
    return a


def atleast_2d(a):
    a = _arr(a)
    if a.ndim == 0:
        return a.reshape([1, 1])
    if a.ndim == 1:
        return a.reshape([1, a.shape[0]])
    return a


def swapaxes(a, a1, a2):
    a = _arr(a)
    if a.ndim == 2:
        return a.T
    raise ValueError("swapaxes supports 2-d arrays")


def stack(arrays, axis=0):
    arrays = [_arr(a) for a in arrays]
    axis = _norm_axis(axis, arrays[0].ndim + 1)
    return np.concatenate([expand_dims(a, axis) for a in arrays], axis=axis)


def column_stack(arrays):
    cols = []
    for a in arrays:
        a = _arr(a)
        if a.ndim <= 1:
            a = a.reshape([a.size, 1])
        cols.append(a)
    return np.concatenate(cols, axis=1)


def dstack(arrays):
    arrays = [_arr(a) for a in arrays]
    out = []
    for a in arrays:
        if a.ndim == 1:
            a = a.reshape([1, a.shape[0], 1])
        elif a.ndim == 2:
            a = a.reshape([a.shape[0], a.shape[1], 1])
        out.append(a)
    return np.concatenate(out, axis=2)


def _rows(a, axis):
    """a 2-d (or 1-d) array as a list of slices along axis 0"""
    return a.tolist()


def flip(a, axis=None):
    a = _arr(a)
    if a.ndim == 1 or axis is None and a.ndim == 1:
        return np.array(a.tolist()[::-1])
    rows = a.tolist()
    if axis is None:
        return np.array([r[::-1] for r in rows[::-1]])
    axis = _norm_axis(axis, a.ndim)
    if axis == 0:
        return np.array(rows[::-1])
    return np.array([r[::-1] for r in rows])


def fliplr(a):
    return flip(a, 1)


def flipud(a):
    return flip(a, 0)


def roll(a, shift, axis=None):
    a = _arr(a)
    if axis is None:
        flat = a.flatten().tolist()
        n = len(flat)
        if n == 0:
            return a
        s = shift % n
        return np.array(flat[n - s:] + flat[:n - s]).reshape(a.shape)
    axis = _norm_axis(axis, a.ndim)
    rows = a.tolist()
    if a.ndim == 1:
        n = len(rows)
        s = shift % n if n else 0
        return np.array(rows[n - s:] + rows[:n - s])
    if axis == 0:
        n = len(rows)
        s = shift % n
        return np.array(rows[n - s:] + rows[:n - s])
    n = len(rows[0])
    s = shift % n
    return np.array([r[n - s:] + r[:n - s] for r in rows])


def repeat(a, repeats, axis=None):
    a = _arr(a)
    if axis is None or a.ndim == 1:
        out = []
        for v in a.flatten().tolist():
            out.extend([v] * repeats)
        return np.array(out)
    axis = _norm_axis(axis, a.ndim)
    rows = a.tolist()
    if axis == 0:
        out = []
        for r in rows:
            for _ in range(repeats):
                out.append(r)
        return np.array(out)
    return np.array([[v for v in r for _ in range(repeats)] for r in rows])


def tile(a, reps):
    a = _arr(a)
    if isinstance(reps, int):
        reps = (reps,)
    reps = tuple(reps)
    if a.ndim == 1 and len(reps) == 1:
        return np.array(a.tolist() * reps[0])
    if a.ndim == 1:
        a = a.reshape([1, a.shape[0]])
    if len(reps) == 1:
        reps = (1, reps[0])
    rows = a.tolist()
    out = []
    for _ in range(reps[0]):
        for r in rows:
            out.append(r * reps[1])
    return np.array(out)


def _splits(n, sections):
    if isinstance(sections, int):
        if n % sections:
            raise ValueError("array split does not result in an equal division")
        k = n // sections
        return [(i * k, (i + 1) * k) for i in range(sections)]
    idx = [0] + list(sections) + [n]
    return [(idx[i], idx[i + 1]) for i in range(len(idx) - 1)]


def split(a, sections, axis=0):
    a = _arr(a)
    axis = _norm_axis(axis, a.ndim)
    rows = a.tolist()
    out = []
    if axis == 0:
        for lo, hi in _splits(len(rows), sections):
            out.append(np.array(rows[lo:hi]))
    else:
        for lo, hi in _splits(len(rows[0]), sections):
            out.append(np.array([r[lo:hi] for r in rows]))
    return out


def array_split(a, sections, axis=0):
    a = _arr(a)
    n = a.shape[_norm_axis(axis, a.ndim)]
    if isinstance(sections, int):
        q, r = divmod(n, sections)
        sizes = [q + 1] * r + [q] * (sections - r)
        idx = []
        s = 0
        for z in sizes[:-1]:
            s += z
            idx.append(s)
        return split(a, idx, axis)
    return split(a, sections, axis)


def take(a, indices, axis=None):
    a = _arr(a)
    if axis is None:
        flat = a.flatten().tolist()
        if isinstance(indices, int):
            return flat[indices]
        return np.array([flat[i] for i in _arr(indices).flatten().tolist()])
    axis = _norm_axis(axis, a.ndim)
    rows = a.tolist()
    if axis == 0:
        if isinstance(indices, int):
            return np.array(rows[indices])
        return np.array([rows[i] for i in indices])
    if isinstance(indices, int):
        return np.array([r[indices] for r in rows])
    return np.array([[r[i] for i in indices] for r in rows])


def append(a, values, axis=None):
    a = _arr(a)
    v = _arr(values)
    if axis is None:
        return np.concatenate([a.flatten(), v.flatten()])
    return np.concatenate([a, v], axis=axis)


def delete(a, obj, axis=None):
    a = _arr(a)
    if isinstance(obj, int):
        obj = [obj]
    obj = [i % a.size if axis is None else i for i in obj]
    if axis is None or a.ndim == 1:
        flat = a.flatten().tolist()
        return np.array([v for i, v in enumerate(flat) if i not in obj])
    axis = _norm_axis(axis, a.ndim)
    rows = a.tolist()
    if axis == 0:
        return np.array([r for i, r in enumerate(rows) if i not in obj])
    return np.array([[v for j, v in enumerate(r) if j not in obj] for r in rows])


def nonzero(a):
    a = _arr(a)
    if a.ndim == 1:
        return (np.array([i for i, v in enumerate(a.tolist()) if v]),)
    rows = a.tolist()
    ii = []
    jj = []
    for i, r in enumerate(rows):
        for j, v in enumerate(r):
            if v:
                ii.append(i)
                jj.append(j)
    return (np.array(ii), np.array(jj))


def flatnonzero(a):
    return np.array([i for i, v in enumerate(_arr(a).flatten().tolist()) if v])


# ---- construction -------------------------------------------------------

def meshgrid(*xi, indexing='xy'):
    xs = [_arr(x).flatten() for x in xi]
    n = len(xs)
    sizes = [x.shape[0] for x in xs]
    if indexing == 'xy' and n >= 2:
        sizes[0], sizes[1] = sizes[1], sizes[0]
    out = []
    zeros = np.zeros(sizes)
    for i in range(n):
        s = [1] * n
        k = i
        if indexing == 'xy' and n >= 2 and i < 2:
            k = 1 - i
        s[k] = xs[i].shape[0]
        z = zeros
        if str(xs[i].dtype).startswith('int'):
            z = zeros.astype(int)
        out.append(xs[i].reshape(s) + z)
    return out


def outer(a, b):
    a = _arr(a).flatten()
    b = _arr(b).flatten()
    return a.reshape([a.shape[0], 1]) * b.reshape([1, b.shape[0]])


def cross(a, b):
    a = _arr(a)
    b = _arr(b)
    if a.ndim == 2 or b.ndim == 2:
        ra = a.tolist() if a.ndim == 2 else [a.tolist()] * len(b.tolist())
        rb = b.tolist() if b.ndim == 2 else [b.tolist()] * len(a.tolist())
        return np.array([cross(np.array(p), np.array(q)).tolist() for p, q in zip(ra, rb)])
    p = a.tolist()
    q = b.tolist()
    if len(p) == 2 and len(q) == 2:
        return p[0] * q[1] - p[1] * q[0]
    return np.array([p[1] * q[2] - p[2] * q[1], p[2] * q[0] - p[0] * q[2], p[0] * q[1] - p[1] * q[0]])


def vdot(a, b):
    return np.dot(_arr(a).flatten(), _arr(b).flatten())


def inner(a, b):
    a = _arr(a)
    b = _arr(b)
    if a.ndim <= 1 and b.ndim <= 1:
        return np.dot(a, b)
    return np.dot(a, b.T)


def tensordot(a, b, axes=2):
    a = _arr(a)
    b = _arr(b)
    if axes == 2 and a.ndim == 2 and b.ndim == 2:
        return (a * b).sum()
    if axes == 1:
        return np.dot(a, b)
    return np.dot(a, b)


def kron(a, b):
    a = _arr(a)
    b = _arr(b)
    if a.ndim <= 1 and b.ndim <= 1:
        out = []
        for p in a.tolist():
            for q in b.tolist():
                out.append(p * q)
        return np.array(out)
    if a.ndim == 1:
        a = a.reshape([1, a.shape[0]])
    if b.ndim == 1:
        b = b.reshape([1, b.shape[0]])
    ra = a.tolist()
    rb = b.tolist()
    out = []
    for r1 in ra:
        for r2 in rb:
            row = []
            for p in r1:
                for q in r2:
                    row.append(p * q)
            out.append(row)
    return np.array(out)


def tril(m, k=0):
    m = _arr(m)
    rows = m.tolist()
    z = 0 if not str(m.dtype).startswith('float') else 0.0
    return np.array([[v if j - i <= k else z for j, v in enumerate(r)] for i, r in enumerate(rows)])


def triu(m, k=0):
    m = _arr(m)
    rows = m.tolist()
    z = 0 if not str(m.dtype).startswith('float') else 0.0
    return np.array([[v if j - i >= k else z for j, v in enumerate(r)] for i, r in enumerate(rows)])


def diagflat(v):
    return np.diag(_arr(v).flatten())


def vander(x, N=None, increasing=False):
    x = _arr(x).flatten().tolist()
    if N is None:
        N = len(x)
    rows = []
    for v in x:
        r = [v ** p for p in range(N)]
        if not increasing:
            r = r[::-1]
        rows.append(r)
    return np.array(rows)


def diag_indices(n):
    r = np.arange(n)
    return (r, r)


def tril_indices(n, k=0):
    ii = []
    jj = []
    for i in range(n):
        for j in range(n):
            if j - i <= k:
                ii.append(i)
                jj.append(j)
    return (np.array(ii), np.array(jj))


def triu_indices(n, k=0):
    ii = []
    jj = []
    for i in range(n):
        for j in range(n):
            if j - i >= k:
                ii.append(i)
                jj.append(j)
    return (np.array(ii), np.array(jj))


def broadcast_to(a, shape):
    a = _arr(a)
    return a + np.zeros(shape)


# ---- sorting / searching / sets ------------------------------------------

def argsort(a, axis=-1, kind=None):
    a = _arr(a)
    if a.ndim == 1:
        v = a.tolist()
        return np.array(sorted(range(len(v)), key=lambda i: v[i]))
    axis = _norm_axis(axis, a.ndim)
    rows = a.tolist()
    if axis == 1:
        return np.array([argsort(np.array(r)).tolist() for r in rows])
    cols = [argsort(np.array(list(c))).tolist() for c in zip(*rows)]
    return np.array([list(r) for r in zip(*cols)])


def lexsort(keys):
    keys = [_arr(k).tolist() for k in keys]
    n = len(keys[0])
    return np.array(sorted(range(n), key=lambda i: tuple(k[i] for k in reversed(keys))))


def searchsorted(a, v, side='left'):
    lst = _arr(a).tolist()

    def one(x):
        lo = 0
        hi = len(lst)
        while lo < hi:
            mid = (lo + hi) // 2
            if (lst[mid] < x) if side == 'left' else (lst[mid] <= x):
                lo = mid + 1
            else:
                hi = mid
        return lo
    if isinstance(v, (int, float)):
        return one(v)
    vv = _arr(v)
    if vv.ndim == 0:
        return one(vv.tolist())
    return np.array([one(x) for x in vv.flatten().tolist()]).reshape(vv.shape)


def digitize(x, bins, right=False):
    b = _arr(bins).tolist()
    side = 'left' if right else 'right'
    return searchsorted(b, x, side=side)


def isin(a, b):
    s = _arr(b).flatten().tolist()
    return _map(lambda v: v in s, a)


in1d = isin


def intersect1d(a, b):
    sb = set(_arr(b).flatten().tolist())
    return np.array(sorted(set(v for v in _arr(a).flatten().tolist() if v in sb)))


def union1d(a, b):
    return np.array(sorted(set(_arr(a).flatten().tolist()) | set(_arr(b).flatten().tolist())))


def setdiff1d(a, b):
    sb = set(_arr(b).flatten().tolist())
    return np.array(sorted(set(v for v in _arr(a).flatten().tolist() if v not in sb)))


def bincount(x, weights=None, minlength=0):
    xs = _arr(x).tolist()
    n = max(max(xs) + 1 if xs else 0, minlength)
    out = [0] * n
    if weights is None:
        for v in xs:
            out[v] += 1
        return np.array(out)
    out = [0.0] * n
    for v, w in zip(xs, _arr(weights).tolist()):
        out[v] += w
    return np.array(out)


def interp(x, xp, fp):
    xs = _arr(xp).tolist()
    ys = _arr(fp).tolist()

    def one(v):
        if v <= xs[0]:
            return float(ys[0])
        if v >= xs[-1]:
            return float(ys[-1])
        lo = 0
        hi = len(xs) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if xs[mid] <= v:
                lo = mid
            else:
                hi = mid
        t = (v - xs[lo]) / (xs[hi] - xs[lo])
        return ys[lo] + t * (ys[hi] - ys[lo])
    if isinstance(x, (int, float)):
        return one(x)
    return _map(one, x)


# ---- statistics -----------------------------------------------------------

def _quantile_sorted(s, q):
    n = len(s)
    if n == 1:
        return float(s[0])
    pos = q * (n - 1)
    lo = int(math.floor(pos))
    hi = min(lo + 1, n - 1)
    frac = pos - lo
    return s[lo] + (s[hi] - s[lo]) * frac


def _quant(a, q, axis, scale):
    a = _arr(a)
    single = not isinstance(q, (list, tuple)) and not _isarr(q)
    qs = [q] if single else _arr(q).flatten().tolist()
    if axis is None or a.ndim == 1:
        s = sorted(a.flatten().tolist())
        out = [_quantile_sorted(s, x / scale) for x in qs]
        if single:
            return out[0]
        return np.array(out)
    axis = _norm_axis(axis, a.ndim)
    rows = a.tolist()
    if axis == 0:
        groups = [list(c) for c in zip(*rows)]
    else:
        groups = rows
    res = [[_quantile_sorted(sorted(g), x / scale) for g in groups] for x in qs]
    if single:
        return np.array(res[0])
    return np.array(res)


def percentile(a, q, axis=None):
    return _quant(a, q, axis, 100.0)


def quantile(a, q, axis=None):
    return _quant(a, q, axis, 1.0)


def histogram(a, bins=10, range=None, density=False, weights=None):
    vals = _arr(a).flatten().tolist()
    if isinstance(bins, int):
        if range is None:
            lo = min(vals)
            hi = max(vals)
        else:
            lo, hi = range
        if lo == hi:
            lo = lo - 0.5
            hi = hi + 0.5
        step = (hi - lo) / bins
        edges = [lo + i * step for i in _range(bins + 1)]
        edges[-1] = hi
    else:
        edges = _arr(bins).tolist()
    nb = len(edges) - 1
    counts = [0] * nb
    wl = None if weights is None else _arr(weights).flatten().tolist()
    for k, v in enumerate(vals):
        if v < edges[0] or v > edges[-1]:
            continue
        lo_i = 0
        hi_i = nb
        while hi_i - lo_i > 1:
            mid = (lo_i + hi_i) // 2
            if edges[mid] <= v:
                lo_i = mid
            else:
                hi_i = mid
        counts[lo_i] += 1 if wl is None else wl[k]
    if density:
        tot = sum(counts)
        counts = [c / tot / (edges[i + 1] - edges[i]) for i, c in enumerate(counts)]
        return np.array(counts), np.array(edges)
    return np.array(counts), np.array([float(e) for e in edges])




def corrcoef(x, y=None, rowvar=True):
    c = cov(x, y, rowvar=rowvar)
    if c.ndim == 0:
        return 1.0
    d = [math.sqrt(c[i][i]) for i in _range(c.shape[0])]
    n = c.shape[0]
    return np.array([[c[i][j] / (d[i] * d[j]) for j in _range(n)] for i in _range(n)])


def _cov_rows(m, rowvar):
    m = _arr(m)
    if m.ndim == 1:
        return [m.tolist()]
    rows = m.tolist()
    if not rowvar:
        rows = [list(c) for c in zip(*rows)]
    return rows


def cov(m, y=None, rowvar=True, bias=False, ddof=None):
    rows = _cov_rows(m, rowvar)
    if y is not None:
        rows = rows + _cov_rows(y, rowvar)
    if ddof is None:
        ddof = 0 if bias else 1
    n = len(rows[0])
    means = [sum(r) / n for r in rows]
    k = len(rows)
    out = [[0.0] * k for _ in _range(k)]
    for i in _range(k):
        for j in _range(i, k):
            s = 0.0
            for t in _range(n):
                s += (rows[i][t] - means[i]) * (rows[j][t] - means[j])
            out[i][j] = s / (n - ddof)
            out[j][i] = out[i][j]
    if k == 1:
        return out[0][0]
    return np.array(out)


def diff(a, n=1, axis=-1):
    a = _arr(a)
    for _ in _range(n):
        if a.ndim == 1:
            a = a[1:] - a[:-1]
        else:
            ax = _norm_axis(axis, a.ndim)
            if ax == 0:
                a = a[1:] - a[:-1]
            else:
                a = a[:, 1:] - a[:, :-1]
    return a


def gradient(f, *varargs):
    f = _arr(f)
    v = f.tolist()
    n = len(v)
    if len(varargs) == 0:
        x = [float(i) for i in _range(n)]
    elif isinstance(varargs[0], (int, float)):
        x = [i * varargs[0] for i in _range(n)]
    else:
        x = _arr(varargs[0]).tolist()
    out = [0.0] * n
    out[0] = (v[1] - v[0]) / (x[1] - x[0])
    out[n - 1] = (v[n - 1] - v[n - 2]) / (x[n - 1] - x[n - 2])
    for i in _range(1, n - 1):
        hs = x[i] - x[i - 1]
        hd = x[i + 1] - x[i]
        out[i] = (hs * hs * v[i + 1] + (hd * hd - hs * hs) * v[i] - hd * hd * v[i - 1]) / (hs * hd * (hd + hs))
    return np.array(out)


def trapezoid(y, x=None, dx=1.0):
    ys = _arr(y).tolist()
    s = 0.0
    if x is None:
        for i in _range(len(ys) - 1):
            s += (ys[i] + ys[i + 1]) * dx / 2.0
    else:
        xs = _arr(x).tolist()
        for i in _range(len(ys) - 1):
            s += (ys[i] + ys[i + 1]) * (xs[i + 1] - xs[i]) / 2.0
    return s


trapz = trapezoid


def polyval(p, x):
    p = _arr(p).tolist()

    def one(v):
        r = 0
        for c in p:
            r = r * v + c
        return r
    if isinstance(x, (int, float)):
        return one(x)
    return _map(one, x)


def polyfit(x, y, deg):
    from numpy import linalg as _la
    xs = _arr(x).flatten().tolist()
    A = np.array([[v ** (deg - j) for j in _range(deg + 1)] for v in xs], dtype=float)
    sol = _la.lstsq(A, _arr(y).astype(float), rcond=None)[0]
    return sol


def polyder(p):
    p = _arr(p).tolist()
    n = len(p) - 1
    return np.array([c * (n - i) for i, c in enumerate(p[:-1])])


def polyadd(a, b):
    a = _arr(a).tolist()
    b = _arr(b).tolist()
    n = max(len(a), len(b))
    a = [0] * (n - len(a)) + a
    b = [0] * (n - len(b)) + b
    return np.array([p + q for p, q in zip(a, b)])


def convolve(a, v, mode='full'):
    a = _arr(a).tolist()
    v = _arr(v).tolist()
    n = len(a)
    m = len(v)
    full = [0] * (n + m - 1)
    for i in _range(n):
        for j in _range(m):
            full[i + j] += a[i] * v[j]
    if mode == 'full':
        return np.array(full)
    if mode == 'same':
        lo = (m - 1) // 2
        return np.array(full[lo:lo + max(n, m)])
    return np.array(full[m - 1:n])


class _LazySub:
    """np.fft: the native numpy module has no submodule path, so the sub module is exposed as an
    attribute of this overlay and loaded on first use"""

    def __init__(self, name):
        self._name = name
        self._mod = None

    def _load(self):
        if self._mod is None:
            import sys
            import numpy._overlay.fft as m
            sys.modules['numpy.' + self._name] = m
            self._mod = m
        return self._mod

    def __getattr__(self, attr):
        if attr[:2] == '__':
            raise AttributeError(attr)
        return getattr(self._load(), attr)

    def __repr__(self):
        return "<module 'numpy.%s'>" % self._name


fft = _LazySub('fft')


def roots(p):
    from numpy._cplx import roots as _roots
    return _roots(p)


def _cx(name):
    def f(*a, **k):
        import numpy._cplx as c
        return getattr(c, name)(*a, **k)
    return f


angle = _cx("angle")
conj = _cx("conj")
conjugate = _cx("conj")
real = _cx("real")
imag = _cx("imag")
iscomplex = _cx("iscomplex")
isreal = _cx("isreal")
iscomplexobj = _cx("iscomplexobj")
vdot = _cx("vdot")
