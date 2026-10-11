"""scipy.optimize subset: minimize (Nelder-Mead, BFGS, CG, L-BFGS-B/TNC (projected BFGS), Powell, SLSQP/COBYLA/
trust-constr (augmented Lagrangian)), minimize_scalar (brent, bounded, golden), root finders (brentq bisect ridder
newton root_scalar), fsolve / root, least_squares (Levenberg-Marquardt), curve_fit, linprog (simplex), nnls,
linear_sum_assignment."""
import math
import numpy as np
from ._util import inf, nan, tolist
from . import _la

_EPS = 2.220446049250313e-16
_SQRT_EPS = 1.4901161193847656e-08


class OptimizeResult:
    def __init__(self, **kw):
        self._keys = []
        for k in kw:
            self._set(k, kw[k])

    def _set(self, k, v):
        if k not in self._keys:
            self._keys.append(k)
        setattr(self, k, v)

    def __getitem__(self, k):
        if k not in self._keys:
            raise KeyError(k)
        return getattr(self, k)

    def __setitem__(self, k, v):
        self._set(k, v)

    def __contains__(self, k):
        return k in self._keys

    def keys(self):
        return list(self._keys)

    def values(self):
        return [getattr(self, k) for k in self._keys]

    def items(self):
        return [(k, getattr(self, k)) for k in self._keys]

    def get(self, k, default=None):
        if k in self._keys:
            return getattr(self, k)
        return default

    def __iter__(self):
        return iter(self._keys)

    def __len__(self):
        return len(self._keys)

    def __repr__(self):
        w = 0
        for k in self._keys:
            if len(k) > w:
                w = len(k)
        lines = []
        for k in self._keys:
            v = repr(getattr(self, k))
            lines.append(" " * (w - len(k)) + k + ": " + v.replace("\n", "\n" + " " * (w + 2)))
        return "\n".join(lines)


class Bounds:
    def __init__(self, lb=-inf, ub=inf, keep_feasible=False):
        self.lb = lb
        self.ub = ub
        self.keep_feasible = keep_feasible


class LinearConstraint:
    def __init__(self, A, lb=-inf, ub=inf, keep_feasible=False):
        self.A = A
        self.lb = lb
        self.ub = ub


class NonlinearConstraint:
    def __init__(self, fun, lb, ub, jac="2-point", hess=None, keep_feasible=False):
        self.fun = fun
        self.lb = lb
        self.ub = ub


def _vec(x):
    """flat list of floats"""
    return tolist(x)


def _fval(v):
    if isinstance(v, np.ndarray):
        return float(v.flatten().tolist()[0])
    if isinstance(v, (list, tuple)):
        return float(v[0])
    return float(v)


def _fvec(v):
    return tolist(v)


def _ar(x):
    return np.array(x)


# ---------------------------------------------------------------------------
# bracketing / scalar minimisation
# ---------------------------------------------------------------------------

def bracket(func, xa=0.0, xb=1.0, args=(), grow_limit=110.0, maxiter=1000):
    _gold = 1.618034
    _verysmall_num = 1e-21
    fa = float(func(xa, *args))
    fb = float(func(xb, *args))
    funcalls = 2
    if fa < fb:
        xa, xb = xb, xa
        fa, fb = fb, fa
    xc = xb + _gold * (xb - xa)
    fc = float(func(xc, *args))
    funcalls += 1
    it = 0
    while fc < fb:
        tmp1 = (xb - xa) * (fb - fc)
        tmp2 = (xb - xc) * (fb - fa)
        val = tmp2 - tmp1
        if abs(val) < _verysmall_num:
            denom = 2.0 * _verysmall_num
        else:
            denom = 2.0 * val
        w = xb - ((xb - xc) * tmp2 - (xb - xa) * tmp1) / denom
        wlim = xb + grow_limit * (xc - xb)
        if it > maxiter:
            raise RuntimeError("Too many iterations.")
        it += 1
        if (w - xc) * (xb - w) > 0.0:
            fw = float(func(w, *args))
            funcalls += 1
            if fw < fc:
                xa = xb
                xb = w
                fa = fb
                fb = fw
                break
            elif fw > fb:
                xc = w
                fc = fw
                break
            w = xc + _gold * (xc - xb)
            fw = float(func(w, *args))
            funcalls += 1
        elif (w - wlim) * (wlim - xc) >= 0.0:
            w = wlim
            fw = float(func(w, *args))
            funcalls += 1
        elif (w - wlim) * (xc - w) > 0.0:
            fw = float(func(w, *args))
            funcalls += 1
            if fw < fc:
                xb = xc
                xc = w
                w = xc + _gold * (xc - xb)
                fb = fc
                fc = fw
                fw = float(func(w, *args))
                funcalls += 1
        else:
            w = xc + _gold * (xc - xb)
            fw = float(func(w, *args))
            funcalls += 1
        xa = xb
        xb = xc
        xc = w
        fa = fb
        fb = fc
        fc = fw
    return xa, xb, xc, fa, fb, fc, funcalls


def _brent_core(func, args, brack, tol, maxiter):
    if brack is None:
        xa, xb, xc, fa, fb, fc, funcalls = bracket(func, args=args)
    elif len(brack) == 2:
        xa, xb, xc, fa, fb, fc, funcalls = bracket(func, xa=brack[0], xb=brack[1], args=args)
    elif len(brack) == 3:
        xa, xb, xc = brack
        if xa > xc:
            xc, xa = xa, xc
        if not ((xa < xb) and (xb < xc)):
            raise ValueError("Not a bracketing interval.")
        fa = float(func(xa, *args))
        fb = float(func(xb, *args))
        fc = float(func(xc, *args))
        if not ((fb < fa) and (fb < fc)):
            raise ValueError("Not a bracketing interval.")
        funcalls = 3
    else:
        raise ValueError("Bracketing interval must be length 2 or 3 sequence.")
    _mintol = 1.0e-11
    _cg = 0.3819660
    x = xb
    w = xb
    v = xb
    fw = fb
    fv = fb
    fx = fb
    if xa < xc:
        a = xa
        b = xc
    else:
        a = xc
        b = xa
    deltax = 0.0
    rat = 0.0
    it = 0
    while it < maxiter:
        tol1 = tol * abs(x) + _mintol
        tol2 = 2.0 * tol1
        xmid = 0.5 * (a + b)
        if abs(x - xmid) < (tol2 - 0.5 * (b - a)):
            break
        if abs(deltax) <= tol1:
            if x >= xmid:
                deltax = a - x
            else:
                deltax = b - x
            rat = _cg * deltax
        else:
            tmp1 = (x - w) * (fx - fv)
            tmp2 = (x - v) * (fx - fw)
            p = (x - v) * tmp2 - (x - w) * tmp1
            tmp2 = 2.0 * (tmp2 - tmp1)
            if tmp2 > 0.0:
                p = -p
            tmp2 = abs(tmp2)
            dx_temp = deltax
            deltax = rat
            if (p > tmp2 * (a - x)) and (p < tmp2 * (b - x)) and (abs(p) < abs(0.5 * tmp2 * dx_temp)):
                rat = p * 1.0 / tmp2
                u = x + rat
                if (u - a) < tol2 or (b - u) < tol2:
                    if xmid - x >= 0:
                        rat = tol1
                    else:
                        rat = -tol1
            else:
                if x >= xmid:
                    deltax = a - x
                else:
                    deltax = b - x
                rat = _cg * deltax
        if abs(rat) < tol1:
            if rat >= 0:
                u = x + tol1
            else:
                u = x - tol1
        else:
            u = x + rat
        fu = float(func(u, *args))
        funcalls += 1
        if fu > fx:
            if u < x:
                a = u
            else:
                b = u
            if (fu <= fw) or (w == x):
                v = w
                w = u
                fv = fw
                fw = fu
            elif (fu <= fv) or (v == x) or (v == w):
                v = u
                fv = fu
        else:
            if u >= x:
                a = x
            else:
                b = x
            v = w
            w = x
            x = u
            fv = fw
            fw = fx
            fx = fu
        it += 1
    return x, fx, it, funcalls


def brent(func, args=(), brack=None, tol=1.48e-8, full_output=0, maxiter=500):
    x, fx, it, nf = _brent_core(func, args, brack, tol, maxiter)
    if full_output:
        return x, fx, it, nf
    return x


def _fminbound_core(func, x1, x2, args, xatol, maxfun):
    sqrt_eps = math.sqrt(2.2e-16)
    golden_mean = 0.5 * (3.0 - math.sqrt(5.0))
    a = x1
    b = x2
    fulc = a + golden_mean * (b - a)
    nfc = fulc
    xf = fulc
    rat = 0.0
    e = 0.0
    x = xf
    fx = float(func(x, *args))
    num = 1
    ffulc = fx
    fnfc = fx
    xm = 0.5 * (a + b)
    tol1 = sqrt_eps * abs(xf) + xatol / 3.0
    tol2 = 2.0 * tol1
    flag = 0
    while abs(xf - xm) > (tol2 - 0.5 * (b - a)):
        golden = 1
        if abs(e) > tol1:
            golden = 0
            r = (xf - nfc) * (fx - ffulc)
            q = (xf - fulc) * (fx - fnfc)
            p = (xf - fulc) * q - (xf - nfc) * r
            q = 2.0 * (q - r)
            if q > 0.0:
                p = -p
            q = abs(q)
            r = e
            e = rat
            if (abs(p) < abs(0.5 * q * r)) and (p > q * (a - xf)) and (p < q * (b - xf)):
                rat = (p + 0.0) / q
                x = xf + rat
                if ((x - a) < tol2) or ((b - x) < tol2):
                    si = 1.0 if (xm - xf) >= 0 else -1.0
                    rat = tol1 * si
            else:
                golden = 1
        if golden:
            if xf >= xm:
                e = a - xf
            else:
                e = b - xf
            rat = golden_mean * e
        si = 1.0 if rat >= 0 else -1.0
        x = xf + si * max(abs(rat), tol1)
        fu = float(func(x, *args))
        num += 1
        if fu <= fx:
            if x >= xf:
                a = xf
            else:
                b = xf
            fulc = nfc
            ffulc = fnfc
            nfc = xf
            fnfc = fx
            xf = x
            fx = fu
        else:
            if x < xf:
                a = x
            else:
                b = x
            if (fu <= fnfc) or (nfc == xf):
                fulc = nfc
                ffulc = fnfc
                nfc = x
                fnfc = fu
            elif (fu <= ffulc) or (fulc == xf) or (fulc == nfc):
                fulc = x
                ffulc = fu
        xm = 0.5 * (a + b)
        tol1 = sqrt_eps * abs(xf) + xatol / 3.0
        tol2 = 2.0 * tol1
        if num >= maxfun:
            flag = 1
            break
    return xf, fx, flag, num


