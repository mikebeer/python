# numpy.linalg in pure Python (small dense matrices), shared by both interpreters
import math
import numpy as np

_range = range


class LinAlgError(Exception):
    pass


def _mat(a):
    a = np.asarray(a)
    if a.ndim == 1:
        return [[float(v)] for v in a.tolist()]
    return [[float(v) for v in row] for row in a.tolist()]


def _vec(a):
    return [float(v) for v in np.asarray(a).flatten().tolist()]


def _square(a):
    a = np.asarray(a)
    if a.ndim != 2:
        raise LinAlgError("%d-dimensional array given. Array must be at least two-dimensional" % a.ndim)
    if a.shape[0] != a.shape[1]:
        raise LinAlgError("Last 2 dimensions of the array must be square")
    return _mat(a)


def _eye(n):
    return [[1.0 if i == j else 0.0 for j in _range(n)] for i in _range(n)]


def _matmul(a, b):
    n = len(a)
    m = len(b[0])
    k = len(b)
    out = []
    for i in _range(n):
        row = []
        ai = a[i]
        for j in _range(m):
            s = 0.0
            for t in _range(k):
                s += ai[t] * b[t][j]
            row.append(s)
        out.append(row)
    return out


def _transpose(a):
    return [list(r) for r in zip(*a)]


def _lu(a):
    """partial pivoting LU: returns (lu, perm, sign) or None when singular"""
    n = len(a)
    lu = [row[:] for row in a]
    perm = list(_range(n))
    sign = 1.0
    for k in _range(n):
        p = k
        big = abs(lu[k][k])
        for i in _range(k + 1, n):
            if abs(lu[i][k]) > big:
                big = abs(lu[i][k])
                p = i
        if big == 0.0:
            return None, perm, 0.0
        if p != k:
            lu[k], lu[p] = lu[p], lu[k]
            perm[k], perm[p] = perm[p], perm[k]
            sign = -sign
        piv = lu[k][k]
        for i in _range(k + 1, n):
            f = lu[i][k] / piv
            lu[i][k] = f
            if f != 0.0:
                for j in _range(k + 1, n):
                    lu[i][j] -= f * lu[k][j]
    return lu, perm, sign


def det(a):
    m = _square(a)
    lu, perm, sign = _lu(m)
    if lu is None:
        return 0.0
    d = sign
    for i in _range(len(m)):
        d *= lu[i][i]
    return d


def slogdet(a):
    d = det(a)
    if d == 0.0:
        return (0.0, -math.inf)
    return (1.0 if d > 0 else -1.0, math.log(abs(d)))


def _lu_solve(lu, perm, b):
    """solve for the columns of b (list of rows)"""
    n = len(lu)
    m = len(b[0])
    x = [b[perm[i]][:] for i in _range(n)]
    for i in _range(n):
        for k in _range(i):
            f = lu[i][k]
            if f != 0.0:
                for j in _range(m):
                    x[i][j] -= f * x[k][j]
    for i in _range(n - 1, -1, -1):
        for k in _range(i + 1, n):
            f = lu[i][k]
            if f != 0.0:
                for j in _range(m):
                    x[i][j] -= f * x[k][j]
        piv = lu[i][i]
        for j in _range(m):
            x[i][j] /= piv
    return x


def solve(a, b):
    m = _square(a)
    bb = np.asarray(b)
    lu, perm, sign = _lu(m)
    if lu is None:
        raise LinAlgError("Singular matrix")
    if bb.ndim == 1:
        x = _lu_solve(lu, perm, _mat(bb))
        return np.array([r[0] for r in x])
    return np.array(_lu_solve(lu, perm, _mat(bb)))


def inv(a):
    m = _square(a)
    lu, perm, sign = _lu(m)
    if lu is None:
        raise LinAlgError("Singular matrix")
    return np.array(_lu_solve(lu, perm, _eye(len(m))))


