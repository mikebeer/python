/* rxnp - standalone C89 numerical core. See rxnp.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <float.h>
#include "rxnp.h"

static double nanv(void) { volatile double z = 0.0; return z / z; }
static double infv(void) { volatile double z = 0.0; return 1.0 / z; }
#undef NAN
#undef INFINITY
#define NAN nanv()
#define INFINITY infv()
static char rxnp_errbuf[160];
static int fail(const char *m) { strncpy(rxnp_errbuf, m, sizeof rxnp_errbuf - 1); return 0; }
static void *failp(const char *m) { fail(m); return NULL; }
const char *rxnp_error(void) { return rxnp_errbuf; }

/* ------------------------------------------------------------------ lifetime */
static long prod_shape(int nd, const long *sh) { long s = 1; int k; for (k = 0; k < nd; k++) s *= sh[k]; return s; }

rxnp *rxnp_new(int dt, int ndim, const long *shape)
{
    rxnp *a; int k;
    if (ndim < 0 || ndim > RXNP_MAXDIM) return (rxnp *)failp("too many dimensions");
    for (k = 0; k < ndim; k++) if (shape[k] < 0) return (rxnp *)failp("negative dimension");
    a = (rxnp *)calloc(1, sizeof *a);
    if (!a) return (rxnp *)failp("out of memory");
    a->dt = dt; a->ndim = ndim;
    for (k = 0; k < ndim; k++) a->shape[k] = shape[k];
    a->size = prod_shape(ndim, shape);
    if (dt == RXNP_F64) a->f = (double *)calloc(a->size ? (size_t)a->size : 1, sizeof(double));
    else a->i = (long *)calloc(a->size ? (size_t)a->size : 1, sizeof(long));
    if (!a->f && !a->i) { free(a); return (rxnp *)failp("out of memory"); }
    a->refs = 1;
    return a;
}
rxnp *rxnp_retain(rxnp *a) { if (a) a->refs++; return a; }
void rxnp_release(rxnp *a)
{
    if (!a) return;
    if (--a->refs > 0) return;
    free(a->f); free(a->i); free(a);
}
rxnp *rxnp_copy(const rxnp *a)
{
    rxnp *r = rxnp_new(a->dt, a->ndim, a->shape);
    if (!r) return NULL;
    if (a->dt == RXNP_F64) memcpy(r->f, a->f, (size_t)a->size * sizeof(double));
    else memcpy(r->i, a->i, (size_t)a->size * sizeof(long));
    return r;
}
static double elem(const rxnp *a, long k) { return a->dt == RXNP_F64 ? a->f[k] : (double)a->i[k]; }
rxnp *rxnp_astype(const rxnp *a, int dt)
{
    long k; rxnp *r;
    if (a->dt == dt) return rxnp_copy(a);
    r = rxnp_new(dt, a->ndim, a->shape);
    if (!r) return NULL;
    for (k = 0; k < a->size; k++) {
        if (dt == RXNP_F64) r->f[k] = (double)a->i[k];
        else r->i[k] = (long)a->f[k];
    }
    return r;
}
/* float64 view: returns a->f or a malloc'd conversion (caller frees if *own) */
static const double *as_f(const rxnp *a, double **own)
{
    long k; double *t;
    *own = NULL;
    if (a->dt == RXNP_F64) return a->f;
    t = (double *)malloc((a->size ? (size_t)a->size : 1) * sizeof(double));
    if (!t) return NULL;
    for (k = 0; k < a->size; k++) t[k] = (double)a->i[k];
    *own = t;
    return t;
}
static rxnp *new_f(int nd, const long *sh) { return rxnp_new(RXNP_F64, nd, sh); }

/* ------------------------------------------------------------------ shapes/parse */
int rxnp_parse_shape(const char *s, long *shape, int *ndim)
{
    int n = 0;
    const char *p = s;
    while (*p) {
        if (isdigit((unsigned char)*p) || (*p == '-' && isdigit((unsigned char)p[1]))) {
            char *e;
            long v = strtol(p, &e, 10);
            if (n >= RXNP_MAXDIM) return fail("too many dimensions");
            shape[n++] = v; p = e;
        } else p++;
    }
    *ndim = n;
    return 1;
}

typedef struct { double *v; long n, cap; } dvec;
static int dv_push(dvec *d, double x)
{
    if (d->n == d->cap) {
        long nc = d->cap ? d->cap * 2 : 64;
        double *t = (double *)realloc(d->v, (size_t)nc * sizeof(double));
        if (!t) return 0;
        d->v = t; d->cap = nc;
    }
    d->v[d->n++] = x;
    return 1;
}

