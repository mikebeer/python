/* =========================================================================
   p7_rnd.c   numpy.random: xoshiro256** generator, legacy functions,
              default_rng / Generator / RandomState objects
   ========================================================================= */
/* A 64-bit unsigned type without "long long": the native unsigned long where it
   has 64 bits, otherwise (VM/370: long is 32 bits) two 32-bit halves.  The
   generator below only uses the u64_* operations, so both give identical
   streams.  -DPY_U64_EMUL forces the two-halves version for testing. */
#define U32M 0xFFFFFFFFUL
#if ULONG_MAX > 4294967295UL && !defined(PY_U64_EMUL)
typedef unsigned long u64;
static u64 u64_c(unsigned long hi, unsigned long lo) { return ((hi << 16) << 16) | lo; }
static u64 u64_ul(unsigned long v) { return v; }
static u64 u64_l(long v) { return (u64)v; }
static long u64_tol(u64 a) { return (long)a; }
static double u64_dbl(u64 a) { return (double)a; }
static u64 u64_xor(u64 a, u64 b) { return a ^ b; }
static u64 u64_add(u64 a, u64 b) { return a + b; }
static u64 u64_sub(u64 a, u64 b) { return a - b; }
static u64 u64_mul(u64 a, u64 b) { return a * b; }
static u64 u64_shl(u64 a, int k) { return a << k; }
static u64 u64_shr(u64 a, int k) { return a >> k; }
static int u64_ge(u64 a, u64 b) { return a >= b; }
static int u64_le1(u64 a) { return a <= 1; }
static u64 u64_max(void) { return (u64)ULONG_MAX; }
static u64 u64_mod(u64 a, u64 n) { return a % n; }
#else
typedef struct u64 { unsigned long hi, lo; } u64;   /* each holds 32 bits */
static u64 u64_c(unsigned long hi, unsigned long lo)
{
    u64 r;
    r.hi = hi & U32M; r.lo = lo & U32M;
    return r;
}
static u64 u64_ul(unsigned long v) { return u64_c((v >> 16) >> 16, v); }
static u64 u64_l(long v)
{
    u64 r = u64_ul((unsigned long)v);
    if (v < 0 && sizeof(long) <= 4) r.hi = U32M;
    return r;
}
static long u64_tol(u64 a) { return (long)(((a.hi << 16) << 16) | a.lo); }
static double u64_dbl(u64 a) { return (double)a.hi * 4294967296.0 + (double)a.lo; }
static u64 u64_xor(u64 a, u64 b) { return u64_c(a.hi ^ b.hi, a.lo ^ b.lo); }
static u64 u64_add(u64 a, u64 b)
{
    unsigned long lo = (a.lo + b.lo) & U32M;
    return u64_c(a.hi + b.hi + (lo < a.lo ? 1UL : 0UL), lo);
}
static u64 u64_sub(u64 a, u64 b)
{
    unsigned long lo = (a.lo - b.lo) & U32M;
    return u64_c(a.hi - b.hi - (a.lo < b.lo ? 1UL : 0UL), lo);
}
static u64 u64_mul(u64 a, u64 b)
{
    unsigned long a0 = a.lo & 0xFFFFUL, a1 = a.lo >> 16, b0 = b.lo & 0xFFFFUL, b1 = b.lo >> 16;
    unsigned long p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    unsigned long mid = (p00 >> 16) + (p01 & 0xFFFFUL) + (p10 & 0xFFFFUL);
    u64 r;
    r.lo = ((mid & 0xFFFFUL) << 16) | (p00 & 0xFFFFUL);
    r.hi = (p11 + (p01 >> 16) + (p10 >> 16) + (mid >> 16) + a.lo * b.hi + a.hi * b.lo) & U32M;
    return r;
}
static u64 u64_shl(u64 a, int k)
{
    if (k >= 32) return u64_c(a.lo << (k - 32), 0UL);
    if (k == 0) return a;
    return u64_c((a.hi << k) | (a.lo >> (32 - k)), a.lo << k);
}
static u64 u64_shr(u64 a, int k)
{
    if (k >= 32) return u64_c(0UL, a.hi >> (k - 32));
    if (k == 0) return a;
    return u64_c(a.hi >> k, (a.lo >> k) | (a.hi << (32 - k)));
}
static int u64_ge(u64 a, u64 b) { return a.hi > b.hi || (a.hi == b.hi && a.lo >= b.lo); }
static int u64_le1(u64 a) { return a.hi == 0 && a.lo <= 1; }
static u64 u64_max(void) { return u64_c(U32M, U32M); }
static u64 u64_mod(u64 a, u64 n)
{
    u64 r = u64_c(0UL, 0UL);
    int i;
    for (i = 63; i >= 0; i--) {
        r = u64_shl(r, 1);
        r.lo |= (i >= 32 ? a.hi >> (i - 32) : a.lo >> i) & 1UL;
        if (u64_ge(r, n)) r = u64_sub(r, n);
    }
    return r;
}
#endif
static u64 g_rs[4];
static Class *cls_rng[2];

