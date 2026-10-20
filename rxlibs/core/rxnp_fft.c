/* rxnp_fft.c - complex FFT (radix-2, Bluestein for other lengths). C89. */
#include <stdlib.h>
#include <math.h>
#include "rxnp.h"

#define TWO_PI 6.283185307179586476925286766559

static void fft2(double *re, double *im, long n, int sign)
{
    long i, j, k, m;
    for (i = 1, j = 0; i < n; i++) {
        long bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (m = 2; m <= n; m <<= 1) {
        double ang = sign * TWO_PI / (double)m;
        long half = m >> 1;
        for (k = 0; k < half; k++) {
            double wr = cos(ang * (double)k), wi = sin(ang * (double)k);
            for (i = k; i < n; i += m) {
                long l = i + half;
                double tr = re[l] * wr - im[l] * wi, ti = re[l] * wi + im[l] * wr;
                re[l] = re[i] - tr; im[l] = im[i] - ti;
                re[i] += tr; im[i] += ti;
            }
        }
    }
}
static int bluestein(double *re, double *im, long n, int sign)
{
    long m = 1, k; double *wr, *wi, *ar, *ai, *br, *bi;
    while (m < 2 * n - 1) m <<= 1;
    wr = (double *)malloc((size_t)n * sizeof(double)); wi = (double *)malloc((size_t)n * sizeof(double));
    ar = (double *)calloc((size_t)m, sizeof(double)); ai = (double *)calloc((size_t)m, sizeof(double));
    br = (double *)calloc((size_t)m, sizeof(double)); bi = (double *)calloc((size_t)m, sizeof(double));
    if (!wr || !wi || !ar || !ai || !br || !bi) { free(wr); free(wi); free(ar); free(ai); free(br); free(bi); return 0; }
    for (k = 0; k < n; k++) {
        /* exp(sign*i*pi*k^2/n), k^2 reduced mod 2n for accuracy */
        long kk = (long)(((unsigned long)k * (unsigned long)k) % (unsigned long)(2 * n));
        double ang = sign * TWO_PI / 2.0 * (double)kk / (double)n;
        wr[k] = cos(ang); wi[k] = sin(ang);
        ar[k] = re[k] * wr[k] - im[k] * wi[k];
        ai[k] = re[k] * wi[k] + im[k] * wr[k];
    }
    br[0] = wr[0]; bi[0] = -wi[0];
    for (k = 1; k < n; k++) { br[k] = br[m - k] = wr[k]; bi[k] = bi[m - k] = -wi[k]; }
    fft2(ar, ai, m, -1); fft2(br, bi, m, -1);
    for (k = 0; k < m; k++) { double t = ar[k] * br[k] - ai[k] * bi[k]; ai[k] = ar[k] * bi[k] + ai[k] * br[k]; ar[k] = t; }
    fft2(ar, ai, m, 1);
    for (k = 0; k < n; k++) {
        double cr = ar[k] / (double)m, ci = ai[k] / (double)m;
        re[k] = cr * wr[k] - ci * wi[k];
        im[k] = cr * wi[k] + ci * wr[k];
    }
    free(wr); free(wi); free(ar); free(ai); free(br); free(bi);
    return 1;
}
rxnp *rxnp_fft(const rxnp *a, int inverse)
{
    long n, k, sh[2]; double *re, *im; rxnp *r; int sign = inverse ? 1 : -1;
    if (a->ndim == 1) n = a->shape[0];
    else if (a->ndim == 2 && a->shape[1] == 2) n = a->shape[0];
    else return NULL;
    sh[0] = n; sh[1] = 2;
    r = rxnp_new(RXNP_F64, 2, sh);
    re = (double *)malloc((size_t)(n + 1) * sizeof(double)); im = (double *)malloc((size_t)(n + 1) * sizeof(double));
    if (!r || !re || !im) { free(re); free(im); rxnp_release(r); return NULL; }
    for (k = 0; k < n; k++) {
        if (a->ndim == 1) { re[k] = a->dt == RXNP_F64 ? a->f[k] : (double)a->i[k]; im[k] = 0.0; }
        else { re[k] = a->dt == RXNP_F64 ? a->f[2 * k] : (double)a->i[2 * k]; im[k] = a->dt == RXNP_F64 ? a->f[2 * k + 1] : (double)a->i[2 * k + 1]; }
    }
    if (n > 1) {
        if ((n & (n - 1)) == 0) fft2(re, im, n, sign);
        else if (!bluestein(re, im, n, sign)) { free(re); free(im); rxnp_release(r); return NULL; }
    }
    for (k = 0; k < n; k++) {
        double s = inverse ? 1.0 / (double)n : 1.0;
        r->f[2 * k] = re[k] * s; r->f[2 * k + 1] = im[k] * s;
    }
    free(re); free(im);
    return r;
}
