"""scipy.sparse.linalg subset (dense fallbacks for small problems)"""
import math
import numpy as np
from .._util import tolist


def _issp(A):
    from . import spmatrix
    return isinstance(A, spmatrix)


def _dense(A):
    if _issp(A):
        return A.toarray() * 1.0
    return np.asarray(A) * 1.0


def spsolve(A, b, permc_spec=None, use_umfpack=True):
    bd = b.toarray() if _issp(b) else np.asarray(b)
    x = np.linalg.solve(_dense(A), bd * 1.0)
    if _issp(b):
        from . import csc_matrix
        return csc_matrix(x)
    return x


def inv(A):
    from . import csc_matrix
    return csc_matrix(np.linalg.inv(_dense(A)))


def norm(x, ord=None, axis=None):
    return np.linalg.norm(_dense(x), ord, axis) if ord is not None or axis is not None else \
        math.sqrt(sum([v * v for v in _dense(x).flatten().tolist()]))


def expm(A):
    from .. import linalg as sl
    from . import csc_matrix
    return csc_matrix(sl.expm(_dense(A)))


def cg(A, b, x0=None, tol=1e-5, maxiter=None, M=None, callback=None, atol=0.0, rtol=None):
    Ad = _dense(A)
    bl = tolist(b)
    n = len(bl)
    x = tolist(x0) if x0 is not None else [0.0] * n
    Al = Ad.tolist()

    def mv(v):
        return [sum([Al[i][j] * v[j] for j in range(n)]) for i in range(n)]
    r = [bl[i] - mv(x)[i] for i in range(n)]
    p = list(r)
    rs = sum([v * v for v in r])
    bn = math.sqrt(sum([v * v for v in bl]))
    t = rtol if rtol is not None else tol
    lim = maxiter if maxiter is not None else n * 10
    info = 0
    for it in range(lim):
        if math.sqrt(rs) <= max(t * bn, atol):
            return np.array(x), 0
        Ap = mv(p)
        alpha = rs / sum([p[i] * Ap[i] for i in range(n)])
        x = [x[i] + alpha * p[i] for i in range(n)]
        r = [r[i] - alpha * Ap[i] for i in range(n)]
        rn = sum([v * v for v in r])
        p = [r[i] + rn / rs * p[i] for i in range(n)]
        rs = rn
        if callback is not None:
            callback(np.array(x))
    if math.sqrt(rs) <= max(t * bn, atol):
        return np.array(x), 0
    return np.array(x), lim


def eigsh(A, k=6, M=None, sigma=None, which="LM", v0=None, ncv=None, maxiter=None, tol=0, return_eigenvectors=True):
    w, v = np.linalg.eigh(_dense(A))
    wl = w.tolist()
    n = len(wl)
    idx = list(range(n))
    if which == "LM":
        idx.sort(key=lambda i: -abs(wl[i]))
    elif which == "LA":
        idx.sort(key=lambda i: -wl[i])
    elif which == "SA":
        idx.sort(key=lambda i: wl[i])
    elif which == "SM":
        idx.sort(key=lambda i: abs(wl[i]))
    sel = sorted(idx[:k], key=lambda i: wl[i])
    vals = np.array([wl[i] for i in sel])
    if not return_eigenvectors:
        return vals
    vv = v.tolist()
    return vals, np.array([[vv[r][i] for i in sel] for r in range(n)])


def svds(A, k=6, which="LM", return_singular_vectors=True, **kw):
    u, s, vh = np.linalg.svd(_dense(A), False)
    sl = s.tolist()
    sel = sorted(range(min(k, len(sl))), key=lambda i: sl[i])
    sv = np.array([sl[i] for i in sel])
    if not return_singular_vectors:
        return sv
    ul = u.tolist()
    vl = vh.tolist()
    return np.array([[ul[r][i] for i in sel] for r in range(len(ul))]), sv, np.array([vl[i] for i in sel])