def fminbound(func, x1, x2, args=(), xtol=1e-5, maxfun=500, full_output=0, disp=1):
    xf, fx, flag, num = _fminbound_core(func, x1, x2, args, xtol, maxfun)
    if full_output:
        return xf, fx, flag, num
    return xf


def _golden_core(func, args, brack, tol, maxiter):
    _gold = 1.618034
    _grat = 2.0 - _gold
    _tol = 1.0 / (_gold ** 2) * 0 + tol
    if brack is None:
        xa, xb, xc, fa, fb, fc, funcalls = bracket(func, args=args)
    elif len(brack) == 2:
        xa, xb, xc, fa, fb, fc, funcalls = bracket(func, xa=brack[0], xb=brack[1], args=args)
    else:
        xa, xb, xc = brack
        funcalls = 0
    _gR = 0.61803399
    _gC = 1.0 - _gR
    x3 = xc
    x0 = xa
    if abs(xc - xb) > abs(xb - xa):
        x1 = xb
        x2 = xb + _gC * (xc - xb)
    else:
        x2 = xb
        x1 = xb - _gC * (xb - xa)
    f1 = float(func(x1, *args))
    f2 = float(func(x2, *args))
    funcalls += 2
    nit = 0
    for i in range(maxiter):
        if abs(x3 - x0) <= _tol * (abs(x1) + abs(x2)):
            break
        if f2 < f1:
            x0 = x1
            x1 = x2
            x2 = _gR * x1 + _gC * x3
            f1 = f2
            f2 = float(func(x2, *args))
        else:
            x3 = x2
            x2 = x1
            x1 = _gR * x2 + _gC * x0
            f2 = f1
            f1 = float(func(x1, *args))
        funcalls += 1
        nit += 1
    if f1 < f2:
        return x1, f1, nit, funcalls
    return x2, f2, nit, funcalls


def golden(func, args=(), brack=None, tol=1.4901161193847656e-08, full_output=0, maxiter=5000):
    x, fx, it, nf = _golden_core(func, args, brack, tol, maxiter)
    if full_output:
        return x, fx, nf
    return x


def minimize_scalar(fun, bracket=None, bounds=None, args=(), method=None, tol=None, options=None):
    if options is None:
        options = {}
    if method is None:
        method = "brent" if bounds is None else "bounded"
    m = method.lower()
    if m == "bounded":
        if bounds is None or len(bounds) != 2:
            raise ValueError("The `bounds` parameter is mandatory for method `bounded`.")
        xatol = options.get("xatol", 1e-5 if tol is None else tol)
        x, fx, flag, num = _fminbound_core(fun, bounds[0], bounds[1], args, xatol, options.get("maxiter", 500))
        msg = "Solution found." if flag == 0 else "Maximum number of function calls reached."
        return OptimizeResult(fun=fx, x=x, nit=num, nfev=num, success=(flag == 0), message=msg, status=flag)
    xtol = options.get("xtol", 1.48e-8 if tol is None else tol)
    if m == "golden":
        xtol = options.get("xtol", 1.4901161193847656e-08 if tol is None else tol)
        x, fx, it, nf = _golden_core(fun, args, bracket, xtol, options.get("maxiter", 5000))
        return OptimizeResult(fun=fx, x=x, nit=it, nfev=nf, success=True)
    if m != "brent":
        raise ValueError("Unknown solver " + method)
    x, fx, it, nf = _brent_core(fun, args, bracket, xtol, options.get("maxiter", 500))
    return OptimizeResult(fun=fx, x=x, nit=it, nfev=nf, success=(it < options.get("maxiter", 500)))


# ---------------------------------------------------------------------------
# scalar root finding
# ---------------------------------------------------------------------------

class RootResults:
    def __init__(self, root, iterations, function_calls, flag):
        self.root = root
        self.iterations = iterations
        self.function_calls = function_calls
        self.converged = flag == "converged"
        self.flag = flag

    def __repr__(self):
        return ("      converged: " + str(self.converged) + "\n           flag: " + self.flag +
                "\n function_calls: " + str(self.function_calls) + "\n     iterations: " +
                str(self.iterations) + "\n           root: " + repr(self.root))


def _wrap_root(r, it, fc, flag, full_output):
    if full_output:
        return r, RootResults(r, it, fc, flag)
    return r


def _sgn(v):
    return v < 0 or (v == 0 and 1.0 / v < 0 if v == 0 else False)


def brentq(f, a, b, args=(), xtol=2e-12, rtol=8.881784197001252e-16, maxiter=100, full_output=False, disp=True):
    if xtol <= 0:
        raise ValueError("xtol too small (%g <= 0)" % xtol)
    xpre = float(a)
    xcur = float(b)
    xblk = 0.0
    fblk = 0.0
    spre = 0.0
    scur = 0.0
    fpre = float(f(xpre, *args))
    fcur = float(f(xcur, *args))
    funcalls = 2
    if fpre == 0:
        return _wrap_root(xpre, 0, funcalls, "converged", full_output)
    if fcur == 0:
        return _wrap_root(xcur, 0, funcalls, "converged", full_output)
    if (fpre < 0) == (fcur < 0):
        raise ValueError("f(a) and f(b) must have different signs")
    iters = 0
    for i in range(maxiter):
        iters += 1
        if fpre != 0 and fcur != 0 and ((fpre < 0) != (fcur < 0)):
            xblk = xpre
            fblk = fpre
            spre = xcur - xpre
            scur = spre
        if abs(fblk) < abs(fcur):
            xpre = xcur
            xcur = xblk
            xblk = xpre
            fpre = fcur
            fcur = fblk
            fblk = fpre
        delta = (xtol + rtol * abs(xcur)) / 2.0
        sbis = (xblk - xcur) / 2.0
        if fcur == 0 or abs(sbis) < delta:
            return _wrap_root(xcur, iters, funcalls, "converged", full_output)
        if abs(spre) > delta and abs(fcur) < abs(fpre):
            if xpre == xblk:
                stry = -fcur * (xcur - xpre) / (fcur - fpre)
            else:
                dpre = (fpre - fcur) / (xpre - xcur)
                dblk = (fblk - fcur) / (xblk - xcur)
                stry = -fcur * (fblk * dblk - fpre * dpre) / (dblk * dpre * (fblk - fpre))
            if 2.0 * abs(stry) < min(abs(spre), 3.0 * abs(sbis) - delta):
                spre = scur
                scur = stry
            else:
                spre = sbis
                scur = sbis
        else:
            spre = sbis
            scur = sbis
        xpre = xcur
        fpre = fcur
        if abs(scur) > delta:
            xcur += scur
        else:
            xcur += delta if sbis > 0 else -delta
        fcur = float(f(xcur, *args))
        funcalls += 1
    if disp:
        raise RuntimeError("Failed to converge after %d iterations." % maxiter)
    return _wrap_root(xcur, iters, funcalls, "convergence error", full_output)


brenth = brentq
toms748 = brentq


def bisect(f, a, b, args=(), xtol=2e-12, rtol=8.881784197001252e-16, maxiter=100, full_output=False, disp=True):
    xa = float(a)
    xb = float(b)
    fa = float(f(xa, *args))
    fb = float(f(xb, *args))
    funcalls = 2
    if fa * fb > 0:
        raise ValueError("f(a) and f(b) must have different signs")
    if fa == 0:
        return _wrap_root(xa, 0, funcalls, "converged", full_output)
    if fb == 0:
        return _wrap_root(xb, 0, funcalls, "converged", full_output)
    dm = xb - xa
    for i in range(maxiter):
        dm *= 0.5
        xm = xa + dm
        fm = float(f(xm, *args))
        funcalls += 1
        if fm * fa >= 0:
            xa = xm
        if fm == 0 or abs(dm) < xtol + rtol * abs(xm):
            return _wrap_root(xm, i + 1, funcalls, "converged", full_output)
    if disp:
        raise RuntimeError("Failed to converge after %d iterations." % maxiter)
    return _wrap_root(xa, maxiter, funcalls, "convergence error", full_output)


def ridder(f, a, b, args=(), xtol=2e-12, rtol=8.881784197001252e-16, maxiter=100, full_output=False, disp=True):
    xa = float(a)
    xb = float(b)
    fa = float(f(xa, *args))
    fb = float(f(xb, *args))
    funcalls = 2
    if fa * fb > 0:
        raise ValueError("f(a) and f(b) must have different signs")
    if fa == 0:
        return _wrap_root(xa, 0, funcalls, "converged", full_output)
    if fb == 0:
        return _wrap_root(xb, 0, funcalls, "converged", full_output)
    tol = xtol + rtol * abs(xa)
    xn = xa
    for i in range(maxiter):
        dm = 0.5 * (xb - xa)
        xm = xa + dm
        fm = float(f(xm, *args))
        sg = 1.0 if (fb - fa) >= 0 else -1.0
        dn = sg * dm * fm / math.sqrt(fm * fm - fa * fb)
        xn = xm - (1.0 if dn >= 0 else -1.0) * min(abs(dn), abs(dm) - 0.5 * tol)
        fn = float(f(xn, *args))
        funcalls += 2
        if fn * fm < 0.0:
            xa = xn
            fa = fn
            xb = xm
            fb = fm
        elif fn * fa < 0.0:
            xb = xn
            fb = fn
        else:
            xa = xn
            fa = fn
        tol = xtol + rtol * xn
        if fn == 0.0 or abs(xb - xa) < tol:
            return _wrap_root(xn, i + 1, funcalls, "converged", full_output)
    if disp:
        raise RuntimeError("Failed to converge after %d iterations." % maxiter)
    return _wrap_root(xn, maxiter, funcalls, "convergence error", full_output)


def _isclose(a, b, rtol, atol):
    return abs(a - b) <= atol + rtol * abs(b)


