/* =========================================================================
   p7_la.c   numpy.linalg (dense real matrices, double precision)
   ========================================================================= */
static Class *cls_LinAlgError;

/* portable C89 replacements for hypot / log1p / expm1 / asinh / ... */
static double n_hypot(double a, double b)
{
    double t;
    a = fabs(a); b = fabs(b);
    if (a < b) { t = a; a = b; b = t; }
    if (a == 0.0) return 0.0;
    if (a - a != 0.0) return a;
    t = b / a;
    return a * sqrt(1.0 + t * t);
}
static double n_log1p(double x)
{
    double u = 1.0 + x;
    if (u == 1.0) return x;
    return log(u) * x / (u - 1.0);
}
static double n_expm1(double x)
{
    double u = exp(x), um1;
    if (u == 1.0) return x;
    um1 = u - 1.0;
    if (um1 == -1.0) return -1.0;
    if (u - u != 0) return u;
    return um1 * x / log(u);
}
static double n_asinh(double v)
{
    double a = fabs(v), r;
    if (a < 1e-8 || a - a != 0.0) return v;
    r = a > 1e8 ? log(a) + 0.69314718055994530942 : n_log1p(a + a * a / (1.0 + sqrt(1.0 + a * a)));
    return v < 0 ? -r : r;
}
static double n_acosh(double v)
{
    if (v < 1.0) return HUGE_VAL - HUGE_VAL;
    if (v > 1e8) return log(v) + 0.69314718055994530942;
    return n_log1p((v - 1.0) + sqrt((v - 1.0) * (v + 1.0)));
}
static double n_atanh(double v)
{
    if (v > 1.0 || v < -1.0) return HUGE_VAL - HUGE_VAL;
    if (v == 1.0) return HUGE_VAL;
    if (v == -1.0) return -HUGE_VAL;
    return 0.5 * n_log1p(2.0 * v / (1.0 - v));
}
static double n_cbrt(double v)
{
    double a, r;
    if (v == 0 || v - v != 0) return v;
    a = fabs(v);
    r = pow(a, 1.0 / 3.0);
    r = r - (r * r * r - a) / (3.0 * r * r);
    return v < 0 ? -r : r;
}
static double n_trunc(double v)
{
    return v < 0 ? ceil(v) : floor(v);
}

static Value np_arg(Args *x, int pos, const char *nm)
{
    if (pos < x->n) return A(pos);
    return kwget(x, nm);
}
static int np_has(Value v)
{
    return v.t != T_UNSET && v.t != T_NONE;
}
/* fresh float copy of any array-like */
static Nd *np_f(Value v)
{
    NdTmp t;
    Nd *a = nd_coerce(v, &t);
    return nd_astype(a, ND_FLOAT);
}
static Nd *la_mat(Value v, long *m, long *n)
{
    Nd *a = np_f(v);
    if (a->nd < 2)
        throw_new(cls_LinAlgError,
            "%d-dimensional array given. Array must be at least two-dimensional", a->nd);
    if (a->nd > 2) throw_new(cls_NotImplementedError, "linalg: stacked matrices are not supported");
    *m = a->shape[0]; *n = a->shape[1];
    return a;
}
static Nd *la_sq(Value v, long *n)
{
    long m;
    Nd *a = la_mat(v, &m, n);
    if (m != *n) throw_new(cls_LinAlgError, "Last 2 dimensions of the array must be square");
    return a;
}
static Nd *mat_new(long m, long n)
{
    long sh[2];
    sh[0] = m; sh[1] = n;
    return nd_new(ND_FLOAT, 2, sh);
}
static Nd *vec_new(long n)
{
    return nd_new1(ND_FLOAT, n);
}

