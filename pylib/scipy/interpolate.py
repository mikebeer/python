"""scipy.interpolate subset: interp1d (linear nearest previous next zero slinear cubic), CubicSpline, PPoly,
PchipInterpolator, CubicHermiteSpline, InterpolatedUnivariateSpline / UnivariateSpline (interpolating), lagrange,
BarycentricInterpolator, RegularGridInterpolator (linear / nearest)."""
import math
import numpy as np
from ._util import inf, nan, tolist
from . import _la


def _bisect_right(a, v):
    lo = 0
    hi = len(a)
    while lo < hi:
        mid = (lo + hi) // 2
        if v < a[mid]:
            hi = mid
        else:
            lo = mid + 1
    return lo


def _bisect_left(a, v):
    lo = 0
    hi = len(a)
    while lo < hi:
        mid = (lo + hi) // 2
        if a[mid] < v:
            lo = mid + 1
        else:
            hi = mid
    return lo


def _shape_out(vals, like):
    """vals: list of results for the flat inputs; like: original input"""
    if isinstance(like, (list, tuple, np.ndarray)):
        a = np.asarray(like)
        if a.ndim == 0:
            return vals[0]
        return np.array(vals).reshape(a.shape)
    return vals[0]


def _is_scalar(x):
    return not isinstance(x, (list, tuple, np.ndarray)) or (isinstance(x, np.ndarray) and x.ndim == 0)


# ---------------------------------------------------------------------------
# piecewise polynomials
# ---------------------------------------------------------------------------

def _poly_real_roots(co):
    """real roots of sum co[j] t**(k-1-j) (degree <= 3), sorted"""
    co = list(co)
    while len(co) > 1 and co[0] == 0.0:
        co = co[1:]
    deg = len(co) - 1
    if deg <= 0:
        return []
    if deg == 1:
        return [-co[1] / co[0]]
    if deg == 2:
        a, b, c = co
        disc = b * b - 4.0 * a * c
        if disc < 0:
            return []
        sq = math.sqrt(disc)
        q = -0.5 * (b + (sq if b >= 0 else -sq))
        r = []
        if q != 0.0:
            r.append(q / a)
            r.append(c / q)
        else:
            r.append(0.0)
        return sorted(set(r))
    if deg > 3:
        raise NotImplementedError("roots of piecewise polynomials of degree > 3")
    a, b, c, d = co
    b /= a
    c /= a
    d /= a
    q = (b * b - 3.0 * c) / 9.0
    r = (2.0 * b ** 3 - 9.0 * b * c + 27.0 * d) / 54.0
    rts = []
    q3 = q * q * q
    if r * r < q3:
        th = math.acos(max(-1.0, min(1.0, r / math.sqrt(q3))))
        sq = -2.0 * math.sqrt(q)
        rts = [sq * math.cos(th / 3.0) - b / 3.0, sq * math.cos((th + 2.0 * math.pi) / 3.0) - b / 3.0,
               sq * math.cos((th - 2.0 * math.pi) / 3.0) - b / 3.0]
    else:
        aa = -(1.0 if r >= 0 else -1.0) * (abs(r) + math.sqrt(r * r - q3)) ** (1.0 / 3.0)
        bb = q / aa if aa != 0.0 else 0.0
        rts = [aa + bb - b / 3.0]
    out = []
    for x in rts:
        for it in range(3):
            f = ((x + b) * x + c) * x + d
            fp = (3.0 * x + 2.0 * b) * x + c
            if fp == 0.0:
                break
            x = x - f / fp
        out.append(x)
    out.sort()
    ded = []
    for x in out:
        if not ded or abs(ded[-1] - x) > 1e-12 * max(1.0, abs(x)):
            ded.append(x)
    return ded


