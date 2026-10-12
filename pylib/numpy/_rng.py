# Bit generators and distribution algorithms of numpy.random in pure Python (no numpy needed here):
# MT19937 + the legacy RandomState distributions, PCG64 + SeedSequence + the Generator distributions.
# Every algorithm follows numpy's C code (distributions.c, legacy-distributions.c) so that results are bit-exact.
import math

_M32 = 0xFFFFFFFF
_M64 = 0xFFFFFFFFFFFFFFFF
_log = math.log
_exp = math.exp
_sqrt = math.sqrt
_floor = math.floor
_fabs = math.fabs
_pow = math.pow


class MT19937:
    def __init__(self, seed=None):
        self.mt = [0] * 624
        self.pos = 624
        self.has_gauss = 0
        self.gauss = 0.0
        self.seed(seed)

    def seed(self, seed=None):
        self.has_gauss = 0
        self.gauss = 0.0
        if seed is None:
            import time
            seed = int(time.time() * 1000000.0) & _M32
        if isinstance(seed, int):
            if seed < 0 or seed > _M32:
                raise ValueError("Seed must be between 0 and 2**32 - 1")
            self._init_genrand(seed)
        else:
            key = [int(v) & _M32 for v in seed]
            if len(key) == 0:
                self._init_genrand(0)
            else:
                self._init_by_array(key)

    def _init_genrand(self, s):
        mt = self.mt
        s &= _M32
        for pos in range(624):
            mt[pos] = s
            s = (1812433253 * (s ^ (s >> 30)) + pos + 1) & _M32
        self.pos = 624

    def _init_by_array(self, key):
        self._init_genrand(19650218)
        mt = self.mt
        n = len(key)
        i = 1
        j = 0
        k = 624 if 624 > n else n
        while k:
            prev = mt[i - 1]
            mt[i] = ((mt[i] ^ ((prev ^ (prev >> 30)) * 1664525)) + key[j] + j) & _M32
            i += 1
            j += 1
            if i >= 624:
                mt[0] = mt[623]
                i = 1
            if j >= n:
                j = 0
            k -= 1
        k = 623
        while k:
            prev = mt[i - 1]
            mt[i] = ((mt[i] ^ ((prev ^ (prev >> 30)) * 1566083941)) - i) & _M32
            i += 1
            if i >= 624:
                mt[0] = mt[623]
                i = 1
            k -= 1
        mt[0] = 0x80000000
        self.pos = 624

    def _gen(self):
        mt = self.mt
        for kk in range(624):
            y = (mt[kk] & 0x80000000) | (mt[(kk + 1) % 624] & 0x7FFFFFFF)
            v = mt[(kk + 397) % 624] ^ (y >> 1)
            if y & 1:
                v ^= 0x9908B0DF
            mt[kk] = v
        self.pos = 0

    def next_uint32(self):
        if self.pos >= 624:
            self._gen()
        y = self.mt[self.pos]
        self.pos += 1
        y ^= y >> 11
        y ^= (y << 7) & 0x9D2C5680
        y ^= (y << 15) & 0xEFC60000
        y ^= y >> 18
        return y

    def next_uint64(self):
        hi = self.next_uint32()
        return (hi << 32) | self.next_uint32()

    def next_double(self):
        a = self.next_uint32() >> 5
        b = self.next_uint32() >> 6
        return (a * 67108864.0 + b) / 9007199254740992.0

    def get_state(self):
        return {'bit_generator': 'MT19937', 'state': {'key': list(self.mt), 'pos': self.pos},
                'has_gauss': self.has_gauss, 'gauss': self.gauss}

    def set_state(self, st):
        self.mt = list(st['state']['key'])
        self.pos = st['state']['pos']
        self.has_gauss = st.get('has_gauss', 0)
        self.gauss = st.get('gauss', 0.0)


# ---------------------------------------------------------------------------------------------------------
# bounded integers (masked rejection, as used by RandomState / legacy)

def gen_mask(m):
    m |= m >> 1
    m |= m >> 2
    m |= m >> 4
    m |= m >> 8
    m |= m >> 16
    m |= m >> 32
    return m


def random_interval(bg, mx):
    if mx == 0:
        return 0
    mask = gen_mask(mx)
    if mx <= 0xFFFFFFFF:
        while True:
            v = bg.next_uint32() & mask
            if v <= mx:
                return v
    while True:
        v = bg.next_uint64() & mask
        if v <= mx:
            return v


def bounded_masked_fill(bg, off, rng, cnt, bits=64):
    """off + uniform integer in [0, rng] with numpy's legacy masked rejection; bits = width of the dtype."""
    out = []
    if rng == 0:
        return [off] * cnt
    if bits == 64:
        if rng <= 0xFFFFFFFF:
            if rng == 0xFFFFFFFF:
                for _ in range(cnt):
                    out.append(off + bg.next_uint32())
            else:
                mask = gen_mask(rng)
                for _ in range(cnt):
                    while True:
                        v = bg.next_uint32() & mask
                        if v <= rng:
                            break
                    out.append(off + v)
        else:
            mask = gen_mask(rng)
            for _ in range(cnt):
                while True:
                    v = bg.next_uint64() & mask
                    if v <= rng:
                        break
                out.append(off + v)
        return out
    if bits == 32:
        if rng == 0xFFFFFFFF:
            for _ in range(cnt):
                out.append(off + bg.next_uint32())
        else:
            mask = gen_mask(rng)
            for _ in range(cnt):
                while True:
                    v = bg.next_uint32() & mask
                    if v <= rng:
                        break
                out.append(off + v)
        return out
    # 8 and 16 bit: the generator output is consumed in pieces
    mask = gen_mask(rng)
    per = 4 if bits == 8 else 2
    sh = bits
    bcnt = 0
    buf = 0
    full = (1 << bits) - 1
    for _ in range(cnt):
        while True:
            if bcnt == 0:
                buf = bg.next_uint32()
                bcnt = per - 1
            else:
                buf >>= sh
                bcnt -= 1
            v = buf & mask
            if v <= rng:
                break
        out.append(off + v)
    return out


