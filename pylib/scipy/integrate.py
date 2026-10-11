"""scipy.integrate subset: quad (QUADPACK 21 / 15 point Gauss-Kronrod, adaptive), dblquad, tplquad, nquad,
fixed_quad, trapezoid, simpson, cumulative_trapezoid, solve_ivp (RK45, RK23; other methods fall back to RK45) and
odeint."""
import math
import numpy as np
from ._util import inf, nan, tolist, slices, Result
from . import special as _sp

_EPMACH = 2.220446049250313e-16
_UFLOW = 2.2250738585072014e-308

_XGK = [0.995657163025808080735527280689003, 0.973906528517171720077964012084452,
        0.930157491355708226001207180059508, 0.865063366688984510732096688423493,
        0.780817726586416897063717578345042, 0.679409568299024406234327365114874,
        0.562757134668604683339000099272694, 0.433395394129247190799265943165784,
        0.294392862701460198131126603103866, 0.148874338981631210884826001129720, 0.0]
_WGK = [0.011694638867371874278064396062192, 0.032558162307964727478818972459390,
        0.054755896574351996031381300244580, 0.075039674810919952767043140916190,
        0.093125454583697605535065465083366, 0.109387158802297641899210590325805,
        0.123491976262065851077958109831074, 0.134709217311473325928054001771707,
        0.142775938577060080797094273138717, 0.147739104901338491374841515972068,
        0.149445554002916905664936468389821]
_WG = [0.066671344308688137593568809893332, 0.149451349150580593145776339657697,
       0.219086362515982043995534934228163, 0.269266719309996355091226921569469,
       0.295524224714752870173892994651338]
_XGK15 = [0.991455371120812639206854697526329, 0.949107912342758524526189684047851,
          0.864864423359769072789712788640926, 0.741531185599394439863864773280788,
          0.586087235467691130294144838258730, 0.405845151377397166906606412076961,
          0.207784955007898467600689403773245, 0.0]
_WGK15 = [0.022935322010529224963732008058970, 0.063092092629978553290700663189204,
          0.104790010322250183839876322541518, 0.140653259715525918745189590510238,
          0.169004726639267902826583426598550, 0.190350578064785409913256402421014,
          0.204432940075298892414161999234649, 0.209482141084727828012999174891714]
_WG15 = [0.0, 0.129484966168869693270611432679082, 0.0, 0.279705391489276667901467771423780,
         0.0, 0.381830050505118944950369775488975, 0.0, 0.417959183673469387755102040816327]


def _fl(v):
    if isinstance(v, np.ndarray):
        return float(v.flatten().tolist()[0])
    return float(v)


def _qk21(f, a, b):
    centr = 0.5 * (a + b)
    hlgth = 0.5 * (b - a)
    dhlgth = abs(hlgth)
    fc = f(centr)
    resg = 0.0
    resk = _WGK[10] * fc
    resabs = abs(resk)
    fv1 = [0.0] * 10
    fv2 = [0.0] * 10
    for j in range(5):
        jtw = 2 * j + 1
        absc = hlgth * _XGK[jtw]
        fval1 = f(centr - absc)
        fval2 = f(centr + absc)
        fv1[jtw] = fval1
        fv2[jtw] = fval2
        fsum = fval1 + fval2
        resg += _WG[j] * fsum
        resk += _WGK[jtw] * fsum
        resabs += _WGK[jtw] * (abs(fval1) + abs(fval2))
    for j in range(5):
        jtwm1 = 2 * j
        absc = hlgth * _XGK[jtwm1]
        fval1 = f(centr - absc)
        fval2 = f(centr + absc)
        fv1[jtwm1] = fval1
        fv2[jtwm1] = fval2
        fsum = fval1 + fval2
        resk += _WGK[jtwm1] * fsum
        resabs += _WGK[jtwm1] * (abs(fval1) + abs(fval2))
    reskh = resk * 0.5
    resasc = _WGK[10] * abs(fc - reskh)
    for j in range(10):
        resasc += _WGK[j] * (abs(fv1[j] - reskh) + abs(fv2[j] - reskh))
    result = resk * hlgth
    resabs *= dhlgth
    resasc *= dhlgth
    abserr = abs((resk - resg) * hlgth)
    return result, _scale_err(abserr, resabs, resasc)


def _scale_err(abserr, resabs, resasc):
    if resasc != 0.0 and abserr != 0.0:
        abserr = resasc * min(1.0, (200.0 * abserr / resasc) ** 1.5)
    if resabs > _UFLOW / (50.0 * _EPMACH):
        abserr = max(_EPMACH * 50.0 * resabs, abserr)
    return abserr


