/* =========================================================================
   p7_la.c   numpy.linalg (dense real matrices, double precision)
   ========================================================================= */
static Class *cls_LinAlgError;

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

/* general real matrix: port of the LAPACK dgeev pipeline (dgebal, dgehd2/dorghr, dlahqr with
   dlanv2, dtrevc back-substitution, back-transformation, dgeev normalisation).  Matrices are
   row-major n x n double arrays. */
#define LA_ULP 2.220446049250313e-16
#define LA_SAFMIN 2.2250738585072014e-308
static double la_sgn(double x)
{
    if (x < 0.0) return -1.0;
    if (x == 0.0 && 1.0 / x < 0.0) return -1.0;
    return 1.0;
}
static double la_max(double a, double b) { return a > b ? a : b; }
/* LAPACK dlapy2: sqrt(x*x + y*y) as w * sqrt(1 + (z/w)^2).  The rounding is part of the
   algorithm (QR sweeps are sign-sensitive), so n_hypot must not be used here. */
static double la_lapy2(double x, double y)
{
    double w = fabs(x), z = fabs(y), q;
    if (z > w) { q = w; w = z; z = q; }
    if (z == 0.0 || w > 1.797693134862315708e308) return w;
    q = z / w;
    return w * sqrt(1.0 + q * q);
}
/* dnrm2 (accurate) */
static double la_nrm2(const double *x, long cnt, long stride)
{
    double r = 0.0;
    long i;
    for (i = 0; i < cnt; i++) r = n_hypot(r, x[i * stride]);
    return r;
}
static double la_min(double a, double b) { return a < b ? a : b; }
static void la_swaprows(double *h, long n, long a, long b)
{
    long c;
    for (c = 0; c < n; c++) {
        double t = h[a * n + c];
        h[a * n + c] = h[b * n + c]; h[b * n + c] = t;
    }
}
/* dlanv2: Schur factorisation of a real 2x2 block */
static void la_lanv2(double *pa, double *pb, double *pc, double *pd, double *rt1r, double *rt1i,
    double *rt2r, double *rt2i, double *pcs, double *psn)
{
    double a = *pa, b = *pb, c = *pc, d = *pd, cs, sn, temp, p, bcmax, bcmis, scale, z, tau;
    if (c == 0.0) { cs = 1.0; sn = 0.0; }
    else if (b == 0.0) {
        cs = 0.0; sn = 1.0;
        temp = d; d = a; a = temp; b = -c; c = 0.0;
    } else if ((a - d) == 0.0 && la_sgn(b) != la_sgn(c)) { cs = 1.0; sn = 0.0; }
    else {
        temp = a - d;
        p = 0.5 * temp;
        bcmax = la_max(fabs(b), fabs(c));
        bcmis = la_min(fabs(b), fabs(c)) * la_sgn(b) * la_sgn(c);
        scale = la_max(fabs(p), bcmax);
        z = p / scale * p + bcmax / scale * bcmis;
        if (z >= 4.0 * LA_ULP) {
            z = p + fabs(sqrt(scale) * sqrt(z)) * la_sgn(p);
            a = d + z;
            d = d - bcmax / z * bcmis;
            tau = la_lapy2(c, z);
            cs = z / tau; sn = c / tau;
            b = b - c; c = 0.0;
        } else {
            double sigma = b + c, aa, bb, cc, dd;
            tau = la_lapy2(sigma, temp);
            cs = sqrt(0.5 * (1.0 + fabs(sigma) / tau));
            sn = -(p / (tau * cs)) * la_sgn(sigma);
            aa = a * cs + b * sn; bb = -a * sn + b * cs;
            cc = c * cs + d * sn; dd = -c * sn + d * cs;
            a = aa * cs + cc * sn; b = bb * cs + dd * sn;
            c = -aa * sn + cc * cs; d = -bb * sn + dd * cs;
            temp = 0.5 * (a + d);
            a = temp; d = temp;
            if (c != 0.0) {
                if (b != 0.0) {
                    if (la_sgn(b) == la_sgn(c)) {
                        double sab = sqrt(fabs(b)), sac = sqrt(fabs(c)), cs1, sn1;
                        p = sab * sac * la_sgn(c);
                        tau = 1.0 / sqrt(fabs(b + c));
                        a = temp + p; d = temp - p;
                        b = b - c; c = 0.0;
                        cs1 = sab * tau; sn1 = sac * tau;
                        temp = cs * cs1 - sn * sn1;
                        sn = cs * sn1 + sn * cs1;
                        cs = temp;
                    }
                } else {
                    b = -c; c = 0.0;
                    temp = cs; cs = -sn; sn = temp;
                }
            }
        }
    }
    *pa = a; *pb = b; *pc = c; *pd = d;
    *rt1r = a; *rt2r = d;
    if (c == 0.0) { *rt1i = 0.0; *rt2i = 0.0; }
    else { *rt1i = sqrt(fabs(b)) * sqrt(fabs(c)); *rt2i = -*rt1i; }
    *pcs = cs; *psn = sn;
}
/* dlarfg: H = I - tau [1 v][1 v]^T; x (cnt entries) becomes v, *alpha becomes beta */
static double la_larfg(double *alpha, double *x, long cnt)
{
    double xn, beta, tau, sc, a = *alpha, safmin = LA_SAFMIN / (DBL_EPSILON * 0.5);
    long i, knt = 0;
    xn = la_nrm2(x, cnt, 1);
    if (xn == 0.0) return 0.0;
    beta = -la_lapy2(a, xn) * la_sgn(a);
    if (fabs(beta) < safmin) {
        /* xnorm, beta may be inaccurate: scale x and recompute them */
        do {
            knt++;
            for (i = 0; i < cnt; i++) x[i] *= 1.0 / safmin;
            beta *= 1.0 / safmin;
            a *= 1.0 / safmin;
        } while (fabs(beta) < safmin && knt < 20);
        xn = la_nrm2(x, cnt, 1);
        beta = -la_lapy2(a, xn) * la_sgn(a);
    }
    tau = (beta - a) / beta;
    sc = 1.0 / (a - beta);
    for (i = 0; i < cnt; i++) x[i] *= sc;
    for (i = 0; i < knt; i++) beta *= safmin;
    *alpha = beta;
    return tau;
}
/* dgebal('B'): permute and scale in place; ilo/ihi 0-based inclusive; scale[] as in LAPACK */
static void la_gebal(double *a, long n, long *pilo, long *pihi, double *scale)
{
    long k = 0, l = n - 1, i, j, r, c, t;
    int noconv, iso;
    double radix = 2.0, sfmin1 = LA_SAFMIN / LA_ULP, sfmax1 = 1.0 / sfmin1;
    double sfmin2 = sfmin1 * radix, sfmax2 = 1.0 / sfmin2;
    for (i = 0; i < n; i++) scale[i] = 1.0;
    noconv = 1;
    while (noconv) {
        noconv = 0;
        for (j = l; j >= 0; j--) {
            iso = 1;
            for (i = 0; i <= l; i++)
                if (i != j && a[j * n + i] != 0.0) { iso = 0; break; }
            if (iso) {
                scale[l] = (double)j;
                if (j != l) {
                    for (r = 0; r <= l; r++) {
                        double tmp = a[r * n + j]; a[r * n + j] = a[r * n + l]; a[r * n + l] = tmp;
                    }
                    for (c = k; c < n; c++) {
                        double tmp = a[j * n + c]; a[j * n + c] = a[l * n + c]; a[l * n + c] = tmp;
                    }
                }
                noconv = 1;
                if (l == 0) { *pilo = 0; *pihi = 0; return; }
                l--;
            }
        }
    }
    noconv = 1;
    while (noconv) {
        noconv = 0;
        for (j = k; j <= l; j++) {
            iso = 1;
            for (i = k; i <= l; i++)
                if (i != j && a[i * n + j] != 0.0) { iso = 0; break; }
            if (iso) {
                scale[k] = (double)j;
                if (j != k) {
                    for (r = 0; r <= l; r++) {
                        double tmp = a[r * n + j]; a[r * n + j] = a[r * n + k]; a[r * n + k] = tmp;
                    }
                    for (c = k; c < n; c++) {
                        double tmp = a[j * n + c]; a[j * n + c] = a[k * n + c]; a[k * n + c] = tmp;
                    }
                }
                noconv = 1;
                k++;
            }
        }
    }
    for (i = k; i <= l; i++) scale[i] = 1.0;
    noconv = 1;
    while (noconv) {
        noconv = 0;
        for (i = k; i <= l; i++) {
            double cn = 0.0, rn = 0.0, ca = 0.0, ra = 0.0, g, f, s;
            for (t = k; t <= l; t++) {
                cn = n_hypot(cn, a[t * n + i]);
                rn = n_hypot(rn, a[i * n + t]);
            }
            for (t = 0; t <= l; t++) ca = la_max(ca, fabs(a[t * n + i]));
            for (t = k; t < n; t++) ra = la_max(ra, fabs(a[i * n + t]));
            if (cn == 0.0 || rn == 0.0) continue;
            g = rn / radix; f = 1.0; s = cn + rn;
            while (cn < g && la_max(f, la_max(cn, ca)) < sfmax2
                && la_min(rn, la_min(g, ra)) > sfmin2) {
                f *= radix; cn *= radix; ca *= radix; rn /= radix; g /= radix; ra /= radix;
            }
            g = cn / radix;
            while (g >= rn && la_max(rn, ra) < sfmax2
                && la_min(la_min(f, cn), la_min(g, ca)) > sfmin2) {
                f /= radix; cn /= radix; g /= radix; ca /= radix; rn *= radix; ra *= radix;
            }
            if (cn + rn >= 0.95 * s) continue;
            if (f < 1.0 && scale[i] < 1.0 && f * scale[i] <= sfmin1) continue;
            if (f > 1.0 && scale[i] > 1.0 && scale[i] >= sfmax1 / f) continue;
            g = 1.0 / f;
            scale[i] *= f;
            noconv = 1;
            for (t = k; t < n; t++) a[i * n + t] *= g;
            for (t = 0; t <= l; t++) a[t * n + i] *= f;
        }
    }
    *pilo = k; *pihi = l;
}
/* Householder reduction to Hessenberg form; refl (n x n) row i holds v of reflector i,
   tau[i] its scalar */
