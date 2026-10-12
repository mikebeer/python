# numpy.random in pure Python.  RandomState / the module level functions (MT19937, "legacy" algorithms) and
# Generator / default_rng (PCG64, SeedSequence, ziggurat) reproduce numpy's streams bit for bit; the
# algorithms live in _rng.py.  Distribution parameters must be scalars (no broadcasting over arrays).
import math
import bisect
import numpy as np
import numpy._rng as _r

_range = range


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
    if len(shape) == 1:
        return np.array(values)
    return np.array(values).reshape(list(shape))


def _draw(fn, size):
    sh = _size(size)
    n = 1 if sh is None else _count(sh)
    return _build([fn() for _ in _range(n)], sh)


def _dtype_bits(dtype):
    if dtype is None:
        return 64, True
    name = getattr(dtype, '__name__', None) or str(dtype)
    name = name.replace("<class 'numpy.", '').replace("'>", '').replace('numpy.', '')
    if name in ('int', 'int_', 'int64', 'long', 'intp', 'uint64', 'uint', 'uintp', 'ulonglong', 'longlong'):
        return 64, True
    if name in ('int32', 'uint32', 'intc', 'uintc'):
        return 32, True
    if name in ('int16', 'uint16', 'short', 'ushort'):
        return 16, True
    if name in ('int8', 'uint8', 'byte', 'ubyte'):
        return 8, True
    raise TypeError("Unsupported dtype %r for randint" % (dtype,))


def _to_list(a):
    if hasattr(a, 'tolist'):
        return a.tolist()
    return list(a)


def _flat(a):
    r = _to_list(a)
    out = []
    stack = [r]
    if not isinstance(r, list):
        return [r]
    def walk(x):
        for v in x:
            if isinstance(v, list):
                walk(v)
            else:
                out.append(v)
    walk(r)
    return out


def _shape_of(a):
    return tuple(np.asarray(a).shape)


class BitGenerator:
    pass


class MT19937(BitGenerator):
    def __init__(self, seed=None):
        self._bg = _r.MT19937(None)
        if isinstance(seed, SeedSequence):
            self._bg._init_by_array(seed.generate_state(624, 32))
        elif seed is None:
            self._bg._init_by_array(SeedSequence().generate_state(624, 32))
        else:
            self._bg._init_by_array(SeedSequence(seed).generate_state(624, 32))

    def random_raw(self, size=None):
        return _draw(self._bg.next_uint32, size)

    @property
    def state(self):
        return self._bg.get_state()

    @state.setter
    def state(self, value):
        self._bg.set_state(value)


class PCG64(BitGenerator):
    def __init__(self, seed=None):
        self._bg = _r.PCG64(seed)

    @property
    def seed_seq(self):
        return self._bg.seed_seq

    def random_raw(self, size=None):
        return _draw(self._bg.next_uint64, size)

    def advance(self, delta):
        self._bg.advance(delta)
        return self

    @property
    def state(self):
        b = self._bg
        return {'bit_generator': 'PCG64', 'state': {'state': b.state, 'inc': b.inc},
                'has_uint32': b.has_uint32, 'uinteger': b.uinteger}

    @state.setter
    def state(self, value):
        b = self._bg
        b.state = value['state']['state']
        b.inc = value['state']['inc']
        b.has_uint32 = value['has_uint32']
        b.uinteger = value['uinteger']


class SeedSequence:
    def __init__(self, entropy=None, *, spawn_key=(), pool_size=4, n_children_spawned=0):
        self._ss = _r.SeedSequence(entropy, spawn_key, pool_size, n_children_spawned)
        self.entropy = self._ss.entropy
        self.spawn_key = self._ss.spawn_key
        self.pool_size = pool_size

    @property
    def pool(self):
        return np.array(self._ss.pool)

    @property
    def n_children_spawned(self):
        return self._ss.n_children_spawned

    def generate_state(self, n_words, dtype=None):
        bits = 32 if dtype is not None and 'uint32' in str(dtype) else 64
        return np.array(self._ss.generate_state(n_words, bits))

    def spawn(self, n_children):
        out = []
        for c in self._ss.spawn(n_children):
            s = SeedSequence.__new__(SeedSequence)
            s._ss = c
            s.entropy = c.entropy
            s.spawn_key = c.spawn_key
            s.pool_size = c.pool_size
            out.append(s)
        return out