class PPoly:
    """piecewise polynomial; c[j][i] is the coefficient of (x - x_i)**(k-1-j) on [x_i, x_{i+1}]"""

    def __init__(self, c, x, extrapolate=None, axis=0):
        self.c = np.array(c) if not isinstance(c, np.ndarray) else c
        self.x = np.array(x) if not isinstance(x, np.ndarray) else x
        self._c = [[float(v) for v in r] for r in self.c.tolist()]
        self._x = tolist(self.x)
        self.extrapolate = True if extrapolate is None else extrapolate

    def _eval1(self, xv, nu, extrapolate):
        xs = self._x
        n = len(xs)
        if xv != xv:
            return nan
        if extrapolate is False or extrapolate is None and not self.extrapolate:
            if xv < xs[0] or xv > xs[-1]:
                return nan
        if extrapolate == "periodic":
            period = xs[-1] - xs[0]
            xv = xs[0] + (xv - xs[0]) % period
        i = _bisect_right(xs, xv) - 1
        if i < 0:
            i = 0
        if i > n - 2:
            i = n - 2
        c = self._c
        k = len(c)
        dx = xv - xs[i]
        if nu == 0:
            res = 0.0
            for j in range(k):
                res = res * dx + c[j][i]
            return res
        # derivative of order nu
        res = 0.0
        for j in range(k - nu):
            m = k - 1 - j
            fac = 1.0
            for q in range(nu):
                fac *= (m - q)
            res = res * dx + fac * c[j][i]
        return res

    def __call__(self, x, nu=0, extrapolate=None):
        if _is_scalar(x):
            return self._eval1(float(x), nu, extrapolate)
        xl = tolist(x)
        return _shape_out([self._eval1(v, nu, extrapolate) for v in xl], x)

    def derivative(self, nu=1):
        c = self._c
        k = len(c)
        cols = len(c[0])
        for step in range(nu):
            k = len(c)
            if k == 1:
                c = [[0.0] * cols]
            else:
                c = [[c[j][i] * (k - 1 - j) for i in range(cols)] for j in range(k - 1)]
        return PPoly(c, self._x, self.extrapolate)

    def antiderivative(self, nu=1):
        c = self._c
        xs = self._x
        cols = len(c[0])
        for step in range(nu):
            k = len(c)
            new = [[c[j][i] / (k - j) for i in range(cols)] for j in range(k)] + [[0.0] * cols]
            # make continuous
            acc = 0.0
            kk = k + 1
            for i in range(cols):
                new[kk - 1][i] = acc
                h = xs[i + 1] - xs[i]
                v = 0.0
                for j in range(kk):
                    v = v * h + new[j][i]
                acc = v
            c = new
        return PPoly(c, xs, self.extrapolate)

    def integrate(self, a, b, extrapolate=None):
        anti = self.antiderivative()
        return anti(b, extrapolate=True) - anti(a, extrapolate=True)

    def roots(self, discontinuity=True, extrapolate=None):
        xs = self._x
        ex = self.extrapolate if extrapolate is None else extrapolate
        out = []
        k = len(self._c)
        m = len(xs) - 1
        for i in range(m):
            co = [self._c[j][i] for j in range(k)]
            lo = xs[i]
            hi = xs[i + 1]
            if ex is True:
                if i == 0:
                    lo = -inf
                if i == m - 1:
                    hi = inf
            rts = _poly_real_roots(co)
            for r in rts:
                t = xs[i] + r
                if t >= lo and (t < hi or (i == m - 1 and t <= hi)):
                    if not out or abs(out[-1] - t) > 1e-13 * max(1.0, abs(t)):
                        out.append(t)
        out.sort()
        return np.array(out)

    def solve(self, y=0.0, discontinuity=True, extrapolate=None):
        if y == 0.0:
            return self.roots()
        c = [list(r) for r in self._c]
        c[-1] = [v - y for v in c[-1]]
        return PPoly(c, self._x, self.extrapolate).roots()


def _hermite_coeffs(xs, ys, ss):
    n = len(xs)
    c0 = []
    c1 = []
    c2 = []
    c3 = []
    for i in range(n - 1):
        dx = xs[i + 1] - xs[i]
        slope = (ys[i + 1] - ys[i]) / dx
        t = (ss[i] + ss[i + 1] - 2.0 * slope) / dx
        c0.append(t / dx)
        c1.append((slope - ss[i]) / dx - t)
        c2.append(ss[i])
        c3.append(ys[i])
    return [c0, c1, c2, c3]


def _solve_gtsv(dl, d, du, b):
    """tridiagonal solve with partial pivoting (LAPACK dgtsv); modifies copies"""
    n = len(d)
    dl = list(dl)
    d = list(d)
    du = list(du) + [0.0]
    b = list(b)
    for i in range(n - 1):
        if abs(d[i]) >= abs(dl[i]):
            if d[i] == 0.0:
                raise ValueError("singular matrix")
            fact = dl[i] / d[i]
            d[i + 1] = d[i + 1] - fact * du[i]
            b[i + 1] = b[i + 1] - fact * b[i]
            dl[i] = 0.0
        else:
            fact = d[i] / dl[i]
            d[i] = dl[i]
            temp = d[i + 1]
            d[i + 1] = du[i] - fact * temp
            if i < n - 2:
                dl[i] = du[i + 1]
                du[i + 1] = -fact * dl[i]
            du[i] = temp
            temp = b[i]
            b[i] = b[i + 1]
            b[i + 1] = temp - fact * b[i + 1]
    if d[n - 1] == 0.0:
        raise ValueError("singular matrix")
    b[n - 1] = b[n - 1] / d[n - 1]
    if n > 1:
        b[n - 2] = (b[n - 2] - du[n - 2] * b[n - 1]) / d[n - 2]
    for i in range(n - 3, -1, -1):
        b[i] = (b[i] - du[i] * b[i + 1] - dl[i] * b[i + 2]) / d[i]
    return b