# ---------------------------------------------------------------------------------------------------------
# legacy distributions (RandomState)

def legacy_gauss(bg):
    if bg.has_gauss:
        t = bg.gauss
        bg.has_gauss = 0
        bg.gauss = 0.0
        return t
    while True:
        x1 = 2.0 * bg.next_double() - 1.0
        x2 = 2.0 * bg.next_double() - 1.0
        r2 = x1 * x1 + x2 * x2
        if r2 < 1.0 and r2 != 0.0:
            break
    f = _sqrt(-2.0 * _log(r2) / r2)
    bg.gauss = f * x1
    bg.has_gauss = 1
    return f * x2


def legacy_standard_exponential(bg):
    return -_log(1.0 - bg.next_double())


def legacy_standard_gamma(bg, shape):
    if shape == 1.0:
        return legacy_standard_exponential(bg)
    if shape == 0.0:
        return 0.0
    if shape < 1.0:
        while True:
            U = bg.next_double()
            V = legacy_standard_exponential(bg)
            if U <= 1.0 - shape:
                X = _pow(U, 1.0 / shape)
                if X <= V:
                    return X
            else:
                Y = -_log((1.0 - U) / shape)
                X = _pow(1.0 - shape + shape * Y, 1.0 / shape)
                if X <= (V + Y):
                    return X
    b = shape - 1.0 / 3.0
    c = 1.0 / _sqrt(9 * b)
    while True:
        while True:
            X = legacy_gauss(bg)
            V = 1.0 + c * X
            if V > 0.0:
                break
        V = V * V * V
        U = bg.next_double()
        if U < 1.0 - 0.0331 * (X * X) * (X * X):
            return b * V
        if _log(U) < 0.5 * X * X + b * (1.0 - V + _log(V)):
            return b * V


_LG_A = [8.333333333333333e-02, -2.777777777777778e-03, 7.936507936507937e-04, -5.952380952380952e-04,
         8.417508417508418e-04, -1.917526917526918e-03, 6.410256410256410e-03, -2.955065359477124e-02,
         1.796443723688307e-01, -1.39243221690590e+00]


def random_loggam(x):
    if x == 1.0 or x == 2.0:
        return 0.0
    n = int(7 - x) if x < 7.0 else 0
    x0 = x + n
    x2 = (1.0 / x0) * (1.0 / x0)
    lg2pi = 1.8378770664093453e+00
    gl0 = _LG_A[9]
    for k in range(8, -1, -1):
        gl0 *= x2
        gl0 += _LG_A[k]
    gl = gl0 / x0 + 0.5 * lg2pi + (x0 - 0.5) * _log(x0) - x0
    if x < 7.0:
        for k in range(1, n + 1):
            gl -= _log(x0 - 1.0)
            x0 -= 1.0
    return gl


def random_poisson_mult(bg, lam):
    enlam = _exp(-lam)
    X = 0
    prod = 1.0
    while True:
        U = bg.next_double()
        prod *= U
        if prod > enlam:
            X += 1
        else:
            return X


def random_poisson_ptrs(bg, lam):
    slam = _sqrt(lam)
    loglam = _log(lam)
    b = 0.931 + 2.53 * slam
    a = -0.059 + 0.02483 * b
    invalpha = 1.1239 + 1.1328 / (b - 3.4)
    vr = 0.9277 - 3.6224 / (b - 2)
    while True:
        U = bg.next_double() - 0.5
        V = bg.next_double()
        us = 0.5 - _fabs(U)
        k = int(_floor((2 * a / us + b) * U + lam + 0.43))
        if us >= 0.07 and V <= vr:
            return k
        if k < 0 or (us < 0.013 and V > us):
            continue
        if (_log(V) + _log(invalpha) - _log(a / (us * us) + b)) <= (-lam + k * loglam - random_loggam(k + 1)):
            return k


def random_poisson(bg, lam):
    if lam >= 10:
        return random_poisson_ptrs(bg, lam)
    if lam == 0:
        return 0
    return random_poisson_mult(bg, lam)


def random_binomial_inversion(bg, n, p):
    q = 1.0 - p
    qn = _exp(n * _log(q))
    px = qn
    X = 0
    U = bg.next_double()
    while U > px:
        X += 1
        if X > n:
            X = 0
            px = qn
            U = bg.next_double()
        else:
            U -= px
            px = ((n - X + 1) * p * px) / (X * q)
    return X


def random_binomial_btpe(bg, n, p):
    r = min(p, 1.0 - p)
    q = 1.0 - r
    fm = n * r + r
    m = int(_floor(fm))
    p1 = int(2.195 * _sqrt(n * r * q) - 4.6 * q) + 0.5
    xm = m + 0.5
    xl = xm - p1
    xr = xm + p1
    c = 0.134 + 20.5 / (15.3 + m)
    a = (fm - xl) / (fm - xl * r)
    laml = a * (1.0 + a / 2.0)
    a = (xr - fm) / (xr * q)
    lamr = a * (1.0 + a / 2.0)
    p2 = p1 * (1.0 + 2.0 * c)
    p3 = p2 + c / laml
    p4 = p3 + c / lamr
    while True:
        u = bg.next_double() * p4
        v = bg.next_double()
        if u <= p1:
            y = int(_floor(xm - p1 * v + u))
            break
        if u <= p2:
            x = xl + (u - p1) / c
            v = v * c + 1.0 - _fabs(m - x + 0.5) / p1
            if v > 1.0:
                continue
            y = int(_floor(x))
        elif u <= p3:
            y = int(_floor(xl + _log(v) / laml))
            if y < 0:
                continue
            v = v * (u - p2) * laml
        else:
            y = int(_floor(xr - _log(v) / lamr))
            if y > n:
                continue
            v = v * (u - p3) * lamr
        k = abs(y - m)
        if k > 20 and k < (n * r * q) / 2.0 - 1:
            # squeeze using upper and lower bounds on log(f(x))
            amaxp = (k / (n * r * q)) * ((k * (k / 3.0 + 0.625) + 0.16666666666666666) / (n * r * q) + 0.5)
            ynorm = -k * k / (2.0 * n * r * q)
            alv = _log(v)
            if alv < ynorm - amaxp:
                break
            if alv <= ynorm + amaxp:
                x1 = y + 1
                f1 = m + 1
                z = n + 1 - m
                w = n - y + 1
                x2 = x1 * x1
                f2 = f1 * f1
                z2 = z * z
                w2 = w * w
                if alv <= (xm * _log(f1 / x1) + (n - m + 0.5) * _log(z / w) + (y - m) * _log(w * r / (x1 * q))
                           + (13680. - (462. - (132. - (99. - 140. / f2) / f2) / f2) / f2) / f1 / 166320.
                           + (13680. - (462. - (132. - (99. - 140. / z2) / z2) / z2) / z2) / z / 166320.
                           + (13680. - (462. - (132. - (99. - 140. / x2) / x2) / x2) / x2) / x1 / 166320.
                           + (13680. - (462. - (132. - (99. - 140. / w2) / w2) / w2) / w2) / w / 166320.):
                    break
        else:
            s = r / q
            a = s * (n + 1)
            F = 1.0
            if m < y:
                for i in range(m + 1, y + 1):
                    F *= (a / i - s)
            elif m > y:
                for i in range(y + 1, m + 1):
                    F /= (a / i - s)
            if v <= F:
                break
    if p > 0.5:
        y = n - y
    return y