rxnp *rxnp_from_text(const char *text)
{
    /* nested-list literal: [[1, 2.5], [3, 4]] ; scalars allowed */
    const char *p = text;
    dvec d; long shape[RXNP_MAXDIM]; long cnt[RXNP_MAXDIM]; int depth = 0, maxd = 0, k;
    int is_int = 1, have_shape[RXNP_MAXDIM], any = 0;
    rxnp *r;
    d.v = NULL; d.n = d.cap = 0;
    for (k = 0; k < RXNP_MAXDIM; k++) { have_shape[k] = 0; shape[k] = 0; cnt[k] = 0; }
    while (*p) {
        char c = *p;
        if (c == '[' || c == '(') {
            if (depth >= RXNP_MAXDIM) { free(d.v); return (rxnp *)failp("nesting too deep"); }
            depth++; if (depth > maxd) maxd = depth;
            cnt[depth - 1] = 0; p++;
        } else if (c == ']' || c == ')') {
            long n = cnt[depth - 1];
            if (depth <= 0) { free(d.v); return (rxnp *)failp("unbalanced brackets"); }
            if (have_shape[depth - 1] && shape[depth - 1] != n) { free(d.v); return (rxnp *)failp("ragged nested sequence"); }
            shape[depth - 1] = n; have_shape[depth - 1] = 1;
            depth--;
            if (depth > 0) cnt[depth - 1]++;
            p++;
        } else if (c == ',' || isspace((unsigned char)c)) p++;
        else {
            char *e; double v; const char *q;
            if (!strncmp(p, "nan", 3)) { v = NAN; e = (char *)p + 3; is_int = 0; }
            else if (!strncmp(p, "inf", 3)) { v = INFINITY; e = (char *)p + 3; is_int = 0; }
            else if (!strncmp(p, "-inf", 4)) { v = -INFINITY; e = (char *)p + 4; is_int = 0; }
            else {
                v = strtod(p, &e);
                if (e == p) { free(d.v); return (rxnp *)failp("cannot parse number"); }
                for (q = p; q < e; q++) if (*q == '.' || *q == 'e' || *q == 'E') is_int = 0;
            }
            if (!dv_push(&d, v)) { free(d.v); return (rxnp *)failp("out of memory"); }
            if (depth > 0) cnt[depth - 1]++;
            any = 1; p = e;
        }
    }
    if (depth != 0) { free(d.v); return (rxnp *)failp("unbalanced brackets"); }
    if (!any && maxd == 0) { free(d.v); return (rxnp *)failp("empty input"); }
    if (prod_shape(maxd, shape) != d.n) { free(d.v); return (rxnp *)failp("ragged nested sequence"); }
    r = rxnp_new(is_int ? RXNP_I64 : RXNP_F64, maxd, shape);
    if (r) for (k = 0; k < d.n; k++) { if (is_int) r->i[k] = (long)d.v[k]; else r->f[k] = d.v[k]; }
    free(d.v);
    return r;
}

static int fmt_double(char *buf, double v)
{
    int prec, n;
    if (v != v) return sprintf(buf, "nan");
    if (v == INFINITY) return sprintf(buf, "inf");
    if (v == -INFINITY) return sprintf(buf, "-inf");
    for (prec = 15; prec <= 17; prec++) {
        sprintf(buf, "%.*g", prec, v);
        if (strtod(buf, NULL) == v) break;
    }
    n = (int)strlen(buf);
    if (!strpbrk(buf, ".eEn")) { strcpy(buf + n, ".0"); n += 2; }
    return n;
}
typedef struct { char *s; size_t n, cap; } sbuf;
static int sb_add(sbuf *b, const char *t, size_t l)
{
    if (b->n + l + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256; char *x;
        while (nc < b->n + l + 1) nc *= 2;
        x = (char *)realloc(b->s, nc); if (!x) return 0;
        b->s = x; b->cap = nc;
    }
    memcpy(b->s + b->n, t, l); b->n += l; b->s[b->n] = 0;
    return 1;
}
static int to_text_rec(const rxnp *a, int dim, long *off, sbuf *b)
{
    long k; char tmp[64]; int n;
    if (dim == a->ndim) {
        if (a->dt == RXNP_F64) n = fmt_double(tmp, a->f[*off]); else n = sprintf(tmp, "%ld", a->i[*off]);
        (*off)++;
        return sb_add(b, tmp, (size_t)n);
    }
    if (!sb_add(b, "[", 1)) return 0;
    for (k = 0; k < a->shape[dim]; k++) {
        if (k && !sb_add(b, ", ", 2)) return 0;
        if (!to_text_rec(a, dim + 1, off, b)) return 0;
    }
    return sb_add(b, "]", 1);
}
char *rxnp_to_text(const rxnp *a)
{
    sbuf b; long off = 0;
    b.s = NULL; b.n = b.cap = 0;
    if (!sb_add(&b, "", 0) || !to_text_rec(a, 0, &off, &b)) { free(b.s); return NULL; }
    return b.s;
}