def matrix_power(a, n):
    a = np.asarray(a)
    if n < 0:
        a = inv(a)
        n = -n
    result = np.eye(a.shape[0])
    if str(a.dtype).startswith('int'):
        result = result.astype(int)
    base = a
    while n > 0:
        if n % 2 == 1:
            result = result @ base
        base = base @ base
        n //= 2
    return result


def cholesky(a):
    m = _square(a)
    n = len(m)
    L = [[0.0] * n for _ in _range(n)]
    for i in _range(n):
        for j in _range(i + 1):
            s = m[i][j]
            for k in _range(j):
                s -= L[i][k] * L[j][k]
            if i == j:
                if s <= 0.0:
                    raise LinAlgError("Matrix is not positive definite")
                L[i][j] = math.sqrt(s)
            else:
                L[i][j] = s / L[j][j]
    return np.array(L)


def _jacobi(m):
    """symmetric eigenproblem: (eigenvalues ascending, eigenvector columns)"""
    n = len(m)
    a = [row[:] for row in m]
    v = _eye(n)
    for sweep in _range(100):
        off = 0.0
        for i in _range(n):
            for j in _range(i + 1, n):
                off += a[i][j] * a[i][j]
        if off < 1e-30:
            break
        for p in _range(n - 1):
            for q in _range(p + 1, n):
                if abs(a[p][q]) < 1e-300:
                    continue
                theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q])
                if theta >= 0:
                    t = 1.0 / (theta + math.sqrt(1.0 + theta * theta))
                else:
                    t = -1.0 / (-theta + math.sqrt(1.0 + theta * theta))
                c = 1.0 / math.sqrt(1.0 + t * t)
                s = t * c
                for k in _range(n):
                    akp = a[k][p]
                    akq = a[k][q]
                    a[k][p] = c * akp - s * akq
                    a[k][q] = s * akp + c * akq
                for k in _range(n):
                    apk = a[p][k]
                    aqk = a[q][k]
                    a[p][k] = c * apk - s * aqk
                    a[q][k] = s * apk + c * aqk
                for k in _range(n):
                    vkp = v[k][p]
                    vkq = v[k][q]
                    v[k][p] = c * vkp - s * vkq
                    v[k][q] = s * vkp + c * vkq
    vals = [a[i][i] for i in _range(n)]
    order = sorted(_range(n), key=lambda i: vals[i])
    w = [vals[i] for i in order]
    vec = [[v[r][i] for i in order] for r in _range(n)]
    return w, vec


def eigh(a, UPLO='L'):
    m = _square(a)
    n = len(m)
    sym = [[m[i][j] if (i >= j) == (UPLO == 'L') or i == j else m[j][i] for j in _range(n)] for i in _range(n)]
    w, vec = _jacobi(sym)
    return np.array(w), np.array(vec)


def eigvalsh(a, UPLO='L'):
    return eigh(a, UPLO)[0]


def _sgn(x):
    return -1.0 if math.copysign(1.0, x) < 0 else 1.0