static void la_gehrd(double *a, long n, long ilo, long ihi, double *refl, double *tau)
{
    long i, r, c, m;
    for (i = 0; i < n; i++) tau[i] = 0.0;
    for (i = ilo; i < ihi; i++) {
        double alpha = a[(i + 1) * n + i], *v = refl + i * n, t;
        m = ihi - i;
        v[0] = 1.0;
        for (r = i + 2; r <= ihi; r++) v[r - i - 1] = a[r * n + i];
        t = la_larfg(&alpha, v + 1, m - 1);
        tau[i] = t;
        if (t != 0.0) {
            for (r = 0; r <= ihi; r++) {
                double *row = a + r * n, s = 0.0;
                for (c = 0; c < m; c++) s += row[i + 1 + c] * v[c];
                s *= t;
                for (c = 0; c < m; c++) row[i + 1 + c] -= s * v[c];
            }
            for (c = i + 1; c < n; c++) {
                double s = 0.0;
                for (r = 0; r < m; r++) s += v[r] * a[(i + 1 + r) * n + c];
                s *= t;
                for (r = 0; r < m; r++) a[(i + 1 + r) * n + c] -= s * v[r];
            }
        }
        a[(i + 1) * n + i] = alpha;
        for (r = i + 2; r < n; r++) a[r * n + i] = 0.0;
    }
}
static void la_orghr(double *q, long n, long ilo, long ihi, const double *refl, const double *tau)
{
    long i, r, c, m;
    for (r = 0; r < n * n; r++) q[r] = 0.0;
    for (r = 0; r < n; r++) q[r * n + r] = 1.0;
    for (i = ihi - 1; i >= ilo; i--) {
        const double *v = refl + i * n;
        double t = tau[i];
        if (t == 0.0) continue;
        m = ihi - i;
        for (c = i + 1; c < n; c++) {
            double s = 0.0;
            for (r = 0; r < m; r++) s += v[r] * q[(i + 1 + r) * n + c];
            s *= t;
            for (r = 0; r < m; r++) q[(i + 1 + r) * n + c] -= s * v[r];
        }
    }
}
/* dlahqr: double-shift QR on a Hessenberg matrix; returns 1 on convergence */
static int la_lahqr(double *h, double *z, long n, long ilo, long ihi, int wantt, int wantz,
    double *wr, double *wi)
{
    long i, j, k, l, m, i1, i2, its, itmax, kdefl, nh, nr;
    double smlnum, s, h11, h12, h21, h22, rt1r, rt1i, rt2r, rt2i, v[3];
#define H(a, b) h[(a) * n + (b)]
#define Z(a, b) z[(a) * n + (b)]
    if (ilo == ihi) { wr[ilo] = H(ilo, ilo); wi[ilo] = 0.0; return 1; }
    for (j = ilo; j < ihi - 2; j++) { H(j + 2, j) = 0.0; H(j + 3, j) = 0.0; }
    if (ilo <= ihi - 2) H(ihi, ihi - 2) = 0.0;
    nh = ihi - ilo + 1;
    smlnum = LA_SAFMIN * ((double)nh / LA_ULP);
    i1 = 0; i2 = n - 1;
    itmax = 30 * (nh > 10 ? nh : 10);
    kdefl = 0;
    i = ihi;
    while (i >= ilo) {
        int conv = 0;
        l = ilo;
        for (its = 0; its <= itmax; its++) {
            k = i;
            while (k > l) {
                double hk = fabs(H(k, k - 1)), tst;
                if (hk <= smlnum) break;
                tst = fabs(H(k - 1, k - 1)) + fabs(H(k, k));
                if (tst == 0.0) {
                    if (k - 2 >= ilo) tst += fabs(H(k - 1, k - 2));
                    if (k + 1 <= ihi) tst += fabs(H(k + 1, k));
                }
                if (hk <= LA_ULP * tst) {
                    double ab = la_max(hk, fabs(H(k - 1, k))), ba = la_min(hk, fabs(H(k - 1, k)));
                    double dd = fabs(H(k - 1, k - 1) - H(k, k));
                    double aa = la_max(fabs(H(k, k)), dd), bb = la_min(fabs(H(k, k)), dd);
                    s = aa + ab;
                    if (ba * (ab / s) <= la_max(smlnum, LA_ULP * (bb * (aa / s)))) break;
                }
                k--;
            }
            l = k;
            if (l > ilo) H(l, l - 1) = 0.0;
            if (l >= i - 1) { conv = 1; break; }
            kdefl++;
            if (!wantt) { i1 = l; i2 = i; }
            if (kdefl % 20 == 0) {
                s = fabs(H(i, i - 1)) + fabs(H(i - 1, i - 2));
                h11 = 0.75 * s + H(i, i); h12 = -0.4375 * s; h21 = s; h22 = h11;
            } else if (kdefl % 10 == 0) {
                s = fabs(H(l + 1, l)) + fabs(H(l + 2, l + 1));
                h11 = 0.75 * s + H(l, l); h12 = -0.4375 * s; h21 = s; h22 = h11;
            } else {
                h11 = H(i - 1, i - 1); h21 = H(i, i - 1); h12 = H(i - 1, i); h22 = H(i, i);
            }
            s = fabs(h11) + fabs(h12) + fabs(h21) + fabs(h22);
            if (s == 0.0) { rt1r = rt1i = rt2r = rt2i = 0.0; }
            else {
                double tr, det, rtdisc;
                h11 /= s; h21 /= s; h12 /= s; h22 /= s;
                tr = (h11 + h22) / 2.0;
                det = (h11 - tr) * (h22 - tr) - h12 * h21;
                rtdisc = sqrt(fabs(det));
                if (det >= 0.0) {
                    rt1r = tr * s; rt2r = rt1r; rt1i = rtdisc * s; rt2i = -rt1i;
                } else {
                    rt1r = tr + rtdisc; rt2r = tr - rtdisc;
                    if (fabs(rt1r - h22) <= fabs(rt2r - h22)) { rt1r = rt1r * s; rt2r = rt1r; }
                    else { rt2r = rt2r * s; rt1r = rt2r; }
                    rt1i = 0.0; rt2i = 0.0;
                }
            }
            m = i - 2;
            for (;;) {
                double h21s = H(m + 1, m), v1, v2, v3;
                s = fabs(H(m, m) - rt2r) + fabs(rt2i) + fabs(h21s);
                h21s = H(m + 1, m) / s;
                v1 = h21s * H(m, m + 1) + (H(m, m) - rt1r) * ((H(m, m) - rt2r) / s)
                    - rt1i * (rt2i / s);
                v2 = h21s * (H(m, m) + H(m + 1, m + 1) - rt1r - rt2r);
                v3 = h21s * H(m + 2, m + 1);
                s = fabs(v1) + fabs(v2) + fabs(v3);
                v1 /= s; v2 /= s; v3 /= s;
                v[0] = v1; v[1] = v2; v[2] = v3;
                if (m == l) break;
                {
                    double h00 = H(m - 1, m - 1), h10 = H(m, m - 1), hm = H(m, m),
                        hn = H(m + 1, m + 1);
                    if (fabs(h10) * (fabs(v2) + fabs(v3))
                        <= LA_ULP * fabs(v1) * (fabs(h00) + fabs(hm) + fabs(hn))) break;
                }
                m--;
            }
            for (k = m; k < i; k++) {
                double t1, t2, t3, vv2, vv3, sm;
                nr = i - k + 1 < 3 ? i - k + 1 : 3;
                if (k > m) for (j = 0; j < nr; j++) v[j] = H(k + j, k - 1);
                t1 = la_larfg(&v[0], v + 1, nr - 1);
                if (k > m) {
                    H(k, k - 1) = v[0];
                    H(k + 1, k - 1) = 0.0;
                    if (k < i - 1) H(k + 2, k - 1) = 0.0;
                } else if (m > l) H(k, k - 1) = H(k, k - 1) * (1.0 - t1);
                vv2 = v[1]; t2 = t1 * vv2;
                if (nr == 3) {
                    long jm = k + 3 < i ? k + 3 : i;
                    vv3 = v[2]; t3 = t1 * vv3;
                    for (j = k; j <= i2; j++) {
                        sm = H(k, j) + vv2 * H(k + 1, j) + vv3 * H(k + 2, j);
                        H(k, j) -= sm * t1; H(k + 1, j) -= sm * t2; H(k + 2, j) -= sm * t3;
                    }
                    for (j = i1; j <= jm; j++) {
                        sm = H(j, k) + vv2 * H(j, k + 1) + vv3 * H(j, k + 2);
                        H(j, k) -= sm * t1; H(j, k + 1) -= sm * t2; H(j, k + 2) -= sm * t3;
                    }
                    if (wantz)
                        for (j = ilo; j <= ihi; j++) {
                            sm = Z(j, k) + vv2 * Z(j, k + 1) + vv3 * Z(j, k + 2);
                            Z(j, k) -= sm * t1; Z(j, k + 1) -= sm * t2; Z(j, k + 2) -= sm * t3;
                        }
                } else if (nr == 2) {
                    for (j = k; j <= i2; j++) {
                        sm = H(k, j) + vv2 * H(k + 1, j);
                        H(k, j) -= sm * t1; H(k + 1, j) -= sm * t2;
                    }
                    for (j = i1; j <= i; j++) {
                        sm = H(j, k) + vv2 * H(j, k + 1);
                        H(j, k) -= sm * t1; H(j, k + 1) -= sm * t2;
                    }
                    if (wantz)
                        for (j = ilo; j <= ihi; j++) {
                            sm = Z(j, k) + vv2 * Z(j, k + 1);
                            Z(j, k) -= sm * t1; Z(j, k + 1) -= sm * t2;
                        }
                }
            }
        }
        if (!conv) return 0;
        if (l == i) { wr[i] = H(i, i); wi[i] = 0.0; }
        else {
            double a = H(i - 1, i - 1), b = H(i - 1, i), c = H(i, i - 1), d = H(i, i), cs, sn;
            la_lanv2(&a, &b, &c, &d, &rt1r, &rt1i, &rt2r, &rt2i, &cs, &sn);
            H(i - 1, i - 1) = a; H(i - 1, i) = b; H(i, i - 1) = c; H(i, i) = d;
            wr[i - 1] = rt1r; wi[i - 1] = rt1i; wr[i] = rt2r; wi[i] = rt2i;
            if (wantt) {
                for (j = i + 1; j <= i2; j++) {
                    double xx = H(i - 1, j), yy = H(i, j);
                    H(i - 1, j) = cs * xx + sn * yy; H(i, j) = cs * yy - sn * xx;
                }
                for (j = i1; j < i - 1; j++) {
                    double xx = H(j, i - 1), yy = H(j, i);
                    H(j, i - 1) = cs * xx + sn * yy; H(j, i) = cs * yy - sn * xx;
                }
            }
            if (wantz)
                for (j = ilo; j <= ihi; j++) {
                    double xx = Z(j, i - 1), yy = Z(j, i);
                    Z(j, i - 1) = cs * xx + sn * yy; Z(j, i) = cs * yy - sn * xx;
                }
        }
        kdefl = 0;
        i = l - 1;
    }
#undef H
#undef Z
    return 1;
}
/* complex helpers (re, im pairs) for the eigenvector back-substitution */
static double la_cabs(double r, double i) { return n_hypot(r, i); }
static void la_cdiv(double ar, double ai, double br, double bi, double *cr, double *ci)
{
    double r, d;
    if (fabs(br) >= fabs(bi)) {
        if (br == 0.0 && bi == 0.0) { *cr = ar / br; *ci = ai / br; return; }
        r = bi / br; d = br + bi * r;
        *cr = (ar + ai * r) / d; *ci = (ai - ar * r) / d;
    } else {
        r = br / bi; d = br * r + bi;
        *cr = (ar * r + ai) / d; *ci = (ai * r - ar) / d;
    }
}
/* 2x2 solve with pivoting, real */
static void la_solve2(double a11, double a12, double a21, double a22, double b1, double b2,
    double smin, double *x1, double *x2)
{
    double f, d, t;
    if (fabs(a21) > fabs(a11)) {
        t = a11; a11 = a21; a21 = t;
        t = a12; a12 = a22; a22 = t;
        t = b1; b1 = b2; b2 = t;
    }
    if (fabs(a11) < smin) a11 = smin;
    f = a21 / a11;
    d = a22 - f * a12;
    if (fabs(d) < smin) d = smin;
    *x2 = (b2 - f * b1) / d;
    *x1 = (b1 - a12 * *x2) / a11;
}
/* 2x2 solve with pivoting, complex; a = {re, im} x 4 in order a11 a12 a21 a22, b = b1 b2 */
static void la_csolve2(const double *a, const double *b, double smin, double *x)
{
    double a11r = a[0], a11i = a[1], a12r = a[2], a12i = a[3], a21r = a[4], a21i = a[5];
    double a22r = a[6], a22i = a[7], b1r = b[0], b1i = b[1], b2r = b[2], b2i = b[3], t;
    double fr, fi, dr, di, x2r, x2i, tr, ti;
    if (la_cabs(a21r, a21i) > la_cabs(a11r, a11i)) {
        t = a11r; a11r = a21r; a21r = t; t = a11i; a11i = a21i; a21i = t;
        t = a12r; a12r = a22r; a22r = t; t = a12i; a12i = a22i; a22i = t;
        t = b1r; b1r = b2r; b2r = t; t = b1i; b1i = b2i; b2i = t;
    }
    if (la_cabs(a11r, a11i) < smin) { a11r = smin; a11i = 0.0; }
    la_cdiv(a21r, a21i, a11r, a11i, &fr, &fi);
    dr = a22r - (fr * a12r - fi * a12i); di = a22i - (fr * a12i + fi * a12r);
    if (la_cabs(dr, di) < smin) { dr = smin; di = 0.0; }
    tr = b2r - (fr * b1r - fi * b1i); ti = b2i - (fr * b1i + fi * b1r);
    la_cdiv(tr, ti, dr, di, &x2r, &x2i);
    tr = b1r - (a12r * x2r - a12i * x2i); ti = b1i - (a12r * x2i + a12i * x2r);
    la_cdiv(tr, ti, a11r, a11i, &x[0], &x[1]);
    x[2] = x2r; x[3] = x2i;
}
/* dtrevc: right eigenvectors of quasi-triangular t, back-transformed with vr (in place) */
static void la_trevc(const double *t, double *vr, long n)
{
    double smlnum = LA_SAFMIN * ((double)n / LA_ULP);
    double *w = (double *)xmalloc(sizeof(double) * (size_t)(2 * n + 2));
    double *wim = w + n;
    long ki = n - 1, j, k, r;
#define T(a, b) t[(a) * n + (b)]
    while (ki >= 0) {
        if (!(ki > 0 && T(ki, ki - 1) != 0.0)) {
            double wr = T(ki, ki), smin = la_max(LA_ULP * fabs(wr), smlnum), emax, rm, s;
            for (k = 0; k < n; k++) w[k] = 0.0;
            w[ki] = 1.0;
            for (k = 0; k < ki; k++) w[k] = -T(k, ki);
            j = ki - 1;
            while (j >= 0) {
                if (j > 0 && T(j, j - 1) != 0.0) {
                    double x1, x2, big;
                    la_solve2(T(j - 1, j - 1) - wr, T(j - 1, j), T(j, j - 1), T(j, j) - wr,
                        w[j - 1], w[j], smin, &x1, &x2);
                    big = la_max(fabs(x1), fabs(x2));
                    if (big > 1e100) {
                        for (k = 0; k <= ki; k++) w[k] /= big;
                        x1 /= big; x2 /= big;
                    }
                    w[j - 1] = x1; w[j] = x2;
                    for (k = 0; k < j - 1; k++) w[k] -= x1 * T(k, j - 1) + x2 * T(k, j);
                    j -= 2;
                } else {
                    double den = T(j, j) - wr, x, big;
                    if (fabs(den) < smin) den = smin;
                    x = w[j] / den;
                    if (fabs(x) > 1e100) {
                        big = fabs(x);
                        for (k = 0; k <= ki; k++) w[k] /= big;
                        x /= big;
                    }
                    w[j] = x;
                    for (k = 0; k < j; k++) w[k] -= x * T(k, j);
                    j -= 1;
                }
            }
            for (r = 0; r < n; r++) {
                double *row = vr + r * n;
                s = w[ki] * row[ki];
                for (k = 0; k < ki; k++) s += row[k] * w[k];
                row[ki] = s;
            }
            emax = 0.0;
            for (r = 0; r < n; r++) emax = la_max(emax, fabs(vr[r * n + ki]));
            rm = 1.0 / emax;
            for (r = 0; r < n; r++) vr[r * n + ki] *= rm;
            ki -= 1;
        } else {
            double wr = T(ki, ki), wi = sqrt(fabs(T(ki, ki - 1))) * sqrt(fabs(T(ki - 1, ki)));
            double smin = la_max(LA_ULP * (fabs(wr) + fabs(wi)), smlnum), emax, rm;
            for (k = 0; k < n; k++) { w[k] = 0.0; wim[k] = 0.0; }
            if (fabs(T(ki - 1, ki)) >= fabs(T(ki, ki - 1))) {
                w[ki - 1] = 1.0; wim[ki - 1] = 0.0;
                w[ki] = 0.0; wim[ki] = wi / T(ki - 1, ki);
            } else {
                w[ki - 1] = -wi / T(ki, ki - 1); wim[ki - 1] = 0.0;
                w[ki] = 0.0; wim[ki] = 1.0;
            }
            for (k = 0; k < ki - 1; k++) {
                w[k] = -w[ki - 1] * T(k, ki - 1);
                wim[k] = -wim[ki] * T(k, ki);
            }
            j = ki - 2;
            while (j >= 0) {
                if (j > 0 && T(j, j - 1) != 0.0) {
                    double a[8], b[4], x[4], big;
                    a[0] = T(j - 1, j - 1) - wr; a[1] = -wi;
                    a[2] = T(j - 1, j); a[3] = 0.0;
                    a[4] = T(j, j - 1); a[5] = 0.0;
                    a[6] = T(j, j) - wr; a[7] = -wi;
                    b[0] = w[j - 1]; b[1] = wim[j - 1]; b[2] = w[j]; b[3] = wim[j];
                    la_csolve2(a, b, smin, x);
                    big = la_max(la_cabs(x[0], x[1]), la_cabs(x[2], x[3]));
                    if (big > 1e100) {
                        for (k = 0; k <= ki; k++) { w[k] /= big; wim[k] /= big; }
                        for (k = 0; k < 4; k++) x[k] /= big;
                    }
                    w[j - 1] = x[0]; wim[j - 1] = x[1]; w[j] = x[2]; wim[j] = x[3];
                    for (k = 0; k < j - 1; k++) {
                        w[k] = w[k] - x[0] * T(k, j - 1) - x[2] * T(k, j);
                        wim[k] = wim[k] - x[1] * T(k, j - 1) - x[3] * T(k, j);
                    }
                    j -= 2;
                } else {
                    double dr = T(j, j) - wr, di = -wi, xr, xi, big;
                    if (la_cabs(dr, di) < smin) { dr = smin; di = 0.0; }
                    la_cdiv(w[j], wim[j], dr, di, &xr, &xi);
                    big = la_cabs(xr, xi);
                    if (big > 1e100) {
                        for (k = 0; k <= ki; k++) { w[k] /= big; wim[k] /= big; }
                        xr /= big; xi /= big;
                    }
                    w[j] = xr; wim[j] = xi;
                    for (k = 0; k < j; k++) {
                        w[k] = w[k] - xr * T(k, j);
                        wim[k] = wim[k] - xi * T(k, j);
                    }
                    j -= 1;
                }
            }
            emax = 0.0;
            for (r = 0; r < n; r++) {
                double *row = vr + r * n, sre = w[ki - 1] * row[ki - 1], sim = wim[ki] * row[ki];
                for (k = 0; k < ki - 1; k++) { sre += row[k] * w[k]; sim += row[k] * wim[k]; }
                row[ki - 1] = sre; row[ki] = sim;
                emax = la_max(emax, fabs(sre) + fabs(sim));
            }
            rm = 1.0 / emax;
            for (r = 0; r < n; r++) { vr[r * n + ki - 1] *= rm; vr[r * n + ki] *= rm; }
            ki -= 2;
        }
    }
#undef T
    free(w);
}
/* dgeev on the row-major n x n matrix a (destroyed): wr, wi, and vr (n x n, or NULL).
   Returns 0 on success, 1 when the QR iteration did not converge. */
