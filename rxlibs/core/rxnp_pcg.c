/* rxnp_pcg.c - numpy.random.default_rng(): SeedSequence + PCG64 + Generator basics, bit-exact with numpy.
 * Needs a 64-bit unsigned long (LP64).  Algorithms follow numpy's distributions.c; tables are numpy's
 * ziggurat constants (rxnp_zig.inc, copied from python/c/src/p7_rnd.c). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rxnp.h"

typedef unsigned long u64;
typedef char rxnp_chk_u64[sizeof(unsigned long) == 8 ? 1 : -1];
#define U32M 0xFFFFFFFFUL

struct rxnp_gen {
    long refs;
    u64 sh, sl, ih, il;       /* 128-bit state and increment */
    int has32; unsigned long u32;
};

#include "rxnp_zig.inc"
#define ZIG_NOR_R 3.6541528853610087963519472518
#define ZIG_NOR_INV_R 0.27366123732975827203338247596
#define ZIG_EXP_R 7.69711747013104972

static u64 mulhi(u64 a, u64 b)
{
    u64 a0 = a & U32M, a1 = a >> 32, b0 = b & U32M, b1 = b >> 32;
    u64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    u64 mid = (p00 >> 32) + (p01 & U32M) + (p10 & U32M);
    return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}
static void pcg_step(rxnp_gen *g)
{
    u64 mh = 0x2360ED051FC65DA4UL, ml = 0x4385DF649FCCF645UL;
    u64 lo = g->sl * ml;
    u64 hi = mulhi(g->sl, ml) + g->sl * mh + g->sh * ml;
    u64 lo2 = lo + g->il;
    g->sh = hi + g->ih + (lo2 < lo ? 1UL : 0UL);
    g->sl = lo2;
}
static u64 next64(rxnp_gen *g)
{
    u64 v; int r;
    pcg_step(g);
    v = g->sh ^ g->sl; r = (int)(g->sh >> 58);
    return r ? (v >> r) | (v << (64 - r)) : v;
}
static unsigned long next32(rxnp_gen *g)
{
    u64 n;
    if (g->has32) { g->has32 = 0; return g->u32; }
    n = next64(g);
    g->has32 = 1; g->u32 = (unsigned long)(n >> 32);
    return (unsigned long)(n & U32M);
}
static double next_double(rxnp_gen *g) { return (double)(next64(g) >> 11) * (1.0 / 9007199254740992.0); }

/* ---- SeedSequence ---- */
static unsigned long hashmix(unsigned long v, unsigned long *hc)
{
    v ^= *hc; *hc = (*hc * 0x931e8875UL) & U32M; v = (v * *hc) & U32M; v ^= v >> 16; return v;
}
static unsigned long mixw(unsigned long x, unsigned long y)
{
    unsigned long r = (0xca01f9ddUL * x - 0x4973f715UL * y) & U32M; r ^= r >> 16; return r;
}
static void seed_pcg(rxnp_gen *g, const unsigned long *ent, long nent)
{
    unsigned long pool[4], st[8], hc = 0x43b0d7e5UL, d;
    long i, j;
    for (i = 0; i < 4; i++) pool[i] = hashmix(i < nent ? ent[i] : 0UL, &hc);
    for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) if (i != j) pool[j] = mixw(pool[j], hashmix(pool[i], &hc));
    for (i = 4; i < nent; i++) for (j = 0; j < 4; j++) pool[j] = mixw(pool[j], hashmix(ent[i], &hc));
    hc = 0x8b51f9ddUL;
    for (i = 0; i < 8; i++) {
        d = pool[i & 3] ^ hc; hc = (hc * 0x58f38dedUL) & U32M; d = (d * hc) & U32M; d ^= d >> 16; st[i] = d;
    }
    {
        u64 s_hi = ((u64)st[1] << 32) | st[0], s_lo = ((u64)st[3] << 32) | st[2];
        u64 q_hi = ((u64)st[5] << 32) | st[4], q_lo = ((u64)st[7] << 32) | st[6], lo;
        g->sh = g->sl = 0;
        g->ih = (q_hi << 1) | (q_lo >> 63); g->il = (q_lo << 1) | 1UL;
        pcg_step(g);
        lo = g->sl + s_lo; g->sh = g->sh + s_hi + (lo < g->sl ? 1UL : 0UL); g->sl = lo;
        pcg_step(g);
        g->has32 = 0; g->u32 = 0;
    }
}
rxnp_gen *rxnp_gen_new(unsigned long seed)
{
    rxnp_gen *g = (rxnp_gen *)calloc(1, sizeof *g);
    unsigned long ent[2]; long n = 1;
    if (!g) return NULL;
    ent[0] = seed & U32M; ent[1] = (seed >> 32) & U32M;
    if (ent[1]) n = 2;
    g->refs = 1;
    seed_pcg(g, ent, n);
    return g;
}
/* seed from an int64 array (SeedSequence entropy words, one 32-bit word per 32 bits of each element) */
rxnp_gen *rxnp_gen_new_array(const rxnp *a)
{
    rxnp_gen *g; unsigned long *w; long i, n = 0;
    if (!a || a->dt != RXNP_I64 || a->size < 1) return NULL;
    w = (unsigned long *)malloc(sizeof(unsigned long) * (size_t)(2 * a->size));
    if (!w) return NULL;
    for (i = 0; i < a->size; i++) {
        u64 v = (u64)a->i[i];
        w[n++] = (unsigned long)(v & U32M);
        if (v >> 32) w[n++] = (unsigned long)(v >> 32);
    }
    g = (rxnp_gen *)calloc(1, sizeof *g);
    if (g) { g->refs = 1; seed_pcg(g, w, n); }
    free(w);
    return g;
}
rxnp_gen *rxnp_gen_retain(rxnp_gen *g) { if (g) g->refs++; return g; }
void rxnp_gen_release(rxnp_gen *g) { if (g && --g->refs <= 0) free(g); }