def _qk15i(f, boun, inf_, a, b):
    dinf = min(1, inf_)
    centr = 0.5 * (a + b)
    hlgth = 0.5 * (b - a)
    tabsc1 = boun + dinf * (1.0 - centr) / centr
    fval1 = f(tabsc1)
    if inf_ == 2:
        fval1 += f(-tabsc1)
    fc = (fval1 / centr) / centr
    resg = _WG15[7] * fc
    resk = _WGK15[7] * fc
    resabs = abs(resk)
    fv1 = [0.0] * 7
    fv2 = [0.0] * 7
    for j in range(7):
        absc = hlgth * _XGK15[j]
        absc1 = centr - absc
        absc2 = centr + absc
        tabsc1 = boun + dinf * (1.0 - absc1) / absc1
        tabsc2 = boun + dinf * (1.0 - absc2) / absc2
        fval1 = f(tabsc1)
        fval2 = f(tabsc2)
        if inf_ == 2:
            fval1 += f(-tabsc1)
            fval2 += f(-tabsc2)
        fval1 = (fval1 / absc1) / absc1
        fval2 = (fval2 / absc2) / absc2
        fv1[j] = fval1
        fv2[j] = fval2
        fsum = fval1 + fval2
        resg += _WG15[j] * fsum
        resk += _WGK15[j] * fsum
        resabs += _WGK15[j] * (abs(fval1) + abs(fval2))
    reskh = resk * 0.5
    resasc = _WGK15[7] * abs(fc - reskh)
    for j in range(7):
        resasc += _WGK15[j] * (abs(fv1[j] - reskh) + abs(fv2[j] - reskh))
    result = resk * hlgth
    resasc *= hlgth
    resabs *= hlgth
    abserr = abs((resk - resg) * hlgth)
    return result, _scale_err(abserr, resabs, resasc)


def _adaptive(rule, a, b, epsabs, epsrel, limit):
    """globally adaptive bisection; rule(a, b) -> (result, error)"""
    r0, e0 = rule(a, b)
    nev = 1
    errbnd = max(epsabs, epsrel * abs(r0))
    ivs = [[a, b, r0, e0]]
    ier = 0
    if e0 <= errbnd:
        return r0, e0, ivs, ier
    result = r0
    errsum = e0
    maxlim = max(limit, 1) * 2
    while len(ivs) < maxlim:
        k = 0
        best = ivs[0][3]
        for i in range(1, len(ivs)):
            if ivs[i][3] > best:
                best = ivs[i][3]
                k = i
        lo, hi, rr, ee = ivs[k]
        mid = 0.5 * (lo + hi)
        r1, e1 = rule(lo, mid)
        r2, e2 = rule(mid, hi)
        ivs[k] = [lo, mid, r1, e1]
        ivs.append([mid, hi, r2, e2])
        result = 0.0
        errsum = 0.0
        for iv in ivs:
            result += iv[2]
            errsum += iv[3]
        errbnd = max(epsabs, epsrel * abs(result))
        if errsum <= errbnd:
            return result, errsum, ivs, 0
        if len(ivs) >= limit:
            ier = 1
        if abs(mid - lo) < 1e-14 * max(abs(lo), abs(hi), 1e-300):
            ier = 3
            break
    return result, errsum, ivs, 1 if ier == 0 else ier


def _quad_one(f, a, b, epsabs, epsrel, limit):
    if a == b:
        return 0.0, 0.0, [[a, b, 0.0, 0.0]], 0
    if a == -inf and b == inf:
        rule = lambda lo, hi: _qk15i(f, 0.0, 2, lo, hi)
        return _adaptive(rule, 0.0, 1.0, epsabs, epsrel, limit)
    if b == inf:
        rule = lambda lo, hi: _qk15i(f, a, 1, lo, hi)
        return _adaptive(rule, 0.0, 1.0, epsabs, epsrel, limit)
    if a == -inf:
        rule = lambda lo, hi: _qk15i(f, b, -1, lo, hi)
        return _adaptive(rule, 0.0, 1.0, epsabs, epsrel, limit)
    return _adaptive(lambda lo, hi: _qk21(f, lo, hi), a, b, epsabs, epsrel, limit)