static u64 splitmix(u64 *z)
{
    u64 r;
    *z = u64_add(*z, u64_c(0x9E3779B9UL, 0x7F4A7C15UL));
    r = *z;
    r = u64_mul(u64_xor(r, u64_shr(r, 30)), u64_c(0xBF58476DUL, 0x1CE4E5B9UL));
    r = u64_mul(u64_xor(r, u64_shr(r, 27)), u64_c(0x94D049BBUL, 0x133111EBUL));
    return u64_xor(r, u64_shr(r, 31));
}
static void nrng_seed(u64 *s, u64 seed)
{
    int i;
    for (i = 0; i < 4; i++) s[i] = splitmix(&seed);
}
static u64 rotl64(u64 v, int k)
{
    return u64_xor(u64_shl(v, k), u64_shr(v, 64 - k));
}
static u64 nrng_next(u64 *s)
{
    u64 r = u64_mul(rotl64(u64_mul(s[1], u64_ul(5UL)), 7), u64_ul(9UL));
    u64 t = u64_shl(s[1], 17);
    s[2] = u64_xor(s[2], s[0]); s[3] = u64_xor(s[3], s[1]);
    s[1] = u64_xor(s[1], s[2]); s[0] = u64_xor(s[0], s[3]);
    s[2] = u64_xor(s[2], t);
    s[3] = rotl64(s[3], 45);
    return r;
}
static double nrng_dbl(u64 *s)
{
    return u64_dbl(u64_shr(nrng_next(s), 11)) * (1.0 / 9007199254740992.0);
}
static double nrng_norm(u64 *s)
{
    double u1, u2;
    do u1 = nrng_dbl(s); while (u1 <= 0.0);
    u2 = nrng_dbl(s);
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586476925 * u2);
}
/* uniform integer in [0, n) */
static u64 nrng_below(u64 *s, u64 n)
{
    u64 lim, r;
    if (u64_le1(n)) return u64_ul(0UL);
    lim = u64_sub(u64_max(), u64_mod(u64_max(), n));
    do r = nrng_next(s); while (u64_ge(r, lim));
    return u64_mod(r, n);
}
static u64 seed_value(Value v)
{
    if (v.t == T_INT || v.t == T_BOOL) return u64_l(v.u.i);
    if (v.t == T_BIG) return u64_ul(py_hash(v));
    if (v.t == T_FLOAT)
        throw_new(cls_TypeError,
        "Cannot cast scalar from dtype('float64') to dtype('int64') according to the rule 'safe'");
    if (v.t == T_ND || v.t == T_LIST || v.t == T_TUPLE) {
        Nd *a = nd_from_value(v, ND_INT);
        u64 h = u64_ul(0x1234567UL);
        long i;
        for (i = 0; i < a->size; i++) { u64 t = u64_add(u64_l(a->iv[i]), h); h = splitmix(&t); }
        return h;
    }
    throw_new(cls_TypeError, "Seed must be an integer");
    return u64_ul(0UL);
}
static void seed_from(u64 *s, Value v)
{
    if (v.t == T_UNSET || v.t == T_NONE) {
        u64 t = u64_add(u64_ul(host_seed()), u64_ul((unsigned long)(size_t)s));
        nrng_seed(s, t);
    } else nrng_seed(s, seed_value(v));
}