def _eig2(a, b, c, d):
    """eigenvalues of [[a, b], [c, d]] as LAPACK dlanv2 orders them (floats or complex)"""
    eps = 2.220446049250313e-16
    if c == 0.0:
        return (a, d)
    if b == 0.0:
        return (d, a)
    if (a - d) == 0.0 and _sgn(b) != _sgn(c):
        w = math.sqrt(abs(b)) * math.sqrt(abs(c))
        return (complex(a, w), complex(d, -w))
    temp = a - d
    p = 0.5 * temp
    bcmax = max(abs(b), abs(c))
    bcmis = min(abs(b), abs(c)) * _sgn(b) * _sgn(c)
    scale = max(abs(p), bcmax)
    z = p / scale * p + bcmax / scale * bcmis
    if z >= 4.0 * eps:
        z = p + math.copysign(math.sqrt(scale) * math.sqrt(z), p)
        return (d + z, d - bcmax / z * bcmis)
    sigma = b + c
    tau = math.hypot(sigma, temp)
    cs = math.sqrt(0.5 * (1.0 + abs(sigma) / tau))
    sn = -(p / (tau * cs)) * _sgn(sigma)
    aa = a * cs + b * sn
    bb = -a * sn + b * cs
    cc = c * cs + d * sn
    dd = -c * sn + d * cs
    a2 = aa * cs + cc * sn
    b2 = bb * cs + dd * sn
    c2 = -aa * sn + cc * cs
    d2 = -bb * sn + dd * cs
    temp = 0.5 * (a2 + d2)
    if c2 != 0.0:
        if b2 != 0.0:
            if _sgn(b2) == _sgn(c2):
                sab = math.sqrt(abs(b2))
                sac = math.sqrt(abs(c2))
                pp = math.copysign(sab * sac, c2)
                return (temp + pp, temp - pp)
            w = math.sqrt(abs(b2)) * math.sqrt(abs(c2))
            return (complex(temp, w), complex(temp, -w))
        return (temp, temp)
    return (temp, temp)


def _qr_eigs(m):
    """eigenvalues of a general real matrix by shifted QR iteration (floats, complex for conjugate pairs)"""
    n = len(m)
    a = [row[:] for row in m]
    ev = [0.0] * n
    hi = n
    it = 0
    while hi > 0:
        if hi == 1:
            ev[0] = a[0][0]
            break
        # look for a negligible sub-diagonal element
        l = hi - 1
        while l > 0:
            if abs(a[l][l - 1]) < 1e-14 * (abs(a[l][l]) + abs(a[l - 1][l - 1]) + 1e-300):
                break
            l -= 1
        if l == hi - 1:
            ev[hi - 1] = a[hi - 1][hi - 1]
            hi -= 1
            a = [row[:hi] for row in a[:hi]]
            it = 0
            continue
        if l == hi - 2:
            e1, e2 = _eig2(a[hi - 2][hi - 2], a[hi - 2][hi - 1], a[hi - 1][hi - 2], a[hi - 1][hi - 1])
            ev[hi - 2] = e1
            ev[hi - 1] = e2
            hi -= 2
            a = [row[:hi] for row in a[:hi]]
            it = 0
            continue
        it += 1
        if it > 500:
            for i in _range(hi):
                ev[i] = a[i][i]
            break
        mu = a[hi - 1][hi - 1]
        if it % 11 == 10:
            mu += abs(a[hi - 1][hi - 2])
        for i in _range(hi):
            a[i][i] -= mu
        q, r = _qr_house(a, hi, hi)
        a = _matmul(r, q)
        for i in _range(hi):
            a[i][i] += mu
    return ev


def _any_complex(w):
    for v in w:
        if isinstance(v, complex):
            return True
    return False


def _csolve(m, lam, x):
    """solve (m - lam I) y = x for complex y (Gaussian elimination, partial pivoting)"""
    n = len(m)
    a = [[complex(m[i][j]) - (lam if i == j else 0.0) for j in _range(n)] + [x[i]] for i in _range(n)]
    for k in _range(n):
        piv = k
        for i in _range(k + 1, n):
            if abs(a[i][k]) > abs(a[piv][k]):
                piv = i
        a[k], a[piv] = a[piv], a[k]
        if abs(a[k][k]) < 1e-300:
            a[k][k] = complex(1e-300)
        for i in _range(k + 1, n):
            f = a[i][k] / a[k][k]
            if f != 0:
                for j in _range(k, n + 1):
                    a[i][j] = a[i][j] - f * a[k][j]
    y = [complex(0)] * n
    for i in _range(n - 1, -1, -1):
        s = a[i][n]
        for j in _range(i + 1, n):
            s = s - a[i][j] * y[j]
        y[i] = s / a[i][i]
    return y