/* ---- bounded integers ---- */
static u64 gen_mask(u64 m)
{
    m |= m >> 1; m |= m >> 2; m |= m >> 4; m |= m >> 8; m |= m >> 16; m |= m >> 32; return m;
}
static u64 interval(rxnp_gen *g, u64 max)
{
    u64 mask, v;
    if (max == 0) return 0;
    mask = gen_mask(max);
    if ((max >> 32) == 0) { do v = (u64)next32(g) & mask; while (v > max); }
    else { do v = next64(g) & mask; while (v > max); }
    return v;
}
static u64 lemire32(rxnp_gen *g, unsigned long rng)
{
    unsigned long excl = (rng + 1UL) & U32M, thr;
    u64 m = (u64)next32(g) * excl;
    unsigned long lo = (unsigned long)(m & U32M);
    if (lo < excl) {
        thr = (U32M - rng) % excl;
        while (lo < thr) { m = (u64)next32(g) * excl; lo = (unsigned long)(m & U32M); }
    }
    return m >> 32;
}
static u64 lemire64(rxnp_gen *g, u64 rng)
{
    u64 excl = rng + 1UL, x = next64(g), hi = mulhi(x, excl), lo = x * excl, thr;
    if (lo < excl) {
        thr = (~0UL - rng) % excl;
        while (lo < thr) { x = next64(g); hi = mulhi(x, excl); lo = x * excl; }
    }
    return hi;
}
/* uniform draw from [0, rng] (Generator.integers, masked = 0) */
static u64 bounded(rxnp_gen *g, u64 rng)
{
    if (rng == 0) return 0;
    if ((rng >> 32) == 0) {
        if (rng == U32M) return (u64)next32(g);
        return lemire32(g, (unsigned long)rng);
    }
    if (rng == ~0UL) return next64(g);
    return lemire64(g, rng);
}

/* ---- ziggurat normal ---- */
static u64 zig_k(const unsigned long *t, int i) { return ((u64)t[2 * i] << 32) | t[2 * i + 1]; }
double rxnp_gen_normal(rxnp_gen *g)
{
    for (;;) {
        u64 r = next64(g), rabs;
        int idx = (int)(r & 0xffUL), sign;
        double x, xx, yy;
        r >>= 8;
        sign = (int)(r & 1UL);
        rabs = (r >> 1) & 0x000fffffffffffffUL;
        x = (double)rabs * zig_nor_w[idx];
        if (sign) x = -x;
        if (rabs < zig_k(zig_nor_k, idx)) return x;
        if (idx == 0) {
            for (;;) {
                xx = -ZIG_NOR_INV_R * log1p(-next_double(g));
                yy = -log1p(-next_double(g));
                if (yy + yy > xx * xx) return ((rabs >> 8) & 1UL) ? -(ZIG_NOR_R + xx) : ZIG_NOR_R + xx;
            }
        } else if ((zig_nor_f[idx - 1] - zig_nor_f[idx]) * next_double(g) + zig_nor_f[idx] < exp(-0.5 * x * x)) return x;
    }
}
double rxnp_gen_exponential(rxnp_gen *g)
{
    for (;;) {
        u64 ri = next64(g) >> 3;
        int idx = (int)(ri & 0xffUL);
        double x;
        ri >>= 8;
        x = (double)ri * zig_exp_w[idx];
        if (ri < zig_k(zig_exp_k, idx)) return x;
        if (idx == 0) return ZIG_EXP_R - log1p(-next_double(g));
        if ((zig_exp_f[idx - 1] - zig_exp_f[idx]) * next_double(g) + zig_exp_f[idx] < exp(-x)) return x;
    }
}
double rxnp_gen_random(rxnp_gen *g) { return next_double(g); }