/* ---- argument helpers: off = 0 for module functions, 1 for methods ---- */
#define RN (x->n - off)
#define RA(i) (x->a[off + (i)])
static Value rarg(Args *x, int off, int pos, const char *nm)
{
    if (pos < RN) return RA(pos);
    return kwget(x, nm);
}
static double rdbl(Args *x, int off, int pos, const char *nm, double dflt)
{
    Value v = rarg(x, off, pos, nm);
    if (!np_has(v)) return dflt;
    return to_dbl(v);
}
/* size argument -> shape; returns -1 for a scalar result */
static int rsize(Value sz, long *shape)
{
    if (!np_has(sz)) return -1;
    return shape_arg(sz, shape);
}
typedef double (*RGen)(u64 *s, double p1, double p2);
static double g_unif(u64 *s, double a, double b) { return a + (b - a) * nrng_dbl(s); }
static double g_norm(u64 *s, double a, double b) { return a + b * nrng_norm(s); }
static double g_exp(u64 *s, double a, double b)
{
    double u;
    (void)b;
    do u = nrng_dbl(s); while (u <= 0.0);
    return -log(u) * a;
}
static Value rfill(u64 *s, Value sz, RGen g, double p1, double p2)
{
    long shape[ND_MAXD], i;
    int nd = rsize(sz, shape);
    Nd *r;
    if (nd < 0) return mkfloat(g(s, p1, p2));
    r = nd_new(ND_FLOAT, nd, shape);
    for (i = 0; i < r->size; i++) r->dv[i] = g(s, p1, p2);
    return nd_ret(r);
}
/* positional dims (rand / randn) */
static Value rdims(Args *x, int off, u64 *s, RGen g)
{
    long shape[ND_MAXD], i;
    int nd = RN, k;
    Nd *r;
    if (nd == 0) return mkfloat(g(s, 0.0, 1.0));
    if (nd > ND_MAXD)
        throw_new(cls_ValueError, "maximum supported dimension for an ndarray is %d", ND_MAXD);
    for (k = 0; k < nd; k++) {
        shape[k] = need_long(RA(k), "dimension");
        if (shape[k] < 0) throw_new(cls_ValueError, "negative dimensions are not allowed");
    }
    r = nd_new(ND_FLOAT, nd, shape);
    for (i = 0; i < r->size; i++) r->dv[i] = g(s, 0.0, 1.0);
    return ndv(r);
}
static Value nr_random(Args *x, int off, u64 *s)
{
    return rfill(s, rarg(x, off, 0, "size"), g_unif, 0.0, 1.0);
}
static Value nr_stdnorm(Args *x, int off, u64 *s)
{
    return rfill(s, rarg(x, off, 0, "size"), g_norm, 0.0, 1.0);
}
static Value nr_normal(Args *x, int off, u64 *s)
{
    double loc = rdbl(x, off, 0, "loc", 0.0), sc = rdbl(x, off, 1, "scale", 1.0);
    if (sc < 0) throw_new(cls_ValueError, "scale < 0");
    return rfill(s, rarg(x, off, 2, "size"), g_norm, loc, sc);
}
static Value nr_uniform(Args *x, int off, u64 *s)
{
    double lo = rdbl(x, off, 0, "low", 0.0), hi = rdbl(x, off, 1, "high", 1.0);
    return rfill(s, rarg(x, off, 2, "size"), g_unif, lo, hi);
}
static Value nr_exponential(Args *x, int off, u64 *s)
{
    double sc = rdbl(x, off, 0, "scale", 1.0);
    if (sc < 0) throw_new(cls_ValueError, "scale < 0");
    return rfill(s, rarg(x, off, 1, "size"), g_exp, sc, 0.0);
}
static Value nr_stdexp(Args *x, int off, u64 *s)
{
    return rfill(s, rarg(x, off, 0, "size"), g_exp, 1.0, 0.0);
}
/* integers: legacy randint has high exclusive; Generator.integers adds endpoint */
static Value nr_ints(Args *x, int off, u64 *s, int gen)
{
    Value lov = rarg(x, off, 0, "low"), hiv = rarg(x, off, 1, "high"),
        szv = rarg(x, off, 2, "size");
    Value epv = gen ? kwget(x, "endpoint") : UNSET_V;
    long lo, hi, shape[ND_MAXD], i;
    int nd;
    Nd *r;
    if (!np_has(lov)) throw_new(cls_TypeError, "integers() missing required argument 'low'");
    if (!np_has(hiv)) { hi = need_long(lov, "low"); lo = 0; }
    else { lo = need_long(lov, "low"); hi = need_long(hiv, "high"); }
    if (np_has(epv) && py_truthy(epv)) hi++;
    if (lo >= hi) throw_new(cls_ValueError, np_has(hiv) ? "low >= high" : "high <= 0");
    nd = rsize(szv, shape);
    if (nd < 0) return mkint(lo + u64_tol(nrng_below(s, u64_sub(u64_l(hi), u64_l(lo)))));
    r = nd_new(ND_INT, nd, shape);
    for (i = 0; i < r->size; i++)
        r->iv[i] = lo + u64_tol(nrng_below(s, u64_sub(u64_l(hi), u64_l(lo))));
    return nd_ret(r);
}
static Value nr_randint(Args *x, int off, u64 *s) { return nr_ints(x, off, s, 0); }
static Value nr_integers(Args *x, int off, u64 *s) { return nr_ints(x, off, s, 1); }