static int la_geev(double *a, long n, double *wr, double *wi, double *vr)
{
    long ilo, ihi, i, j, k, r;
    double *scale, *refl = NULL, *tau = NULL;
    int ok;
    if (n == 0) return 0;
    scale = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
    la_gebal(a, n, &ilo, &ihi, scale);
    refl = (double *)xmalloc(sizeof(double) * (size_t)(n * n + 1));
    tau = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
    la_gehrd(a, n, ilo, ihi, refl, tau);
    if (vr) la_orghr(vr, n, ilo, ihi, refl, tau);
    free(refl); free(tau);
    for (i = 0; i < n; i++)
        if (i < ilo || i > ihi) { wr[i] = a[i * n + i]; wi[i] = 0.0; }
    ok = la_lahqr(a, vr, n, ilo, ihi, vr != NULL, vr != NULL, wr, wi);
    if (!ok) { free(scale); return 1; }
    if (!vr) { free(scale); return 0; }
    for (i = 0; i < n; i++)
        for (j = i + 2; j < n; j++) a[j * n + i] = 0.0;
    la_trevc(a, vr, n);
    for (i = ilo; i <= ihi; i++) {
        double s = scale[i];
        if (ilo != ihi && s != 1.0)
            for (j = 0; j < n; j++) vr[i * n + j] *= s;
    }
    for (j = 0; j < n; j++) {
        i = j;
        if (i >= ilo && i <= ihi) continue;
        if (i < ilo) i = ilo - 1 - j;
        k = (long)scale[i];
        if (k != i) la_swaprows(vr, n, i, k);
    }
    i = 0;
    while (i < n) {
        if (wi[i] == 0.0) {
            double nr = 0.0, sc;
            for (r = 0; r < n; r++) nr = n_hypot(nr, vr[r * n + i]);
            sc = 1.0 / nr;
            for (r = 0; r < n; r++) vr[r * n + i] *= sc;
            i += 1;
        } else {
            double n1 = 0.0, n2 = 0.0, sc, bv = -1.0, f, g, cs, sn, d;
            long big = 0;
            for (r = 0; r < n; r++) {
                n1 = n_hypot(n1, vr[r * n + i]);
                n2 = n_hypot(n2, vr[r * n + i + 1]);
            }
            sc = 1.0 / la_lapy2(n1, n2);
            for (r = 0; r < n; r++) {
                double q;
                vr[r * n + i] *= sc; vr[r * n + i + 1] *= sc;
                q = vr[r * n + i] * vr[r * n + i] + vr[r * n + i + 1] * vr[r * n + i + 1];
                if (q > bv) { bv = q; big = r; }
            }
            f = vr[big * n + i]; g = vr[big * n + i + 1];
            if (g == 0.0) { cs = 1.0; sn = 0.0; }
            else if (f == 0.0) { cs = 0.0; sn = la_sgn(g); }
            else {
                d = sqrt(f * f + g * g);
                cs = fabs(f) / d;
                sn = g / (fabs(d) * la_sgn(f));
            }
            for (r = 0; r < n; r++) {
                double xx = vr[r * n + i], yy = vr[r * n + i + 1];
                vr[r * n + i] = cs * xx + sn * yy;
                vr[r * n + i + 1] = cs * yy - sn * xx;
            }
            vr[big * n + i + 1] = 0.0;
            i += 2;
        }
    }
    free(scale);
    return 0;
}
static int eig_has_complex(const double *wi, long n)
{
    long i;
    for (i = 0; i < n; i++) if (wi[i] != 0.0) return 1;
    return 0;
}
static void eig_check_finite(const Nd *a, long n)
{
    long i;
    for (i = 0; i < n * n; i++)
        if (a->dv[i] - a->dv[i] != 0.0)
            throw_new(cls_LinAlgError, "Array must not contain infs or NaNs");
}
static Nd *eig_cvals(const double *wr, const double *wi, long n)
{
    Nd *w = nd_new1(ND_COMPLEX, n);
    long i;
    for (i = 0; i < n; i++) { w->dv[2 * i] = wr[i]; w->dv[2 * i + 1] = wi[i]; }
    return w;
}
/* Nd float n x n (not modified) -> eigenvalues (float or complex Nd) */
static Nd *eigvals_nd(Nd *a, long n)
{
    Nd *w, *r;
    double *wi, *m;
    int bad;
    eig_check_finite(a, n);
    w = vec_new(n);
    wi = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
    m = (double *)xmalloc(sizeof(double) * (size_t)(n * n + 1));
    memcpy(m, a->dv, sizeof(double) * (size_t)(n * n));
    bad = la_geev(m, n, w->dv, wi, NULL);
    free(m);
    if (bad) { free(wi); throw_new(cls_LinAlgError, "Eigenvalues did not converge"); }
    r = eig_has_complex(wi, n) ? eig_cvals(w->dv, wi, n) : w;
    free(wi);
    return r;
}
BI(la_eigvals)
{
    long n;
    Nd *a;
    need(x, 1, 1, "eigvals");
    a = la_sq(A(0), &n);
    return ndv(eigvals_nd(a, n));
}
BI(la_eig)
{
    long n, i, j;
    Nd *a, *w, *v;
    double *wi, *m;
    Value tv[2];
    int bad;
    need(x, 1, 1, "eig");
    a = la_sq(A(0), &n);
    eig_check_finite(a, n);
    w = vec_new(n);
    v = mat_new(n, n);
    wi = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
    m = (double *)xmalloc(sizeof(double) * (size_t)(n * n + 1));
    memcpy(m, a->dv, sizeof(double) * (size_t)(n * n));
    bad = la_geev(m, n, w->dv, wi, v->dv);
    free(m);
    if (bad) { free(wi); throw_new(cls_LinAlgError, "Eigenvalues did not converge"); }
    if (eig_has_complex(wi, n)) {
        Nd *cw = eig_cvals(w->dv, wi, n), *cv = nd_new(ND_COMPLEX, 2, v->shape);
        for (j = 0; j < n;) {
            if (wi[j] == 0.0) {
                for (i = 0; i < n; i++) {
                    cv->dv[2 * (i * n + j)] = v->dv[i * n + j];
                    cv->dv[2 * (i * n + j) + 1] = 0.0;
                }
                j++;
            } else {
                for (i = 0; i < n; i++) {
                    cv->dv[2 * (i * n + j)] = v->dv[i * n + j];
                    cv->dv[2 * (i * n + j) + 1] = v->dv[i * n + j + 1];
                    cv->dv[2 * (i * n + j + 1)] = v->dv[i * n + j];
                    cv->dv[2 * (i * n + j + 1) + 1] = -v->dv[i * n + j + 1];
                }
                j += 2;
            }
        }
        free(wi);
        tv[0] = ndv(cw); tv[1] = ndv(cv);
        return mktuple_from(tv, 2);
    }
    free(wi);
    tv[0] = ndv(w); tv[1] = ndv(v);
    return mktuple_from(tv, 2);
}