def _ss_state(self, n, bits):
    return self._ss.generate_state(n, bits)


SeedSequence._raw_state = _ss_state


class RandomState:
    def __init__(self, seed=None):
        if isinstance(seed, MT19937):
            self._bg = seed._bg
        else:
            self._bg = _r.MT19937(None)
            self.seed(seed)

    def seed(self, seed=None):
        if seed is not None and not isinstance(seed, int):
            seed = _flat(seed)
            if len(seed) == 1:
                seed = seed[0]
        if seed is None:
            seed = int(__import__('time').time() * 1000.0) & 0xFFFFFFFF
        self._bg.seed(seed)

    def get_state(self, legacy=True):
        b = self._bg
        return ('MT19937', np.array(b.mt), b.pos, b.has_gauss, b.gauss)

    def set_state(self, state):
        b = self._bg
        b.mt = [int(v) for v in _to_list(state[1])]
        b.pos = int(state[2])
        if len(state) > 3:
            b.has_gauss = int(state[3])
            b.gauss = float(state[4])
        else:
            b.has_gauss = 0
            b.gauss = 0.0

    # --- uniform
    def random_sample(self, size=None):
        return _draw(self._bg.next_double, size)

    random = random_sample
    ranf = random_sample
    sample = random_sample

    def rand(self, *dims):
        if len(dims) == 0:
            return self._bg.next_double()
        return self.random_sample(tuple(dims))

    def uniform(self, low=0.0, high=1.0, size=None):
        bg = self._bg
        rng = high - low
        return _draw(lambda: low + rng * bg.next_double(), size)

    # --- integers
    def randint(self, low, high=None, size=None, dtype=None):
        if high is None:
            low, high = 0, low
        bits, _ = _dtype_bits(dtype)
        if low >= high:
            raise ValueError("low >= high")
        sh = _size(size)
        n = 1 if sh is None else _count(sh)
        vals = _r.bounded_masked_fill(self._bg, low, high - low - 1, n, bits)
        return _build(vals, sh)

    def random_integers(self, low, high=None, size=None):
        if high is None:
            high = low
            low = 1
        return self.randint(low, high + 1, size)

    def bytes(self, length):
        out = b''
        bg = self._bg
        n = (length + 3) // 4
        for _ in _range(n):
            out += bg.next_uint32().to_bytes(4, 'little')
        return out[:length]

    # --- normal family
    def standard_normal(self, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_gauss(bg), size)

    def randn(self, *dims):
        if len(dims) == 0:
            return _r.legacy_gauss(self._bg)
        return self.standard_normal(tuple(dims))

    def normal(self, loc=0.0, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_normal(bg, loc, scale), size)

    def lognormal(self, mean=0.0, sigma=1.0, size=None):
        bg = self._bg
        return _draw(lambda: math.exp(_r.legacy_normal(bg, mean, sigma)), size)

    def standard_exponential(self, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_standard_exponential(bg), size)

    def exponential(self, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: scale * _r.legacy_standard_exponential(bg), size)

    def standard_gamma(self, shape, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_standard_gamma(bg, shape), size)

    def gamma(self, shape, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: scale * _r.legacy_standard_gamma(bg, shape), size)

    def beta(self, a, b, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_beta_legacy(bg, a, b), size)

    def chisquare(self, df, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_chisquare(bg, df), size)

    def f(self, dfnum, dfden, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_f(bg, dfnum, dfden), size)

    def standard_t(self, df, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_standard_t(bg, df), size)

    def pareto(self, a, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_pareto(bg, a), size)

    def weibull(self, a, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_weibull(bg, a), size)

    def power(self, a, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_power(bg, a), size)

    def laplace(self, loc=0.0, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_laplace(bg, loc, scale), size)

    def gumbel(self, loc=0.0, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_gumbel(bg, loc, scale), size)

    def logistic(self, loc=0.0, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_logistic(bg, loc, scale), size)

    def rayleigh(self, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_rayleigh(bg, scale), size)

    def standard_cauchy(self, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_standard_cauchy(bg), size)

    def triangular(self, left, mode, right, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_triangular(bg, left, mode, right), size)

    def wald(self, mean, scale, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_wald(bg, mean, scale), size)

    # --- discrete
    def binomial(self, n, p, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_binomial(bg, p, n), size)

    def poisson(self, lam=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_poisson(bg, lam), size)

    def geometric(self, p, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_geometric(bg, p), size)

    def negative_binomial(self, n, p, size=None):
        bg = self._bg
        return _draw(lambda: _r.legacy_negative_binomial(bg, n, p), size)

    def multinomial(self, n, pvals, size=None):
        pv = [float(v) for v in _to_list(pvals)]
        bg = self._bg
        sh = _size(size)
        cnt = 1 if sh is None else _count(sh)
        rows = [_r.random_multinomial(bg, n, pv) for _ in _range(cnt)]
        if sh is None:
            return np.array(rows[0])
        return np.array(rows).reshape(list(sh) + [len(pv)])

    def dirichlet(self, alpha, size=None):
        al = [float(v) for v in _to_list(alpha)]
        bg = self._bg
        sh = _size(size)
        cnt = 1 if sh is None else _count(sh)
        rows = []
        for _ in _range(cnt):
            vals = [_r.legacy_standard_gamma(bg, a) for a in al]
            acc = 0.0
            for v in vals:
                acc += v
            inv = 1.0 / acc
            rows.append([v * inv for v in vals])
        if sh is None:
            return np.array(rows[0])
        return np.array(rows).reshape(list(sh) + [len(al)])

    # --- permutations and choice
    def shuffle(self, x):
        bg = self._bg
        if isinstance(x, list):
            for i in _range(len(x) - 1, 0, -1):
                j = _r.random_interval(bg, i)
                x[i], x[j] = x[j], x[i]
            return None
        n = x.shape[0]
        for i in _range(n - 1, 0, -1):
            j = _r.random_interval(bg, i)
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

    def choice(self, a, size=None, replace=True, p=None):
        if isinstance(a, int):
            pop_size = a
            pool = None
        elif isinstance(a, (list, tuple)) and (len(a) == 0 or not isinstance(a[0], (list, tuple))):
            pool = list(a)
            pop_size = len(pool)
        else:
            arr = np.asarray(a)
            if arr.ndim == 0:
                pop_size = int(arr.tolist())
                pool = None
            else:
                pop_size = arr.shape[0]
                pool = arr
        if pop_size == 0:
            raise ValueError("a must be a positive integer unless no samples are taken")
        sh = _size(size)
        cnt = 1 if sh is None else _count(sh)
        bg = self._bg
        pl = None
        if p is not None:
            pl = [float(v) for v in _to_list(p)]
            if len(pl) != pop_size:
                raise ValueError("'a' and 'p' must have same size")
            s = 0.0
            for v in pl:
                s += v
            if abs(s - 1.0) > 1.4901161193847656e-08:
                raise ValueError("probabilities do not sum to 1")
        if replace:
            if pl is not None:
                cdf = _cumsum_norm(pl)
                idx = [bisect.bisect_right(cdf, bg.next_double()) for _ in _range(cnt)]
            else:
                idx = _r.bounded_masked_fill(bg, 0, pop_size - 1, cnt, 64)
        else:
            if cnt > pop_size:
                raise ValueError("Cannot take a larger sample than population when 'replace=False'")
            if pl is not None:
                if np.count_nonzero(np.array(pl)) < cnt:
                    raise ValueError("Fewer non-zero entries in p than size")
                pp = list(pl)
                found = []
                while len(found) < cnt:
                    x = [bg.next_double() for _ in _range(cnt - len(found))]
                    for k in found:
                        pp[k] = 0.0
                    cdf = _cumsum_norm(pp)
                    new = [bisect.bisect_right(cdf, v) for v in x]
                    seen = {}
                    for pos, v in enumerate(new):
                        if v not in seen:
                            seen[v] = pos
                    firsts = sorted(seen.values())
                    new = [new[i] for i in firsts]
                    found.extend(new)
                idx = found
            else:
                perm = list(_range(pop_size))
                for i in _range(pop_size - 1, 0, -1):
                    j = _r.random_interval(bg, i)
                    perm[i], perm[j] = perm[j], perm[i]
                idx = perm[:cnt]
        return _take(pool, idx, sh, pop_size)