def random_binomial(bg, p, n):
    if n == 0 or p == 0.0:
        return 0
    if p <= 0.5:
        if p * n <= 30.0:
            return random_binomial_inversion(bg, n, p)
        return random_binomial_btpe(bg, n, p)
    q = 1.0 - p
    if q * n <= 30.0:
        return n - random_binomial_inversion(bg, n, q)
    return n - random_binomial_btpe(bg, n, q)


def legacy_normal(bg, loc, scale):
    return loc + scale * legacy_gauss(bg)


def legacy_chisquare(bg, df):
    return 2.0 * legacy_standard_gamma(bg, df / 2.0)


def legacy_f(bg, dfnum, dfden):
    return (legacy_chisquare(bg, dfnum) * dfden) / (legacy_chisquare(bg, dfden) * dfnum)


def legacy_standard_t(bg, df):
    num = legacy_gauss(bg)
    denom = legacy_standard_gamma(bg, df / 2)
    return _sqrt(df / 2) * num / _sqrt(denom)


def legacy_pareto(bg, a):
    return _exp(legacy_standard_exponential(bg) / a) - 1


def legacy_weibull(bg, a):
    if a == 0.0:
        return 0.0
    return _pow(legacy_standard_exponential(bg), 1.0 / a)


def legacy_power(bg, a):
    return _pow(1 - _exp(-legacy_standard_exponential(bg)), 1.0 / a)


def random_laplace(bg, loc, scale):
    while True:
        U = bg.next_double()
        if U >= 0.5:
            return loc - scale * _log(2.0 - U - U)
        if U > 0.0:
            return loc + scale * _log(U + U)


def random_gumbel(bg, loc, scale):
    while True:
        U = 1.0 - bg.next_double()
        if U < 1.0:
            return loc - scale * _log(-_log(U))


def random_logistic(bg, loc, scale):
    while True:
        U = bg.next_double()
        if U > 0.0:
            return loc + scale * _log(U / (1.0 - U))


def random_rayleigh(bg, mode):
    return mode * _sqrt(-2.0 * math.log1p(-bg.next_double()))


def legacy_standard_cauchy(bg):
    return legacy_gauss(bg) / legacy_gauss(bg)


def random_triangular(bg, left, mode, right):
    base = right - left
    leftbase = mode - left
    ratio = leftbase / base
    leftprod = leftbase * base
    rightprod = (right - mode) * base
    U = bg.next_double()
    if U <= ratio:
        return left + _sqrt(U * leftprod)
    return right - _sqrt((1.0 - U) * rightprod)


def legacy_negative_binomial(bg, n, p):
    Y = (1 - p) / p * legacy_standard_gamma(bg, n)
    return random_poisson(bg, Y)


def legacy_wald(bg, mean, scale):
    mu_2l = mean / (2 * scale)
    Y = legacy_gauss(bg)
    Y = mean * Y * Y
    X = mean + mu_2l * (Y - _sqrt(4 * scale * Y + Y * Y))
    U = bg.next_double()
    if U <= mean / (mean + X):
        return X
    return mean * mean / X


def random_geometric_search(bg, p):
    X = 1
    s = prod = p
    q = 1.0 - p
    U = bg.next_double()
    while U > s:
        prod *= q
        s += prod
        X += 1
    return X


def random_geometric(bg, p):
    if p >= 0.333333333333333333333333:
        return random_geometric_search(bg, p)
    return int(math.ceil(-legacy_standard_exponential(bg) / math.log1p(-p)))


def random_multinomial(bg, n, pvals):
    d = len(pvals)
    out = [0] * d
    remaining_p = 1.0
    dn = n
    for j in range(d - 1):
        out[j] = random_binomial(bg, pvals[j] / remaining_p, dn)
        dn -= out[j]
        if dn <= 0:
            break
        remaining_p -= pvals[j]
    if dn > 0:
        out[d - 1] = dn
    return out


def random_beta_legacy(bg, a, b):
    if a <= 1.0 and b <= 1.0:
        while True:
            U = bg.next_double()
            V = bg.next_double()
            X = _pow(U, 1.0 / a)
            Y = _pow(V, 1.0 / b)
            XpY = X + Y
            if XpY <= 1.0 and U + V > 0.0:
                if XpY > 0:
                    return X / XpY
                logX = _log(U) / a
                logY = _log(V) / b
                logM = logX if logX > logY else logY
                logX -= logM
                logY -= logM
                return _exp(logX - _log(_exp(logX) + _exp(logY)))
    Ga = legacy_standard_gamma(bg, a)
    Gb = legacy_standard_gamma(bg, b)
    return Ga / (Ga + Gb)


