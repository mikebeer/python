"""scipy.linalg subset on top of numpy.linalg (no complex numbers: eig only supports real spectra)."""
import math
import numpy as np
from ._util import inf, nan, tolist
from . import _la

LinAlgError = np.linalg.LinAlgError
LinAlgWarning = UserWarning


def _mat(a):
    return [[float(v) for v in r] for r in np.asarray(a).tolist()]


def _arr2(a):
    return np.asarray(a) * 1.0


def inv(a, overwrite_a=False, check_finite=True):
    a = _arr2(a)
    if a.ndim != 2 or a.shape[0] != a.shape[1]:
        raise ValueError("expected square matrix")
    try:
        return np.linalg.inv(a)
    except LinAlgError:
        raise LinAlgError("singular matrix")


def det(a, overwrite_a=False, check_finite=True):
    a = _arr2(a)
    if a.ndim != 2 or a.shape[0] != a.shape[1]:
        raise ValueError("expected square matrix")
    return float(np.linalg.det(a))


def solve(a, b, lower=False, overwrite_a=False, overwrite_b=False, check_finite=True, assume_a=None,
          transposed=False):
    a = _arr2(a)
    b = _arr2(b)
    if a.ndim != 2 or a.shape[0] != a.shape[1]:
        raise ValueError("expected square matrix")
    if transposed:
        a = a.T
    try:
        return np.linalg.solve(a, b)
    except LinAlgError:
        raise LinAlgError("Matrix is singular.")


def norm(a, ord=None, axis=None, keepdims=False, check_finite=True):
    a = _arr2(a)
    if ord is None and axis is None:
        s = 0.0
        for v in a.flatten().tolist():
            s += v * v
        return math.sqrt(s)
    return np.linalg.norm(a, ord, axis)


def lstsq(a, b, cond=None, overwrite_a=False, overwrite_b=False, check_finite=True, lapack_driver=None):
    a = _arr2(a)
    b = _arr2(b)
    x, res, rk, sv = np.linalg.lstsq(a, b, rcond=cond)
    if b.ndim == 1 and res.size == 1:
        res = res.tolist()[0]
    return x, res, rk, sv


def pinv(a, atol=None, rtol=None, return_rank=False, check_finite=True, cond=None, rcond=None):
    a = _arr2(a)
    r = rcond if rcond is not None else cond
    if rtol is not None:
        r = rtol
    p = np.linalg.pinv(a) if r is None else np.linalg.pinv(a, r)
    if return_rank:
        return p, int(np.linalg.matrix_rank(a))
    return p


def pinvh(a, atol=None, rtol=None, lower=True, return_rank=False, check_finite=True):
    return pinv(a, atol, rtol, return_rank)


def svd(a, full_matrices=True, compute_uv=True, overwrite_a=False, check_finite=True, lapack_driver="gesdd"):
    a = _arr2(a)
    if not compute_uv:
        return np.linalg.svd(a, False)[1]
    return np.linalg.svd(a, full_matrices)


def svdvals(a, overwrite_a=False, check_finite=True):
    return np.linalg.svd(_arr2(a), False)[1]


def diagsvd(s, M, N):
    s = tolist(s)
    k = len(s)
    out = [[0.0] * N for i in range(M)]
    for i in range(k):
        out[i][i] = s[i]
    return np.array(out)


def eigh(a, b=None, lower=True, eigvals_only=False, overwrite_a=False, overwrite_b=False, turbo=True,
         eigvals=None, type=1, check_finite=True, subset_by_index=None, subset_by_value=None, driver=None):
    a = _arr2(a)
    if b is not None:
        b = _arr2(b)
        L = np.linalg.cholesky(b)
        Li = np.linalg.inv(L)
        c = Li @ a @ Li.T
        w, v = np.linalg.eigh(c)
        v = Li.T @ v
    else:
        w, v = np.linalg.eigh(a)
    if subset_by_index is not None:
        lo, hi = subset_by_index
        w = w[lo:hi + 1]
        v = v[:, lo:hi + 1]
    if eigvals_only:
        return w
    return w, v


def eigvalsh(a, b=None, lower=True, overwrite_a=False, overwrite_b=False, turbo=True, eigvals=None, type=1,
             check_finite=True, subset_by_index=None, subset_by_value=None, driver=None):
    return eigh(a, b, eigvals_only=True, subset_by_index=subset_by_index)


