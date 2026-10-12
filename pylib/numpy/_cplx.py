# complex128 arrays for the CREXX numpy subset.
# An array with complex elements is a _CArr holding two native float64 arrays (re, im); the interpreter
# routes np.array(...) of complex data, np.<fn>(complex array) and array<->complex scalar arithmetic here.
# (Only used by the CREXX interpreter; the C interpreter has native complex arrays.)
import math
import cmath
import numpy as np
from numpy._dt import _dt

_range = range


def _is_carr(x):
    return isinstance(x, _CArr)


def _flat_shape(x, shape, flat, depth):
    """collect complex values of the nested sequence x into flat; shape discovered on the first path"""
    if isinstance(x, (list, tuple)):
        n = len(x)
        if depth == len(shape):
            shape.append(n)
        elif shape[depth] != n:
            raise ValueError("setting an array element with a sequence. The requested array has an inhomogeneous shape")
        for v in x:
            _flat_shape(v, shape, flat, depth + 1)
    elif isinstance(x, np.ndarray):
        _flat_shape(x.tolist(), shape, flat, depth)
    elif isinstance(x, _CArr):
        _flat_shape(x.tolist(), shape, flat, depth)
    else:
        flat.append(complex(x))


def _from_obj(x):
    shape = []
    flat = []
    _flat_shape(x, shape, flat, 0)
    if len(shape) == 0:
        return flat[0]
    re = np.array([float(v.real) for v in flat])
    im = np.array([float(v.imag) for v in flat])
    if len(shape) > 1:
        re = re.reshape(shape)
        im = im.reshape(shape)
    return _CArr(re, im)


def _from_real(a):
    a = np.asarray(a)
    re = a.astype(float) + 0.0
    return _CArr(re, re * 0.0)


def _parts(x):
    """(re, im) of a scalar / array / _CArr operand"""
    if isinstance(x, _CArr):
        return (x.re, x.im)
    if isinstance(x, complex):
        return (x.real, x.imag)
    if isinstance(x, (int, float)):
        return (float(x), 0.0)
    a = np.asarray(x)
    if _isc(a):
        return (a.re, a.im)
    a = a.astype(float)
    return (a, a * 0.0)


def _isc(a):
    return isinstance(a, _CArr)


def _wrap(re, im):
    if isinstance(re, np.ndarray):
        return _CArr(re, im)
    return complex(re, im)


def _map1(f, a):
    """apply the complex function f element wise"""
    if not _is_carr(a):
        return f(complex(a))
    rl = a.re.flatten().tolist()
    il = a.im.flatten().tolist()
    outr = []
    outi = []
    for k in _range(len(rl)):
        z = f(complex(rl[k], il[k]))
        outr.append(z.real)
        outi.append(z.imag)
    re = np.array(outr)
    im = np.array(outi)
    if a.re.ndim > 1:
        re = re.reshape(a.re.shape)
        im = im.reshape(a.re.shape)
    return _CArr(re, im)


def _div_parts(ar, ai, br, bi):
    if not isinstance(br, np.ndarray) and not isinstance(bi, np.ndarray):
        # scalar divisor: Smith's algorithm as in numpy
        if br == 0.0 and bi == 0.0:
            nan = float("nan")
            return (ar * 0.0 + nan, ai * 0.0 + nan)
        if abs(br) >= abs(bi):
            rat = bi / br
            scl = 1.0 / (br + bi * rat)
            return ((ar + ai * rat) * scl, (ai - ar * rat) * scl)
        rat = br / bi
        scl = 1.0 / (br * rat + bi)
        return ((ar * rat + ai) * scl, (ai * rat - ar) * scl)
    # array divisor: element wise through python complex division
    shape = None
    for v in (ar, ai, br, bi):
        if isinstance(v, np.ndarray):
            shape = v.shape
            break
    n = 1
    for d in shape:
        n *= d

    def fl(v):
        if isinstance(v, np.ndarray):
            if v.shape != shape:
                v = v + np.zeros(shape)
            return v.flatten().tolist()
        return [v] * n

    a1 = fl(ar)
    a2 = fl(ai)
    b1 = fl(br)
    b2 = fl(bi)
    outr = []
    outi = []
    for k in _range(n):
        z = _cdiv(complex(a1[k], a2[k]), complex(b1[k], b2[k]))
        outr.append(z.real)
        outi.append(z.imag)
    re = np.array(outr).reshape(shape)
    im = np.array(outi).reshape(shape)
    return (re, im)