def newton(func, x0, fprime=None, args=(), tol=1.48e-8, maxiter=50, fprime2=None, x1=None, rtol=0.0,
           full_output=False, disp=True):
    if tol <= 0:
        raise ValueError("tol too small (%g <= 0)" % tol)
    if isinstance(x0, (list, tuple, np.ndarray)):
        res = [newton(func, v, fprime, args, tol, maxiter, fprime2, x1, rtol, False, disp) for v in tolist(x0)]
        return np.array(res)
    p0 = float(x0)
    funcalls = 0
    itr = 0
    if fprime is not None:
        for itr in range(maxiter):
            fval = float(func(p0, *args))
            funcalls += 1
            if fval == 0:
                return _wrap_root(p0, itr, funcalls, "converged", full_output)
            fder = float(fprime(p0, *args))
            funcalls += 1
            if fder == 0:
                return _wrap_root(p0, itr + 1, funcalls, "convergence error", full_output)
            step = fval / fder
            if fprime2 is not None:
                fder2 = float(fprime2(p0, *args))
                funcalls += 1
                step = step / (1.0 - 0.5 * step * fder2 / fder)
            p = p0 - step
            if _isclose(p, p0, rtol, tol):
                return _wrap_root(p, itr + 1, funcalls, "converged", full_output)
            p0 = p
    else:
        if x1 is not None:
            if x1 == x0:
                raise ValueError("x1 and x0 must be different")
            p1 = float(x1)
        else:
            eps = 1e-4
            p1 = p0 * (1 + eps)
            p1 += eps if p1 >= 0 else -eps
        q0 = float(func(p0, *args))
        q1 = float(func(p1, *args))
        funcalls = 2
        if abs(q1) < abs(q0):
            p0, p1, q0, q1 = p1, p0, q1, q0
        for itr in range(maxiter):
            if q1 == q0:
                p = (p1 + p0) / 2.0
                return _wrap_root(p, itr + 1, funcalls, "converged", full_output)
            if abs(q1) > abs(q0):
                p = (-q0 / q1 * p1 + p0) / (1 - q0 / q1)
            else:
                p = (-q1 / q0 * p0 + p1) / (1 - q1 / q0)
            if _isclose(p, p1, rtol, tol):
                return _wrap_root(p, itr + 1, funcalls, "converged", full_output)
            p0 = p1
            q0 = q1
            p1 = p
            q1 = float(func(p1, *args))
            funcalls += 1
    if disp:
        raise RuntimeError("Failed to converge after %d iterations, value is %s." % (maxiter, repr(p0)))
    return _wrap_root(p0, maxiter, funcalls, "convergence error", full_output)


def root_scalar(f, args=(), method=None, bracket=None, fprime=None, fprime2=None, x0=None, x1=None,
                xtol=None, rtol=None, maxiter=None, options=None):
    if not isinstance(args, tuple):
        args = (args,)
    if method is None:
        if bracket is not None:
            method = "brentq"
        elif x0 is not None:
            if fprime2 is not None:
                method = "halley"
            elif fprime is not None:
                method = "newton"
            else:
                method = "secant"
        else:
            raise ValueError("Unable to select a solver as neither bracket nor starting point provided.")
    kw = {}
    if xtol is not None:
        kw["xtol"] = xtol
    if rtol is not None:
        kw["rtol"] = rtol
    if maxiter is not None:
        kw["maxiter"] = maxiter
    m = method.lower()
    counter = [0]

    def fc(x, *a):
        counter[0] += 1
        return f(x, *a)
    try:
        if m in ("brentq", "brenth", "toms748", "bisect", "ridder"):
            fn = {"brentq": brentq, "brenth": brentq, "toms748": brentq, "bisect": bisect, "ridder": ridder}[m]
            r, rr = fn(f, bracket[0], bracket[1], args=args, full_output=True, **kw)
            return rr
        tol = {}
        if xtol is not None:
            tol["tol"] = xtol
        if rtol is not None:
            tol["rtol"] = rtol
        if maxiter is not None:
            tol["maxiter"] = maxiter
        if m in ("newton", "secant", "halley"):
            fp = fprime if m != "secant" else None
            fp2 = fprime2 if m == "halley" else None
            if m == "halley" and fp2 is None:
                raise ValueError("fprime2 must be specified for halley")
            r, rr = newton(f, x0, fprime=fp, args=args, fprime2=fp2, x1=x1, full_output=True, **tol)
            return rr
    except RuntimeError as e:
        return RootResults(nan, 0, counter[0], "convergence error")
    raise ValueError("Unknown solver " + method)


# ---------------------------------------------------------------------------
# multivariate minimisation
# ---------------------------------------------------------------------------

class _Problem:
    """objective with counting, optional analytic gradient and 2-point numeric gradient"""

    def __init__(self, fun, args, jac, lb=None, ub=None, eps=_SQRT_EPS, central=False):
        self.central = central
        self.fun = fun
        self.args = args
        self.jac = jac
        self.nfev = 0
        self.njev = 0
        self.lb = lb
        self.ub = ub
        self.eps = eps
        self.cache_x = None
        self.cache_f = None
        self.cache_g = None

    def f(self, x):
        self.nfev += 1
        v = self.fun(np.array(x), *self.args)
        if self.jac is True:
            self.cache_x = list(x)
            self.cache_g = _vec(v[1])
            return _fval(v[0])
        return _fval(v)

    def g(self, x, fx=None):
        if self.jac is True:
            if self.cache_x == list(x):
                self.njev += 1
                return list(self.cache_g)
            self.f(x)
            self.njev += 1
            return list(self.cache_g)
        if callable(self.jac):
            self.njev += 1
            return _vec(self.jac(np.array(x), *self.args))
        if fx is None:
            fx = self.f(x)
        n = len(x)
        g = [0.0] * n
        h0 = self.eps
        if self.central:
            for i in range(n):
                h = 1e-6 * max(1.0, abs(x[i]))
                xp = list(x)
                xm = list(x)
                xp[i] = x[i] + h
                xm[i] = x[i] - h
                g[i] = (self.f(xp) - self.f(xm)) / (xp[i] - xm[i])
            return g
        for i in range(n):
            xi = x[i]
            h = h0
            if self.ub is not None and xi + h > self.ub[i]:
                h = -h0
            xp = list(x)
            xp[i] = xi + h
            dx = xp[i] - xi
            if dx == 0.0:
                h = h0 * max(1.0, abs(xi))
                xp[i] = xi + h
                dx = xp[i] - xi
            g[i] = (self.f(xp) - fx) / dx
        self.njev += n if False else 0
        return g


def _wolfe(phi, dphi, f0, g0, alpha1, c1=1e-4, c2=0.9, amax=1e100):
    """strong Wolfe line search; returns (alpha, f(alpha)) or None"""
    a_prev = 0.0
    f_prev = f0
    g_prev = g0
    a = alpha1
    for i in range(40):
        fa = phi(a)
        if fa > f0 + c1 * a * g0 or (i > 0 and fa >= f_prev):
            return _zoom(phi, dphi, f0, g0, a_prev, a, f_prev, fa, g_prev, c1, c2)
        ga = dphi(a)
        if abs(ga) <= -c2 * g0:
            return (a, fa)
        if ga >= 0:
            return _zoom(phi, dphi, f0, g0, a, a_prev, fa, f_prev, ga, c1, c2)
        a_prev = a
        f_prev = fa
        g_prev = ga
        a = min(2.0 * a, amax)
        if a >= amax:
            break
    return None


def _zoom(phi, dphi, f0, g0, lo, hi, flo, fhi, glo, c1, c2):
    best = None
    for j in range(40):
        d = hi - lo
        den = 2.0 * (fhi - flo - glo * d)
        if den != 0.0:
            a = lo - glo * d * d / den
        else:
            a = lo + 0.5 * d
        lo_b = min(lo, hi)
        hi_b = max(lo, hi)
        margin = 0.1 * abs(d)
        if not (a > lo_b + margin and a < hi_b - margin):
            a = lo + 0.5 * d
        fa = phi(a)
        if fa > f0 + c1 * a * g0 or fa >= flo:
            hi = a
            fhi = fa
        else:
            ga = dphi(a)
            if abs(ga) <= -c2 * g0:
                return (a, fa)
            if ga * (hi - lo) >= 0:
                hi = lo
                fhi = flo
            lo = a
            flo = fa
            glo = ga
            best = (a, fa)
        if abs(hi - lo) < 1e-16 * max(abs(lo), 1e-300):
            break
    if best is not None and best[1] < f0:
        return best
    return None


def _clip(x, lb, ub):
    return [min(max(x[i], lb[i]), ub[i]) for i in range(len(x))]