/* ---- symmetric eigenproblem: the LAPACK dsyevd path for n <= 25 (dsytd2, dsteqr, dormtr) ----
   Same operation order as the reference routines, so eigenvector signs match numpy. */
static double la_lartg_c, la_lartg_s;
/* dlartg (LAPACK 3.10): rotation with c*f + s*g = r, -s*f + c*g = 0; sets la_lartg_c/s */
static double la_lartg(double f, double g)
{
    double f1 = fabs(f), g1 = fabs(g), d, r, u, fs, gs;
    double safmin = LA_SAFMIN, safmax = 1.0 / LA_SAFMIN, rtmin = sqrt(safmin);
    double rtmax = sqrt(safmax / 2.0);
    if (g == 0.0) { la_lartg_c = 1.0; la_lartg_s = 0.0; return f; }
    if (f == 0.0) { la_lartg_c = 0.0; la_lartg_s = la_sgn(g); return g1; }
    if (f1 > rtmin && f1 < rtmax && g1 > rtmin && g1 < rtmax) {
        d = sqrt(f * f + g * g);
        la_lartg_c = f1 / d;
        r = d * la_sgn(f);
        la_lartg_s = g / r;
        return r;
    }
    u = la_min(safmax, la_max(safmin, la_max(f1, g1)));
    fs = f / u; gs = g / u;
    d = sqrt(fs * fs + gs * gs);
    la_lartg_c = fabs(fs) / d;
    r = d * la_sgn(f);
    la_lartg_s = gs / r;
    return r * u;
}
/* dlaev2: eigen decomposition of [[a, b], [b, c]] */
static void la_laev2(double a, double b, double c, double *rt1, double *rt2, double *cs1,
    double *sn1)
{
    double sm = a + c, df = a - c, adf = fabs(df), tb = b + b, ab = fabs(tb);
    double acmx, acmn, rt, cs, ct, tn, acs;
    int sgn1, sgn2;
    if (fabs(a) > fabs(c)) { acmx = a; acmn = c; } else { acmx = c; acmn = a; }
    if (adf > ab) { double q = ab / adf; rt = adf * sqrt(1.0 + q * q); }
    else if (adf < ab) { double q = adf / ab; rt = ab * sqrt(1.0 + q * q); }
    else rt = ab * sqrt(2.0);
    if (sm < 0.0) {
        *rt1 = 0.5 * (sm - rt); sgn1 = -1;
        *rt2 = (acmx / *rt1) * acmn - (b / *rt1) * b;
    } else if (sm > 0.0) {
        *rt1 = 0.5 * (sm + rt); sgn1 = 1;
        *rt2 = (acmx / *rt1) * acmn - (b / *rt1) * b;
    } else { *rt1 = 0.5 * rt; *rt2 = -0.5 * rt; sgn1 = 1; }
    if (df >= 0.0) { cs = df + rt; sgn2 = 1; } else { cs = df - rt; sgn2 = -1; }
    acs = fabs(cs);
    if (acs > ab) {
        ct = -tb / cs;
        *sn1 = 1.0 / sqrt(1.0 + ct * ct);
        *cs1 = ct * *sn1;
    } else if (ab == 0.0) { *cs1 = 1.0; *sn1 = 0.0; }
    else {
        tn = -cs / tb;
        *cs1 = 1.0 / sqrt(1.0 + tn * tn);
        *sn1 = tn * *cs1;
    }
    if (sgn1 == sgn2) { tn = *cs1; *cs1 = -*sn1; *sn1 = tn; }
}
/* dlasr('R', 'V', dir): rotations j = 1..mm-1 on columns col0+j-1, col0+j of z (n x n) */
static void la_lasr(double *z, long n, long col0, long mm, const double *c, const double *s,
    int backward)
{
    long j, i, jj;
    for (jj = 1; jj < mm; jj++) {
        double ct, st;
        j = backward ? mm - jj : jj;
        ct = c[j - 1]; st = s[j - 1];
        if (ct != 1.0 || st != 0.0)
            for (i = 0; i < n; i++) {
                double *p = z + i * n + (col0 - 1 + j - 1);
                double temp = p[1];
                p[1] = ct * temp - st * p[0];
                p[0] = st * temp + ct * p[0];
            }
    }
}
/* dsteqr (QL/QR implicit) on d[0..n-1], e[0..n-2]; z (n x n, row-major) is updated when not
   NULL (initialise it with the identity first).  Returns 0, or 1 on non-convergence. */
static int la_steqr(double *d0, double *e0, long n, double *z)
{
    double eps = DBL_EPSILON * 0.5, eps2, safmin = LA_SAFMIN, ssfmax, ssfmin, anorm, scl = 1.0;
    double *wk = NULL, *wc, *ws, p, g, r, f, b, c, s, tst, rt1, rt2;
    double *d = d0 - 1, *e = e0 - 1;       /* 1-based views */
    long l, l1, lend, lsv, lendsv, m, i, j, k, jtot = 0, nmaxit, nm1, mm, lm1, mm1;
    int iscale, bad = 0;
    if (n <= 1) return 0;
    eps2 = eps * eps;
    ssfmax = sqrt(1.0 / safmin) / 3.0;
    ssfmin = sqrt(safmin) / eps2;
    wk = (double *)xmalloc(sizeof(double) * (size_t)(2 * n + 2));
    wc = wk - 1; ws = wk + n - 2;         /* wc[i] = work(i), ws[i] = work(n-1+i) */
    nmaxit = n * 30;
    l1 = 1; nm1 = n - 1;
l10:
    if (l1 > n) goto l160;
    if (l1 > 1) e[l1 - 1] = 0.0;
    m = n;
    if (l1 <= nm1) {
        for (m = l1; m <= nm1; m++) {
            tst = fabs(e[m]);
            if (tst == 0.0) goto l30;
            if (tst <= (sqrt(fabs(d[m])) * sqrt(fabs(d[m + 1]))) * eps) { e[m] = 0.0; goto l30; }
        }
        m = n;
    }
l30:
    l = l1; lsv = l; lend = m; lendsv = lend; l1 = m + 1;
    if (lend == l) goto l10;
    anorm = 0.0;
    for (i = l; i <= lend; i++) {
        if (fabs(d[i]) > anorm) anorm = fabs(d[i]);
        if (i < lend && fabs(e[i]) > anorm) anorm = fabs(e[i]);
    }
    iscale = 0;
    if (anorm == 0.0) goto l10;
    if (anorm > ssfmax) {
        iscale = 1; scl = ssfmax / anorm;
        for (i = l; i <= lend; i++) d[i] *= scl;
        for (i = l; i < lend; i++) e[i] *= scl;
    } else if (anorm < ssfmin) {
        iscale = 2; scl = ssfmin / anorm;
        for (i = l; i <= lend; i++) d[i] *= scl;
        for (i = l; i < lend; i++) e[i] *= scl;
    }
    if (fabs(d[lend]) < fabs(d[l])) { lend = lsv; l = lendsv; }
    if (lend > l) {
        /* QL iteration */
l40:
        m = lend;
        if (l != lend)
            for (m = l; m <= lend - 1; m++) {
                tst = fabs(e[m]) * fabs(e[m]);
                if (tst <= (eps2 * fabs(d[m])) * fabs(d[m + 1]) + safmin) goto l60;
            }
        m = lend;
l60:
        if (m < lend) e[m] = 0.0;
        p = d[l];
        if (m == l) goto l80;
        if (m == l + 1) {
            la_laev2(d[l], e[l], d[l + 1], &rt1, &rt2, &c, &s);
            if (z) {
                wc[l] = c; ws[l] = s;
                la_lasr(z, n, l, 2, &wc[l], &ws[l], 1);
            }
            d[l] = rt1; d[l + 1] = rt2; e[l] = 0.0;
            l += 2;
            if (l <= lend) goto l40;
            goto l140;
        }
        if (jtot == nmaxit) goto l140;
        jtot++;
        g = (d[l + 1] - p) / (2.0 * e[l]);
        r = la_lapy2(g, 1.0);
        g = d[m] - p + (e[l] / (g + r * la_sgn(g)));
        s = 1.0; c = 1.0; p = 0.0;
        mm1 = m - 1;
        for (i = mm1; i >= l; i--) {
            f = s * e[i];
            b = c * e[i];
            r = la_lartg(g, f); c = la_lartg_c; s = la_lartg_s;
            if (i != m - 1) e[i + 1] = r;
            g = d[i + 1] - p;
            r = (d[i] - g) * s + 2.0 * c * b;
            p = s * r;
            d[i + 1] = g + p;
            g = c * r - b;
            if (z) { wc[i] = c; ws[i] = -s; }
        }
        if (z) {
            mm = m - l + 1;
            la_lasr(z, n, l, mm, &wc[l], &ws[l], 1);
        }
        d[l] = d[l] - p;
        e[l] = g;
        goto l40;
l80:
        d[l] = p;
        l++;
        if (l <= lend) goto l40;
        goto l140;
    } else {
        /* QR iteration */
l90:
        m = lend;
        if (l != lend)
            for (m = l; m >= lend + 1; m--) {
                tst = fabs(e[m - 1]) * fabs(e[m - 1]);
                if (tst <= (eps2 * fabs(d[m])) * fabs(d[m - 1]) + safmin) goto l110;
            }
        m = lend;
l110:
        if (m > lend) e[m - 1] = 0.0;
        p = d[l];
        if (m == l) goto l130;
        if (m == l - 1) {
            la_laev2(d[l - 1], e[l - 1], d[l], &rt1, &rt2, &c, &s);
            if (z) {
                wc[m] = c; ws[m] = s;
                la_lasr(z, n, l - 1, 2, &wc[m], &ws[m], 0);
            }
            d[l - 1] = rt1; d[l] = rt2; e[l - 1] = 0.0;
            l -= 2;
            if (l >= lend) goto l90;
            goto l140;
        }
        if (jtot == nmaxit) goto l140;
        jtot++;
        g = (d[l - 1] - p) / (2.0 * e[l - 1]);
        r = la_lapy2(g, 1.0);
        g = d[m] - p + (e[l - 1] / (g + r * la_sgn(g)));
        s = 1.0; c = 1.0; p = 0.0;
        lm1 = l - 1;
        for (i = m; i <= lm1; i++) {
            f = s * e[i];
            b = c * e[i];
            r = la_lartg(g, f); c = la_lartg_c; s = la_lartg_s;
            if (i != m) e[i - 1] = r;
            g = d[i] - p;
            r = (d[i + 1] - g) * s + 2.0 * c * b;
            p = s * r;
            d[i] = g + p;
            g = c * r - b;
            if (z) { wc[i] = c; ws[i] = s; }
        }
        if (z) {
            mm = l - m + 1;
            la_lasr(z, n, m, mm, &wc[m], &ws[m], 0);
        }
        d[l] = d[l] - p;
        e[lm1] = g;
        goto l90;
l130:
        d[l] = p;
        l--;
        if (l >= lend) goto l90;
        goto l140;
    }
l140:
    if (iscale == 1 || iscale == 2) {
        for (i = lsv; i <= lendsv; i++) d[i] /= scl;
        for (i = lsv; i < lendsv; i++) e[i] /= scl;
    }
    if (jtot < nmaxit) goto l10;
    for (i = 1; i <= n - 1; i++) if (e[i] != 0.0) bad = 1;
    free(wk);
    return bad ? 1 : 0;
l160:
    /* selection sort, swapping eigenvector columns */
    for (j = 2; j <= n; j++) {
        i = j - 1; k = i; p = d[i];
        for (m = j; m <= n; m++) if (d[m] < p) { k = m; p = d[m]; }
        if (k != i) {
            d[k] = d[i]; d[i] = p;
            if (z) {
                long rr;
                for (rr = 0; rr < n; rr++) {
                    double t = z[rr * n + i - 1];
                    z[rr * n + i - 1] = z[rr * n + k - 1];
                    z[rr * n + k - 1] = t;
                }
            }
        }
    }
    free(wk);
    return 0;
}
/* dsytd2 on the lower (upper == 0) or upper triangle of the row-major n x n matrix a:
   a becomes the reflectors, d/e the tridiagonal, tau the scalars */