def _cdiv(a, b):
    if b == 0:
        return complex(float("nan"), float("nan"))
    return a / b


def _cpow_scalar(z, n):
    return z ** n


def _fmt_float_group(vals, plus):
    """numpy FloatingFormat for a list of floats: list of padded strings"""
    fin = []
    for v in vals:
        if v == v and v != float("inf") and v != float("-inf"):
            fin.append(v)
    nz = [abs(v) for v in fin if v != 0.0]
    exp_fmt = False
    if len(nz) > 0:
        mx = max(nz)
        mn = min(nz)
        if mx >= 1.0e8 or mn < 0.0001 or mx / mn > 1000.0:
            exp_fmt = True
    strs = []
    pad_left = 0
    pad_right = 0
    exp_size = -1
    prec = 0
    if exp_fmt:
        parts = []
        for v in fin:
            s = "%.8e" % v
            mant, ex = s.split("e")
            ip, fp = mant.split(".")
            fp = fp.rstrip("0")
            parts.append((ip, fp, ex))
        exp_size = 1
        for p in parts:
            ee = p[2]
            digs = len(ee) - 1
            if digs > exp_size:
                exp_size = digs
        for p in parts:
            if len(p[1]) > prec:
                prec = len(p[1])
            li = len(p[0]) + (1 if (plus and not p[0].startswith("-")) else 0)
            if li > pad_left:
                pad_left = li
        pad_right = exp_size + 2 + prec
    else:
        for v in fin:
            s = "%.8f" % abs(v)
            ip, fp = s.split(".")
            fp = fp.rstrip("0")
            neg = math.copysign(1.0, v) < 0
            li = len(ip) + (1 if (neg or plus) else 0)
            if li > pad_left:
                pad_left = li
            if len(fp) > pad_right:
                pad_right = len(fp)
    if len(fin) != len(vals):
        has_neg_inf = False
        for v in vals:
            if v == float("-inf"):
                has_neg_inf = True
        off = pad_right + 1
        need = 3 - off
        t = 3 + (1 if (plus or has_neg_inf) else 0) - off
        if t > need:
            need = t
        if need > pad_left:
            pad_left = need
    out = []
    for v in vals:
        if v != v:
            r = "nan"
            out.append(" " * (pad_left + pad_right + 1 - len(r)) + r)
        elif v == float("inf") or v == float("-inf"):
            r = "-inf" if v < 0 else ("+inf" if plus else "inf")
            out.append(" " * (pad_left + pad_right + 1 - len(r)) + r)
        elif exp_fmt:
            s = "%.8e" % v
            mant, ex = s.split("e")
            ip, fp = mant.split(".")
            fp = fp.rstrip("0")
            sg = ex[0]
            dg = ex[1:].lstrip("0")
            while len(dg) < exp_size:
                dg = "0" + dg
            if len(dg) < 2:
                dg = "0" + dg
            ip2 = ip
            if plus and not ip2.startswith("-"):
                ip2 = "+" + ip2
            ip2 = " " * (pad_left - len(ip2)) + ip2
            out.append(ip2 + "." + fp + "0" * (prec - len(fp)) + "e" + sg + dg)
        else:
            s = "%.8f" % abs(v)
            ip, fp = s.split(".")
            fp = fp.rstrip("0")
            if math.copysign(1.0, v) < 0:
                ip = "-" + ip
            elif plus:
                ip = "+" + ip
            ip = " " * (pad_left - len(ip)) + ip
            out.append(ip + "." + fp + " " * (pad_right - len(fp)))
    return out


def _fmt_all(re, im):
    rs = _fmt_float_group(re, False)
    isx = _fmt_float_group(im, True)
    out = []
    for k in _range(len(rs)):
        i = isx[k]
        sp = len(i.rstrip())
        i = i[:sp] + "j" + i[sp:]
        out.append(rs[k] + i)
    return out