def eig(a, b=None, left=False, right=True, overwrite_a=False, overwrite_b=False, check_finite=True,
        homogeneous_eigvals=False):
    a = _arr2(a)
    if b is not None:
        a = np.linalg.inv(_arr2(b)) @ a
    w, v = np.linalg.eig(a)
    if left:
        w2, vl = np.linalg.eig(a.T)
        return w, vl, v
    return w, v


def eigvals(a, b=None, overwrite_a=False, check_finite=True, homogeneous_eigvals=False):
    return eig(a, b)[0]


def cholesky(a, lower=False, overwrite_a=False, check_finite=True):
    a = _arr2(a)
    try:
        L = np.linalg.cholesky(a)
    except LinAlgError:
        raise LinAlgError("Matrix is not positive definite")
    return L if lower else L.T


def cho_factor(a, lower=False, overwrite_a=False, check_finite=True):
    return (cholesky(a, lower), lower)


def cho_solve(c_and_lower, b, overwrite_b=False, check_finite=True):
    c, lower = c_and_lower
    L = np.asarray(c) if lower else np.asarray(c).T
    b = _arr2(b)
    y = solve_triangular(L, b, lower=True)
    return solve_triangular(L.T, y, lower=False)


def qr(a, overwrite_a=False, lwork=None, mode="full", pivoting=False, check_finite=True):
    a = _arr2(a)
    if pivoting:
        raise NotImplementedError("qr pivoting")
    if mode == "r":
        return (np.linalg.qr(a, "r"),)
    m = {"full": "complete", "economic": "reduced", "raw": "raw"}[mode]
    return np.linalg.qr(a, m)


def solve_triangular(a, b, trans=0, lower=False, unit_diagonal=False, overwrite_b=False, check_finite=True):
    A = _mat(a)
    n = len(A)
    if trans in (1, 2, "T", "C"):
        A = _la.transpose(A)
        lower = not lower
    bb = np.asarray(b) * 1.0
    vec = bb.ndim == 1
    B = [[v] for v in bb.tolist()] if vec else bb.tolist()
    ncol = len(B[0])
    X = [[0.0] * ncol for i in range(n)]
    for c in range(ncol):
        if lower:
            for i in range(n):
                s = B[i][c]
                for j in range(i):
                    s -= A[i][j] * X[j][c]
                if not unit_diagonal:
                    if A[i][i] == 0.0:
                        raise LinAlgError("singular matrix: resolution failed at diagonal %d" % i)
                    s /= A[i][i]
                X[i][c] = s
        else:
            for i in range(n - 1, -1, -1):
                s = B[i][c]
                for j in range(i + 1, n):
                    s -= A[i][j] * X[j][c]
                if not unit_diagonal:
                    if A[i][i] == 0.0:
                        raise LinAlgError("singular matrix: resolution failed at diagonal %d" % i)
                    s /= A[i][i]
                X[i][c] = s
    if vec:
        return np.array([r[0] for r in X])
    return np.array(X)


def lu_factor(a, overwrite_a=False, check_finite=True):
    A = _mat(a)
    m = len(A)
    n = len(A[0])
    piv = []
    for k in range(min(m, n)):
        p = k
        best = abs(A[k][k])
        for i in range(k + 1, m):
            if abs(A[i][k]) > best:
                best = abs(A[i][k])
                p = i
        piv.append(p)
        if p != k:
            t = A[k]
            A[k] = A[p]
            A[p] = t
        if A[k][k] != 0.0:
            for i in range(k + 1, m):
                A[i][k] /= A[k][k]
                f = A[i][k]
                if f != 0.0:
                    for j in range(k + 1, n):
                        A[i][j] -= f * A[k][j]
    return np.array(A), np.array(piv)


def lu(a, permute_l=False, overwrite_a=False, check_finite=True, p_indices=False):
    LU, piv = lu_factor(a)
    LUl = LU.tolist()
    pv = piv.tolist()
    m = len(LUl)
    n = len(LUl[0])
    k = min(m, n)
    L = [[(LUl[i][j] if j < i else (1.0 if i == j else 0.0)) for j in range(k)] for i in range(m)]
    U = [[(LUl[i][j] if j >= i else 0.0) for j in range(n)] for i in range(k)]
    perm = list(range(m))
    for i in range(len(pv)):
        t = perm[i]
        perm[i] = perm[pv[i]]
        perm[pv[i]] = t
    # row i of (P^T A) is row perm[i] of A, so A = P L U with P[perm[i]][i] = 1
    P = [[0.0] * m for r in range(m)]
    for i in range(m):
        P[perm[i]][i] = 1.0
    if permute_l:
        return np.array(_la.matmul(P, L)), np.array(U)
    if p_indices:
        return np.array(perm), np.array(L), np.array(U)
    return np.array(P), np.array(L), np.array(U)