rxnp *rxnp_full(const char *shape, double v, int dt)
{
    long sh[RXNP_MAXDIM]; int nd; long k; rxnp *r;
    if (!rxnp_parse_shape(shape, sh, &nd)) return NULL;
    r = rxnp_new(dt, nd, sh);
    if (!r) return NULL;
    for (k = 0; k < r->size; k++) { if (dt == RXNP_F64) r->f[k] = v; else r->i[k] = (long)v; }
    return r;
}
rxnp *rxnp_scalar(double v) { rxnp *r = rxnp_new(RXNP_F64, 0, NULL); if (r) r->f[0] = v; return r; }
rxnp *rxnp_arange(double start, double stop, double step, int as_int)
{
    long n, k; long sh[1]; rxnp *r;
    if (step == 0.0) return (rxnp *)failp("step must not be zero");
    n = (long)ceil((stop - start) / step);
    if (n < 0) n = 0;
    sh[0] = n;
    r = rxnp_new(as_int ? RXNP_I64 : RXNP_F64, 1, sh);
    if (!r) return NULL;
    for (k = 0; k < n; k++) { if (as_int) r->i[k] = (long)(start + k * step); else r->f[k] = start + k * step; }
    return r;
}
rxnp *rxnp_linspace(double start, double stop, long num)
{
    long k; long sh[1]; rxnp *r; double step;
    sh[0] = num < 0 ? 0 : num;
    r = new_f(1, sh);
    if (!r) return NULL;
    if (num == 1) { r->f[0] = start; return r; }
    step = (stop - start) / (double)(num - 1);
    for (k = 0; k < num; k++) r->f[k] = start + (double)k * step;
    if (num > 1) r->f[num - 1] = stop;
    return r;
}
rxnp *rxnp_eye(long n)
{
    long sh[2]; long k; rxnp *r;
    sh[0] = sh[1] = n; r = new_f(2, sh);
    if (r) for (k = 0; k < n; k++) r->f[k * n + k] = 1.0;
    return r;
}

