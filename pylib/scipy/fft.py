"""scipy.fft in plain Python on top of numpy.fft: the same transforms (the extra scipy keywords
overwrite_x / workers / plan are accepted and ignored) plus dct, idct, dst, idst, dctn, idctn,
dstn, idstn, next_fast_len and prev_fast_len."""
import math
import numpy as np

_range = range
_fft = np.fft

fftfreq = _fft.fftfreq
rfftfreq = _fft.rfftfreq
fftshift = _fft.fftshift
ifftshift = _fft.ifftshift


def _realfix(x, y, axis):
    """scipy computes the transform of real input with a real FFT and mirrors the result, which
    leaves a negative zero imaginary part in the DC (and Nyquist) bins"""
    if str(np.asarray(x).dtype)[:7] == 'complex':
        return y
    a = np.asarray(y)
    if a.ndim == 0:
        return y
    nd = a.ndim
    ax = axis + nd if axis < 0 else axis
    shape = tuple(a.shape)
    n = shape[ax]
    outer = 1
    for d in shape[:ax]:
        outer *= d
    inner = 1
    for d in shape[ax + 1:]:
        inner *= d
    flat = a.flatten().tolist()
    pos = [0]
    if n % 2 == 0 and n > 1:
        pos.append(n // 2)
    for o in _range(outer):
        for i in _range(inner):
            for k in pos:
                j = (o * n + k) * inner + i
                if flat[j].imag == 0.0:
                    flat[j] = complex(flat[j].real, -0.0)
    return np.array(flat).reshape(shape)


def fft(x, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, plan=None):
    return _realfix(x, _fft.fft(x, n, axis, norm), axis)


def ifft(x, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, plan=None):
    return _realfix(x, _fft.ifft(x, n, axis, norm), axis)


def rfft(x, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.rfft(x, n, axis, norm)


def irfft(x, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.irfft(x, n, axis, norm)


def hfft(x, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.hfft(x, n, axis, norm)


def ihfft(x, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.ihfft(x, n, axis, norm)


def fft2(x, s=None, axes=(-2, -1), norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.fft2(x, s, axes, norm)


def ifft2(x, s=None, axes=(-2, -1), norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.ifft2(x, s, axes, norm)


def rfft2(x, s=None, axes=(-2, -1), norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.rfft2(x, s, axes, norm)


def irfft2(x, s=None, axes=(-2, -1), norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.irfft2(x, s, axes, norm)


def fftn(x, s=None, axes=None, norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.fftn(x, s, axes, norm)


def ifftn(x, s=None, axes=None, norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.ifftn(x, s, axes, norm)


def rfftn(x, s=None, axes=None, norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.rfftn(x, s, axes, norm)


def irfftn(x, s=None, axes=None, norm=None, overwrite_x=False, workers=None, plan=None):
    return _fft.irfftn(x, s, axes, norm)


# ----------------------------------------------------------------------------- fast lengths

def _smooth(n, primes):
    for p in primes:
        while n % p == 0:
            n //= p
    return n == 1


def next_fast_len(target, real=False):
    if target < 0:
        raise ValueError("Target length must be positive")
    if target == 0:
        return 0
    primes = (2, 3, 5) if real else (2, 3, 5, 7, 11)
    n = target
    while not _smooth(n, primes):
        n += 1
    return n


def prev_fast_len(target, real=False):
    if target < 0:
        raise ValueError("Target length must be positive")
    if target == 0:
        return 0
    primes = (2, 3, 5) if real else (2, 3, 5, 7, 11)
    n = target
    while not _smooth(n, primes):
        n -= 1
    return n


# ----------------------------------------------------------------------------- DCT / DST

_SQ2 = math.sqrt(2.0)
_INV_TYPE = {1: 1, 2: 3, 3: 2, 4: 4}


def _snap(y):
    """rounding noise (below ~8 eps of the largest value) becomes +0.0"""
    m = 0.0
    for v in y:
        if v > m:
            m = v
        elif -v > m:
            m = -v
    t = m * 1.8e-15
    return [0.0 if (v <= t and -v <= t) else v for v in y]


def _cx(re, im):
    return np.array([complex(re[i], im[i]) for i in _range(len(re))])


def _ffts(re, im):
    """forward fft of a list pair -> (real list, imag list)"""
    r = _fft.fft(_cx(re, im)).tolist()
    return [v.real for v in r], [v.imag for v in r]


def _dct_raw(x, t):
    """unnormalised scipy DCT of type t of a list of floats"""
    N = len(x)
    z = [0.0] * N
    if t == 1:
        if N < 2:
            raise ValueError("invalid number of data points (%d) specified" % N)
        ext = list(x) + x[N - 2:0:-1]
        fr, fi = _ffts(ext, [0.0] * len(ext))
        return fr[:N]
    if t == 2:
        fr, fi = _ffts(list(x) + z, [0.0] * (2 * N))
        out = []
        for k in _range(N):
            a = math.pi * k / (2.0 * N)
            out.append(2.0 * (fr[k] * math.cos(a) + fi[k] * math.sin(a)))
        return out
    if t == 3:
        ar = [0.0] * (2 * N)
        ai = [0.0] * (2 * N)
        for n in _range(N):
            v = x[n] * 0.5 if n == 0 else x[n]
            a = math.pi * n / (2.0 * N)
            ar[n] = v * math.cos(a)
            ai[n] = -v * math.sin(a)       # conj(a)
        fr, fi = _ffts(ar, ai)
        return [2.0 * fr[k] for k in _range(N)]
    if t == 4:
        br = [0.0] * (2 * N)
        bi = [0.0] * (2 * N)
        for n in _range(N):
            a = math.pi * n / (2.0 * N)
            br[n] = x[n] * math.cos(a)
            bi[n] = -x[n] * math.sin(a)
        fr, fi = _ffts(br, bi)
        out = []
        for k in _range(N):
            a = math.pi * (2 * k + 1) / (4.0 * N)
            out.append(2.0 * (fr[k] * math.cos(a) + fi[k] * math.sin(a)))
        return out
    raise ValueError("Type %d is not a supported DCT type" % t)


def _dst_raw(x, t):
    N = len(x)
    if t == 1:
        ext = [0.0] + list(x) + [0.0] + [-v for v in x[::-1]]
        fr, fi = _ffts(ext, [0.0] * len(ext))
        return [-fi[k + 1] for k in _range(N)]
    if t == 2:
        fr, fi = _ffts(list(x) + [0.0] * N, [0.0] * (2 * N))
        out = []
        for k in _range(N):
            a = math.pi * (k + 1) / (2.0 * N)
            # Im(e^{-ia} F) = fi cos(a) - fr sin(a)
            out.append(-2.0 * (fi[k + 1] * math.cos(a) - fr[k + 1] * math.sin(a)))
        return out
    if t == 3:
        ar = [0.0] * (2 * N)
        ai = [0.0] * (2 * N)
        for n in _range(N):
            v = x[n] * 0.5 if n == N - 1 else x[n]
            a = math.pi * (n + 1) / (2.0 * N)
            ar[n + 1] = v * math.cos(a)
            ai[n + 1] = -v * math.sin(a)   # conj
        fr, fi = _ffts(ar, ai)
        return [-2.0 * fi[k] for k in _range(N)]
    if t == 4:
        br = [0.0] * (2 * N)
        bi = [0.0] * (2 * N)
        for n in _range(N):
            a = math.pi * n / (2.0 * N)
            br[n] = x[n] * math.cos(a)
            bi[n] = -x[n] * math.sin(a)
        fr, fi = _ffts(br, bi)
        out = []
        for k in _range(N):
            a = math.pi * (2 * k + 1) / (4.0 * N)
            out.append(-2.0 * (fi[k] * math.cos(a) - fr[k] * math.sin(a)))
        return out
    raise ValueError("Type %d is not a supported DST type" % t)


def _line(x, t, norm, inverse, orth, dst):
    N = len(x)
    if dst:
        nn = 2 * (N + 1) if t == 1 else 2 * N
    else:
        nn = 2 * (N - 1) if t == 1 else 2 * N
    kt = _INV_TYPE[t] if inverse else t
    x = list(x)
    f = 1.0
    if norm == 'ortho':
        f = math.sqrt(1.0 / nn)
        if orth:
            if not dst:
                if kt == 1:
                    x[0] *= _SQ2
                    x[N - 1] *= _SQ2
                elif kt == 3:
                    x[0] *= _SQ2
            elif kt == 3:
                x[N - 1] *= _SQ2
    elif norm == 'forward':
        f = 1.0 if inverse else 1.0 / nn
    else:
        f = 1.0 / nn if inverse else 1.0
    y = _dst_raw(x, kt) if dst else _dct_raw(x, kt)
    if norm == 'ortho' and orth:
        if not dst:
            if kt == 1:
                y[0] /= _SQ2
                y[N - 1] /= _SQ2
            elif kt == 2:
                y[0] /= _SQ2
        elif kt == 2:
            y[N - 1] /= _SQ2
    if f != 1.0:
        y = [v * f for v in y]
    return _snap(y)


def _check(t, norm):
    if t not in (1, 2, 3, 4):
        raise ValueError("Type %s is not a supported DCT type" % (t,))
    if norm is not None and norm not in ('backward', 'ortho', 'forward'):
        raise ValueError('Invalid norm value %s; should be "backward", "ortho" or "forward".' % (norm,))


def _trig(x, t, n, axis, norm, orth, inverse, dst):
    _check(t, norm)
    a = np.asarray(x)
    shape = tuple(a.shape)
    nd = len(shape)
    if axis < -nd or axis >= nd:
        raise ValueError("axis %d is out of bounds for array of dimension %d" % (axis, nd))
    ax = axis + nd if axis < 0 else axis
    cplx = str(a.dtype)[:7] == 'complex'
    flat = a.flatten().tolist()
    outer = 1
    for d in shape[:ax]:
        outer *= d
    inner = 1
    for d in shape[ax + 1:]:
        inner *= d
    m = shape[ax]
    if n is None:
        n = m
    if orth is None:
        orth = norm == 'ortho'
    out = [0.0] * (outer * n * inner)
    outi = [0.0] * (outer * n * inner) if cplx else None
    for o in _range(outer):
        for i in _range(inner):
            base = o * m * inner + i
            lr = [float(flat[base + k * inner].real) for k in _range(m)]
            lr = lr[:n] + [0.0] * (n - m) if n > m else lr[:n]
            yr = _line(lr, t, norm, inverse, orth, dst)
            if cplx:
                li = [float(flat[base + k * inner].imag) for k in _range(m)]
                li = li[:n] + [0.0] * (n - m) if n > m else li[:n]
                yi = _line(li, t, norm, inverse, orth, dst)
            obase = o * n * inner + i
            for k in _range(n):
                out[obase + k * inner] = yr[k]
                if cplx:
                    outi[obase + k * inner] = yi[k]
    nshape = shape[:ax] + (n,) + shape[ax + 1:]
    if cplx:
        out = [complex(out[i], outi[i]) for i in _range(len(out))]
    if nd == 0:
        return out[0]
    return np.array(out).reshape(nshape)


def dct(x, type=2, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, orthogonalize=None):
    return _trig(x, type, n, axis, norm, orthogonalize, False, False)


def idct(x, type=2, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, orthogonalize=None):
    return _trig(x, type, n, axis, norm, orthogonalize, True, False)


def dst(x, type=2, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, orthogonalize=None):
    return _trig(x, type, n, axis, norm, orthogonalize, False, True)


def idst(x, type=2, n=None, axis=-1, norm=None, overwrite_x=False, workers=None, orthogonalize=None):
    return _trig(x, type, n, axis, norm, orthogonalize, True, True)


def _trign(x, t, s, axes, norm, orth, inverse, dst):
    a = np.asarray(x)
    nd = a.ndim
    if axes is None:
        if s is None:
            axes = list(_range(nd))
        else:
            axes = list(_range(nd - len(s), nd))
    elif isinstance(axes, int):
        axes = [axes]
    else:
        axes = list(axes)
    if s is not None and isinstance(s, int):
        s = [s]
    for i in _range(len(axes) - 1, -1, -1):
        n = None if s is None else s[i]
        a = _trig(a, t, n, axes[i], norm, orth, inverse, dst)
    return a


def dctn(x, type=2, s=None, axes=None, norm=None, overwrite_x=False, workers=None, orthogonalize=None):
    return _trign(x, type, s, axes, norm, orthogonalize, False, False)


def idctn(x, type=2, s=None, axes=None, norm=None, overwrite_x=False, workers=None, orthogonalize=None):
    return _trign(x, type, s, axes, norm, orthogonalize, True, False)


def dstn(x, type=2, s=None, axes=None, norm=None, overwrite_x=False, workers=None, orthogonalize=None):
    return _trign(x, type, s, axes, norm, orthogonalize, False, True)


def idstn(x, type=2, s=None, axes=None, norm=None, overwrite_x=False, workers=None, orthogonalize=None):
    return _trign(x, type, s, axes, norm, orthogonalize, True, True)