def lu_solve(lu_and_piv, b, trans=0, overwrite_b=False, check_finite=True):
    LU, piv = lu_and_piv
    LUl = _mat(LU)
    n = len(LUl)
    bb = np.asarray(b) * 1.0
    vec = bb.ndim == 1
    B = [[v] for v in bb.tolist()] if vec else bb.tolist()
    pv = piv.tolist() if hasattr(piv, "tolist") else list(piv)
    if trans == 0:
        for i in range(len(pv)):
            if pv[i] != i:
                t = B[i]
                B[i] = B[pv[i]]
                B[pv[i]] = t
        X = solve_triangular(np.array([[LUl[i][j] if j < i else (1.0 if i == j else 0.0) for j in range(n)]
                                       for i in range(n)]), np.array(B), lower=True)
        X = solve_triangular(np.array([[LUl[i][j] if j >= i else 0.0 for j in range(n)] for i in range(n)]), X)
    else:
        Ut = np.array([[LUl[j][i] if j <= i else 0.0 for j in range(n)] for i in range(n)])
        Lt = np.array([[LUl[j][i] if j > i else (1.0 if i == j else 0.0) for j in range(n)] for i in range(n)])
        X = solve_triangular(Ut, np.array(B), lower=True)
        X = solve_triangular(Lt, X, lower=False).tolist()
        for i in range(len(pv) - 1, -1, -1):
            if pv[i] != i:
                t = X[i]
                X[i] = X[pv[i]]
                X[pv[i]] = t
        X = np.array(X)
    if vec:
        return np.array([r[0] for r in X.tolist()])
    return X


_PADE13 = [64764752532480000.0, 32382376266240000.0, 7771770303897600.0, 1187353796428800.0, 129060195264000.0,
           10559470521600.0, 670442572800.0, 33522128640.0, 1323241920.0, 40840800.0, 960960.0, 16380.0, 182.0, 1.0]


def _madd(a, b, fa=1.0, fb=1.0):
    return [[fa * a[i][j] + fb * b[i][j] for j in range(len(a[0]))] for i in range(len(a))]


def _mscale(a, f):
    return [[f * v for v in r] for r in a]


def expm(A):
    M = _mat(A)
    n = len(M)
    if n == 0:
        return np.array(M)
    nrm = 0.0
    for j in range(n):
        s = 0.0
        for i in range(n):
            s += abs(M[i][j])
        if s > nrm:
            nrm = s
    s = 0
    if nrm > 5.371920351148152:
        s = max(0, int(math.ceil(math.log(nrm / 5.371920351148152, 2))))
    if s > 0:
        M = _mscale(M, 1.0 / (2.0 ** s))
    b = _PADE13
    I = _la.eye(n)
    A2 = _la.matmul(M, M)
    A4 = _la.matmul(A2, A2)
    A6 = _la.matmul(A4, A2)
    t1 = _madd(_madd(_mscale(A6, b[13]), _mscale(A4, b[11])), _mscale(A2, b[9]))
    t2 = _madd(_madd(_mscale(A6, b[7]), _mscale(A4, b[5])), _madd(_mscale(A2, b[3]), _mscale(I, b[1])))
    U = _la.matmul(M, _madd(_la.matmul(A6, t1), t2))
    t3 = _madd(_madd(_mscale(A6, b[12]), _mscale(A4, b[10])), _mscale(A2, b[8]))
    t4 = _madd(_madd(_mscale(A6, b[6]), _mscale(A4, b[4])), _madd(_mscale(A2, b[2]), _mscale(I, b[0])))
    V = _madd(_la.matmul(A6, t3), t4)
    R = _la.solve(_madd(V, U, 1.0, -1.0), _madd(V, U))
    for i in range(s):
        R = _la.matmul(R, R)
    return np.array(R)