# ---------------------------------------------------------------------------------------------------------
# SeedSequence and PCG64

def _words32(n):
    out = []
    if n == 0:
        return [0]
    while n:
        out.append(n & _M32)
        n >>= 32
    return out


class SeedSequence:
    def __init__(self, entropy=None, spawn_key=(), pool_size=4, n_children_spawned=0):
        if entropy is None:
            import time
            entropy = int(time.time() * 1000000.0) * 2654435761 + 12345
        self.entropy = entropy
        self.spawn_key = tuple(spawn_key)
        self.pool_size = pool_size
        self.n_children_spawned = n_children_spawned
        self.pool = [0] * pool_size
        self._mix_entropy()

    def _mix_entropy(self):
        e = self.entropy
        if isinstance(e, int):
            run = _words32(e)
        else:
            run = []
            for v in e:
                run.extend(_words32(int(v)))
        if len(self.spawn_key) > 0 and len(run) < self.pool_size:
            run = run + [0] * (self.pool_size - len(run))
        spawn = []
        for v in self.spawn_key:
            spawn.extend(_words32(int(v)))
        ent = run + spawn
        pool = self.pool
        hc = [0x43b0d7e5]

        def hashmix(value):
            value ^= hc[0]
            hc[0] = (hc[0] * 0x931e8875) & _M32
            value = (value * hc[0]) & _M32
            value ^= value >> 16
            return value

        def mix(x, y):
            r = (0xca01f9dd * x - 0x4973f715 * y) & _M32
            r ^= r >> 16
            return r

        n = len(pool)
        for i in range(n):
            pool[i] = hashmix(ent[i] if i < len(ent) else 0)
        for i_src in range(n):
            for i_dst in range(n):
                if i_src != i_dst:
                    pool[i_dst] = mix(pool[i_dst], hashmix(pool[i_src]))
        for i_src in range(n, len(ent)):
            for i_dst in range(n):
                pool[i_dst] = mix(pool[i_dst], hashmix(ent[i_src]))

    def generate_state(self, n_words, dtype=64):
        if dtype == 64:
            n32 = n_words * 2
        else:
            n32 = n_words
        hc = 0x8b51f9dd
        state = []
        pool = self.pool
        n = len(pool)
        for i in range(n32):
            v = pool[i % n]
            v ^= hc
            hc = (hc * 0x58f38ded) & _M32
            v = (v * hc) & _M32
            v ^= v >> 16
            state.append(v)
        if dtype == 64:
            return [state[2 * i] | (state[2 * i + 1] << 32) for i in range(n_words)]
        return state

    def spawn(self, n_children):
        out = []
        for i in range(self.n_children_spawned, self.n_children_spawned + n_children):
            out.append(SeedSequence(self.entropy, self.spawn_key + (i,), self.pool_size))
        self.n_children_spawned += n_children
        return out


_PCG_MULT = 47026247687942121848144207491837523525
_M128 = (1 << 128) - 1


class PCG64:
    def __init__(self, seed=None):
        if isinstance(seed, SeedSequence):
            ss = seed
        else:
            ss = SeedSequence(seed)
        self.seed_seq = ss
        val = ss.generate_state(4, 64)
        self._seed_pcg((val[0] << 64) | val[1], (val[2] << 64) | val[3])
        self.has_uint32 = 0
        self.uinteger = 0

    def _seed_pcg(self, initstate, initseq):
        self.state = 0
        self.inc = ((initseq << 1) | 1) & _M128
        self.state = (self.state * _PCG_MULT + self.inc) & _M128
        self.state = (self.state + initstate) & _M128
        self.state = (self.state * _PCG_MULT + self.inc) & _M128

    def next_uint64(self):
        st = (self.state * _PCG_MULT + self.inc) & _M128
        self.state = st
        x = (st >> 64) ^ (st & _M64)
        rot = st >> 122
        return ((x >> rot) | (x << ((64 - rot) & 63))) & _M64

    def next_uint32(self):
        if self.has_uint32:
            self.has_uint32 = 0
            return self.uinteger
        nxt = self.next_uint64()
        self.has_uint32 = 1
        self.uinteger = nxt >> 32
        return nxt & _M32

    def next_double(self):
        return (self.next_uint64() >> 11) * (1.0 / 9007199254740992.0)

    def advance(self, delta):
        # jump ahead by delta steps
        cur_mult = _PCG_MULT
        cur_plus = self.inc
        acc_mult = 1
        acc_plus = 0
        delta &= _M128
        while delta > 0:
            if delta & 1:
                acc_mult = (acc_mult * cur_mult) & _M128
                acc_plus = (acc_plus * cur_mult + cur_plus) & _M128
            cur_plus = ((cur_mult + 1) * cur_plus) & _M128
            cur_mult = (cur_mult * cur_mult) & _M128
            delta >>= 1
        self.state = (acc_mult * self.state + acc_plus) & _M128
        return self


# ---------------------------------------------------------------------------------------------------------
# ziggurat tables