def _spline_slopes(xs, ys, bc):
    n = len(xs)
    dx = [xs[i + 1] - xs[i] for i in range(n - 1)]
    slope = [(ys[i + 1] - ys[i]) / dx[i] for i in range(n - 1)]
    if n == 2:
        if bc[0] in ("not-a-knot",) and bc[1] in ("not-a-knot",):
            return [slope[0], slope[0]]
        if bc[0] == "periodic":
            return [0.0, 0.0]
    if bc[0] == "periodic":
        # cyclic system for n-1 unknowns
        m = n - 1
        A = [[0.0] * m for r in range(m)]
        rhs = [0.0] * m
        for i in range(m):
            im = (i - 1) % m
            ip = (i + 1) % m
            dxl = dx[im]
            dxr = dx[i]
            A[i][im] += dxr
            A[i][i] += 2.0 * (dxl + dxr)
            A[i][ip] += dxl
            rhs[i] = 3.0 * (dxr * slope[im] + dxl * slope[i])
        s = _la.solve(A, rhs)
        return s + [s[0]]
    nn = n
    if nn == 3 and bc[0] == "not-a-knot" and bc[1] == "not-a-knot":
        m0 = slope[0]
        a = (slope[1] - slope[0]) / (xs[2] - xs[0])
        return [m0 - a * dx[0], m0 + a * dx[0], m0 + a * (dx[0] + 2.0 * dx[1])]
    dl = [0.0] * (n - 1)
    d = [0.0] * n
    du = [0.0] * (n - 1)
    b = [0.0] * n
    for i in range(1, n - 1):
        dl[i - 1] = dx[i]
        d[i] = 2.0 * (dx[i - 1] + dx[i])
        du[i] = dx[i - 1]
        b[i] = 3.0 * (dx[i] * slope[i - 1] + dx[i - 1] * slope[i])
    left = bc[0]
    right = bc[1]
    if left == "not-a-knot":
        dd = xs[2] - xs[0]
        d[0] = dx[1]
        du[0] = dd
        b[0] = ((dx[0] + 2.0 * dd) * dx[1] * slope[0] + dx[0] * dx[0] * slope[1]) / dd
    elif left == "natural":
        d[0] = 2.0
        du[0] = 1.0
        b[0] = 3.0 * slope[0]
    elif left[0] == 1:
        d[0] = 1.0
        du[0] = 0.0
        b[0] = left[1]
    else:
        d[0] = 2.0
        du[0] = 1.0
        b[0] = 3.0 * slope[0] - 0.5 * dx[0] * left[1]
    if right == "not-a-knot":
        dd = xs[n - 1] - xs[n - 3]
        d[n - 1] = dx[n - 3]
        dl[n - 2] = dd
        b[n - 1] = (dx[n - 2] * dx[n - 2] * slope[n - 3] + (2.0 * dd + dx[n - 2]) * dx[n - 3] * slope[n - 2]) / dd
    elif right == "natural":
        d[n - 1] = 2.0
        dl[n - 2] = 1.0
        b[n - 1] = 3.0 * slope[n - 2]
    elif right[0] == 1:
        d[n - 1] = 1.0
        dl[n - 2] = 0.0
        b[n - 1] = right[1]
    else:
        d[n - 1] = 2.0
        dl[n - 2] = 1.0
        b[n - 1] = 3.0 * slope[n - 2] + 0.5 * dx[n - 2] * right[1]
    return _solve_gtsv(dl, d, du, b)


def _norm_bc(bc_type):
    def one(b, side):
        if isinstance(b, str):
            if b == "clamped":
                return (1, 0.0)
            if b in ("natural", "not-a-knot", "periodic"):
                return b
            raise ValueError("bc_type is not valid")
        o, v = b
        return (int(o), float(v))
    if isinstance(bc_type, str):
        if bc_type == "periodic":
            return ("periodic", "periodic")
        return (one(bc_type, 0), one(bc_type, 1))
    return (one(bc_type[0], 0), one(bc_type[1], 1))