def sqrtm(A, disp=True, blocksize=64):
    M = _mat(A)
    n = len(M)
    Y = [list(r) for r in M]
    Z = _la.eye(n)
    for it in range(100):
        Yi = _la.inv(Y)
        Zi = _la.inv(Z)
        Yn = _mscale(_madd(Y, Zi), 0.5)
        Zn = _mscale(_madd(Z, Yi), 0.5)
        d = 0.0
        for i in range(n):
            for j in range(n):
                d = max(d, abs(Yn[i][j] - Y[i][j]))
        Y = Yn
        Z = Zn
        if d < 1e-15 * max(1.0, _la.norm_inf([v for r in Y for v in r])):
            break
    return np.array(Y)


def fractional_matrix_power(A, t):
    w, v = np.linalg.eigh(_arr2(A))
    wl = [x ** t for x in tolist(w)]
    return v @ np.diag(np.array(wl)) @ v.T


def block_diag(*arrs):
    mats = []
    for a in arrs:
        a = np.asarray(a)
        if a.ndim == 0:
            mats.append([[float(a.tolist())]])
        elif a.ndim == 1:
            mats.append([a.tolist()])
        else:
            mats.append(a.tolist())
    R = sum([len(m) for m in mats])
    C = sum([len(m[0]) for m in mats])
    out = [[0.0] * C for i in range(R)]
    r = 0
    c = 0
    for m in mats:
        for i in range(len(m)):
            for j in range(len(m[0])):
                out[r + i][c + j] = m[i][j]
        r += len(m)
        c += len(m[0])
    return np.array(out)


def toeplitz(c, r=None):
    cl = np.asarray(c).flatten().tolist()
    rl = cl if r is None else np.asarray(r).flatten().tolist()
    m = len(cl)
    n = len(rl)
    return np.array([[cl[i - j] if i >= j else rl[j - i] for j in range(n)] for i in range(m)])


def circulant(c):
    cl = np.asarray(c).flatten().tolist()
    n = len(cl)
    return np.array([[cl[(i - j) % n] for j in range(n)] for i in range(n)])


def hankel(c, r=None):
    cl = np.asarray(c).flatten().tolist()
    m = len(cl)
    rl = [0] * m if r is None else np.asarray(r).flatten().tolist()
    n = len(rl)
    return np.array([[cl[i + j] if i + j < m else rl[i + j - m + 1] for j in range(n)] for i in range(m)])


def hilbert(n):
    return np.array([[1.0 / (i + j + 1) for j in range(n)] for i in range(n)])


def invhilbert(n, exact=False):
    return np.linalg.inv(hilbert(n))


def pascal(n, kind="symmetric", exact=True):
    if kind == "symmetric":
        return np.array([[math.comb(i + j, i) for j in range(n)] for i in range(n)])
    if kind == "lower":
        return np.array([[math.comb(i, j) for j in range(n)] for i in range(n)])
    return np.array([[math.comb(j, i) for j in range(n)] for i in range(n)])


def null_space(A, rcond=None):
    a = _arr2(A)
    u, s, vh = np.linalg.svd(a, True)
    sl = tolist(s)
    M, N = a.shape
    if rcond is None:
        rcond = 2.220446049250313e-16 * max(M, N)
    tol = (max(sl) if sl else 0.0) * rcond
    num = len([v for v in sl if v > tol])
    return vh[num:, :].T


def orth(A, rcond=None):
    a = _arr2(A)
    u, s, vh = np.linalg.svd(a, True)
    sl = tolist(s)
    M, N = a.shape
    if rcond is None:
        rcond = 2.220446049250313e-16 * max(M, N)
    tol = (max(sl) if sl else 0.0) * rcond
    num = len([v for v in sl if v > tol])
    return u[:, :num]


def polar(a, side="right"):
    a = _arr2(a)
    w, s, vh = np.linalg.svd(a, False)
    u = w @ vh
    if side == "right":
        p = vh.T @ np.diag(s) @ vh
    else:
        p = w @ np.diag(s) @ w.T
    return u, p


def solve_banded(l_and_u, ab, b, overwrite_ab=False, overwrite_b=False, check_finite=True):
    l, u = l_and_u
    ab = _mat(ab)
    n = len(ab[0])
    A = [[0.0] * n for i in range(n)]
    for i in range(n):
        for j in range(n):
            k = u + i - j
            if 0 <= k < l + u + 1:
                A[i][j] = ab[k][j]
    return solve(np.array(A), b)


def matrix_balance(A, permute=True, scale=True, separate=False, overwrite_a=False):
    raise NotImplementedError("matrix_balance")


def logm(A, disp=True):
    raise NotImplementedError("logm requires complex arithmetic")