# derived from numpy's ziggurat tables (wi_double = x_i / 2**52, we_double = x_i / 2**53)
_WI = [
    8.683627060801306e-16, 4.779330175727737e-17, 6.354352417405262e-17, 7.454870481247696e-17,
    8.3293668157931e-17, 9.068060405059482e-17, 9.714860076567762e-17, 1.0294750314241019e-16,
    1.0823430288447684e-16, 1.131147019610903e-16, 1.176635945702292e-16, 1.2193617278714363e-16,
    1.2597439914637093e-16, 1.2981099886264032e-16, 1.3347203736824123e-16, 1.3697864842571203e-16,
    1.4034823001242382e-16, 1.4359529452056943e-16, 1.4673208742364422e-16, 1.4976904668391037e-16,
    1.5271515003596198e-16, 1.5557818169460764e-16, 1.5836494009290885e-16, 1.6108140175274928e-16,
    1.6373285203969853e-16, 1.6632399058420835e-16, 1.6885901708676596e-16, 1.713417017655966e-16,
    1.737754436586486e-16, 1.7616331923000996e-16, 1.7850812316976727e-16, 1.8081240285799152e-16,
    1.830784876482675e-16, 1.853085138861802e-16, 1.8750444639373882e-16, 1.896680970077476e-16,
    1.918011406483862e-16, 1.9390512930625104e-16, 1.9598150426628824e-16, 1.9803160683128174e-16,
    2.000566877627333e-16, 2.0205791562071654e-16, 2.0403638415480212e-16, 2.0599311887403706e-16,
    2.079290829041402e-16, 2.0984518222370352e-16, 2.1174227035760342e-16, 2.1362115259449868e-16,
    2.1548258978581458e-16, 2.1732730177564367e-16, 2.191559705042727e-16, 2.2096924282235318e-16,
    2.2276773304789553e-16, 2.2455202529414355e-16, 2.263226755928568e-16, 2.280802138345017e-16,
    2.2982514554424684e-16, 2.3155795351040804e-16, 2.3327909928004356e-16, 2.3498902453470955e-16,
    2.3668815235791604e-16, 2.3837688840454243e-16, 2.4005562198135063e-16, 2.4172472704675025e-16,
    2.433845631371103e-16, 2.4503547622614954e-16, 2.466777995232705e-16, 2.4831185421610877e-16,
    2.4993795016204524e-16, 2.515563865329658e-16, 2.5316745241713583e-16, 2.547714273816944e-16,
    2.563685819989397e-16, 2.579591783392867e-16, 2.5954347043351707e-16, 2.6112170470670194e-16,
    2.6269412038597256e-16, 2.6426094988411895e-16, 2.658224191608307e-16, 2.6737874806323633e-16,
    2.689301506472616e-16, 2.704768354811995e-16, 2.720190059327732e-16, 2.735568604408679e-16,
    2.7509059277301666e-16, 2.7662039226963903e-16, 2.781464440759544e-16, 2.79668929362423e-16,
    2.8118802553450207e-16, 2.827039064324479e-16, 2.842167425218406e-16, 2.8572670107546015e-16,
    2.87233946347098e-16, 2.887386397378482e-16, 2.9024093995538423e-16, 2.9174100316669455e-16,
    2.9323898314471816e-16, 2.947350314092935e-16, 2.9622929736280665e-16, 2.977219284209029e-16,
    2.992130701386013e-16, 3.007028663321331e-16, 3.0219145919680615e-16, 3.036789894211802e-16,
    3.051655962978219e-16, 3.0665141783089545e-16, 3.081365908408297e-16, 3.0962125106629225e-16,
    3.111055332636893e-16, 3.125895713043999e-16, 3.140734982699446e-16, 3.1555744654528006e-16,
    3.1704154791040285e-16, 3.1852593363044065e-16, 3.2001073454440114e-16, 3.214960811527447e-16,
    3.2298210370394156e-16, 3.244689322801698e-16, 3.2595669688230784e-16, 3.2744552751437067e-16,
    3.2893555426753697e-16, 3.3042690740391284e-16, 3.3191971744017523e-16, 3.3341411523123725e-16,
    3.3491023205407785e-16, 3.364081996918765e-16, 3.37908150518595e-16, 3.394102175841489e-16,
    3.409145347003126e-16, 3.424212365275018e-16, 3.4393045866258313e-16, 3.454423377278584e-16,
    3.4695701146137835e-16, 3.4847461880874137e-16, 3.499953000165381e-16, 3.5151919672760744e-16,
    3.53046452078274e-16, 3.5457721079774357e-16, 3.5611161930983884e-16, 3.5764982583726505e-16,
    3.59191980508603e-16, 3.6073823546823514e-16, 3.6228874498941915e-16, 3.6384366559073444e-16,
    3.65403156156137e-16, 3.669673780588701e-16, 3.685364952894914e-16, 3.7011067458828983e-16,
    3.716900855823823e-16, 3.7327490092779435e-16, 3.7486529645684887e-16, 3.7646145133120287e-16,
    3.7806354820089604e-16, 3.7967177336979443e-16, 3.8128631696783774e-16, 3.829073731305243e-16,
    3.8453514018609596e-16, 3.8616982085091493e-16, 3.878116224335587e-16, 3.894607570481926e-16,
    3.9111744183782054e-16, 3.9278189920805415e-16, 3.944543570720877e-16, 3.9613504910761354e-16,
    3.9782421502646826e-16, 3.995221008578565e-16, 4.012289592460629e-16, 4.029450497636328e-16,
    4.04670639241075e-16, 4.0640600211422504e-16, 4.0815142079049387e-16, 4.0990718603532664e-16,
    4.1167359738030257e-16, 4.134509635544236e-16, 4.1523960294026883e-16, 4.170398440568316e-16,
    4.1885202607101123e-16, 4.206764993399015e-16, 4.2251362598620494e-16, 4.243637805093078e-16,
    4.262273504347798e-16, 4.2810473700531167e-16, 4.2999635591638323e-16, 4.3190263810026294e-16,
    4.338240305622791e-16, 4.357609972736849e-16, 4.3771402012585875e-16, 4.3968359995105214e-16,
    4.4167025761542035e-16, 4.4367453519065673e-16, 4.456969972112043e-16, 4.477382320247534e-16,
    4.49798853244555e-16, 4.518795013130059e-16, 4.539808451870034e-16, 4.561035841567422e-16,
    4.582484498109567e-16, 4.604162081631153e-16, 4.626076619547846e-16, 4.648236531543207e-16,
    4.670650656712631e-16, 4.693328283093329e-16, 4.716279179838351e-16, 4.739513632325867e-16,
    4.763042480533137e-16, 4.786877161048723e-16, 4.811029753147417e-16, 4.835513029411525e-16,
    4.860340511450812e-16, 4.885526531353603e-16, 4.91108629959527e-16, 4.937035980240335e-16,
    4.963392774403987e-16, 4.990175013091822e-16, 5.017402260718089e-16, 5.045095430818727e-16,
    5.073276915733542e-16, 5.101970732341562e-16, 5.131202686306784e-16, 5.161000557743228e-16,
    5.191394311757699e-16, 5.222416338000234e-16, 5.254101724177597e-16, 5.286488569504945e-16,
    5.3196183453384e-16, 5.353536311816497e-16, 5.388292001334053e-16, 5.423939782201712e-16,
    5.46053951907478e-16, 5.498157350892814e-16, 5.536866612467876e-16, 5.576748932926576e-16,
    5.617895553555417e-16, 5.660408920082422e-16, 5.704404621291389e-16, 5.750013768919895e-16,
    5.797385945724594e-16, 5.846692893455479e-16, 5.898133176477899e-16, 5.951938149641444e-16,
    6.008379696271908e-16, 6.067780409333449e-16, 6.130527208725282e-16, 6.197089894581626e-16,
    6.268046963301284e-16, 6.344122407127506e-16, 6.426239659548055e-16, 6.515603317344994e-16,
    6.613827885097664e-16, 6.723150462505587e-16, 6.846803417564259e-16, 6.98971833638762e-16,
    7.159994934830664e-16, 7.372424301798799e-16, 7.658936370805573e-16, 8.113849337656484e-16,
]
_WE = [
    9.655740063209183e-16, 7.089014243955414e-18, 1.1639412496691224e-17, 1.524391512353216e-17,
    1.833284885723744e-17, 2.1089651094644866e-17, 2.3611280778431382e-17, 2.595595772310894e-17,
    2.8161735541977523e-17, 3.0255041303213823e-17, 3.225508254836375e-17, 3.417632340185027e-17,
    3.6029969787344525e-17, 3.782490776869649e-17, 3.956832198097553e-17, 4.1266117781759464e-17,
    4.2923218084425256e-17, 4.4543777432823714e-17, 4.613133981483186e-17, 4.768895725264636e-17,
    4.921928043727963e-17, 5.072462904503147e-17, 5.220704702792672e-17, 5.366834661718192e-17,
    5.511014372835095e-17, 5.653388673239667e-17, 5.794088004852767e-17, 5.933230365208943e-17,
    6.07092293284718e-17, 6.207263431163193e-17, 6.342341280303077e-17, 6.476238575956142e-17,
    6.609030925769405e-17, 6.740788167872722e-17, 6.871574991183812e-17, 7.00145147340393e-17,
    7.130473549660643e-17, 7.258693422414648e-17, 7.386159921381792e-17, 7.512918820723728e-17,
    7.639013119550826e-17, 7.764483290797848e-17, 7.88936750272979e-17, 8.013701816675454e-17,
    8.137520364041762e-17, 8.260855505210038e-17, 8.383737972539139e-17, 8.506196999385323e-17,
    8.628260436784113e-17, 8.749954859216183e-17, 8.871305660690252e-17, 8.992337142215357e-17,
    9.113072591597909e-17, 9.233534356381788e-17, 9.353743910649129e-17, 9.47372191631295e-17,
    9.593488279457997e-17, 9.713062202221521e-17, 9.832462230649511e-17, 9.951706298915072e-17,
    1.0070811770242949e-16, 1.0189795474846941e-16, 1.030867374515422e-16, 1.0427462448561886e-16,
    1.0546177017945764e-16, 1.0664832480119147e-16, 1.0783443482419485e-16, 1.0902024317583505e-16,
    1.1020588947055781e-16, 1.1139151022861975e-16, 1.1257723908165675e-16, 1.1376320696616847e-16,
    1.1494954230590093e-16, 1.1613637118402183e-16, 1.1732381750590458e-16, 1.1851200315326694e-16,
    1.1970104813034652e-16, 1.2089107070273855e-16, 1.2208218752947062e-16, 1.2327451378884152e-16,
    1.2446816329851125e-16, 1.2566324863028985e-16, 1.2685988122003975e-16, 1.2805817147307494e-16,
    1.2925822886541196e-16, 1.3046016204120288e-16, 1.3166407890665726e-16, 1.328700867207381e-16,
    1.3407829218289994e-16, 1.3528880151811755e-16, 1.3650172055943978e-16, 1.377171548282881e-16,
    1.389352096127064e-16, 1.4015599004375715e-16, 1.4137960117024852e-16, 1.4260614803196654e-16,
    1.4383573573157902e-16, 1.4506846950536877e-16, 1.4630445479294757e-16, 1.4754379730609516e-16,
    1.487866030968626e-16, 1.500329786250737e-16, 1.5128303082535394e-16, 1.5253686717381255e-16,
    1.537945957544997e-16, 1.5505632532575771e-16, 1.5632216538658375e-16, 1.5759222624311761e-16,
    1.5886661907536842e-16, 1.6014545600429167e-16, 1.6142885015932787e-16, 1.6271691574651305e-16,
    1.640097681172718e-16, 1.653075238380037e-16, 1.666103007605742e-16, 1.6791821809382289e-16,
    1.6923139647620223e-16, 1.7054995804966298e-16, 1.7187402653490317e-16, 1.7320372730810084e-16,
    1.745391874792534e-16, 1.7588053597224914e-16, 1.7722790360680065e-16, 1.7858142318237326e-16,
    1.7994122956424637e-16, 1.8130745977185016e-16, 1.8268025306952523e-16, 1.8405975105985878e-16,
    1.8544609777975695e-16, 1.8683943979941927e-16, 1.882399263243892e-16, 1.8964770930086167e-16,
    1.9106294352443765e-16, 1.9248578675252438e-16, 1.9391639982058994e-16, 1.9535494676249091e-16,
    1.9680159493510374e-16, 1.982565151475019e-16, 1.997198817949342e-16, 2.0119187299787347e-16,
    2.0267267074641983e-16, 2.0416246105035888e-16, 2.0566143409519179e-16, 2.071697844044737e-16,
    2.0868771100881597e-16, 2.1021541762192928e-16, 2.117531128241076e-16, 2.133010102535779e-16,
    2.1485932880616633e-16, 2.1642829284376047e-16, 2.180081324120784e-16, 2.1959908346828707e-16,
    2.212013881190496e-16, 2.2281529486961805e-16, 2.2444105888463086e-16, 2.2607894226131737e-16,
    2.277292143158621e-16, 2.2939215188373114e-16, 2.3106803963482133e-16, 2.3275717040435346e-16,
    2.344598455404958e-16, 2.361763752697774e-16, 2.3790707908142767e-16, 2.3965228613186235e-16,
    2.4141233567062933e-16, 2.431875774892256e-16, 2.44978372394307e-16, 2.4678509270692887e-16,
    2.4860812278958517e-16, 2.504478596029557e-16, 2.523047132944217e-16, 2.541791078205812e-16,
    2.560714816061771e-16, 2.579822882420531e-16, 2.599119972249747e-16, 2.618610947423924e-16,
    2.638300845054943e-16, 2.658194886341845e-16, 2.678298485979525e-16, 2.698617262169489e-16,
    2.7191570472798185e-16, 2.739923899205815e-16, 2.760924113487617e-16, 2.782164236246436e-16,
    2.8036510780069835e-16, 2.825391728480253e-16, 2.847393572388174e-16, 2.8696643064198177e-16,
    2.8922119574179956e-16, 2.915044901905293e-16, 2.9381718870700286e-16, 2.9616020533454657e-16,
    2.9853449587300453e-16, 3.009410605012618e-16, 3.0338094660850034e-16, 3.058552518544861e-16,
    3.08365127481531e-16, 3.1091178190342663e-16, 3.134964845996663e-16, 3.1612057034671057e-16,
    3.187854438219713e-16, 3.2149258462067974e-16, 3.2424355273094516e-16, 3.2703999451822404e-16,
    3.298836492772283e-16, 3.3277635641716714e-16, 3.357200633553244e-16, 3.387168342045505e-16,
    3.417688593525637e-16, 3.448784660453424e-16, 3.4804813010374423e-16, 3.5128048892229794e-16,
    3.545783559224792e-16, 3.5794473666042765e-16, 3.6138284682190606e-16, 3.6489613237645425e-16,
    3.6848829220956213e-16, 3.7216330360802073e-16, 3.7592545104162565e-16, 3.7977935876688744e-16,
    3.8373002787892137e-16, 3.8778287856078953e-16, 3.919437984311429e-16, 3.962191980786775e-16,
    4.0061607510565417e-16, 4.051420882956573e-16, 4.0980564389030625e-16, 4.1461599642909046e-16,
    4.195833672073399e-16, 4.247190841824385e-16, 4.3003574816674707e-16, 4.355474314693952e-16,
    4.41269916903607e-16, 4.472209874259932e-16, 4.534207798565834e-16, 4.598922204905932e-16,
    4.666615664711476e-16, 4.737590853262492e-16, 4.812199172829238e-16, 4.89085182739221e-16,
    4.97403423619194e-16, 5.06232507214416e-16, 5.156421828878083e-16, 5.257175802022275e-16,
    5.365640977112022e-16, 5.483144034258704e-16, 5.61138745467516e-16, 5.752606481503332e-16,
    5.909817641652103e-16, 6.087231416180908e-16, 6.290979034877557e-16, 6.530492053564041e-16,
    6.821393079028929e-16, 7.192444966089362e-16, 7.706095350032097e-16, 8.545517038584027e-16,
]