class CubicSpline(PPoly):
    def __init__(self, x, y, axis=0, bc_type="not-a-knot", extrapolate=None):
        xs = tolist(x)
        ya = np.asarray(y)
        self._cols = None
        if ya.ndim == 2:
            cols = [CubicSpline(xs, [row[j] for row in ya.tolist()], 0, bc_type, extrapolate) for j in range(ya.shape[1])]
            self._cols = cols
            self.x = np.array(xs)
            self._x = xs
            self.extrapolate = True if extrapolate is None else extrapolate
            self._c = cols[0]._c
            self.c = cols[0].c
            return
        ys = tolist(ya)
        n = len(xs)
        if n < 2:
            raise ValueError("`x` must contain at least 2 elements.")
        for i in range(n - 1):
            if not xs[i + 1] > xs[i]:
                raise ValueError("`x` must be strictly increasing sequence.")
        bc = _norm_bc(bc_type)
        if bc[0] == "periodic" and abs(ys[0] - ys[-1]) > 1e-14 * max(1.0, abs(ys[0])):
            raise ValueError("The first and last `y` point along axis 0 must be identical (within machine precision) "
                             "when bc_type='periodic'.")
        if n == 3 and bc[0] == "not-a-knot" and bc[1] == "not-a-knot":
            m0 = (ys[1] - ys[0]) / (xs[1] - xs[0])
            aq = ((ys[2] - ys[1]) / (xs[2] - xs[1]) - m0) / (xs[2] - xs[0])
            dx0 = xs[1] - xs[0]
            coef = [[0.0, 0.0], [aq, aq], [m0 - aq * dx0, m0 + aq * dx0], [ys[0], ys[1]]]
        else:
            s = _spline_slopes(xs, ys, bc)
            coef = _hermite_coeffs(xs, ys, s)
        PPoly.__init__(self, coef, xs, extrapolate if extrapolate != "periodic" else "periodic")
        if bc[0] == "periodic" and extrapolate is None:
            self.extrapolate = "periodic"

    def __call__(self, x, nu=0, extrapolate=None):
        if self._cols is not None:
            outs = [c(x, nu, extrapolate) for c in self._cols]
            if _is_scalar(x):
                return np.array(outs)
            a = np.array([tolist(o) for o in outs])
            return a.T
        ex = extrapolate
        if ex is None:
            ex = self.extrapolate
        return PPoly.__call__(self, x, nu, ex if ex != True else None)

    def derivative(self, nu=1):
        if self._cols is not None:
            raise NotImplementedError("derivative of a vector valued spline")
        r = PPoly.derivative(self, nu)
        return r

    def integrate(self, a, b, extrapolate=None):
        sign = 1.0
        if a > b:
            a, b = b, a
            sign = -1.0
        anti = self.antiderivative()
        ex = self.extrapolate if extrapolate is None else extrapolate
        if ex == "periodic":
            xs = self._x
            period = xs[-1] - xs[0]
            full = anti(xs[-1], extrapolate=True) - anti(xs[0], extrapolate=True)
            k = math.floor((b - a) / period)
            tot = k * full
            a2 = xs[0] + (a - xs[0]) % period
            b2 = a2 + (b - a) - k * period
            if b2 <= xs[-1]:
                tot += anti(b2, extrapolate=True) - anti(a2, extrapolate=True)
            else:
                tot += anti(xs[-1], extrapolate=True) - anti(a2, extrapolate=True)
                tot += anti(xs[0] + b2 - xs[-1], extrapolate=True) - anti(xs[0], extrapolate=True)
            return sign * tot
        return sign * (anti(b, extrapolate=ex if ex else True) - anti(a, extrapolate=ex if ex else True))


class CubicHermiteSpline(PPoly):
    def __init__(self, x, y, dydx, axis=0, extrapolate=None):
        xs = tolist(x)
        PPoly.__init__(self, _hermite_coeffs(xs, tolist(y), tolist(dydx)), xs, extrapolate)


