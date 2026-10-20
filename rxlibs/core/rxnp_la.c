/* rxnp_la.c - linear algebra front end over the LAPACK-style kernels. C89. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rxnp.h"
#include "rxnp_lapack.h"

static double rx_hypot(double a, double b) { return sqrt(a * a + b * b); }
static char lerr[160];
const char *rxnp_la_error(void) { return lerr; }
static void *lfail(const char *m) { strncpy(lerr, m, sizeof lerr - 1); return NULL; }

/* float64 row-major copy of a square/2-d matrix */
static double *mat_copy(const rxnp *a, long *m, long *n)
{
    double *w; long k;
    if (a->ndim != 2) return (double *)lfail("linalg: array must be 2-dimensional");
    *m = a->shape[0]; *n = a->shape[1];
    w = (double *)malloc((size_t)(a->size ? a->size : 1) * sizeof(double));
    if (!w) return (double *)lfail("out of memory");
    for (k = 0; k < a->size; k++) w[k] = a->dt == RXNP_F64 ? a->f[k] : (double)a->i[k];
    for (k = 0; k < a->size; k++) if (w[k] - w[k] != 0.0) { free(w); return (double *)lfail("Array must not contain infs or NaNs"); }
    return w;
}
static rxnp *wrap(double *w, int nd, long r, long c)
{
    long sh[2]; rxnp *o;
    sh[0] = r; sh[1] = c;
    o = rxnp_new(RXNP_F64, nd, sh);
    if (!o) { free(w); return NULL; }
    memcpy(o->f, w, (size_t)(r * (nd == 2 ? c : 1)) * sizeof(double));
    free(w);
    return o;
}
static double *square(const rxnp *a, long *n)
{
    long m; double *w = mat_copy(a, &m, n);
    if (w && m != *n) { free(w); return (double *)lfail("Last 2 dimensions of the array must be square"); }
    return w;
}

