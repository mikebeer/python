"""scipy.signal subset: convolve / correlate (direct), convolve2d, find_peaks and friends, savgol_filter, medfilt,
detrend, lfilter, argrelextrema, windows."""
import math
import numpy as np
from .._util import inf, nan, tolist
from .. import _la
from . import windows


def _raw(x):
    a = np.asarray(x)
    return a.flatten().tolist() if a.ndim > 0 else [a.tolist()]


def _conv_full(a, b):
    n1 = len(a)
    n2 = len(b)
    out = [0] * (n1 + n2 - 1)
    for i in range(n1):
        ai = a[i]
        if ai == 0:
            continue
        for j in range(n2):
            out[i + j] += ai * b[j]
    return out


def _centered(v, n):
    start = (len(v) - n) // 2
    return v[start:start + n]


def convolve(in1, in2, mode="full", method="auto"):
    a = _raw(in1)
    b = _raw(in2)
    if len(a) == 0 or len(b) == 0:
        return np.array([])
    if mode == "valid" and len(b) > len(a):
        a, b = b, a
    full = _conv_full(a, b)
    if mode == "full":
        return np.array(full)
    if mode == "same":
        return np.array(_centered(full, len(_raw(in1))))
    if mode == "valid":
        n = max(len(a), len(b)) - min(len(a), len(b)) + 1
        return np.array(_centered(full, n))
    raise ValueError("acceptable mode flags are 'valid', 'same', or 'full'")


def fftconvolve(in1, in2, mode="full", axes=None):
    r = convolve(in1, in2, mode)
    return r * 1.0


oaconvolve = fftconvolve


def correlate(in1, in2, mode="full", method="auto"):
    a = _raw(in1)
    b = _raw(in2)
    swapped = False
    if mode == "valid" and len(b) > len(a):
        a, b = b, a
        swapped = True
    full = _conv_full(a, b[::-1])
    if mode == "full":
        out = full
    elif mode == "same":
        out = _centered(full, len(_raw(in1)))
    elif mode == "valid":
        n = len(a) - len(b) + 1
        out = _centered(full, n)
        if swapped:
            out = out[::-1]
    else:
        raise ValueError("acceptable mode flags are 'valid', 'same', or 'full'")
    return np.array(out)


def correlation_lags(in1_len, in2_len, mode="full"):
    if mode == "full":
        return np.arange(-in2_len + 1, in1_len)
    if mode == "same":
        lags = np.arange(-in2_len + 1, in1_len)
        mid = lags.size // 2
        lag_bound = in1_len // 2
        if in1_len % 2 == 0:
            lags = lags[(mid - lag_bound):(mid + lag_bound)]
        else:
            lags = lags[(mid - lag_bound):(mid + lag_bound) + 1]
        return lags
    if mode == "valid":
        lag_bound = in1_len - in2_len
        if in1_len >= in2_len:
            return np.arange(0, lag_bound + 1)
        return np.arange(lag_bound, 1)
    raise ValueError("Mode must be 'full', 'valid' or 'same'")


def convolve2d(in1, in2, mode="full", boundary="fill", fillvalue=0, _corr=False):
    a = np.asarray(in1).tolist()
    k = np.asarray(in2).tolist()
    ra = len(a)
    ca = len(a[0])
    rk = len(k)
    ck = len(k[0])
    fr = ra + rk - 1
    fc = ca + ck - 1
    full = [[0] * fc for i in range(fr)]
    for i in range(ra):
        for j in range(ca):
            v = a[i][j]
            if v == 0:
                continue
            for p in range(rk):
                for q in range(ck):
                    full[i + p][j + q] += v * k[p][q]
    if mode == "full":
        return np.array(full)
    if mode == "same":
        r0 = (fr - ra) // 2
        c0 = (fc - ca) // 2
        if _corr:
            r0 = rk // 2
            c0 = ck // 2
        return np.array([row[c0:c0 + ca] for row in full[r0:r0 + ra]])
    if mode == "valid":
        nr = max(ra, rk) - min(ra, rk) + 1
        nc = max(ca, ck) - min(ca, ck) + 1
        r0 = (fr - nr) // 2
        c0 = (fc - nc) // 2
        return np.array([row[c0:c0 + nc] for row in full[r0:r0 + nr]])
    raise ValueError("mode")


def correlate2d(in1, in2, mode="full", boundary="fill", fillvalue=0):
    k = np.asarray(in2).tolist()
    rev = [r[::-1] for r in k[::-1]]
    return convolve2d(in1, rev, mode, boundary, fillvalue, True)