def _layout(words, shape, sep, indent, width):
    """numpy's _formatArray for the formatted element strings (row major list words)"""
    pos = [0]

    def rec(axis, hanging, curr_width):
        if axis == len(shape):
            w = words[pos[0]]
            pos[0] += 1
            return w
        n = shape[axis]
        if axis == len(shape) - 1:
            elem_width = curr_width - max(len(sep.rstrip()), 1)
            s = ""
            line = hanging
            for i in _range(n):
                word = rec(axis + 1, hanging + " ", curr_width - 1)
                if i < n - 1:
                    # _extendLine then add separator
                    if len(line) + len(word) > elem_width and len(line) > len(hanging):
                        s += line.rstrip() + "\n"
                        line = hanging
                    line += word
                    line += sep
                else:
                    if len(line) + len(word) > elem_width and len(line) > len(hanging):
                        s += line.rstrip() + "\n"
                        line = hanging
                    line += word
            s += line
            return "[" + s[len(hanging):] + "]"
        line_sep = sep.rstrip() + "\n" * (len(shape) - axis - 1)
        s = ""
        for i in _range(n):
            nested = rec(axis + 1, hanging + " ", curr_width - 1)
            s += hanging + nested
            if i < n - 1:
                s += line_sep
        return "[" + s[len(hanging):] + "]"

    return rec(0, indent, width)


class _CArr:
    def __init__(self, re, im):
        self.re = re
        self.im = im

    # --- attributes
    @property
    def real(self):
        return self.re

    @property
    def imag(self):
        return self.im

    @property
    def shape(self):
        return self.re.shape

    @property
    def ndim(self):
        return self.re.ndim

    @property
    def size(self):
        return self.re.size

    @property
    def dtype(self):
        return _dt("complex128")

    @property
    def T(self):
        return _CArr(self.re.T, self.im.T)

    def __len__(self):
        return len(self.re)

    def __bool__(self):
        return bool(self.re.any() or self.im.any())

    # --- element access
    def __getitem__(self, k):
        r = self.re[k]
        i = self.im[k]
        if isinstance(r, np.ndarray):
            return _CArr(r, i)
        return complex(r, i)

    def __setitem__(self, k, v):
        vr, vi = _parts(v)
        self.re[k] = vr
        self.im[k] = vi

    def __iter__(self):
        out = []
        for k in _range(len(self.re)):
            out.append(self[k])
        return iter(out)

    def tolist(self):
        rl = self.re.tolist()
        il = self.im.tolist()
        return _zip_nested(rl, il)

    def copy(self):
        return _CArr(self.re + 0.0, self.im + 0.0)

    def astype(self, t):
        if t is complex or (isinstance(t, str) and t.startswith("complex")):
            return self.copy()
        return self.re.astype(t)

    def reshape(self, *shape):
        return _CArr(self.re.reshape(*shape), self.im.reshape(*shape))

    def flatten(self):
        return _CArr(self.re.flatten(), self.im.flatten())

    def ravel(self):
        return self.flatten()

    def transpose(self, *a):
        return _CArr(self.re.transpose(*a), self.im.transpose(*a))

    def conj(self):
        return _CArr(self.re + 0.0, -self.im)

    conjugate = conj

    def round(self, decimals=0):
        return _CArr(np.round(self.re, decimals), np.round(self.im, decimals))

    # --- arithmetic
    def __neg__(self):
        return _CArr(-self.re, -self.im)

    def __pos__(self):
        return self

    def __abs__(self):
        return _abs(self)

    def __add__(self, o):
        br, bi = _parts(o)
        return _wrap(self.re + br, self.im + bi)

    __radd__ = __add__

    def __sub__(self, o):
        br, bi = _parts(o)
        return _wrap(self.re - br, self.im - bi)

    def __rsub__(self, o):
        br, bi = _parts(o)
        return _wrap(br - self.re, bi - self.im)

    def __mul__(self, o):
        br, bi = _parts(o)
        return _wrap(self.re * br - self.im * bi, self.re * bi + self.im * br)

    __rmul__ = __mul__

    def __truediv__(self, o):
        br, bi = _parts(o)
        r, i = _div_parts(self.re, self.im, br, bi)
        return _wrap(r, i)

    def __rtruediv__(self, o):
        ar, ai = _parts(o)
        r, i = _div_parts(ar, ai, self.re, self.im)
        return _wrap(r, i)

    def __pow__(self, p):
        if isinstance(p, int) and not isinstance(p, bool) and 0 <= p <= 64:
            res = _CArr(self.re * 0.0 + 1.0, self.re * 0.0)
            base = self
            n = p
            while n > 0:
                if n % 2 == 1:
                    res = res * base
                base = base * base
                n //= 2
            return res
        if isinstance(p, (int, float, complex)):
            return _map1(lambda z: z ** p, self)
        return _map2(lambda a, b: a ** b, self, p)

    def __rpow__(self, b):
        return _map2(lambda x, y: x ** y, b, self)

    def __eq__(self, o):
        br, bi = _parts(o)
        return (self.re == br) & (self.im == bi)

    def __ne__(self, o):
        br, bi = _parts(o)
        return (self.re != br) | (self.im != bi)

    def __matmul__(self, o):
        return dot(self, o)

    def __rmatmul__(self, o):
        return dot(o, self)

    # --- reductions
    def sum(self, axis=None, **kw):
        return _red("sum", self, axis)

    def mean(self, axis=None, **kw):
        return _red("mean", self, axis)

    def prod(self, axis=None, **kw):
        return _red("prod", self, axis)

    def dot(self, o):
        return dot(self, o)

    def cumsum(self, axis=None, **kw):
        return _cumsum(self, axis)

    def sort(self, axis=-1, **kw):
        r = _sort(self, axis)
        self.re[...] = r.re
        self.im[...] = r.im

    def argsort(self, axis=-1, **kw):
        return _argsort(self, axis)

    def trace(self, **kw):
        return _trace(self)

    def diagonal(self, **kw):
        return _diag(self)

    def any(self, **kw):
        return bool(self.__bool__())

    def all(self, **kw):
        return bool(np.all((self.re != 0) | (self.im != 0)))

    def nonzero(self):
        return np.nonzero((self.re != 0) | (self.im != 0))

    # --- printing
    def _words(self):
        return _fmt_all(self.re.flatten().tolist(), self.im.flatten().tolist())

    def __str__(self):
        return _layout(self._words(), list(self.re.shape), " ", " ", 75)

    def __repr__(self):
        return "array(" + _layout(self._words(), list(self.re.shape), ", ", "       ", 75 - 6) + ")"


