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


def _fma(a, b, c):
    """a * b + c with one rounding (Dekker product, then one rounded sum)"""
    t = 134217729.0 * a
    ah = t - (t - a)
    al = a - ah
    t = 134217729.0 * b
    bh = t - (t - b)
    bl = b - bh
    p = a * b
    e = ((ah * bh - p) + ah * bl + al * bh) + al * bl
    s = p + c
    bb = s - p
    er = (p - (s - bb)) + (c - bb)
    return s + (er + e)


def _lu(a):
    """partial pivoting LU, left-looking with reciprocal pivot scaling and fused dot products
    in the order of OpenBLAS getf2 (same as the C interpreter), so exactly singular matrices
    such as [[1,2,3],[4,5,6],[7,8,9]] get an exactly zero pivot as in numpy:
    returns (lu, perm, sign), lu is None when singular"""
    n = len(a)
    lu = [[float(v) for v in row] for row in a]
    perm = list(_range(n))
    sign = 1.0
    for j in _range(n):
        nvec = (n - j) - (n - j) % 4
        for i in _range(j):
            s = 0.0
            for k in _range(i):
                s = _fma(lu[i][k], lu[k][j], s)
            lu[i][j] -= s
        for i in _range(j, n):
            s = 0.0
            if i - j >= nvec:
                for k in _range(j):
                    s = _fma(lu[i][k], lu[k][j], s)
                lu[i][j] -= s
            else:
                k = 0
                while k + 4 <= j:
                    t = 0.0
                    for q in _range(4):
                        t = _fma(lu[i][k + q], -lu[k + q][j], t)
                    s += t
                    k += 4
                while k < j:
                    s += lu[i][k] * -lu[k][j]
                    k += 1
                lu[i][j] += s
        p = j
        big = abs(lu[j][j])
        for i in _range(j + 1, n):
            if abs(lu[i][j]) > big:
                big = abs(lu[i][j])
                p = i
        if big == 0.0:
            return None, perm, 0.0
        if p != j:
            lu[j], lu[p] = lu[p], lu[j]
            perm[j], perm[p] = perm[p], perm[j]
            sign = -sign
        r = 1.0 / lu[j][j]
        for i in _range(j + 1, n):
            lu[i][j] *= r
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
        for k in _range(n - 1, i, -1):
            f = lu[i][k]
            if f != 0.0:
                for j in _range(m):
                    x[i][j] -= f * x[k][j]
        r = 1.0 / lu[i][i]
        for j in _range(m):
            x[i][j] *= r
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


def _lanv2(a, b, c, d):
    """LAPACK dlanv2: Schur factorisation of a real 2x2 block -> (a, b, c, d, rt1r, rt1i, rt2r, rt2i, cs, sn)"""
    eps = 2.220446049250313e-16
    if c == 0.0:
        cs = 1.0
        sn = 0.0
    elif b == 0.0:
        cs = 0.0
        sn = 1.0
        temp = d
        d = a
        a = temp
        b = -c
        c = 0.0
    elif (a - d) == 0.0 and _sgn(b) != _sgn(c):
        cs = 1.0
        sn = 0.0
    else:
        temp = a - d
        p = 0.5 * temp
        bcmax = max(abs(b), abs(c))
        bcmis = min(abs(b), abs(c)) * _sgn(b) * _sgn(c)
        scale = max(abs(p), bcmax)
        z = p / scale * p + bcmax / scale * bcmis
        if z >= 4.0 * eps:
            z = p + math.copysign(math.sqrt(scale) * math.sqrt(z), p)
            a = d + z
            d = d - bcmax / z * bcmis
            tau = _lapy2(c, z)
            cs = z / tau
            sn = c / tau
            b = b - c
            c = 0.0
        else:
            sigma = b + c
            tau = _lapy2(sigma, temp)
            cs = math.sqrt(0.5 * (1.0 + abs(sigma) / tau))
            sn = -(p / (tau * cs)) * _sgn(sigma)
            aa = a * cs + b * sn
            bb = -a * sn + b * cs
            cc = c * cs + d * sn
            dd = -c * sn + d * cs
            a = aa * cs + cc * sn
            b = bb * cs + dd * sn
            c = -aa * sn + cc * cs
            d = -bb * sn + dd * cs
            temp = 0.5 * (a + d)
            a = temp
            d = temp
            if c != 0.0:
                if b != 0.0:
                    if _sgn(b) == _sgn(c):
                        sab = math.sqrt(abs(b))
                        sac = math.sqrt(abs(c))
                        p = math.copysign(sab * sac, c)
                        tau = 1.0 / math.sqrt(abs(b + c))
                        a = temp + p
                        d = temp - p
                        b = b - c
                        c = 0.0
                        cs1 = sab * tau
                        sn1 = sac * tau
                        temp = cs * cs1 - sn * sn1
                        sn = cs * sn1 + sn * cs1
                        cs = temp
                else:
                    b = -c
                    c = 0.0
                    temp = cs
                    cs = -sn
                    sn = temp
    rt1r = a
    rt2r = d
    if c == 0.0:
        rt1i = 0.0
        rt2i = 0.0
    else:
        rt1i = math.sqrt(abs(b)) * math.sqrt(abs(c))
        rt2i = -rt1i
    return a, b, c, d, rt1r, rt1i, rt2r, rt2i, cs, sn


