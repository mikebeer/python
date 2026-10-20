/* rxnp_idx.c - comparison, fancy / boolean indexing, joining, unique, n-d matmul / dot / tensordot.  C89. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rxnp.h"

#define ERR(m) (rxnp_seterror(m), NULL)

static int bshape(const rxnp *a, const rxnp *b, long *out, int *nout)
{
    int n = a->ndim > b->ndim ? a->ndim : b->ndim, k;
    for (k = 0; k < n; k++) {
        long da = k < n - a->ndim ? 1 : a->shape[k - (n - a->ndim)];
        long db = k < n - b->ndim ? 1 : b->shape[k - (n - b->ndim)];
        if (da != db && da != 1 && db != 1) return 0;
        out[k] = da == 1 ? db : da;
    }
    *nout = n; return 1;
}
/* element offsets of a into the broadcast shape out[0..n) for flat out index q */
static long boff(const rxnp *a, int n, const long *out, long q)
{
    long off = 0, stride = 1, idx; int k;
    for (k = a->ndim - 1; k >= 0; k--) {
        int ok = k + (n - a->ndim);
        idx = (q / 1);
        {   long qq = q, s = 1; int j;
            for (j = n - 1; j > ok; j--) s *= out[j];
            idx = (qq / s) % out[ok];
        }
        if (a->shape[k] != 1) off += idx * stride;
        stride *= a->shape[k];
    }
    return off;
}
static double getd(const rxnp *a, long k) { return a->dt == RXNP_F64 ? a->f[k] : (double)a->i[k]; }

rxnp *rxnp_cmp(int op, const rxnp *a, const rxnp *b)
{
    long out[RXNP_MAXDIM], q; int n; rxnp *r;
    if (!bshape(a, b, out, &n)) return ERR("operands could not be broadcast together");
    r = rxnp_new(RXNP_I64, n, out);
    if (!r) return NULL;
    for (q = 0; q < r->size; q++) {
        double x = getd(a, boff(a, n, out, q)), y = getd(b, boff(b, n, out, q));
        int v = 0;
        switch (op) {
            case RXNP_GT: v = x > y; break; case RXNP_LT: v = x < y; break;
            case RXNP_GE: v = x >= y; break; case RXNP_LE: v = x <= y; break;
            case RXNP_EQ: v = x == y; break; default: v = x != y; break;
        }
        r->i[q] = v;
    }
    return r;
}

rxnp *rxnp_take(const rxnp *a, const rxnp *idx, int axis)
{
    long sh[RXNP_MAXDIM], outer = 1, inner = 1, n, o, j, m; int nd = 0, d; rxnp *r;
    sh[0] = 0;
    if (idx->dt != RXNP_I64) return ERR("take: indices must be integers");
    if (axis < -1000) {                       /* flattened */
        n = a->size; inner = 1; outer = 1;
        for (d = 0; d < idx->ndim; d++) sh[nd++] = idx->shape[d];
    } else {
        if (axis < 0) axis += a->ndim;
        if (axis < 0 || axis >= a->ndim) return ERR("axis out of range");
        n = a->shape[axis];
        for (d = 0; d < axis; d++) { outer *= a->shape[d]; sh[nd++] = a->shape[d]; }
        for (d = 0; d < idx->ndim; d++) sh[nd++] = idx->shape[d];
        for (d = axis + 1; d < a->ndim; d++) { inner *= a->shape[d]; sh[nd++] = a->shape[d]; }
    }
    m = idx->size;
    for (j = 0; j < m; j++) if (idx->i[j] >= n || idx->i[j] < -n) { rxnp_seterror("index out of bounds"); return NULL; }
    if (nd > RXNP_MAXDIM) return ERR("too many dimensions");
    r = rxnp_new(a->dt, nd, sh);
    if (!r) return NULL;
    for (o = 0; o < outer; o++) for (j = 0; j < m; j++) {
        long ii = idx->i[j] < 0 ? idx->i[j] + n : idx->i[j], k;
        for (k = 0; k < inner; k++) {
            long dst = (o * m + j) * inner + k, src = (o * n + ii) * inner + k;
            if (a->dt == RXNP_F64) r->f[dst] = a->f[src]; else r->i[dst] = a->i[src];
        }
    }
    return r;
}

