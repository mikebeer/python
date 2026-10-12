# numpy.fft in plain Python (shared by both interpreters, also runs on CPython).
# Internally every transform works on two lists of floats (real and imaginary part), so only
# the boundary code touches the complex type.  Lengths: radix 2, mixed radix (any small
# prime factor, naive DFT for the prime butterflies) and Bluestein for large prime lengths.
import math
import numpy as np

_range = range
_TW = {}


def _tw(n):
    """cos / sin of 2*pi*k/n for k in 0..n-1 (cached)"""
    t = _TW.get(n)
    if t is None:
        c = [0.0] * n
        s = [0.0] * n
        w = 2.0 * math.pi / n
        for k in _range(n):
            a = w * k
            c[k] = math.cos(a)
            s[k] = math.sin(a)
        for k in _range(n):
            # exact values on the axes
            if (4 * k) % n == 0:
                q = (4 * k) // n
                c[k] = [1.0, 0.0, -1.0, 0.0][q]
                s[k] = [0.0, 1.0, 0.0, -1.0][q]
        if len(_TW) > 64:
            _TW.clear()
        t = (c, s)
        _TW[n] = t
    return t


def _is_pow2(n):
    return n > 0 and (n & (n - 1)) == 0


def _next_pow2(n):
    p = 1
    while p < n:
        p *= 2
    return p


def _smallest_factor(n):
    if n % 2 == 0:
        return 2
    f = 3
    while f * f <= n:
        if n % f == 0:
            return f
        f += 2
    return n


def _radix2(re, im, sign):
    n = len(re)
    if n == 1:
        return re, im
    # bit reversal permutation
    j = 0
    for i in _range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            t = re[i]
            re[i] = re[j]
            re[j] = t
            t = im[i]
            im[i] = im[j]
            im[j] = t
    c, s = _tw(n)
    size = 2
    while size <= n:
        half = size // 2
        step = n // size
        for start in _range(0, n, size):
            k = 0
            for a in _range(start, start + half):
                b = a + half
                wr = c[k]
                wi = sign * s[k]
                xr = re[b] * wr - im[b] * wi
                xi = re[b] * wi + im[b] * wr
                re[b] = re[a] - xr
                im[b] = im[a] - xi
                re[a] = re[a] + xr
                im[a] = im[a] + xi
                k += step
        size *= 2
    return re, im


def _naive(re, im, sign):
    n = len(re)
    c, s = _tw(n)
    ore = [0.0] * n
    oim = [0.0] * n
    for k in _range(n):
        sr = 0.0
        si = 0.0
        for j in _range(n):
            m = (j * k) % n
            wr = c[m]
            wi = sign * s[m]
            sr += re[j] * wr - im[j] * wi
            si += re[j] * wi + im[j] * wr
        ore[k] = sr
        oim[k] = si
    return ore, oim


def _bluestein(re, im, sign):
    n = len(re)
    two = 2 * n
    cr = [0.0] * n
    ci = [0.0] * n
    for j in _range(n):
        a = math.pi * ((j * j) % two) / n
        cr[j] = math.cos(a)
        ci[j] = sign * math.sin(a)
    m = _next_pow2(2 * n - 1)
    ar = [0.0] * m
    ai = [0.0] * m
    br = [0.0] * m
    bi = [0.0] * m
    for j in _range(n):
        ar[j] = re[j] * cr[j] - im[j] * ci[j]
        ai[j] = re[j] * ci[j] + im[j] * cr[j]
    br[0] = cr[0]
    bi[0] = -ci[0]
    for j in _range(1, n):
        br[j] = cr[j]
        bi[j] = -ci[j]
        br[m - j] = cr[j]
        bi[m - j] = -ci[j]
    _radix2(ar, ai, -1.0)
    _radix2(br, bi, -1.0)
    for j in _range(m):
        t = ar[j] * br[j] - ai[j] * bi[j]
        ai[j] = ar[j] * bi[j] + ai[j] * br[j]
        ar[j] = t
    _radix2(ar, ai, 1.0)
    ore = [0.0] * n
    oim = [0.0] * n
    for k in _range(n):
        xr = ar[k] / m
        xi = ai[k] / m
        ore[k] = xr * cr[k] - xi * ci[k]
        oim[k] = xr * ci[k] + xi * cr[k]
    return ore, oim