def quad(func, a, b, args=(), full_output=0, epsabs=1.49e-8, epsrel=1.49e-8, limit=50, points=None,
         weight=None, wvar=None, wopts=None, maxp1=50, limlst=50):
    if not isinstance(args, tuple):
        args = (args,)
    a = float(a)
    b = float(b)
    nev = [0]

    def f(x):
        nev[0] += 1
        return _fl(func(x, *args))
    if weight is not None:
        w = weight
        if w in ("cos", "sin"):
            g = (lambda x: f(x) * math.cos(wvar * x)) if w == "cos" else (lambda x: f(x) * math.sin(wvar * x))
            r, e, ivs, ier = _quad_one(g, a, b, epsabs, epsrel, limit)
            return (r, e)
        raise NotImplementedError("quad: weight " + str(w))
    if points is not None and a != -inf and b != inf:
        pts = sorted([float(p) for p in tolist(points)])
        edges = [a] + [p for p in pts if min(a, b) < p < max(a, b)] + [b]
        if a > b:
            edges = [a] + sorted([p for p in pts if b < p < a], reverse=True) + [b]
        tot = 0.0
        err = 0.0
        for i in range(len(edges) - 1):
            r, e, ivs, ier = _quad_one(f, edges[i], edges[i + 1], epsabs, epsrel, limit)
            tot += r
            err += e
        return (tot, err)
    r, e, ivs, ier = _quad_one(f, a, b, epsabs, epsrel, limit)
    if full_output:
        return (r, e, {"neval": nev[0], "last": len(ivs)})
    return (r, e)


def _bound(g, x, args):
    if callable(g):
        return float(g(*(tuple(x) + tuple(args))))
    return float(g)


def nquad(func, ranges, args=None, opts=None, full_output=False):
    if args is None:
        args = ()
    if opts is None:
        opts = [{}] * len(ranges)
    elif isinstance(opts, dict):
        opts = [opts] * len(ranges)
    n = len(ranges)

    def rec(level, xs):
        # xs: values of the already fixed outer variables (outermost first); integrate variable `level`
        rng = ranges[level]
        if callable(rng):
            rng = rng(*(tuple(reversed(xs)) + tuple(args)))
        lo, hi = rng
        o = opts[level]
        if callable(o):
            o = o(*(tuple(reversed(xs)) + tuple(args)))
        epsabs = o.get("epsabs", 1.49e-8)
        epsrel = o.get("epsrel", 1.49e-8)
        limit = o.get("limit", 50)
        pts = o.get("points", None)
        if level == 0:
            def g(x):
                return func(*((x,) + tuple(reversed(xs)) + tuple(args)))
        else:
            def g(x):
                return rec(level - 1, xs + [x])[0]
        r = quad(g, lo, hi, epsabs=epsabs, epsrel=epsrel, limit=limit, points=pts)
        return r
    return rec(n - 1, [])


def dblquad(func, a, b, gfun, hfun, args=(), epsabs=1.49e-8, epsrel=1.49e-8):
    def inner(x):
        lo = _bound(gfun, [x], ())
        hi = _bound(hfun, [x], ())
        return quad(func, lo, hi, args=(x,) + tuple(args), epsabs=epsabs, epsrel=epsrel)[0]
    return quad(inner, a, b, epsabs=epsabs, epsrel=epsrel)


def tplquad(func, a, b, gfun, hfun, qfun, rfun, args=(), epsabs=1.49e-8, epsrel=1.49e-8):
    def mid(y, x):
        lo = _bound(qfun, [x, y], ())
        hi = _bound(rfun, [x, y], ())
        return quad(func, lo, hi, args=(y, x) + tuple(args), epsabs=epsabs, epsrel=epsrel)[0]
    return dblquad(mid, a, b, gfun, hfun, epsabs=epsabs, epsrel=epsrel)


def fixed_quad(func, a, b, args=(), n=5):
    x, w = _sp.roots_legendre(n)
    xl = tolist(x)
    wl = tolist(w)
    s = 0.0
    for i in range(n):
        s += wl[i] * _fl(func((b - a) * (xl[i] + 1.0) / 2.0 + a, *args))
    return (s * (b - a) / 2.0, None)


# ---------------------------------------------------------------------------
# sampled data
# ---------------------------------------------------------------------------

def _along(y, axis, fn):
    """apply fn(list) -> float / list along `axis` of a 1-d / 2-d array"""
    ya = np.asarray(y)
    if ya.ndim <= 1:
        return fn(tolist(ya))
    sl, k = slices(ya, axis)
    res = [fn(s) for s in sl]
    if isinstance(res[0], list):
        if axis < 0:
            axis += 2
        if axis == 1:
            return np.array(res)
        return np.array([[res[j][i] for j in range(len(res))] for i in range(len(res[0]))])
    return np.array(res)