def _make_tables():
    two52 = 4503599627370496.0
    two53 = 9007199254740992.0
    wi = _WI
    we = _WE
    ki = [0] * 256
    fi = [0.0] * 256
    ke = [0] * 256
    fe = [0.0] * 256
    ki[0] = int((3.6541528853610088 / (wi[0] * two52)) * two52)
    fi[0] = 1.0
    ke[0] = int((7.69711747013104972 / (we[0] * two53)) * two53)
    fe[0] = 1.0
    for i in range(1, 256):
        x = wi[i] * two52
        fi[i] = _exp(-0.5 * x * x)
        if i >= 2:
            ki[i] = int((wi[i - 1] / wi[i]) * two52)
        y = we[i] * two53
        fe[i] = _exp(-y)
        if i >= 2:
            ke[i] = int((we[i - 1] / we[i]) * two53)
    return ki, wi, fi, ke, we, fe


_ZT = None


def _zt():
    global _ZT
    if _ZT is None:
        _ZT = _make_tables()
    return _ZT


def random_standard_normal(bg):
    ki, wi, fi = _zt()[0:3]
    while True:
        r = bg.next_uint64()
        idx = r & 0xff
        r >>= 8
        sign = r & 1
        rabs = (r >> 1) & 0x000fffffffffffff
        x = rabs * wi[idx]
        if sign:
            x = -x
        if rabs < ki[idx]:
            return x
        if idx == 0:
            while True:
                xx = -0.27366123732975828 * math.log1p(-bg.next_double())
                yy = -math.log1p(-bg.next_double())
                if yy + yy > xx * xx:
                    if (rabs >> 8) & 1:
                        return -(3.6541528853610088 + xx)
                    return 3.6541528853610088 + xx
        else:
            if (fi[idx - 1] - fi[idx]) * bg.next_double() + fi[idx] < _exp(-0.5 * x * x):
                return x