/* ------------------------------------------------------------------ shape ops */
rxnp *rxnp_reshape(const rxnp *a, const char *shape)
{
    long sh[RXNP_MAXDIM]; int nd, k, neg = -1; long known = 1; rxnp *r;
    if (!rxnp_parse_shape(shape, sh, &nd)) return NULL;
    for (k = 0; k < nd; k++) {
        if (sh[k] == -1) { if (neg >= 0) return (rxnp *)failp("only one dimension can be -1"); neg = k; }
        else known *= sh[k];
    }
    if (neg >= 0) {
        if (known == 0 || a->size % known) return (rxnp *)failp("cannot reshape array");
        sh[neg] = a->size / known; known *= sh[neg];
    }
    if (known != a->size) return (rxnp *)failp("cannot reshape array: size mismatch");
    r = rxnp_copy(a);
    if (!r) return NULL;
    r->ndim = nd; for (k = 0; k < nd; k++) r->shape[k] = sh[k];
    return r;
}
rxnp *rxnp_flatten(const rxnp *a)
{
    rxnp *r = rxnp_copy(a);
    if (r) { r->ndim = 1; r->shape[0] = a->size; }
    return r;
}
rxnp *rxnp_transpose(const rxnp *a)
{
    long sh[RXNP_MAXDIM], st[RXNP_MAXDIM], idx[RXNP_MAXDIM]; int k, nd = a->ndim; long out, pos;
    rxnp *r;
    if (nd < 2) return rxnp_copy(a);
    for (k = 0; k < nd; k++) sh[k] = a->shape[nd - 1 - k];
    st[nd - 1] = 1; for (k = nd - 2; k >= 0; k--) st[k] = st[k + 1] * a->shape[k + 1];
    r = rxnp_new(a->dt, nd, sh);
    if (!r) return NULL;
    for (k = 0; k < nd; k++) idx[k] = 0;
    pos = 0;
    for (out = 0; out < r->size; out++) {
        /* output index idx over shape sh; source stride for out-dim k is st[nd-1-k] */
        if (a->dt == RXNP_F64) r->f[out] = a->f[pos]; else r->i[out] = a->i[pos];
        for (k = nd - 1; k >= 0; k--) {
            idx[k]++; pos += st[nd - 1 - k];
            if (idx[k] < sh[k]) break;
            pos -= st[nd - 1 - k] * sh[k]; idx[k] = 0;
        }
    }
    return r;
}
rxnp *rxnp_slice(const rxnp *a, int axis, long start, long stop, long step)
{
    long outer = 1, inner = 1, n, m, k, o, c; int d; long sh[RXNP_MAXDIM]; rxnp *r;
    if (a->ndim == 0) return (rxnp *)failp("cannot slice a 0-d array");
    if (axis < 0) axis += a->ndim;
    if (axis < 0 || axis >= a->ndim) return (rxnp *)failp("axis out of range");
    if (step == 0) return (rxnp *)failp("slice step cannot be zero");
    n = a->shape[axis];
    /* python slice normalisation */
    if (step > 0) {
        if (start < 0) { start += n; if (start < 0) start = 0; } else if (start > n) start = n;
        if (stop < 0) { stop += n; if (stop < 0) stop = 0; } else if (stop > n) stop = n;
        m = stop > start ? (stop - start + step - 1) / step : 0;
    } else {
        if (start < 0) { start += n; if (start < 0) start = -1; } else if (start >= n) start = n - 1;
        if (stop < 0) { stop += n; if (stop < 0) stop = -1; } else if (stop >= n) stop = n - 1;
        m = start > stop ? (start - stop - step - 1) / (-step) : 0;
    }
    for (d = 0; d < a->ndim; d++) sh[d] = a->shape[d];
    sh[axis] = m;
    for (d = 0; d < axis; d++) outer *= a->shape[d];
    for (d = axis + 1; d < a->ndim; d++) inner *= a->shape[d];
    r = rxnp_new(a->dt, a->ndim, sh);
    if (!r) return NULL;
    for (o = 0; o < outer; o++)
        for (k = 0; k < m; k++)
            for (c = 0; c < inner; c++) {
                long src = (o * n + start + k * step) * inner + c, dst = (o * m + k) * inner + c;
                if (a->dt == RXNP_F64) r->f[dst] = a->f[src]; else r->i[dst] = a->i[src];
            }
    return r;
}
rxnp *rxnp_concat(const rxnp *a, const rxnp *b, int axis)
{
    long sh[RXNP_MAXDIM], outer = 1, inner = 1, o; int d, dt; rxnp *r, *aa, *bb;
    if (a->ndim != b->ndim || a->ndim == 0) return (rxnp *)failp("concatenate: dimension mismatch");
    if (axis < 0) axis += a->ndim;
    if (axis < 0 || axis >= a->ndim) return (rxnp *)failp("axis out of range");
    for (d = 0; d < a->ndim; d++) if (d != axis && a->shape[d] != b->shape[d]) return (rxnp *)failp("concatenate: shape mismatch");
    dt = (a->dt == RXNP_I64 && b->dt == RXNP_I64) ? RXNP_I64 : RXNP_F64;
    aa = rxnp_astype(a, dt); bb = rxnp_astype(b, dt);
    if (!aa || !bb) { rxnp_release(aa); rxnp_release(bb); return NULL; }
    for (d = 0; d < a->ndim; d++) sh[d] = a->shape[d];
    sh[axis] = a->shape[axis] + b->shape[axis];
    for (d = 0; d < axis; d++) outer *= a->shape[d];
    for (d = axis + 1; d < a->ndim; d++) inner *= a->shape[d];
    r = rxnp_new(dt, a->ndim, sh);
    if (r) for (o = 0; o < outer; o++) {
        long la = a->shape[axis] * inner, lb = b->shape[axis] * inner;
        if (dt == RXNP_F64) {
            memcpy(r->f + o * (la + lb), aa->f + o * la, (size_t)la * sizeof(double));
            memcpy(r->f + o * (la + lb) + la, bb->f + o * lb, (size_t)lb * sizeof(double));
        } else {
            memcpy(r->i + o * (la + lb), aa->i + o * la, (size_t)la * sizeof(long));
            memcpy(r->i + o * (la + lb) + la, bb->i + o * lb, (size_t)lb * sizeof(long));
        }
    }
    rxnp_release(aa); rxnp_release(bb);
    return r;
}
int rxnp_getf(const rxnp *a, long idx, double *out)
{
    if (idx < 0) idx += a->size;
    if (idx < 0 || idx >= a->size) return fail("index out of range");
    *out = elem(a, idx);
    return 1;
}
int rxnp_setf(rxnp *a, long idx, double v)
{
    if (idx < 0) idx += a->size;
    if (idx < 0 || idx >= a->size) return fail("index out of range");
    if (a->dt == RXNP_F64) a->f[idx] = v; else a->i[idx] = (long)v;
    return 1;
}