def trapezoid(y, x=None, dx=1.0, axis=-1):
    xv = tolist(x) if x is not None else None

    def fn(v):
        n = len(v)
        s = 0.0
        if xv is None:
            for i in range(n - 1):
                s += dx * (v[i + 1] + v[i]) / 2.0
        else:
            for i in range(n - 1):
                s += (xv[i + 1] - xv[i]) * (v[i + 1] + v[i]) / 2.0
        return s
    return _along(y, axis, fn)


def cumulative_trapezoid(y, x=None, dx=1.0, axis=-1, initial=None):
    xv = tolist(x) if x is not None else None

    def fn(v):
        n = len(v)
        out = []
        s = 0.0
        for i in range(n - 1):
            d = dx if xv is None else xv[i + 1] - xv[i]
            s += d * (v[i + 1] + v[i]) / 2.0
            out.append(s)
        if initial is not None:
            out = [float(initial)] + out
        return out
    return _along(y, axis, fn)


def _basic_simpson(y, start, stop, x, dx):
    # sums over pairs of intervals y[start..stop] (stop - start even)
    s = 0.0
    i = start
    while i + 2 <= stop + 0 and i + 2 < len(y) and i < stop:
        if x is None:
            h0 = dx
            h1 = dx
        else:
            h0 = x[i + 1] - x[i]
            h1 = x[i + 2] - x[i + 1]
        hsum = h0 + h1
        hprod = h0 * h1
        h0divh1 = h0 / h1
        s += hsum / 6.0 * (y[i] * (2.0 - 1.0 / h0divh1) + y[i + 1] * hsum * hsum / hprod
                           + y[i + 2] * (2.0 - h0divh1))
        i += 2
    return s


def simpson(y, x=None, dx=1.0, axis=-1):
    xv = tolist(x) if x is not None else None

    def fn(v):
        N = len(v)
        if N < 2:
            return 0.0
        if N == 2:
            d = dx if xv is None else xv[1] - xv[0]
            return 0.5 * d * (v[0] + v[1])
        result = 0.0
        last = N
        if N % 2 == 0:
            last = N - 1
            if xv is None:
                h0 = dx
                h1 = dx
            else:
                h0 = xv[N - 2] - xv[N - 3]
                h1 = xv[N - 1] - xv[N - 2]
            alpha = (2.0 * h1 * h1 + 3.0 * h1 * h0) / (6.0 * (h0 + h1))
            beta = (h1 * h1 + 3.0 * h1 * h0) / (6.0 * h0)
            eta = h1 * h1 * h1 / (6.0 * h0 * (h0 + h1))
            result += alpha * v[N - 1] + beta * v[N - 2] - eta * v[N - 3]
        result += _basic_simpson(v[:last], 0, last - 1, xv, dx)
        return result
    return _along(y, axis, fn)


def romb(y, dx=1.0, axis=-1, show=False):
    v = tolist(y)
    n = len(v)
    k = 0
    while (1 << k) + 1 < n:
        k += 1
    if (1 << k) + 1 != n:
        raise ValueError("Number of samples must be one plus a non-negative power of 2.")
    R = [[0.0] * (k + 1) for i in range(k + 1)]
    h = (n - 1) * dx
    R[0][0] = (v[0] + v[-1]) * h / 2.0
    for i in range(1, k + 1):
        h = h / 2.0
        step = (n - 1) >> i
        s = 0.0
        for j in range(1, 1 << i, 2):
            s += v[j * ((n - 1) >> i)]
        R[i][0] = 0.5 * R[i - 1][0] + h * s
        for j in range(1, i + 1):
            R[i][j] = R[i][j - 1] + (R[i][j - 1] - R[i - 1][j - 1]) / (4.0 ** j - 1.0)
    return R[k][k]


# ---------------------------------------------------------------------------
# ordinary differential equations
# ---------------------------------------------------------------------------

_RK45_A = [[],
           [1 / 5],
           [3 / 40, 9 / 40],
           [44 / 45, -56 / 15, 32 / 9],
           [19372 / 6561, -25360 / 2187, 64448 / 6561, -212 / 729],
           [9017 / 3168, -355 / 33, 46732 / 5247, 49 / 176, -5103 / 18656]]