def _fft_raw(re, im, sign):
    """unnormalised DFT with exponent sign*2*pi*i*j*k/n, returns new lists"""
    n = len(re)
    if n <= 1:
        return list(re), list(im)
    if _is_pow2(n):
        return _radix2(list(re), list(im), sign)
    p = _smallest_factor(n)
    if p == n:
        if n <= 40:
            return _naive(re, im, sign)
        return _bluestein(re, im, sign)
    m = n // p
    subs = []
    for r in _range(p):
        subs.append(_fft_raw(re[r::p], im[r::p], sign))
    c, s = _tw(n)
    ore = [0.0] * n
    oim = [0.0] * n
    tr = [0.0] * p
    ti = [0.0] * p
    for k in _range(m):
        for r in _range(p):
            yr = subs[r][0][k]
            yi = subs[r][1][k]
            idx = (r * k) % n
            wr = c[idx]
            wi = sign * s[idx]
            tr[r] = yr * wr - yi * wi
            ti[r] = yr * wi + yi * wr
        for q in _range(p):
            sr = tr[0]
            si = ti[0]
            for r in _range(1, p):
                idx = (m * r * q) % n
                wr = c[idx]
                wi = sign * s[idx]
                sr += tr[r] * wr - ti[r] * wi
                si += tr[r] * wi + ti[r] * wr
            ore[k + m * q] = sr
            oim[k + m * q] = si
    return ore, oim


# ----------------------------------------------------------------------------- array plumbing

def _check_norm(norm):
    if norm is None or norm == 'backward' or norm == 'ortho' or norm == 'forward':
        return
    raise ValueError('Invalid norm value %s; should be "backward", "ortho" or "forward".' % (norm,))


def _scale(norm, n, inverse):
    """scale factor applied to a transform of length n"""
    _check_norm(norm)
    if norm == 'ortho':
        return 1.0 / math.sqrt(n)
    if norm == 'forward':
        return 1.0 if inverse else 1.0 / n
    return 1.0 / n if inverse else 1.0


def _parts(x):
    """flat real and imaginary part lists of an array like object, plus its shape"""
    a = np.asarray(x)
    shape = tuple(a.shape)
    flat = a.flatten().tolist()
    re = []
    im = []
    for v in flat:
        re.append(float(v.real))
        im.append(float(v.imag))
    return re, im, shape


def _build(re, im, shape):
    out = [complex(re[i], im[i]) for i in _range(len(re))]
    if len(shape) == 0:
        return out[0]
    return np.array(out).reshape(shape)


def _build_real(re, shape):
    if len(shape) == 0:
        return re[0]
    return np.array(re).reshape(shape)


def _axis(axis, nd):
    if axis < -nd or axis >= nd:
        raise ValueError("axis %d is out of bounds for array of dimension %d" % (axis, nd))
    return axis + nd if axis < 0 else axis


def _lines(shape, ax):
    """(outer, n, inner) decomposition for iterating over lines along axis ax"""
    outer = 1
    for d in shape[:ax]:
        outer *= d
    inner = 1
    for d in shape[ax + 1:]:
        inner *= d
    return outer, shape[ax], inner


def _along(re, im, shape, ax, fn, newn):
    """apply fn(re_line, im_line) -> (re_line, im_line of length newn) along axis ax"""
    outer, n, inner = _lines(shape, ax)
    ore = [0.0] * (outer * newn * inner)
    oim = [0.0] * (outer * newn * inner)
    for o in _range(outer):
        for i in _range(inner):
            base = o * n * inner + i
            lr = [re[base + k * inner] for k in _range(n)]
            li = [im[base + k * inner] for k in _range(n)]
            rr, ri = fn(lr, li)
            obase = o * newn * inner + i
            for k in _range(newn):
                ore[obase + k * inner] = rr[k]
                oim[obase + k * inner] = ri[k]
    nshape = shape[:ax] + (newn,) + shape[ax + 1:]
    return ore, oim, nshape


def _clean(rr, ri):
    """snap rounding noise (below ~8 eps of the largest component) to +0.0 so that exact zeros of
    symmetric inputs print as 0 like pocketfft's, and drop the sign of zero"""
    m = 0.0
    for v in rr:
        if v > m:
            m = v
        elif -v > m:
            m = -v
    for v in ri:
        if v > m:
            m = v
        elif -v > m:
            m = -v
    t = m * 1.8e-15
    for i in _range(len(rr)):
        v = rr[i]
        rr[i] = 0.0 if (v <= t and -v <= t) else v
        v = ri[i]
        ri[i] = 0.0 if (v <= t and -v <= t) else v
    return rr, ri