/* ------------------------------------------------------------------ elementwise */
static double pymod(double a, double b)
{
    double m = fmod(a, b);
    if (m != 0.0 && ((b < 0) != (m < 0))) m += b;
    return m;
}
static long lpymod(long a, long b) { long m = a % b; if (m != 0 && ((b < 0) != (m < 0))) m += b; return m; }
static long lfloordiv(long a, long b) { long q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
static long lpow(long a, long b) { long r = 1; while (b > 0) { if (b & 1) r *= a; a *= a; b >>= 1; } return r; }

static double fop(int op, double x, double y)
{
    switch (op) {
    case RXNP_ADD: return x + y;
    case RXNP_SUB: return x - y;
    case RXNP_MUL: return x * y;
    case RXNP_DIV: return x / y;
    case RXNP_POW: return pow(x, y);
    case RXNP_MOD: return y == 0.0 ? NAN : pymod(x, y);
    case RXNP_FLOORDIV: return floor(x / y);
    case RXNP_MAXIMUM: return (x != x || x > y) ? x : y;
    case RXNP_MINIMUM: return (x != x || x < y) ? x : y;
    case RXNP_ATAN2: return atan2(x, y);
    }
    return 0.0;
}
rxnp *rxnp_binary(int op, const rxnp *a, const rxnp *b)
{
    int nd = a->ndim > b->ndim ? a->ndim : b->ndim, k;
    long sh[RXNP_MAXDIM], sa[RXNP_MAXDIM], sb[RXNP_MAXDIM], idx[RXNP_MAXDIM];
    long ta, tb, pa, pb, o; int ints; rxnp *r; long x;
    ints = a->dt == RXNP_I64 && b->dt == RXNP_I64 && op != RXNP_DIV && op != RXNP_ATAN2;
    ta = 1; tb = 1;
    for (k = nd - 1; k >= 0; k--) {
        int ka = k - (nd - a->ndim), kb = k - (nd - b->ndim);
        long da = ka >= 0 ? a->shape[ka] : 1, db = kb >= 0 ? b->shape[kb] : 1;
        if (da != db && da != 1 && db != 1) return (rxnp *)failp("operands could not be broadcast together");
        sh[k] = da == 1 ? db : da;
        sa[k] = da == 1 ? 0 : ta; sb[k] = db == 1 ? 0 : tb;
        ta *= da; tb *= db;
    }
    r = rxnp_new(ints ? RXNP_I64 : RXNP_F64, nd, sh);
    if (!r) return NULL;
    /* fast paths: same shape contiguous */
    if (a->ndim == b->ndim && a->size == b->size && a->size == r->size) {
        int same = 1; for (k = 0; k < nd; k++) if (a->shape[k] != b->shape[k]) same = 0;
        if (same) {
            if (!ints && a->dt == RXNP_F64 && b->dt == RXNP_F64 && op <= RXNP_DIV) {
                const double *p = a->f, *q = b->f; double *z = r->f;
                switch (op) {
                case RXNP_ADD: for (x = 0; x < r->size; x++) z[x] = p[x] + q[x]; break;
                case RXNP_SUB: for (x = 0; x < r->size; x++) z[x] = p[x] - q[x]; break;
                case RXNP_MUL: for (x = 0; x < r->size; x++) z[x] = p[x] * q[x]; break;
                default: for (x = 0; x < r->size; x++) z[x] = p[x] / q[x]; break;
                }
                return r;
            }
        }
    }
    for (k = 0; k < nd; k++) idx[k] = 0;
    pa = pb = 0;
    for (o = 0; o < r->size; o++) {
        if (ints) {
            long x1 = a->i[pa], y1 = b->i[pb], z;
            switch (op) {
            case RXNP_ADD: z = x1 + y1; break;
            case RXNP_SUB: z = x1 - y1; break;
            case RXNP_MUL: z = x1 * y1; break;
            case RXNP_POW: if (y1 < 0) { rxnp_release(r); return (rxnp *)failp("integers to negative integer powers are not allowed"); } z = lpow(x1, y1); break;
            case RXNP_MOD: z = y1 == 0 ? 0 : lpymod(x1, y1); break;
            case RXNP_FLOORDIV: z = y1 == 0 ? 0 : lfloordiv(x1, y1); break;
            case RXNP_MAXIMUM: z = x1 > y1 ? x1 : y1; break;
            default: z = x1 < y1 ? x1 : y1; break;
            }
            r->i[o] = z;
        } else r->f[o] = fop(op, elem(a, pa), elem(b, pb));
        for (k = nd - 1; k >= 0; k--) {
            idx[k]++; pa += sa[k]; pb += sb[k];
            if (idx[k] < sh[k]) break;
            pa -= sa[k] * sh[k]; pb -= sb[k] * sh[k]; idx[k] = 0;
        }
    }
    return r;
}
rxnp *rxnp_unary(int op, const rxnp *a)
{
    rxnp *r; long k;
    if (a->dt == RXNP_I64 && (op == RXNP_NEG || op == RXNP_ABS || op == RXNP_SQUARE || op == RXNP_SIGN ||
                              op == RXNP_FLOOR || op == RXNP_CEIL || op == RXNP_ROUND)) {
        r = rxnp_new(RXNP_I64, a->ndim, a->shape);
        if (!r) return NULL;
        for (k = 0; k < a->size; k++) {
            long v = a->i[k];
            switch (op) {
            case RXNP_NEG: v = -v; break;
            case RXNP_ABS: v = v < 0 ? -v : v; break;
            case RXNP_SQUARE: v = v * v; break;
            case RXNP_SIGN: v = (v > 0) - (v < 0); break;
            default: break;
            }
            r->i[k] = v;
        }
        return r;
    }
    r = new_f(a->ndim, a->shape);
    if (!r) return NULL;
    for (k = 0; k < a->size; k++) {
        double v = elem(a, k);
        switch (op) {
        case RXNP_NEG: v = -v; break;
        case RXNP_ABS: v = fabs(v); break;
        case RXNP_SQRT: v = sqrt(v); break;
        case RXNP_EXP: v = exp(v); break;
        case RXNP_LOG: v = log(v); break;
        case RXNP_LOG10: v = log10(v); break;
        case RXNP_LOG2: v = log(v) / 0.69314718055994530942; break;
        case RXNP_SIN: v = sin(v); break;
        case RXNP_COS: v = cos(v); break;
        case RXNP_TAN: v = tan(v); break;
        case RXNP_TANH: v = tanh(v); break;
        case RXNP_ARCTAN: v = atan(v); break;
        case RXNP_FLOOR: v = floor(v); break;
        case RXNP_CEIL: v = ceil(v); break;
        case RXNP_SQUARE: v = v * v; break;
        case RXNP_SIGN: v = (v > 0) - (v < 0); break;
        case RXNP_ROUND: { double rr = floor(v + 0.5); if (rr - v == 0.5 && fmod(rr, 2.0) != 0.0) rr -= 1.0; v = rr; } break;
        }
        r->f[k] = v;
    }
    return r;
}
rxnp *rxnp_clip(const rxnp *a, double lo, double hi)
{
    rxnp *r; long k;
    r = rxnp_new(a->dt, a->ndim, a->shape);
    if (!r) return NULL;
    for (k = 0; k < a->size; k++) {
        double v = elem(a, k);
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        if (a->dt == RXNP_F64) r->f[k] = v; else r->i[k] = (long)v;
    }
    return r;
}

/* ------------------------------------------------------------------ reductions */
/* numpy's pairwise summation (contiguous or strided) */
double rxnp_pairwise_sum(const double *a, long n, long s)
{
    if (n < 8) {
        long i; double res = 0.0;
        for (i = 0; i < n; i++) res += a[i * s];
        /* numpy starts from -0.0 style identity; plain sum is equivalent for finite data */
        return res;
    } else if (n <= 128) {
        double r[8], res; long i;
        for (i = 0; i < 8; i++) r[i] = a[i * s];
        for (i = 8; i < n - (n % 8); i += 8) {
            r[0] += a[(i + 0) * s]; r[1] += a[(i + 1) * s]; r[2] += a[(i + 2) * s]; r[3] += a[(i + 3) * s];
            r[4] += a[(i + 4) * s]; r[5] += a[(i + 5) * s]; r[6] += a[(i + 6) * s]; r[7] += a[(i + 7) * s];
        }
        res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; i++) res += a[i * s];
        return res;
    } else {
        long n2 = n / 2;
        n2 -= n2 % 8;
        return rxnp_pairwise_sum(a, n2, s) + rxnp_pairwise_sum(a + n2 * s, n - n2, s);
    }
}
static int cmpd(const void *x, const void *y)
{
    double a = *(const double *)x, b = *(const double *)y;
    if (a != a) return b != b ? 0 : 1;
    if (b != b) return -1;
    return (a > b) - (a < b);
}
rxnp *rxnp_reduce(int op, const rxnp *a, int axis, int all, long ddof)
{
    long outer = 1, inner = 1, n, o, c, k, sh[RXNP_MAXDIM]; int d, nd = 0; rxnp *r;
    double *own = NULL; const double *x; double *tmp = NULL; long iv = 0;
    int cum = (op == RXNP_CUMSUM || op == RXNP_CUMPROD);
    int argop = (op == RXNP_ARGMIN || op == RXNP_ARGMAX);
    int intres = (a->dt == RXNP_I64 && (op == RXNP_SUM || op == RXNP_PROD || op == RXNP_MIN || op == RXNP_MAX ||
                  cum)) || argop;
    (void)iv;
    if (all) { n = a->size; outer = 1; inner = 1; }
    else {
        if (a->ndim == 0) return (rxnp *)failp("axis out of range");
        if (axis < 0) axis += a->ndim;
        if (axis < 0 || axis >= a->ndim) return (rxnp *)failp("axis out of range");
        n = a->shape[axis];
        for (d = 0; d < axis; d++) outer *= a->shape[d];
        for (d = axis + 1; d < a->ndim; d++) inner *= a->shape[d];
    }
    if (cum) {
        if (all) { sh[0] = a->size; nd = 1; }
        else { for (d = 0; d < a->ndim; d++) sh[d] = a->shape[d]; nd = a->ndim; }
    } else if (!all) {
        for (d = 0; d < a->ndim; d++) if (d != axis) sh[nd++] = a->shape[d];
    }
    if (n == 0 && (op == RXNP_MIN || op == RXNP_MAX || argop || op == RXNP_MEDIAN)) return (rxnp *)failp("zero-size array reduction");
    r = rxnp_new(intres ? RXNP_I64 : RXNP_F64, nd, sh);
    if (!r) return NULL;
    x = as_f(a, &own);
    if (!x) { rxnp_release(r); return (rxnp *)failp("out of memory"); }
    if (op == RXNP_MEDIAN) tmp = (double *)malloc((size_t)(n ? n : 1) * sizeof(double));
    for (o = 0; o < outer; o++)
        for (c = 0; c < inner; c++) {
            const double *p = x + o * n * inner + c; long s = inner; long oi = o * inner + c;
            double res = 0.0, m; long am;
            if (a->dt == RXNP_I64 && intres && !cum && !argop) {
                const long *q = a->i + o * n * inner + c; long ir = (op == RXNP_PROD) ? 1 : 0;
                if (op == RXNP_MIN || op == RXNP_MAX) ir = q[0];
                for (k = 0; k < n; k++) {
                    long v = q[k * s];
                    switch (op) {
                    case RXNP_SUM: ir += v; break;
                    case RXNP_PROD: ir *= v; break;
                    case RXNP_MIN: if (v < ir) ir = v; break;
                    default: if (v > ir) ir = v; break;
                    }
                }
                r->i[oi] = ir; continue;
            }
            switch (op) {
            case RXNP_SUM: res = rxnp_pairwise_sum(p, n, s); r->f[oi] = res; break;
            case RXNP_MEAN: r->f[oi] = rxnp_pairwise_sum(p, n, s) / (double)n; break;
            case RXNP_PROD: res = 1.0; for (k = 0; k < n; k++) res *= p[k * s]; r->f[oi] = res; break;
            case RXNP_MIN: case RXNP_MAX:
                m = p[0];
                for (k = 1; k < n; k++) {
                    double v = p[k * s];
                    if (m != m) break;
                    if (v != v || (op == RXNP_MIN ? v < m : v > m)) m = v;
                }
                r->f[oi] = m; break;
            case RXNP_ARGMIN: case RXNP_ARGMAX:
                m = p[0]; am = 0;
                for (k = 1; k < n; k++) {
                    double v = p[k * s];
                    if (m != m) break;
                    if (v != v || (op == RXNP_ARGMIN ? v < m : v > m)) { m = v; am = k; }
                }
                r->i[oi] = am; break;
            case RXNP_STD: case RXNP_VAR: {
                double mean = rxnp_pairwise_sum(p, n, s) / (double)n, acc = 0.0, dd;
                /* numpy: sum((x-mean)^2) via pairwise on the squared deviations */
                double *dev = (double *)malloc((size_t)(n ? n : 1) * sizeof(double));
                if (!dev) { free(tmp); free(own); rxnp_release(r); return (rxnp *)failp("out of memory"); }
                for (k = 0; k < n; k++) { dd = p[k * s] - mean; dev[k] = dd * dd; }
                acc = rxnp_pairwise_sum(dev, n, 1); free(dev);
                dd = (double)(n - ddof); if (dd < 0) dd = 0;
                acc = acc / dd;
                r->f[oi] = op == RXNP_STD ? sqrt(acc) : acc; break; }
            case RXNP_MEDIAN:
                for (k = 0; k < n; k++) tmp[k] = p[k * s];
                qsort(tmp, (size_t)n, sizeof(double), cmpd);
                r->f[oi] = (n % 2) ? tmp[n / 2] : (tmp[n / 2 - 1] + tmp[n / 2]) / 2.0; break;
            case RXNP_CUMSUM: case RXNP_CUMPROD:
                if (a->dt == RXNP_I64) {
                    const long *q = a->i + o * n * inner + c; long ir = (op == RXNP_CUMSUM) ? 0 : 1;
                    for (k = 0; k < n; k++) { ir = op == RXNP_CUMSUM ? ir + q[k * s] : ir * q[k * s]; r->i[(o * n + k) * inner + c] = ir; }
                } else {
                    res = (op == RXNP_CUMSUM) ? 0.0 : 1.0;
                    for (k = 0; k < n; k++) { res = op == RXNP_CUMSUM ? res + p[k * s] : res * p[k * s]; r->f[(o * n + k) * inner + c] = res; }
                }
                break;
            }
        }
    free(tmp); free(own);
    return r;
}