def random_standard_exponential(bg):
    t = _zt()
    ke, we, fe = t[3], t[4], t[5]
    while True:
        ri = bg.next_uint64()
        ri >>= 3
        idx = ri & 0xff
        ri >>= 8
        x = ri * we[idx]
        if ri < ke[idx]:
            return x
        if idx == 0:
            return 7.69711747013104972 - math.log1p(-bg.next_double())
        if (fe[idx - 1] - fe[idx]) * bg.next_double() + fe[idx] < _exp(-x):
            return x


def random_standard_gamma(bg, shape):
    if shape == 1.0:
        return random_standard_exponential(bg)
    if shape == 0.0:
        return 0.0
    if shape < 1.0:
        while True:
            U = bg.next_double()
            V = random_standard_exponential(bg)
            if U <= 1.0 - shape:
                X = _pow(U, 1.0 / shape)
                if X <= V:
                    return X
            else:
                Y = -_log((1.0 - U) / shape)
                X = _pow(1.0 - shape + shape * Y, 1.0 / shape)
                if X <= (V + Y):
                    return X
    b = shape - 1.0 / 3.0
    c = 1.0 / _sqrt(9 * b)
    while True:
        while True:
            X = random_standard_normal(bg)
            V = 1.0 + c * X
            if V > 0.0:
                break
        V = V * V * V
        U = bg.next_double()
        if U < 1.0 - 0.0331 * (X * X) * (X * X):
            return b * V
        if _log(U) < 0.5 * X * X + b * (1.0 - V + _log(V)):
            return b * V