def _bfgs(prob, x0, gtol, maxiter, callback, lb=None, ub=None, norm_inf=True):
    n = len(x0)
    bounded = lb is not None
    xk = list(x0)
    if bounded:
        xk = _clip(xk, lb, ub)
    f0 = prob.f(xk)
    gfk = prob.g(xk, f0)
    k = 0
    I = _la.eye(n)
    Hk = _la.eye(n)
    old_fval = f0
    old_old_fval = old_fval + _la.norm2(gfk) / 2.0

    def pg_norm(x, g):
        if not bounded:
            return _la.norm_inf(g) if norm_inf else _la.norm2(g)
        m = 0.0
        for i in range(n):
            pg = x[i] - min(max(x[i] - g[i], lb[i]), ub[i])
            if abs(pg) > m:
                m = abs(pg)
        return m
    gnorm = pg_norm(xk, gfk)
    warnflag = 0
    free_prev = None
    while gnorm > gtol and k < maxiter:
        if bounded:
            free = [i for i in range(n) if not ((xk[i] <= lb[i] and gfk[i] > 0) or (xk[i] >= ub[i] and gfk[i] < 0))]
            if free != free_prev:
                Hk = _la.eye(n)
                free_prev = free
            pk = [0.0] * n
            for i in free:
                s = 0.0
                for j in free:
                    s += Hk[i][j] * gfk[j]
                pk[i] = -s
        else:
            pk = [-v for v in _la.matvec(Hk, gfk)]
        gp = _la.dot(gfk, pk)
        if gp >= 0:
            Hk = _la.eye(n)
            pk = [-v for v in gfk]
            if bounded:
                pk = [(-gfk[i] if i in free_prev else 0.0) for i in range(n)]
            gp = _la.dot(gfk, pk)
            if gp >= 0:
                break
        store = {}
        x_cur = xk

        def phi(a):
            xn = [x_cur[i] + a * pk[i] for i in range(n)]
            if bounded:
                xn = _clip(xn, lb, ub)
            v = prob.f(xn)
            store[a] = (xn, v)
            return v

        def dphi(a):
            xn = store[a][0]
            g = prob.g(xn, store[a][1])
            store[("g", a)] = g
            if bounded:
                return _la.dot(g, [(xn[i] - x_cur[i]) / a for i in range(n)])
            return _la.dot(g, pk)
        if k == 0:
            alpha1 = min(1.0, 1.01 * 2.0 * (old_fval - old_old_fval) / gp)
        else:
            alpha1 = min(1.0, 1.01 * 2.0 * (old_fval - old_old_fval) / gp)
        if alpha1 <= 0:
            alpha1 = 1.0
        if bounded:
            # Armijo backtracking along the projected path (the curvature condition is not used)
            a = 1.0 if k > 0 else min(1.0, 1.0 / max(_la.norm2(gfk), 1e-10))
            res = None
            for it in range(60):
                fa = phi(a)
                xn = store[a][0]
                dec = fa - old_fval
                if dec <= 1e-4 * _la.dot(gfk, [xn[i] - xk[i] for i in range(n)]):
                    res = (a, fa)
                    break
                a *= 0.5
        else:
            res = _wolfe(phi, dphi, old_fval, gp, alpha1)
        if res is None:
            warnflag = 2
            break
        alpha_k = res[0]
        xkp1 = store[alpha_k][0]
        fnew = store[alpha_k][1]
        gkey = ("g", alpha_k)
        gfkp1 = store[gkey] if gkey in store else prob.g(xkp1, fnew)
        old_old_fval = old_fval
        old_fval = fnew
        sk = [xkp1[i] - xk[i] for i in range(n)]
        xk = xkp1
        yk = [gfkp1[i] - gfk[i] for i in range(n)]
        gfk = gfkp1
        k += 1
        if callback is not None:
            callback(np.array(xk))
        gnorm = pg_norm(xk, gfk)
        if gnorm <= gtol:
            break
        if not (abs(old_fval) < inf):
            warnflag = 2
            break
        rhok_inv = _la.dot(yk, sk)
        if rhok_inv == 0.0:
            rhok = 1000.0
        else:
            rhok = 1.0 / rhok_inv
        if rhok_inv > 0 or not bounded:
            A1 = [[I[i][j] - sk[i] * yk[j] * rhok for j in range(n)] for i in range(n)]
            A2 = [[I[i][j] - yk[i] * sk[j] * rhok for j in range(n)] for i in range(n)]
            Hk = _la.matmul(A1, _la.matmul(Hk, A2))
            for i in range(n):
                for j in range(n):
                    Hk[i][j] += rhok * sk[i] * sk[j]
    return xk, old_fval, gfk, Hk, k, warnflag, gnorm


def _cg(prob, x0, gtol, maxiter, callback, lb=None, ub=None):
    n = len(x0)
    xk = list(x0)
    f0 = prob.f(xk)
    gfk = prob.g(xk, f0)
    old_fval = f0
    old_old_fval = f0 + _la.norm2(gfk) / 2.0
    pk = [-v for v in gfk]
    gnorm = _la.norm_inf(gfk)
    k = 0
    warnflag = 0
    while gnorm > gtol and k < maxiter:
        gp = _la.dot(gfk, pk)
        if gp >= 0:
            pk = [-v for v in gfk]
            gp = _la.dot(gfk, pk)
        store = {}
        x_cur = xk

        def phi(a):
            xn = [x_cur[i] + a * pk[i] for i in range(n)]
            v = prob.f(xn)
            store[a] = (xn, v)
            return v

        def dphi(a):
            xn = store[a][0]
            g = prob.g(xn, store[a][1])
            store[("g", a)] = g
            return _la.dot(g, pk)
        alpha1 = min(1.0, 1.01 * 2.0 * (old_fval - old_old_fval) / gp)
        if alpha1 <= 0:
            alpha1 = 1.0
        res = _wolfe(phi, dphi, old_fval, gp, alpha1, 1e-4, 0.4)
        if res is None:
            warnflag = 2
            break
        a = res[0]
        xkp1 = store[a][0]
        old_old_fval = old_fval
        old_fval = store[a][1]
        gkey = ("g", a)
        gnew = store[gkey] if gkey in store else prob.g(xkp1, old_fval)
        denom = _la.dot(gfk, gfk)
        beta = max(0.0, _la.dot(gnew, [gnew[i] - gfk[i] for i in range(n)]) / denom) if denom > 0 else 0.0
        pk = [-gnew[i] + beta * pk[i] for i in range(n)]
        xk = xkp1
        gfk = gnew
        k += 1
        if callback is not None:
            callback(np.array(xk))
        gnorm = _la.norm_inf(gfk)
    return xk, old_fval, gfk, k, warnflag, gnorm


def _nelder_mead(f, x0, xatol, fatol, maxiter, maxfev, adaptive, initial_simplex, lb, ub, callback):
    N = len(x0)
    rho = 1.0
    chi = 2.0
    psi = 0.5
    sigma = 0.5
    if adaptive:
        rho = 1.0
        chi = 1.0 + 2.0 / N
        psi = 0.75 - 1.0 / (2.0 * N)
        sigma = 1.0 - 1.0 / N
    nfev = [0]

    def fe(x):
        nfev[0] += 1
        return f(x)
    if lb is not None:
        x0 = _clip(x0, lb, ub)
    if initial_simplex is None:
        sim = [list(x0)]
        for k in range(N):
            y = list(x0)
            if y[k] != 0:
                y[k] = (1.0 + 0.05) * y[k]
            else:
                y[k] = 0.00025
            sim.append(y)
    else:
        sim = [list(r) for r in initial_simplex]
    if lb is not None:
        sim = [_clip(r, lb, ub) for r in sim]
    fsim = [fe(r) for r in sim]
    ind = sorted(range(N + 1), key=lambda i: fsim[i])
    sim = [sim[i] for i in ind]
    fsim = [fsim[i] for i in ind]
    iterations = 1
    while nfev[0] < maxfev and iterations < maxiter:
        a = 0.0
        for r in range(1, N + 1):
            for c in range(N):
                a = max(a, abs(sim[r][c] - sim[0][c]))
        b = 0.0
        for r in range(1, N + 1):
            b = max(b, abs(fsim[0] - fsim[r]))
        if a <= xatol and b <= fatol:
            break
        xbar = [sum([sim[r][c] for r in range(N)]) / N for c in range(N)]
        xr = [(1 + rho) * xbar[c] - rho * sim[-1][c] for c in range(N)]
        if lb is not None:
            xr = _clip(xr, lb, ub)
        fxr = fe(xr)
        doshrink = 0
        if fxr < fsim[0]:
            xe = [(1 + rho * chi) * xbar[c] - rho * chi * sim[-1][c] for c in range(N)]
            if lb is not None:
                xe = _clip(xe, lb, ub)
            fxe = fe(xe)
            if fxe < fxr:
                sim[-1] = xe
                fsim[-1] = fxe
            else:
                sim[-1] = xr
                fsim[-1] = fxr
        else:
            if fxr < fsim[-2]:
                sim[-1] = xr
                fsim[-1] = fxr
            else:
                if fxr < fsim[-1]:
                    xc = [(1 + psi * rho) * xbar[c] - psi * rho * sim[-1][c] for c in range(N)]
                    if lb is not None:
                        xc = _clip(xc, lb, ub)
                    fxc = fe(xc)
                    if fxc <= fxr:
                        sim[-1] = xc
                        fsim[-1] = fxc
                    else:
                        doshrink = 1
                else:
                    xcc = [(1 - psi) * xbar[c] + psi * sim[-1][c] for c in range(N)]
                    if lb is not None:
                        xcc = _clip(xcc, lb, ub)
                    fxcc = fe(xcc)
                    if fxcc < fsim[-1]:
                        sim[-1] = xcc
                        fsim[-1] = fxcc
                    else:
                        doshrink = 1
                if doshrink:
                    for j in range(1, N + 1):
                        sim[j] = [sim[0][c] + sigma * (sim[j][c] - sim[0][c]) for c in range(N)]
                        if lb is not None:
                            sim[j] = _clip(sim[j], lb, ub)
                        fsim[j] = fe(sim[j])
        iterations += 1
        ind = sorted(range(N + 1), key=lambda i: fsim[i])
        sim = [sim[i] for i in ind]
        fsim = [fsim[i] for i in ind]
        if callback is not None:
            callback(np.array(sim[0]))
    return sim[0], fsim[0], iterations, nfev[0], sim, fsim


def _powell(prob, x0, xtol, ftol, maxiter, maxfev, lb, ub):
    n = len(x0)
    x = list(x0)
    fx = prob.f(x)
    direc = _la.eye(n)
    it = 0

    def line_min(p, d):
        def g(a):
            return prob.f([p[i] + a * d[i] for i in range(n)])
        # bracket + brent on the line
        xa, xb, xc, fa, fb, fc, nf = bracket(g)
        a, fa_, _i, _n = _brent_core(g, (), (xa, xb, xc), 1e-7 if True else 0, 500)
        return a, fa_
    while it < maxiter and prob.nfev < maxfev:
        it += 1
        fx0 = fx
        x0_ = list(x)
        bigind = 0
        delta = 0.0
        for i in range(n):
            d = direc[i]
            fold = fx
            a, fnew = line_min(x, d)
            x = [x[j] + a * d[j] for j in range(n)]
            fx = fnew
            if fold - fx > delta:
                delta = fold - fx
                bigind = i
        if 2.0 * (fx0 - fx) <= ftol * (abs(fx0) + abs(fx)) + 1e-20:
            break
        d = [x[j] - x0_[j] for j in range(n)]
        xe = [2.0 * x[j] - x0_[j] for j in range(n)]
        fe_ = prob.f(xe)
        if fe_ < fx0:
            t = 2.0 * (fx0 - 2.0 * fx + fe_) * (fx0 - fx - delta) ** 2 - delta * (fx0 - fe_) ** 2
            if t < 0.0:
                a, fnew = line_min(x, d)
                x = [x[j] + a * d[j] for j in range(n)]
                fx = fnew
                direc[bigind] = direc[n - 1]
                direc[n - 1] = d
    return x, fx, it