def _cvec(m, lam):
    n = len(m)
    lam2 = lam + complex(1e-10 * (1.0 + abs(lam)), 0.0)
    x = [complex(1.0)] * n
    for _ in _range(4):
        x = _csolve(m, lam2, x)
        nr = math.sqrt(sum(abs(v) * abs(v) for v in x))
        x = [v / nr for v in x]
    big = 0
    for i in _range(n):
        if abs(x[i]) > abs(x[big]):
            big = i
    ph = x[big] / abs(x[big])
    x = [v / ph for v in x]
    x = [complex(v.real, 0.0) if i == big else v for i, v in enumerate(x)]
    return x


def eigvals(a):
    m = _square(a)
    n = len(m)
    sym = True
    for i in _range(n):
        for j in _range(i):
            if abs(m[i][j] - m[j][i]) > 1e-12 * (abs(m[i][j]) + abs(m[j][i]) + 1e-300):
                sym = False
    if sym:
        return eigh(a)[0]
    w = _qr_eigs(m)
    if _any_complex(w):
        return np.array(w)
    if n == 2:
        return np.array(w)
    return np.array(sorted(w))


def eig(a):
    m = _square(a)
    n = len(m)
    sym = True
    for i in _range(n):
        for j in _range(i):
            if abs(m[i][j] - m[j][i]) > 1e-12 * (abs(m[i][j]) + abs(m[j][i]) + 1e-300):
                sym = False
    if sym:
        return eigh(a)
    w = _qr_eigs(m)
    if _any_complex(w):
        ccols = []
        for lam in w:
            ccols.append(_cvec(m, complex(lam)))
        return np.array(w), np.array(_transpose(ccols))
    w = sorted(w)
    cols = []
    for lam in w:
        shifted = [row[:] for row in m]
        for i in _range(n):
            shifted[i][i] -= lam + 1e-10 * (1.0 + abs(lam))
        lu, perm, sign = _lu(shifted)
        if lu is None:
            vec = [1.0] + [0.0] * (n - 1)
        else:
            x = [[1.0] for _ in _range(n)]
            for _ in _range(3):
                x = _lu_solve(lu, perm, x)
                nr = math.sqrt(sum(r[0] * r[0] for r in x))
                x = [[r[0] / nr] for r in x]
            vec = [r[0] for r in x]
        cols.append(vec)
    return np.array(w), np.array(_transpose(cols))


def _qr_house(a, m, n):
    """Householder QR of the m x n list matrix a: returns (Q m x m, R m x n)"""
    r = [row[:] for row in a]
    q = _eye(m)
    for k in _range(min(m - 1, n)):
        nrm = 0.0
        for i in _range(k, m):
            nrm += r[i][k] * r[i][k]
        nrm = math.sqrt(nrm)
        if nrm == 0.0:
            continue
        alpha = -nrm if r[k][k] >= 0 else nrm
        v = [0.0] * m
        for i in _range(k, m):
            v[i] = r[i][k]
        v[k] -= alpha
        vn = 0.0
        for i in _range(k, m):
            vn += v[i] * v[i]
        if vn == 0.0:
            continue
        for j in _range(n):
            s = 0.0
            for i in _range(k, m):
                s += v[i] * r[i][j]
            s = 2.0 * s / vn
            for i in _range(k, m):
                r[i][j] -= s * v[i]
        for j in _range(m):
            s = 0.0
            for i in _range(k, m):
                s += v[i] * q[j][i]
            s = 2.0 * s / vn
            for i in _range(k, m):
                q[j][i] -= s * v[i]
    for i in _range(m):
        for j in _range(min(i, n)):
            r[i][j] = 0.0
    return q, r


def qr(a, mode='reduced'):
    a = np.asarray(a)
    m, n = a.shape
    q, r = _qr_house(_mat(a), m, n)
    k = min(m, n)
    if mode == 'complete':
        return np.array(q), np.array(r)
    if mode == 'r':
        return np.array(r[:k])
    return np.array([row[:k] for row in q]), np.array(r[:k])


