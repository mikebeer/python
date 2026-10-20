/* rxnp_rng.c - legacy numpy.random (MT19937, seed(int), random_sample, gauss, randint). C89. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rxnp.h"

#define MT_N 624
#define MT_M 397
#define U32M 0xFFFFFFFFUL
struct rxnp_rng {
    unsigned long mt[MT_N];
    int pos;
    int has_gauss;
    double gauss;
};
rxnp_rng *rxnp_rng_new(void)
{
    rxnp_rng *g = (rxnp_rng *)calloc(1, sizeof *g);
    if (g) rxnp_rng_seed(g, 5489UL);
    return g;
}
void rxnp_rng_free(rxnp_rng *g) { free(g); }
void rxnp_rng_seed(rxnp_rng *g, unsigned long s)
{
    int i;
    s &= U32M;
    for (i = 0; i < MT_N; i++) {
        g->mt[i] = s;
        s = (1812433253UL * (s ^ (s >> 30)) + (unsigned long)i + 1UL) & U32M;
    }
    g->pos = MT_N; g->has_gauss = 0; g->gauss = 0.0;
}
static unsigned long next32(rxnp_rng *g)
{
    unsigned long y, *mt = g->mt;
    if (g->pos >= MT_N) {
        int kk;
        for (kk = 0; kk < MT_N - MT_M; kk++) {
            y = (mt[kk] & 0x80000000UL) | (mt[kk + 1] & 0x7fffffffUL);
            mt[kk] = mt[kk + MT_M] ^ (y >> 1) ^ ((y & 1UL) ? 0x9908b0dfUL : 0UL);
        }
        for (; kk < MT_N - 1; kk++) {
            y = (mt[kk] & 0x80000000UL) | (mt[kk + 1] & 0x7fffffffUL);
            mt[kk] = mt[kk + (MT_M - MT_N)] ^ (y >> 1) ^ ((y & 1UL) ? 0x9908b0dfUL : 0UL);
        }
        y = (mt[MT_N - 1] & 0x80000000UL) | (mt[0] & 0x7fffffffUL);
        mt[MT_N - 1] = mt[MT_M - 1] ^ (y >> 1) ^ ((y & 1UL) ? 0x9908b0dfUL : 0UL);
        g->pos = 0;
    }
    y = mt[g->pos++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);
    return y & U32M;
}
double rxnp_rng_random(rxnp_rng *g)
{
    unsigned long a = next32(g) >> 5, b = next32(g) >> 6;
    return ((double)a * 67108864.0 + (double)b) / 9007199254740992.0;
}
double rxnp_rng_gauss(rxnp_rng *g)
{
    double f, x1, x2, r2;
    if (g->has_gauss) { g->has_gauss = 0; return g->gauss; }
    do {
        x1 = 2.0 * rxnp_rng_random(g) - 1.0;
        x2 = 2.0 * rxnp_rng_random(g) - 1.0;
        r2 = x1 * x1 + x2 * x2;
    } while (r2 >= 1.0 || r2 == 0.0);
    f = sqrt(-2.0 * log(r2) / r2);
    g->gauss = f * x1; g->has_gauss = 1;
    return f * x2;
}
long rxnp_rng_randint(rxnp_rng *g, long low, long high)
{
    unsigned long rng, mask, v;
    if (high <= low) return low;
    rng = (unsigned long)(high - 1 - low);
    if (rng == 0) return low;
    mask = rng; mask |= mask >> 1; mask |= mask >> 2; mask |= mask >> 4;
    mask |= mask >> 8; mask |= mask >> 16;
    if (rng <= U32M) {
        while ((v = next32(g) & mask) > rng) ;
    } else {
        mask |= mask >> 32;
        while (((v = ((next32(g) << 32) | next32(g)) & mask)) > rng) ;
    }
    return low + (long)v;
}
rxnp *rxnp_rand(rxnp_rng *g, const char *shape)
{
    rxnp *r = rxnp_full(shape, 0.0, RXNP_F64); long k;
    if (r) for (k = 0; k < r->size; k++) r->f[k] = rxnp_rng_random(g);
    return r;
}
rxnp *rxnp_randn(rxnp_rng *g, const char *shape)
{
    rxnp *r = rxnp_full(shape, 0.0, RXNP_F64); long k;
    if (r) for (k = 0; k < r->size; k++) r->f[k] = rxnp_rng_gauss(g);
    return r;
}
rxnp *rxnp_randint(rxnp_rng *g, long low, long high, const char *shape)
{
    rxnp *r = rxnp_full(shape, 0.0, RXNP_I64); long k;
    if (r) for (k = 0; k < r->size; k++) r->i[k] = rxnp_rng_randint(g, low, high);
    return r;
}