def _normalize_constraints(constraints):
    out = []
    if constraints is None:
        return out
    if isinstance(constraints, dict) or isinstance(constraints, (LinearConstraint, NonlinearConstraint)):
        constraints = [constraints]
    for c in constraints:
        if isinstance(c, dict):
            args = c.get("args", ())
            fn = c["fun"]
            out.append((c["type"], (lambda x, fn=fn, args=args: _vec(fn(x, *args)))))
        elif isinstance(c, LinearConstraint):
            A = np.asarray(c.A).tolist()
            if not isinstance(A[0], list):
                A = [A]
            lbv = tolist(c.lb) if isinstance(c.lb, (list, tuple, np.ndarray)) else [float(c.lb)] * len(A)
            ubv = tolist(c.ub) if isinstance(c.ub, (list, tuple, np.ndarray)) else [float(c.ub)] * len(A)
            for i in range(len(A)):
                row = [float(v) for v in A[i]]
                if lbv[i] == ubv[i]:
                    out.append(("eq", (lambda x, row=row, v=lbv[i]: [_la.dot(row, _vec(x)) - v])))
                else:
                    if lbv[i] > -inf:
                        out.append(("ineq", (lambda x, row=row, v=lbv[i]: [_la.dot(row, _vec(x)) - v])))
                    if ubv[i] < inf:
                        out.append(("ineq", (lambda x, row=row, v=ubv[i]: [v - _la.dot(row, _vec(x))])))
        elif isinstance(c, NonlinearConstraint):
            fn = c.fun
            lbv = c.lb
            ubv = c.ub
            if lbv == ubv:
                out.append(("eq", (lambda x, fn=fn, v=lbv: [a - v for a in _vec(fn(x))])))
            else:
                if lbv > -inf:
                    out.append(("ineq", (lambda x, fn=fn, v=lbv: [a - v for a in _vec(fn(x))])))
                if ubv < inf:
                    out.append(("ineq", (lambda x, fn=fn, v=ubv: [v - a for a in _vec(fn(x))])))
    return out


def _auglag(prob, x0, cons, lb, ub, tol, maxiter):
    """augmented Lagrangian with BFGS inner solves for eq / ineq constraints"""
    n = len(x0)
    x = list(x0)
    # evaluate constraint sizes
    sizes = []
    for typ, fn in cons:
        sizes.append(len(fn(np.array(x))))
    lam = [[0.0] * s for s in sizes]
    mu = 10.0
    total_nit = 0
    viol_prev = inf
    nfev0 = 0
    for outer in range(60):
        lam_cur = lam
        mu_cur = mu

        def fl(xx, fun=prob):
            v = fun.fun(np.array(xx), *fun.args)
            v = _fval(v[0]) if fun.jac is True else _fval(v)
            fun.nfev += 1
            for ci in range(len(cons)):
                typ, fn = cons[ci]
                cv = fn(np.array(xx))
                for j in range(len(cv)):
                    if typ == "eq":
                        v += lam_cur[ci][j] * cv[j] + 0.5 * mu_cur * cv[j] * cv[j]
                    else:
                        t = max(0.0, lam_cur[ci][j] - mu_cur * cv[j])
                        v += (t * t - lam_cur[ci][j] ** 2) / (2.0 * mu_cur)
            return v
        sub = _Problem(lambda xx: fl(list(xx)), (), None, lb, ub, central=True)
        xs, fv, gk, Hk, k, wf, gn = _bfgs(sub, x, 1e-9 if tol is None else max(tol * 0.01, 1e-12), 500, None, lb, ub)
        total_nit += k
        x = xs
        viol = 0.0
        for ci in range(len(cons)):
            typ, fn = cons[ci]
            cv = fn(np.array(x))
            for j in range(len(cv)):
                if typ == "eq":
                    lam[ci][j] += mu * cv[j]
                    viol = max(viol, abs(cv[j]))
                else:
                    lam[ci][j] = max(0.0, lam[ci][j] - mu * cv[j])
                    viol = max(viol, max(0.0, -cv[j]))
        if viol < 1e-10:
            break
        if viol > 0.25 * viol_prev:
            mu *= 10.0
        viol_prev = viol
        if mu > 1e12:
            break
    return x, total_nit, viol


def minimize(fun, x0, args=(), method=None, jac=None, hess=None, hessp=None, bounds=None, constraints=(),
             tol=None, callback=None, options=None):
    if not isinstance(args, tuple):
        args = (args,)
    if options is None:
        options = {}
    x0l = _vec(x0)
    n = len(x0l)
    lb = None
    ub = None
    if bounds is not None:
        lb = [-inf] * n
        ub = [inf] * n
        if isinstance(bounds, Bounds):
            lbv = tolist(bounds.lb)
            ubv = tolist(bounds.ub)
            for i in range(n):
                lb[i] = lbv[i] if len(lbv) > 1 else lbv[0]
                ub[i] = ubv[i] if len(ubv) > 1 else ubv[0]
        else:
            for i in range(n):
                lo, hi = bounds[i]
                if lo is not None:
                    lb[i] = float(lo)
                if hi is not None:
                    ub[i] = float(hi)
    cons = _normalize_constraints(constraints)
    if method is None:
        method = "SLSQP" if cons else ("L-BFGS-B" if bounds is not None else "BFGS")
    m = method.lower()
    if callable(m):
        raise NotImplementedError("custom minimizer")
    if jac is not None and not callable(jac) and jac is not True:
        jac = None
    eps = options.get("eps", _SQRT_EPS)
    prob = _Problem(fun, args, jac, lb, ub, eps)
    if cons and m not in ("slsqp", "cobyla", "trust-constr", "cobyqa"):
        raise ValueError("Constraints only available for 'COBYLA', 'SLSQP', 'trust-constr' and 'COBYQA'")
    if cons:
        x, nit, viol = _auglag(prob, x0l, cons, lb, ub, tol, options.get("maxiter", 100))
        fx = prob.f(x)
        gk = prob.g(x, fx)
        ok = viol < 1e-6
        return OptimizeResult(message="Optimization terminated successfully" if ok else
                              "Positive directional derivative for linesearch" if False else "Did not converge",
                              success=ok, status=0 if ok else 1, fun=fx, x=np.array(x), nit=nit, jac=np.array(gk),
                              nfev=prob.nfev, njev=nit)
    maxiter = options.get("maxiter", None)
    if m == "nelder-mead":
        xatol = options.get("xatol", 1e-4)
        fatol = options.get("fatol", 1e-4)
        if tol is not None:
            xatol = tol
            fatol = tol
        mi = maxiter if maxiter is not None else n * 200
        mf = options.get("maxfev", None)
        if mf is None:
            mf = n * 200 if maxiter is None else inf
        if maxiter is None and options.get("maxfev", None) is not None:
            mi = inf
        isim = options.get("initial_simplex", None)
        if isim is not None:
            isim = np.asarray(isim).tolist()
        x, fx, it, nf, sim, fsim = _nelder_mead(prob.f, x0l, xatol, fatol, mi, mf, options.get("adaptive", False),
                                                isim, lb, ub, callback)
        if nf >= mf:
            status = 1
            msg = "Maximum number of function evaluations has been exceeded."
        elif it >= mi:
            status = 2
            msg = "Maximum number of iterations has been exceeded."
        else:
            status = 0
            msg = "Optimization terminated successfully."
        return OptimizeResult(message=msg, success=(status == 0), status=status, fun=fx, x=np.array(x), nit=it,
                              nfev=nf, final_simplex=(np.array(sim), np.array(fsim)))
    if m == "powell":
        xt = options.get("xtol", 1e-4 if tol is None else tol)
        ft = options.get("ftol", 1e-4 if tol is None else tol)
        mf = options.get("maxfev", n * 1000)
        x, fx, it = _powell(prob, x0l, xt, ft, maxiter if maxiter is not None else n * 1000, mf, lb, ub)
        if lb is not None:
            x = _clip(x, lb, ub)
            fx = prob.f(x)
        return OptimizeResult(message="Optimization terminated successfully.", success=True, status=0, fun=fx,
                              x=np.array(x), nit=it, nfev=prob.nfev, direc=None)
    if m == "cg":
        gtol = options.get("gtol", 1e-5 if tol is None else tol)
        x, fx, gk, k, wf, gn = _cg(prob, x0l, gtol, maxiter if maxiter is not None else n * 200, callback)
        return _gres(x, fx, gk, k, wf, gn, gtol, maxiter if maxiter is not None else n * 200, prob, None)
    if m in ("l-bfgs-b", "tnc"):
        gtol = options.get("gtol", options.get("pgtol", 1e-5 if tol is None else tol))
        mi = maxiter if maxiter is not None else 15000
        x, fx, gk, Hk, k, wf, gn = _bfgs(prob, x0l, gtol, mi, callback, lb, ub)
        ok = wf == 0 and k < mi
        msg = "CONVERGENCE: NORM OF PROJECTED GRADIENT <= PGTOL" if ok else (
            "STOP: TOTAL NO. OF ITERATIONS REACHED LIMIT" if wf == 0 else "ABNORMAL: ")
        return OptimizeResult(fun=fx, jac=np.array(gk), nfev=prob.nfev, njev=prob.njev if prob.jac else prob.nfev,
                              nit=k, status=0 if ok else (1 if wf == 0 else 2), message=msg, x=np.array(x),
                              success=ok)
    # BFGS (also stands in for Newton-CG, trust-* and the other gradient based methods)
    gtol = options.get("gtol", 1e-5 if tol is None else tol)
    mi = maxiter if maxiter is not None else n * 200
    x, fx, gk, Hk, k, wf, gn = _bfgs(prob, x0l, gtol, mi, callback, lb, ub, options.get("norm", inf) == inf)
    return _gres(x, fx, gk, k, wf, gn, gtol, mi, prob, Hk)


