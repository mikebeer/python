# small dense linear algebra on lists of lists (fast enough for the tiny systems used by optimize/interpolate)
import math


def eye(n):
    return [[1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]


def transpose(a):
    if not a:
        return []
    return [[a[i][j] for i in range(len(a))] for j in range(len(a[0]))]


def matvec(a, x):
    out = []
    for row in a:
        s = 0.0
        for j in range(len(x)):
            s += row[j] * x[j]
        out.append(s)
    return out


def matmul(a, b):
    bt = transpose(b)
    out = []
    for row in a:
        r = []
        for col in bt:
            s = 0.0
            for k in range(len(col)):
                s += row[k] * col[k]
            r.append(s)
        out.append(r)
    return out


def dot(x, y):
    s = 0.0
    for i in range(len(x)):
        s += x[i] * y[i]
    return s


def norm2(x):
    return math.sqrt(dot(x, x))


def norm_inf(x):
    m = 0.0
    for v in x:
        if abs(v) > m:
            m = abs(v)
    return m


def solve(a, b):
    """solve a x = b (b a list or a list of rows); raises ValueError if singular"""
    n = len(a)
    vec = not isinstance(b[0], list) if b else True
    if vec:
        m = [list(a[i]) + [b[i]] for i in range(n)]
    else:
        m = [list(a[i]) + list(b[i]) for i in range(n)]
    for c in range(n):
        p = c
        best = abs(m[c][c])
        for r in range(c + 1, n):
            if abs(m[r][c]) > best:
                best = abs(m[r][c])
                p = r
        if best == 0.0:
            raise ValueError("singular matrix")
        if p != c:
            t = m[c]
            m[c] = m[p]
            m[p] = t
        piv = m[c][c]
        rowc = m[c]
        for r in range(c + 1, n):
            f = m[r][c] / piv
            if f != 0.0:
                rowr = m[r]
                for k in range(c, len(rowr)):
                    rowr[k] -= f * rowc[k]
    w = len(m[0])
    out = [[0.0] * (w - n) for i in range(n)]
    for col in range(w - n):
        for i in range(n - 1, -1, -1):
            s = m[i][n + col]
            for k in range(i + 1, n):
                s -= m[i][k] * out[k][col]
            out[i][col] = s / m[i][i]
    if vec:
        return [out[i][0] for i in range(n)]
    return out


def inv(a):
    return solve(a, eye(len(a)))


def det(a):
    n = len(a)
    m = [list(r) for r in a]
    d = 1.0
    for c in range(n):
        p = c
        for r in range(c + 1, n):
            if abs(m[r][c]) > abs(m[p][c]):
                p = r
        if m[p][c] == 0.0:
            return 0.0
        if p != c:
            t = m[c]
            m[c] = m[p]
            m[p] = t
            d = -d
        d *= m[c][c]
        for r in range(c + 1, n):
            f = m[r][c] / m[c][c]
            for k in range(c, n):
                m[r][k] -= f * m[c][k]
    return d