def _zip_nested(rl, il):
    if isinstance(rl, list):
        out = []
        for k in _range(len(rl)):
            out.append(_zip_nested(rl[k], il[k]))
        return out
    return complex(rl, il)


def _map2(f, a, b):
    if not _is_carr(a) and not _is_carr(b):
        return f(a, b)
    ref = a if _is_carr(a) else b
    ar = _as_c_flat(a, ref)
    br = _as_c_flat(b, ref)
    outr = []
    outi = []
    for k in _range(len(ar)):
        z = f(ar[k], br[k])
        z = complex(z)
        outr.append(z.real)
        outi.append(z.imag)
    re = np.array(outr)
    im = np.array(outi)
    if ref.re.ndim > 1:
        re = re.reshape(ref.re.shape)
        im = im.reshape(ref.re.shape)
    return _CArr(re, im)


def _as_c_flat(x, ref):
    n = ref.re.size
    if _is_carr(x):
        rl = x.re.flatten().tolist()
        il = x.im.flatten().tolist()
        return [complex(rl[k], il[k]) for k in _range(len(rl))]
    if isinstance(x, (int, float, complex)):
        return [complex(x)] * n
    a = np.asarray(x).flatten().tolist()
    return [complex(v) for v in a]


def _abs(a):
    if not _is_carr(a):
        z = complex(a)
        return math.hypot(z.real, z.imag)
    return np.sqrt(a.re * a.re + a.im * a.im) if not hasattr(np, "hypot") else np.hypot(a.re, a.im)


def _red(name, a, axis):
    if axis is not None:
        r = getattr(a.re, "sum" if name != "prod" else "sum")(axis=axis)
        i = a.im.sum(axis=axis)
        if name == "mean":
            n = a.re.shape[axis]
            return _wrap(r / n, i / n)
        if name == "prod":
            raise NotImplementedError("complex prod with axis")
        return _wrap(r, i)
    rl = a.re.flatten().tolist()
    il = a.im.flatten().tolist()
    if name == "prod":
        z = complex(1.0, 0.0)
        for k in _range(len(rl)):
            z = z * complex(rl[k], il[k])
        return z
    sr = 0.0
    si = 0.0
    for k in _range(len(rl)):
        sr += rl[k]
        si += il[k]
    if name == "mean":
        n = len(rl)
        return complex(sr / n, si / n)
    return complex(sr, si)


def dot(a, b):
    ar, ai = _parts(a)
    br, bi = _parts(b)
    return _wrap(np.dot(ar, br) - np.dot(ai, bi), np.dot(ar, bi) + np.dot(ai, br))


def _cumsum(a, axis=None):
    a = a if _is_carr(a) else _from_obj(a)
    if axis is None:
        return _CArr(np.cumsum(a.re.flatten()), np.cumsum(a.im.flatten()))
    return _CArr(np.cumsum(a.re, axis=axis), np.cumsum(a.im, axis=axis))


def _key(z):
    return (z.real, z.imag)