def _gres(x, fx, gk, k, wf, gn, gtol, mi, prob, Hk):
    if wf == 2:
        status = 2
        msg = "Desired error not necessarily achieved due to precision loss."
    elif k >= mi:
        status = 1
        msg = "Maximum number of iterations has been exceeded."
    elif not (fx == fx) or not (gn == gn):
        status = 3
        msg = "NaN result encountered."
    else:
        status = 0
        msg = "Optimization terminated successfully."
    r = OptimizeResult(message=msg, success=(status == 0), status=status, fun=fx, x=np.array(x),
                       nit=k, jac=np.array(gk), nfev=prob.nfev, njev=prob.nfev if not prob.jac else prob.njev)
    if Hk is not None:
        r["hess_inv"] = np.array(Hk)
    return r


def fmin(func, x0, args=(), xtol=1e-4, ftol=1e-4, maxiter=None, maxfun=None, full_output=0, disp=1,
         retall=0, callback=None, initial_simplex=None):
    opts = {"xatol": xtol, "fatol": ftol}
    if maxiter is not None:
        opts["maxiter"] = maxiter
    if maxfun is not None:
        opts["maxfev"] = maxfun
    r = minimize(func, x0, args=args, method="Nelder-Mead", options=opts, callback=callback)
    if full_output:
        return r.x, r.fun, r.nit, r.nfev, r.status
    return r.x


def fmin_bfgs(f, x0, fprime=None, args=(), gtol=1e-5, norm=inf, epsilon=_SQRT_EPS, maxiter=None, full_output=0,
              disp=1, retall=0, callback=None):
    r = minimize(f, x0, args=args, method="BFGS", jac=fprime, callback=callback,
                 options={"gtol": gtol, "eps": epsilon, "maxiter": maxiter} if maxiter else {"gtol": gtol, "eps": epsilon})
    if full_output:
        return r.x, r.fun, r.jac, r.hess_inv, r.nfev, r.njev, r.status
    return r.x


def fmin_powell(func, x0, args=(), xtol=1e-4, ftol=1e-4, maxiter=None, maxfun=None, full_output=0, disp=1):
    r = minimize(func, x0, args=args, method="Powell", options={"xtol": xtol, "ftol": ftol})
    if full_output:
        return r.x, r.fun, None, r.nit, r.nfev, r.status
    return r.x


def fmin_cg(f, x0, fprime=None, args=(), gtol=1e-5, norm=inf, epsilon=_SQRT_EPS, maxiter=None, full_output=0,
            disp=1, retall=0, callback=None):
    r = minimize(f, x0, args=args, method="CG", jac=fprime, options={"gtol": gtol})
    if full_output:
        return r.x, r.fun, r.nfev, r.njev, r.status
    return r.x


def fmin_l_bfgs_b(func, x0, fprime=None, args=(), approx_grad=0, bounds=None, m=10, factr=1e7, pgtol=1e-5,
                  epsilon=1e-8, iprint=-1, maxfun=15000, maxiter=15000, disp=None, callback=None, maxls=20):
    jac = True if (fprime is None and not approx_grad) else fprime
    r = minimize(func, x0, args=args, method="L-BFGS-B", jac=jac, bounds=bounds, options={"gtol": pgtol})
    return r.x, r.fun, {"grad": r.jac, "funcalls": r.nfev, "nit": r.nit, "warnflag": r.status}


def approx_fprime(xk, f, epsilon=_SQRT_EPS, *args):
    x = _vec(xk)
    f0 = _fval(f(np.array(x), *args))
    g = []
    for i in range(len(x)):
        xp = list(x)
        xp[i] += epsilon
        g.append((_fval(f(np.array(xp), *args)) - f0) / epsilon)
    return np.array(g)


def check_grad(func, grad, x0, *args, **kw):
    g1 = _vec(grad(np.array(_vec(x0)), *args))
    g2 = _vec(approx_fprime(x0, func, kw.get("epsilon", _SQRT_EPS), *args))
    return math.sqrt(sum([(a - b) ** 2 for a, b in zip(g1, g2)]))


# ---------------------------------------------------------------------------
# systems of equations
# ---------------------------------------------------------------------------

def _num_jac(fun, x, f0, rel=_SQRT_EPS, lb=None, ub=None):
    n = len(x)
    m = len(f0)
    cols = []
    for j in range(n):
        h = rel * (1.0 if x[j] >= 0 else -1.0) * max(1.0, abs(x[j]))
        xp = list(x)
        xp[j] = x[j] + h
        if lb is not None:
            if xp[j] > ub[j] or xp[j] < lb[j]:
                h = -h
                xp[j] = x[j] + h
        dx = xp[j] - x[j]
        fp = fun(xp)
        cols.append([(fp[i] - f0[i]) / dx for i in range(m)])
    return [[cols[j][i] for j in range(n)] for i in range(m)]


def _newton_system(fun, x0, xtol, maxfev, jacf=None):
    """damped Newton / Levenberg-Marquardt iteration for F(x) = 0"""
    n = len(x0)
    x = list(x0)
    nfev = [0]

    def fe(v):
        nfev[0] += 1
        return fun(v)
    fx = fe(x)
    nrm = _la.norm2(fx)
    lam = 0.0
    it = 0
    ok = False
    while nfev[0] < maxfev:
        it += 1
        if _la.norm_inf(fx) == 0.0:
            ok = True
            break
        J = jacf(x) if jacf is not None else _num_jac(fe, x, fx)
        if jacf is None:
            pass
        # normal equations with damping
        JT = _la.transpose(J)
        A = _la.matmul(JT, J)
        g = _la.matvec(JT, fx)
        improved = False
        for attempt in range(30):
            B = [list(r) for r in A]
            for i in range(n):
                B[i][i] += lam * (A[i][i] if A[i][i] > 0 else 1.0)
            try:
                dx = _la.solve(B, [-v for v in g])
            except ValueError:
                lam = 1e-3 if lam == 0.0 else lam * 10.0
                continue
            xn = [x[i] + dx[i] for i in range(n)]
            fn = fe(xn)
            nn = _la.norm2(fn)
            if nn < nrm or nn == 0.0:
                lam = lam / 10.0 if lam > 1e-12 else 0.0
                step = _la.norm2(dx)
                xs = _la.norm2(xn)
                x = xn
                fx = fn
                nrm = nn
                improved = True
                if step <= 1e-14 * max(xs, 1e-300) + 1e-300:
                    ok = True
                break
            lam = 1e-3 if lam == 0.0 else lam * 10.0
        if not improved:
            ok = nrm <= 1e-10 * (1.0 + _la.norm2(x))
            break
        if ok:
            break
        if nrm <= 1e-15 * (1.0 + _la.norm2(fx)) and False:
            break
        if _la.norm_inf(fx) <= 1e-15:
            ok = True
            break
        if it > 200:
            break
    return x, fx, it, nfev[0], ok


def _make_sys(func, args):
    def fun(xl):
        return _fvec(func(np.array(xl), *args))
    return fun


def fsolve(func, x0, args=(), fprime=None, full_output=0, col_deriv=0, xtol=1.49012e-08, maxfev=0, band=None,
           epsfcn=None, factor=100, diag=None):
    if not isinstance(args, tuple):
        args = (args,)
    x0l = _vec(x0)
    fun = _make_sys(func, args)
    jacf = None
    if fprime is not None:
        jacf = lambda xl: np.asarray(fprime(np.array(xl), *args)).tolist()
    mf = maxfev if maxfev > 0 else 200 * (len(x0l) + 1)
    x, fx, it, nf, ok = _newton_system(fun, x0l, xtol, mf * 20, jacf)
    ier = 1 if ok else 5
    msg = "The solution converged." if ok else "The iteration is not making good progress."
    if full_output:
        return np.array(x), {"nfev": nf, "fvec": np.array(fx)}, ier, msg
    return np.array(x)


def root(fun, x0, args=(), method="hybr", jac=None, tol=None, callback=None, options=None):
    if not isinstance(args, tuple):
        args = (args,)
    x0l = _vec(x0)
    f = _make_sys(fun, args)
    jacf = None
    if callable(jac):
        jacf = lambda xl: np.asarray(jac(np.array(xl), *args)).tolist()
    x, fx, it, nf, ok = _newton_system(f, x0l, 1.49012e-08, 4000 * (len(x0l) + 1), jacf)
    return OptimizeResult(x=np.array(x), success=ok, status=1 if ok else 5, fun=np.array(fx), nfev=nf,
                          message="The solution converged." if ok else
                          "The iteration is not making good progress, as measured by the \n  improvement from the last ten iterations.")


# ---------------------------------------------------------------------------
# least squares / curve fitting
# ---------------------------------------------------------------------------

def _rho(loss, z, fs):
    if loss == "linear":
        return [z, 1.0, 0.0]
    if loss == "huber":
        if z <= 1.0:
            return [z, 1.0, 0.0]
        r = math.sqrt(z)
        return [2.0 * r - 1.0, 1.0 / r, -0.5 / (z * r)]
    if loss == "soft_l1":
        t = 1.0 + z
        r = math.sqrt(t)
        return [2.0 * (r - 1.0), 1.0 / r, -0.5 / (t * r)]
    if loss == "cauchy":
        t = 1.0 + z
        return [math.log(t), 1.0 / t, -1.0 / (t * t)]
    if loss == "arctan":
        return [math.atan(z), 1.0 / (1.0 + z * z), -2.0 * z / (1.0 + z * z) ** 2]
    raise ValueError("`loss` must be one of 'linear', 'soft_l1', 'huber', 'cauchy', 'arctan'")


