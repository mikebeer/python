"""scipy.spatial.distance subset"""
import math
import numpy as np
from .._util import inf, nan, tolist


def _vec(u):
    return tolist(u)


def _w(w, n):
    if w is None:
        return None
    return _vec(w)


def minkowski(u, v, p=2, w=None):
    a = _vec(u)
    b = _vec(v)
    wt = _w(w, len(a))
    if p <= 0:
        raise ValueError("p must be greater than 0")
    if p == inf:
        return max([abs(a[i] - b[i]) for i in range(len(a)) if wt is None or wt[i] != 0] or [0.0])
    s = 0.0
    for i in range(len(a)):
        d = abs(a[i] - b[i])
        t = d ** p
        if wt is not None:
            t = wt[i] * t
        s += t
    if p == 2:
        return math.sqrt(s)
    if p == 1:
        return s
    return s ** (1.0 / p)


def euclidean(u, v, w=None):
    return minkowski(u, v, 2, w)


def sqeuclidean(u, v, w=None):
    a = _vec(u)
    b = _vec(v)
    wt = _w(w, len(a))
    s = 0.0
    for i in range(len(a)):
        d = a[i] - b[i]
        s += d * d * (1.0 if wt is None else wt[i])
    return s


def cityblock(u, v, w=None):
    return minkowski(u, v, 1, w)


def chebyshev(u, v, w=None):
    return minkowski(u, v, inf, w)


def _wavg(x, w):
    if w is None:
        return sum(x) / len(x)
    return sum([x[i] * w[i] for i in range(len(x))]) / sum(w)


def correlation(u, v, w=None, centered=True):
    a = _vec(u)
    b = _vec(v)
    wt = _w(w, len(a))
    if centered:
        am = _wavg(a, wt)
        bm = _wavg(b, wt)
        a = [t - am for t in a]
        b = [t - bm for t in b]
    uv = _wavg([a[i] * b[i] for i in range(len(a))], wt)
    uu = _wavg([t * t for t in a], wt)
    vv = _wavg([t * t for t in b], wt)
    den = math.sqrt(uu * vv)
    if den == 0.0:
        return nan
    dist = 1.0 - uv / den
    return max(0.0, min(dist, 2.0))


def cosine(u, v, w=None):
    return correlation(u, v, w, centered=False)


def braycurtis(u, v, w=None):
    a = _vec(u)
    b = _vec(v)
    wt = _w(w, len(a))
    num = 0.0
    den = 0.0
    for i in range(len(a)):
        f = 1.0 if wt is None else wt[i]
        num += f * abs(a[i] - b[i])
        den += f * abs(a[i] + b[i])
    return num / den if den != 0 else nan


def canberra(u, v, w=None):
    a = _vec(u)
    b = _vec(v)
    wt = _w(w, len(a))
    s = 0.0
    for i in range(len(a)):
        d = abs(a[i]) + abs(b[i])
        if d != 0:
            s += (1.0 if wt is None else wt[i]) * abs(a[i] - b[i]) / d
    return s


def hamming(u, v, w=None):
    a = tolist(u)
    b = tolist(v)
    wt = _w(w, len(a))
    num = 0.0
    den = 0.0
    for i in range(len(a)):
        f = 1.0 if wt is None else wt[i]
        if a[i] != b[i]:
            num += f
        den += f
    return num / den


def jaccard(u, v, w=None):
    a = tolist(u)
    b = tolist(v)
    wt = _w(w, len(a))
    nz = 0.0
    nt = 0.0
    for i in range(len(a)):
        f = 1.0 if wt is None else wt[i]
        x = a[i] != 0
        y = b[i] != 0
        if x or y:
            nt += f
            if x != y:
                nz += f
    return nz / nt if nt != 0 else 0.0


def dice(u, v, w=None):
    a = tolist(u)
    b = tolist(v)
    ctt = 0.0
    cdiff = 0.0
    for i in range(len(a)):
        x = a[i] != 0
        y = b[i] != 0
        if x and y:
            ctt += 1
        elif x != y:
            cdiff += 1
    return cdiff / (2.0 * ctt + cdiff)


def mahalanobis(u, v, VI):
    a = _vec(u)
    b = _vec(v)
    d = [a[i] - b[i] for i in range(len(a))]
    M = np.asarray(VI).tolist()
    s = 0.0
    for i in range(len(d)):
        t = 0.0
        for j in range(len(d)):
            t += M[i][j] * d[j]
        s += t * d[i]
    return math.sqrt(s)


def seuclidean(u, v, V):
    a = _vec(u)
    b = _vec(v)
    vv = _vec(V)
    return math.sqrt(sum([(a[i] - b[i]) ** 2 / vv[i] for i in range(len(a))]))