def _svd_core(rows, m, n):
    """one-sided Jacobi on an m x n matrix with m >= n: (U m x n, s, V n x n)"""
    u = [row[:] for row in rows]
    v = _eye(n)
    for sweep in _range(60):
        rotated = False
        for p in _range(n - 1):
            for q in _range(p + 1, n):
                alpha = 0.0
                beta = 0.0
                gamma = 0.0
                for i in _range(m):
                    alpha += u[i][p] * u[i][p]
                    beta += u[i][q] * u[i][q]
                    gamma += u[i][p] * u[i][q]
                if gamma == 0.0 or abs(gamma) <= 1e-15 * math.sqrt(alpha * beta):
                    continue
                rotated = True
                zeta = (beta - alpha) / (2.0 * gamma)
                if zeta >= 0:
                    t = 1.0 / (zeta + math.sqrt(1.0 + zeta * zeta))
                else:
                    t = -1.0 / (-zeta + math.sqrt(1.0 + zeta * zeta))
                c = 1.0 / math.sqrt(1.0 + t * t)
                s = c * t
                for i in _range(m):
                    up = u[i][p]
                    uq = u[i][q]
                    u[i][p] = c * up - s * uq
                    u[i][q] = s * up + c * uq
                for i in _range(n):
                    vp = v[i][p]
                    vq = v[i][q]
                    v[i][p] = c * vp - s * vq
                    v[i][q] = s * vp + c * vq
        if not rotated:
            break
    sv = []
    for j in _range(n):
        s = 0.0
        for i in _range(m):
            s += u[i][j] * u[i][j]
        sv.append(math.sqrt(s))
    order = sorted(_range(n), key=lambda j: -sv[j])
    s_sorted = [sv[j] for j in order]
    uu = [[u[i][j] / sv[j] if sv[j] > 1e-300 else 0.0 for j in order] for i in _range(m)]
    vv = [[v[i][j] for j in order] for i in _range(n)]
    return uu, s_sorted, vv


def _complete_basis(cols, m, want):
    """extend orthonormal columns (list of vectors) to `want` vectors of length m"""
    basis = [c[:] for c in cols]
    for e in _range(m):
        if len(basis) >= want:
            break
        cand = [0.0] * m
        cand[e] = 1.0
        for _ in _range(2):
            for b in basis:
                d = 0.0
                for i in _range(m):
                    d += cand[i] * b[i]
                for i in _range(m):
                    cand[i] -= d * b[i]
        nr = math.sqrt(sum(x * x for x in cand))
        if nr > 1e-8:
            basis.append([x / nr for x in cand])
    return basis


def svd(a, full_matrices=True, compute_uv=True):
    a = np.asarray(a)
    m, n = a.shape
    rows = _mat(a)
    flip = m < n
    if flip:
        rows = _transpose(rows)
        m, n = n, m
    uu, s, vv = _svd_core(rows, m, n)
    if not compute_uv:
        return np.array(s)
    ucols = [[uu[i][j] for i in _range(m)] for j in _range(n)]
    good = [j for j in _range(n) if s[j] > 1e-300]
    ucols_ok = [ucols[j] for j in good]
    if len(good) < n:
        ucols_ok = _complete_basis(ucols_ok, m, n)
        ucols = ucols_ok[:n]
    if full_matrices:
        ucols_full = _complete_basis(ucols, m, m)
    else:
        ucols_full = ucols
    U = _transpose(ucols_full)
    Vt = _transpose(vv)
    if flip:
        # a = (U s Vt)^T = V s U^T
        Ufull = _transpose(Vt)
        if full_matrices:
            return np.array(Ufull), np.array(s), np.array(_transpose(U))
        return np.array(Ufull), np.array(s), np.array(_transpose(U))
    return np.array(U), np.array(s), np.array(Vt)