def _lapy2(x, y):
    """LAPACK dlapy2: w * sqrt(1 + (z/w)^2); this rounding (not math.hypot) is what dlahqr's
    sign-sensitive QR sweeps reproduce bit for bit"""
    w = abs(x)
    z = abs(y)
    if z > w:
        w, z = z, w
    if z == 0.0 or w > 1.797693134862315708e308:
        return w
    q = z / w
    return w * math.sqrt(1.0 + q * q)


def _larfg(alpha, x):
    """LAPACK dlarfg: returns (beta, tau, v) with H = I - tau [1 v][1 v]^T"""
    xn = 0.0
    for t in x:
        xn = math.hypot(xn, t)
    if xn == 0.0:
        return alpha, 0.0, x
    beta = -math.copysign(_lapy2(alpha, xn), alpha)
    tau = (beta - alpha) / beta
    sc = 1.0 / (alpha - beta)
    return beta, tau, [t * sc for t in x]


def _gebal(a, n):
    """LAPACK dgebal('B') on the list-of-rows a (in place) -> (ilo, ihi, scale), 0-based inclusive"""
    scale = [1.0] * n
    k = 0
    l = n - 1
    # rows that isolate an eigenvalue are pushed down
    noconv = True
    while noconv:
        noconv = False
        for j in _range(l, -1, -1):
            iso = True
            for i in _range(l + 1):
                if i != j and a[j][i] != 0.0:
                    iso = False
                    break
            if iso:
                scale[l] = float(j)
                if j != l:
                    for r in _range(l + 1):
                        a[r][j], a[r][l] = a[r][l], a[r][j]
                    for c in _range(k, n):
                        a[j][c], a[l][c] = a[l][c], a[j][c]
                noconv = True
                if l == 0:
                    return 0, 0, scale
                l -= 1
    noconv = True
    while noconv:
        noconv = False
        for j in _range(k, l + 1):
            iso = True
            for i in _range(k, l + 1):
                if i != j and a[i][j] != 0.0:
                    iso = False
                    break
            if iso:
                scale[k] = float(j)
                if j != k:
                    for r in _range(l + 1):
                        a[r][j], a[r][k] = a[r][k], a[r][j]
                    for c in _range(k, n):
                        a[j][c], a[k][c] = a[k][c], a[j][c]
                noconv = True
                k += 1
    for i in _range(k, l + 1):
        scale[i] = 1.0
    radix = 2.0
    sfmin1 = 2.2250738585072014e-308 / 2.220446049250313e-16
    sfmax1 = 1.0 / sfmin1
    sfmin2 = sfmin1 * radix
    sfmax2 = 1.0 / sfmin2
    noconv = True
    while noconv:
        noconv = False
        for i in _range(k, l + 1):
            c = 0.0
            r = 0.0
            for t in _range(k, l + 1):
                c = math.hypot(c, a[t][i])
                r = math.hypot(r, a[i][t])
            ca = 0.0
            for t in _range(l + 1):
                ca = max(ca, abs(a[t][i]))
            ra = 0.0
            for t in _range(k, n):
                ra = max(ra, abs(a[i][t]))
            if c == 0.0 or r == 0.0:
                continue
            g = r / radix
            f = 1.0
            s = c + r
            while c < g and max(f, c, ca) < sfmax2 and min(r, g, ra) > sfmin2:
                f *= radix
                c *= radix
                ca *= radix
                r /= radix
                g /= radix
                ra /= radix
            g = c / radix
            while g >= r and max(r, ra) < sfmax2 and min(f, c, g, ca) > sfmin2:
                f /= radix
                c /= radix
                g /= radix
                ca /= radix
                r *= radix
                ra *= radix
            if c + r >= 0.95 * s:
                continue
            if f < 1.0 and scale[i] < 1.0 and f * scale[i] <= sfmin1:
                continue
            if f > 1.0 and scale[i] > 1.0 and scale[i] >= sfmax1 / f:
                continue
            g = 1.0 / f
            scale[i] *= f
            noconv = True
            for t in _range(k, n):
                a[i][t] *= g
            for t in _range(l + 1):
                a[t][i] *= f
    return k, l, scale