_METRICS = {"euclidean": euclidean, "sqeuclidean": sqeuclidean, "cityblock": cityblock, "manhattan": cityblock,
            "cityblock_": cityblock, "chebyshev": chebyshev, "cosine": cosine, "correlation": correlation,
            "braycurtis": braycurtis, "canberra": canberra, "hamming": hamming, "jaccard": jaccard, "dice": dice,
            "minkowski": minkowski, "mahalanobis": mahalanobis, "seuclidean": seuclidean, "l1": cityblock,
            "l2": euclidean, "taxicab": cityblock}


def _metric_fn(metric, kw):
    if callable(metric):
        return lambda a, b: float(metric(np.array(a), np.array(b), **kw))
    if metric == "cityblock" or metric == "chebyshev" or metric == "euclidean" or metric == "sqeuclidean":
        pass
    m = metric.lower()
    if m not in _METRICS:
        raise ValueError("Unknown Distance Metric: " + str(metric))
    f = _METRICS[m]
    if m == "mahalanobis" and "VI" not in kw:
        return None
    return lambda a, b: f(a, b, **kw)


def cdist(XA, XB, metric="euclidean", *, out=None, **kwargs):
    A = np.asarray(XA)
    B = np.asarray(XB)
    if A.ndim != 2 or B.ndim != 2:
        raise ValueError("XA must be a 2-dimensional array.")
    ra = [[float(v) for v in r] for r in A.tolist()]
    rb = [[float(v) for v in r] for r in B.tolist()]
    if not callable(metric) and metric.lower() == "mahalanobis" and "VI" not in kwargs:
        allp = np.vstack([A, B]) if False else np.concatenate([A, B])
        kwargs["VI"] = np.linalg.inv(np.cov(allp.T)).T
    if not callable(metric) and metric.lower() == "seuclidean" and "V" not in kwargs:
        allp = np.concatenate([A, B])
        kwargs["V"] = np.var(allp, axis=0, ddof=1)
    fn = _metric_fn(metric, kwargs)
    return np.array([[fn(a, b) for b in rb] for a in ra])


def pdist(X, metric="euclidean", *, out=None, **kwargs):
    A = np.asarray(X)
    if A.ndim != 2:
        raise ValueError("A 2-dimensional array must be passed.")
    ra = [[float(v) for v in r] for r in A.tolist()]
    if not callable(metric) and metric.lower() == "mahalanobis" and "VI" not in kwargs:
        kwargs["VI"] = np.linalg.inv(np.cov(A.T)).T
    if not callable(metric) and metric.lower() == "seuclidean" and "V" not in kwargs:
        kwargs["V"] = np.var(A, axis=0, ddof=1)
    fn = _metric_fn(metric, kwargs)
    n = len(ra)
    res = []
    for i in range(n):
        for j in range(i + 1, n):
            res.append(fn(ra[i], ra[j]))
    return np.array(res)


def squareform(X, force="no", checks=True):
    A = np.asarray(X)
    if A.ndim == 1:
        m = A.shape[0]
        d = int(math.ceil(math.sqrt(m * 2)))
        if d * (d - 1) != m * 2:
            raise ValueError("Incompatible vector size. It must be a binomial coefficient n choose 2 for some integer n >= 2.")
        v = A.tolist()
        out = [[0.0 * v[0]] * d for i in range(d)] if m else [[0.0]]
        out = [list(r) for r in out]
        k = 0
        for i in range(d):
            for j in range(i + 1, d):
                out[i][j] = v[k]
                out[j][i] = v[k]
                k += 1
        return np.array(out)
    if A.ndim == 2:
        n = A.shape[0]
        if A.shape[1] != n:
            raise ValueError("The matrix argument must be square.")
        rows = A.tolist()
        if checks:
            for i in range(n):
                if rows[i][i] != 0:
                    raise ValueError("Distance matrix 'X' diagonal must be zero.")
                for j in range(i + 1, n):
                    if rows[i][j] != rows[j][i]:
                        raise ValueError("Distance matrix 'X' must be symmetric.")
        out = []
        for i in range(n):
            for j in range(i + 1, n):
                out.append(rows[i][j])
        return np.array(out)
    raise ValueError("The first argument must be one or two dimensional array.")


def is_valid_dm(D, tol=0.0, throw=False, name="D", warning=False):
    a = np.asarray(D)
    if a.ndim != 2 or a.shape[0] != a.shape[1]:
        return False
    rows = a.tolist()
    n = len(rows)
    for i in range(n):
        if abs(rows[i][i]) > tol:
            return False
        for j in range(i + 1, n):
            if abs(rows[i][j] - rows[j][i]) > tol:
                return False
    return True