# ---- peaks -----------------------------------------------------------------

def _local_maxima(x):
    n = len(x)
    mids = []
    lefts = []
    rights = []
    i = 1
    i_max = n - 1
    while i < i_max:
        if x[i - 1] < x[i]:
            ahead = i + 1
            while ahead < i_max and x[ahead] == x[i]:
                ahead += 1
            if x[ahead] < x[i]:
                lefts.append(i)
                rights.append(ahead - 1)
                mids.append((i + ahead - 1) // 2)
                i = ahead
        i += 1
    return mids, lefts, rights


def _interval(v):
    if isinstance(v, (list, tuple, np.ndarray)):
        vl = list(v) if not isinstance(v, np.ndarray) else v.tolist()
        return (vl[0], vl[1])
    return (v, None)


def peak_prominences(x, peaks, wlen=None):
    xl = tolist(x)
    n = len(xl)
    if wlen is not None:
        wlen = int(math.ceil(wlen))
    pr = []
    lb = []
    rb = []
    for peak in tolist(peaks):
        peak = int(peak)
        i_min = 0
        i_max = n - 1
        if peak < i_min or peak > i_max:
            raise ValueError("peak %d is not a valid index for `x`" % peak)
        if wlen is not None and wlen >= 2:
            i_min = max(peak - wlen // 2, i_min)
            i_max = min(peak + wlen // 2, i_max)
        i = peak
        left_min = xl[peak]
        left_base = peak
        while i_min <= i and xl[i] <= xl[peak]:
            if xl[i] < left_min:
                left_min = xl[i]
                left_base = i
            i -= 1
        i = peak
        right_min = xl[peak]
        right_base = peak
        while i <= i_max and xl[i] <= xl[peak]:
            if xl[i] < right_min:
                right_min = xl[i]
                right_base = i
            i += 1
        pr.append(xl[peak] - max(left_min, right_min))
        lb.append(left_base)
        rb.append(right_base)
    return np.array(pr), np.array(lb), np.array(rb)


def peak_widths(x, peaks, rel_height=0.5, prominence_data=None, wlen=None):
    xl = tolist(x)
    pk = [int(p) for p in tolist(peaks)]
    if prominence_data is None:
        prominence_data = peak_prominences(x, peaks, wlen)
    prom = tolist(prominence_data[0])
    lbs = [int(v) for v in tolist(prominence_data[1])]
    rbs = [int(v) for v in tolist(prominence_data[2])]
    w = []
    wh = []
    lip = []
    rip = []
    for k in range(len(pk)):
        p = pk[k]
        i_min = lbs[k]
        i_max = rbs[k]
        height = xl[p] - prom[k] * rel_height
        i = p
        while i_min < i and height < xl[i]:
            i -= 1
        left_ip = float(i)
        if xl[i] < height:
            left_ip += (height - xl[i]) / (xl[i + 1] - xl[i])
        i = p
        while i < i_max and height < xl[i]:
            i += 1
        right_ip = float(i)
        if xl[i] < height:
            right_ip -= (height - xl[i]) / (xl[i - 1] - xl[i])
        w.append(right_ip - left_ip)
        wh.append(height)
        lip.append(left_ip)
        rip.append(right_ip)
    return np.array(w), np.array(wh), np.array(lip), np.array(rip)


def find_peaks(x, height=None, threshold=None, distance=None, prominence=None, width=None, wlen=None,
               rel_height=0.5, plateau_size=None):
    xl = tolist(x)
    if distance is not None and distance < 1:
        raise ValueError("`distance` must be greater or equal to 1")
    mids, lefts, rights = _local_maxima(xl)
    props = {}

    def select(keep):
        nonlocal mids, lefts, rights
        mids2 = [mids[i] for i in range(len(mids)) if keep[i]]
        lefts = [lefts[i] for i in range(len(lefts)) if keep[i]]
        rights = [rights[i] for i in range(len(rights)) if keep[i]]
        for k in props:
            props[k] = [props[k][i] for i in range(len(props[k])) if keep[i]]
        mids = mids2
    if plateau_size is not None:
        sizes = [rights[i] - lefts[i] + 1 for i in range(len(mids))]
        pmin, pmax = _interval(plateau_size)
        keep = [True] * len(mids)
        for i in range(len(mids)):
            if pmin is not None and sizes[i] < pmin:
                keep[i] = False
            if pmax is not None and sizes[i] > pmax:
                keep[i] = False
        props["plateau_sizes"] = sizes
        props["left_edges"] = list(lefts)
        props["right_edges"] = list(rights)
        select(keep)
    if height is not None:
        hmin, hmax = _interval(height)
        hs = [xl[p] for p in mids]
        keep = [True] * len(mids)
        for i in range(len(mids)):
            if hmin is not None and hs[i] < hmin:
                keep[i] = False
            if hmax is not None and hs[i] > hmax:
                keep[i] = False
        props["peak_heights"] = hs
        select(keep)
    if threshold is not None:
        tmin, tmax = _interval(threshold)
        lt = [xl[p] - xl[p - 1] for p in mids]
        rt = [xl[p] - xl[p + 1] for p in mids]
        keep = [True] * len(mids)
        for i in range(len(mids)):
            if tmin is not None and (lt[i] < tmin or rt[i] < tmin):
                keep[i] = False
            if tmax is not None and (lt[i] > tmax or rt[i] > tmax):
                keep[i] = False
        props["left_thresholds"] = lt
        props["right_thresholds"] = rt
        select(keep)
    if distance is not None:
        dist = int(math.ceil(distance))
        n = len(mids)
        keep = [True] * n
        order = sorted(range(n), key=lambda i: xl[mids[i]])
        for ii in range(n - 1, -1, -1):
            j = order[ii]
            if not keep[j]:
                continue
            k = j - 1
            while k >= 0 and mids[j] - mids[k] < dist:
                keep[k] = False
                k -= 1
            k = j + 1
            while k < n and mids[k] - mids[j] < dist:
                keep[k] = False
                k += 1
        select(keep)
    if prominence is not None or width is not None:
        pr, lb, rb = peak_prominences(xl, mids, wlen)
        prl = tolist(pr)
        pmin, pmax = _interval(prominence) if prominence is not None else (None, None)
        keep = [True] * len(mids)
        for i in range(len(mids)):
            if pmin is not None and prl[i] < pmin:
                keep[i] = False
            if pmax is not None and prl[i] > pmax:
                keep[i] = False
        props["prominences"] = prl
        props["left_bases"] = [int(v) for v in tolist(lb)]
        props["right_bases"] = [int(v) for v in tolist(rb)]
        select(keep)
    if width is not None:
        wd, wh, lip, rip = peak_widths(xl, mids, rel_height, (np.array(props["prominences"]),
                                                              np.array(props["left_bases"]),
                                                              np.array(props["right_bases"])))
        wl = tolist(wd)
        wmin, wmax = _interval(width)
        keep = [True] * len(mids)
        for i in range(len(mids)):
            if wmin is not None and wl[i] < wmin:
                keep[i] = False
            if wmax is not None and wl[i] > wmax:
                keep[i] = False
        props["widths"] = wl
        props["width_heights"] = tolist(wh)
        props["left_ips"] = tolist(lip)
        props["right_ips"] = tolist(rip)
        select(keep)
    out = {}
    for k in props:
        if k in ("left_bases", "right_bases", "plateau_sizes", "left_edges", "right_edges"):
            out[k] = np.array([int(v) for v in props[k]])
        else:
            out[k] = np.array(props[k])
    return np.array([int(p) for p in mids]), out


def argrelextrema(data, comparator, axis=0, order=1, mode="clip"):
    d = tolist(data)
    n = len(d)
    res = []
    for i in range(n):
        ok = True
        for k in range(1, order + 1):
            lo = i - k
            hi = i + k
            if mode == "wrap":
                lo %= n
                hi %= n
            else:
                lo = max(lo, 0)
                hi = min(hi, n - 1)
            if not (comparator(d[i], d[lo]) and comparator(d[i], d[hi])):
                ok = False
                break
        if ok:
            res.append(i)
    return (np.array(res),)


def argrelmax(data, axis=0, order=1, mode="clip"):
    return argrelextrema(data, lambda a, b: a > b, axis, order, mode)


def argrelmin(data, axis=0, order=1, mode="clip"):
    return argrelextrema(data, lambda a, b: a < b, axis, order, mode)


# ---- filters ---------------------------------------------------------------

def savgol_coeffs(window_length, polyorder, deriv=0, delta=1.0, pos=None, use="conv"):
    if polyorder >= window_length:
        raise ValueError("polyorder must be less than window_length.")
    halflen, rem = divmod(window_length, 2)
    if pos is None:
        if rem == 0:
            pos = halflen - 0.5
        else:
            pos = halflen
    if not (0 <= pos < window_length):
        raise ValueError("pos must be nonnegative and less than window_length.")
    x = [float(i) - pos for i in range(window_length)]
    if use == "conv":
        x = x[::-1]
    A = [[xi ** o for xi in x] for o in range(polyorder + 1)]
    y = [0.0] * (polyorder + 1)
    if deriv <= polyorder:
        y[deriv] = math.factorial(deriv) / (delta ** deriv)
    AAt = _la.matmul(A, _la.transpose(A))
    z = _la.solve(AAt, y)
    c = _la.matvec(_la.transpose(A), z)
    return np.array(c)


def _polyfit_eval(ts, ys, order, deriv, delta, at):
    """least squares polynomial of `order` through (ts, ys); derivative `deriv`/delta**deriv evaluated at `at`"""
    n = len(ts)
    mid = (ts[0] + ts[-1]) / 2.0
    tt = [t - mid for t in ts]
    V = [[t ** (order - j) for j in range(order + 1)] for t in tt]
    Vt = _la.transpose(V)
    coef = _la.solve(_la.matmul(Vt, V), _la.matvec(Vt, ys))
    for k in range(deriv):
        m = len(coef) - 1
        coef = [coef[i] * (m - i) for i in range(m)] if m > 0 else [0.0]
    out = []
    for a in at:
        t = a - mid
        r = 0.0
        for cc in coef:
            r = r * t + cc
        out.append(r / (delta ** deriv))
    return out


def savgol_filter(x, window_length, polyorder, deriv=0, delta=1.0, axis=-1, mode="interp", cval=0.0):
    if mode not in ("mirror", "constant", "nearest", "interp", "wrap"):
        raise ValueError("mode must be 'mirror', 'constant', 'nearest' 'wrap' or 'interp'.")
    xa = np.asarray(x) * 1.0
    if xa.ndim == 2:
        if axis in (0, -2):
            cols = [savgol_filter([r[j] for r in xa.tolist()], window_length, polyorder, deriv, delta, -1, mode, cval)
                    for j in range(xa.shape[1])]
            return np.array([[cols[j].tolist()[i] for j in range(len(cols))] for i in range(xa.shape[0])])
        return np.array([savgol_filter(r, window_length, polyorder, deriv, delta, -1, mode, cval).tolist()
                         for r in xa.tolist()])
    v = tolist(xa)
    n = len(v)
    c = tolist(savgol_coeffs(window_length, polyorder, deriv, delta))
    half = window_length // 2
    if mode == "interp":
        if window_length > n:
            raise ValueError("If mode is 'interp', window_length must be less than or equal to the size of x.")
        # convolve in the valid region, fit edges
        out = [0.0] * n
        for i in range(half, n - half):
            s = 0.0
            for k in range(window_length):
                s += c[k] * v[i + half - k]
            out[i] = s
        t = [float(i) for i in range(window_length)]
        lead = _polyfit_eval(t, v[:window_length], polyorder, deriv, delta, [float(i) for i in range(half)])
        tail = _polyfit_eval(t, v[n - window_length:], polyorder, deriv, delta,
                             [float(i) for i in range(window_length - half, window_length)])
        for i in range(half):
            out[i] = lead[i]
        for i in range(half):
            out[n - half + i] = tail[i]
        return np.array(out)
    # padded modes
    def get(i):
        if 0 <= i < n:
            return v[i]
        if mode == "constant":
            return cval
        if mode == "nearest":
            return v[0] if i < 0 else v[n - 1]
        if mode == "wrap":
            return v[i % n]
        # mirror (scipy 'mirror' = reflect without repeating the edge)
        period = 2 * (n - 1)
        j = i % period if period > 0 else 0
        if j >= n:
            j = period - j
        return v[j]
    out = []
    for i in range(n):
        s = 0.0
        for k in range(window_length):
            s += c[k] * get(i + half - k)
        out.append(s)
    return np.array(out)


def medfilt(volume, kernel_size=3):
    v = tolist(volume)
    k = int(kernel_size)
    if k % 2 == 0:
        raise ValueError("Each element of kernel_size should be odd.")
    h = k // 2
    n = len(v)
    out = []
    for i in range(n):
        w = []
        for j in range(i - h, i + h + 1):
            w.append(v[j] if 0 <= j < n else 0.0)
        w.sort()
        out.append(w[h])
    return np.array(out)


def detrend(data, axis=-1, type="linear", bp=0, overwrite_data=False):
    a = np.asarray(data) * 1.0
    if a.ndim == 2:
        if axis in (0, -2):
            cols = [detrend([r[j] for r in a.tolist()], -1, type).tolist() for j in range(a.shape[1])]
            return np.array([[cols[j][i] for j in range(len(cols))] for i in range(a.shape[0])])
        return np.array([detrend(r, -1, type).tolist() for r in a.tolist()])
    v = tolist(a)
    n = len(v)
    if type == "constant":
        m = sum(v) / n
        return np.array([t - m for t in v])
    t = [float(i) for i in range(n)]
    tm = sum(t) / n
    vm = sum(v) / n
    sxy = 0.0
    sxx = 0.0
    for i in range(n):
        sxy += (t[i] - tm) * (v[i] - vm)
        sxx += (t[i] - tm) ** 2
    slope = sxy / sxx if sxx != 0 else 0.0
    return np.array([v[i] - (vm + slope * (t[i] - tm)) for i in range(n)])


def lfilter(b, a, x, axis=-1, zi=None):
    bl = tolist(b)
    al = tolist(a)
    a0 = al[0]
    bl = [t / a0 for t in bl]
    al = [t / a0 for t in al]
    n = max(len(bl), len(al))
    bl = bl + [0.0] * (n - len(bl))
    al = al + [0.0] * (n - len(al))
    z = [0.0] * (n - 1) if zi is None else tolist(zi)
    xs = tolist(x)
    out = []
    for xv in xs:
        yv = bl[0] * xv + (z[0] if n > 1 else 0.0)
        for i in range(n - 2):
            z[i] = z[i + 1] + bl[i + 1] * xv - al[i + 1] * yv
        if n > 1:
            z[n - 2] = bl[n - 1] * xv - al[n - 1] * yv
        out.append(yv)
    if zi is not None:
        return np.array(out), np.array(z)
    return np.array(out)


def lfilter_zi(b, a):
    bl = tolist(b)
    al = tolist(a)
    n = max(len(bl), len(al))
    bl = [t / al[0] for t in bl] + [0.0] * (n - len(bl))
    al = [t / al[0] for t in al] + [0.0] * (n - len(al))
    # steady state for unit step: solve (I - A) zi = B - a[1:] * b0
    m = n - 1
    IminusA = [[0.0] * m for i in range(m)]
    for i in range(m):
        IminusA[i][0] = al[i + 1]
        for j in range(m):
            IminusA[i][j] += (1.0 if i == j else 0.0) - (1.0 if j == i + 1 else 0.0)
    B = [bl[i + 1] - al[i + 1] * bl[0] for i in range(m)]
    return np.array(_la.solve(IminusA, B))


def filtfilt(b, a, x, axis=-1, padtype="odd", padlen=None, method="pad", irlen=None):
    bl = tolist(b)
    al = tolist(a)
    xs = tolist(x)
    ntaps = max(len(al), len(bl))
    edge = padlen if padlen is not None else 3 * ntaps
    if len(xs) <= edge:
        raise ValueError("The length of the input vector x must be greater than padlen, which is %d." % edge)
    if padtype == "odd":
        left = [2 * xs[0] - xs[edge - i] for i in range(edge)]
        right = [2 * xs[-1] - xs[-2 - i] for i in range(edge)]
        ext = left + xs + right
    elif padtype == "even":
        ext = xs[edge:0:-1] + xs + xs[-2:-edge - 2:-1]
    elif padtype == "constant":
        ext = [xs[0]] * edge + xs + [xs[-1]] * edge
    else:
        ext = xs
    zi = tolist(lfilter_zi(bl, al))
    y, _ = lfilter(bl, al, ext, zi=[t * ext[0] for t in zi])
    y = tolist(y)
    y2, _ = lfilter(bl, al, y[::-1], zi=[t * y[-1] for t in zi])
    y2 = tolist(y2)[::-1]
    return np.array(y2[edge:len(y2) - edge] if edge > 0 else y2)


def unit_impulse(shape, idx=None, dtype=float):
    n = shape if isinstance(shape, int) else shape[0]
    out = [0.0] * n
    if idx is None:
        idx = 0
    if idx == "mid":
        idx = n // 2
    out[idx] = 1.0
    return np.array(out)


def sawtooth(t, width=1):
    def f(v):
        tmod = v % (2 * math.pi)
        w = width * 2 * math.pi
        if tmod < w:
            return tmod / (math.pi * width) - 1 if width > 0 else 0.0
        return (math.pi * (width + 1) - tmod) / (math.pi * (1 - width)) if width < 1 else 0.0
    return np.array([f(v) for v in tolist(t)])


def square(t, duty=0.5):
    return np.array([1.0 if (v % (2 * math.pi)) < 2 * math.pi * duty else -1.0 for v in tolist(t)])


get_window = windows.get_window