/* ------------------------------------------------------------------ sort */
static const double *g_key;
static void msort(long *idx, long *tmp, long lo, long hi, const double *key)
{
    long mid, i, j, k;
    if (hi - lo < 2) return;
    mid = (lo + hi) / 2;
    msort(idx, tmp, lo, mid, key); msort(idx, tmp, mid, hi, key);
    i = lo; j = mid; k = lo;
    while (i < mid && j < hi) {
        if (cmpd(&key[idx[j]], &key[idx[i]]) < 0) tmp[k++] = idx[j++]; else tmp[k++] = idx[i++];
    }
    while (i < mid) tmp[k++] = idx[i++];
    while (j < hi) tmp[k++] = idx[j++];
    for (k = lo; k < hi; k++) idx[k] = tmp[k];
}
static rxnp *sort_impl(const rxnp *a, int arg)
{
    long n, rows, r0, k; rxnp *res; double *own; const double *x; long *idx, *tmp;
    if (a->ndim == 0) return (rxnp *)failp("cannot sort a 0-d array");
    n = a->shape[a->ndim - 1]; rows = n ? a->size / n : 0;
    res = rxnp_new(arg ? RXNP_I64 : a->dt, a->ndim, a->shape);
    if (!res) return NULL;
    x = as_f(a, &own);
    idx = (long *)malloc((size_t)(n ? n : 1) * sizeof(long)); tmp = (long *)malloc((size_t)(n ? n : 1) * sizeof(long));
    if (!x || !idx || !tmp) { free(idx); free(tmp); free(own); rxnp_release(res); return (rxnp *)failp("out of memory"); }
    (void)g_key;
    for (r0 = 0; r0 < rows; r0++) {
        const double *key = x + r0 * n;
        for (k = 0; k < n; k++) idx[k] = k;
        msort(idx, tmp, 0, n, key);
        for (k = 0; k < n; k++) {
            if (arg) res->i[r0 * n + k] = idx[k];
            else if (a->dt == RXNP_F64) res->f[r0 * n + k] = a->f[r0 * n + idx[k]];
            else res->i[r0 * n + k] = a->i[r0 * n + idx[k]];
        }
    }
    free(idx); free(tmp); free(own);
    return res;
}
rxnp *rxnp_sort(const rxnp *a) { return sort_impl(a, 0); }
rxnp *rxnp_argsort(const rxnp *a) { return sort_impl(a, 1); }