def _gehrd(a, n, ilo, ihi):
    """Householder reduction to Hessenberg form (dgehd2); returns the reflectors [(i, tau, v)]"""
    refl = []
    for i in _range(ilo, ihi):
        alpha = a[i + 1][i]
        x = [a[r][i] for r in _range(i + 2, ihi + 1)]
        beta, tau, xs = _larfg(alpha, x)
        v = [1.0] + xs
        refl.append((i, tau, v))
        if tau != 0.0:
            m = len(v)
            for r in _range(ihi + 1):
                row = a[r]
                s = 0.0
                for c in _range(m):
                    s += row[i + 1 + c] * v[c]
                s *= tau
                for c in _range(m):
                    row[i + 1 + c] -= s * v[c]
            for c in _range(i + 1, n):
                s = 0.0
                for r in _range(m):
                    s += v[r] * a[i + 1 + r][c]
                s *= tau
                for r in _range(m):
                    a[i + 1 + r][c] -= s * v[r]
        a[i + 1][i] = beta
        for r in _range(i + 2, n):
            a[r][i] = 0.0
    return refl


def _orghr(n, refl):
    q = _eye(n)
    for t in _range(len(refl) - 1, -1, -1):
        i, tau, v = refl[t]
        if tau == 0.0:
            continue
        m = len(v)
        for c in _range(i + 1, n):
            s = 0.0
            for r in _range(m):
                s += v[r] * q[i + 1 + r][c]
            s *= tau
            for r in _range(m):
                q[i + 1 + r][c] -= s * v[r]
    return q