/* LU with partial pivoting, in place; returns sign (+1/-1) or 0 when singular */
static int lu_dec(double *a, long n, long *piv)
{
    long i, j, k, p;
    int sign = 1, sing = 0;
    for (k = 0; k < n; k++) {
        double mx = fabs(a[k * n + k]);
        p = k;
        for (i = k + 1; i < n; i++)
            if (fabs(a[i * n + k]) > mx) { mx = fabs(a[i * n + k]); p = i; }
        piv[k] = p;
        if (mx == 0.0) { sing = 1; continue; }
        if (p != k) {
            for (j = 0; j < n; j++) {
                double t = a[k * n + j]; a[k * n + j] = a[p * n + j]; a[p * n + j] = t;
            }
            sign = -sign;
        }
        for (i = k + 1; i < n; i++) {
            double f = a[i * n + k] / a[k * n + k];
            a[i * n + k] = f;
            if (f != 0.0) for (j = k + 1; j < n; j++) a[i * n + j] -= f * a[k * n + j];
        }
    }
    return sing ? 0 : sign;
}
/* solve with an LU factorisation: b is n x nrhs (row-major), overwritten */
static void lu_solve(const double *lu, long n, const long *piv, double *b, long nrhs)
{
    long i, j, k;
    for (k = 0; k < n; k++)
        if (piv[k] != k)
            for (j = 0; j < nrhs; j++) {
                double t = b[k * nrhs + j];
                b[k * nrhs + j] = b[piv[k] * nrhs + j];
                b[piv[k] * nrhs + j] = t;
            }
    for (i = 0; i < n; i++)
        for (k = 0; k < i; k++) {
            double f = lu[i * n + k];
            if (f != 0.0) for (j = 0; j < nrhs; j++) b[i * nrhs + j] -= f * b[k * nrhs + j];
        }
    for (i = n - 1; i >= 0; i--) {
        for (k = i + 1; k < n; k++) {
            double f = lu[i * n + k];
            if (f != 0.0) for (j = 0; j < nrhs; j++) b[i * nrhs + j] -= f * b[k * nrhs + j];
        }
        for (j = 0; j < nrhs; j++) b[i * nrhs + j] /= lu[i * n + i];
    }
}
BI(la_det)
{
    long n, i, *piv;
    int sg;
    Nd *a;
    double d;
    need(x, 1, 1, "det");
    a = la_sq(A(0), &n);
    if (n == 0) return mkfloat(1.0);
    piv = (long *)xmalloc(sizeof(long) * (size_t)n);
    sg = lu_dec(a->dv, n, piv);
    free(piv);
    if (sg == 0) return mkfloat(0.0);
    d = (double)sg;
    for (i = 0; i < n; i++) d *= a->dv[i * n + i];
    return mkfloat(d);
}
BI(la_slogdet)
{
    long n, i, *piv;
    int sg;
    Nd *a;
    double ld = 0.0, sgn;
    Value tv[2];
    need(x, 1, 1, "slogdet");
    a = la_sq(A(0), &n);
    piv = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    sg = lu_dec(a->dv, n, piv);
    free(piv);
    if (sg == 0) { tv[0] = mkfloat(0.0); tv[1] = mkfloat(-HUGE_VAL); return mktuple_from(tv, 2); }
    sgn = (double)sg;
    for (i = 0; i < n; i++) {
        double d = a->dv[i * n + i];
        if (d < 0) { sgn = -sgn; d = -d; }
        ld += log(d);
    }
    tv[0] = mkfloat(sgn); tv[1] = mkfloat(ld);
    return mktuple_from(tv, 2);
}
BI(la_inv)
{
    long n, i, *piv;
    Nd *a, *r;
    need(x, 1, 1, "inv");
    a = la_sq(A(0), &n);
    r = mat_new(n, n);
    piv = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    if (n > 0 && lu_dec(a->dv, n, piv) == 0) {
        free(piv);
        throw_new(cls_LinAlgError, "Singular matrix");
    }
    for (i = 0; i < n * n; i++) r->dv[i] = 0.0;
    for (i = 0; i < n; i++) r->dv[i * n + i] = 1.0;
    if (n > 0) lu_solve(a->dv, n, piv, r->dv, n);
    free(piv);
    return ndv(r);
}
BI(la_solve)
{
    long n, i, *piv, nrhs;
    Nd *a, *b, *r;
    need(x, 2, 2, "solve");
    a = la_sq(A(0), &n);
    b = np_f(A(1));
    if (b->nd < 1 || b->nd > 2 || b->shape[0] != n)
        throw_new(cls_ValueError,
            "solve: Input operand 1 has a mismatch in its core dimension 0, with gufunc "
            "signature (m,m),(m,n)->(m,n) (size %ld is different from %ld)",
                  b->nd > 0 ? b->shape[0] : 0L, n);
    nrhs = b->nd == 1 ? 1 : b->shape[1];
    r = nd_astype(b, ND_FLOAT);
    piv = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    if (n > 0 && lu_dec(a->dv, n, piv) == 0) {
        free(piv);
        throw_new(cls_LinAlgError, "Singular matrix");
    }
    if (n > 0) lu_solve(a->dv, n, piv, r->dv, nrhs);
    free(piv);
    (void)i;
    return ndv(r);
}
BI(la_cholesky)
{
    long n, i, j, k;
    Nd *a, *r;
    need(x, 1, 1, "cholesky");
    a = la_sq(A(0), &n);
    r = mat_new(n, n);
    for (i = 0; i < n * n; i++) r->dv[i] = 0.0;
    for (i = 0; i < n; i++)
        for (j = 0; j <= i; j++) {
            double s = a->dv[i * n + j];
            for (k = 0; k < j; k++) s -= r->dv[i * n + k] * r->dv[j * n + k];
            if (i == j) {
                if (s <= 0.0) throw_new(cls_LinAlgError, "Matrix is not positive definite");
                r->dv[i * n + i] = sqrt(s);
            } else r->dv[i * n + j] = s / r->dv[j * n + j];
        }
    return ndv(r);
}

/* symmetric eigenproblem: cyclic Jacobi.  a (n x n, destroyed), w ascending,
   v receives eigenvectors as columns */
static void jacobi_eig(double *a, long n, double *w, double *v)
{
    long i, j, p, q, sweep, k;
    for (i = 0; i < n * n; i++) v[i] = 0.0;
    for (i = 0; i < n; i++) v[i * n + i] = 1.0;
    for (sweep = 0; sweep < 100; sweep++) {
        double off = 0.0, dsum = 0.0;
        for (p = 0; p < n; p++) {
            dsum += a[p * n + p] * a[p * n + p];
            for (q = p + 1; q < n; q++) off += a[p * n + q] * a[p * n + q];
        }
        if (off == 0.0 || off < 1e-32 * dsum) break;
        for (p = 0; p < n - 1; p++)
            for (q = p + 1; q < n; q++) {
                double apq = a[p * n + q], theta, t, c, s;
                if (apq == 0.0) continue;
                theta = (a[q * n + q] - a[p * n + p]) / (2.0 * apq);
                t = (theta >= 0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
                c = 1.0 / sqrt(t * t + 1.0);
                s = t * c;
                for (k = 0; k < n; k++) {
                    double akp = a[k * n + p], akq = a[k * n + q];
                    a[k * n + p] = c * akp - s * akq;
                    a[k * n + q] = s * akp + c * akq;
                }
                for (k = 0; k < n; k++) {
                    double apk = a[p * n + k], aqk = a[q * n + k];
                    a[p * n + k] = c * apk - s * aqk;
                    a[q * n + k] = s * apk + c * aqk;
                }
                for (k = 0; k < n; k++) {
                    double vkp = v[k * n + p], vkq = v[k * n + q];
                    v[k * n + p] = c * vkp - s * vkq;
                    v[k * n + q] = s * vkp + c * vkq;
                }
            }
    }
    for (i = 0; i < n; i++) w[i] = a[i * n + i];
    /* selection sort ascending, swapping columns of v */
    for (i = 0; i < n - 1; i++) {
        long mi = i;
        for (j = i + 1; j < n; j++) if (w[j] < w[mi]) mi = j;
        if (mi != i) {
            double t = w[i]; w[i] = w[mi]; w[mi] = t;
            for (k = 0; k < n; k++) {
                t = v[k * n + i]; v[k * n + i] = v[k * n + mi]; v[k * n + mi] = t;
            }
        }
    }
}
/* copy one triangle over the other so the matrix is exactly symmetric */
static void symmetrize(double *a, long n, int upper)
{
    long i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < i; j++) {
            if (upper) a[i * n + j] = a[j * n + i];
            else a[j * n + i] = a[i * n + j];
        }
}
static int uplo_arg(Args *x, int pos)
{
    Value u = np_arg(x, pos, "UPLO");
    if (u.t == T_STR) {
        const char *s = AS_STR(u)->s;
        if (s[0] == 'U' || s[0] == 'u') return 1;
    }
    return 0;
}
BI(la_eigvalsh)
{
    long n;
    Nd *a, *w;
    double *v;
    need(x, 1, 2, "eigvalsh");
    a = la_sq(A(0), &n);
    symmetrize(a->dv, n, uplo_arg(x, 1));
    w = vec_new(n);
    v = (double *)xmalloc(sizeof(double) * (size_t)(n * n > 0 ? n * n : 1));
    jacobi_eig(a->dv, n, w->dv, v);
    free(v);
    return ndv(w);
}
/* make each eigenvector's largest component positive-free of ambiguity:
   LAPACK gives no guarantee, so normalise the sign deterministically */