rxnp *rxnp_where(const rxnp *c, const rxnp *a, const rxnp *b)
{
    long o1[RXNP_MAXDIM], out[RXNP_MAXDIM], q; int n1, n; rxnp *r; rxnp tmp; int dt;
    if (!bshape(a, b, o1, &n1)) return ERR("operands could not be broadcast together");
    tmp.ndim = n1; memcpy(tmp.shape, o1, sizeof(long) * (size_t)n1);
    if (!bshape(c, &tmp, out, &n)) return ERR("operands could not be broadcast together");
    dt = (a->dt == RXNP_I64 && b->dt == RXNP_I64) ? RXNP_I64 : RXNP_F64;
    r = rxnp_new(dt, n, out);
    if (!r) return NULL;
    for (q = 0; q < r->size; q++) {
        int t = getd(c, boff(c, n, out, q)) != 0.0;
        const rxnp *s = t ? a : b;
        long o = boff(s, n, out, q);
        if (dt == RXNP_F64) r->f[q] = getd(s, o); else r->i[q] = s->i[o];
    }
    return r;
}

rxnp *rxnp_compress(const rxnp *mask, const rxnp *a, int axis)
{
    long cnt = 0, k, j; rxnp *idx, *r;
    if (mask->ndim != 1) return ERR("compress: mask must be 1-d");
    for (k = 0; k < mask->size; k++) if (getd(mask, k) != 0.0) cnt++;
    idx = rxnp_new(RXNP_I64, 1, &cnt);
    if (!idx) return NULL;
    for (k = 0, j = 0; k < mask->size; k++) if (getd(mask, k) != 0.0) idx->i[j++] = k;
    r = rxnp_take(a, idx, axis);
    rxnp_release(idx);
    return r;
}
rxnp *rxnp_boolean_get(const rxnp *a, const rxnp *mask)
{
    int same = a->ndim == mask->ndim, d; long k, cnt = 0, j; rxnp *r;
    if (same) for (d = 0; d < a->ndim; d++) if (a->shape[d] != mask->shape[d]) same = 0;
    if (!same) {
        if (mask->ndim == 1 && a->ndim >= 1 && mask->shape[0] == a->shape[0]) return rxnp_compress(mask, a, 0);
        return ERR("boolean index did not match indexed array");
    }
    for (k = 0; k < mask->size; k++) if (getd(mask, k) != 0.0) cnt++;
    r = rxnp_new(a->dt, 1, &cnt);
    if (!r) return NULL;
    for (k = 0, j = 0; k < a->size; k++) if (getd(mask, k) != 0.0) {
        if (a->dt == RXNP_F64) r->f[j++] = a->f[k]; else r->i[j++] = a->i[k];
    }
    return r;
}
int rxnp_boolean_set(rxnp *a, const rxnp *mask, const rxnp *values)
{
    int d; long k, cnt = 0, j = 0;
    if (a->ndim != mask->ndim) { rxnp_seterror("boolean index did not match indexed array"); return 0; }
    for (d = 0; d < a->ndim; d++) if (a->shape[d] != mask->shape[d]) { rxnp_seterror("boolean index did not match indexed array"); return 0; }
    for (k = 0; k < mask->size; k++) if (getd(mask, k) != 0.0) cnt++;
    if (values->size != 1 && values->size != cnt) { rxnp_seterror("NumPy boolean array indexing assignment cannot assign values to the target"); return 0; }
    for (k = 0; k < a->size; k++) if (getd(mask, k) != 0.0) {
        double v = getd(values, values->size == 1 ? 0 : j);
        if (a->dt == RXNP_F64) a->f[k] = v; else a->i[k] = (long)v;
        j++;
    }
    return 1;
}