def _lahqr(h, z, n, ilo, ihi, wantt, wantz, wr, wi):
    """LAPACK dlahqr (double-shift QR on a Hessenberg matrix), 0-based. Returns True on convergence."""
    if ilo == ihi:
        wr[ilo] = h[ilo][ilo]
        wi[ilo] = 0.0
        return True
    for j in _range(ilo, ihi - 2):
        h[j + 2][j] = 0.0
        h[j + 3][j] = 0.0
    if ilo <= ihi - 2:
        h[ihi][ihi - 2] = 0.0
    nh = ihi - ilo + 1
    ulp = 2.220446049250313e-16
    smlnum = 2.2250738585072014e-308 * (nh / ulp)
    i1 = 0
    i2 = n - 1
    itmax = 30 * max(10, nh)
    kdefl = 0
    i = ihi
    while i >= ilo:
        l = ilo
        conv = False
        for its in _range(itmax + 1):
            k = i
            while k > l:
                hk = abs(h[k][k - 1])
                if hk <= smlnum:
                    break
                tst = abs(h[k - 1][k - 1]) + abs(h[k][k])
                if tst == 0.0:
                    if k - 2 >= ilo:
                        tst += abs(h[k - 1][k - 2])
                    if k + 1 <= ihi:
                        tst += abs(h[k + 1][k])
                if hk <= ulp * tst:
                    ab = max(hk, abs(h[k - 1][k]))
                    ba = min(hk, abs(h[k - 1][k]))
                    dd = abs(h[k - 1][k - 1] - h[k][k])
                    aa = max(abs(h[k][k]), dd)
                    bb = min(abs(h[k][k]), dd)
                    s = aa + ab
                    if ba * (ab / s) <= max(smlnum, ulp * (bb * (aa / s))):
                        break
                k -= 1
            l = k
            if l > ilo:
                h[l][l - 1] = 0.0
            if l >= i - 1:
                conv = True
                break
            kdefl += 1
            if not wantt:
                i1 = l
                i2 = i
            if kdefl % 20 == 0:
                s = abs(h[i][i - 1]) + abs(h[i - 1][i - 2])
                h11 = 0.75 * s + h[i][i]
                h12 = -0.4375 * s
                h21 = s
                h22 = h11
            elif kdefl % 10 == 0:
                s = abs(h[l + 1][l]) + abs(h[l + 2][l + 1])
                h11 = 0.75 * s + h[l][l]
                h12 = -0.4375 * s
                h21 = s
                h22 = h11
            else:
                h11 = h[i - 1][i - 1]
                h21 = h[i][i - 1]
                h12 = h[i - 1][i]
                h22 = h[i][i]
            s = abs(h11) + abs(h12) + abs(h21) + abs(h22)
            if s == 0.0:
                rt1r = rt1i = rt2r = rt2i = 0.0
            else:
                h11 /= s
                h21 /= s
                h12 /= s
                h22 /= s
                tr = (h11 + h22) / 2.0
                det = (h11 - tr) * (h22 - tr) - h12 * h21
                rtdisc = math.sqrt(abs(det))
                if det >= 0.0:
                    rt1r = tr * s
                    rt2r = rt1r
                    rt1i = rtdisc * s
                    rt2i = -rt1i
                else:
                    rt1r = tr + rtdisc
                    rt2r = tr - rtdisc
                    if abs(rt1r - h22) <= abs(rt2r - h22):
                        rt1r = rt1r * s
                        rt2r = rt1r
                    else:
                        rt2r = rt2r * s
                        rt1r = rt2r
                    rt1i = 0.0
                    rt2i = 0.0
            m = i - 2
            while True:
                h21s = h[m + 1][m]
                s = abs(h[m][m] - rt2r) + abs(rt2i) + abs(h21s)
                h21s = h[m + 1][m] / s
                v1 = h21s * h[m][m + 1] + (h[m][m] - rt1r) * ((h[m][m] - rt2r) / s) - rt1i * (rt2i / s)
                v2 = h21s * (h[m][m] + h[m + 1][m + 1] - rt1r - rt2r)
                v3 = h21s * h[m + 2][m + 1]
                s = abs(v1) + abs(v2) + abs(v3)
                v1 /= s
                v2 /= s
                v3 /= s
                if m == l:
                    break
                h00 = h[m - 1][m - 1]
                h10 = h[m][m - 1]
                h11 = h[m][m]
                h22 = h[m + 1][m + 1]
                if abs(h10) * (abs(v2) + abs(v3)) <= ulp * abs(v1) * (abs(h00) + abs(h11) + abs(h22)):
                    break
                m -= 1
            v = [v1, v2, v3]
            for k in _range(m, i):
                nr = min(3, i - k + 1)
                if k > m:
                    v = [h[k + t][k - 1] for t in _range(nr)]
                beta, t1, xs = _larfg(v[0], v[1:nr])
                v = [beta] + xs
                if k > m:
                    h[k][k - 1] = beta
                    h[k + 1][k - 1] = 0.0
                    if k < i - 1:
                        h[k + 2][k - 1] = 0.0
                elif m > l:
                    h[k][k - 1] = h[k][k - 1] * (1.0 - t1)
                vv2 = v[1]
                t2 = t1 * vv2
                if nr == 3:
                    vv3 = v[2]
                    t3 = t1 * vv3
                    rk = h[k]
                    rk1 = h[k + 1]
                    rk2 = h[k + 2]
                    for j in _range(k, i2 + 1):
                        sm = rk[j] + vv2 * rk1[j] + vv3 * rk2[j]
                        rk[j] -= sm * t1
                        rk1[j] -= sm * t2
                        rk2[j] -= sm * t3
                    for j in _range(i1, min(k + 3, i) + 1):
                        rj = h[j]
                        sm = rj[k] + vv2 * rj[k + 1] + vv3 * rj[k + 2]
                        rj[k] -= sm * t1
                        rj[k + 1] -= sm * t2
                        rj[k + 2] -= sm * t3
                    if wantz:
                        for j in _range(ilo, ihi + 1):
                            rj = z[j]
                            sm = rj[k] + vv2 * rj[k + 1] + vv3 * rj[k + 2]
                            rj[k] -= sm * t1
                            rj[k + 1] -= sm * t2
                            rj[k + 2] -= sm * t3
                elif nr == 2:
                    rk = h[k]
                    rk1 = h[k + 1]
                    for j in _range(k, i2 + 1):
                        sm = rk[j] + vv2 * rk1[j]
                        rk[j] -= sm * t1
                        rk1[j] -= sm * t2
                    for j in _range(i1, i + 1):
                        rj = h[j]
                        sm = rj[k] + vv2 * rj[k + 1]
                        rj[k] -= sm * t1
                        rj[k + 1] -= sm * t2
                    if wantz:
                        for j in _range(ilo, ihi + 1):
                            rj = z[j]
                            sm = rj[k] + vv2 * rj[k + 1]
                            rj[k] -= sm * t1
                            rj[k + 1] -= sm * t2
        if not conv:
            return False
        if l == i:
            wr[i] = h[i][i]
            wi[i] = 0.0
        else:
            a, b, c, d, r1r, r1i, r2r, r2i, cs, sn = _lanv2(h[i - 1][i - 1], h[i - 1][i], h[i][i - 1], h[i][i])
            h[i - 1][i - 1] = a
            h[i - 1][i] = b
            h[i][i - 1] = c
            h[i][i] = d
            wr[i - 1] = r1r
            wi[i - 1] = r1i
            wr[i] = r2r
            wi[i] = r2i
            if wantt:
                for j in _range(i + 1, i2 + 1):
                    x = h[i - 1][j]
                    y = h[i][j]
                    h[i - 1][j] = cs * x + sn * y
                    h[i][j] = cs * y - sn * x
                for j in _range(i1, i - 1):
                    x = h[j][i - 1]
                    y = h[j][i]
                    h[j][i - 1] = cs * x + sn * y
                    h[j][i] = cs * y - sn * x
            if wantz:
                for j in _range(ilo, ihi + 1):
                    x = z[j][i - 1]
                    y = z[j][i]
                    z[j][i - 1] = cs * x + sn * y
                    z[j][i] = cs * y - sn * x
        kdefl = 0
        i = l - 1
    return True