static void fix_vec_signs(double *v, long n)
{
    long i, j;
    for (j = 0; j < n; j++) {
        double mx = 0.0, sgn = 1.0;
        for (i = 0; i < n; i++)
            if (fabs(v[i * n + j]) > fabs(mx) * (1.0 + 1e-12)) mx = v[i * n + j];
        if (mx < 0) sgn = -1.0;
        if (sgn < 0) for (i = 0; i < n; i++) v[i * n + j] = -v[i * n + j];
    }
}
BI(la_eigh)
{
    long n;
    Nd *a, *w, *v;
    Value tv[2];
    need(x, 1, 2, "eigh");
    a = la_sq(A(0), &n);
    symmetrize(a->dv, n, uplo_arg(x, 1));
    w = vec_new(n);
    v = mat_new(n, n);
    jacobi_eig(a->dv, n, w->dv, v->dv);
    fix_vec_signs(v->dv, n);
    tv[0] = ndv(w); tv[1] = ndv(v);
    return mktuple_from(tv, 2);
}

/* general real matrix: Hessenberg reduction + shifted QR; returns 0 on
   success, sets wr/wi */
static void hessenberg(double *a, long n)
{
    long k, i, j;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    for (k = 0; k + 2 < n; k++) {
        double alpha = 0.0, nrm = 0.0, vn = 0.0;
        for (i = k + 1; i < n; i++) nrm += a[i * n + k] * a[i * n + k];
        nrm = sqrt(nrm);
        if (nrm == 0.0) continue;
        alpha = a[(k + 1) * n + k] > 0 ? -nrm : nrm;
        for (i = k + 1; i < n; i++) v[i] = a[i * n + k];
        v[k + 1] -= alpha;
        for (i = k + 1; i < n; i++) vn += v[i] * v[i];
        if (vn == 0.0) continue;
        for (j = 0; j < n; j++) {
            double s = 0.0;
            for (i = k + 1; i < n; i++) s += v[i] * a[i * n + j];
            s = 2.0 * s / vn;
            for (i = k + 1; i < n; i++) a[i * n + j] -= s * v[i];
        }
        for (i = 0; i < n; i++) {
            double s = 0.0;
            for (j = k + 1; j < n; j++) s += a[i * n + j] * v[j];
            s = 2.0 * s / vn;
            for (j = k + 1; j < n; j++) a[i * n + j] -= s * v[j];
        }
    }
    free(v);
}
static void qr_eigs(double *a, long n, double *wr, double *wi)
{
    long hi = n - 1, i, k, iter = 0, total = 0;
    double *cs = (double *)xmalloc(sizeof(double) * (size_t)(2 * n + 2));
    double *sn = cs + n + 1;
    hessenberg(a, n);
    while (hi >= 0) {
        long l;
        double mu;
        if (hi == 0) { wr[0] = a[0]; wi[0] = 0.0; break; }
        /* look for a small subdiagonal element */
        for (l = hi; l > 0; l--) {
            double s = fabs(a[(l - 1) * n + l - 1]) + fabs(a[l * n + l]);
            if (s == 0.0) s = 1.0;
            if (fabs(a[l * n + l - 1]) <= DBL_EPSILON * s) { a[l * n + l - 1] = 0.0; break; }
        }
        if (l == hi) {
            wr[hi] = a[hi * n + hi]; wi[hi] = 0.0;
            hi--; iter = 0;
            continue;
        }
        if (l == hi - 1) {
            double p = a[(hi - 1) * n + hi - 1], q = a[(hi - 1) * n + hi];
            double r = a[hi * n + hi - 1], s = a[hi * n + hi];
            double m = (p + s) / 2.0, disc = (p - s) * (p - s) / 4.0 + q * r;
            if (disc >= 0) {
                double sq = sqrt(disc);
                wr[hi - 1] = m + sq; wr[hi] = m - sq;
                if (wr[hi - 1] != 0.0 && wr[hi] != 0.0) {
                    /* refine the smaller root through the determinant */
                    double det = p * s - q * r;
                    if (fabs(wr[hi - 1]) > fabs(wr[hi])) wr[hi] = det / wr[hi - 1];
                    else wr[hi - 1] = det / wr[hi];
                }
                wi[hi - 1] = wi[hi] = 0.0;
            } else {
                wr[hi - 1] = wr[hi] = m;
                wi[hi - 1] = sqrt(-disc); wi[hi] = -wi[hi - 1];
            }
            hi -= 2; iter = 0;
            continue;
        }
        if (++total > 500 * n + 1000) {
            free(cs);
            throw_new(cls_LinAlgError, "Eigenvalues did not converge");
        }
        /* Wilkinson shift from the trailing 2x2 block (real part if complex) */
        {
            double p = a[(hi - 1) * n + hi - 1], q = a[(hi - 1) * n + hi];
            double r = a[hi * n + hi - 1], s = a[hi * n + hi];
            double m = (p + s) / 2.0, disc = (p - s) * (p - s) / 4.0 + q * r;
            if (disc >= 0) {
                double sq = sqrt(disc), e1 = m + sq, e2 = m - sq;
                mu = fabs(e1 - s) < fabs(e2 - s) ? e1 : e2;
            } else mu = m;
            if (++iter % 11 == 10) mu += fabs(a[hi * n + hi - 1]) * 0.75;
        }
        for (i = l; i <= hi; i++) a[i * n + i] -= mu;
        for (k = l; k < hi; k++) {
            double f = a[k * n + k], g = a[(k + 1) * n + k], r = n_hypot(f, g), c, s;
            long j;
            if (r == 0.0) { c = 1.0; s = 0.0; } else { c = f / r; s = g / r; }
            cs[k] = c; sn[k] = s;
            for (j = k; j <= hi; j++) {
                double t1 = a[k * n + j], t2 = a[(k + 1) * n + j];
                a[k * n + j] = c * t1 + s * t2;
                a[(k + 1) * n + j] = -s * t1 + c * t2;
            }
        }
        for (k = l; k < hi; k++) {
            double c = cs[k], s = sn[k];
            long top = k + 2 < hi ? k + 2 : hi, r2;
            for (r2 = l; r2 <= top; r2++) {
                double t1 = a[r2 * n + k], t2 = a[r2 * n + k + 1];
                a[r2 * n + k] = c * t1 + s * t2;
                a[r2 * n + k + 1] = -s * t1 + c * t2;
            }
        }
        for (i = l; i <= hi; i++) a[i * n + i] += mu;
    }
    free(cs);
}
BI(la_eigvals)
{
    long n, i;
    Nd *a, *w;
    double *wi;
    need(x, 1, 1, "eigvals");
    a = la_sq(A(0), &n);
    w = vec_new(n);
    wi = (double *)xmalloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    if (n > 0) qr_eigs(a->dv, n, w->dv, wi);
    for (i = 0; i < n; i++)
        if (wi[i] != 0.0) {
            free(wi);
            throw_new(cls_NotImplementedError, "eigvals: complex eigenvalues are not supported");
        }
    free(wi);
    /* ascending order is not guaranteed by LAPACK; keep QR deflation order reversed
       so that a triangular matrix returns its diagonal in order */
    for (i = 0; i < n / 2; i++) { double t = w->dv[i];
        w->dv[i] = w->dv[n - 1 - i];
        w->dv[n - 1 - i] = t;
    }
    return ndv(w);
}
/* eig: eigenvalues by QR, eigenvectors by solving (A - w I) v = 0 with inverse iteration */
BI(la_eig)
{
    long n, i, j, it, *piv;
    Nd *a, *w, *v, *a0;
    double *wi, *b, *lu;
    Value tv[2];
    need(x, 1, 1, "eig");
    a = la_sq(A(0), &n);
    a0 = nd_astype(a, ND_FLOAT);
    w = vec_new(n);
    v = mat_new(n, n);
    wi = (double *)xmalloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    if (n > 0) qr_eigs(a->dv, n, w->dv, wi);
    for (i = 0; i < n; i++)
        if (wi[i] != 0.0) {
            free(wi);
            throw_new(cls_NotImplementedError, "eig: complex eigenvalues are not supported");
        }
    free(wi);
    for (i = 0; i < n / 2; i++) { double t = w->dv[i];
        w->dv[i] = w->dv[n - 1 - i];
        w->dv[n - 1 - i] = t;
    }
    lu = (double *)xmalloc(sizeof(double) * (size_t)(n * n + n + 1));
    b = lu + n * n;
    piv = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    for (j = 0; j < n; j++) {
        double nrm = 0.0, shift = w->dv[j];
        double scale = 0.0;
        for (i = 0; i < n * n; i++) if (fabs(a0->dv[i]) > scale) scale = fabs(a0->dv[i]);
        if (scale == 0.0) scale = 1.0;
        for (i = 0; i < n * n; i++) lu[i] = a0->dv[i];
        for (i = 0; i < n; i++) lu[i * n + i] -= shift + scale * 1e-14;
        lu_dec(lu, n, piv);
        for (i = 0; i < n; i++) if (lu[i * n + i] == 0.0) lu[i * n + i] = scale * 1e-300;
        for (i = 0; i < n; i++) b[i] = 1.0 / sqrt((double)n) + 0.01 * (double)i;
        for (it = 0; it < 4; it++) {
            nrm = 0.0;
            lu_solve(lu, n, piv, b, 1);
            for (i = 0; i < n; i++) nrm += b[i] * b[i];
            nrm = sqrt(nrm);
            if (nrm == 0.0 || nrm != nrm) break;
            for (i = 0; i < n; i++) b[i] /= nrm;
        }
        for (i = 0; i < n; i++) v->dv[i * n + j] = b[i];
    }
    fix_vec_signs(v->dv, n);
    free(lu); free(piv);
    tv[0] = ndv(w); tv[1] = ndv(v);
    return mktuple_from(tv, 2);
}

