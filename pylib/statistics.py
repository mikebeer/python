"""statistics: basic descriptive statistics."""
import math as _m


class StatisticsError(ValueError):
    pass


def _fsum(data):
    return _m.fsum(data)


def mean(data):
    data = list(data)
    n = len(data)
    if n < 1:
        raise StatisticsError("mean requires at least one data point")
    allint = True
    for x in data:
        if not isinstance(x, int):
            allint = False
    if allint:
        t = sum(data)
        if t % n == 0:
            return t // n
        return t / n
    return _fsum(data) / n


def fmean(data, weights=None):
    data = list(data)
    n = len(data)
    if n < 1:
        raise StatisticsError("fmean requires at least one data point")
    if weights is not None:
        w = list(weights)
        return _fsum([a * b for a, b in zip(data, w)]) / _fsum(w)
    return _fsum(data) / n


def geometric_mean(data):
    data = list(data)
    if not data:
        raise StatisticsError("geometric mean requires a non-empty dataset containing positive numbers")
    return _m.exp(_fsum([_m.log(x) for x in data]) / len(data))


def harmonic_mean(data):
    data = list(data)
    if not data:
        raise StatisticsError("harmonic_mean requires at least one data point")
    return len(data) / _fsum([1.0 / x for x in data])


def median(data):
    data = sorted(data)
    n = len(data)
    if n == 0:
        raise StatisticsError("no median for empty data")
    if n % 2 == 1:
        return data[n // 2]
    i = n // 2
    a = data[i - 1]
    b = data[i]
    return (a + b) / 2


def median_low(data):
    data = sorted(data)
    n = len(data)
    if n == 0:
        raise StatisticsError("no median for empty data")
    if n % 2 == 1:
        return data[n // 2]
    return data[n // 2 - 1]


def median_high(data):
    data = sorted(data)
    n = len(data)
    if n == 0:
        raise StatisticsError("no median for empty data")
    return data[n // 2]


def median_grouped(data, interval=1):
    data = sorted(data)
    n = len(data)
    if n == 0:
        raise StatisticsError("no median for empty data")
    x = data[n // 2]
    l = x - interval / 2
    l1 = data.index(x)
    f = data.count(x)
    return l + interval * (n / 2 - l1) / f


def mode(data):
    data = list(data)
    if not data:
        raise StatisticsError("no mode for empty data")
    counts = {}
    for x in data:
        counts[x] = counts.get(x, 0) + 1
    best = None
    bc = 0
    for x in counts:
        if counts[x] > bc:
            bc = counts[x]
            best = x
    return best


def multimode(data):
    counts = {}
    for x in data:
        counts[x] = counts.get(x, 0) + 1
    if not counts:
        return []
    mx = max(counts.values())
    return [x for x in counts if counts[x] == mx]


def _ss(data, c=None):
    if c is None:
        c = fmean(data)
    return _fsum([(x - c) ** 2 for x in data])


def variance(data, xbar=None):
    data = list(data)
    n = len(data)
    if n < 2:
        raise StatisticsError("variance requires at least two data points")
    if xbar is None and all([isinstance(x, int) for x in data]):
        t = sum(data)
        ss = sum([(x * n - t) ** 2 for x in data])
        d = n * n * (n - 1)
        if ss % d == 0:
            return ss // d
        return ss / d
    return _ss(data, xbar) / (n - 1)


def pvariance(data, mu=None):
    data = list(data)
    n = len(data)
    if n < 1:
        raise StatisticsError("pvariance requires at least one data point")
    if mu is None and all([isinstance(x, int) for x in data]):
        t = sum(data)
        ss = sum([(x * n - t) ** 2 for x in data])
        d = n * n * n
        if ss % d == 0:
            return ss // d
        return ss / d
    return _ss(data, mu) / n


def stdev(data, xbar=None):
    return _m.sqrt(variance(data, xbar))


def pstdev(data, mu=None):
    return _m.sqrt(pvariance(data, mu))


def covariance(x, y):
    x = list(x)
    y = list(y)
    n = len(x)
    if n != len(y):
        raise StatisticsError("covariance requires that both inputs have same number of data points")
    if n < 2:
        raise StatisticsError("covariance requires at least two data points")
    mx = fmean(x)
    my = fmean(y)
    return _fsum([(a - mx) * (b - my) for a, b in zip(x, y)]) / (n - 1)


def correlation(x, y):
    x = list(x)
    y = list(y)
    n = len(x)
    if n != len(y):
        raise StatisticsError("correlation requires that both inputs have same number of data points")
    if n < 2:
        raise StatisticsError("correlation requires at least two data points")
    mx = fmean(x)
    my = fmean(y)
    sxy = _fsum([(a - mx) * (b - my) for a, b in zip(x, y)])
    sxx = _fsum([(a - mx) ** 2 for a in x])
    syy = _fsum([(b - my) ** 2 for b in y])
    return sxy / _m.sqrt(sxx * syy)


def linear_regression(x, y):
    x = list(x)
    y = list(y)
    mx = fmean(x)
    my = fmean(y)
    sxy = _fsum([(a - mx) * (b - my) for a, b in zip(x, y)])
    sxx = _fsum([(a - mx) ** 2 for a in x])
    slope = sxy / sxx
    return (slope, my - slope * mx)


def quantiles(data, n=4, method="exclusive"):
    data = sorted(data)
    ld = len(data)
    if ld < 2:
        raise StatisticsError("must have at least two data points")
    result = []
    if method == "inclusive":
        m = ld - 1
        for i in range(1, n):
            j = i * m // n
            delta = i * m - j * n
            result.append((data[j] * (n - delta) + data[j + 1] * delta) / n)
        return result
    m = ld + 1
    for i in range(1, n):
        j = i * m // n
        j = 1 if j < 1 else ld - 1 if j > ld - 1 else j
        delta = i * m - j * n
        result.append((data[j - 1] * (n - delta) + data[j] * delta) / n)
    return result