static void la_sytd2(double *a, long n, int upper, double *d, double *e, double *tau)
{
    long i, j, r, nn;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(2 * n + 2)), *w = v + n + 1;
    double *xb = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
#define A_(r_, c_) a[((r_) - 1) * n + ((c_) - 1)]
    if (n == 0) { free(v); free(xb); return; }
    if (!upper) {
        for (i = 1; i <= n - 1; i++) {
            double alpha = A_(i + 1, i), taui, al2, dt;
            nn = n - i;
            for (r = 0; r < nn - 1; r++) xb[r] = A_(i + 2 + r, i);
            taui = la_larfg(&alpha, xb, nn - 1);
            for (r = 0; r < nn - 1; r++) A_(i + 2 + r, i) = xb[r];
            e[i - 1] = alpha;
            if (taui != 0.0) {
                A_(i + 1, i) = 1.0;
                for (r = 0; r < nn; r++) { v[r] = A_(i + 1 + r, i); w[r] = 0.0; }
                for (j = 0; j < nn; j++) {            /* w = taui * A22 * v (lower) */
                    double t1 = taui * v[j], t2 = 0.0;
                    w[j] += t1 * A_(i + 1 + j, i + 1 + j);
                    for (r = j + 1; r < nn; r++) {
                        w[r] += t1 * A_(i + 1 + r, i + 1 + j);
                        t2 += A_(i + 1 + r, i + 1 + j) * v[r];
                    }
                    w[j] += taui * t2;
                }
                dt = 0.0;
                for (r = 0; r < nn; r++) dt += w[r] * v[r];
                al2 = -0.5 * taui * dt;
                for (r = 0; r < nn; r++) w[r] += al2 * v[r];
                for (j = 0; j < nn; j++)
                    if (v[j] != 0.0 || w[j] != 0.0) {
                        double t1 = -w[j], t2 = -v[j];
                        for (r = j; r < nn; r++)
                            A_(i + 1 + r, i + 1 + j) += v[r] * t1 + w[r] * t2;
                    }
                A_(i + 1, i) = e[i - 1];
            }
            d[i - 1] = A_(i, i);
            tau[i - 1] = taui;
        }
        d[n - 1] = A_(n, n);
    } else {
        for (i = n - 1; i >= 1; i--) {
            double alpha = A_(i, i + 1), taui, al2, dt;
            for (r = 0; r < i - 1; r++) xb[r] = A_(1 + r, i + 1);
            taui = la_larfg(&alpha, xb, i - 1);
            for (r = 0; r < i - 1; r++) A_(1 + r, i + 1) = xb[r];
            e[i - 1] = alpha;
            if (taui != 0.0) {
                A_(i, i + 1) = 1.0;
                for (r = 0; r < i; r++) { v[r] = A_(1 + r, i + 1); w[r] = 0.0; }
                for (j = 0; j < i; j++) {             /* w = taui * A11 * v (upper) */
                    double t1 = taui * v[j], t2 = 0.0;
                    for (r = 0; r < j; r++) {
                        w[r] += t1 * A_(1 + r, 1 + j);
                        t2 += A_(1 + r, 1 + j) * v[r];
                    }
                    w[j] += t1 * A_(1 + j, 1 + j) + taui * t2;
                }
                dt = 0.0;
                for (r = 0; r < i; r++) dt += w[r] * v[r];
                al2 = -0.5 * taui * dt;
                for (r = 0; r < i; r++) w[r] += al2 * v[r];
                for (j = 0; j < i; j++)
                    if (v[j] != 0.0 || w[j] != 0.0) {
                        double t1 = -w[j], t2 = -v[j];
                        for (r = 0; r <= j; r++) A_(1 + r, 1 + j) += v[r] * t1 + w[r] * t2;
                    }
                A_(i, i + 1) = e[i - 1];
            }
            d[i] = A_(i + 1, i + 1);
            tau[i - 1] = taui;
        }
        d[0] = A_(1, 1);
    }
    free(v); free(xb);
#undef A_
}
/* eigen decomposition of the symmetric matrix a (n x n, only the chosen triangle is read,
   destroyed): ascending w, eigenvectors as columns of v (NULL: values only).
   Returns 1 when the QL/QR iteration did not converge. */
static int la_syev(double *a, long n, int upper, double *w, double *v)
{
    double *e, *tau, *u;
    long i, r, c, nn;
    int bad;
    if (n == 0) return 0;
    e = (double *)xmalloc(sizeof(double) * (size_t)(2 * n + 2));
    tau = e + n;
    la_sytd2(a, n, upper, w, e, tau);
    if (v) {
        for (i = 0; i < n * n; i++) v[i] = 0.0;
        for (i = 0; i < n; i++) v[i * n + i] = 1.0;
    }
    bad = la_steqr(w, e, n, v);
    if (!bad && v) {
        u = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
        if (!upper) {
            for (i = n - 1; i >= 1; i--) {
                double t = tau[i - 1];
                if (t == 0.0) continue;
                nn = n - i;
                u[0] = 1.0;
                for (r = 1; r < nn; r++) u[r] = a[(i + 1 + r - 1) * n + (i - 1)];
                for (c = 0; c < n; c++) {
                    double s = 0.0;
                    for (r = 0; r < nn; r++) s += u[r] * v[(i + r) * n + c];
                    s *= t;
                    for (r = 0; r < nn; r++) v[(i + r) * n + c] -= s * u[r];
                }
            }
        } else {
            for (i = 1; i <= n - 1; i++) {
                double t = tau[i - 1];
                if (t == 0.0) continue;
                for (r = 0; r < i - 1; r++) u[r] = a[r * n + i];
                u[i - 1] = 1.0;
                for (c = 0; c < n; c++) {
                    double s = 0.0;
                    for (r = 0; r < i; r++) s += u[r] * v[r * n + c];
                    s *= t;
                    for (r = 0; r < i; r++) v[r * n + c] -= s * u[r];
                }
            }
        }
        free(u);
    }
    free(e);
    return bad;
}
static int uplo_arg(Args *x, int pos)
{
    Value u = np_arg(x, pos, "UPLO");
    if (u.t == T_STR) {
        const char *s = AS_STR(u)->s;
        if (s[0] == 'U' || s[0] == 'u') return 1;
        if (s[0] == 'L' || s[0] == 'l') return 0;
    }
    if (u.t == T_STR || (u.t != T_UNSET && u.t != T_NONE))
        throw_new(cls_ValueError, "UPLO argument must be 'L' or 'U'");
    return 0;
}
static void eigh_check_finite(const Nd *a, long n)
{
    long i;
    for (i = 0; i < n * n; i++)
        if (a->dv[i] - a->dv[i] != 0.0)
            throw_new(cls_LinAlgError, "Eigenvalues did not converge");
}
BI(la_eigvalsh)
{
    long n;
    Nd *a, *w;
    int bad;
    need(x, 1, 2, "eigvalsh");
    a = la_sq(A(0), &n);
    eigh_check_finite(a, n);
    w = vec_new(n);
    bad = la_syev(a->dv, n, uplo_arg(x, 1), w->dv, NULL);
    if (bad) throw_new(cls_LinAlgError, "Eigenvalues did not converge");
    return ndv(w);
}
BI(la_eigh)
{
    long n;
    Nd *a, *w, *v;
    Value tv[2];
    int bad;
    need(x, 1, 2, "eigh");
    a = la_sq(A(0), &n);
    eigh_check_finite(a, n);
    w = vec_new(n);
    v = mat_new(n, n);
    bad = la_syev(a->dv, n, uplo_arg(x, 1), w->dv, v->dv);
    if (bad) throw_new(cls_LinAlgError, "Eigenvalues did not converge");
    tv[0] = ndv(w); tv[1] = ndv(v);
    return mktuple_from(tv, 2);
}

/* ---- SVD: the LAPACK dgesdd pipeline for small matrices (dgeqr2/dgelq2 when much taller or
   wider, dgebd2, dbdsqr via dbdsdc, dormbr) so that singular vector signs match numpy ---- */