/* one-sided Jacobi SVD of an m x n matrix with m >= n.
   u: m x n (overwritten with left vectors scaled), v: n x n, s: n singular values */
static void svd_jacobi(double *u, long m, long n, double *s, double *v)
{
    long i, p, q, sweep;
    for (i = 0; i < n * n; i++) v[i] = 0.0;
    for (i = 0; i < n; i++) v[i * n + i] = 1.0;
    for (sweep = 0; sweep < 60; sweep++) {
        int rotated = 0;
        for (p = 0; p < n - 1; p++)
            for (q = p + 1; q < n; q++) {
                double al = 0.0, be = 0.0, ga = 0.0, zeta, t, c, sn;
                for (i = 0; i < m; i++) {
                    al += u[i * n + p] * u[i * n + p];
                    be += u[i * n + q] * u[i * n + q];
                    ga += u[i * n + p] * u[i * n + q];
                }
                if (ga == 0.0 || fabs(ga) <= 1e-15 * sqrt(al * be)) continue;
                rotated = 1;
                zeta = (be - al) / (2.0 * ga);
                t = (zeta >= 0 ? 1.0 : -1.0) / (fabs(zeta) + sqrt(1.0 + zeta * zeta));
                c = 1.0 / sqrt(1.0 + t * t);
                sn = c * t;
                for (i = 0; i < m; i++) {
                    double up = u[i * n + p], uq = u[i * n + q];
                    u[i * n + p] = c * up - sn * uq;
                    u[i * n + q] = sn * up + c * uq;
                }
                for (i = 0; i < n; i++) {
                    double vp = v[i * n + p], vq = v[i * n + q];
                    v[i * n + p] = c * vp - sn * vq;
                    v[i * n + q] = sn * vp + c * vq;
                }
            }
        if (!rotated) break;
    }
    for (q = 0; q < n; q++) {
        double nr = 0.0;
        for (i = 0; i < m; i++) nr += u[i * n + q] * u[i * n + q];
        s[q] = sqrt(nr);
    }
    /* sort descending */
    for (p = 0; p < n - 1; p++) {
        long mi = p;
        for (q = p + 1; q < n; q++) if (s[q] > s[mi]) mi = q;
        if (mi != p) {
            double t = s[p]; s[p] = s[mi]; s[mi] = t;
            for (i = 0; i < m; i++) { t = u[i * n + p];
                u[i * n + p] = u[i * n + mi];
                u[i * n + mi] = t;
            }
            for (i = 0; i < n; i++) { t = v[i * n + p];
                v[i * n + p] = v[i * n + mi];
                v[i * n + mi] = t;
            }
        }
    }
    for (q = 0; q < n; q++)
        if (s[q] > 0.0) for (i = 0; i < m; i++) u[i * n + q] /= s[q];
}
/* fill the columns of q (m x nc, row-major) flagged invalid so that all columns are orthonormal */
static void orth_complete(double *q, long m, long nc, const char *valid)
{
    long j, c, i, k;
    double *w = (double *)xmalloc(sizeof(double) * (size_t)(m > 0 ? m : 1));
    char *ok = (char *)xmalloc((size_t)(nc > 0 ? nc : 1));
    for (j = 0; j < nc; j++) ok[j] = valid[j];
    for (j = 0; j < nc; j++) {
        double best = -1.0;
        long bc = 0;
        if (ok[j]) continue;
        for (c = 0; c < m; c++) {
            double nr;
            for (i = 0; i < m; i++) w[i] = i == c ? 1.0 : 0.0;
            for (k = 0; k < nc; k++) {
                double d = 0.0;
                if (!ok[k]) continue;
                for (i = 0; i < m; i++) d += w[i] * q[i * nc + k];
                for (i = 0; i < m; i++) w[i] -= d * q[i * nc + k];
            }
            nr = 0.0;
            for (i = 0; i < m; i++) nr += w[i] * w[i];
            if (nr > best) { best = nr; bc = c; }
        }
        for (i = 0; i < m; i++) w[i] = i == bc ? 1.0 : 0.0;
        for (c = 0; c < 2; c++)
            for (k = 0; k < nc; k++) {
                double d = 0.0;
                if (!ok[k]) continue;
                for (i = 0; i < m; i++) d += w[i] * q[i * nc + k];
                for (i = 0; i < m; i++) w[i] -= d * q[i * nc + k];
            }
        best = 0.0;
        for (i = 0; i < m; i++) best += w[i] * w[i];
        best = sqrt(best);
        for (i = 0; i < m; i++) q[i * nc + j] = best > 0 ? w[i] / best : 0.0;
        ok[j] = 1;
    }
    free(w); free(ok);
}
/* full SVD driver: returns u (m x r), s (r), vt (r x n), r = min(m, n) */
static void svd_thin(Nd *a, long m, long n, double **pu, double **ps, double **pvt)
{
    long r = m < n ? m : n, i, j;
    double *u, *s, *vt, *work, *v;
    char *valid;
    u = (double *)xmalloc(sizeof(double) * (size_t)(m * r + 1));
    s = (double *)xmalloc(sizeof(double) * (size_t)(r + 1));
    vt = (double *)xmalloc(sizeof(double) * (size_t)(r * n + 1));
    valid = (char *)xmalloc((size_t)(r + 1));
    memset(valid, 0, (size_t)(r + 1));
    if (m >= n) {
        v = (double *)xmalloc(sizeof(double) * (size_t)(n * n + 1));
        memcpy(u, a->dv, sizeof(double) * (size_t)(m * n));
        svd_jacobi(u, m, n, s, v);
        for (i = 0; i < n; i++) for (j = 0; j < n; j++) vt[i * n + j] = v[j * n + i];
        free(v);
        for (j = 0; j < r; j++) valid[j] = s[j] > 0.0;
        orth_complete(u, m, r, valid);
    } else {
        /* A^T = U' S V'^T  =>  A = V' S U'^T */
        work = (double *)xmalloc(sizeof(double) * (size_t)(n * m + 1));
        v = (double *)xmalloc(sizeof(double) * (size_t)(m * m + 1));
        for (i = 0; i < m; i++) for (j = 0; j < n; j++) work[j * m + i] = a->dv[i * n + j];
        svd_jacobi(work, n, m, s, v);
        for (i = 0; i < m; i++) for (j = 0; j < m; j++) u[i * m + j] = v[i * m + j];
        for (i = 0; i < m; i++) for (j = 0; j < n; j++) vt[i * n + j] = work[j * m + i];
        /* vt is m x n; complete invalid rows through the transposed view */
        for (j = 0; j < r; j++) valid[j] = s[j] > 0.0;
        {
            double *tq = (double *)xmalloc(sizeof(double) * (size_t)(n * m + 1));
            for (i = 0; i < m; i++) for (j = 0; j < n; j++) tq[j * m + i] = vt[i * n + j];
            orth_complete(tq, n, m, valid);
            for (i = 0; i < m; i++) for (j = 0; j < n; j++) vt[i * n + j] = tq[j * m + i];
            free(tq);
        }
        free(work); free(v);
    }
    free(valid);
    *pu = u; *ps = s; *pvt = vt;
}
BI(la_svd)
{
    long m, n, r, i, j;
    Nd *a, *S;
    double *u, *s, *vt;
    int full = 1, cuv = 1;
    Value fv = np_arg(x, 1, "full_matrices"), cv = np_arg(x, 2, "compute_uv");
    Value tv[3];
    need(x, 1, 4, "svd");
    if (np_has(fv)) full = py_truthy(fv);
    if (np_has(cv)) cuv = py_truthy(cv);
    a = la_mat(A(0), &m, &n);
    r = m < n ? m : n;
    svd_thin(a, m, n, &u, &s, &vt);
    S = vec_new(r);
    for (i = 0; i < r; i++) S->dv[i] = s[i];
    if (!cuv) { free(u); free(s); free(vt); return ndv(S); }
    {
        long uc = full ? m : r, vr = full ? n : r;
        Nd *U = mat_new(m, uc), *V = mat_new(vr, n);
        char *valid;
        double *tmp;
        if (uc > r) {
            tmp = (double *)xmalloc(sizeof(double) * (size_t)(m * uc + 1));
            valid = (char *)xmalloc((size_t)uc + 1);
            for (j = 0; j < uc; j++) valid[j] = j < r;
            for (i = 0; i < m; i++) for (j = 0; j < uc; j++)
                tmp[i * uc + j] = j < r ? u[i * r + j] : 0.0;
            orth_complete(tmp, m, uc, valid);
            memcpy(U->dv, tmp, sizeof(double) * (size_t)(m * uc));
            free(tmp); free(valid);
        } else memcpy(U->dv, u, sizeof(double) * (size_t)(m * r));
        if (vr > r) {
            tmp = (double *)xmalloc(sizeof(double) * (size_t)(n * vr + 1));
            valid = (char *)xmalloc((size_t)vr + 1);
            for (j = 0; j < vr; j++) valid[j] = j < r;
            for (i = 0; i < n; i++) for (j = 0; j < vr; j++)
                tmp[i * vr + j] = j < r ? vt[j * n + i] : 0.0;
            orth_complete(tmp, n, vr, valid);
            for (i = 0; i < n; i++) for (j = 0; j < vr; j++) V->dv[j * n + i] = tmp[i * vr + j];
            free(tmp); free(valid);
        } else memcpy(V->dv, vt, sizeof(double) * (size_t)(r * n));
        tv[0] = ndv(U); tv[1] = ndv(S); tv[2] = ndv(V);
    }
    free(u); free(s); free(vt);
    return mktuple_from(tv, 3);
}
static double la_eps(void)
{
    return DBL_EPSILON;
}
BI(la_matrix_rank)
{
    long m, n, r, i, rank = 0;
    Nd *a;
    double *u, *s, *vt, tol, smax = 0.0;
    Value tv = np_arg(x, 1, "tol");
    need(x, 1, 3, "matrix_rank");
    a = np_f(A(0));
    if (a->nd < 2) {
        for (i = 0; i < a->size; i++) if (a->dv[i] != 0.0) return mkint(1);
        return mkint(0);
    }
    if (a->nd > 2) throw_new(cls_NotImplementedError, "linalg: stacked matrices are not supported");
    m = a->shape[0]; n = a->shape[1];
    r = m < n ? m : n;
    if (r == 0) return mkint(0);
    svd_thin(a, m, n, &u, &s, &vt);
    for (i = 0; i < r; i++) if (s[i] > smax) smax = s[i];
    tol = np_has(tv) ? to_dbl(tv) : smax * (double)(m > n ? m : n) * la_eps();
    for (i = 0; i < r; i++) if (s[i] > tol) rank++;
    free(u); free(s); free(vt);
    return mkint(rank);
}
/* least squares x = pinv(a) b with SVD cutoff; returns rank, fills singular values */
static void lstsq_core(Nd *a, long m, long n, const double *b, long k, double rcond,
                       double *xo, long *rankp, double *sv)
{
    long r = m < n ? m : n, i, j, c, rank = 0;
    double *u, *s, *vt, cut, smax = 0.0;
    svd_thin(a, m, n, &u, &s, &vt);
    for (i = 0; i < r; i++) if (s[i] > smax) smax = s[i];
    cut = rcond * smax;
    for (i = 0; i < n * k; i++) xo[i] = 0.0;
    for (i = 0; i < r; i++) {
        if (sv) sv[i] = s[i];
        if (s[i] > cut) {
            rank++;
            for (c = 0; c < k; c++) {
                double d = 0.0;
                for (j = 0; j < m; j++) d += u[j * r + i] * b[j * k + c];
                d /= s[i];
                for (j = 0; j < n; j++) xo[j * k + c] += d * vt[i * n + j];
            }
        }
    }
    *rankp = rank;
    free(u); free(s); free(vt);
}
BI(la_lstsq)
{
    long m, n, k, rank, i, j, c, r;
    Nd *a, *b, *xr, *res, *sv;
    double rc, *bb;
    Value rcv = np_arg(x, 2, "rcond"), tv[4];
    need(x, 2, 3, "lstsq");
    a = la_mat(A(0), &m, &n);
    b = np_f(A(1));
    if (b->nd < 1 || b->nd > 2 || b->shape[0] != m)
        throw_new(cls_LinAlgError, "Incompatible dimensions");
    k = b->nd == 1 ? 1 : b->shape[1];
    r = m < n ? m : n;
    rc = np_has(rcv) && to_dbl(rcv) >= 0 ? to_dbl(rcv) : la_eps() * (double)(m > n ? m : n);
    xr = b->nd == 1 ? vec_new(n) : mat_new(n, k);
    sv = vec_new(r);
    bb = b->dv;
    lstsq_core(a, m, n, bb, k, rc, xr->dv, &rank, sv->dv);
    if (rank == n && m > n) {
        res = vec_new(k);
        for (c = 0; c < k; c++) {
            double ss = 0.0;
            for (i = 0; i < m; i++) {
                double d = bb[i * k + c];
                for (j = 0; j < n; j++) d -= a->dv[i * n + j] * xr->dv[j * k + c];
                ss += d * d;
            }
            res->dv[c] = ss;
        }
    } else res = vec_new(0);
    tv[0] = ndv(xr); tv[1] = ndv(res); tv[2] = mkint(rank); tv[3] = ndv(sv);
    return mktuple_from(tv, 4);
}
BI(la_pinv)
{
    long m, n, i, j, k, r;
    Nd *a, *res;
    double *u, *s, *vt, cut, smax = 0.0, rc;
    Value rcv = np_arg(x, 1, "rcond");
    need(x, 1, 2, "pinv");
    a = la_mat(A(0), &m, &n);
    r = m < n ? m : n;
    res = mat_new(n, m);
    for (i = 0; i < n * m; i++) res->dv[i] = 0.0;
    if (r == 0) return ndv(res);
    svd_thin(a, m, n, &u, &s, &vt);
    for (i = 0; i < r; i++) if (s[i] > smax) smax = s[i];
    rc = np_has(rcv) ? to_dbl(rcv) : la_eps() * (double)(m > n ? m : n);
    cut = rc * smax;
    for (k = 0; k < r; k++)
        if (s[k] > cut)
            for (i = 0; i < n; i++)
                for (j = 0; j < m; j++) res->dv[i * m + j] += vt[k * n + i] * u[j * r + k] / s[k];
    free(u); free(s); free(vt);
    return ndv(res);
}
/* Householder QR (LAPACK sign conventions) */
BI(la_qr)
{
    long m, n, K, i, j, k, qc;
    Nd *a, *Q, *R;
    double *vs, *tau;
    const char *mode = "reduced";
    Value mv = np_arg(x, 1, "mode"), tv[2];
    need(x, 1, 2, "qr");
    if (mv.t == T_STR) mode = AS_STR(mv)->s;
    a = la_mat(A(0), &m, &n);
    K = m < n ? m : n;
    vs = (double *)xmalloc(sizeof(double) * (size_t)(m * (K > 0 ? K : 1) + 1));
    tau = (double *)xmalloc(sizeof(double) * (size_t)(K + 1));
    for (k = 0; k < K; k++) {
        double alpha = a->dv[k * n + k], xn = 0.0, beta;
        for (i = k + 1; i < m; i++) xn += a->dv[i * n + k] * a->dv[i * n + k];
        xn = sqrt(xn);
        for (i = 0; i < m; i++) vs[i * K + k] = 0.0;
        if (xn == 0.0) { tau[k] = 0.0; vs[k * K + k] = 1.0; continue; }
        beta = -(alpha >= 0 ? 1.0 : -1.0) * n_hypot(alpha, xn);
        tau[k] = (beta - alpha) / beta;
        vs[k * K + k] = 1.0;
        for (i = k + 1; i < m; i++) vs[i * K + k] = a->dv[i * n + k] / (alpha - beta);
        a->dv[k * n + k] = beta;
        for (i = k + 1; i < m; i++) a->dv[i * n + k] = 0.0;
        for (j = k + 1; j < n; j++) {
            double d = 0.0;
            for (i = k; i < m; i++) d += vs[i * K + k] * a->dv[i * n + j];
            d *= tau[k];
            for (i = k; i < m; i++) a->dv[i * n + j] -= d * vs[i * K + k];
        }
    }
    if (strcmp(mode, "r") == 0) {
        R = mat_new(K, n);
        for (i = 0; i < K; i++) for (j = 0; j < n; j++)
            R->dv[i * n + j] = j >= i ? a->dv[i * n + j] : 0.0;
        free(vs); free(tau);
        return ndv(R);
    }
    qc = strcmp(mode, "complete") == 0 ? m : K;
    Q = mat_new(m, m);
    for (i = 0; i < m * m; i++) Q->dv[i] = 0.0;
    for (i = 0; i < m; i++) Q->dv[i * m + i] = 1.0;
    for (k = K - 1; k >= 0; k--) {
        if (tau[k] == 0.0) continue;
        for (j = 0; j < m; j++) {
            double d = 0.0;
            for (i = k; i < m; i++) d += vs[i * K + k] * Q->dv[i * m + j];
            d *= tau[k];
            for (i = k; i < m; i++) Q->dv[i * m + j] -= d * vs[i * K + k];
        }
    }
    {
        Nd *Qo = mat_new(m, qc);
        long rr = qc;
        for (i = 0; i < m; i++) for (j = 0; j < qc; j++) Qo->dv[i * qc + j] = Q->dv[i * m + j];
        R = mat_new(rr, n);
        for (i = 0; i < rr; i++) for (j = 0; j < n; j++) R->dv[i * n + j] = (j >= i && i < m)
            ? a->dv[i * n + j] : 0.0;
        tv[0] = ndv(Qo); tv[1] = ndv(R);
    }
    free(vs); free(tau);
    return mktuple_from(tv, 2);
}
/* ---- norms ------------------------------------------------------------- */
static double vec_norm(const double *p, long n, long stride, double ord, int ordnone)
{
    long i;
    double s = 0.0;
    if (ordnone || ord == 2.0) {
        double scale = 0.0;
        for (i = 0; i < n; i++) if (fabs(p[i * stride]) > scale) scale = fabs(p[i * stride]);
        if (scale == 0.0 || scale != scale || scale - scale != 0.0) {
            for (i = 0; i < n; i++) s += p[i * stride] * p[i * stride];
            return sqrt(s);
        }
        for (i = 0; i < n; i++) s += p[i * stride] * p[i * stride];
        return sqrt(s);
    }
    if (ord == HUGE_VAL) {
        for (i = 0; i < n; i++) if (fabs(p[i * stride]) > s || p[i * stride] != p[i * stride])
            s = fabs(p[i * stride]);
        return s;
    }
    if (ord == -HUGE_VAL) {
        s = HUGE_VAL;
        for (i = 0; i < n; i++) if (fabs(p[i * stride]) < s) s = fabs(p[i * stride]);
        return s;
    }
    if (ord == 0.0) {
        for (i = 0; i < n; i++) if (p[i * stride] != 0.0) s += 1.0;
        return s;
    }
    if (ord == 1.0) {
        for (i = 0; i < n; i++) s += fabs(p[i * stride]);
        return s;
    }
    for (i = 0; i < n; i++) s += pow(fabs(p[i * stride]), ord);
    return pow(s, 1.0 / ord);
}
BI(la_norm)
{
    Nd *a;
    Value ov = np_arg(x, 1, "ord"), av = np_arg(x, 2, "axis"), kv = np_arg(x, 3, "keepdims");
    double ord = 2.0;
    int ordnone = !np_has(ov), fro = 0, nuc = 0, keep = 0;
    need(x, 1, 4, "norm");
    a = np_f(A(0));
    if (np_has(kv)) keep = py_truthy(kv);
    if (np_has(ov)) {
        if (ov.t == T_STR) {
            if (strcmp(AS_STR(ov)->s, "fro") == 0) fro = 1;
            else if (strcmp(AS_STR(ov)->s, "nuc") == 0) nuc = 1;
            else throw_new(cls_ValueError, "Invalid norm order for matrices.");
        } else ord = to_dbl(ov);
    }
    if (np_has(av)) {
        long ax;
        if (av.t == T_TUPLE)
            throw_new(cls_NotImplementedError,
            "norm: matrix norms over an axis pair are not supported");
        ax = axis_norm(need_long(av, "axis"), a->nd);
        {
            long outer = 1, len = a->shape[ax], inner = 1, o, i, sh[ND_MAXD];
            long k;
            int rn = 0;
            Nd *r;
            for (k = 0; k < ax; k++) outer *= a->shape[k];
            for (k = ax + 1; k < a->nd; k++) inner *= a->shape[k];
            for (k = 0; k < a->nd; k++) {
                if (k == ax) { if (keep) sh[rn++] = 1; }
                else sh[rn++] = a->shape[k];
            }
            r = nd_new(ND_FLOAT, rn, sh);
            for (o = 0; o < outer; o++)
                for (i = 0; i < inner; i++)
                    r->dv[o * inner + i] = vec_norm(a->dv + o * len * inner + i, len, inner, ord,
                        ordnone || fro);
            return nd_ret(r);
        }
    }
    if (a->nd <= 1 || (ordnone && !nuc) || fro) {
        double v;
        if (a->nd > 2 && !ordnone && !fro)
            throw_new(cls_ValueError, "Improper number of dimensions to norm.");
        v = vec_norm(a->dv, a->size, 1, ord, ordnone || fro);
        if (keep && a->nd > 0) {
            Nd *r;
            long sh[ND_MAXD];
            int k;
            for (k = 0; k < a->nd; k++) sh[k] = 1;
            r = nd_new(ND_FLOAT, a->nd, sh);
            r->dv[0] = v;
            return ndv(r);
        }
        return mkfloat(v);
    }
    if (a->nd != 2) throw_new(cls_ValueError, "Improper number of dimensions to norm.");
    {
        long m = a->shape[0], n = a->shape[1], i, j;
        double v = 0.0;
        if (nuc || ord == 2.0 || ord == -2.0) {
            double *u, *s, *vt;
            long r = m < n ? m : n;
            if (r == 0) return mkfloat(0.0);
            svd_thin(a, m, n, &u, &s, &vt);
            if (nuc) for (i = 0; i < r; i++) v += s[i];
            else v = ord > 0 ? s[0] : s[r - 1];
            free(u); free(s); free(vt);
        } else if (ord == 1.0 || ord == -1.0) {
            v = ord > 0 ? 0.0 : HUGE_VAL;
            for (j = 0; j < n; j++) {
                double cs = 0.0;
                for (i = 0; i < m; i++) cs += fabs(a->dv[i * n + j]);
                if (ord > 0 ? cs > v : cs < v) v = cs;
            }
        } else if (ord == HUGE_VAL || ord == -HUGE_VAL) {
            v = ord > 0 ? 0.0 : HUGE_VAL;
            for (i = 0; i < m; i++) {
                double rs = 0.0;
                for (j = 0; j < n; j++) rs += fabs(a->dv[i * n + j]);
                if (ord > 0 ? rs > v : rs < v) v = rs;
            }
        } else throw_new(cls_ValueError, "Invalid norm order for matrices.");
        if (keep) {
            Nd *r = mat_new(1, 1);
            r->dv[0] = v;
            return ndv(r);
        }
        return mkfloat(v);
    }
}
BI(la_cond)
{
    long m, n, r, i;
    Nd *a;
    double *u, *s, *vt, v;
    need(x, 1, 2, "cond");
    if (x->n > 1 && np_has(A(1))) {
        Args y;
        Value av[2], c1, c2;
        Value inv;
        y.n = 1; y.a = av; y.nk = 0; y.kw = NULL;
        av[0] = A(0);
        inv = la_inv(&y);
        y.n = 2; av[0] = A(0); av[1] = A(1);
        c1 = la_norm(&y);
        av[0] = inv;
        c2 = la_norm(&y);
        return mkfloat(to_dbl(c1) * to_dbl(c2));
    }
    a = la_mat(A(0), &m, &n);
    r = m < n ? m : n;
    svd_thin(a, m, n, &u, &s, &vt);
    v = s[r - 1] == 0.0 ? HUGE_VAL : s[0] / s[r - 1];
    (void)i;
    free(u); free(s); free(vt);
    return mkfloat(v);
}
BI(la_matrix_power)
{
    long n, p, i;
    Nd *a, *r, *base;
    Value pv, res;
    need(x, 2, 2, "matrix_power");
    a = la_sq(A(0), &n);
    p = need_long(A(1), "n");
    if (p < 0) {
        Args y;
        Value av[1];
        y.n = 1; y.a = av; y.nk = 0; y.kw = NULL;
        av[0] = A(0);
        pv = la_inv(&y);
        a = AS_ND(pv);
        p = -p;
    }
    r = mat_new(n, n);
    for (i = 0; i < n * n; i++) r->dv[i] = 0.0;
    for (i = 0; i < n; i++) r->dv[i * n + i] = 1.0;
    base = a;
    while (p > 0) {
        if (p & 1) { res = nd_matmul(r, base, 0); r = AS_ND(res); }
        p >>= 1;
        if (p) { res = nd_matmul(base, base, 0); base = AS_ND(res); }
    }
    return ndv(r);
}
static void la_init(Module *np)
{
    Module *m = new_module("numpy.linalg");
    {
        Class **b = (Class **)xmalloc(sizeof(Class *));
        b[0] = cls_ValueError;
        cls_LinAlgError = make_class(intern_c("LinAlgError"), b, 1, intern_c("numpy.linalg"));
    }
    mod_def(m, "det", la_det); mod_def(m, "slogdet", la_slogdet); mod_def(m, "inv", la_inv);
    mod_def(m, "solve", la_solve); mod_def(m, "cholesky", la_cholesky);
    mod_def(m, "eigvalsh", la_eigvalsh); mod_def(m, "eigh", la_eigh);
    mod_def(m, "eigvals", la_eigvals); mod_def(m, "eig", la_eig);
    mod_def(m, "svd", la_svd); mod_def(m, "matrix_rank", la_matrix_rank);
    mod_def(m, "lstsq", la_lstsq); mod_def(m, "pinv", la_pinv); mod_def(m, "qr", la_qr);
    mod_def(m, "norm", la_norm); mod_def(m, "cond", la_cond);
    mod_def(m, "matrix_power", la_matrix_power);
    mod_val(m, "LinAlgError", mkobj(cls_LinAlgError, T_CLASS));
    mod_val(np, "linalg", mkobj(m, T_MODULE));
    mod_val(np, "LinAlgError", mkobj(cls_LinAlgError, T_CLASS));
}