static rxnp *shaped(int dt, const char *shape)
{
    long sh[RXNP_MAXDIM]; int nd;
    if (!rxnp_parse_shape(shape, sh, &nd)) return NULL;
    return rxnp_new(dt, nd, sh);
}
rxnp *rxnp_gen_random_arr(rxnp_gen *g, const char *shape)
{
    rxnp *r = shaped(RXNP_F64, shape); long k;
    if (r) for (k = 0; k < r->size; k++) r->f[k] = next_double(g);
    return r;
}
rxnp *rxnp_gen_normal_arr(rxnp_gen *g, double loc, double scale, const char *shape)
{
    rxnp *r = shaped(RXNP_F64, shape); long k;
    if (r) for (k = 0; k < r->size; k++) r->f[k] = loc + scale * rxnp_gen_normal(g);
    return r;
}
rxnp *rxnp_gen_exponential_arr(rxnp_gen *g, double scale, const char *shape)
{
    rxnp *r = shaped(RXNP_F64, shape); long k;
    if (r) for (k = 0; k < r->size; k++) r->f[k] = scale * rxnp_gen_exponential(g);
    return r;
}
/* Generator.integers(low, high) with high exclusive (endpoint=False) */
rxnp *rxnp_gen_integers(rxnp_gen *g, long low, long high, const char *shape)
{
    rxnp *r; long k; u64 rng;
    if (low >= high) return NULL;
    r = shaped(RXNP_I64, shape);
    if (!r) return NULL;
    rng = (u64)(high - 1) - (u64)low;
    for (k = 0; k < r->size; k++) r->i[k] = low + (long)bounded(g, rng);
    return r;
}
long rxnp_gen_integer1(rxnp_gen *g, long low, long high)
{
    return low + (long)bounded(g, (u64)(high - 1) - (u64)low);
}
/* in-place Fisher-Yates over the first axis (Generator.shuffle) */
int rxnp_gen_shuffle(rxnp_gen *g, rxnp *a)
{
    long n, row, i, j, k;
    if (a->ndim < 1) return 0;
    n = a->shape[0]; row = n > 0 ? a->size / n : 0;
    for (i = n - 1; i > 0; i--) {
        j = (long)interval(g, (u64)i);
        if (j == i) continue;
        for (k = 0; k < row; k++) {
            if (a->dt == RXNP_F64) { double t = a->f[i * row + k]; a->f[i * row + k] = a->f[j * row + k]; a->f[j * row + k] = t; }
            else { long t = a->i[i * row + k]; a->i[i * row + k] = a->i[j * row + k]; a->i[j * row + k] = t; }
        }
    }
    return 1;
}
rxnp *rxnp_gen_permutation(rxnp_gen *g, long n)
{
    rxnp *r = rxnp_arange(0.0, (double)n, 1.0, 1);
    if (r) rxnp_gen_shuffle(g, r);
    return r;
}
/* Generator.choice(a, size, replace, p=None, shuffle=True) for 1-d a; returns array of selected elements */
rxnp *rxnp_gen_choice(rxnp_gen *g, const rxnp *a, long size, int replace)
{
    long n = a->size, i, j; rxnp *idx, *res;
    if (a->ndim != 1 || n < 1 || size < 0) return NULL;
    if (!replace && size > n) return NULL;
    idx = rxnp_new(RXNP_I64, 1, &size);
    if (!idx) return NULL;
    if (replace) {
        for (i = 0; i < size; i++) idx->i[i] = (long)bounded(g, (u64)(n - 1));
    } else if (n > 10000 && size > n / 50) {
        long *arr = (long *)malloc(sizeof(long) * (size_t)n), stop = n - size > 1 ? n - size : 1, t;
        if (!arr) { rxnp_release(idx); return NULL; }
        for (i = 0; i < n; i++) arr[i] = i;
        for (i = n - 1; i >= stop; i--) { j = (long)bounded(g, (u64)i); t = arr[i]; arr[i] = arr[j]; arr[j] = t; }
        for (i = 0; i < size; i++) idx->i[i] = arr[n - size + i];
        free(arr);
    } else {
        unsigned long m1 = (unsigned long)(1.2 * (double)size), mask, setsize;
        long *hs;
        m1 |= m1 >> 1; m1 |= m1 >> 2; m1 |= m1 >> 4; m1 |= m1 >> 8; m1 |= m1 >> 16; mask = m1; setsize = mask + 1UL;
        hs = (long *)malloc(sizeof(long) * (size_t)setsize);
        if (!hs) { rxnp_release(idx); return NULL; }
        for (i = 0; i < (long)setsize; i++) hs[i] = -1;
        for (j = n - size; j < n; j++) {
            long val = (long)bounded(g, (u64)j);
            unsigned long loc = (unsigned long)val & mask;
            while (hs[loc] != -1 && hs[loc] != val) loc = (loc + 1UL) & mask;
            if (hs[loc] == -1) { hs[loc] = val; idx->i[j - n + size] = val; }
            else {
                loc = (unsigned long)j & mask;
                while (hs[loc] != -1) loc = (loc + 1UL) & mask;
                hs[loc] = j; idx->i[j - n + size] = j;
            }
        }
        free(hs);
        for (i = size - 1; i > 0; i--) {
            long t; j = (long)bounded(g, (u64)i); t = idx->i[i]; idx->i[i] = idx->i[j]; idx->i[j] = t;
        }
    }
    res = rxnp_new(a->dt, 1, &size);
    if (res) for (i = 0; i < size; i++) { if (a->dt == RXNP_F64) res->f[i] = a->f[idx->i[i]]; else res->i[i] = a->i[idx->i[i]]; }
    rxnp_release(idx);
    return res;
}