static void la_larf_l(double *c, long ldc, long nr, long nc, const double *v, double tau)
{
    long i, j;
    if (tau == 0.0) return;
    for (j = 0; j < nc; j++) {
        double w = 0.0, t;
        for (i = 0; i < nr; i++) w += v[i] * c[i * ldc + j];
        t = -tau * w;
        for (i = 0; i < nr; i++) c[i * ldc + j] += v[i] * t;
    }
}
static void la_larf_r(double *c, long ldc, long nr, long nc, const double *v, double tau)
{
    long i, j;
    if (tau == 0.0) return;
    for (i = 0; i < nr; i++) {
        double w = 0.0;
        for (j = 0; j < nc; j++) w += c[i * ldc + j] * v[j];
        for (j = 0; j < nc; j++) c[i * ldc + j] += w * (-tau * v[j]);
    }
}
/* dgebd2 of the row-major m x n matrix a (ld n) */
static void la_gebd2(double *a, long m, long n, double *d, double *e, double *tauq, double *taup)
{
    long i, k, r = m > n ? m : n;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(r + 1));
    if (m >= n) {
        for (i = 0; i < n; i++) {
            double alpha = a[i * n + i];
            for (k = 0; k < m - i - 1; k++) v[1 + k] = a[(i + 1 + k) * n + i];
            tauq[i] = la_larfg(&alpha, v + 1, m - i - 1);
            for (k = 0; k < m - i - 1; k++) a[(i + 1 + k) * n + i] = v[1 + k];
            d[i] = alpha;
            v[0] = 1.0;
            if (i < n - 1) la_larf_l(a + i * n + i + 1, n, m - i, n - i - 1, v, tauq[i]);
            a[i * n + i] = d[i];
            if (i < n - 1) {
                alpha = a[i * n + i + 1];
                for (k = 0; k < n - i - 2; k++) v[1 + k] = a[i * n + i + 2 + k];
                taup[i] = la_larfg(&alpha, v + 1, n - i - 2);
                for (k = 0; k < n - i - 2; k++) a[i * n + i + 2 + k] = v[1 + k];
                e[i] = alpha;
                v[0] = 1.0;
                la_larf_r(a + (i + 1) * n + i + 1, n, m - i - 1, n - i - 1, v, taup[i]);
                a[i * n + i + 1] = e[i];
            } else taup[i] = 0.0;
        }
    } else {
        for (i = 0; i < m; i++) {
            double alpha = a[i * n + i];
            for (k = 0; k < n - i - 1; k++) v[1 + k] = a[i * n + i + 1 + k];
            taup[i] = la_larfg(&alpha, v + 1, n - i - 1);
            for (k = 0; k < n - i - 1; k++) a[i * n + i + 1 + k] = v[1 + k];
            d[i] = alpha;
            v[0] = 1.0;
            if (i < m - 1) la_larf_r(a + (i + 1) * n + i, n, m - i - 1, n - i, v, taup[i]);
            a[i * n + i] = d[i];
            if (i < m - 1) {
                alpha = a[(i + 1) * n + i];
                for (k = 0; k < m - i - 2; k++) v[1 + k] = a[(i + 2 + k) * n + i];
                tauq[i] = la_larfg(&alpha, v + 1, m - i - 2);
                for (k = 0; k < m - i - 2; k++) a[(i + 2 + k) * n + i] = v[1 + k];
                e[i] = alpha;
                v[0] = 1.0;
                la_larf_l(a + (i + 1) * n + i + 1, n, m - i - 1, n - i - 1, v, tauq[i]);
                a[(i + 1) * n + i] = e[i];
            } else tauq[i] = 0.0;
        }
    }
    free(v);
}
/* C := Q C with Q = H(1)..H(nref); reflector i has its unit entry in row i+off (dormbr 'Q') */
static void la_ormq_left(const double *a, long lda, long nq, long nref, long off, const double *tau,
    double *c, long ldc, long ncols)
{
    long i, k;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(nq + 1));
    for (i = nref - 1; i >= 0; i--) {
        long p = i + off;
        v[0] = 1.0;
        for (k = 1; k < nq - p; k++) v[k] = a[(p + k) * lda + i];
        la_larf_l(c + p * ldc, ldc, nq - p, ncols, v, tau[i]);
    }
    free(v);
}
/* C := C P^T with P = G(1)..G(nref); reflector i has its unit entry in column i+off */
static void la_ormp_right(const double *a, long lda, long nq, long nref, long off,
    const double *tau, double *c, long ldc, long nrows)
{
    long i, k;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(nq + 1));
    for (i = nref - 1; i >= 0; i--) {
        long p = i + off;
        v[0] = 1.0;
        for (k = 1; k < nq - p; k++) v[k] = a[i * lda + p + k];
        la_larf_r(c + p, ldc, nrows, nq - p, v, tau[i]);
    }
    free(v);
}
/* dgeqr2 (m x n, ld n) and dorg2r (q: m x ncol, ld ncol, reflectors already in its lower part) */
static void la_geqr2(double *a, long m, long n, double *tau)
{
    long i, k, kk = m < n ? m : n;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(m + 1));
    for (i = 0; i < kk; i++) {
        double alpha = a[i * n + i];
        for (k = 0; k < m - i - 1; k++) v[1 + k] = a[(i + 1 + k) * n + i];
        tau[i] = la_larfg(&alpha, v + 1, m - i - 1);
        for (k = 0; k < m - i - 1; k++) a[(i + 1 + k) * n + i] = v[1 + k];
        v[0] = 1.0;
        if (i < n - 1) la_larf_l(a + i * n + i + 1, n, m - i, n - i - 1, v, tau[i]);
        a[i * n + i] = alpha;
    }
    free(v);
}
static void la_org2r(double *q, long m, long ncol, long kk, const double *tau)
{
    long i, j, l;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(m + 1));
    for (j = kk; j < ncol; j++) {
        for (l = 0; l < m; l++) q[l * ncol + j] = 0.0;
        q[j * ncol + j] = 1.0;
    }
    for (i = kk - 1; i >= 0; i--) {
        if (i < ncol - 1) {
            q[i * ncol + i] = 1.0;
            for (l = 0; l < m - i; l++) v[l] = q[(i + l) * ncol + i];
            la_larf_l(q + i * ncol + i + 1, ncol, m - i, ncol - i - 1, v, tau[i]);
        }
        for (l = i + 1; l < m; l++) q[l * ncol + i] *= -tau[i];
        q[i * ncol + i] = 1.0 - tau[i];
        for (l = 0; l < i; l++) q[l * ncol + i] = 0.0;
    }
    free(v);
}
/* dgelq2 (m x n, ld n) and dorgl2 (q: nrow x n, reflectors in its upper part) */
static void la_gelq2(double *a, long m, long n, double *tau)
{
    long i, k, kk = m < n ? m : n;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
    for (i = 0; i < kk; i++) {
        double alpha = a[i * n + i];
        for (k = 0; k < n - i - 1; k++) v[1 + k] = a[i * n + i + 1 + k];
        tau[i] = la_larfg(&alpha, v + 1, n - i - 1);
        for (k = 0; k < n - i - 1; k++) a[i * n + i + 1 + k] = v[1 + k];
        v[0] = 1.0;
        if (i < m - 1) la_larf_r(a + (i + 1) * n + i, n, m - i - 1, n - i, v, tau[i]);
        a[i * n + i] = alpha;
    }
    free(v);
}
static void la_orgl2(double *q, long nrow, long n, long kk, const double *tau)
{
    long i, j, l;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
    if (kk < nrow)
        for (j = 0; j < n; j++) {
            for (l = kk; l < nrow; l++) q[l * n + j] = 0.0;
            if (j >= kk && j < nrow) q[j * n + j] = 1.0;
        }
    for (i = kk - 1; i >= 0; i--) {
        if (i < n - 1) {
            if (i < nrow - 1) {
                q[i * n + i] = 1.0;
                for (l = 0; l < n - i; l++) v[l] = q[i * n + i + l];
                la_larf_r(q + (i + 1) * n + i, n, nrow - i - 1, n - i, v, tau[i]);
            }
            for (l = i + 1; l < n; l++) q[i * n + l] *= -tau[i];
        }
        q[i * n + i] = 1.0 - tau[i];
        for (l = 0; l < i; l++) q[i * n + l] = 0.0;
    }
    free(v);
}
/* dlasr('L', 'V', dir) on rows row0.. of z (ld ncols): rotation j mixes rows row0+j-1, row0+j */
static void la_lasr_l(double *z, long ncols, long row0, long mm, const double *c, const double *s,
    int backward)
{
    long j, i, jj;
    for (jj = 1; jj < mm; jj++) {
        double ct, st;
        j = backward ? mm - jj : jj;
        ct = c[j - 1]; st = s[j - 1];
        if (ct != 1.0 || st != 0.0)
            for (i = 0; i < ncols; i++) {
                double *p = z + (row0 + j - 1) * ncols + i;   /* row0 is 0-based here */
                double temp = p[ncols];
                p[ncols] = ct * temp - st * p[0];
                p[0] = st * temp + ct * p[0];
            }
    }
}
static void la_rot_rows(double *z, long ncols, long r1, long r2, double c, double s)
{
    long k;
    for (k = 0; k < ncols; k++) {
        double x = z[r1 * ncols + k], y = z[r2 * ncols + k];
        z[r1 * ncols + k] = c * x + s * y;
        z[r2 * ncols + k] = c * y - s * x;
    }
}
static void la_rot_cols(double *z, long ld, long nrows, long c1, long c2, double c, double s)
{
    long k;
    for (k = 0; k < nrows; k++) {
        double x = z[k * ld + c1], y = z[k * ld + c2];
        z[k * ld + c1] = c * x + s * y;
        z[k * ld + c2] = c * y - s * x;
    }
}
static void la_swap_rows(double *z, long ncols, long r1, long r2)
{
    long k;
    for (k = 0; k < ncols; k++) {
        double t = z[r1 * ncols + k];
        z[r1 * ncols + k] = z[r2 * ncols + k];
        z[r2 * ncols + k] = t;
    }
}
static void la_swap_cols(double *z, long ld, long nrows, long c1, long c2)
{
    long k;
    for (k = 0; k < nrows; k++) {
        double t = z[k * ld + c1];
        z[k * ld + c1] = z[k * ld + c2];
        z[k * ld + c2] = t;
    }
}
static void la_las2(double f, double g, double h, double *ssmin, double *ssmax)
{
    double fa = fabs(f), ga = fabs(g), ha = fabs(h), fhmn = la_min(fa, ha), fhmx = la_max(fa, ha);
    double as, at, au, c, q;
    if (fhmn == 0.0) {
        *ssmin = 0.0;
        if (fhmx == 0.0) *ssmax = ga;
        else {
            q = la_min(fhmx, ga) / la_max(fhmx, ga);
            *ssmax = la_max(fhmx, ga) * sqrt(1.0 + q * q);
        }
    } else if (ga < fhmx) {
        as = 1.0 + fhmn / fhmx; at = (fhmx - fhmn) / fhmx;
        q = ga / fhmx; au = q * q;
        c = 2.0 / (sqrt(as * as + au) + sqrt(at * at + au));
        *ssmin = fhmn * c; *ssmax = fhmx / c;
    } else {
        au = fhmx / ga;
        if (au == 0.0) { *ssmin = (fhmn * fhmx) / ga; *ssmax = ga; }
        else {
            as = 1.0 + fhmn / fhmx; at = (fhmx - fhmn) / fhmx;
            c = 1.0 / (sqrt(1.0 + (as * au) * (as * au)) + sqrt(1.0 + (at * au) * (at * au)));
            *ssmin = (fhmn * c) * au; *ssmin = *ssmin + *ssmin;
            *ssmax = ga / (c + c);
        }
    }
}
static void la_lasv2(double f, double g, double h, double *ssmin, double *ssmax, double *snr,
    double *csr, double *snl, double *csl)
{
    double ft = f, fa = fabs(ft), ht = h, ha = fabs(h), gt, ga, temp, clt, crt, slt, srt, tsign;
    double d, l, m, t, mm, tt, s, r, a;
    int pmax = 1, swap = ha > fa, gasmal;
    if (swap) {
        pmax = 3;
        temp = ft; ft = ht; ht = temp;
        temp = fa; fa = ha; ha = temp;
    }
    gt = g; ga = fabs(gt);
    if (ga == 0.0) {
        *ssmin = ha; *ssmax = fa; clt = 1.0; crt = 1.0; slt = 0.0; srt = 0.0;
    } else {
        gasmal = 1;
        clt = crt = slt = srt = 0.0;
        *ssmin = *ssmax = 0.0;
        if (ga > fa) {
            pmax = 2;
            if ((fa / ga) < DBL_EPSILON * 0.5) {
                gasmal = 0;
                *ssmax = ga;
                if (ha > 1.0) *ssmin = fa / (ga / ha);
                else *ssmin = (fa / ga) * ha;
                clt = 1.0; slt = ht / gt; srt = 1.0; crt = ft / gt;
            }
        }
        if (gasmal) {
            d = fa - ha;
            l = d == fa ? 1.0 : d / fa;
            m = gt / ft;
            t = 2.0 - l;
            mm = m * m; tt = t * t;
            s = sqrt(tt + mm);
            r = l == 0.0 ? fabs(m) : sqrt(l * l + mm);
            a = 0.5 * (s + r);
            *ssmin = ha / a; *ssmax = fa * a;
            if (mm == 0.0) {
                if (l == 0.0) t = fabs(2.0) * la_sgn(ft) * la_sgn(gt);
                else t = gt / (fabs(d) * la_sgn(ft)) + m / t;
            } else t = (m / (s + t) + m / (r + l)) * (1.0 + a);
            l = sqrt(t * t + 4.0);
            crt = 2.0 / l; srt = t / l;
            clt = (crt + srt * m) / a;
            slt = (ht / ft) * srt / a;
        }
    }
    if (swap) { *csl = srt; *snl = crt; *csr = slt; *snr = clt; }
    else { *csl = clt; *snl = slt; *csr = crt; *snr = srt; }
    if (pmax == 1) tsign = la_sgn(*csr) * la_sgn(*csl) * la_sgn(f);
    else if (pmax == 2) tsign = la_sgn(*snr) * la_sgn(*csl) * la_sgn(g);
    else tsign = la_sgn(*snr) * la_sgn(*snl) * la_sgn(h);
    *ssmax = fabs(*ssmax) * la_sgn(tsign);
    *ssmin = fabs(*ssmin) * la_sgn(tsign * la_sgn(f) * la_sgn(h));
}
/* dbdsqr('U') on the n x n upper bidiagonal (d, e): vt (n x n, rows rotated) and u (n x n,
   columns rotated) may be NULL.  Returns 0 or the number of non-converged superdiagonals. */