_RK45_B = [35 / 384, 0, 500 / 1113, 125 / 192, -2187 / 6784, 11 / 84]
_RK45_C = [0, 1 / 5, 3 / 10, 4 / 5, 8 / 9, 1]
_RK45_E = [-71 / 57600, 0, 71 / 16695, -71 / 1920, 17253 / 339200, -22 / 525, 1 / 40]
_RK45_P = [[1, -8048581381 / 2820520608, 8663915743 / 2820520608, -12715105075 / 11282082432],
           [0, 0, 0, 0],
           [0, 131558114200 / 32700410799, -68118460800 / 10900136933, 87487479700 / 32700410799],
           [0, -1754552775 / 470086768, 14199869525 / 1410260304, -10690763975 / 1880347072],
           [0, 127303824393 / 49829197408, -318862633887 / 49829197408, 701980252875 / 199316789632],
           [0, -282668133 / 205662961, 2019193451 / 616988883, -1453857185 / 822651844],
           [0, 40617522 / 29380423, -110615467 / 29380423, 69997945 / 29380423]]
_RK23_A = [[], [1 / 2], [0, 3 / 4]]
_RK23_B = [2 / 9, 1 / 3, 4 / 9]
_RK23_C = [0, 1 / 2, 3 / 4]
_RK23_E = [5 / 72, -1 / 12, -1 / 9, 1 / 8]
_RK23_P = [[1, -4 / 3, 5 / 9], [0, 1, -2 / 3], [0, 4 / 3, -8 / 9], [0, -1, 1]]

_SAFETY = 0.9
_MIN_FACTOR = 0.2
_MAX_FACTOR = 10.0


def _rms(v):
    s = 0.0
    for t in v:
        s += t * t
    return math.sqrt(s) / math.sqrt(len(v))


def _next_after(t, direction):
    if t == 0:
        return 5e-324 * direction
    return t + direction * abs(t) * 2.220446049250313e-16


