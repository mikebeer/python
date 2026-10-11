# Pure Python additions to the native random module.
import math as _math
import random as _r

_gauss_next = None


def gauss(mu=0.0, sigma=1.0):
    u1 = _r.random()
    while u1 <= 1e-300:
        u1 = _r.random()
    u2 = _r.random()
    z = _math.sqrt(-2.0 * _math.log(u1)) * _math.cos(2.0 * _math.pi * u2)
    return mu + z * sigma


normalvariate = gauss


def lognormvariate(mu, sigma):
    return _math.exp(gauss(mu, sigma))


def expovariate(lambd=1.0):
    u = _r.random()
    while u <= 1e-300:
        u = _r.random()
    return -_math.log(u) / lambd


def triangular(low=0.0, high=1.0, mode=None):
    u = _r.random()
    c = 0.5 if mode is None else (mode - low) / (high - low)
    if u > c:
        u = 1.0 - u
        c = 1.0 - c
        low, high = high, low
    return low + (high - low) * _math.sqrt(u * c)


def getrandbits(k):
    r = 0
    n = 0
    while n < k:
        r = r * 2 + int(_r.random() * 2)
        n += 1
    return r


def choices(population, weights=None, cum_weights=None, k=1):
    n = len(population)
    if cum_weights is None and weights is not None:
        cum_weights = []
        t = 0.0
        for w in weights:
            t += w
            cum_weights.append(t)
    out = []
    for _ in range(k):
        if cum_weights is None:
            out.append(population[int(_r.random() * n)])
        else:
            u = _r.random() * cum_weights[-1]
            i = 0
            while i < n - 1 and cum_weights[i] <= u:
                i += 1
            out.append(population[i])
    return out