def _pchip_slopes(xs, ys):
    n = len(xs)
    hk = [xs[i + 1] - xs[i] for i in range(n - 1)]
    mk = [(ys[i + 1] - ys[i]) / hk[i] for i in range(n - 1)]
    if n == 2:
        return [mk[0], mk[0]]

    def sgn(v):
        return 1.0 if v > 0 else (-1.0 if v < 0 else 0.0)
    d = [0.0] * n
    for i in range(1, n - 1):
        m0 = mk[i - 1]
        m1 = mk[i]
        if sgn(m0) != sgn(m1) or m0 == 0 or m1 == 0:
            d[i] = 0.0
        else:
            w1 = 2.0 * hk[i] + hk[i - 1]
            w2 = hk[i] + 2.0 * hk[i - 1]
            whmean = (w1 / m0 + w2 / m1) / (w1 + w2)
            d[i] = 1.0 / whmean

    def edge(h0, h1, m0, m1):
        dd = ((2.0 * h0 + h1) * m0 - h0 * m1) / (h0 + h1)
        if sgn(dd) != sgn(m0):
            return 0.0
        if sgn(m0) != sgn(m1) and abs(dd) > 3.0 * abs(m0):
            return 3.0 * m0
        return dd
    d[0] = edge(hk[0], hk[1], mk[0], mk[1])
    d[n - 1] = edge(hk[n - 2], hk[n - 3], mk[n - 2], mk[n - 3])
    return d


class PchipInterpolator(PPoly):
    def __init__(self, x, y, axis=0, extrapolate=None):
        xs = tolist(x)
        ys = tolist(y)
        PPoly.__init__(self, _hermite_coeffs(xs, ys, _pchip_slopes(xs, ys)), xs, extrapolate)


def pchip_interpolate(xi, yi, x, der=0, axis=0):
    p = PchipInterpolator(xi, yi)
    return p(x, nu=der)


# ---------------------------------------------------------------------------
# interp1d
# ---------------------------------------------------------------------------

class interp1d:
    def __init__(self, x, y, kind="linear", axis=-1, copy=True, bounds_error=None, fill_value=nan,
                 assume_sorted=False):
        xs = tolist(x)
        ya = np.asarray(y)
        if ya.ndim == 0:
            raise ValueError("y must be at least 1-d")
        self._yshape = ya.shape
        ax = axis if axis >= 0 else axis + ya.ndim
        self._axis = ax
        if ya.ndim == 1:
            lines = [tolist(ya)]
        elif ya.ndim == 2:
            rows = [[float(v) for v in r] for r in ya.tolist()]
            if ax == 1:
                lines = rows
            else:
                lines = [[rows[i][j] for i in range(len(rows))] for j in range(len(rows[0]))]
        else:
            raise NotImplementedError("interp1d: only 1-d and 2-d y")
        if len(lines[0]) != len(xs):
            raise ValueError("x and y arrays must be equal in length along interpolation axis.")
        if not assume_sorted:
            order = sorted(range(len(xs)), key=lambda i: xs[i])
            xs = [xs[i] for i in order]
            lines = [[ln[i] for i in order] for ln in lines]
        self.x = np.array(xs)
        self._xs = xs
        self._lines = lines
        self.kind = kind
        if isinstance(kind, int):
            kind = {0: "zero", 1: "slinear", 2: "quadratic", 3: "cubic"}.get(kind, kind)
            self.kind = kind
        if kind not in ("linear", "nearest", "nearest-up", "previous", "next", "zero", "slinear", "cubic",
                        "quadratic"):
            raise NotImplementedError("interp1d kind " + str(kind))
        if kind in ("cubic",) and len(xs) < 4:
            raise ValueError("The number of derivatives at boundaries does not match: expected 1, got 0+0")
        self._splines = None
        if kind == "cubic":
            self._splines = [CubicSpline(xs, ln, bc_type="not-a-knot", extrapolate=True) for ln in lines]
        if kind == "quadratic":
            self._splines = [_make_interp_bspline(xs, ln, 2) for ln in lines]
        self.fill_value = fill_value
        self._extrapolate = False
        if isinstance(fill_value, str) and fill_value == "extrapolate":
            self._extrapolate = True
            self.bounds_error = False if bounds_error is None else bounds_error
        else:
            self.bounds_error = True if bounds_error is None else bounds_error
            if isinstance(fill_value, tuple) and len(fill_value) == 2:
                self._fill = (float(fill_value[0]), float(fill_value[1]))
            else:
                self._fill = (float(fill_value), float(fill_value))

    def _eval_line(self, li, xv):
        xs = self._xs
        ys = self._lines[li]
        n = len(xs)
        kind = self.kind
        if kind == "cubic" or kind == "quadratic":
            return self._splines[li]._eval1(xv, 0, True)
        if kind in ("linear", "slinear"):
            i = _bisect_left(xs, xv)
            if i < 1:
                i = 1
            if i > n - 1:
                i = n - 1
            lo = i - 1
            hi = i
            slope = (ys[hi] - ys[lo]) / (xs[hi] - xs[lo])
            return slope * (xv - xs[lo]) + ys[lo]
        if kind == "nearest" or kind == "nearest-up":
            bds = []
            for i in range(n - 1):
                bds.append(xs[i] / 2.0 + xs[i + 1] / 2.0)
            if kind == "nearest":
                idx = _bisect_left(bds, xv)
            else:
                idx = _bisect_right(bds, xv)
            return ys[idx]
        if kind == "previous":
            idx = _bisect_right(xs, xv) - 1
            idx = min(max(idx, 0), n - 1)
            return ys[idx]
        if kind == "next":
            idx = _bisect_left(xs, xv)
            idx = min(max(idx, 0), n - 1)
            return ys[idx]
        if kind == "zero":
            idx = _bisect_right(xs, xv) - 1
            idx = min(max(idx, 0), n - 1)
            return ys[idx]
        raise NotImplementedError(kind)

    def _eval_one(self, xv):
        xs = self._xs
        lo = xs[0]
        hi = xs[-1]
        if xv != xv:
            return [nan] * len(self._lines)
        if not self._extrapolate:
            if xv < lo:
                if self.bounds_error:
                    raise ValueError("A value (" + repr(xv) + ") in x_new is below the interpolation range's "
                                     "minimum value (" + repr(lo) + ").")
                return [self._fill[0]] * len(self._lines)
            if xv > hi:
                if self.bounds_error:
                    raise ValueError("A value (" + repr(xv) + ") in x_new is above the interpolation range's "
                                     "maximum value (" + repr(hi) + ").")
                return [self._fill[1]] * len(self._lines)
        return [self._eval_line(li, xv) for li in range(len(self._lines))]

    def __call__(self, x_new):
        scalar = _is_scalar(x_new)
        xl = [float(x_new)] if scalar else tolist(x_new)
        res = [self._eval_one(v) for v in xl]
        nl = len(self._lines)
        yshape = self._yshape
        if len(yshape) == 1:
            vals = [r[0] for r in res]
            if scalar:
                return vals[0]
            return _shape_out(vals, x_new)
        # 2-d y
        ax = self._axis
        m = len(res)
        if scalar:
            return np.array([res[0][li] for li in range(nl)])
        xa = np.asarray(x_new)
        if xa.ndim != 1:
            raise NotImplementedError("interp1d: multi-dimensional x_new with 2-d y")
        if ax == 1:
            return np.array([[res[j][li] for j in range(m)] for li in range(nl)])
        return np.array([[res[j][li] for li in range(nl)] for j in range(m)])