class _RK:
    def __init__(self, fun, t0, y0, t_bound, A, B, C, E, P, order, err_order, max_step=inf, rtol=1e-3,
                 atol=1e-6, first_step=None):
        self.fun = fun
        self.t = float(t0)
        self.y = list(y0)
        self.t_bound = float(t_bound)
        self.direction = 1.0 if t_bound >= t0 else -1.0
        self.A = A
        self.B = B
        self.C = C
        self.E = E
        self.P = P
        self.n = len(y0)
        self.max_step = max_step
        self.rtol = rtol
        self.atol = atol
        self.nfev = 0
        self.status = "running"
        self.f = self._call(self.t, self.y)
        self.error_exponent = -1.0 / (err_order + 1)
        self.stages = len(B) + 1
        if first_step is None:
            self.h_abs = self._initial_step(err_order)
        else:
            self.h_abs = float(first_step)
        self.K = None
        self.t_old = None
        self.y_old = None
        self.h_previous = None

    def _call(self, t, y):
        self.nfev += 1
        return self.fun(t, y)

    def _initial_step(self, order):
        y0 = self.y
        f0 = self.f
        t0 = self.t
        n = self.n
        if n == 0:
            return inf
        interval = abs(self.t_bound - t0)
        if interval == 0.0:
            return 0.0
        scale = [self.atol[i] + abs(y0[i]) * self.rtol for i in range(n)]
        d0 = _rms([y0[i] / scale[i] for i in range(n)])
        d1 = _rms([f0[i] / scale[i] for i in range(n)])
        if d0 < 1e-5 or d1 < 1e-5:
            h0 = 1e-6
        else:
            h0 = 0.01 * d0 / d1
        h0 = min(h0, interval)
        y1 = [y0[i] + h0 * self.direction * f0[i] for i in range(n)]
        f1 = self._call(t0 + h0 * self.direction, y1)
        d2 = _rms([(f1[i] - f0[i]) / scale[i] for i in range(n)]) / h0
        if d1 <= 1e-15 and d2 <= 1e-15:
            h1 = max(1e-6, h0 * 1e-3)
        else:
            h1 = (0.01 / max(d1, d2)) ** (1.0 / (order + 1))
        return min(100 * h0, h1, interval, self.max_step)

    def step(self):
        if self.status != "running":
            raise RuntimeError("Attempt to step on a failed or finished solver.")
        if self.n == 0 or self.t == self.t_bound:
            self.t_old = self.t
            self.t = self.t_bound
            self.status = "finished"
            return None
        t = self.t
        y = self.y
        n = self.n
        direction = self.direction
        min_step = 10.0 * abs(_next_after(t, direction) - t)
        if self.h_abs > self.max_step:
            h_abs = self.max_step
        elif self.h_abs < min_step:
            h_abs = min_step
        else:
            h_abs = self.h_abs
        accepted = False
        rejected = False
        A = self.A
        B = self.B
        C = self.C
        while not accepted:
            if h_abs < min_step:
                self.status = "failed"
                return "Required step size is less than spacing between numbers."
            h = h_abs * direction
            t_new = t + h
            if direction * (t_new - self.t_bound) > 0:
                t_new = self.t_bound
            h = t_new - t
            h_abs = abs(h)
            K = [self.f]
            for s in range(1, self.stages - 1):
                a = A[s]
                dy = []
                for i in range(n):
                    acc = 0.0
                    for j in range(s):
                        acc += K[j][i] * a[j]
                    dy.append(acc * h)
                K.append(self._call(t + C[s] * h, [y[i] + dy[i] for i in range(n)]))
            y_new = []
            for i in range(n):
                acc = 0.0
                for j in range(self.stages - 1):
                    acc += K[j][i] * B[j]
                y_new.append(y[i] + h * acc)
            f_new = self._call(t + h, y_new)
            K.append(f_new)
            err = []
            for i in range(n):
                acc = 0.0
                for j in range(self.stages):
                    acc += K[j][i] * self.E[j]
                scale = self.atol[i] + max(abs(y[i]), abs(y_new[i])) * self.rtol
                err.append(acc * h / scale)
            error_norm = _rms(err)
            if error_norm < 1:
                if error_norm == 0:
                    factor = _MAX_FACTOR
                else:
                    factor = min(_MAX_FACTOR, _SAFETY * error_norm ** self.error_exponent)
                if rejected:
                    factor = min(1.0, factor)
                h_abs *= factor
                accepted = True
            else:
                h_abs *= max(_MIN_FACTOR, _SAFETY * error_norm ** self.error_exponent)
                rejected = True
        self.h_previous = h
        self.y_old = y
        self.t_old = t
        self.t = t_new
        self.y = y_new
        self.h_abs = h_abs
        self.f = f_new
        self.K = K
        if direction * (self.t - self.t_bound) >= 0:
            self.status = "finished"
        return None

    def dense_output(self):
        n = self.n
        K = self.K
        P = self.P
        ncol = len(P[0])
        Q = []
        for i in range(n):
            row = []
            for c in range(ncol):
                acc = 0.0
                for s in range(self.stages):
                    acc += K[s][i] * P[s][c]
                row.append(acc)
            Q.append(row)
        return _RkDense(self.t_old, self.t, self.y_old, Q)


class _RkDense:
    def __init__(self, t_old, t, y_old, Q):
        self.t_old = t_old
        self.t = t
        self.t_min = min(t_old, t)
        self.t_max = max(t_old, t)
        self.h = t - t_old
        self.y_old = y_old
        self.Q = Q

    def at(self, tt):
        x = (tt - self.t_old) / self.h
        ncol = len(self.Q[0])
        p = []
        acc = x
        for c in range(ncol):
            p.append(acc)
            acc = acc * x
        out = []
        for i in range(len(self.y_old)):
            s = 0.0
            row = self.Q[i]
            for c in range(ncol):
                s += row[c] * p[c]
            out.append(self.h * s + self.y_old[i])
        return out


class OdeSolution:
    def __init__(self, ts, interps):
        self.ts = ts
        self.interps = interps
        self.t_min = min(ts[0], ts[-1])
        self.t_max = max(ts[0], ts[-1])
        self.ascending = ts[-1] >= ts[0]

    def _one(self, tt):
        ts = self.ts
        n = len(self.interps)
        lo = 0
        hi = n - 1
        # segment i covers ts[i]..ts[i+1]
        if self.ascending:
            while lo < hi:
                mid = (lo + hi) // 2
                if ts[mid + 1] < tt:
                    lo = mid + 1
                else:
                    hi = mid
        else:
            while lo < hi:
                mid = (lo + hi) // 2
                if ts[mid + 1] > tt:
                    lo = mid + 1
                else:
                    hi = mid
        return self.interps[lo].at(tt)

    def __call__(self, t):
        if isinstance(t, (int, float)):
            return np.array(self._one(float(t)))
        tl = tolist(t)
        cols = [self._one(v) for v in tl]
        n = len(cols[0]) if cols else 0
        return np.array([[cols[j][i] for j in range(len(cols))] for i in range(n)])