def least_squares(fun, x0, jac="2-point", bounds=(-inf, inf), method="trf", ftol=1e-8, xtol=1e-8, gtol=1e-8,
                  x_scale=1.0, loss="linear", f_scale=1.0, diff_step=None, tr_solver=None, tr_options=None,
                  jac_sparsity=None, max_nfev=None, verbose=0, args=(), kwargs=None):
    if kwargs is None:
        kwargs = {}
    x = _vec(x0)
    n = len(x)
    lb = [-inf] * n
    ub = [inf] * n
    bounded = False
    if bounds is not None:
        if isinstance(bounds, Bounds):
            bl = bounds.lb
            bu = bounds.ub
        else:
            bl, bu = bounds
        bl = tolist(bl)
        bu = tolist(bu)
        for i in range(n):
            lb[i] = bl[i] if len(bl) > 1 else bl[0]
            ub[i] = bu[i] if len(bu) > 1 else bu[0]
            if lb[i] > -inf or ub[i] < inf:
                bounded = True
    if bounded:
        for i in range(n):
            if lb[i] >= ub[i]:
                raise ValueError("Each lower bound must be strictly less than each upper bound.")
            if not (lb[i] <= x[i] <= ub[i]):
                raise ValueError("`x0` is infeasible.")
    counts = [0, 0]

    def fe(xl):
        counts[0] += 1
        return _fvec(fun(np.array(xl), *args, **kwargs))

    def je(xl, fl):
        counts[1] += 1
        if callable(jac):
            J = np.asarray(jac(np.array(xl), *args, **kwargs))
            if J.ndim == 2:
                return J.tolist()
            if n == 1:
                return [[v] for v in tolist(J)]
            return [tolist(J)]
        rel = _SQRT_EPS if diff_step is None else diff_step
        c0 = counts[0]
        J = _num_jac(fe, xl, fl, rel, lb if bounded else None, ub if bounded else None)
        counts[0] = c0
        return J
    f = fe(x)
    m = len(f)
    if max_nfev is None:
        max_nfev = 100 * n
    robust = loss != "linear"
    c2 = f_scale * f_scale

    def cost_of(fv):
        s = 0.0
        if not robust:
            for v in fv:
                s += v * v
            return 0.5 * s
        for v in fv:
            s += _rho(loss, v * v / c2, f_scale)[0]
        return 0.5 * c2 * s

    cost = cost_of(f)
    J = je(x, f)
    lam = 1e-3
    status = 0
    nit = 0
    while True:
        if robust:
            fs = []
            Js = []
            for i in range(m):
                r = _rho(loss, f[i] * f[i] / c2, f_scale)
                js = r[1] + 2.0 * (r[2] / c2) * c2 * f[i] * f[i] / c2 * 0 + 2.0 * r[2] * f[i] * f[i] / c2
                if js < _EPS:
                    js = _EPS
                js = math.sqrt(js)
                fs.append(f[i] * r[1] / js)
                Js.append([J[i][j] * js for j in range(n)])
        else:
            fs = f
            Js = J
        JT = _la.transpose(Js)
        g = _la.matvec(JT, fs)
        A = _la.matmul(JT, Js)
        free = list(range(n))
        if bounded:
            free = [j for j in range(n) if not ((x[j] <= lb[j] and g[j] > 0) or (x[j] >= ub[j] and g[j] < 0))]
        gnorm = 0.0
        for j in free:
            if abs(g[j]) > gnorm:
                gnorm = abs(g[j])
        if gnorm < gtol:
            status = 1
            break
        if counts[0] >= max_nfev:
            status = 0
            break
        nf = len(free)
        scale = [A[j][j] if A[j][j] > 0 else 1.0 for j in free]
        accepted = False
        tries = 0
        while True:
            tries += 1
            B = [[A[free[a]][free[b]] for b in range(nf)] for a in range(nf)]
            for a in range(nf):
                B[a][a] += lam * scale[a]
            try:
                dxf = _la.solve(B, [-g[free[a]] for a in range(nf)])
            except ValueError:
                lam *= 10.0
                if lam > 1e30:
                    break
                continue
            dx = [0.0] * n
            for a in range(nf):
                dx[free[a]] = dxf[a]
            xn = [x[j] + dx[j] for j in range(n)]
            if bounded:
                xn = _clip(xn, lb, ub)
                dx = [xn[j] - x[j] for j in range(n)]
            fn = fe(xn)
            costn = cost_of(fn)
            Jdx = _la.matvec(Js, dx)
            pred = -(_la.dot(g, dx)) - 0.5 * _la.dot(Jdx, Jdx)
            actual = cost - costn
            if costn == costn and costn < inf and actual > 0:
                ratio = actual / pred if pred > 0 else 1.0
                step_norm = _la.norm2(dx)
                xnorm = _la.norm2(x)
                F_old = cost
                x = xn
                f = fn
                cost = costn
                nit += 1
                if ratio > 0.0:
                    lam = lam * max(1.0 / 3.0, 1.0 - (2.0 * ratio - 1.0) ** 3)
                if lam < 1e-30:
                    lam = 1e-30
                accepted = True
                small = actual < 0.1 * F_old
                if actual < ftol * 1e-3 * F_old and ratio > 0.25:
                    status = 2
                if step_norm < xtol * 1e-3 * (xtol + xnorm) and small:
                    status = 4 if status == 2 else 3
                break
            lam *= 4.0
            if counts[0] >= max_nfev or lam > 1e30:
                break
        if not accepted:
            if counts[0] >= max_nfev:
                status = 0
            else:
                status = 2
            break
        if status != 0:
            break
        J = je(x, f)
    J = je(x, f)
    g = _la.matvec(_la.transpose(J), f)
    gp = list(g)
    active = [0] * n
    if bounded:
        for j in range(n):
            if x[j] <= lb[j]:
                active[j] = -1
                if g[j] > 0:
                    gp[j] = 0.0
            elif x[j] >= ub[j]:
                active[j] = 1
                if g[j] < 0:
                    gp[j] = 0.0
    msgs = {-1: "Improper input parameters status returned from `leastsq`",
            0: "The maximum number of function evaluations is exceeded.",
            1: "`gtol` termination condition is satisfied.",
            2: "`ftol` termination condition is satisfied.",
            3: "`xtol` termination condition is satisfied.",
            4: "Both `ftol` and `xtol` termination conditions are satisfied."}
    return OptimizeResult(message=msgs[status], success=status > 0, status=status, fun=np.array(f),
                          x=np.array(x), cost=cost, jac=np.array(J), grad=np.array(g),
                          optimality=_la.norm_inf(gp), active_mask=np.array(active), nfev=counts[0],
                          njev=counts[1])


def _count_free(fe, counts, v):
    # numeric jacobian evaluations are not counted as nfev (scipy reports only function evaluations)
    counts[0] -= 1
    return fe(v)


def leastsq(func, x0, args=(), Dfun=None, full_output=0, col_deriv=0, ftol=1.49012e-08, xtol=1.49012e-08,
            gtol=0.0, maxfev=0, epsfcn=None, factor=100, diag=None):
    if not isinstance(args, tuple):
        args = (args,)
    r = least_squares(func, x0, jac=(Dfun if Dfun is not None else "2-point"), ftol=ftol, xtol=xtol,
                      gtol=max(gtol, 1e-12), args=args)
    if full_output:
        return r.x, None, {"nfev": r.nfev, "fvec": r.fun}, r.message, 1 if r.success else 5
    return r.x, 1 if r.success else 5


def _nparams(f, xdata):
    for n in range(1, 30):
        try:
            f(xdata, *([1.0] * n))
            return n
        except TypeError:
            continue
    raise ValueError("Unable to determine number of fit parameters.")


def curve_fit(f, xdata, ydata, p0=None, sigma=None, absolute_sigma=False, check_finite=None, bounds=(-inf, inf),
              method=None, jac=None, full_output=False, nan_policy=None, **kwargs):
    xd = np.asarray(xdata)
    if xd.dtype != np.float64 and xd.ndim >= 1:
        xd = xd.astype(float)
    yl = tolist(ydata)
    if p0 is None:
        n = _nparams(f, xd)
        p0l = [1.0] * n
    else:
        p0l = _vec(p0)
        n = len(p0l)
    has_bounds = False
    if bounds is not None:
        if isinstance(bounds, Bounds):
            has_bounds = True
        else:
            for part in bounds:
                for v in tolist(part):
                    if abs(v) != inf:
                        has_bounds = True
    if method is None:
        method = "trf" if has_bounds else "lm"
    if method == "lm" and has_bounds:
        raise ValueError("Method 'lm' only works for unconstrained problems. Use 'trf' or 'dogbox' instead.")
    sig = None
    if sigma is not None:
        sig = tolist(sigma)
        if len(sig) != len(yl):
            raise ValueError("`sigma` has incorrect shape.")

    def resid(p):
        r = tolist(f(xd, *tolist(p)))
        out = []
        for i in range(len(yl)):
            v = r[i] - yl[i]
            if sig is not None:
                v = v / sig[i]
            out.append(v)
        return np.array(out)
    if has_bounds:
        res = least_squares(resid, p0l, bounds=bounds, method="trf", **kwargs)
    else:
        kwargs.setdefault("gtol", 0.0)
        res = least_squares(resid, p0l, **kwargs)
    if not res.success and res.status == 0:
        raise RuntimeError("Optimal parameters not found: " + res.message)
    popt = res.x
    J = np.asarray(res.jac).tolist()
    ysize = len(yl)
    cost = res.cost
    warn_cov = False
    try:
        JT = _la.transpose(J)
        pcov = _la.inv(_la.matmul(JT, J))
        for i in range(n):
            for j in range(n):
                if not (pcov[i][j] == pcov[i][j]):
                    warn_cov = True
    except ValueError:
        warn_cov = True
        pcov = None
    if pcov is None or warn_cov:
        pc = np.full((n, n), inf)
    else:
        if ysize > n:
            s_sq = 2.0 * cost / (ysize - n)
        else:
            s_sq = None
        if s_sq is None:
            pc = np.full((n, n), inf)
        elif not absolute_sigma:
            pc = np.array([[v * s_sq for v in r] for r in pcov])
        else:
            pc = np.array(pcov)
    if full_output:
        return popt, pc, {"nfev": res.nfev, "fvec": res.fun}, res.message, 1 if res.success else 5
    return popt, pc


# ---------------------------------------------------------------------------
# linear programming (two-phase simplex), nnls, assignment
# ---------------------------------------------------------------------------