# ---------------------------------------------------------------------------
# polynomial / other interpolators
# ---------------------------------------------------------------------------

class _Poly1d:
    """minimal numpy.poly1d replacement (coefficients, highest power first)"""

    def __init__(self, coeffs):
        c = [float(v) for v in coeffs]
        while len(c) > 1 and c[0] == 0.0:
            c = c[1:]
        self.coeffs = np.array(c)
        self._c = c
        self.order = len(c) - 1

    @property
    def coef(self):
        return self.coeffs

    @property
    def c(self):
        return self.coeffs

    def __call__(self, x):
        def ev(v):
            r = 0.0
            for cc in self._c:
                r = r * v + cc
            return r
        if _is_scalar(x):
            return ev(float(x))
        xa = np.asarray(x)
        return np.array([ev(float(v)) for v in xa.flatten().tolist()]).reshape(xa.shape)

    def deriv(self, m=1):
        c = self._c
        for k in range(m):
            n = len(c) - 1
            c = [c[i] * (n - i) for i in range(n)] if n > 0 else [0.0]
        return _Poly1d(c)

    def integ(self, m=1, k=0):
        c = self._c
        for q in range(m):
            n = len(c)
            c = [c[i] / (n - i) for i in range(n)] + [float(k)]
        return _Poly1d(c)

    def __repr__(self):
        return "poly1d(" + repr(self.coeffs) + ")"


def lagrange(x, w):
    xs = tolist(x)
    ws = tolist(w)
    n = len(xs)
    total = [0.0]
    for j in range(n):
        pt = [ws[j]]
        for k in range(n):
            if k == j:
                continue
            fac = xs[j] - xs[k]
            # multiply pt by (x - xs[k]) / fac
            new = [0.0] * (len(pt) + 1)
            for i in range(len(pt)):
                new[i] += pt[i] / fac
                new[i + 1] -= pt[i] * xs[k] / fac
            pt = new
        if len(pt) > len(total):
            total = [0.0] * (len(pt) - len(total)) + total
        total = [total[i] + pt[i] for i in range(len(total))]
    return _Poly1d(total)