def _sorted_rows(a, axis, want_idx):
    """sort along an axis (1-d and 2-d); complex numbers order lexicographically (real, then imaginary)"""
    a = a if _is_carr(a) else _from_obj(a)
    nd = a.re.ndim
    if nd == 0:
        return a
    if axis is None:
        a = a.flatten()
        nd = 1
        axis = -1
    if axis < 0:
        axis += nd
    if nd == 1:
        z = a.tolist()
        idx = sorted(_range(len(z)), key=lambda k: _key(z[k]))
        if want_idx:
            return np.array(idx)
        return _from_obj([z[k] for k in idx])
    if nd == 2:
        if axis == 0:
            r = _sorted_rows(a.T, 1, want_idx)
            return r.T
        rows = a.tolist()
        out = [_sorted_rows(_from_obj(row), -1, want_idx) for row in rows]
        if want_idx:
            return np.array([o.tolist() for o in out])
        return _from_obj([o.tolist() for o in out])
    raise NotImplementedError("complex sort of arrays with more than 2 dimensions")


def _sort(a, axis=-1):
    return _sorted_rows(a, axis, False)


def _argsort(a, axis=-1):
    return _sorted_rows(a, axis, True)


def _unique(a):
    a = a if _is_carr(a) else _from_obj(a)
    z = sorted(set(a.flatten().tolist()), key=_key)
    return _from_obj(z)


def _diag(a, k=0):
    a = a if _is_carr(a) else _from_obj(a)
    return _CArr(np.diag(a.re, k), np.diag(a.im, k))


def _trace(a):
    a = a if _is_carr(a) else _from_obj(a)
    d = np.diag(a.re)
    e = np.diag(a.im)
    return complex(float(d.sum()), float(e.sum()))


def _unary(f):
    def g(a):
        return _map1(f, a)
    return g


_FUNCS = {
    "sqrt": cmath.sqrt, "exp": cmath.exp, "log": cmath.log, "log10": cmath.log10,
    "sin": cmath.sin, "cos": cmath.cos, "tan": cmath.tan,
    "sinh": cmath.sinh, "cosh": cmath.cosh, "tanh": cmath.tanh,
    "arcsin": cmath.asin, "arccos": cmath.acos, "arctan": cmath.atan,
}


def _log2(z):
    r = cmath.log(z)
    ln2 = math.log(2.0)
    return complex(r.real / ln2, r.imag / ln2)


def _square(z):
    return z * z