def linprog(c, A_ub=None, b_ub=None, A_eq=None, b_eq=None, bounds=(0, None), method="highs", callback=None,
            options=None, x0=None, integrality=None):
    cv = _vec(c)
    n = len(cv)
    Aub = np.asarray(A_ub).tolist() if A_ub is not None else []
    bub = _vec(b_ub) if b_ub is not None else []
    Aeq = np.asarray(A_eq).tolist() if A_eq is not None else []
    beq = _vec(b_eq) if b_eq is not None else []
    if Aub and not isinstance(Aub[0], list):
        Aub = [Aub]
    if Aeq and not isinstance(Aeq[0], list):
        Aeq = [Aeq]
    lo = [0.0] * n
    hi = [inf] * n
    if bounds is not None:
        if isinstance(bounds, (list, tuple)) and len(bounds) == 2 and not isinstance(bounds[0], (list, tuple)):
            pairs = [bounds] * n
        elif isinstance(bounds, Bounds):
            pairs = [(tolist(bounds.lb)[i if len(tolist(bounds.lb)) > 1 else 0],
                      tolist(bounds.ub)[i if len(tolist(bounds.ub)) > 1 else 0]) for i in range(n)]
        else:
            pairs = bounds
        for i in range(n):
            l_, h_ = pairs[i]
            lo[i] = -inf if l_ is None else float(l_)
            hi[i] = inf if h_ is None else float(h_)
    # variable substitution to x' >= 0: columns list of (index, sign, offset)
    cols = []
    extra_rows = []
    for i in range(n):
        if lo[i] > -inf:
            cols.append((i, 1.0, lo[i]))
            if hi[i] < inf:
                extra_rows.append((len(cols) - 1, hi[i] - lo[i]))
        elif hi[i] < inf:
            cols.append((i, -1.0, hi[i]))
        else:
            cols.append((i, 1.0, 0.0))
            cols.append((i, -1.0, 0.0))
    nc = len(cols)
    rows = []
    rhs = []
    kinds = []
    for r in range(len(Aub)):
        rows.append([cols[k][1] * Aub[r][cols[k][0]] for k in range(nc)])
        kinds.append("le")
    # recompute rhs with offsets properly
    off = [0.0] * n
    for k in range(nc):
        off[cols[k][0]] += cols[k][2]
    rhs = []
    for r in range(len(Aub)):
        rhs.append(bub[r] - sum([Aub[r][j] * off[j] for j in range(n)]))
    for r in range(len(Aeq)):
        rows.append([cols[k][1] * Aeq[r][cols[k][0]] for k in range(nc)])
        rhs.append(beq[r] - sum([Aeq[r][j] * off[j] for j in range(n)]))
        kinds.append("eq")
    for (k, ubv) in extra_rows:
        row = [0.0] * nc
        row[k] = 1.0
        rows.append(row)
        rhs.append(ubv)
        kinds.append("le")
    cc = [cols[k][1] * cv[cols[k][0]] for k in range(nc)]
    const = sum([cv[j] * off[j] for j in range(n)])
    status, xs, nit = _simplex(cc, rows, rhs, kinds)
    if status == 0:
        x = list(off)
        for k in range(nc):
            x[cols[k][0]] += cols[k][1] * xs[k] - 0.0
        # off already includes lower/upper offsets; add signed contribution
        fun = sum([cv[j] * x[j] for j in range(n)])
        slack = [bub[r] - sum([Aub[r][j] * x[j] for j in range(n)]) for r in range(len(Aub))]
        con = [beq[r] - sum([Aeq[r][j] * x[j] for j in range(n)]) for r in range(len(Aeq))]
        return OptimizeResult(message="Optimization terminated successfully. (HiGHS Status 7: Optimal)",
                              success=True, status=0, fun=fun, x=np.array(x), slack=np.array(slack),
                              con=np.array(con), nit=nit)
    if status == 2:
        return OptimizeResult(message="The problem is infeasible. (HiGHS Status 8: model_status is Infeasible; "
                              "primal_status is At lower/fixed bound)", success=False, status=2, fun=None, x=None,
                              slack=None, con=None, nit=nit)
    return OptimizeResult(message="The problem is unbounded. (HiGHS Status 10: model_status is Unbounded; "
                          "primal_status is At upper bound)", success=False, status=3, fun=None, x=None,
                          slack=None, con=None, nit=nit)


def _simplex(c, rows, rhs, kinds):
    """min c x s.t. rows x (<=|=) rhs, x >= 0.  returns (status, x, iterations); 0 ok, 2 infeasible, 3 unbounded"""
    m = len(rows)
    n = len(c)
    nslack = 0
    for k in kinds:
        if k == "le":
            nslack += 1
    # build tableau columns: x (n), slack (nslack), artificial (m)
    total = n + nslack + m
    T = []
    basis = []
    si = 0
    for r in range(m):
        row = list(rows[r]) + [0.0] * (nslack + m)
        b = rhs[r]
        if kinds[r] == "le":
            row[n + si] = 1.0
            si += 1
        if b < 0:
            row = [-v for v in row]
            b = -b
        row[n + nslack + r] = 1.0
        T.append(row + [b])
        basis.append(n + nslack + r)
    nit = [0]

    def run(cost, allowed):
        # reduced costs row
        z = [0.0] * (total + 1)
        for j in range(total):
            z[j] = cost[j]
        for r in range(m):
            cb = cost[basis[r]]
            if cb != 0.0:
                for j in range(total + 1):
                    z[j] -= cb * T[r][j]
        for it in range(5000):
            # Bland's rule: first column with negative reduced cost
            e = -1
            for j in range(allowed):
                if z[j] < -1e-9:
                    e = j
                    break
            if e < 0:
                return 0
            lv = -1
            best = inf
            for r in range(m):
                if T[r][e] > 1e-9:
                    ratio = T[r][total] / T[r][e]
                    if ratio < best - 1e-12 or (abs(ratio - best) <= 1e-12 and basis[r] < basis[lv]):
                        best = ratio
                        lv = r
            if lv < 0:
                return 3
            piv = T[lv][e]
            T[lv] = [v / piv for v in T[lv]]
            for r in range(m):
                if r != lv and T[r][e] != 0.0:
                    f = T[r][e]
                    rr = T[r]
                    rl = T[lv]
                    for j in range(total + 1):
                        rr[j] -= f * rl[j]
            f = z[e]
            for j in range(total + 1):
                z[j] -= f * T[lv][j]
            basis[lv] = e
            nit[0] += 1
        return 1
    cost1 = [0.0] * (n + nslack) + [1.0] * m
    st = run(cost1, total)
    obj1 = 0.0
    for r in range(m):
        if basis[r] >= n + nslack:
            obj1 += T[r][total]
    if obj1 > 1e-8:
        return 2, None, nit[0]
    # drive artificial variables out of the basis
    for r in range(m):
        if basis[r] >= n + nslack:
            for j in range(n + nslack):
                if abs(T[r][j]) > 1e-9:
                    piv = T[r][j]
                    T[r] = [v / piv for v in T[r]]
                    for q in range(m):
                        if q != r and T[q][j] != 0.0:
                            f = T[q][j]
                            for k2 in range(total + 1):
                                T[q][k2] -= f * T[r][k2]
                    basis[r] = j
                    break
    cost2 = list(c) + [0.0] * (nslack + m)
    st = run(cost2, n + nslack)
    if st == 3:
        return 3, None, nit[0]
    x = [0.0] * n
    for r in range(m):
        if basis[r] < n:
            x[basis[r]] = T[r][total]
    return 0, x, nit[0]


def nnls(A, b, maxiter=None, atol=None):
    """non-negative least squares (Lawson-Hanson active set)"""
    Al = np.asarray(A).tolist()
    bl = _vec(b)
    m = len(Al)
    n = len(Al[0])
    x = [0.0] * n
    P = []
    AT = _la.transpose(Al)
    w = _la.matvec(AT, bl)
    tol = 1e-10 * max(1.0, max([abs(v) for v in w]))
    it = 0
    while it < 3 * n + 10:
        wl = [(w[j] if j not in P else -inf) for j in range(n)]
        j = max(range(n), key=lambda t: wl[t])
        if wl[j] <= tol:
            break
        P.append(j)
        while True:
            sub = [[Al[i][k] for k in P] for i in range(m)]
            sT = _la.transpose(sub)
            s = _la.solve(_la.matmul(sT, sub), _la.matvec(sT, bl))
            if min(s) > 0:
                break
            alpha = inf
            for idx in range(len(P)):
                if s[idx] <= 0:
                    xi = x[P[idx]]
                    a = xi / (xi - s[idx])
                    if a < alpha:
                        alpha = a
            for idx in range(len(P)):
                k = P[idx]
                x[k] = x[k] + alpha * (s[idx] - x[k])
            P = [k for k in P if x[k] > 1e-14]
            if not P:
                s = []
                break
        for idx in range(len(P)):
            x[P[idx]] = s[idx]
        for k in range(n):
            if k not in P:
                x[k] = 0.0
        r = [bl[i] - sum([Al[i][k] * x[k] for k in range(n)]) for i in range(m)]
        w = _la.matvec(AT, r)
        it += 1
    r = [bl[i] - sum([Al[i][k] * x[k] for k in range(n)]) for i in range(m)]
    return np.array(x), math.sqrt(sum([v * v for v in r]))


def linear_sum_assignment(cost_matrix, maximize=False):
    C = np.asarray(cost_matrix).tolist()
    nr = len(C)
    nc = len(C[0])
    transposed = nr > nc
    if transposed:
        C = _la.transpose(C)
        nr, nc = nc, nr
    if maximize:
        C = [[-v for v in r] for r in C]
    u = [0.0] * (nr + 1)
    v = [0.0] * (nc + 1)
    p = [0] * (nc + 1)
    way = [0] * (nc + 1)
    for i in range(1, nr + 1):
        p[0] = i
        j0 = 0
        minv = [inf] * (nc + 1)
        used = [False] * (nc + 1)
        while True:
            used[j0] = True
            i0 = p[j0]
            delta = inf
            j1 = 0
            for j in range(1, nc + 1):
                if not used[j]:
                    cur = C[i0 - 1][j - 1] - u[i0] - v[j]
                    if cur < minv[j]:
                        minv[j] = cur
                        way[j] = j0
                    if minv[j] < delta:
                        delta = minv[j]
                        j1 = j
            for j in range(nc + 1):
                if used[j]:
                    u[p[j]] += delta
                    v[j] -= delta
                else:
                    minv[j] -= delta
            j0 = j1
            if p[j0] == 0:
                break
        while True:
            j1 = way[j0]
            p[j0] = p[j1]
            j0 = j1
            if j0 == 0:
                break
    pairs = []
    for j in range(1, nc + 1):
        if p[j] != 0:
            pairs.append((p[j] - 1, j - 1))
    if transposed:
        pairs = [(b, a) for (a, b) in pairs]
    pairs.sort()
    return np.array([a for a, b in pairs]), np.array([b for a, b in pairs])