def matrix_rank(a, tol=None):
    a = np.asarray(a)
    if a.ndim < 2:
        return int(any(v != 0 for v in a.flatten().tolist()))
    s = svd(a, compute_uv=False).tolist()
    if not s:
        return 0
    if tol is None:
        tol = max(s) * max(a.shape) * 2.220446049250313e-16
    return sum(1 for v in s if v > tol)


def pinv(a, rcond=1e-15):
    a = np.asarray(a)
    if a.ndim == 1:
        a = a.reshape([1, a.shape[0]])
    U, s, Vt = svd(a, full_matrices=False)
    sl = s.tolist()
    cut = rcond * max(sl) if sl else 0.0
    ut = _transpose(U.tolist())
    v = _transpose(Vt.tolist())
    k = len(sl)
    scaled = [[v[i][j] / sl[j] if sl[j] > cut else 0.0 for j in _range(k)] for i in _range(len(v))]
    return np.array(_matmul(scaled, ut))


def lstsq(a, b, rcond=None):
    a = np.asarray(a)
    b = np.asarray(b)
    m, n = a.shape
    U, s, Vt = svd(a, full_matrices=False)
    sl = s.tolist()
    if rcond is None:
        rcond = 2.220446049250313e-16 * max(m, n)
    cut = rcond * (max(sl) if sl else 0.0)
    rank = sum(1 for v in sl if v > cut)
    x = pinv(a, rcond=rcond) @ b
    if rank == n and m > n:
        r = a @ x - b
        if b.ndim == 1:
            res = np.array([float((r * r).sum())])
        else:
            res = (r * r).sum(axis=0)
    else:
        res = np.array([])
    return x, res, rank, s


def cond(a, p=None):
    s = svd(a, compute_uv=False).tolist()
    if p is None or p == 2:
        if min(s) == 0.0:
            return math.inf
        return max(s) / min(s)
    if p == -2:
        return min(s) / max(s)
    return norm(a, p) * norm(inv(a), p)


def norm(x, ord=None, axis=None, keepdims=False):
    x = np.asarray(x)
    if axis is None:
        if ord is None or (ord == 'fro' and x.ndim == 2) or (ord == 2 and x.ndim == 1):
            return math.sqrt(sum(v * v for v in x.flatten().tolist()))
        if x.ndim == 1:
            v = x.tolist()
            if ord == math.inf:
                return float(max(abs(t) for t in v))
            if ord == -math.inf:
                return float(min(abs(t) for t in v))
            if ord == 0:
                return float(sum(1 for t in v if t != 0))
            if ord == 1:
                return float(sum(abs(t) for t in v))
            return float(sum(abs(t) ** ord for t in v) ** (1.0 / ord))
        rows = x.tolist()
        if ord == 1:
            return float(max(sum(abs(rows[i][j]) for i in _range(len(rows))) for j in _range(len(rows[0]))))
        if ord == -1:
            return float(min(sum(abs(rows[i][j]) for i in _range(len(rows))) for j in _range(len(rows[0]))))
        if ord == math.inf:
            return float(max(sum(abs(t) for t in r) for r in rows))
        if ord == -math.inf:
            return float(min(sum(abs(t) for t in r) for r in rows))
        if ord == 2:
            return float(svd(x, compute_uv=False).tolist()[0])
        if ord == -2:
            return float(svd(x, compute_uv=False).tolist()[-1])
        if ord == 'nuc':
            return float(sum(svd(x, compute_uv=False).tolist()))
        raise ValueError("Invalid norm order for matrices.")
    rows = x.tolist()
    if x.ndim == 1:
        return norm(x, ord)
    ax = axis if axis >= 0 else axis + 2
    groups = rows if ax == 1 else [list(c) for c in zip(*rows)]
    out = [norm(np.array(g), ord) for g in groups]
    return np.array(out)


def multi_dot(arrays):
    r = np.asarray(arrays[0])
    for a in arrays[1:]:
        r = r @ np.asarray(a)
    return r


def trace(a):
    return np.trace(np.asarray(a))