rxnp *rxnp_solve(const rxnp *a, const rxnp *b)
{
    long n, nrhs, k; double *lu, *x; long *piv; rxnp *r; int sg;
    lu = square(a, &n);
    if (!lu) return NULL;
    if (b->ndim < 1 || b->ndim > 2 || b->shape[0] != n) { free(lu); return (rxnp *)lfail("solve: incompatible dimensions"); }
    nrhs = b->ndim == 2 ? b->shape[1] : 1;
    x = (double *)malloc((size_t)(n * nrhs + 1) * sizeof(double));
    piv = (long *)malloc((size_t)(n + 1) * sizeof(long));
    if (!x || !piv) { free(lu); free(x); free(piv); return (rxnp *)lfail("out of memory"); }
    for (k = 0; k < n * nrhs; k++) x[k] = b->dt == RXNP_F64 ? b->f[k] : (double)b->i[k];
    sg = rxla_lu_dec(lu, n, piv);
    if (!sg) { free(lu); free(x); free(piv); return (rxnp *)lfail("Singular matrix"); }
    rxla_lu_solve(lu, n, piv, x, nrhs);
    free(lu); free(piv);
    r = wrap(x, b->ndim, n, nrhs);
    return r;
}
rxnp *rxnp_inv(const rxnp *a)
{
    long n, k; double *lu, *x; long *piv; int sg;
    lu = square(a, &n);
    if (!lu) return NULL;
    x = (double *)calloc((size_t)(n * n + 1), sizeof(double));
    piv = (long *)malloc((size_t)(n + 1) * sizeof(long));
    if (!x || !piv) { free(lu); free(x); free(piv); return (rxnp *)lfail("out of memory"); }
    for (k = 0; k < n; k++) x[k * n + k] = 1.0;
    sg = rxla_lu_dec(lu, n, piv);
    if (!sg) { free(lu); free(x); free(piv); return (rxnp *)lfail("Singular matrix"); }
    rxla_lu_solve(lu, n, piv, x, n);
    free(lu); free(piv);
    return wrap(x, 2, n, n);
}
int rxnp_det(const rxnp *a, double *out)
{
    long n, k; double *lu; long *piv; int sg; double d;
    lu = square(a, &n);
    if (!lu) return 0;
    if (n == 0) { free(lu); *out = 1.0; return 1; }
    piv = (long *)malloc((size_t)(n + 1) * sizeof(long));
    if (!piv) { free(lu); lfail("out of memory"); return 0; }
    sg = rxla_lu_dec(lu, n, piv);
    if (!sg) { *out = 0.0; free(lu); free(piv); return 1; }
    d = (double)sg;
    for (k = 0; k < n; k++) d *= lu[k * n + k];
    free(lu); free(piv);
    *out = d;
    return 1;
}
rxnp *rxnp_cholesky(const rxnp *a)
{
    long n, i, j, k; double *w, *l; double s;
    w = square(a, &n);
    if (!w) return NULL;
    l = (double *)calloc((size_t)(n * n + 1), sizeof(double));
    if (!l) { free(w); return (rxnp *)lfail("out of memory"); }
    for (j = 0; j < n; j++) {
        s = w[j * n + j];
        for (k = 0; k < j; k++) s -= l[j * n + k] * l[j * n + k];
        if (!(s > 0.0)) { free(w); free(l); return (rxnp *)lfail("Matrix is not positive definite"); }
        l[j * n + j] = sqrt(s);
        for (i = j + 1; i < n; i++) {
            s = w[i * n + j];
            for (k = 0; k < j; k++) s -= l[i * n + k] * l[j * n + k];
            l[i * n + j] = s / l[j * n + j];
        }
    }
    free(w);
    return wrap(l, 2, n, n);
}
int rxnp_qr(const rxnp *a, rxnp **q, rxnp **r)
{
    long m, n, K, i, j, k; double *w, *vs, *tau, *Q, *R;
    w = mat_copy(a, &m, &n);
    if (!w) return 0;
    K = m < n ? m : n;
    vs = (double *)calloc((size_t)(m * (K ? K : 1) + 1), sizeof(double));
    tau = (double *)calloc((size_t)(K + 1), sizeof(double));
    Q = (double *)calloc((size_t)(m * (K ? K : 1) + 1), sizeof(double));
    R = (double *)calloc((size_t)(K * n + 1), sizeof(double));
    if (!vs || !tau || !Q || !R) { free(w); free(vs); free(tau); free(Q); free(R); lfail("out of memory"); return 0; }
    for (k = 0; k < K; k++) {
        double alpha = w[k * n + k], xn = 0.0, beta;
        for (i = k + 1; i < m; i++) xn = rx_hypot(xn, w[i * n + k]);
        vs[k * K + k] = 1.0;
        if (xn == 0.0) { tau[k] = 0.0; continue; }
        beta = -rx_hypot(alpha, xn) * (alpha >= 0 ? 1.0 : -1.0);
        tau[k] = (beta - alpha) / beta;
        for (i = k + 1; i < m; i++) vs[i * K + k] = w[i * n + k] / (alpha - beta);
        w[k * n + k] = beta;
        for (i = k + 1; i < m; i++) w[i * n + k] = 0.0;
        for (j = k + 1; j < n; j++) {
            double d = 0.0;
            for (i = k; i < m; i++) d += vs[i * K + k] * w[i * n + j];
            d *= tau[k];
            for (i = k; i < m; i++) w[i * n + j] -= d * vs[i * K + k];
        }
    }
    for (i = 0; i < K; i++) for (j = i; j < n; j++) R[i * n + j] = w[i * n + j];
    for (i = 0; i < K; i++) Q[i * K + i] = 1.0;
    for (k = K - 1; k >= 0; k--) {
        if (tau[k] == 0.0) continue;
        for (j = 0; j < K; j++) {
            double d = 0.0;
            for (i = k; i < m; i++) d += vs[i * K + k] * Q[i * K + j];
            d *= tau[k];
            for (i = k; i < m; i++) Q[i * K + j] -= d * vs[i * K + k];
        }
    }
    free(w); free(vs); free(tau);
    *q = wrap(Q, 2, m, K); *r = wrap(R, 2, K, n);
    return *q && *r;
}
int rxnp_eigh(const rxnp *a, rxnp **wv, rxnp **vv)
{
    long n; double *m, *w, *v; int bad;
    m = square(a, &n);
    if (!m) return 0;
    w = (double *)calloc((size_t)(n + 1), sizeof(double));
    v = (double *)calloc((size_t)(n * n + 1), sizeof(double));
    if (!w || !v) { free(m); free(w); free(v); lfail("out of memory"); return 0; }
    bad = rxla_syev(m, n, 0, w, v);
    free(m);
    if (bad) { free(w); free(v); lfail("Eigenvalues did not converge"); return 0; }
    *wv = wrap(w, 1, n, 1); *vv = wrap(v, 2, n, n);
    return *wv && *vv;
}
int rxnp_svd(const rxnp *a, rxnp **u, rxnp **s, rxnp **vt)
{
    long m, n, r; double *w, *U, *S, *V; int bad;
    w = mat_copy(a, &m, &n);
    if (!w) return 0;
    r = m < n ? m : n;
    U = (double *)calloc((size_t)(m * r + 1), sizeof(double));
    S = (double *)calloc((size_t)(r + 1), sizeof(double));
    V = (double *)calloc((size_t)(r * n + 1), sizeof(double));
    if (!U || !S || !V) { free(w); free(U); free(S); free(V); lfail("out of memory"); return 0; }
    bad = r ? rxla_gesdd(w, m, n, 0, 1, S, U, V) : 0;
    free(w);
    if (bad) { free(U); free(S); free(V); lfail("SVD did not converge"); return 0; }
    *u = wrap(U, 2, m, r); *s = wrap(S, 1, r, 1); *vt = wrap(V, 2, r, n);
    return *u && *s && *vt;
}
rxnp *rxnp_eigvals(const rxnp *a)
{
    long n, i; double *m, *wr, *wi, *o; int bad;
    m = square(a, &n);
    if (!m) return NULL;
    wr = (double *)calloc((size_t)(n + 1), sizeof(double));
    wi = (double *)calloc((size_t)(n + 1), sizeof(double));
    o = (double *)calloc((size_t)(2 * n + 1), sizeof(double));
    if (!wr || !wi || !o) { free(m); free(wr); free(wi); free(o); return (rxnp *)lfail("out of memory"); }
    bad = rxla_geev(m, n, wr, wi, NULL);
    free(m);
    if (bad) { free(wr); free(wi); free(o); return (rxnp *)lfail("Eigenvalues did not converge"); }
    for (i = 0; i < n; i++) { o[2 * i] = wr[i]; o[2 * i + 1] = wi[i]; }
    free(wr); free(wi);
    return wrap(o, 2, n, 2);
}