def _fit(lr, li, n):
    """crop / zero pad a line to length n"""
    m = len(lr)
    if n < m:
        return lr[:n], li[:n]
    if n > m:
        z = [0.0] * (n - m)
        return lr + z, li + z
    return lr, li


def _c2c(re, im, shape, n, axis, norm, inverse):
    nd = len(shape)
    if nd == 0:
        raise ValueError("axis %d is out of bounds for array of dimension 0" % axis)
    ax = _axis(axis, nd)
    if n is None:
        n = shape[ax]
    if n < 1:
        raise ValueError("Invalid number of FFT data points (%d) specified." % n)
    f = _scale(norm, n, inverse)
    sign = 1.0 if inverse else -1.0

    def fn(lr, li):
        lr, li = _fit(lr, li, n)
        rr, ri = _fft_raw(lr, li, sign)
        if f != 1.0:
            rr = [v * f for v in rr]
            ri = [v * f for v in ri]
        return _clean(rr, ri)
    return _along(re, im, shape, ax, fn, n)


def fft(a, n=None, axis=-1, norm=None):
    re, im, shape = _parts(a)
    re, im, shape = _c2c(re, im, shape, n, axis, norm, False)
    return _build(re, im, shape)


def ifft(a, n=None, axis=-1, norm=None):
    re, im, shape = _parts(a)
    re, im, shape = _c2c(re, im, shape, n, axis, norm, True)
    return _build(re, im, shape)


def _r2c(re, im, shape, n, axis, norm, inverse_scale=False):
    nd = len(shape)
    if nd == 0:
        raise ValueError("axis %d is out of bounds for array of dimension 0" % axis)
    ax = _axis(axis, nd)
    if n is None:
        n = shape[ax]
    if n < 1:
        raise ValueError("Invalid number of FFT data points (%d) specified." % n)
    f = _scale(norm, n, inverse_scale)
    half = n // 2 + 1

    def fn(lr, li):
        lr, li = _fit(lr, [0.0] * len(lr), n)
        rr, ri = _fft_raw(lr, [0.0] * n, -1.0)
        rr = rr[:half]
        ri = ri[:half]
        if f != 1.0:
            rr = [v * f for v in rr]
            ri = [v * f for v in ri]
        return _clean(rr, ri)
    return _along(re, im, shape, ax, fn, half)


def rfft(a, n=None, axis=-1, norm=None):
    re, im, shape = _parts(a)
    re, im, shape = _r2c(re, im, shape, n, axis, norm)
    return _build(re, im, shape)