static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return a < b ? -1 : a > b ? 1 : 0; }
static int cmpl(const void *x, const void *y) { long a = *(const long *)x, b = *(const long *)y; return a < b ? -1 : a > b ? 1 : 0; }
rxnp *rxnp_unique(const rxnp *a, rxnp **counts)
{
    long n = a->size, k, u = 0; rxnp *r, *c; double *tf = NULL; long *tl = NULL, *cn;
    if (a->dt == RXNP_F64) { tf = (double *)malloc(sizeof(double) * (size_t)(n + 1)); if (!tf) return ERR("out of memory"); memcpy(tf, a->f, sizeof(double) * (size_t)n); qsort(tf, (size_t)n, sizeof(double), cmpd); }
    else { tl = (long *)malloc(sizeof(long) * (size_t)(n + 1)); if (!tl) return ERR("out of memory"); memcpy(tl, a->i, sizeof(long) * (size_t)n); qsort(tl, (size_t)n, sizeof(long), cmpl); }
    cn = (long *)malloc(sizeof(long) * (size_t)(n + 1));
    if (!cn) { free(tf); free(tl); return ERR("out of memory"); }
    for (k = 0; k < n; k++) {
        int same = k > 0 && (tf ? tf[k] == tf[k - 1] : tl[k] == tl[k - 1]);
        if (same) cn[u - 1]++; else { if (tf) tf[u] = tf[k]; else tl[u] = tl[k]; cn[u++] = 1; }
    }
    r = rxnp_new(a->dt, 1, &u);
    if (r) { if (tf) memcpy(r->f, tf, sizeof(double) * (size_t)u); else memcpy(r->i, tl, sizeof(long) * (size_t)u); }
    if (counts) {
        c = rxnp_new(RXNP_I64, 1, &u);
        if (c) memcpy(c->i, cn, sizeof(long) * (size_t)u);
        *counts = c;
    }
    free(tf); free(tl); free(cn);
    return r;
}

static rxnp *with_axis(const rxnp *a, int axis)
{
    char buf[200]; int d, n = 0, nd = a->ndim + 1; rxnp *r;
    if (axis < 0) axis += nd;
    if (axis < 0 || axis >= nd) return ERR("axis out of range");
    buf[0] = 0;
    for (d = 0; d < nd; d++) {
        long v = d == axis ? 1 : a->shape[d < axis ? d : d - 1];
        n += sprintf(buf + n, "%s%ld", d ? "," : "", v);
    }
    r = rxnp_reshape(a, buf);
    return r;
}
rxnp *rxnp_stack(const rxnp *a, const rxnp *b, int axis)
{
    rxnp *x, *y, *r; int d;
    if (a->ndim != b->ndim) return ERR("all input arrays must have the same shape");
    for (d = 0; d < a->ndim; d++) if (a->shape[d] != b->shape[d]) return ERR("all input arrays must have the same shape");
    x = with_axis(a, axis); y = with_axis(b, axis);
    if (!x || !y) { rxnp_release(x); rxnp_release(y); return NULL; }
    r = rxnp_concat(x, y, axis < 0 ? axis + a->ndim + 1 : axis);
    rxnp_release(x); rxnp_release(y);
    return r;
}
static rxnp *atleast2(const rxnp *a)
{
    char buf[64];
    if (a->ndim >= 2) return rxnp_retain((rxnp *)a);
    if (a->ndim == 0) return rxnp_reshape(a, "1,1");
    sprintf(buf, "1,%ld", a->shape[0]);
    return rxnp_reshape(a, buf);
}
rxnp *rxnp_vstack(const rxnp *a, const rxnp *b)
{
    rxnp *x = atleast2(a), *y = atleast2(b), *r;
    if (!x || !y) { rxnp_release(x); rxnp_release(y); return NULL; }
    r = rxnp_concat(x, y, 0); rxnp_release(x); rxnp_release(y); return r;
}
rxnp *rxnp_hstack(const rxnp *a, const rxnp *b)
{
    if (a->ndim <= 1 && b->ndim <= 1) {
        rxnp *x = rxnp_reshape(a, "-1"), *y = rxnp_reshape(b, "-1"), *r;
        if (!x || !y) { rxnp_release(x); rxnp_release(y); return NULL; }
        r = rxnp_concat(x, y, 0); rxnp_release(x); rxnp_release(y); return r;
    }
    return rxnp_concat(a, b, 1);
}