class BarycentricInterpolator:
    def __init__(self, xi, yi=None, axis=0):
        self.xi = tolist(xi)
        self.yi = tolist(yi)
        n = len(self.xi)
        w = []
        for j in range(n):
            p = 1.0
            for k in range(n):
                if k != j:
                    p *= (self.xi[j] - self.xi[k])
            w.append(1.0 / p)
        self.wi = w

    def _one(self, x):
        num = 0.0
        den = 0.0
        for j in range(len(self.xi)):
            d = x - self.xi[j]
            if d == 0.0:
                return self.yi[j]
            t = self.wi[j] / d
            num += t * self.yi[j]
            den += t
        return num / den

    def __call__(self, x):
        if _is_scalar(x):
            return self._one(float(x))
        xl = tolist(x)
        return _shape_out([self._one(v) for v in xl], x)


def barycentric_interpolate(xi, yi, x, axis=0):
    return BarycentricInterpolator(xi, yi)(x)


class InterpolatedUnivariateSpline:
    def __init__(self, x, y, w=None, bbox=(None, None), k=3, ext=0, check_finite=False):
        xs = tolist(x)
        ys = tolist(y)
        self._k = k
        self._ext = ext
        if k == 3:
            self._pp = CubicSpline(xs, ys, bc_type="not-a-knot", extrapolate=True)
        elif k == 1:
            self._pp = PPoly([[(ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]) for i in range(len(xs) - 1)],
                              ys[:-1]], xs, True)
        else:
            raise NotImplementedError("spline order k=%d" % k)
        self._xs = xs

    def __call__(self, x, nu=0, ext=None):
        e = self._ext if ext is None else ext
        xs = self._xs

        def one(v):
            if v < xs[0] or v > xs[-1]:
                if e == 1:
                    return 0.0
                if e == 2:
                    raise ValueError("Interpolation outside the knot range")
                if e == 3:
                    v = xs[0] if v < xs[0] else xs[-1]
            return self._pp._eval1(v, nu, True)
        if _is_scalar(x):
            return one(float(x))
        return _shape_out([one(v) for v in tolist(x)], x)

    def derivative(self, n=1):
        return _SplineView(self._pp.derivative(n), self._xs)

    def antiderivative(self, n=1):
        return _SplineView(self._pp.antiderivative(n), self._xs)

    def integral(self, a, b):
        return self._pp.integrate(a, b, True)

    def derivatives(self, x):
        return np.array([self._pp._eval1(float(x), q, True) for q in range(self._k + 1)])

    def roots(self):
        return self._pp.roots()

    def get_knots(self):
        return np.array(self._xs)


class _SplineView(InterpolatedUnivariateSpline):
    def __init__(self, pp, xs):
        self._pp = pp
        self._xs = xs
        self._k = len(pp._c) - 1
        self._ext = 0


class UnivariateSpline(InterpolatedUnivariateSpline):
    def __init__(self, x, y, w=None, bbox=(None, None), k=3, s=None, ext=0, check_finite=False):
        if s is not None and s != 0:
            raise NotImplementedError("UnivariateSpline: smoothing (s > 0) is not implemented, use s=0")
        if s is None:
            raise NotImplementedError("UnivariateSpline: default smoothing is not implemented, pass s=0")
        InterpolatedUnivariateSpline.__init__(self, x, y, w, bbox, k, ext)


class BSpline:
    def __init__(self, t, c, k, extrapolate=True, axis=0):
        self.t = np.array(t) if not isinstance(t, np.ndarray) else t
        self.c = np.array(c) if not isinstance(c, np.ndarray) else c
        self.k = k
        self.extrapolate = extrapolate
        self._t = [float(v) for v in tolist(self.t)]
        self._cc = [float(v) for v in tolist(self.c)]

    def _eval1(self, xv, nu, extrapolate):
        t = self._t
        k = self.k
        c = self._cc
        n = len(t) - k - 1
        if xv != xv:
            return nan
        if (extrapolate is False or (extrapolate is None and not self.extrapolate)) and (xv < t[k] or xv > t[n]):
            return nan
        if nu > 0:
            sp = self
            for q in range(nu):
                sp = sp._deriv1()
            return sp._eval1(xv, 0, extrapolate)
        i = k
        while i < n - 1 and xv >= t[i + 1]:
            i += 1
        d = [c[j + i - k] for j in range(k + 1)]
        for r in range(1, k + 1):
            for j in range(k, r - 1, -1):
                den = t[j + 1 + i - r] - t[j + i - k]
                a = (xv - t[j + i - k]) / den if den != 0 else 0.0
                d[j] = (1.0 - a) * d[j - 1] + a * d[j]
        return d[k]

    def _deriv1(self):
        t = self._t
        k = self.k
        c = self._cc
        n = len(t) - k - 1
        nc = [k * (c[i + 1] - c[i]) / (t[i + k + 1] - t[i + 1]) if t[i + k + 1] != t[i + 1] else 0.0
              for i in range(n - 1)]
        return BSpline(t[1:-1], nc, k - 1, self.extrapolate)

    def derivative(self, nu=1):
        sp = self
        for q in range(nu):
            sp = sp._deriv1()
        return sp

    def __call__(self, x, nu=0, extrapolate=None):
        if _is_scalar(x):
            return self._eval1(float(x), nu, extrapolate)
        xl = tolist(x)
        return _shape_out([self._eval1(v, nu, extrapolate) for v in xl], x)