def _c2r(re, im, shape, n, axis, norm, forward_scale=False):
    nd = len(shape)
    if nd == 0:
        raise ValueError("axis %d is out of bounds for array of dimension 0" % axis)
    ax = _axis(axis, nd)
    if n is None:
        n = 2 * (shape[ax] - 1)
    if n < 1:
        raise ValueError("Invalid number of FFT data points (%d) specified." % n)
    f = _scale(norm, n, not forward_scale)
    half = n // 2 + 1

    def fn(lr, li):
        lr, li = _fit(lr, li, half)
        fr = [0.0] * n
        fi = [0.0] * n
        for k in _range(half):
            fr[k] = lr[k]
            fi[k] = li[k]
        fi[0] = 0.0
        if n % 2 == 0:
            fi[n // 2] = 0.0
        for k in _range(1, (n + 1) // 2):
            fr[n - k] = lr[k]
            fi[n - k] = -li[k]
        rr, ri = _fft_raw(fr, fi, 1.0)
        if f != 1.0:
            rr = [v * f for v in rr]
        return _clean(rr, [0.0] * n)
    return _along(re, im, shape, ax, fn, n)


def irfft(a, n=None, axis=-1, norm=None):
    re, im, shape = _parts(a)
    re, im, shape = _c2r(re, im, shape, n, axis, norm)
    return _build_real(re, shape)


def hfft(a, n=None, axis=-1, norm=None):
    re, im, shape = _parts(a)
    im = [-v for v in im]
    re, im, shape = _c2r(re, im, shape, n, axis, norm, True)
    return _build_real(re, shape)


def ihfft(a, n=None, axis=-1, norm=None):
    re, im, shape = _parts(a)
    re, im, shape = _r2c(re, im, shape, n, axis, norm, True)
    im = [-v for v in im]
    return _build(re, im, shape)


# ----------------------------------------------------------------------------- n dimensional

def _axes_s(shape, s, axes, default_last=None):
    nd = len(shape)
    if axes is None:
        if s is None:
            axes = list(_range(nd)) if default_last is None else list(_range(nd - default_last, nd))
        else:
            axes = list(_range(nd - len(s), nd))
    else:
        axes = list(axes) if isinstance(axes, (list, tuple)) else [axes]
    axes = [_axis(a, nd) for a in axes]
    if s is not None:
        s = list(s) if isinstance(s, (list, tuple)) else [s]
        if len(s) != len(axes):
            raise ValueError("Shape and axes have different lengths.")
    return s, axes


def _nd(a, s, axes, norm, inverse):
    re, im, shape = _parts(a)
    s, axes = _axes_s(shape, s, axes)
    for i in _range(len(axes) - 1, -1, -1):
        n = None if s is None else s[i]
        if n == -1:
            n = None
        re, im, shape = _c2c(re, im, shape, n, axes[i], norm, inverse)
    return _build(re, im, shape)


def fftn(a, s=None, axes=None, norm=None):
    return _nd(a, s, axes, norm, False)


def ifftn(a, s=None, axes=None, norm=None):
    return _nd(a, s, axes, norm, True)


def fft2(a, s=None, axes=(-2, -1), norm=None):
    return _nd(a, s, axes, norm, False)


def ifft2(a, s=None, axes=(-2, -1), norm=None):
    return _nd(a, s, axes, norm, True)


def rfftn(a, s=None, axes=None, norm=None):
    re, im, shape = _parts(a)
    s, axes = _axes_s(shape, s, axes)
    last = len(axes) - 1
    n = None if s is None else s[last]
    re, im, shape = _r2c(re, im, shape, n, axes[last], norm)
    for i in _range(last - 1, -1, -1):
        n = None if s is None else s[i]
        re, im, shape = _c2c(re, im, shape, n, axes[i], norm, False)
    return _build(re, im, shape)


def irfftn(a, s=None, axes=None, norm=None):
    re, im, shape = _parts(a)
    s, axes = _axes_s(shape, s, axes)
    last = len(axes) - 1
    for i in _range(0, last):
        n = None if s is None else s[i]
        re, im, shape = _c2c(re, im, shape, n, axes[i], norm, True)
    n = None if s is None else s[last]
    re, im, shape = _c2r(re, im, shape, n, axes[last], norm)
    return _build_real(re, shape)


def rfft2(a, s=None, axes=(-2, -1), norm=None):
    return rfftn(a, s, axes, norm)


def irfft2(a, s=None, axes=(-2, -1), norm=None):
    return irfftn(a, s, axes, norm)


# ----------------------------------------------------------------------------- helpers

def fftfreq(n, d=1.0):
    if not isinstance(n, int):
        raise ValueError("n should be an integer")
    val = 1.0 / (n * d)
    N = (n - 1) // 2 + 1
    res = [0] * n
    for i in _range(N):
        res[i] = i
    k = -(n // 2)
    for i in _range(N, n):
        res[i] = k
        k += 1
    return np.array([v * val for v in res])


def rfftfreq(n, d=1.0):
    if not isinstance(n, int):
        raise ValueError("n should be an integer")
    val = 1.0 / (n * d)
    return np.array([i * val for i in _range(n // 2 + 1)])


def _roll(x, shifts, axes):
    a = np.asarray(x)
    shape = tuple(a.shape)
    flat = a.flatten().tolist()
    for ax, sh in zip(axes, shifts):
        outer, n, inner = _lines(shape, ax)
        if n == 0:
            continue
        sh = sh % n
        if sh == 0:
            continue
        out = list(flat)
        for o in _range(outer):
            for k in _range(n):
                dst = ((k + sh) % n)
                src_base = (o * n + k) * inner
                dst_base = (o * n + dst) * inner
                for i in _range(inner):
                    out[dst_base + i] = flat[src_base + i]
        flat = out
    if len(shape) == 0:
        return a
    return np.array(flat).reshape(shape)


def _shift_axes(x, axes):
    nd = np.asarray(x).ndim
    if axes is None:
        return list(_range(nd))
    if isinstance(axes, (list, tuple)):
        return [_axis(a, nd) for a in axes]
    return [_axis(axes, nd)]


def fftshift(x, axes=None):
    a = np.asarray(x)
    axs = _shift_axes(a, axes)
    return _roll(a, [a.shape[i] // 2 for i in axs], axs)


def ifftshift(x, axes=None):
    a = np.asarray(x)
    axs = _shift_axes(a, axes)
    return _roll(a, [-(a.shape[i] // 2) for i in axs], axs)