static int la_bdsqr(long n, double *d0, double *e0, double *vt, double *u)
{
    double eps = DBL_EPSILON * 0.5, unfl = LA_SAFMIN, tolmul, tol, smax, sminoa, mu, thresh;
    double smin = 0.0, shift, sll, r, cs, sn, oldcs, oldsn, f, g, h, cosr, sinr, cosl, sinl;
    double sigmn, sigmx, abss, abse;
    double *d = d0 - 1, *e = e0 - 1, *wk = NULL, *w1, *w2, *w3, *w4;
    long i, j, m, ll, lll, nm1, oldll, oldm, iter, itermax, idir = 0, isub, maxit;
    int info = 0;
    if (n <= 0) return 0;
    nm1 = n - 1;
    wk = (double *)xmalloc(sizeof(double) * (size_t)(4 * n + 4));
    w1 = wk; w2 = wk + nm1; w3 = wk + 2 * nm1; w4 = wk + 3 * nm1;   /* work(1..) views */
    tolmul = la_max(10.0, la_min(100.0, pow(eps, -0.125)));
    tol = tolmul * eps;
    smax = 0.0;
    for (i = 1; i <= n; i++) smax = la_max(smax, fabs(d[i]));
    for (i = 1; i <= n - 1; i++) smax = la_max(smax, fabs(e[i]));
    sminoa = fabs(d[1]);
    if (sminoa != 0.0) {
        mu = sminoa;
        for (i = 2; i <= n; i++) {
            mu = fabs(d[i]) * (mu / (mu + fabs(e[i - 1])));
            sminoa = la_min(sminoa, mu);
            if (sminoa == 0.0) break;
        }
    }
    sminoa = sminoa / sqrt((double)n);
    thresh = la_max(tol * sminoa, 6.0 * ((double)n * ((double)n * unfl)));
    maxit = 6 * n;
    itermax = 0;
    iter = -1; oldll = -1; oldm = -1;
    m = n;
l60:
    if (m <= 1) goto l160;
    if (iter >= n) {
        iter -= n;
        itermax++;
        if (itermax >= maxit) goto l200;
    }
    smax = fabs(d[m]);
    ll = 0;
    for (lll = 1; lll <= m - 1; lll++) {
        ll = m - lll;
        abss = fabs(d[ll]); abse = fabs(e[ll]);
        if (abse <= thresh) goto l80;
        smax = la_max(smax, la_max(abss, abse));
    }
    ll = 0;
    goto l90;
l80:
    e[ll] = 0.0;
    if (ll == m - 1) { m--; goto l60; }
l90:
    ll++;
    if (ll == m - 1) {
        la_lasv2(d[m - 1], e[m - 1], d[m], &sigmn, &sigmx, &sinr, &cosr, &sinl, &cosl);
        d[m - 1] = sigmx; e[m - 1] = 0.0; d[m] = sigmn;
        if (vt) la_rot_rows(vt, n, m - 2, m - 1, cosr, sinr);
        if (u) la_rot_cols(u, n, n, m - 2, m - 1, cosl, sinl);
        m -= 2;
        goto l60;
    }
    if (ll > oldm || m < oldll) {
        if (fabs(d[ll]) >= fabs(d[m])) idir = 1; else idir = 2;
    }
    if (idir == 1) {
        if (fabs(e[m - 1]) <= fabs(tol) * fabs(d[m])) { e[m - 1] = 0.0; goto l60; }
        mu = fabs(d[ll]); smin = mu;
        for (lll = ll; lll <= m - 1; lll++) {
            if (fabs(e[lll]) <= tol * mu) { e[lll] = 0.0; goto l60; }
            mu = fabs(d[lll + 1]) * (mu / (mu + fabs(e[lll])));
            smin = la_min(smin, mu);
        }
    } else {
        if (fabs(e[ll]) <= fabs(tol) * fabs(d[ll])) { e[ll] = 0.0; goto l60; }
        mu = fabs(d[m]); smin = mu;
        for (lll = m - 1; lll >= ll; lll--) {
            if (fabs(e[lll]) <= tol * mu) { e[lll] = 0.0; goto l60; }
            mu = fabs(d[lll]) * (mu / (mu + fabs(e[lll])));
            smin = la_min(smin, mu);
        }
    }
    oldll = ll; oldm = m;
    if ((double)n * tol * (smin / smax) <= la_max(eps, 0.01 * tol)) shift = 0.0;
    else {
        if (idir == 1) { sll = fabs(d[ll]); la_las2(d[m - 1], e[m - 1], d[m], &shift, &r); }
        else { sll = fabs(d[m]); la_las2(d[ll], e[ll], d[ll + 1], &shift, &r); }
        if (sll > 0.0 && (shift / sll) * (shift / sll) < eps) shift = 0.0;
    }
    iter += m - ll;
    if (shift == 0.0) {
        if (idir == 1) {
            cs = 1.0; oldcs = 1.0; oldsn = 0.0; sn = 0.0;
            for (i = ll; i <= m - 1; i++) {
                r = la_lartg(d[i] * cs, e[i]); cs = la_lartg_c; sn = la_lartg_s;
                if (i > ll) e[i - 1] = oldsn * r;
                d[i] = la_lartg(oldcs * r, d[i + 1] * sn); oldcs = la_lartg_c; oldsn = la_lartg_s;
                w1[i - ll] = cs; w2[i - ll] = sn; w3[i - ll] = oldcs; w4[i - ll] = oldsn;
            }
            h = d[m] * cs;
            d[m] = h * oldcs;
            e[m - 1] = h * oldsn;
            if (vt) la_lasr_l(vt, n, ll - 1, m - ll + 1, w1, w2, 0);
            if (u) la_lasr(u, n, ll, m - ll + 1, w3, w4, 0);
            if (fabs(e[m - 1]) <= thresh) e[m - 1] = 0.0;
        } else {
            cs = 1.0; oldcs = 1.0; oldsn = 0.0; sn = 0.0;
            for (i = m; i >= ll + 1; i--) {
                r = la_lartg(d[i] * cs, e[i - 1]); cs = la_lartg_c; sn = la_lartg_s;
                if (i < m) e[i] = oldsn * r;
                d[i] = la_lartg(oldcs * r, d[i - 1] * sn); oldcs = la_lartg_c; oldsn = la_lartg_s;
                w1[i - ll - 1] = cs; w2[i - ll - 1] = -sn; w3[i - ll - 1] = oldcs;
                w4[i - ll - 1] = -oldsn;
            }
            h = d[ll] * cs;
            d[ll] = h * oldcs;
            e[ll] = h * oldsn;
            if (vt) la_lasr_l(vt, n, ll - 1, m - ll + 1, w3, w4, 1);
            if (u) la_lasr(u, n, ll, m - ll + 1, w1, w2, 1);
            if (fabs(e[ll]) <= thresh) e[ll] = 0.0;
        }
    } else {
        if (idir == 1) {
            f = (fabs(d[ll]) - shift) * (la_sgn(d[ll]) + shift / d[ll]);
            g = e[ll];
            for (i = ll; i <= m - 1; i++) {
                r = la_lartg(f, g); cosr = la_lartg_c; sinr = la_lartg_s;
                if (i > ll) e[i - 1] = r;
                f = cosr * d[i] + sinr * e[i];
                e[i] = cosr * e[i] - sinr * d[i];
                g = sinr * d[i + 1];
                d[i + 1] = cosr * d[i + 1];
                r = la_lartg(f, g); cosl = la_lartg_c; sinl = la_lartg_s;
                d[i] = r;
                f = cosl * e[i] + sinl * d[i + 1];
                d[i + 1] = cosl * d[i + 1] - sinl * e[i];
                if (i < m - 1) { g = sinl * e[i + 1]; e[i + 1] = cosl * e[i + 1]; }
                w1[i - ll] = cosr; w2[i - ll] = sinr; w3[i - ll] = cosl; w4[i - ll] = sinl;
            }
            e[m - 1] = f;
            if (vt) la_lasr_l(vt, n, ll - 1, m - ll + 1, w1, w2, 0);
            if (u) la_lasr(u, n, ll, m - ll + 1, w3, w4, 0);
            if (fabs(e[m - 1]) <= thresh) e[m - 1] = 0.0;
        } else {
            f = (fabs(d[m]) - shift) * (la_sgn(d[m]) + shift / d[m]);
            g = e[m - 1];
            for (i = m; i >= ll + 1; i--) {
                r = la_lartg(f, g); cosr = la_lartg_c; sinr = la_lartg_s;
                if (i < m) e[i] = r;
                f = cosr * d[i] + sinr * e[i - 1];
                e[i - 1] = cosr * e[i - 1] - sinr * d[i];
                g = sinr * d[i - 1];
                d[i - 1] = cosr * d[i - 1];
                r = la_lartg(f, g); cosl = la_lartg_c; sinl = la_lartg_s;
                d[i] = r;
                f = cosl * e[i - 1] + sinl * d[i - 1];
                d[i - 1] = cosl * d[i - 1] - sinl * e[i - 1];
                if (i > ll + 1) { g = sinl * e[i - 2]; e[i - 2] = cosl * e[i - 2]; }
                w1[i - ll - 1] = cosr; w2[i - ll - 1] = -sinr; w3[i - ll - 1] = cosl;
                w4[i - ll - 1] = -sinl;
            }
            e[ll] = f;
            if (fabs(e[ll]) <= thresh) e[ll] = 0.0;
            if (vt) la_lasr_l(vt, n, ll - 1, m - ll + 1, w3, w4, 1);
            if (u) la_lasr(u, n, ll, m - ll + 1, w1, w2, 1);
        }
    }
    goto l60;
l160:
    for (i = 1; i <= n; i++) {
        if (d[i] == 0.0) d[i] = 0.0;
        if (d[i] < 0.0) {
            d[i] = -d[i];
            if (vt) for (j = 0; j < n; j++) vt[(i - 1) * n + j] = -vt[(i - 1) * n + j];
        }
    }
    for (i = 1; i <= n - 1; i++) {
        double sm;
        isub = 1; sm = d[1];
        for (j = 2; j <= n + 1 - i; j++) if (d[j] <= sm) { isub = j; sm = d[j]; }
        if (isub != n + 1 - i) {
            d[isub] = d[n + 1 - i]; d[n + 1 - i] = sm;
            if (vt) la_swap_rows(vt, n, isub - 1, n - i);
            if (u) la_swap_cols(u, n, n, isub - 1, n - i);
        }
    }
    free(wk);
    return 0;
l200:
    for (i = 1; i <= n - 1; i++) if (e[i] != 0.0) info++;
    free(wk);
    return info;
}
/* dbdsdc('U'/'L', 'I'/'N') for the small-matrix branch (dlasdq -> dbdsqr), n x n u and vt
   (row-major) or NULL.  Returns non-zero when the iteration did not converge. */