def _basis_row(t, k, xv, n):
    i = k
    while i < n - 1 and xv >= t[i + 1]:
        i += 1
    row = [0.0] * n
    b = [1.0]
    for r in range(1, k + 1):
        nb = [0.0] * (r + 1)
        for j in range(r):
            left = t[i + 1 + j - r]
            right = t[i + 1 + j]
            den = right - left
            w = b[j] / den if den != 0 else 0.0
            nb[j] += w * (right - xv)
            nb[j + 1] += w * (xv - left)
        b = nb
    for j in range(k + 1):
        row[i - k + j] = b[j]
    return row


def _make_interp_bspline(x, y, k):
    xs = tolist(x)
    ys = [float(v) for v in tolist(y)]
    n = len(xs)
    if n <= k:
        raise ValueError("Need more data points")
    if k % 2 == 1:
        inner = xs[(k + 1) // 2: n - (k + 1) // 2]
    else:
        inner = [(xs[i] + xs[i + 1]) / 2.0 for i in range(k // 2, n - k // 2 - 1)]
    t = [xs[0]] * (k + 1) + inner + [xs[-1]] * (k + 1)
    A = [_basis_row(t, k, xv, n) for xv in xs]
    c = _la.solve(A, ys)
    return BSpline(t, c, k)


def make_interp_spline(x, y, k=3, t=None, bc_type=None, axis=0, check_finite=True):
    if k == 3:
        return CubicSpline(x, y, bc_type="not-a-knot" if bc_type is None else bc_type, extrapolate=True)
    if k == 1:
        xs = tolist(x)
        ys = tolist(y)
        return PPoly([[(ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]) for i in range(len(xs) - 1)], ys[:-1]], xs, True)
    if k == 0:
        raise NotImplementedError("make_interp_spline: k=0")
    return _make_interp_bspline(x, y, k)


class RegularGridInterpolator:
    def __init__(self, points, values, method="linear", bounds_error=True, fill_value=nan):
        self.grid = [tolist(p) for p in points]
        self.values = np.asarray(values)
        self.method = method
        self.bounds_error = bounds_error
        self.fill_value = fill_value
        self._vals = self.values.tolist()

    def _at(self, idx):
        v = self._vals
        for i in idx:
            v = v[i]
        return v

    def _one(self, pt):
        nd = len(self.grid)
        idx = []
        wts = []
        for d in range(nd):
            g = self.grid[d]
            xv = pt[d]
            if xv < g[0] or xv > g[-1] or xv != xv:
                if self.bounds_error:
                    raise ValueError("One of the requested xi is out of bounds in dimension %d" % d)
                if self.fill_value is None:
                    xv = min(max(xv, g[0]), g[-1])
                else:
                    return float(self.fill_value)
            i = _bisect_right(g, xv) - 1
            i = min(max(i, 0), len(g) - 2)
            t = (xv - g[i]) / (g[i + 1] - g[i])
            idx.append(i)
            wts.append(t)
        if self.method == "nearest":
            return float(self._at([idx[d] + (1 if wts[d] > 0.5 else 0) for d in range(nd)]))
        total = 0.0
        for corner in range(1 << nd):
            w = 1.0
            ii = []
            for d in range(nd):
                if (corner >> d) & 1:
                    w *= wts[d]
                    ii.append(idx[d] + 1)
                else:
                    w *= 1.0 - wts[d]
                    ii.append(idx[d])
            if w != 0.0:
                total += w * float(self._at(ii))
        return total

    def __call__(self, xi, method=None):
        a = np.asarray(xi) * 1.0
        nd = len(self.grid)
        if a.ndim == 1:
            return np.array([self._one(a.tolist())])
        rows = a.reshape((-1, nd)).tolist()
        return np.array([self._one(r) for r in rows])