/* ---- n-d products ---- */
static rxnp *matmul2(const double *x, const double *y, long m, long k, long n, double *z)
{
    long i, j, p;
    for (i = 0; i < m * n; i++) z[i] = 0.0;
    for (i = 0; i < m; i++) for (p = 0; p < k; p++) {
        double av = x[i * k + p];
        for (j = 0; j < n; j++) z[i * n + j] += av * y[p * n + j];
    }
    return NULL;
}
rxnp *rxnp_matmul_nd(const rxnp *a0, const rxnp *b0)
{
    rxnp *a = NULL, *b = NULL, *r = NULL, *fa, *fb; int ints = a0->dt == RXNP_I64 && b0->dt == RXNP_I64;
    long m, k, n, nb, q, sh[RXNP_MAXDIM], bsh[RXNP_MAXDIM], ba[RXNP_MAXDIM], bb[RXNP_MAXDIM]; int nbd, nd, d, a1 = 0, b1 = 0;
    rxnp ta, tb;
    if (a0->ndim == 0 || b0->ndim == 0) return ERR("matmul: scalar operands are not allowed");
    a = a0->ndim == 1 ? rxnp_reshape(a0, "1,-1") : rxnp_retain((rxnp *)a0);
    if (a0->ndim == 1) a1 = 1;
    if (b0->ndim == 1) { b = rxnp_reshape(b0, "-1,1"); b1 = 1; } else b = rxnp_retain((rxnp *)b0);
    if (!a || !b) { rxnp_release(a); rxnp_release(b); return NULL; }
    m = a->shape[a->ndim - 2]; k = a->shape[a->ndim - 1]; n = b->shape[b->ndim - 1];
    if (b->shape[b->ndim - 2] != k) { rxnp_release(a); rxnp_release(b); return ERR("matmul: mismatch in core dimension"); }
    ta.ndim = a->ndim - 2; memcpy(ta.shape, a->shape, sizeof(long) * (size_t)ta.ndim);
    tb.ndim = b->ndim - 2; memcpy(tb.shape, b->shape, sizeof(long) * (size_t)tb.ndim);
    if (!bshape(&ta, &tb, bsh, &nbd)) { rxnp_release(a); rxnp_release(b); return ERR("matmul: batch dimensions could not be broadcast"); }
    nd = 0; for (d = 0; d < nbd; d++) sh[nd++] = bsh[d];
    if (!a1) sh[nd++] = m;
    if (!b1) sh[nd++] = n;
    r = rxnp_new(ints ? RXNP_I64 : RXNP_F64, nd, sh);
    fa = rxnp_astype(a, RXNP_F64); fb = rxnp_astype(b, RXNP_F64);
    if (!r || !fa || !fb) { rxnp_release(a); rxnp_release(b); rxnp_release(r); rxnp_release(fa); rxnp_release(fb); return NULL; }
    nb = 1; for (d = 0; d < nbd; d++) nb *= bsh[d];
    (void)ba; (void)bb;
    for (q = 0; q < nb; q++) {
        long oa = boff(&ta, nbd, bsh, q), ob = boff(&tb, nbd, bsh, q);
        double *z = (double *)malloc(sizeof(double) * (size_t)(m * n + 1)); long t;
        if (!z) { rxnp_release(a); rxnp_release(b); rxnp_release(r); rxnp_release(fa); rxnp_release(fb); return ERR("out of memory"); }
        matmul2(fa->f + oa * m * k, fb->f + ob * k * n, m, k, n, z);
        for (t = 0; t < m * n; t++) { if (ints) r->i[q * m * n + t] = (long)z[t]; else r->f[q * m * n + t] = z[t]; }
        free(z);
    }
    rxnp_release(a); rxnp_release(b); rxnp_release(fa); rxnp_release(fb);
    return r;
}