class OdeResult:
    def __init__(self, **kw):
        self._keys = list(kw.keys())
        for k in kw:
            setattr(self, k, kw[k])

    def __getitem__(self, k):
        return getattr(self, k)

    def keys(self):
        return list(self._keys)

    def __contains__(self, k):
        return k in self._keys

    def __repr__(self):
        w = max([len(k) for k in self._keys])
        return "\n".join([" " * (w - len(k)) + k + ": " + repr(getattr(self, k)) for k in self._keys])


def _make_solver(method, fun, t0, y0, tf, rtol, atol, max_step, first_step):
    m = method if isinstance(method, str) else "RK45"
    if m in ("RK23",):
        return _RK(fun, t0, y0, tf, _RK23_A, _RK23_B, _RK23_C, _RK23_E, _RK23_P, 3, 2, max_step, rtol, atol, first_step)
    if m == "DOP853":
        from . import _dop853
        return _dop853.make(fun, t0, y0, tf, rtol, atol, max_step, first_step)
    return _RK(fun, t0, y0, tf, _RK45_A, _RK45_B, _RK45_C, _RK45_E, _RK45_P, 4, 4, max_step, rtol, atol, first_step)


def _active_events(g, g_new, dirs):
    out = []
    for i in range(len(g)):
        up = g[i] <= 0 and g_new[i] >= 0
        down = g[i] >= 0 and g_new[i] <= 0
        d = dirs[i]
        if (up and d > 0) or (down and d < 0) or ((up or down) and d == 0):
            out.append(i)
    return out


def solve_ivp(fun, t_span, y0, method="RK45", t_eval=None, dense_output=False, events=None, vectorized=False,
              args=None, **options):
    t0 = float(t_span[0])
    tf = float(t_span[1])
    y0l = tolist(y0)
    n = len(y0l)
    if args is not None:
        if not isinstance(args, tuple):
            args = tuple(args)
        user = fun
        fun = lambda t, y: user(t, y, *args)
        if events is not None:
            evs = events if isinstance(events, (list, tuple)) else [events]
            events = [(lambda t, y, e=e: e(t, y, *args)) for e in evs]
            for i in range(len(evs)):
                for att in ("terminal", "direction"):
                    if hasattr(evs[i], att):
                        setattr(events[i], att, getattr(evs[i], att))
    f = lambda t, y: tolist(fun(t, np.array(y)))
    rtol = options.get("rtol", 1e-3)
    atol = options.get("atol", 1e-6)
    atl = tolist(atol)
    if len(atl) == 1:
        atl = atl * n
    max_step = options.get("max_step", inf)
    first_step = options.get("first_step", None)
    solver = _make_solver(method, f, t0, y0l, tf, rtol, atl, max_step, first_step)
    direction = solver.direction
    if t_eval is not None:
        te = tolist(t_eval)
        te_i = 0
        ts = []
        ys = []
    else:
        ts = [t0]
        ys = [list(y0l)]
    interps = []
    ts_sol = [t0]
    evl = None
    t_events = None
    y_events = None
    if events is not None:
        evl = events if isinstance(events, (list, tuple)) else [events]
        dirs = [getattr(e, "direction", 0) for e in evl]
        terms = []
        for e in evl:
            tm = getattr(e, "terminal", None)
            terms.append(0 if tm is None else (1 if tm is True else (0 if tm is False else int(tm))))
        max_events = [(t if t > 0 else inf) for t in terms]
        count = [0] * len(evl)
        t_events = [[] for e in evl]
        y_events = [[] for e in evl]
        g = [_fl(e(t0, np.array(y0l))) for e in evl]
    status = None
    message = None
    sol_obj = None
    while status is None:
        message = solver.step()
        if solver.status == "finished":
            status = 0
        elif solver.status == "failed":
            status = -1
            break
        t_old = solver.t_old
        t = solver.t
        y = solver.y
        sol = None
        if dense_output:
            sol = solver.dense_output()
            interps.append(sol)
            ts_sol.append(t)
        if evl is not None:
            g_new = [_fl(e(t, np.array(y))) for e in evl]
            act = _active_events(g, g_new, dirs)
            if act:
                if sol is None:
                    sol = solver.dense_output()
                roots = []
                for ei in act:
                    count[ei] += 1
                    ev = evl[ei]

                    def h(tt, ev=ev, sol=sol):
                        return _fl(ev(tt, np.array(sol.at(tt))))
                    from .optimize import brentq
                    gl = g[ei]
                    gh = g_new[ei]
                    if gl == 0.0:
                        r = t_old
                    elif gh == 0.0:
                        r = t
                    else:
                        r = brentq(h, t_old, t, xtol=4 * 2.220446049250313e-16, rtol=4 * 2.220446049250313e-16)
                    roots.append((r, ei))
                roots.sort(key=lambda p: p[0] * direction)
                terminate = False
                kept = []
                for (r, ei) in roots:
                    kept.append((r, ei))
                    if count[ei] >= max_events[ei]:
                        terminate = True
                        break
                for (r, ei) in kept:
                    t_events[ei].append(r)
                    y_events[ei].append(np.array(sol.at(r)))
                if terminate:
                    status = 1
                    t = kept[-1][0]
                    y = sol.at(t)
            g = g_new
        if t_eval is None:
            ts.append(t)
            ys.append(list(y))
        else:
            while te_i < len(te) and te[te_i] * direction <= t * direction:
                tv = te[te_i]
                te_i += 1
                if tv * direction < t0 * direction:
                    continue
                ts.append(tv)
                if tv == t:
                    ys.append(list(y))
                else:
                    if sol is None:
                        sol = solver.dense_output()
                    ys.append(sol.at(tv))
    if status == 1:
        message = "A termination event occurred."
    elif status == 0:
        message = "The solver successfully reached the end of the integration interval."
    elif message is None:
        message = "Required step size is less than spacing between numbers."
    if dense_output:
        if len(interps) > 0:
            sol_obj = OdeSolution(ts_sol if status != 1 else ts_sol, interps)
    if t_eval is not None and len(ts) == 0:
        yarr = np.zeros((n, 0))
    else:
        yarr = np.array([[ys[j][i] for j in range(len(ys))] for i in range(n)])
    res = OdeResult(t=np.array(ts), y=yarr, sol=sol_obj,
                    t_events=[np.array(v) for v in t_events] if t_events is not None else None,
                    y_events=[np.array(v) for v in y_events] if y_events is not None else None,
                    nfev=solver.nfev, njev=0, nlu=0, status=status, message=message, success=status >= 0)
    return res


