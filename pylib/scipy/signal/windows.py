"""scipy.signal.windows subset"""
import math
import numpy as np


def _extend(M, sym):
    if not sym:
        return M + 1, True
    return M, False


def _truncate(w, needed):
    return w[:-1] if needed else w


def _gen(M, sym, f):
    if M == 0:
        return np.array([])
    if M == 1:
        return np.ones(1)
    M2, trunc = _extend(M, sym)
    return np.array(_truncate([f(n, M2) for n in range(M2)], trunc))


def boxcar(M, sym=True):
    return np.ones(M) if M > 0 else np.array([])


def hann(M, sym=True):
    return _gen(M, sym, lambda n, m: 0.5 - 0.5 * math.cos(2.0 * math.pi * n / (m - 1)))


def hamming(M, sym=True):
    return _gen(M, sym, lambda n, m: 0.54 - 0.46 * math.cos(2.0 * math.pi * n / (m - 1)))


def blackman(M, sym=True):
    return _gen(M, sym, lambda n, m: 0.42 - 0.5 * math.cos(2.0 * math.pi * n / (m - 1))
                + 0.08 * math.cos(4.0 * math.pi * n / (m - 1)))


def bartlett(M, sym=True):
    def f(n, m):
        if n <= (m - 1) / 2.0:
            return 2.0 * n / (m - 1)
        return 2.0 - 2.0 * n / (m - 1)
    return _gen(M, sym, f)


def triang(M, sym=True):
    def f(n, m):
        k = n + 1
        if m % 2 == 0:
            if k <= m // 2:
                return (2.0 * k - 1.0) / m
            return (2.0 * (m - k + 1) - 1.0) / m
        if k <= (m + 1) // 2:
            return 2.0 * k / (m + 1.0)
        return 2.0 * (m - k + 1) / (m + 1.0)
    return _gen(M, sym, f)


def gaussian(M, std, sym=True):
    def f(n, m):
        x = n - (m - 1.0) / 2.0
        return math.exp(-x * x / (2.0 * std * std))
    return _gen(M, sym, f)


def _i0(x):
    s = 1.0
    t = 1.0
    k = 1
    while True:
        t *= (x / (2.0 * k)) ** 2
        s += t
        if t < 1e-17 * s:
            break
        k += 1
    return s


def kaiser(M, beta, sym=True):
    def f(n, m):
        alpha = (m - 1) / 2.0
        r = (n - alpha) / alpha
        return _i0(beta * math.sqrt(max(0.0, 1.0 - r * r))) / _i0(beta)
    return _gen(M, sym, f)


def cosine(M, sym=True):
    return _gen(M, sym, lambda n, m: math.sin(math.pi / m * (n + 0.5)))


def get_window(window, Nx, fftbins=True):
    sym = not fftbins
    if isinstance(window, tuple):
        name = window[0]
        args = window[1:]
    else:
        name = window
        args = ()
    fn = {"hann": hann, "hanning": hann, "hamming": hamming, "blackman": blackman, "bartlett": bartlett,
          "boxcar": boxcar, "rectangular": boxcar, "ones": boxcar, "triang": triang, "gaussian": gaussian,
          "kaiser": kaiser, "cosine": cosine}.get(name)
    if fn is None:
        raise ValueError("Unknown window type.")
    if fn is boxcar:
        return fn(Nx)
    return fn(Nx, *args, sym=sym)