def _solve2(a11, a12, a21, a22, b1, b2, smin):
    """2x2 solve with pivoting (floats or complex)"""
    if abs(a21) > abs(a11):
        a11, a12, a21, a22, b1, b2 = a21, a22, a11, a12, b2, b1
    if abs(a11) < smin:
        a11 = smin
    f = a21 / a11
    d = a22 - f * a12
    if abs(d) < smin:
        d = smin
    x2 = (b2 - f * b1) / d
    x1 = (b1 - a12 * x2) / a11
    return x1, x2


def _trevc(t, vr, n):
    """right eigenvectors of the quasi-triangular t, back-transformed with the columns of vr (in place)"""
    ulp = 2.220446049250313e-16
    smlnum = 2.2250738585072014e-308 * (n / ulp)
    ki = n - 1
    while ki >= 0:
        pair = ki > 0 and t[ki][ki - 1] != 0.0
        if not pair:
            wr = t[ki][ki]
            smin = max(ulp * abs(wr), smlnum)
            w = [0.0] * n
            w[ki] = 1.0
            for k in _range(ki):
                w[k] = -t[k][ki]
            j = ki - 1
            while j >= 0:
                if j > 0 and t[j][j - 1] != 0.0:
                    x1, x2 = _solve2(t[j - 1][j - 1] - wr, t[j - 1][j], t[j][j - 1], t[j][j] - wr, w[j - 1], w[j], smin)
                    big = max(abs(x1), abs(x2))
                    if big > 1e100:
                        for k in _range(ki + 1):
                            w[k] /= big
                        x1 /= big
                        x2 /= big
                    w[j - 1] = x1
                    w[j] = x2
                    for k in _range(j - 1):
                        w[k] -= x1 * t[k][j - 1] + x2 * t[k][j]
                    j -= 2
                else:
                    den = t[j][j] - wr
                    if abs(den) < smin:
                        den = smin
                    x = w[j] / den
                    if abs(x) > 1e100:
                        big = abs(x)
                        for k in _range(ki + 1):
                            w[k] /= big
                        x /= big
                    w[j] = x
                    for k in _range(j):
                        w[k] -= x * t[k][j]
                    j -= 1
            for r in _range(n):
                row = vr[r]
                s = w[ki] * row[ki]
                for k in _range(ki):
                    s += row[k] * w[k]
                row[ki] = s
            emax = 0.0
            for r in _range(n):
                emax = max(emax, abs(vr[r][ki]))
            rm = 1.0 / emax
            for r in _range(n):
                vr[r][ki] *= rm
            ki -= 1
        else:
            wr = t[ki][ki]
            wi = math.sqrt(abs(t[ki][ki - 1])) * math.sqrt(abs(t[ki - 1][ki]))
            smin = max(ulp * (abs(wr) + abs(wi)), smlnum)
            w = [0j] * n
            if abs(t[ki - 1][ki]) >= abs(t[ki][ki - 1]):
                w[ki - 1] = complex(1.0, 0.0)
                w[ki] = complex(0.0, wi / t[ki - 1][ki])
            else:
                w[ki - 1] = complex(-wi / t[ki][ki - 1], 0.0)
                w[ki] = complex(0.0, 1.0)
            for k in _range(ki - 1):
                w[k] = complex(-w[ki - 1].real * t[k][ki - 1], -w[ki].imag * t[k][ki])
            lam = complex(wr, wi)
            j = ki - 2
            while j >= 0:
                if j > 0 and t[j][j - 1] != 0.0:
                    x1, x2 = _solve2(t[j - 1][j - 1] - lam, t[j - 1][j], t[j][j - 1], t[j][j] - lam, w[j - 1], w[j], smin)
                    big = max(abs(x1), abs(x2))
                    if big > 1e100:
                        for k in _range(ki + 1):
                            w[k] = w[k] / big
                        x1 = x1 / big
                        x2 = x2 / big
                    w[j - 1] = x1
                    w[j] = x2
                    for k in _range(j - 1):
                        w[k] = w[k] - x1 * t[k][j - 1] - x2 * t[k][j]
                    j -= 2
                else:
                    den = t[j][j] - lam
                    if abs(den) < smin:
                        den = complex(smin, 0.0)
                    x = w[j] / den
                    if abs(x) > 1e100:
                        big = abs(x)
                        for k in _range(ki + 1):
                            w[k] = w[k] / big
                        x = x / big
                    w[j] = x
                    for k in _range(j):
                        w[k] = w[k] - x * t[k][j]
                    j -= 1
            emax = 0.0
            for r in _range(n):
                row = vr[r]
                sre = w[ki - 1].real * row[ki - 1]
                sim = w[ki].imag * row[ki]
                for k in _range(ki - 1):
                    sre += row[k] * w[k].real
                    sim += row[k] * w[k].imag
                row[ki - 1] = sre
                row[ki] = sim
                emax = max(emax, abs(sre) + abs(sim))
            rm = 1.0 / emax
            for r in _range(n):
                vr[r][ki - 1] *= rm
                vr[r][ki] *= rm
            ki -= 2