/* ------------------------------------------------------------------ products */
rxnp *rxnp_matmul(const rxnp *a, const rxnp *b)
{
    long m, k, n, i, j, p, sh[2]; int nd; rxnp *r; double *oa, *ob; const double *x, *y; double *z;
    int ints = a->dt == RXNP_I64 && b->dt == RXNP_I64;
    if (a->ndim == 0 || b->ndim == 0 || a->ndim > 2 || b->ndim > 2) return (rxnp *)failp("dot: only 1-d and 2-d operands supported");
    if (a->ndim == 1) { m = 1; k = a->shape[0]; } else { m = a->shape[0]; k = a->shape[1]; }
    if (b->ndim == 1) { n = 1; if (b->shape[0] != k) return (rxnp *)failp("shapes not aligned"); }
    else { n = b->shape[1]; if (b->shape[0] != k) return (rxnp *)failp("shapes not aligned"); }
    nd = 0;
    if (a->ndim == 2) sh[nd++] = m;
    if (b->ndim == 2) sh[nd++] = n;
    r = rxnp_new(ints ? RXNP_I64 : RXNP_F64, nd, sh);
    if (!r) return NULL;
    if (ints) {
        for (i = 0; i < m; i++) for (p = 0; p < k; p++) {
            long av = a->i[i * k + p];
            for (j = 0; j < n; j++) r->i[i * n + j] += av * b->i[p * n + j];
        }
        return r;
    }
    x = as_f(a, &oa); y = as_f(b, &ob); z = r->f;
    if (!x || !y) { free(oa); free(ob); rxnp_release(r); return (rxnp *)failp("out of memory"); }
    for (i = 0; i < m; i++)
        for (p = 0; p < k; p++) {
            double av = x[i * k + p]; const double *yr = y + p * n; double *zr = z + i * n;
            for (j = 0; j < n; j++) zr[j] += av * yr[j];
        }
    free(oa); free(ob);
    return r;
}
rxnp *rxnp_outer(const rxnp *a, const rxnp *b)
{
    long sh[2], i, j; rxnp *r;
    sh[0] = a->size; sh[1] = b->size;
    r = rxnp_new(a->dt == RXNP_I64 && b->dt == RXNP_I64 ? RXNP_I64 : RXNP_F64, 2, sh);
    if (r) for (i = 0; i < sh[0]; i++) for (j = 0; j < sh[1]; j++) {
        if (r->dt == RXNP_I64) r->i[i * sh[1] + j] = a->i[i] * b->i[j];
        else r->f[i * sh[1] + j] = elem(a, i) * elem(b, j);
    }
    return r;
}
double rxnp_trace(const rxnp *a)
{
    long i, n; double s = 0.0;
    if (a->ndim != 2) return 0.0;
    n = a->shape[0] < a->shape[1] ? a->shape[0] : a->shape[1];
    for (i = 0; i < n; i++) s += elem(a, i * a->shape[1] + i);
    return s;
}
double rxnp_norm(const rxnp *a)
{
    long k; double s = 0.0, v;
    for (k = 0; k < a->size; k++) { v = elem(a, k); s += v * v; }
    return sqrt(s);
}
