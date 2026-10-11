# private helpers shared by the scipy submodules (flat float lists, element wise mapping)
import math
import numpy as np

inf = float("inf")
nan = float("nan")


def isarr(x):
    return isinstance(x, np.ndarray)


def tolist(x):
    """flat python list of floats from a scalar / list / tuple / ndarray"""
    if isinstance(x, np.ndarray):
        if x.ndim == 0:
            return [float(x.tolist())]
        return [float(v) for v in x.flatten().tolist()]
    if isinstance(x, (list, tuple)):
        out = []
        for v in x:
            if isinstance(v, (list, tuple, np.ndarray)):
                out.extend(tolist(v))
            else:
                out.append(float(v))
        return out
    return [float(x)]


def vmap(f, x):
    """apply the scalar function f element wise; scalars give scalars, arrays arrays"""
    if isinstance(x, np.ndarray):
        if x.ndim == 0:
            return f(x.tolist())
        return np.array([f(v) for v in x.flatten().tolist()]).reshape(x.shape)
    if isinstance(x, (list, tuple)):
        return vmap(f, np.array(x))
    return f(x)


def vmap2(f, a, b):
    """element wise f(a, b) with broadcasting"""
    sa = isinstance(a, (np.ndarray, list, tuple))
    sb = isinstance(b, (np.ndarray, list, tuple))
    if not sa and not sb:
        return f(a, b)
    if not sa:
        return vmap(lambda v: f(a, v), b)
    if not sb:
        return vmap(lambda v: f(v, b), a)
    a = np.asarray(a)
    b = np.asarray(b)
    if a.shape != b.shape:
        z = (a + b) * 0
        a = a + z
        b = b + z
    fa = a.flatten().tolist()
    fb = b.flatten().tolist()
    return np.array([f(p, q) for p, q in zip(fa, fb)]).reshape(a.shape)


def vmap3(f, a, b, c):
    if not isinstance(a, (np.ndarray, list, tuple)) and not isinstance(b, (np.ndarray, list, tuple)) \
            and not isinstance(c, (np.ndarray, list, tuple)):
        return f(a, b, c)
    a = np.asarray(a)
    b = np.asarray(b)
    c = np.asarray(c)
    z = (a + b + c) * 0
    a = a + z
    b = b + z
    c = c + z
    return np.array([f(p, q, r) for p, q, r in zip(a.flatten().tolist(), b.flatten().tolist(),
                                                     c.flatten().tolist())]).reshape(a.shape)


def slices(a, axis):
    """list of 1-d lists along `axis` of a (1-d or 2-d array-like) -> (lists, kind)"""
    a = np.asarray(a)
    if a.ndim <= 1:
        return [tolist(a)], 1
    if a.ndim == 2:
        if axis < 0:
            axis += 2
        rows = [[float(v) for v in r] for r in a.tolist()]
        if axis == 1:
            return rows, 2
        n = len(rows[0]) if rows else 0
        return [[rows[i][j] for i in range(len(rows))] for j in range(n)], 2
    raise ValueError("only 1-d and 2-d inputs are supported")


def reduce_axis(f, a, axis=0):
    """f(list) -> scalar applied along axis; axis None flattens"""
    if axis is None:
        return f(tolist(a))
    sl, k = slices(a, axis)
    if k == 1:
        return f(sl[0])
    return np.array([f(s) for s in sl])


def map_axis(f, a, axis=0):
    """f(list) -> list of the same length applied along axis; keeps the shape"""
    a = np.asarray(a)
    if axis is None:
        return np.array(f(tolist(a))).reshape(a.shape)
    sl, k = slices(a, axis)
    if k == 1:
        return np.array(f(sl[0]))
    res = [f(s) for s in sl]
    if axis < 0:
        axis += 2
    if axis == 1:
        return np.array(res)
    n = len(res[0]) if res else 0
    return np.array([[res[j][i] for j in range(len(res))] for i in range(n)])


class Result:
    """tuple-like result object with named fields (NamedTuple replacement)"""

    def __init__(self, names, values, extra=None, name="Result"):
        self._names = list(names)
        self._values = list(values)
        self._rname = name
        i = 0
        for n in names:
            setattr(self, n, values[i])
            i += 1
        if extra:
            for k in extra:
                setattr(self, k, extra[k])

    def __iter__(self):
        return iter(self._values)

    def __len__(self):
        return len(self._values)

    def __getitem__(self, i):
        return self._values[i]

    def _asdict(self):
        d = {}
        i = 0
        for n in self._names:
            d[n] = self._values[i]
            i += 1
        return d

    def __repr__(self):
        s = []
        i = 0
        for n in self._names:
            s.append(n + "=" + repr(self._values[i]))
            i += 1
        return self._rname + "(" + ", ".join(s) + ")"

    def __eq__(self, other):
        if isinstance(other, Result):
            return self._values == other._values
        return tuple(self._values) == other