def _geev(m, vectors):
    """LAPACK dgeev for a real square matrix (list of rows): (wr, wi, vr or None)"""
    n = len(m)
    a = [row[:] for row in m]
    wr = [0.0] * n
    wi = [0.0] * n
    if n == 0:
        return wr, wi, [] if vectors else None
    ilo, ihi, scale = _gebal(a, n)
    refl = _gehrd(a, n, ilo, ihi)
    z = None
    if vectors:
        z = _orghr(n, refl)
    for i in _range(n):
        if i < ilo or i > ihi:
            wr[i] = a[i][i]
            wi[i] = 0.0
    if not _lahqr(a, z, n, ilo, ihi, vectors, vectors, wr, wi):
        raise LinAlgError("Eigenvalues did not converge")
    if not vectors:
        return wr, wi, None
    for i in _range(n):
        for j in _range(i + 2, n):
            a[j][i] = 0.0
    vr = z
    _trevc(a, vr, n)
    for i in _range(ilo, ihi + 1):
        s = scale[i]
        if ilo != ihi and s != 1.0:
            vr[i] = [v * s for v in vr[i]]
    for ii in _range(n):
        i = ii
        if i >= ilo and i <= ihi:
            continue
        if i < ilo:
            i = ilo - 1 - ii
        k = int(scale[i])
        if k != i:
            vr[i], vr[k] = vr[k], vr[i]
    i = 0
    while i < n:
        if wi[i] == 0.0:
            nr = 0.0
            for r in _range(n):
                nr = math.hypot(nr, vr[r][i])
            sc = 1.0 / nr
            for r in _range(n):
                vr[r][i] *= sc
            i += 1
        else:
            n1 = 0.0
            n2 = 0.0
            for r in _range(n):
                n1 = math.hypot(n1, vr[r][i])
                n2 = math.hypot(n2, vr[r][i + 1])
            sc = 1.0 / _lapy2(n1, n2)
            big = 0
            bv = -1.0
            for r in _range(n):
                vr[r][i] *= sc
                vr[r][i + 1] *= sc
                q = vr[r][i] * vr[r][i] + vr[r][i + 1] * vr[r][i + 1]
                if q > bv:
                    bv = q
                    big = r
            f = vr[big][i]
            g = vr[big][i + 1]
            if g == 0.0:
                cs = 1.0
                sn = 0.0
            elif f == 0.0:
                cs = 0.0
                sn = _sgn(g)
            else:
                d = math.sqrt(f * f + g * g)
                cs = abs(f) / d
                sn = g / math.copysign(d, f)
            for r in _range(n):
                x = vr[r][i]
                y = vr[r][i + 1]
                vr[r][i] = cs * x + sn * y
                vr[r][i + 1] = cs * y - sn * x
            vr[big][i + 1] = 0.0
            i += 2
    return wr, wi, vr


def eigvals(a):
    m = _square(a)
    wr, wi, _v = _geev(m, False)
    if _any_nonreal(wi):
        return np.array([complex(wr[i], wi[i]) for i in _range(len(wr))])
    return np.array(wr)


def _any_nonreal(wi):
    for v in wi:
        if v != 0.0:
            return True
    return False


def eig(a):
    m = _square(a)
    n = len(m)
    wr, wi, vr = _geev(m, True)
    if not _any_nonreal(wi):
        return np.array(wr), np.array(vr)
    w = [complex(wr[i], wi[i]) for i in _range(n)]
    cols = [[0j] * n for _ in _range(n)]
    j = 0
    while j < n:
        if wi[j] == 0.0:
            for r in _range(n):
                cols[r][j] = complex(vr[r][j], 0.0)
            j += 1
        else:
            for r in _range(n):
                cols[r][j] = complex(vr[r][j], vr[r][j + 1])
                cols[r][j + 1] = complex(vr[r][j], -vr[r][j + 1])
            j += 2
    return np.array(w), np.array(cols)


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