/* in-place Fisher-Yates of the first axis of an array / list */
static void shuffle_nd(Nd *a, u64 *s)
{
    long n, row, i, j;
    Nd *tmp;
    if (a->nd == 0) throw_new(cls_TypeError, "shuffle: 0-d array");
    n = a->shape[0];
    row = n > 0 ? a->size / n : 0;
    tmp = nd_new(a->dt, 1, &row);
    for (i = n - 1; i > 0; i--) {
        j = u64_tol(nrng_below(s, u64_l(i + 1)));
        if (j != i && row > 0) {
            nd_cp(tmp, 0, a, i * row, row);
            nd_cp(a, i * row, a, j * row, row);
            nd_cp(a, j * row, tmp, 0, row);
        }
    }
}
static Value nr_shuffle(Args *x, int off, u64 *s)
{
    Value v;
    if (RN < 1) throw_new(cls_TypeError, "shuffle() missing 1 required positional argument: 'x'");
    v = RA(0);
    if (v.t == T_ND) shuffle_nd(AS_ND(v), s);
    else if (v.t == T_LIST) {
        List *l = AS_LIST(v);
        long i, j;
        for (i = l->n - 1; i > 0; i--) {
            Value t;
            j = u64_tol(nrng_below(s, u64_l(i + 1)));
            t = l->a[i]; l->a[i] = l->a[j]; l->a[j] = t;
        }
    } else throw_new(cls_TypeError, "shuffle: unsupported type '%.40s'", type_name(v));
    return NONE_V;
}
static Value nr_permutation(Args *x, int off, u64 *s)
{
    Value v;
    Nd *r;
    if (RN < 1)
        throw_new(cls_TypeError, "permutation() missing 1 required positional argument: 'x'");
    v = RA(0);
    if (v.t == T_INT) {
        long n = v.u.i, i;
        if (n < 0) throw_new(cls_ValueError, "negative dimensions are not allowed");
        r = nd_new1(ND_INT, n);
        for (i = 0; i < n; i++) r->iv[i] = i;
    } else {
        r = nd_from_value(v, -1);
        if (r->nd == 0) throw_new(cls_ValueError, "x must be an integer or at least 1-dimensional");
    }
    shuffle_nd(r, s);
    return ndv(r);
}
static Value nr_choice(Args *x, int off, u64 *s)
{
    Value av = rarg(x, off, 0, "a"), szv = rarg(x, off, 1, "size"),
        rpv = rarg(x, off, 2, "replace");
    Value pv = rarg(x, off, 3, "p");
    Nd *src, *idx, *res;
    long n, cnt, i, j, shape[ND_MAXD], row;
    int nd, repl = 1, k;
    double *cum = NULL;
    if (!np_has(av)) throw_new(cls_TypeError, "choice() missing required argument 'a'");
    if (np_has(rpv)) repl = py_truthy(rpv);
    if (av.t == T_INT) {
        n = av.u.i;
        if (n <= 0)
            throw_new(cls_ValueError, "a must be a positive integer unless no samples are taken");
        src = NULL;
    } else {
        src = nd_from_value(av, -1);
        if (src->nd < 1) throw_new(cls_ValueError, "a must be 1-dimensional or an integer");
        n = src->shape[0];
        if (n == 0) throw_new(cls_ValueError, "a cannot be empty unless no samples are taken");
    }
    nd = rsize(szv, shape);
    cnt = 1;
    if (nd >= 0) for (k = 0; k < nd; k++) cnt *= shape[k];
    if (np_has(pv)) {
        Nd *p = np_f(pv);
        double tot = 0.0;
        if (p->size != n) throw_new(cls_ValueError, "a and p must have same size");
        cum = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
        for (i = 0; i < n; i++) {
            if (p->dv[i] < 0) { free(cum);
                throw_new(cls_ValueError, "probabilities are not non-negative");
            }
            tot += p->dv[i];
            cum[i] = tot;
        }
        if (fabs(tot - 1.0) > 1e-8) { free(cum);
            throw_new(cls_ValueError, "probabilities do not sum to 1");
        }
        for (i = 0; i < n; i++) cum[i] /= tot;
    }
    if (!repl && cnt > n) {
        free(cum);
        throw_new(cls_ValueError,
            "Cannot take a larger sample than population when 'replace=False'");
    }
    idx = nd_new1(ND_INT, cnt);
    if (cum) {
        if (repl) {
            for (i = 0; i < cnt; i++) {
                double u = nrng_dbl(s);
                long lo = 0, hi = n - 1;
                while (lo < hi) {
                    long mid = (lo + hi) / 2;
                    if (cum[mid] > u) hi = mid; else lo = mid + 1;
                }
                idx->iv[i] = lo;
            }
        } else {
            /* sequential removal, renormalising */
            double *w = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
            double prev = 0.0;
            for (i = 0; i < n; i++) { w[i] = cum[i] - prev; prev = cum[i]; }
            for (i = 0; i < cnt; i++) {
                double tot = 0.0, u, acc = 0.0;
                long pick = n - 1;
                for (j = 0; j < n; j++) tot += w[j];
                u = nrng_dbl(s) * tot;
                for (j = 0; j < n; j++) {
                    acc += w[j];
                    if (w[j] > 0 && acc > u) { pick = j; break; }
                }
                idx->iv[i] = pick;
                w[pick] = 0.0;
            }
            free(w);
        }
        free(cum);
    } else if (repl) {
        for (i = 0; i < cnt; i++) idx->iv[i] = u64_tol(nrng_below(s, u64_l(n)));
    } else {
        long *pool = (long *)xmalloc(sizeof(long) * (size_t)n);
        for (i = 0; i < n; i++) pool[i] = i;
        for (i = 0; i < cnt; i++) {
            long t;
            j = i + u64_tol(nrng_below(s, u64_l(n - i)));
            t = pool[i]; pool[i] = pool[j]; pool[j] = t;
            idx->iv[i] = pool[i];
        }
        free(pool);
    }
    if (nd < 0) {
        if (!src) return mkint(idx->iv[0]);
        if (src->nd == 1) return nd_elem(src, idx->iv[0]);
        row = src->size / n;
        res = nd_new(src->dt, src->nd - 1, src->shape + 1);
        nd_cp(res, 0, src, idx->iv[0] * row, row);
        return ndv(res);
    }
    if (!src) {
        res = nd_new(ND_INT, nd, shape);
        for (i = 0; i < cnt; i++) res->iv[i] = idx->iv[i];
        return nd_ret(res);
    }
    row = src->size / n;
    {
        long rs[ND_MAXD];
        int rn = nd;
        for (k = 0; k < nd; k++) rs[k] = shape[k];
        for (k = 1; k < src->nd && rn < ND_MAXD; k++) rs[rn++] = src->shape[k];
        res = nd_new(src->dt, rn, rs);
    }
    for (i = 0; i < cnt; i++) nd_cp(res, i * row, src, idx->iv[i] * row, row);
    return nd_ret(res);
}
static long nr_poisson1(u64 *s, double lam)
{
    long k = 0;
    while (lam > 500.0) {
        /* split: Poisson(lam) = Poisson(500) + Poisson(lam - 500) */
        k += nr_poisson1(s, 500.0);
        lam -= 500.0;
    }
    {
        double L = exp(-lam), p = 1.0;
        do { k++; p *= nrng_dbl(s); } while (p > L);
    }
    return k - 1;
}
static Value nr_poisson(Args *x, int off, u64 *s)
{
    double lam = rdbl(x, off, 0, "lam", 1.0);
    long shape[ND_MAXD], i;
    int nd = rsize(rarg(x, off, 1, "size"), shape);
    Nd *r;
    if (lam < 0) throw_new(cls_ValueError, "lam < 0 or lam is NaN");
    if (nd < 0) return mkint(nr_poisson1(s, lam));
    r = nd_new(ND_INT, nd, shape);
    for (i = 0; i < r->size; i++) r->iv[i] = nr_poisson1(s, lam);
    return nd_ret(r);
}
static long nr_binom1(u64 *s, long n, double p)
{
    long k = 0, i;
    for (i = 0; i < n; i++) if (nrng_dbl(s) < p) k++;
    return k;
}
static Value nr_binomial(Args *x, int off, u64 *s)
{
    long n = need_long(rarg(x, off, 0, "n"), "n"), shape[ND_MAXD], i;
    double p = rdbl(x, off, 1, "p", 0.5);
    int nd = rsize(rarg(x, off, 2, "size"), shape);
    Nd *r;
    if (n < 0) throw_new(cls_ValueError, "n < 0");
    if (!(p >= 0.0 && p <= 1.0)) throw_new(cls_ValueError, "p < 0, p > 1 or p is NaN");
    if (nd < 0) return mkint(nr_binom1(s, n, p));
    r = nd_new(ND_INT, nd, shape);
    for (i = 0; i < r->size; i++) r->iv[i] = nr_binom1(s, n, p);
    return nd_ret(r);
}