def random_beta(bg, a, b):
    if a <= 1.0 and b <= 1.0:
        while True:
            U = bg.next_double()
            V = bg.next_double()
            X = _pow(U, 1.0 / a)
            Y = _pow(V, 1.0 / b)
            XpY = X + Y
            if XpY <= 1.0 and U + V > 0.0:
                if XpY > 0:
                    return X / XpY
                logX = _log(U) / a
                logY = _log(V) / b
                logM = logX if logX > logY else logY
                logX -= logM
                logY -= logM
                return _exp(logX - _log(_exp(logX) + _exp(logY)))
    Ga = random_standard_gamma(bg, a)
    Gb = random_standard_gamma(bg, b)
    return Ga / (Ga + Gb)


def random_chisquare(bg, df):
    return 2.0 * random_standard_gamma(bg, df / 2.0)


def random_f(bg, dfnum, dfden):
    return (random_chisquare(bg, dfnum) * dfden) / (random_chisquare(bg, dfden) * dfnum)


def random_standard_t(bg, df):
    num = random_standard_normal(bg)
    denom = random_standard_gamma(bg, df / 2)
    return _sqrt(df / 2) * num / _sqrt(denom)


def random_pareto(bg, a):
    return math.expm1(random_standard_exponential(bg) / a)


def random_weibull(bg, a):
    if a == 0.0:
        return 0.0
    return _pow(random_standard_exponential(bg), 1.0 / a)


def random_power(bg, a):
    return _pow(-math.expm1(-random_standard_exponential(bg)), 1.0 / a)


def random_rayleigh_gen(bg, mode):
    return mode * _sqrt(2.0 * random_standard_exponential(bg))


def random_standard_cauchy(bg):
    return random_standard_normal(bg) / random_standard_normal(bg)


def random_negative_binomial(bg, n, p):
    Y = random_standard_gamma(bg, n) * ((1 - p) / p)
    return random_poisson(bg, Y)


def random_wald(bg, mean, scale):
    mu_2l = mean / (2 * scale)
    Y = random_standard_normal(bg)
    Y = mean * Y * Y
    X = mean + mu_2l * (Y - _sqrt(4 * scale * Y + Y * Y))
    U = bg.next_double()
    if U <= mean / (mean + X):
        return X
    return mean * mean / X


def random_geometric_gen(bg, p):
    if p >= 0.333333333333333333333333:
        return random_geometric_search(bg, p)
    return int(math.ceil(-random_standard_exponential(bg) / math.log1p(-p)))


# ---- bounded integers: Lemire (Generator.integers / choice / permutation)

def lemire32(bg, rng):
    rng_excl = rng + 1
    m = bg.next_uint32() * rng_excl
    leftover = m & _M32
    if leftover < rng_excl:
        threshold = (_M32 - rng) % rng_excl
        while leftover < threshold:
            m = bg.next_uint32() * rng_excl
            leftover = m & _M32
    return m >> 32


def lemire64(bg, rng):
    rng_excl = rng + 1
    m = bg.next_uint64() * rng_excl
    leftover = m & _M64
    if leftover < rng_excl:
        threshold = (_M64 - rng) % rng_excl
        while leftover < threshold:
            m = bg.next_uint64() * rng_excl
            leftover = m & _M64
    return m >> 64


def bounded_uint64(bg, off, rng):
    if rng == 0:
        return off
    if rng <= 0xFFFFFFFF:
        if rng == 0xFFFFFFFF:
            return off + bg.next_uint32()
        return off + lemire32(bg, rng)
    if rng == _M64:
        return off + bg.next_uint64()
    return off + lemire64(bg, rng)


def lemire_fill(bg, off, rng, cnt):
    return [bounded_uint64(bg, off, rng) for _ in range(cnt)]


def gen_shuffle_raw(bg, x):
    # Generator.shuffle on a python list (in place)
    for i in range(len(x) - 1, 0, -1):
        j = random_interval(bg, i)
        x[i], x[j] = x[j], x[i]


def gen_shuffle_int(bg, n, first, data):
    for i in range(n - 1, first - 1, -1):
        j = bounded_uint64(bg, 0, i)
        data[i], data[j] = data[j], data[i]


def gen_choice_noreplace(bg, pop_size, size, shuffle=True):
    cutoff = 50 if shuffle else 20
    if pop_size > 10000 and size > (pop_size // cutoff):
        idx = list(range(pop_size))
        gen_shuffle_int(bg, pop_size, max(pop_size - size, 1), idx)
        return idx[pop_size - size:]
    idx = [0] * size
    set_size = int(1.2 * size)
    mask = gen_mask(set_size)
    set_size = 1 + mask
    hs = [-1] * set_size
    for j in range(pop_size - size, pop_size):
        val = bounded_uint64(bg, 0, j)
        loc = val & mask
        while hs[loc] != -1 and hs[loc] != val:
            loc = (loc + 1) & mask
        if hs[loc] == -1:
            hs[loc] = val
            idx[j - pop_size + size] = val
        else:
            loc = j & mask
            while hs[loc] != -1:
                loc = (loc + 1) & mask
            hs[loc] = j
            idx[j - pop_size + size] = j
    if shuffle:
        gen_shuffle_int(bg, size, 1, idx)
    return idx