def odeint(func, y0, t, args=(), Dfun=None, col_deriv=0, full_output=0, ml=None, mu=None, rtol=None, atol=None,
           tcrit=None, h0=0.0, hmax=0.0, hmin=0.0, ixpr=0, mxstep=0, mxhnil=0, mxordn=12, mxords=5,
           printmessg=0, tfirst=False):
    if not isinstance(args, tuple):
        args = (args,)
    tl = tolist(t)
    y0l = tolist(y0)
    n = len(y0l)
    rt = 1.49012e-8 if rtol is None else rtol
    at = 1.49012e-8 if atol is None else atol
    if tfirst:
        f = lambda tt, y: tolist(func(tt, np.array(y), *args))
    else:
        f = lambda tt, y: tolist(func(np.array(y), tt, *args))
    atl = tolist(at)
    if len(atl) == 1:
        atl = atl * n
    rt = rt * 0.01
    atl = [a * 0.01 for a in atl]
    out = [list(y0l)]
    nfev = 0
    tend = tl[-1]
    if len(tl) > 1 and tend != tl[0]:
        solver = _RK(f, tl[0], y0l, tend, _RK45_A, _RK45_B, _RK45_C, _RK45_E, _RK45_P, 4, 4, inf, rt, atl, None)
        dirn = solver.direction
        k = 1
        while k < len(tl) and tl[k] == tl[0]:
            out.append(list(y0l))
            k += 1
        while solver.status == "running":
            solver.step()
            if solver.status == "failed":
                raise RuntimeError("Excess work done on this call (perhaps wrong Dfun type).")
            tcur = solver.t
            sol = None
            while k < len(tl) and tl[k] * dirn <= tcur * dirn:
                if tl[k] == tcur:
                    out.append(list(solver.y))
                else:
                    if sol is None:
                        sol = solver.dense_output()
                    out.append(sol.at(tl[k]))
                k += 1
        while k < len(tl):
            out.append(list(solver.y))
            k += 1
        nfev = solver.nfev
    else:
        for k in range(1, len(tl)):
            out.append(list(y0l))
    arr = np.array(out)
    if full_output:
        return arr, {"nfe": nfev, "message": "Integration successful."}
    return arr