rxnp *rxnp_tensordot(const rxnp *a, const rxnp *b, int naxes)
{
    long ka = 1, kb = 1, ma = 1, nb = 1, sh[RXNP_MAXDIM]; int d, nd = 0; rxnp *x, *y, *z, *r; char buf[64];
    if (naxes < 0 || naxes > a->ndim || naxes > b->ndim) return ERR("tensordot: bad number of axes");
    for (d = 0; d < naxes; d++) {
        if (a->shape[a->ndim - naxes + d] != b->shape[d]) return ERR("shape-mismatch for sum");
        ka *= b->shape[d];
    }
    kb = ka;
    for (d = 0; d < a->ndim - naxes; d++) { ma *= a->shape[d]; sh[nd++] = a->shape[d]; }
    for (d = naxes; d < b->ndim; d++) { nb *= b->shape[d]; sh[nd++] = b->shape[d]; }
    sprintf(buf, "%ld,%ld", ma, ka); x = rxnp_reshape(a, buf);
    sprintf(buf, "%ld,%ld", kb, nb); y = rxnp_reshape(b, buf);
    if (!x || !y) { rxnp_release(x); rxnp_release(y); return NULL; }
    z = rxnp_matmul(x, y); rxnp_release(x); rxnp_release(y);
    if (!z) return NULL;
    if (nd == 0) { r = rxnp_new(z->dt, 0, sh); if (r) { if (z->dt == RXNP_F64) r->f[0] = z->f[0]; else r->i[0] = z->i[0]; } }
    else { char sb[200]; int n = 0; sb[0] = 0; for (d = 0; d < nd; d++) n += sprintf(sb + n, "%s%ld", d ? "," : "", sh[d]); r = rxnp_reshape(z, sb); }
    rxnp_release(z);
    return r;
}
rxnp *rxnp_dot_nd(const rxnp *a, const rxnp *b)
{
    /* sum over last axis of a and second-to-last of b (1-d: its only axis) */
    long ka, kb, ma = 1, nb = 1, mid = 1, sh[RXNP_MAXDIM], i, j, p, o; int d, nd = 0, bax; rxnp *r; int ints;
    if (a->ndim == 0 || b->ndim == 0) return rxnp_binary(RXNP_MUL, a, b);
    bax = b->ndim >= 2 ? b->ndim - 2 : 0;
    ka = a->shape[a->ndim - 1]; kb = b->shape[bax];
    if (ka != kb) return ERR("shapes not aligned");
    for (d = 0; d < a->ndim - 1; d++) { ma *= a->shape[d]; sh[nd++] = a->shape[d]; }
    for (d = 0; d < b->ndim; d++) if (d != bax) { sh[nd++] = b->shape[d]; if (d < bax) mid *= b->shape[d]; else nb *= b->shape[d]; }
    ints = a->dt == RXNP_I64 && b->dt == RXNP_I64;
    r = rxnp_new(ints ? RXNP_I64 : RXNP_F64, nd, sh);
    if (!r) return NULL;
    for (i = 0; i < ma; i++) for (o = 0; o < mid; o++) for (j = 0; j < nb; j++) {
        double s = 0.0; long si = 0;
        for (p = 0; p < ka; p++) {
            double x = getd(a, i * ka + p), y = getd(b, (o * kb + p) * nb + j);
            if (ints) si += a->i[i * ka + p] * b->i[(o * kb + p) * nb + j]; else s += x * y;
        }
        if (ints) r->i[(i * mid + o) * nb + j] = si; else r->f[(i * mid + o) * nb + j] = s;
    }
    return r;
}