def _cumsum_norm(p):
    cdf = []
    t = 0.0
    for v in p:
        t += v
        cdf.append(t)
    last = cdf[-1]
    return [v / last for v in cdf]


def _build_any(values, sh):
    try:
        return _build(values, sh)
    except Exception:
        return values


def _take(pool, idx, sh, pop_size):
    if pool is None:
        if sh is None:
            return idx[0]
        return _build(idx, sh)
    if isinstance(pool, list):
        vals = [pool[i] for i in idx]
        if sh is None:
            return vals[0]
        return _build_any(vals, sh)
    if pool.ndim == 1:
        lst = pool.tolist()
        vals = [lst[i] for i in idx]
        if sh is None:
            return vals[0]
        return _build(vals, sh)
    rows = [pool[i] for i in idx]
    if sh is None:
        return rows[0]
    return np.array([r.tolist() for r in rows]).reshape(list(sh) + list(pool.shape[1:]))


class Generator:
    def __init__(self, bit_generator=None):
        if bit_generator is None:
            bit_generator = PCG64()
        self.bit_generator = bit_generator
        self._bg = bit_generator._bg

    def __repr__(self):
        return "Generator(%s)" % type(self.bit_generator).__name__

    def spawn(self, n_children):
        return [Generator(PCG64(s)) for s in self.bit_generator.seed_seq.spawn(n_children)]

    def random(self, size=None, dtype=None, out=None):
        return _draw(self._bg.next_double, size)

    def integers(self, low, high=None, size=None, dtype=None, endpoint=False):
        if high is None:
            low, high = 0, low
        if endpoint:
            high += 1
        if low >= high:
            raise ValueError("low >= high" if not endpoint else "low > high")
        sh = _size(size)
        n = 1 if sh is None else _count(sh)
        bg = self._bg
        rng = high - low - 1
        vals = [_r.bounded_uint64(bg, low, rng) for _ in _range(n)]
        return _build(vals, sh)

    def bytes(self, length):
        out = b''
        for _ in _range((length + 3) // 4):
            out += self._bg.next_uint32().to_bytes(4, 'little')
        return out[:length]

    def standard_normal(self, size=None, dtype=None, out=None):
        bg = self._bg
        return _draw(lambda: _r.random_standard_normal(bg), size)

    def normal(self, loc=0.0, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: loc + scale * _r.random_standard_normal(bg), size)

    def uniform(self, low=0.0, high=1.0, size=None):
        bg = self._bg
        rng = high - low
        return _draw(lambda: low + rng * bg.next_double(), size)

    def lognormal(self, mean=0.0, sigma=1.0, size=None):
        bg = self._bg
        return _draw(lambda: math.exp(mean + sigma * _r.random_standard_normal(bg)), size)

    def standard_exponential(self, size=None, dtype=None, method=None, out=None):
        bg = self._bg
        return _draw(lambda: _r.random_standard_exponential(bg), size)

    def exponential(self, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: scale * _r.random_standard_exponential(bg), size)

    def standard_gamma(self, shape, size=None, dtype=None, out=None):
        bg = self._bg
        return _draw(lambda: _r.random_standard_gamma(bg, shape), size)

    def gamma(self, shape, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: scale * _r.random_standard_gamma(bg, shape), size)

    def beta(self, a, b, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_beta(bg, a, b), size)

    def chisquare(self, df, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_chisquare(bg, df), size)

    def f(self, dfnum, dfden, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_f(bg, dfnum, dfden), size)

    def standard_t(self, df, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_standard_t(bg, df), size)

    def pareto(self, a, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_pareto(bg, a), size)

    def weibull(self, a, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_weibull(bg, a), size)

    def power(self, a, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_power(bg, a), size)

    def laplace(self, loc=0.0, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_laplace(bg, loc, scale), size)

    def gumbel(self, loc=0.0, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_gumbel(bg, loc, scale), size)

    def logistic(self, loc=0.0, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_logistic(bg, loc, scale), size)

    def rayleigh(self, scale=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_rayleigh_gen(bg, scale), size)

    def standard_cauchy(self, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_standard_cauchy(bg), size)

    def triangular(self, left, mode, right, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_triangular(bg, left, mode, right), size)

    def wald(self, mean, scale, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_wald(bg, mean, scale), size)

    def binomial(self, n, p, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_binomial(bg, p, n), size)

    def poisson(self, lam=1.0, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_poisson(bg, lam), size)

    def geometric(self, p, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_geometric_gen(bg, p), size)

    def negative_binomial(self, n, p, size=None):
        bg = self._bg
        return _draw(lambda: _r.random_negative_binomial(bg, n, p), size)

    def multinomial(self, n, pvals, size=None):
        pv = [float(v) for v in _to_list(pvals)]
        bg = self._bg
        sh = _size(size)
        cnt = 1 if sh is None else _count(sh)
        rows = [_r.random_multinomial(bg, n, pv) for _ in _range(cnt)]
        if sh is None:
            return np.array(rows[0])
        return np.array(rows).reshape(list(sh) + [len(pv)])

    def dirichlet(self, alpha, size=None):
        al = [float(v) for v in _to_list(alpha)]
        bg = self._bg
        sh = _size(size)
        cnt = 1 if sh is None else _count(sh)
        rows = []
        for _ in _range(cnt):
            vals = [_r.random_standard_gamma(bg, a) for a in al]
            acc = 0.0
            for v in vals:
                acc += v
            inv = 1.0 / acc
            rows.append([v * inv for v in vals])
        if sh is None:
            return np.array(rows[0])
        return np.array(rows).reshape(list(sh) + [len(al)])

    def shuffle(self, x, axis=0):
        bg = self._bg
        if isinstance(x, list):
            for i in _range(len(x) - 1, 0, -1):
                j = _r.random_interval(bg, i)
                x[i], x[j] = x[j], x[i]
            return None
        n = x.shape[0]
        for i in _range(n - 1, 0, -1):
            j = _r.random_interval(bg, i)
            if x.ndim == 1:
                t = x[i]
                x[i] = x[j]
                x[j] = t
            else:
                t = x[i].copy()
                x[i] = x[j]
                x[j] = t
        return None

    def permutation(self, x, axis=0):
        if isinstance(x, int):
            arr = np.arange(x)
        else:
            arr = np.array(x)
        self.shuffle(arr)
        return arr

    def permuted(self, x, axis=None, out=None):
        arr = np.array(x)
        self.shuffle(arr)
        return arr

    def choice(self, a, size=None, replace=True, p=None, axis=0, shuffle=True):
        if isinstance(a, int):
            pop_size = a
            pool = None
        elif isinstance(a, (list, tuple)) and (len(a) == 0 or not isinstance(a[0], (list, tuple))):
            pool = list(a)
            pop_size = len(pool)
        else:
            arr = np.asarray(a)
            if arr.ndim == 0:
                pop_size = int(arr.tolist())
                pool = None
            else:
                pop_size = arr.shape[0]
                pool = arr
        if pop_size == 0:
            raise ValueError("a cannot be empty unless no samples are taken")
        sh = _size(size)
        cnt = 1 if sh is None else _count(sh)
        bg = self._bg
        pl = None
        if p is not None:
            pl = [float(v) for v in _to_list(p)]
            if len(pl) != pop_size:
                raise ValueError("a and p must have same size")
            s = 0.0
            for v in pl:
                s += v
            if abs(s - 1.0) > 1.4901161193847656e-08:
                raise ValueError("probabilities do not sum to 1")
        if replace:
            if pl is not None:
                cdf = _cumsum_norm(pl)
                idx = [bisect.bisect_right(cdf, bg.next_double()) for _ in _range(cnt)]
            else:
                idx = [_r.bounded_uint64(bg, 0, pop_size - 1) for _ in _range(cnt)]
        else:
            if cnt > pop_size:
                raise ValueError("Cannot take a larger sample than population when replace is False")
            if pl is not None:
                pp = list(pl)
                found = []
                while len(found) < cnt:
                    x = [bg.next_double() for _ in _range(cnt - len(found))]
                    for k in found:
                        pp[k] = 0.0
                    cdf = _cumsum_norm(pp)
                    new = [bisect.bisect_right(cdf, v) for v in x]
                    seen = {}
                    for pos, v in enumerate(new):
                        if v not in seen:
                            seen[v] = pos
                    firsts = sorted(seen.values())
                    new = [new[i] for i in firsts]
                    found.extend(new)
                idx = found
            else:
                idx = _r.gen_choice_noreplace(bg, pop_size, cnt, shuffle)
        return _take(pool, idx, sh, pop_size)


_global = RandomState()


def seed(s=None):
    _global.seed(s)


def get_state(legacy=True):
    return _global.get_state(legacy)


def set_state(state):
    _global.set_state(state)


def default_rng(seed=None):
    if isinstance(seed, Generator):
        return seed
    if isinstance(seed, BitGenerator):
        return Generator(seed)
    return Generator(PCG64(seed))


def get_bit_generator():
    return _global._bg


def rand(*dims):
    return _global.rand(*dims)


def randn(*dims):
    return _global.randn(*dims)


def random_sample(size=None):
    return _global.random_sample(size)


random = random_sample
ranf = random_sample
sample = random_sample


def randint(low, high=None, size=None, dtype=None):
    return _global.randint(low, high, size, dtype)


def random_integers(low, high=None, size=None):
    return _global.random_integers(low, high, size)


def bytes(length):
    return _global.bytes(length)


def shuffle(x):
    return _global.shuffle(x)


def permutation(x):
    return _global.permutation(x)


def choice(a, size=None, replace=True, p=None):
    return _global.choice(a, size, replace, p)


def standard_normal(size=None):
    return _global.standard_normal(size)


def normal(loc=0.0, scale=1.0, size=None):
    return _global.normal(loc, scale, size)


def lognormal(mean=0.0, sigma=1.0, size=None):
    return _global.lognormal(mean, sigma, size)


def uniform(low=0.0, high=1.0, size=None):
    return _global.uniform(low, high, size)


def standard_exponential(size=None):
    return _global.standard_exponential(size)


def exponential(scale=1.0, size=None):
    return _global.exponential(scale, size)


def standard_gamma(shape, size=None):
    return _global.standard_gamma(shape, size)


def gamma(shape, scale=1.0, size=None):
    return _global.gamma(shape, scale, size)


def beta(a, b, size=None):
    return _global.beta(a, b, size)


def chisquare(df, size=None):
    return _global.chisquare(df, size)


def f(dfnum, dfden, size=None):
    return _global.f(dfnum, dfden, size)


def standard_t(df, size=None):
    return _global.standard_t(df, size)


def pareto(a, size=None):
    return _global.pareto(a, size)


def weibull(a, size=None):
    return _global.weibull(a, size)


def power(a, size=None):
    return _global.power(a, size)


def laplace(loc=0.0, scale=1.0, size=None):
    return _global.laplace(loc, scale, size)


def gumbel(loc=0.0, scale=1.0, size=None):
    return _global.gumbel(loc, scale, size)


def logistic(loc=0.0, scale=1.0, size=None):
    return _global.logistic(loc, scale, size)


def rayleigh(scale=1.0, size=None):
    return _global.rayleigh(scale, size)


def standard_cauchy(size=None):
    return _global.standard_cauchy(size)


def triangular(left, mode, right, size=None):
    return _global.triangular(left, mode, right, size)


def wald(mean, scale, size=None):
    return _global.wald(mean, scale, size)


def binomial(n, p, size=None):
    return _global.binomial(n, p, size)


def poisson(lam=1.0, size=None):
    return _global.poisson(lam, size)


def geometric(p, size=None):
    return _global.geometric(p, size)


def negative_binomial(n, p, size=None):
    return _global.negative_binomial(n, p, size)


def multinomial(n, pvals, size=None):
    return _global.multinomial(n, pvals, size)


def dirichlet(alpha, size=None):
    return _global.dirichlet(alpha, size)