/* module-level (global state) and method (instance state) wrappers */
static u64 *self_state(Args *x)
{
    Value *sv;
    if (x->n < 1 || x->a[0].t != T_INST)
        throw_new(cls_TypeError, "descriptor requires a Generator object");
    sv = amap_get(&AS_INST(x->a[0])->a, intern_c("_s"));
    if (!sv || sv->t != T_ND) throw_new(cls_TypeError, "uninitialised Generator");
    return (u64 *)AS_ND(*sv)->iv;
}
#define RFN(nm, impl) \
  BI(rm_##nm) { return impl(x, 0, g_rs); } \
  BI(rg_##nm) { return impl(x, 1, self_state(x)); }
RFN(random, nr_random)
RFN(stdnorm, nr_stdnorm)
RFN(normal, nr_normal)
RFN(uniform, nr_uniform)
RFN(exponential, nr_exponential)
RFN(stdexp, nr_stdexp)
RFN(randint, nr_randint)
RFN(integers, nr_integers)
RFN(choice, nr_choice)
RFN(shuffle, nr_shuffle)
RFN(permutation, nr_permutation)
RFN(poisson, nr_poisson)
RFN(binomial, nr_binomial)
BI(rm_rand) { return rdims(x, 0, g_rs, g_unif); }
BI(rg_rand) { return rdims(x, 1, self_state(x), g_unif); }
BI(rm_randn) { return rdims(x, 0, g_rs, g_norm); }
BI(rg_randn) { return rdims(x, 1, self_state(x), g_norm); }
BI(rm_seed)
{
    Value sv = x->n > 0 ? A(0) : kwget(x, "seed");
    seed_from(g_rs, sv);
    return NONE_V;
}
static Value make_rng(Class *c, Value seed)
{
    Inst *in = new_inst(c);
    Nd *st = nd_new1(ND_INT, (long)(4 * sizeof(u64) / sizeof(long)));
    Value iv = mkobj(in, T_INST);
    VPUSH(iv);
    seed_from((u64 *)st->iv, seed);
    amap_set(&in->a, intern_c("_s"), ndv(st));
    vsp--;
    return iv;
}
BI(rm_default_rng)
{
    Value sv = x->n > 0 ? A(0) : kwget(x, "seed");
    if (sv.t == T_INST && AS_INST(sv)->cls == cls_rng[0]) return sv;
    return make_rng(cls_rng[0], sv);
}
BI(rm_RandomState)
{
    Value sv = x->n > 0 ? A(0) : kwget(x, "seed");
    return make_rng(cls_rng[1], sv);
}
BI(rg_seed)
{
    Value sv = x->n > 1 ? A(1) : kwget(x, "seed");
    seed_from(self_state(x), sv);
    return NONE_V;
}
static void rnd_methods(Class *c)
{
    def_bi(c, "random", rg_random); def_bi(c, "random_sample", rg_random);
    def_bi(c, "standard_normal", rg_stdnorm); def_bi(c, "normal", rg_normal);
    def_bi(c, "uniform", rg_uniform); def_bi(c, "exponential", rg_exponential);
    def_bi(c, "standard_exponential", rg_stdexp);
    def_bi(c, "integers", rg_integers); def_bi(c, "randint", rg_randint);
    def_bi(c, "choice", rg_choice); def_bi(c, "shuffle", rg_shuffle);
    def_bi(c, "permutation", rg_permutation); def_bi(c, "poisson", rg_poisson);
    def_bi(c, "binomial", rg_binomial); def_bi(c, "rand", rg_rand);
    def_bi(c, "randn", rg_randn); def_bi(c, "seed", rg_seed);
}
static void rnd_init(Module *np)
{
    Module *m = new_module("numpy.random");
    int i;
    static const char *nm[2] = { "Generator", "RandomState" };
    (void)rm_integers;
    for (i = 0; i < 2; i++) {
        Class **b = (Class **)xmalloc(sizeof(Class *));
        b[0] = cls_object;
        cls_rng[i] = make_class(intern_c(nm[i]), b, 1, intern_c("numpy.random"));
        rnd_methods(cls_rng[i]);
        mod_val(m, nm[i], mkobj(cls_rng[i], T_CLASS));
    }
    cls_rng[1]->ctor = rm_RandomState;
    nrng_seed(g_rs, u64_mul(u64_ul(host_seed()), u64_ul(2654435761UL)));
    mod_def(m, "seed", rm_seed); mod_def(m, "default_rng", rm_default_rng);
    mod_def(m, "random", rm_random); mod_def(m, "random_sample", rm_random);
    mod_def(m, "ranf", rm_random); mod_def(m, "sample", rm_random);
    mod_def(m, "rand", rm_rand); mod_def(m, "randn", rm_randn);
    mod_def(m, "standard_normal", rm_stdnorm); mod_def(m, "normal", rm_normal);
    mod_def(m, "uniform", rm_uniform); mod_def(m, "exponential", rm_exponential);
    mod_def(m, "standard_exponential", rm_stdexp);
    mod_def(m, "randint", rm_randint); mod_def(m, "choice", rm_choice);
    mod_def(m, "shuffle", rm_shuffle); mod_def(m, "permutation", rm_permutation);
    mod_def(m, "poisson", rm_poisson); mod_def(m, "binomial", rm_binomial);
    mod_val(np, "random", mkobj(m, T_MODULE));
}