static int la_bdsdc(int lower, long n, double *d0, double *e0, double *u, double *vt)
{
    double *d = d0 - 1, *work = NULL, p, smin;
    long i, j, kk, isub, nm1 = n - 1;
    int info;
    if (n == 0) return 0;
    if (n == 1) {
        if (u) u[0] = la_sgn(d0[0]);
        if (vt) vt[0] = 1.0;
        d0[0] = fabs(d0[0]);
        return 0;
    }
    if (lower) {
        work = (double *)xmalloc(sizeof(double) * (size_t)(2 * n + 2));
        for (i = 1; i <= n - 1; i++) {
            double r = la_lartg(d[i], e0[i - 1]), cs = la_lartg_c, sn = la_lartg_s;
            d[i] = r;
            e0[i - 1] = sn * d[i + 1];
            d[i + 1] = cs * d[i + 1];
            work[i - 1] = cs;
            work[nm1 + i - 1] = -sn;
        }
    }
    if (u) for (i = 0; i < n * n; i++) u[i] = 0.0;
    if (vt) for (i = 0; i < n * n; i++) vt[i] = 0.0;
    for (i = 0; i < n; i++) { if (u) u[i * n + i] = 1.0; if (vt) vt[i * n + i] = 1.0; }
    info = la_bdsqr(n, d0, e0, vt, u);
    /* dlasdq: selection sort (ascending), then dbdsdc: selection sort (descending) */
    for (i = 1; i <= n; i++) {
        isub = i; smin = d[i];
        for (j = i + 1; j <= n; j++) if (d[j] < smin) { isub = j; smin = d[j]; }
        if (isub != i) {
            d[isub] = d[i]; d[i] = smin;
            if (vt) la_swap_rows(vt, n, isub - 1, i - 1);
            if (u) la_swap_cols(u, n, n, isub - 1, i - 1);
        }
    }
    for (j = 2; j <= n; j++) {
        i = j - 1; kk = i; p = d[i];
        for (isub = j; isub <= n; isub++) if (d[isub] > p) { kk = isub; p = d[isub]; }
        if (kk != i) {
            d[kk] = d[i]; d[i] = p;
            if (u) la_swap_cols(u, n, n, i - 1, kk - 1);
            if (vt) la_swap_rows(vt, n, i - 1, kk - 1);
        }
    }
    if (lower && u) la_lasr_l(u, n, 0, n, work, work + nm1, 1);
    free(work);
    return info;
}
/* dgesdd on the row-major m x n matrix a (ld n, destroyed).  s: min(m, n) values; if vec,
   u is m x ucols and vt is vrows x n (ucols = m, vrows = n when full, else min(m, n)).
   Returns non-zero when the iteration did not converge. */
static int la_gesdd(double *a, long m, long n, int full, int vec, double *s, double *u, double *vt)
{
    long mn = m < n ? m : n, i, j, k, ucols = full ? m : mn, vrows = full ? n : mn, mnthr;
    double *d, *e, *tauq, *taup, *tau, *ub = NULL, *vtb = NULL, *q = NULL, *r = NULL;
    int info;
    if (mn == 0) return 0;
    d = (double *)xmalloc(sizeof(double) * (size_t)(4 * mn + 4));
    e = d + mn; tauq = e + mn; taup = tauq + mn;
    tau = (double *)xmalloc(sizeof(double) * (size_t)(mn + 1));
    if (vec) {
        ub = (double *)xmalloc(sizeof(double) * (size_t)(mn * mn + 1));
        vtb = (double *)xmalloc(sizeof(double) * (size_t)(mn * mn + 1));
    }
    if (m >= n) {
        mnthr = (long)((double)n * 11.0 / 6.0);
        if (m >= mnthr) {
            la_geqr2(a, m, n, tau);
            r = (double *)xmalloc(sizeof(double) * (size_t)(n * n + 1));
            for (i = 0; i < n; i++)
                for (j = 0; j < n; j++) r[i * n + j] = j >= i ? a[i * n + j] : 0.0;
            if (vec) {
                q = (double *)xmalloc(sizeof(double) * (size_t)(m * ucols + 1));
                for (i = 0; i < m * ucols; i++) q[i] = 0.0;
                for (i = 0; i < m; i++)
                    for (j = 0; j < n && j < i; j++) q[i * ucols + j] = a[i * n + j];
                la_org2r(q, m, ucols, n, tau);
            }
            la_gebd2(r, n, n, d, e, tauq, taup);
            info = la_bdsdc(0, n, d, e, ub, vtb);
            if (vec && !info) {
                la_ormq_left(r, n, n, n, 0, tauq, ub, n, n);
                la_ormp_right(r, n, n, n - 1, 1, taup, vtb, n, n);
                for (i = 0; i < m; i++)
                    for (j = 0; j < ucols; j++) {
                        if (j < n) {
                            double sum = 0.0;
                            for (k = 0; k < n; k++) sum += q[i * ucols + k] * ub[k * n + j];
                            u[i * ucols + j] = sum;
                        } else u[i * ucols + j] = q[i * ucols + j];
                    }
                memcpy(vt, vtb, sizeof(double) * (size_t)(n * n));
            }
        } else {
            la_gebd2(a, m, n, d, e, tauq, taup);
            info = la_bdsdc(0, n, d, e, ub, vtb);
            if (vec && !info) {
                for (i = 0; i < m * ucols; i++) u[i] = 0.0;
                for (i = 0; i < n; i++) for (j = 0; j < n; j++) u[i * ucols + j] = ub[i * n + j];
                for (i = n; i < ucols; i++) u[i * ucols + i] = 1.0;
                la_ormq_left(a, n, m, n, 0, tauq, u, ucols, ucols);
                la_ormp_right(a, n, n, n - 1, 1, taup, vtb, n, n);
                memcpy(vt, vtb, sizeof(double) * (size_t)(n * n));
            }
        }
    } else {
        mnthr = (long)((double)m * 11.0 / 6.0);
        if (n >= mnthr) {
            la_gelq2(a, m, n, tau);
            r = (double *)xmalloc(sizeof(double) * (size_t)(m * m + 1));
            for (i = 0; i < m; i++)
                for (j = 0; j < m; j++) r[i * m + j] = j <= i ? a[i * n + j] : 0.0;
            if (vec) {
                q = (double *)xmalloc(sizeof(double) * (size_t)(vrows * n + 1));
                for (i = 0; i < vrows * n; i++) q[i] = 0.0;
                for (i = 0; i < m; i++) for (j = i + 1; j < n; j++) q[i * n + j] = a[i * n + j];
                la_orgl2(q, vrows, n, m, tau);
            }
            la_gebd2(r, m, m, d, e, tauq, taup);
            info = la_bdsdc(0, m, d, e, ub, vtb);
            if (vec && !info) {
                la_ormq_left(r, m, m, m, 0, tauq, ub, m, m);
                la_ormp_right(r, m, m, m - 1, 1, taup, vtb, m, m);
                memcpy(u, ub, sizeof(double) * (size_t)(m * m));
                for (i = 0; i < vrows; i++)
                    for (j = 0; j < n; j++) {
                        if (i < m) {
                            double sum = 0.0;
                            for (k = 0; k < m; k++) sum += vtb[i * m + k] * q[k * n + j];
                            vt[i * n + j] = sum;
                        } else vt[i * n + j] = q[i * n + j];
                    }
            }
        } else {
            la_gebd2(a, m, n, d, e, tauq, taup);
            info = la_bdsdc(1, m, d, e, ub, vtb);
            if (vec && !info) {
                for (i = 0; i < vrows * n; i++) vt[i] = 0.0;
                for (i = 0; i < m; i++) for (j = 0; j < m; j++) vt[i * n + j] = vtb[i * m + j];
                for (i = m; i < vrows; i++) vt[i * n + i] = 1.0;
                memcpy(u, ub, sizeof(double) * (size_t)(m * m));
                la_ormq_left(a, n, m, m - 1, 1, tauq, u, m, m);
                la_ormp_right(a, n, n, m, 0, taup, vt, n, vrows);
            }
        }
    }
    for (i = 0; i < mn; i++) s[i] = d[i];
    free(d); free(tau); free(ub); free(vtb); free(q); free(r);
    return info;
}
static void svd_check_finite(const Nd *a)
{
    long i;
    for (i = 0; i < a->size; i++)
        if (a->dv[i] - a->dv[i] != 0.0) throw_new(cls_LinAlgError, "SVD did not converge");
}
/* thin SVD driver: returns u (m x r), s (r), vt (r x n), r = min(m, n) */
static void svd_thin(Nd *a, long m, long n, double **pu, double **ps, double **pvt)
{
    long r = m < n ? m : n;
    double *u, *s, *vt, *w;
    svd_check_finite(a);
    u = (double *)xmalloc(sizeof(double) * (size_t)(m * r + 1));
    s = (double *)xmalloc(sizeof(double) * (size_t)(r + 1));
    vt = (double *)xmalloc(sizeof(double) * (size_t)(r * n + 1));
    w = (double *)xmalloc(sizeof(double) * (size_t)(m * n + 1));
    memcpy(w, a->dv, sizeof(double) * (size_t)(m * n));
    if (la_gesdd(w, m, n, 0, 1, s, u, vt)) {
        free(u); free(s); free(vt); free(w);
        throw_new(cls_LinAlgError, "SVD did not converge");
    }
    free(w);
    *pu = u; *ps = s; *pvt = vt;
}
BI(la_svd)
{
    long m, n, r, i;
    Nd *a, *S;
    int full = 1, cuv = 1;
    Value fv = np_arg(x, 1, "full_matrices"), cv = np_arg(x, 2, "compute_uv");
    Value tv[3];
    double *w;
    need(x, 1, 4, "svd");
    if (np_has(fv)) full = py_truthy(fv);
    if (np_has(cv)) cuv = py_truthy(cv);
    a = la_mat(A(0), &m, &n);
    svd_check_finite(a);
    r = m < n ? m : n;
    S = vec_new(r);
    if (r == 0) {
        if (!cuv) return ndv(S);
        {
            long uc = full ? m : 0, vr = full ? n : 0;
            Nd *U = mat_new(m, uc), *V = mat_new(vr, n);
            for (i = 0; i < uc; i++) U->dv[i * uc + i] = 1.0;
            for (i = 0; i < vr; i++) V->dv[i * n + i] = 1.0;
            tv[0] = ndv(U); tv[1] = ndv(S); tv[2] = ndv(V);
            return mktuple_from(tv, 3);
        }
    }
    w = (double *)xmalloc(sizeof(double) * (size_t)(m * n + 1));
    memcpy(w, a->dv, sizeof(double) * (size_t)(m * n));
    if (!cuv) {
        int bad = la_gesdd(w, m, n, 0, 0, S->dv, NULL, NULL);
        free(w);
        if (bad) throw_new(cls_LinAlgError, "SVD did not converge");
        return ndv(S);
    }
    {
        long uc = full ? m : r, vr = full ? n : r;
        Nd *U = mat_new(m, uc), *V = mat_new(vr, n);
        int bad = la_gesdd(w, m, n, full, 1, S->dv, U->dv, V->dv);
        free(w);
        if (bad) throw_new(cls_LinAlgError, "SVD did not converge");
        tv[0] = ndv(U); tv[1] = ndv(S); tv[2] = ndv(V);
    }
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
        double alpha = a->dv[k * n + k], xn, beta;
        xn = m > k + 1 ? la_nrm2(a->dv + (k + 1) * n + k, m - k - 1, n) : 0.0;
        for (i = 0; i < m; i++) vs[i * K + k] = 0.0;
        if (xn == 0.0) { tau[k] = 0.0; vs[k * K + k] = 1.0; continue; }
        beta = -la_lapy2(alpha, xn) * la_sgn(alpha);
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