def _dispatch(name, args, kw):
    a = args[0] if len(args) > 0 else None
    if name in _FUNCS:
        return _map1(_FUNCS[name], a)
    if name == "log2":
        return _map1(_log2, a)
    if name == "square":
        return _map1(_square, a)
    if name == "abs" or name == "absolute":
        return _abs(a)
    if name == "angle":
        z = a
        ar, ai = _parts(z)
        r = np.arctan2(ai, ar) if isinstance(ar, np.ndarray) or isinstance(ai, np.ndarray) else math.atan2(ai, ar)
        if kw.get("deg", False) or (len(args) > 1 and args[1]):
            r = np.degrees(r) if isinstance(r, np.ndarray) else math.degrees(r)
        return r
    if name == "conj" or name == "conjugate":
        ar, ai = _parts(a)
        return _wrap(ar + 0.0, -ai)
    if name == "real":
        return _parts(a)[0]
    if name == "imag":
        return _parts(a)[1]
    if name == "sum" or name == "mean" or name == "prod":
        return _red(name, a if _is_carr(a) else _from_obj(a), kw.get("axis", args[1] if len(args) > 1 else None))
    if name == "dot" or name == "matmul" or name == "inner" or name == "vdot":
        if name == "vdot":
            return dot(_dispatch("conj", (args[0],), {}), args[1])
        return dot(args[0], args[1])
    if name == "add":
        return args[0] + args[1]
    if name == "subtract":
        return args[0] - args[1]
    if name == "multiply":
        return args[0] * args[1]
    if name == "divide" or name == "true_divide":
        return args[0] / args[1]
    if name == "power":
        return args[0] ** args[1]
    if name == "negative":
        return -args[0]
    if name == "round" or name == "around":
        dec = kw.get("decimals", args[1] if len(args) > 1 else 0)
        return a.round(dec)
    if name == "isnan":
        ar, ai = _parts(a)
        return np.isnan(ar) | np.isnan(ai)
    if name == "shape":
        return a.shape
    if name == "ndim":
        return a.ndim
    if name == "size":
        return a.size
    if name == "copy" or name == "array" or name == "asarray":
        return a.copy() if _is_carr(a) else a
    if name == "reshape":
        return a.reshape(args[1])
    if name == "transpose":
        return a.T
    if name == "zeros_like":
        return _CArr(np.zeros_like(a.re), np.zeros_like(a.re))
    if name == "ones_like":
        return _CArr(np.ones_like(a.re), np.zeros_like(a.re))
    if name == "full":
        fr, fi = _parts(args[1])
        shp = args[0]
        return _CArr(np.full(shp, fr), np.full(shp, fi))
    if name == "concatenate" or name == "hstack" or name == "vstack" or name == "stack":
        seq = [x if _is_carr(x) else _from_real(x) for x in a]
        fn = getattr(np, name)
        axis = kw.get("axis", None)
        if axis is not None:
            return _CArr(fn([x.re for x in seq], axis=axis), fn([x.im for x in seq], axis=axis))
        return _CArr(fn([x.re for x in seq]), fn([x.im for x in seq]))
    if name == "isclose" or name == "allclose":
        d = _abs(args[0] - args[1])
        tol = kw.get("atol", 1e-8) + kw.get("rtol", 1e-5) * _abs(args[1])
        r = d <= tol
        if name == "allclose":
            return bool(np.all(r)) if isinstance(r, np.ndarray) else bool(r)
        return r
    if name == "array_equal":
        r = args[0] == args[1]
        return bool(np.all(r)) if isinstance(r, np.ndarray) else bool(r)
    if name == "cumsum":
        return _cumsum(a, kw.get("axis", args[1] if len(args) > 1 else None))
    if name == "sort":
        return _sort(a, kw.get("axis", args[1] if len(args) > 1 else -1))
    if name == "argsort":
        return _argsort(a, kw.get("axis", args[1] if len(args) > 1 else -1))
    if name == "unique":
        return _unique(a)
    if name == "diag":
        return _diag(a, kw.get("k", args[1] if len(args) > 1 else 0))
    if name == "trace":
        return _trace(a)
    if name == "count_nonzero":
        return int(np.sum((_parts(a)[0] != 0) | (_parts(a)[1] != 0)))
    if name == "isreal":
        return _parts(a)[1] == 0
    if name == "iscomplex":
        return _parts(a)[1] != 0
    if name == "where":
        cnd = args[0]
        x = args[1]
        y = args[2]
        xr, xi = _parts(x)
        yr, yi = _parts(y)
        return _wrap(np.where(cnd, xr, yr), np.where(cnd, xi, yi))
    raise TypeError("numpy function '%s' is not supported for complex data" % name)


def roots(p):
    """roots of the polynomial with coefficients p (highest degree first), via the companion matrix"""
    p = [complex(v) if isinstance(v, complex) else float(v) for v in np.asarray(p).tolist()] if not _is_carr(p) else p.tolist()
    # strip leading and trailing zeros
    i = 0
    n = len(p)
    while i < n and p[i] == 0:
        i += 1
    j = n
    while j > i and p[j - 1] == 0:
        j -= 1
    nz = n - j
    p = p[i:j]
    m = len(p) - 1
    if m < 1:
        return np.zeros(nz) if nz > 0 else np.array([])
    cplx = False
    for v in p:
        if isinstance(v, complex):
            cplx = True
    comp = [[0.0] * m for _ in _range(m)]
    for r in _range(1, m):
        comp[r][r - 1] = 1.0
    for c in _range(m):
        comp[0][c] = -p[c + 1] / p[0]
    ev = np.linalg.eigvals(np.array(comp))
    if nz > 0:
        if _is_carr(ev):
            ev = _CArr(np.concatenate([ev.re, np.zeros(nz)]), np.concatenate([ev.im, np.zeros(nz)]))
        else:
            ev = np.concatenate([ev, np.zeros(nz)])
    return ev


# ---- public numpy functions that exist only for complex support (np.angle, np.conj, ...) ----
def angle(z, deg=False):
    return _dispatch("angle", (z,), {"deg": deg})


def conj(z):
    return _dispatch("conj", (z,), {})


conjugate = conj


def real(z):
    return _parts(z)[0]


def imag(z):
    return _parts(z)[1]


def iscomplex(z):
    return _parts(z)[1] != 0


def isreal(z):
    return _parts(z)[1] == 0


def iscomplexobj(z):
    return _is_carr(z) or isinstance(z, complex)


def vdot(a, b):
    return dot(conj(a), b)
