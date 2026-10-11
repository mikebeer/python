# numpy.random in pure Python (values do not match numpy), shared by both interpreters
import math
import time
import numpy as np

_range = range
_M = 2147483647


class _Core:
    """Park-Miller minimal standard generator (31 bit state, products stay below 2**47)"""

    def __init__(self, seed=None):
        self.seed(seed)

    def seed(self, seed=None):
        if seed is None:
            seed = int(time.time() * 1000.0)
        if not isinstance(seed, int):
            seed = int(sum(np.asarray(seed).flatten().tolist()))
        s = (seed * 2654435761 + 12345) % _M
        if s == 0:
            s = 1
        self.state = s
        for _ in _range(5):
            self._next()

    def _next(self):
        self.state = (self.state * 48271) % _M
        return self.state

    def random(self):
        a = self._next()
        b = self._next()
        return ((a - 1) / (_M - 1) + (b - 1) / (_M - 1) / _M) % 1.0

    def normal(self):
        u1 = self.random()
        while u1 <= 1e-300:
            u1 = self.random()
        u2 = self.random()
        return math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * math.pi * u2)

    def below(self, n):
        return int(self.random() * n)


def _size(size):
    if size is None:
        return None
    if isinstance(size, int):
        return (size,)
    return tuple(size)


def _count(shape):
    n = 1
    for d in shape:
        n *= d
    return n


def _build(values, shape):
    if shape is None:
        return values[0]
    return np.array(values).reshape(list(shape))


class RandomState:
    def __init__(self, seed=None):
        self._c = _Core(seed)

    def seed(self, seed=None):
        self._c.seed(seed)

    def random_sample(self, size=None):
        sh = _size(size)
        n = 1 if sh is None else _count(sh)
        return _build([self._c.random() for _ in _range(n)], sh)

    random = random_sample
    ranf = random_sample
    sample = random_sample

    def rand(self, *dims):
        if len(dims) == 0:
            return self._c.random()
        return self.random_sample(tuple(dims))

    def standard_normal(self, size=None):
        sh = _size(size)
        n = 1 if sh is None else _count(sh)
        return _build([self._c.normal() for _ in _range(n)], sh)

    def randn(self, *dims):
        if len(dims) == 0:
            return self._c.normal()
        return self.standard_normal(tuple(dims))

    def normal(self, loc=0.0, scale=1.0, size=None):
        sh = _size(size)
        n = 1 if sh is None else _count(sh)
        return _build([loc + scale * self._c.normal() for _ in _range(n)], sh)

    def uniform(self, low=0.0, high=1.0, size=None):
        sh = _size(size)
        n = 1 if sh is None else _count(sh)
        return _build([low + (high - low) * self._c.random() for _ in _range(n)], sh)

    def randint(self, low, high=None, size=None, dtype=None):
        if high is None:
            low, high = 0, low
        span = high - low
        sh = _size(size)
        n = 1 if sh is None else _count(sh)
        return _build([low + self._c.below(span) for _ in _range(n)], sh)

    def integers(self, low, high=None, size=None, dtype=None, endpoint=False):
        if high is None:
            low, high = 0, low
        if endpoint:
            high += 1
        return self.randint(low, high, size)

    def exponential(self, scale=1.0, size=None):
        sh = _size(size)
        n = 1 if sh is None else _count(sh)
        out = []
        for _ in _range(n):
            u = self._c.random()
            while u <= 1e-300:
                u = self._c.random()
            out.append(-scale * math.log(u))
        return _build(out, sh)

    def poisson(self, lam=1.0, size=None):
        sh = _size(size)
        n = 1 if sh is None else _count(sh)
        out = []
        lim = math.exp(-lam)
        for _ in _range(n):
            k = 0
            p = 1.0
            while True:
                p *= self._c.random()
                if p <= lim:
                    break
                k += 1
            out.append(k)
        return _build(out, sh)

    def binomial(self, n, p, size=None):
        sh = _size(size)
        cnt = 1 if sh is None else _count(sh)
        out = []
        for _ in _range(cnt):
            k = 0
            for _ in _range(n):
                if self._c.random() < p:
                    k += 1
            out.append(k)
        return _build(out, sh)

    def choice(self, a, size=None, replace=True, p=None):
        if isinstance(a, int):
            pool = list(_range(a))
        else:
            pool = np.asarray(a).tolist()
        n = len(pool)
        sh = _size(size)
        cnt = 1 if sh is None else _count(sh)
        if p is not None:
            probs = np.asarray(p).flatten().tolist()
            cum = []
            t = 0.0
            for q in probs:
                t += q
                cum.append(t)
        picks = []
        if replace or p is not None and False:
            for _ in _range(cnt):
                if p is None:
                    picks.append(self._c.below(n))
                else:
                    u = self._c.random() * cum[-1]
                    k = 0
                    while k < n - 1 and cum[k] <= u:
                        k += 1
                    picks.append(k)
        else:
            if cnt > n:
                raise ValueError("Cannot take a larger sample than population when 'replace=False'")
            idx = list(_range(n))
            if p is None:
                for i in _range(cnt):
                    j = i + self._c.below(n - i)
                    idx[i], idx[j] = idx[j], idx[i]
                    picks.append(idx[i])
            else:
                weights = probs[:]
                for _ in _range(cnt):
                    tot = sum(weights)
                    u = self._c.random() * tot
                    k = 0
                    acc = weights[0]
                    while k < n - 1 and acc <= u:
                        k += 1
                        acc += weights[k]
                    picks.append(k)
                    weights[k] = 0.0
        vals = [pool[i] for i in picks]
        return _build(vals, sh)

    def shuffle(self, x):
        if isinstance(x, list):
            for i in _range(len(x) - 1, 0, -1):
                j = self._c.below(i + 1)
                x[i], x[j] = x[j], x[i]
            return None
        n = x.shape[0]
        for i in _range(n - 1, 0, -1):
            j = self._c.below(i + 1)
            if x.ndim == 1:
                t = x[i]
                x[i] = x[j]
                x[j] = t
            else:
                t = x[i].copy()
                x[i] = x[j]
                x[j] = t
        return None

    def permutation(self, x):
        if isinstance(x, int):
            arr = np.arange(x)
        else:
            arr = np.array(x)
        self.shuffle(arr)
        return arr


class Generator(RandomState):
    def standard_normal(self, size=None):
        return RandomState.standard_normal(self, size)

    def random(self, size=None):
        return RandomState.random_sample(self, size)


_global = RandomState()


def seed(s=None):
    _global.seed(s)


def default_rng(seed=None):
    return Generator(seed)


def rand(*dims):
    return _global.rand(*dims)


def randn(*dims):
    return _global.randn(*dims)


def random_sample(size=None):
    return _global.random_sample(size)


random = random_sample
ranf = random_sample
sample = random_sample


def standard_normal(size=None):
    return _global.standard_normal(size)


def normal(loc=0.0, scale=1.0, size=None):
    return _global.normal(loc, scale, size)


def uniform(low=0.0, high=1.0, size=None):
    return _global.uniform(low, high, size)


def randint(low, high=None, size=None, dtype=None):
    return _global.randint(low, high, size)


def exponential(scale=1.0, size=None):
    return _global.exponential(scale, size)


def poisson(lam=1.0, size=None):
    return _global.poisson(lam, size)


def binomial(n, p, size=None):
    return _global.binomial(n, p, size)


def choice(a, size=None, replace=True, p=None):
    return _global.choice(a, size, replace, p)


def shuffle(x):
    return _global.shuffle(x)


def permutation(x):
    return _global.permutation(x)
