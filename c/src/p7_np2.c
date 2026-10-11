/* =========================================================================
   p7_np2.c   more numpy: elementwise helpers, shape functions, statistics,
              polynomials, histogram, ... (all on contiguous copies)
   ========================================================================= */
static double g_rtol, g_atol;
static int g_eqnan;
typedef double (*DF2)(double, double);

/* elementwise binary op with broadcasting; result dtype outdt */
static Value nd_map2(Nd *a, Nd *b, DF2 f, int outdt)
{
    long rs[ND_MAXD], sa[ND_MAXD], sb[ND_MAXD], idx[ND_MAXD], i, oa = 0, ob = 0;
    int rnd, k;
    Nd *r;
    rnd = nd_bshape(a, b, rs);
    nd_bstrides(a, rnd, sa);
    nd_bstrides(b, rnd, sb);
    r = nd_new(outdt, rnd, rs);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        double v = f(nd_d(a, oa), nd_d(b, ob));
        if (outdt == ND_FLOAT) r->dv[i] = v; else r->iv[i] = v != 0.0;
        nd_odo(idx, rnd, rs, sa, sb, &oa, &ob);
    }
    return nd_ret(r);
}
static double f_atan2(double y, double x) { return atan2(y, x); }
static double f_hypot(double a, double b) { return n_hypot(a, b); }
static double f_fmax(double a, double b) { return a != a ? b : (b != b ? a : (a > b ? a : b)); }
static double f_fmin(double a, double b) { return a != a ? b : (b != b ? a : (a < b ? a : b)); }
static double f_copysign(double a, double b) { return b < 0 || (b == 0 && 1.0 / b < 0) ? -fabs(a) : fabs(a); }
static double f_isclose(double a, double b)
{
    if (a != a || b != b) return g_eqnan && a != a && b != b;
    if (a == b) return 1.0;
    if (a - a != 0.0 || b - b != 0.0) return 0.0;
    return fabs(a - b) <= g_atol + g_rtol * fabs(b);
}
static double f_land(double a, double b) { return a != 0.0 && b != 0.0; }
static double f_lor(double a, double b) { return a != 0.0 || b != 0.0; }
static double f_lxor(double a, double b) { return (a != 0.0) != (b != 0.0); }
#define NDM2(fname, f, dt) \
    BI(fname) { \
        NdTmp ta_, tb_; \
        if (x->n != 2) throw_new(cls_TypeError, "function takes exactly 2 arguments"); \
        return nd_map2(nd_coerce(A(0), &ta_), nd_coerce(A(1), &tb_), f, dt); }
NDM2(np_arctan2, f_atan2, ND_FLOAT)
NDM2(np_hypot, f_hypot, ND_FLOAT)
NDM2(np_copysign, f_copysign, ND_FLOAT)
NDM2(np_logical_and, f_land, ND_BOOL)
NDM2(np_logical_or, f_lor, ND_BOOL)
NDM2(np_logical_xor, f_lxor, ND_BOOL)
BI(np_fmax)
{
    NdTmp ta, tb;
    Nd *a, *b;
    need(x, 2, 2, "fmax");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->dt != ND_FLOAT && b->dt != ND_FLOAT) return nd_binary(OP_MAX, a, b);
    return nd_map2(a, b, f_fmax, ND_FLOAT);
}
BI(np_fmin)
{
    NdTmp ta, tb;
    Nd *a, *b;
    need(x, 2, 2, "fmin");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->dt != ND_FLOAT && b->dt != ND_FLOAT) return nd_binary(OP_MIN, a, b);
    return nd_map2(a, b, f_fmin, ND_FLOAT);
}
BI(np_isclose)
{
    NdTmp ta, tb;
    Value rt = np_arg(x, 2, "rtol"), at = np_arg(x, 3, "atol"), en = np_arg(x, 4, "equal_nan");
    need(x, 2, 5, "isclose");
    g_rtol = np_has(rt) ? to_dbl(rt) : 1e-5;
    g_atol = np_has(at) ? to_dbl(at) : 1e-8;
    g_eqnan = np_has(en) && py_truthy(en);
    return nd_map2(nd_coerce(A(0), &ta), nd_coerce(A(1), &tb), f_isclose, ND_BOOL);
}
BI(np_logical_not)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long i;
    r = nd_new(ND_BOOL, a->nd, a->shape);
    for (i = 0; i < a->size; i++) r->iv[i] = a->dt == ND_FLOAT ? a->dv[i] == 0.0 : a->iv[i] == 0;
    return nd_ret(r);
}

/* more unary functions via a double->double table */
typedef double (*DF1)(double);
static double d_exp2(double v) { return pow(2.0, v); }
static double nd_em1(double v) { return n_expm1(v); }
static double nd_l1p(double v) { return n_log1p(v); }
static double d_cbrt(double v) { return n_cbrt(v); }
static double d_asinh(double v) { return n_asinh(v); }
static double d_acosh(double v) { return n_acosh(v); }
static double d_atanh(double v) { return n_atanh(v); }
static double d_deg2rad(double v) { return v * (3.14159265358979323846 / 180.0); }
static double d_rad2deg(double v) { return v * (180.0 / 3.14159265358979323846); }
static double d_recip(double v) { return 1.0 / v; }
static double d_trunc(double v) { return n_trunc(v); }
static double d_rint(double v) { return nd_rint(v); }
static double d_sinc(double v)
{
    double t = 3.14159265358979323846 * v;
    return v == 0.0 ? 1.0 : sin(t) / t;
}
static Value nd_map1(Nd *a, DF1 f)
{
    Nd *r = nd_new(ND_FLOAT, a->nd, a->shape);
    long i;
    for (i = 0; i < a->size; i++) r->dv[i] = f(nd_d(a, i));
    return nd_ret(r);
}
#define NDD1(fname, f) BI(fname) { NdTmp t_; Nd *a_ = nd_arg0(x, &t_); return nd_map1(a_, f); }
NDD1(np_exp2, d_exp2)
NDD1(np_expm1, nd_em1)
NDD1(np_log1p, nd_l1p)
NDD1(np_cbrt, d_cbrt)
NDD1(np_arcsinh, d_asinh)
NDD1(np_arccosh, d_acosh)
NDD1(np_arctanh, d_atanh)
NDD1(np_deg2rad, d_deg2rad)
NDD1(np_rad2deg, d_rad2deg)
NDD1(np_reciprocal, d_recip)
NDD1(np_trunc, d_trunc)
NDD1(np_rint, d_rint)
NDD1(np_sinc, d_sinc)

BI(np_nan_to_num)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value nv = np_arg(x, 1, "nan"), pv = np_arg(x, 2, "posinf"), mv = np_arg(x, 3, "neginf");
    double nanv = np_has(nv) ? to_dbl(nv) : 0.0;
    double pinf = np_has(pv) ? to_dbl(pv) : DBL_MAX, minf = np_has(mv) ? to_dbl(mv) : -DBL_MAX;
    long i;
    r = nd_astype(a, a->dt);
    if (r->dt == ND_FLOAT)
        for (i = 0; i < r->size; i++) {
            double d = r->dv[i];
            if (d != d) r->dv[i] = nanv;
            else if (d == HUGE_VAL) r->dv[i] = pinf;
            else if (d == -HUGE_VAL) r->dv[i] = minf;
        }
    return nd_ret(r);
}
/* nan-aware reductions: flat or along one axis */
static Nd *nan_fill(Nd *a, double fillv)
{
    Nd *r = nd_astype(a, ND_FLOAT);
    long i;
    for (i = 0; i < r->size; i++) if (r->dv[i] != r->dv[i]) r->dv[i] = fillv;
    return r;
}
static Nd *nan_count(Nd *a)
{
    Nd *r = nd_new(ND_INT, a->nd, a->shape);
    long i;
    for (i = 0; i < a->size; i++) r->iv[i] = a->dt == ND_FLOAT ? a->dv[i] == a->dv[i] : 1;
    return r;
}
BI(np_nansum)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (a->dt != ND_FLOAT) return nd_reduce(R_SUM, a, has, axis, 0);
    return nd_reduce(R_SUM, nan_fill(a, 0.0), has, axis, 0);
}
BI(np_nanprod)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (a->dt != ND_FLOAT) return nd_reduce(R_PROD, a, has, axis, 0);
    return nd_reduce(R_PROD, nan_fill(a, 1.0), has, axis, 0);
}
BI(np_nanmean)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *cnt;
    int axis = 0, has = get_axis(x, 1, a, &axis);
    Value s, c;
    NdTmp t1, t2;
    if (a->dt != ND_FLOAT) return nd_reduce(R_MEAN, a, has, axis, 0);
    s = nd_reduce(R_SUM, nan_fill(a, 0.0), has, axis, 0);
    cnt = nan_count(a);
    c = nd_reduce(R_SUM, cnt, has, axis, 0);
    return nd_binary(B_DIV, nd_coerce(s, &t1), nd_coerce(c, &t2));
}
static Value nan_var(Args *x, int issd)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *z, *cnt, *dev;
    long ddof = ddof_arg(x), i, n = 0;
    double mean = 0.0, ss = 0.0, v;
    int axis = 0;
    if (get_axis(x, 1, a, &axis))
        throw_new(cls_NotImplementedError, "nanstd/nanvar: axis is not supported");
    z = nan_fill(a, 0.0);
    cnt = nan_count(a);
    for (i = 0; i < z->size; i++) { mean += nd_d(z, i); n += cnt->iv[i]; }
    if (n == 0) return mkfloat(HUGE_VAL - HUGE_VAL);
    mean /= (double)n;
    dev = nd_astype(a, ND_FLOAT);
    for (i = 0; i < dev->size; i++) if (cnt->iv[i]) { double d = dev->dv[i] - mean; ss += d * d; }
    v = ss / (double)(n - ddof > 0 ? n - ddof : 0);
    return mkfloat(issd ? sqrt(v) : v);
}
BI(np_nanvar) { return nan_var(x, 0); }
BI(np_nanstd) { return nan_var(x, 1); }
BI(np_nanmax)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (a->dt != ND_FLOAT) return nd_reduce(R_MAX, a, has, axis, 0);
    return nd_reduce(R_MAX, nan_fill(a, -HUGE_VAL), has, axis, 0);
}
BI(np_nanmin)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (a->dt != ND_FLOAT) return nd_reduce(R_MIN, a, has, axis, 0);
    return nd_reduce(R_MIN, nan_fill(a, HUGE_VAL), has, axis, 0);
}
BI(np_nanargmax)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    return nd_reduce(R_ARGMAX, a->dt == ND_FLOAT ? nan_fill(a, -HUGE_VAL) : a, has, axis, 0);
}
BI(np_nanargmin)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    return nd_reduce(R_ARGMIN, a->dt == ND_FLOAT ? nan_fill(a, HUGE_VAL) : a, has, axis, 0);
}
BI(np_ptp)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    Value mx = nd_reduce(R_MAX, a, has, axis, 0), mn = nd_reduce(R_MIN, a, has, axis, 0);
    NdTmp t1, t2;
    return nd_binary(B_SUB, nd_coerce(mx, &t1), nd_coerce(mn, &t2));
}
BI(np_average)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    Value wv = np_arg(x, 2, "weights");
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (!np_has(wv)) return nd_reduce(R_MEAN, a, has, axis, 0);
    {
        NdTmp tw, t1, t2, t3;
        Nd *w = nd_coerce(wv, &tw);
        Value prod = nd_binary(B_MUL, a, w), num, den;
        if (w->nd != a->nd && has) throw_new(cls_NotImplementedError, "average: weights must match the array shape");
        num = nd_reduce(R_SUM, nd_coerce(prod, &t1), has, axis, 0);
        if (w->nd == a->nd || !has) den = nd_reduce(R_SUM, w, has, axis, 0);
        else den = nd_reduce(R_SUM, w, 0, 0, 0);
        return nd_binary(B_DIV, nd_coerce(num, &t2), nd_coerce(den, &t3));
    }
}

/* ---- axis helpers -------------------------------------------------------- */
static void axis_split(const Nd *a, int ax, long *outer, long *len, long *inner)
{
    int k;
    *outer = 1; *inner = 1; *len = a->shape[ax];
    for (k = 0; k < ax; k++) *outer *= a->shape[k];
    for (k = ax + 1; k < a->nd; k++) *inner *= a->shape[k];
}
BI(np_diff)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *cur;
    Value nv = np_arg(x, 1, "n"), av = np_arg(x, 2, "axis");
    long n = np_has(nv) ? need_long(nv, "n") : 1, step;
    int ax;
    if (a->nd == 0) throw_new(cls_ValueError, "diff requires input that is at least one dimensional");
    ax = np_has(av) ? axis_norm(need_long(av, "axis"), a->nd) : a->nd - 1;
    if (n < 0) throw_new(cls_ValueError, "order must be non-negative but got %ld", n);
    cur = nd_astype(a, a->dt);
    for (step = 0; step < n; step++) {
        long outer, len, inner, o, j, i, sh[ND_MAXD];
        Nd *r;
        int k;
        axis_split(cur, ax, &outer, &len, &inner);
        if (len == 0) break;
        for (k = 0; k < cur->nd; k++) sh[k] = cur->shape[k];
        sh[ax] = len - 1;
        r = nd_new(cur->dt, cur->nd, sh);
        for (o = 0; o < outer; o++)
            for (j = 0; j + 1 < len; j++)
                for (i = 0; i < inner; i++) {
                    long p = (o * len + j) * inner + i, q = p + inner, d = (o * (len - 1) + j) * inner + i;
                    if (cur->dt == ND_FLOAT) r->dv[d] = cur->dv[q] - cur->dv[p];
                    else if (cur->dt == ND_BOOL) r->iv[d] = cur->iv[q] != cur->iv[p];
                    else r->iv[d] = cur->iv[q] - cur->iv[p];
                }
        cur = r;
    }
    return ndv(cur);
}
BI(np_ediff1d)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *f;
    long n = a->size;
    Args y;
    Value av[1];
    f = nd_reshaped(a, 1, &n);
    y.n = 1; y.a = av; y.nk = 0; y.kw = NULL;
    av[0] = ndv(f);
    return np_diff(&y);
}
BI(np_gradient)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *f;
    int naxes, ax, k, single = 0;
    Value axv = kwget(x, "axis"), tv[ND_MAXD];
    int axes[ND_MAXD];
    int nvar = x->n - 1;
    if (a->nd == 0) throw_new(cls_ValueError, "gradient: need at least 1-d input");
    f = nd_astype(a, ND_FLOAT);
    if (np_has(axv)) {
        if (axv.t == T_TUPLE || axv.t == T_LIST) {
            List *l = AS_LIST(axv);
            naxes = (int)l->n;
            for (k = 0; k < naxes; k++) axes[k] = axis_norm(need_long(l->a[k], "axis"), a->nd);
        } else { naxes = 1; axes[0] = axis_norm(need_long(axv, "axis"), a->nd); single = 1; }
    } else {
        naxes = a->nd;
        for (k = 0; k < naxes; k++) axes[k] = k;
    }
    if (nvar != 0 && nvar != 1 && nvar != naxes)
        throw_new(cls_TypeError, "invalid number of arguments");
    for (k = 0; k < naxes; k++) {
        long outer, len, inner, o, j, i;
        Nd *r, *xs = NULL;
        double h = 1.0;
        Value sp = nvar == 0 ? UNSET_V : (nvar == 1 ? A(1) : A(1 + k));
        ax = axes[k];
        axis_split(f, ax, &outer, &len, &inner);
        if (len < 2) throw_new(cls_ValueError, "Shape of array too small to calculate a numerical gradient, at least (edge_order + 1) elements are required.");
        if (np_has(sp)) {
            if (sp.t == T_INT || sp.t == T_FLOAT || sp.t == T_BOOL) h = to_dbl(sp);
            else {
                xs = np_f(sp);
                if (xs->size != len)
                    throw_new(cls_ValueError, "when 1d, distances must match the length of the corresponding dimension");
            }
        }
        r = nd_new(ND_FLOAT, f->nd, f->shape);
        for (o = 0; o < outer; o++)
            for (i = 0; i < inner; i++) {
                double *fp = f->dv + o * len * inner + i, *rp = r->dv + o * len * inner + i;
                rp[0] = (fp[inner] - fp[0]) / (xs ? xs->dv[1] - xs->dv[0] : h);
                rp[(len - 1) * inner] = (fp[(len - 1) * inner] - fp[(len - 2) * inner]) /
                                        (xs ? xs->dv[len - 1] - xs->dv[len - 2] : h);
                for (j = 1; j < len - 1; j++) {
                    if (xs) {
                        double hd = xs->dv[j] - xs->dv[j - 1], hs = xs->dv[j + 1] - xs->dv[j];
                        double fm = fp[(j - 1) * inner], f0 = fp[j * inner], fpl = fp[(j + 1) * inner];
                        rp[j * inner] = (hd * hd * fpl - hs * hs * fm + (hs * hs - hd * hd) * f0) /
                                        (hs * hd * (hd + hs));
                    } else
                        rp[j * inner] = (fp[(j + 1) * inner] - fp[(j - 1) * inner]) / (2.0 * h);
                }
            }
        tv[k] = ndv(r);
    }
    if (single || naxes == 1) return tv[0];
    return mktuple_from(tv, naxes);
}

/* ---- sorting related ------------------------------------------------------ */
BI(np_searchsorted)
{
    NdTmp t, tv_;
    Nd *a = nd_arg0(x, &t), *v, *r;
    Value sd = np_arg(x, 2, "side");
    int right = 0;
    long i;
    need(x, 2, 4, "searchsorted");
    if (sd.t == T_STR && AS_STR(sd)->s[0] == 'r') right = 1;
    if (a->nd != 1) throw_new(cls_ValueError, "object too deep for desired array");
    v = nd_coerce(A(1), &tv_);
    r = nd_new(ND_INT, v->nd, v->shape);
    for (i = 0; i < v->size; i++) {
        double key = nd_d(v, i);
        long lo = 0, hi = a->size;
        while (lo < hi) {
            long mid = (lo + hi) / 2;
            double e = nd_d(a, mid);
            int go = right ? (e <= key) : (e < key);
            if (e != e) go = 0;
            if (key != key) go = e == e;
            if (go) lo = mid + 1; else hi = mid;
        }
        r->iv[i] = lo;
    }
    return nd_ret(r);
}
/* percentile / quantile / median along an axis (linear interpolation) */
static double lerp_q(double a, double b, double t)
{
    double d = b - a, r = a + d * t;
    if (t >= 0.5) r = b - d * (1.0 - t);
    return r;
}
static int g_median_mode;
static double quant_sorted(const double *s, long n, double q)
{
    double vi, t;
    long lo, hi;
    if (n == 0) return HUGE_VAL - HUGE_VAL;
    if (g_median_mode) {
        if (s[n - 1] != s[n - 1]) return s[n - 1];
        return n % 2 ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2.0;
    }
    vi = q * (double)(n - 1);
    lo = (long)floor(vi); hi = lo + 1;
    if (lo < 0) lo = 0;
    if (hi > n - 1) hi = n - 1;
    t = vi - floor(vi);
    if (s[hi] != s[hi]) return s[hi];
    return lerp_q(s[lo], s[hi], t);
}
static Value quantile_impl(Args *x, double scale, const char *fname)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *qs, *r, *f;
    Value av = np_arg(x, 2, "axis"), mv = kwget(x, "method");
    long nq, qi, outer, len, inner, o, i, j;
    int ax = -1, scalar_q;
    double *buf;
    need(x, 2, 6, fname);
    if (mv.t == T_STR && strcmp(AS_STR(mv)->s, "linear") != 0)
        throw_new(cls_NotImplementedError, "%s: only method='linear' is supported", fname);
    {
        NdTmp tq;
        qs = nd_astype(nd_coerce(A(1), &tq), ND_FLOAT);
        scalar_q = qs->nd == 0;
        nq = qs->size;
        for (qi = 0; qi < nq; qi++) {
            qs->dv[qi] /= scale;
            if (!(qs->dv[qi] >= 0.0 && qs->dv[qi] <= 1.0))
                throw_new(cls_ValueError, scale == 1.0 ? "Quantiles must be in the range [0, 1]" : "Percentiles must be in the range [0, 100]");
        }
    }
    f = nd_astype(a, ND_FLOAT);
    if (np_has(av)) ax = axis_norm(need_long(av, "axis"), a->nd);
    if (ax < 0) {
        long n = f->size;
        buf = f->dv;
        qsort(buf, (size_t)n, sizeof(double), cmp_dbl);
        if (scalar_q) return mkfloat(quant_sorted(buf, n, qs->dv[0]));
        r = nd_new1(ND_FLOAT, nq);
        for (qi = 0; qi < nq; qi++) r->dv[qi] = quant_sorted(buf, n, qs->dv[qi]);
        return ndv(r);
    }
    axis_split(f, ax, &outer, &len, &inner);
    {
        long sh[ND_MAXD];
        int k, rn = 0;
        if (!scalar_q) sh[rn++] = nq;
        for (k = 0; k < a->nd; k++) if (k != ax) sh[rn++] = a->shape[k];
        r = nd_new(ND_FLOAT, rn, sh);
    }
    buf = (double *)xmalloc(sizeof(double) * (size_t)(len > 0 ? len : 1));
    for (o = 0; o < outer; o++)
        for (i = 0; i < inner; i++) {
            for (j = 0; j < len; j++) buf[j] = f->dv[(o * len + j) * inner + i];
            qsort(buf, (size_t)len, sizeof(double), cmp_dbl);
            for (qi = 0; qi < nq; qi++)
                r->dv[qi * outer * inner + o * inner + i] = quant_sorted(buf, len, qs->dv[qi]);
        }
    free(buf);
    return nd_ret(r);
}
BI(np_percentile) { return quantile_impl(x, 100.0, "percentile"); }
BI(np_quantile) { return quantile_impl(x, 1.0, "quantile"); }
BI(np_median2)
{
    Value av = np_arg(x, 1, "axis"), r;
    Args y;
    Value v[3];
    if (!np_has(av)) return np_median(x);
    y.n = 3; y.a = v; y.nk = 0; y.kw = NULL;
    v[0] = A(0); v[1] = mkfloat(0.5); v[2] = av;
    g_median_mode = 1;
    r = quantile_impl(&y, 1.0, "median");
    g_median_mode = 0;
    return r;
}
BI(np_histogram)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *f, *edges, *hist, *w = NULL;
    Value bv = np_arg(x, 1, "bins"), rv = np_arg(x, 2, "range"), dv = kwget(x, "density"), wv = kwget(x, "weights");
    long nb = 10, i, n;
    double lo, hi;
    int density = np_has(dv) && py_truthy(dv);
    Value tv[2];
    Nd *edges_out = NULL;
    f = nd_astype(a, ND_FLOAT);
    n = f->size;
    if (np_has(wv)) {
        w = np_f(wv);
        if (w->size != n) throw_new(cls_ValueError, "weights should have the same shape as a.");
    }
    if (np_has(bv) && (bv.t == T_LIST || bv.t == T_TUPLE || bv.t == T_ND)) {
        edges = np_f(bv);
        edges_out = nd_from_value(bv, -1);
        nb = edges->size - 1;
        if (nb < 1) throw_new(cls_ValueError, "`bins` must have at least 2 edges");
        for (i = 0; i < nb; i++)
            if (edges->dv[i + 1] < edges->dv[i]) throw_new(cls_ValueError, "`bins` must increase monotonically, when an array");
    } else {
        if (np_has(bv)) nb = need_long(bv, "bins");
        if (nb < 1) throw_new(cls_ValueError, "`bins` must be positive, when an integer");
        if (np_has(rv)) {
            Nd *rg = np_f(rv);
            if (rg->size != 2) throw_new(cls_ValueError, "range must be a pair");
            lo = rg->dv[0]; hi = rg->dv[1];
            if (lo > hi) throw_new(cls_ValueError, "max must be larger than min in range parameter.");
        } else if (n == 0) { lo = 0.0; hi = 1.0; }
        else {
            lo = HUGE_VAL; hi = -HUGE_VAL;
            for (i = 0; i < n; i++) { if (f->dv[i] < lo) lo = f->dv[i]; if (f->dv[i] > hi) hi = f->dv[i]; }
        }
        if (lo == hi) { lo -= 0.5; hi += 0.5; }
        edges = vec_new(nb + 1);
        {
            double step = (hi - lo) / (double)nb;
            for (i = 0; i <= nb; i++) edges->dv[i] = (double)i * step + lo;
            edges->dv[nb] = hi;
        }
    }
    hist = nd_new1(w || density ? ND_FLOAT : ND_INT, nb);
    for (i = 0; i < nb; i++) { if (hist->dt == ND_FLOAT) hist->dv[i] = 0.0; else hist->iv[i] = 0; }
    for (i = 0; i < n; i++) {
        double v = f->dv[i];
        long l = 0, h = nb;
        if (!(v >= edges->dv[0] && v <= edges->dv[nb])) continue;
        while (l < h) {
            long mid = (l + h) / 2;
            if (edges->dv[mid + 1] <= v) l = mid + 1; else h = mid;
        }
        if (l >= nb) l = nb - 1;
        if (hist->dt == ND_FLOAT) hist->dv[l] += w ? w->dv[i] : 1.0; else hist->iv[l]++;
    }
    if (density) {
        double tot = 0.0;
        for (i = 0; i < nb; i++) tot += hist->dv[i];
        for (i = 0; i < nb; i++) hist->dv[i] = hist->dv[i] / tot / (edges->dv[i + 1] - edges->dv[i]);
    }
    tv[0] = ndv(hist); tv[1] = ndv(edges_out ? edges_out : edges);
    return mktuple_from(tv, 2);
}
BI(np_bincount)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r, *w = NULL;
    Value wv = np_arg(x, 1, "weights"), mv = np_arg(x, 2, "minlength");
    long i, mx = -1, ml = np_has(mv) ? need_long(mv, "minlength") : 0;
    if (a->dt == ND_FLOAT) throw_new(cls_TypeError, "Cannot cast array data from dtype('float64') to dtype('int64') according to the rule 'safe'");
    if (a->nd != 1) throw_new(cls_ValueError, "object too deep for desired array");
    for (i = 0; i < a->size; i++) {
        if (a->iv[i] < 0) throw_new(cls_ValueError, "'list' argument must have no negative elements");
        if (a->iv[i] > mx) mx = a->iv[i];
    }
    if (mx + 1 < ml) mx = ml - 1;
    if (np_has(wv)) w = np_f(wv);
    r = nd_new1(w ? ND_FLOAT : ND_INT, mx + 1);
    for (i = 0; i <= mx; i++) { if (w) r->dv[i] = 0.0; else r->iv[i] = 0; }
    for (i = 0; i < a->size; i++) { if (w) r->dv[a->iv[i]] += w->dv[i]; else r->iv[a->iv[i]]++; }
    return ndv(r);
}
/* ---- covariance ---------------------------------------------------------- */
static Nd *cov_matrix(Args *x, int *scalar)
{
    NdTmp t;
    Nd *m = nd_astype(nd_arg0(x, &t), ND_FLOAT), *X, *c;
    Value yv = np_arg(x, 1, "y"), rv = np_arg(x, 2, "rowvar"), bv = np_arg(x, 3, "bias"), dv = np_arg(x, 4, "ddof");
    int rowvar = !np_has(rv) || py_truthy(rv);
    long ddof = np_has(bv) && py_truthy(bv) ? 0 : 1, nv, N, i, j, k;
    double fact;
    double *means;
    if (np_has(dv)) ddof = need_long(dv, "ddof");
    if (m->nd > 2) throw_new(cls_ValueError, "m has more than 2 dimensions");
    if (m->nd < 2) { long sh[2]; sh[0] = 1; sh[1] = m->size; m = nd_reshaped(m, 2, sh); rowvar = 1; }
    *scalar = 0;
    if (!rowvar && m->shape[0] != 1) {
        long sh[2];
        Nd *tr = mat_new(m->shape[1], m->shape[0]);
        for (i = 0; i < m->shape[0]; i++) for (j = 0; j < m->shape[1]; j++) tr->dv[j * m->shape[0] + i] = m->dv[i * m->shape[1] + j];
        m = tr; (void)sh;
    }
    if (np_has(yv)) {
        Nd *y = np_f(yv);
        long sh[2];
        if (y->nd < 2) { sh[0] = 1; sh[1] = y->size; y = nd_reshaped(y, 2, sh); }
        else if (!rowvar && y->shape[0] != 1) {
            Nd *tr = mat_new(y->shape[1], y->shape[0]);
            for (i = 0; i < y->shape[0]; i++) for (j = 0; j < y->shape[1]; j++) tr->dv[j * y->shape[0] + i] = y->dv[i * y->shape[1] + j];
            y = tr;
        }
        if (y->shape[1] != m->shape[1]) throw_new(cls_ValueError, "all the input array dimensions except for the concatenation axis must match exactly");
        X = mat_new(m->shape[0] + y->shape[0], m->shape[1]);
        memcpy(X->dv, m->dv, sizeof(double) * (size_t)m->size);
        memcpy(X->dv + m->size, y->dv, sizeof(double) * (size_t)y->size);
    } else X = m;
    nv = X->shape[0]; N = X->shape[1];
    fact = (double)(N - ddof);
    if (fact <= 0) fact = 0.0;
    means = (double *)xmalloc(sizeof(double) * (size_t)(nv + 1));
    for (i = 0; i < nv; i++) {
        means[i] = pw_sum(X->dv + i * N, N) / (double)N;
        for (j = 0; j < N; j++) X->dv[i * N + j] -= means[i];
    }
    free(means);
    c = mat_new(nv, nv);
    for (i = 0; i < nv; i++)
        for (j = 0; j < nv; j++) {
            double s = 0.0;
            for (k = 0; k < N; k++) s += X->dv[i * N + k] * X->dv[j * N + k];
            c->dv[i * nv + j] = s * (1.0 / fact);
        }
    if (nv == 1) *scalar = 1;
    return c;
}
BI(np_cov)
{
    int sc;
    Nd *c = cov_matrix(x, &sc);
    if (sc) return mkfloat(c->dv[0]);
    return ndv(c);
}
BI(np_corrcoef)
{
    int sc;
    Nd *c = cov_matrix(x, &sc);
    long n = c->shape[0], i, j;
    double *sd;
    if (sc) return mkfloat(c->dv[0] / c->dv[0]);
    sd = (double *)xmalloc(sizeof(double) * (size_t)n);
    for (i = 0; i < n; i++) sd[i] = sqrt(c->dv[i * n + i]);
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            double v = c->dv[i * n + j] / sd[i] / sd[j];
            if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
            c->dv[i * n + j] = v;
        }
    free(sd);
    return ndv(c);
}
/* ---- triangular, kron, outer, cross ------------------------------------- */
static Value tri_impl(Args *x, int upper)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value kv = np_arg(x, 1, "k");
    long k = np_has(kv) ? need_long(kv, "k") : 0, m, n, o, nmat, i, j;
    if (a->nd < 2) {
        /* numpy broadcasts a 1-d input against its own length */
        long sh[2];
        Nd *tmp;
        sh[0] = a->size; sh[1] = a->size;
        tmp = nd_new(a->dt, 2, sh);
        for (i = 0; i < a->size; i++) nd_cp(tmp, i * a->size, a, 0, a->size);
        a = tmp;
    }
    m = a->shape[a->nd - 2]; n = a->shape[a->nd - 1];
    nmat = m * n > 0 ? a->size / (m * n) : 0;
    r = nd_astype(a, a->dt);
    for (o = 0; o < nmat; o++)
        for (i = 0; i < m; i++)
            for (j = 0; j < n; j++) {
                int keep = upper ? (j - i >= k) : (j - i <= k);
                if (!keep) { if (r->dt == ND_FLOAT) r->dv[o * m * n + i * n + j] = 0.0; else r->iv[o * m * n + i * n + j] = 0; }
            }
    return ndv(r);
}
BI(np_tril) { return tri_impl(x, 0); }
BI(np_triu) { return tri_impl(x, 1); }
BI(np_kron)
{
    NdTmp ta, tb;
    Nd *a, *b, *ra, *rb, *r;
    int nd, k;
    long sh[ND_MAXD], sa[ND_MAXD], sb[ND_MAXD], i, j, idx[ND_MAXD], dt;
    need(x, 2, 2, "kron");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    nd = a->nd > b->nd ? a->nd : b->nd;
    if (nd == 0) return nd_binary(B_MUL, a, b);
    for (k = 0; k < nd; k++) {
        int ka = k - (nd - a->nd), kb = k - (nd - b->nd);
        sa[k] = ka >= 0 ? a->shape[ka] : 1;
        sb[k] = kb >= 0 ? b->shape[kb] : 1;
        sh[k] = sa[k] * sb[k];
    }
    dt = (a->dt == ND_FLOAT || b->dt == ND_FLOAT) ? ND_FLOAT : ND_INT;
    ra = nd_astype(a, (int)dt); rb = nd_astype(b, (int)dt);
    r = nd_new(dt, nd, sh);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        long oa = 0, ob = 0, ma = 1, mb = 1;
        for (k = nd - 1; k >= 0; k--) {
            oa += (idx[k] / sb[k]) * ma; ma *= sa[k];
            ob += (idx[k] % sb[k]) * mb; mb *= sb[k];
        }
        if (dt == ND_FLOAT) r->dv[i] = ra->dv[oa] * rb->dv[ob];
        else r->iv[i] = (long)((unsigned long)ra->iv[oa] * (unsigned long)rb->iv[ob]);
        for (k = nd - 1; k >= 0; k--) { if (++idx[k] < sh[k]) break; idx[k] = 0; }
    }
    (void)j;
    return ndv(r);
}
BI(np_outer)
{
    NdTmp ta, tb;
    Nd *a, *b, *ra, *rb;
    long sh[2];
    need(x, 2, 2, "outer");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    sh[0] = a->size; sh[1] = 1;
    ra = nd_reshaped(a, 2, sh);
    sh[0] = 1; sh[1] = b->size;
    rb = nd_reshaped(b, 2, sh);
    return nd_binary(B_MUL, ra, rb);
}
BI(np_inner)
{
    NdTmp ta, tb;
    Nd *a, *b;
    need(x, 2, 2, "inner");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->nd <= 1 && b->nd <= 1) return nd_matmul(a, b, 1);
    if (a->nd == 2 && b->nd == 1) return nd_matmul(a, b, 1);
    if (a->nd <= 1 && b->nd == 2) return nd_matmul(a, nd_transposed(b), 1);
    if (a->nd == 2 && b->nd == 2) return nd_matmul(a, nd_transposed(b), 1);
    throw_new(cls_NotImplementedError, "inner: only up to 2-d arrays are supported");
    return NONE_V;
}
BI(np_vdot)
{
    NdTmp ta, tb;
    Nd *a, *b;
    long n;
    need(x, 2, 2, "vdot");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->size != b->size) throw_new(cls_ValueError, "cannot reshape array of size %ld into shape (%ld)", b->size, a->size);
    n = a->size;
    return nd_matmul(nd_reshaped(a, 1, &n), nd_reshaped(b, 1, &n), 1);
}
BI(np_cross)
{
    NdTmp ta, tb;
    Nd *a, *b, *r;
    long na, nb, n, i;
    int dt;
    need(x, 2, 2, "cross");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->nd < 1 || b->nd < 1 || a->nd > 2 || b->nd > 2 ||
        a->shape[a->nd - 1] != 3 || b->shape[b->nd - 1] != 3)
        throw_new(cls_ValueError, "incompatible dimensions for cross product\n(dimension must be 3)");
    na = a->nd == 2 ? a->shape[0] : 1;
    nb = b->nd == 2 ? b->shape[0] : 1;
    if (na != nb && na != 1 && nb != 1)
        throw_new(cls_ValueError, "operands could not be broadcast together with shapes (%ld,3) (%ld,3) ", na, nb);
    n = na > nb ? na : nb;
    dt = (a->dt == ND_FLOAT || b->dt == ND_FLOAT) ? ND_FLOAT : ND_INT;
    if (a->nd == 1 && b->nd == 1) r = nd_new1(dt, 3);
    else { long sh[2]; sh[0] = n; sh[1] = 3; r = nd_new(dt, 2, sh); }
    for (i = 0; i < n; i++) {
        long pa = (na == 1 ? 0 : i) * 3, pb = (nb == 1 ? 0 : i) * 3;
        if (dt == ND_FLOAT) {
            double a0 = nd_d(a, pa), a1 = nd_d(a, pa + 1), a2 = nd_d(a, pa + 2);
            double b0 = nd_d(b, pb), b1 = nd_d(b, pb + 1), b2 = nd_d(b, pb + 2);
            r->dv[i * 3] = a1 * b2 - a2 * b1;
            r->dv[i * 3 + 1] = a2 * b0 - a0 * b2;
            r->dv[i * 3 + 2] = a0 * b1 - a1 * b0;
        } else {
            long a0 = a->iv[pa], a1 = a->iv[pa + 1], a2 = a->iv[pa + 2];
            long b0 = b->iv[pb], b1 = b->iv[pb + 1], b2 = b->iv[pb + 2];
            r->iv[i * 3] = a1 * b2 - a2 * b1;
            r->iv[i * 3 + 1] = a2 * b0 - a0 * b2;
            r->iv[i * 3 + 2] = a0 * b1 - a1 * b0;
        }
    }
    return ndv(r);
}
BI(np_meshgrid)
{
    int n = x->n, k, xy = 1;
    Value iv = kwget(x, "indexing");
    Nd *g[ND_MAXD];
    Value tv[ND_MAXD];
    long sh[ND_MAXD], idx[ND_MAXD], i;
    if (iv.t == T_STR) {
        if (strcmp(AS_STR(iv)->s, "ij") == 0) xy = 0;
        else if (strcmp(AS_STR(iv)->s, "xy") != 0) throw_new(cls_ValueError, "Valid values for `indexing` are 'xy' and 'ij'.");
    }
    if (n > ND_MAXD) throw_new(cls_ValueError, "too many arrays");
    for (k = 0; k < n; k++) {
        NdTmp t;
        Nd *a = nd_coerce(A(k), &t);
        long sz = a->size;
        g[k] = nd_reshaped(a, 1, &sz);
        sh[k] = sz;
    }
    if (xy && n >= 2) { long t = sh[0]; sh[0] = sh[1]; sh[1] = t; }
    for (k = 0; k < n; k++) {
        int axis = k;
        Nd *r = nd_new(g[k]->dt, n, sh);
        if (xy && n >= 2) { if (k == 0) axis = 1; else if (k == 1) axis = 0; }
        for (i = 0; i < n; i++) idx[i] = 0;
        for (i = 0; i < r->size; i++) {
            int d;
            nd_cp(r, i, g[k], idx[axis], 1);
            for (d = n - 1; d >= 0; d--) { if (++idx[d] < sh[d]) break; idx[d] = 0; }
        }
        tv[k] = ndv(r);
    }
    return mktuple_from(tv, n);
}
/* ---- polynomials, convolution ------------------------------------------- */
BI(np_polyfit)
{
    NdTmp tx, ty;
    Nd *xs, *ys, *V, *res, *w = NULL;
    Value dv = np_arg(x, 2, "deg"), wv = kwget(x, "w");
    long deg, m, cols, k, i, j, rank;
    double *scale, *b, *sol, rc;
    need(x, 3, 8, "polyfit");
    deg = need_long(dv, "deg");
    if (deg < 0) throw_new(cls_ValueError, "expected deg >= 0");
    xs = nd_astype(nd_coerce(A(0), &tx), ND_FLOAT);
    ys = nd_astype(nd_coerce(A(1), &ty), ND_FLOAT);
    if (xs->nd != 1) throw_new(cls_TypeError, "expected 1D vector for x");
    if (xs->size == 0) throw_new(cls_TypeError, "expected non-empty vector for x");
    if (ys->nd < 1 || ys->nd > 2) throw_new(cls_TypeError, "expected 1D or 2D array for y");
    if (ys->shape[0] != xs->size) throw_new(cls_TypeError, "expected x and y to have same length");
    if (np_has(wv)) {
        w = np_f(wv);
        if (w->size != xs->size) throw_new(cls_TypeError, "expected w and x to have same length");
    }
    m = xs->size; cols = deg + 1;
    k = ys->nd == 1 ? 1 : ys->shape[1];
    V = mat_new(m, cols);
    for (i = 0; i < m; i++) {
        double p = 1.0;
        for (j = cols - 1; j >= 0; j--) { V->dv[i * cols + j] = p; p *= xs->dv[i]; }
    }
    b = (double *)xmalloc(sizeof(double) * (size_t)(m * k + 1));
    memcpy(b, ys->dv, sizeof(double) * (size_t)(m * k));
    if (w) {
        for (i = 0; i < m; i++) {
            for (j = 0; j < cols; j++) V->dv[i * cols + j] *= w->dv[i];
            for (j = 0; j < k; j++) b[i * k + j] *= w->dv[i];
        }
    }
    scale = (double *)xmalloc(sizeof(double) * (size_t)cols);
    for (j = 0; j < cols; j++) {
        double s = 0.0;
        for (i = 0; i < m; i++) s += V->dv[i * cols + j] * V->dv[i * cols + j];
        scale[j] = sqrt(s);
        if (scale[j] == 0.0) scale[j] = 1.0;
        for (i = 0; i < m; i++) V->dv[i * cols + j] /= scale[j];
    }
    sol = (double *)xmalloc(sizeof(double) * (size_t)(cols * k + 1));
    rc = (double)m * DBL_EPSILON;
    lstsq_core(V, m, cols, b, k, rc, sol, &rank, NULL);
    for (j = 0; j < cols; j++) for (i = 0; i < k; i++) sol[j * k + i] /= scale[j];
    if (ys->nd == 1) { res = vec_new(cols); memcpy(res->dv, sol, sizeof(double) * (size_t)cols); }
    else { res = mat_new(cols, k); memcpy(res->dv, sol, sizeof(double) * (size_t)(cols * k)); }
    free(b); free(scale); free(sol);
    return ndv(res);
}
BI(np_polyval)
{
    NdTmp tp, tx;
    Nd *p, *xs, *r;
    long i, j;
    need(x, 2, 2, "polyval");
    p = nd_coerce(A(0), &tp); xs = nd_coerce(A(1), &tx);
    if (p->nd > 1) throw_new(cls_ValueError, "object too deep for desired array");
    if (p->dt == ND_FLOAT || xs->dt == ND_FLOAT) {
        r = nd_new(ND_FLOAT, xs->nd, xs->shape);
        for (i = 0; i < xs->size; i++) {
            double v = 0.0, xv = nd_d(xs, i);
            for (j = 0; j < p->size; j++) v = v * xv + nd_d(p, j);
            r->dv[i] = v;
        }
    } else {
        r = nd_new(ND_INT, xs->nd, xs->shape);
        for (i = 0; i < xs->size; i++) {
            long v = 0;
            for (j = 0; j < p->size; j++) v = v * xs->iv[i] + p->iv[j];
            r->iv[i] = v;
        }
    }
    return nd_ret(r);
}
BI(np_convolve)
{
    NdTmp ta, tv_;
    Nd *a, *v, *full, *r, *t;
    Value mv = np_arg(x, 2, "mode");
    const char *mode = mv.t == T_STR ? AS_STR(mv)->s : "full";
    long na, nv, nf, i, j, start = 0, len = 0;
    int fl;
    need(x, 2, 3, "convolve");
    a = nd_coerce(A(0), &ta); v = nd_coerce(A(1), &tv_);
    if (a->nd > 1 || v->nd > 1) throw_new(cls_ValueError, "object too deep for desired array");
    if (a->size == 0) throw_new(cls_ValueError, "a cannot be empty");
    if (v->size == 0) throw_new(cls_ValueError, "v cannot be empty");
    if (v->size > a->size) { t = a; a = v; v = t; }
    na = a->size; nv = v->size; nf = na + nv - 1;
    fl = a->dt == ND_FLOAT || v->dt == ND_FLOAT;
    full = nd_new1(fl ? ND_FLOAT : ND_INT, nf);
    for (i = 0; i < nf; i++) { if (fl) full->dv[i] = 0.0; else full->iv[i] = 0; }
    for (i = 0; i < na; i++)
        for (j = 0; j < nv; j++) {
            if (fl) full->dv[i + j] += nd_d(a, i) * nd_d(v, j);
            else full->iv[i + j] += a->iv[i] * v->iv[j];
        }
    if (strcmp(mode, "full") == 0 || mode[0] == 'f') return ndv(full);
    if (strcmp(mode, "same") == 0 || mode[0] == 's') { start = (nv - 1) / 2; len = na; }
    else if (strcmp(mode, "valid") == 0 || mode[0] == 'v') { start = nv - 1; len = na - nv + 1; }
    else throw_new(cls_ValueError, "mode must be one of 'full', 'same', or 'valid'");
    r = nd_new1(full->dt, len);
    nd_cp(r, 0, full, start, len);
    return ndv(r);
}
BI(np_trapz)
{
    NdTmp t;
    Nd *y = nd_astype(nd_arg0(x, &t), ND_FLOAT), *xs = NULL;
    Value xv = np_arg(x, 1, "x"), dxv = np_arg(x, 2, "dx");
    double dx = np_has(dxv) ? to_dbl(dxv) : 1.0, s = 0.0;
    long i;
    if (y->nd != 1) throw_new(cls_NotImplementedError, "trapz: only 1-d input is supported");
    if (np_has(xv)) xs = np_f(xv);
    for (i = 1; i < y->size; i++) {
        double h = xs ? xs->dv[i] - xs->dv[i - 1] : dx;
        s += h * (y->dv[i] + y->dv[i - 1]) / 2.0;
    }
    return mkfloat(s);
}
/* ---- shape manipulation --------------------------------------------------- */
BI(np_flip)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value av = np_arg(x, 1, "axis");
    long outer, len, inner, o, j, i;
    int ax;
    if (a->nd == 0) return nd_ret(nd_astype(a, a->dt));
    r = nd_astype(a, a->dt);
    if (!np_has(av)) {
        for (ax = 0; ax < a->nd; ax++) {
            Nd *s = nd_astype(r, r->dt);
            axis_split(s, ax, &outer, &len, &inner);
            for (o = 0; o < outer; o++) for (j = 0; j < len; j++)
                nd_cp(r, (o * len + j) * inner, s, (o * len + (len - 1 - j)) * inner, inner);
        }
        return ndv(r);
    }
    ax = axis_norm(need_long(av, "axis"), a->nd);
    axis_split(a, ax, &outer, &len, &inner);
    for (o = 0; o < outer; o++) for (j = 0; j < len; j++)
        for (i = 0; i < 1; i++) nd_cp(r, (o * len + j) * inner, a, (o * len + (len - 1 - j)) * inner, inner);
    return ndv(r);
}
BI(np_fliplr)
{
    Args y;
    Value v[2];
    y.n = 2; y.a = v; y.nk = 0; y.kw = NULL;
    v[0] = A(0); v[1] = mkint(1);
    return np_flip(&y);
}
BI(np_flipud)
{
    Args y;
    Value v[2];
    y.n = 2; y.a = v; y.nk = 0; y.kw = NULL;
    v[0] = A(0); v[1] = mkint(0);
    return np_flip(&y);
}
BI(np_roll)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value sv = np_arg(x, 1, "shift"), av = np_arg(x, 2, "axis");
    long shift, outer, len, inner, o, j;
    int ax;
    shift = need_long(sv, "shift");
    r = nd_astype(a, a->dt);
    if (a->size == 0) return ndv(r);
    if (!np_has(av)) {
        long n = a->size;
        shift = ((shift % n) + n) % n;
        for (j = 0; j < n; j++) nd_cp(r, (j + shift) % n, a, j, 1);
        return ndv(r);
    }
    ax = axis_norm(need_long(av, "axis"), a->nd);
    axis_split(a, ax, &outer, &len, &inner);
    shift = ((shift % len) + len) % len;
    for (o = 0; o < outer; o++) for (j = 0; j < len; j++)
        nd_cp(r, (o * len + (j + shift) % len) * inner, a, (o * len + j) * inner, inner);
    return ndv(r);
}
BI(np_tile)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long reps[ND_MAXD], sh[ND_MAXD], idx[ND_MAXD], i;
    int rn, nd, k;
    need(x, 2, 2, "tile");
    memset(sh, 0, sizeof(sh));
    rn = A(1).t == T_INT ? (reps[0] = A(1).u.i, 1) : shape_arg(A(1), reps);
    nd = a->nd > rn ? a->nd : rn;
    {
        long ash[ND_MAXD], rr[ND_MAXD];
        for (k = 0; k < nd; k++) {
            int ka = k - (nd - a->nd), kr = k - (nd - rn);
            ash[k] = ka >= 0 ? a->shape[ka] : 1;
            rr[k] = kr >= 0 ? reps[kr] : 1;
            sh[k] = ash[k] * rr[k];
        }
        r = nd_new(a->dt, nd, sh);
        for (k = 0; k < nd; k++) idx[k] = 0;
        for (i = 0; i < r->size; i++) {
            long off = 0, mul = 1;
            for (k = nd - 1; k >= 0; k--) { off += (idx[k] % ash[k]) * mul; mul *= ash[k]; }
            nd_cp(r, i, a, off, 1);
            for (k = nd - 1; k >= 0; k--) { if (++idx[k] < sh[k]) break; idx[k] = 0; }
        }
    }
    return ndv(r);
}
BI(np_repeat)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r, *src;
    Value rv = np_arg(x, 1, "repeats"), av = np_arg(x, 2, "axis");
    long outer, len, inner, o, j, i, c, total = 0, pos = 0, sh[ND_MAXD];
    Nd *reps = NULL;
    int ax, k;
    need(x, 2, 3, "repeat");
    if (rv.t == T_INT) c = rv.u.i;
    else { reps = nd_astype(nd_coerce(rv, &t), ND_INT); c = -1; }
    if (!np_has(av)) {
        long n = a->size;
        src = nd_reshaped(a, 1, &n);
        ax = 0;
    } else { src = a; ax = axis_norm(need_long(av, "axis"), a->nd); }
    axis_split(src, ax, &outer, &len, &inner);
    if (reps && reps->size != len && reps->size != 1)
        throw_new(cls_ValueError, "operands could not be broadcast together with shape (%ld,) (%ld,)", len, reps->size);
    for (j = 0; j < len; j++) total += c >= 0 ? c : reps->iv[reps->size == 1 ? 0 : j];
    for (k = 0; k < src->nd; k++) sh[k] = src->shape[k];
    sh[ax] = total;
    r = nd_new(src->dt, src->nd, sh);
    for (o = 0; o < outer; o++) {
        for (j = 0; j < len; j++) {
            long cnt = c >= 0 ? c : reps->iv[reps->size == 1 ? 0 : j];
            for (i = 0; i < cnt; i++) {
                nd_cp(r, (o * total + pos + i) * inner, src, (o * len + j) * inner, inner);
            }
            pos += cnt;
        }
        pos = 0;
    }
    return ndv(r);
}
BI(np_squeeze)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long sh[ND_MAXD];
    int k, rn = 0;
    Value av = np_arg(x, 1, "axis");
    for (k = 0; k < a->nd; k++) {
        if (a->shape[k] == 1 && (!np_has(av) || axis_norm(need_long(av, "axis"), a->nd) == k)) continue;
        if (np_has(av) && axis_norm(need_long(av, "axis"), a->nd) == k)
            throw_new(cls_ValueError, "cannot select an axis to squeeze out which has size not equal to one");
        sh[rn++] = a->shape[k];
    }
    return nd_ret(nd_reshaped(a, rn, sh));
}
BI(np_expand_dims)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long sh[ND_MAXD], ax;
    int k, j = 0;
    need(x, 2, 2, "expand_dims");
    ax = need_long(A(1), "axis");
    if (a->nd + 1 > ND_MAXD) throw_new(cls_ValueError, "maximum supported dimension for an ndarray is %d", ND_MAXD);
    if (ax < 0) ax += a->nd + 1;
    if (ax < 0 || ax > a->nd) throw_new(cls_ValueError, "axis %ld is out of bounds for array of dimension %d", ax, a->nd + 1);
    for (k = 0; k <= a->nd; k++) {
        if (k == ax) sh[k] = 1; else sh[k] = a->shape[j++];
    }
    return ndv(nd_reshaped(a, a->nd + 1, sh));
}
BI(np_swapaxes)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long i, idx[ND_MAXD], sh[ND_MAXD], st[ND_MAXD], mul = 1;
    int a1, a2, k;
    need(x, 3, 3, "swapaxes");
    a1 = axis_norm(need_long(A(1), "axis1"), a->nd);
    a2 = axis_norm(need_long(A(2), "axis2"), a->nd);
    for (k = a->nd - 1; k >= 0; k--) { st[k] = mul; mul *= a->shape[k]; }
    for (k = 0; k < a->nd; k++) sh[k] = a->shape[k];
    sh[a1] = a->shape[a2]; sh[a2] = a->shape[a1];
    { long t1 = st[a1]; st[a1] = st[a2]; st[a2] = t1; }
    r = nd_new(a->dt, a->nd, sh);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        long off = 0;
        for (k = 0; k < a->nd; k++) off += idx[k] * st[k];
        nd_cp(r, i, a, off, 1);
        for (k = a->nd - 1; k >= 0; k--) { if (++idx[k] < sh[k]) break; idx[k] = 0; }
    }
    return nd_ret(r);
}
BI(np_argwhere)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long cnt = 0, i, pos = 0, idx[ND_MAXD], sh[2];
    int k;
    for (i = 0; i < a->size; i++) if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) cnt++;
    sh[0] = cnt; sh[1] = a->nd;
    r = nd_new(ND_INT, 2, sh);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < a->size; i++) {
        if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) {
            for (k = 0; k < a->nd; k++) r->iv[pos * a->nd + k] = idx[k];
            pos++;
        }
        for (k = a->nd - 1; k >= 0; k--) { if (++idx[k] < a->shape[k]) break; idx[k] = 0; }
    }
    return ndv(r);
}
BI(np_isin)
{
    NdTmp ta, tb;
    Nd *a, *b, *r;
    long i, j;
    need(x, 2, 2, "isin");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    r = nd_new(ND_BOOL, a->nd, a->shape);
    for (i = 0; i < a->size; i++) {
        double v = nd_d(a, i);
        r->iv[i] = 0;
        for (j = 0; j < b->size; j++) if (nd_d(b, j) == v) { r->iv[i] = 1; break; }
    }
    return nd_ret(r);
}
BI(np_append)
{
    NdTmp ta, tb;
    Nd *a, *b, *arr[2];
    Value av = np_arg(x, 2, "axis");
    need(x, 2, 3, "append");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (!np_has(av)) {
        long na = a->size, nb = b->size;
        arr[0] = nd_reshaped(a, 1, &na); arr[1] = nd_reshaped(b, 1, &nb);
        return ndv(nd_concat(arr, 2, 0));
    }
    arr[0] = nd_astype(a, a->dt); arr[1] = nd_astype(b, b->dt);
    return ndv(nd_concat(arr, 2, axis_norm(need_long(av, "axis"), a->nd)));
}
BI(np_take)
{
    NdTmp ta, ti;
    Nd *a, *ix, *r, *src;
    Value av = np_arg(x, 2, "axis");
    long outer, len, inner, o, j, i, sh[ND_MAXD];
    int ax, k, rn = 0;
    need(x, 2, 3, "take");
    a = nd_coerce(A(0), &ta); ix = nd_coerce(A(1), &ti);
    if (ix->dt == ND_FLOAT) throw_new(cls_TypeError, "Cannot cast array data from dtype('float64') to dtype('int64') according to the rule 'same_kind'");
    if (!np_has(av)) { long n = a->size; src = nd_reshaped(a, 1, &n); ax = 0; }
    else { src = a; ax = axis_norm(need_long(av, "axis"), a->nd); }
    axis_split(src, ax, &outer, &len, &inner);
    for (k = 0; k < ax; k++) sh[rn++] = src->shape[k];
    for (k = 0; k < ix->nd; k++) sh[rn++] = ix->shape[k];
    for (k = ax + 1; k < src->nd; k++) sh[rn++] = src->shape[k];
    r = nd_new(src->dt, rn, sh);
    for (o = 0; o < outer; o++)
        for (j = 0; j < ix->size; j++) {
            long p = ix->iv[j];
            if (p < -len || p >= len)
                throw_new(cls_IndexError, "index %ld is out of bounds for axis %d with size %ld", p, ax, len);
            if (p < 0) p += len;
            for (i = 0; i < 1; i++) nd_cp(r, (o * ix->size + j) * inner, src, (o * len + p) * inner, inner);
        }
    return nd_ret(r);
}
BI(np_shape) { NdTmp t; Nd *a = nd_arg0(x, &t); Value tv[ND_MAXD]; int k; for (k = 0; k < a->nd; k++) tv[k] = mkint(a->shape[k]); return mktuple_from(tv, a->nd); }
BI(np_ndim) { NdTmp t; Nd *a = nd_arg0(x, &t); return mkint(a->nd); }
BI(np_size) { NdTmp t; Nd *a = nd_arg0(x, &t); return mkint(a->size); }
BI(np_isscalar)
{
    need(x, 1, 1, "isscalar");
    return tobool(A(0).t == T_INT || A(0).t == T_FLOAT || A(0).t == T_BOOL || A(0).t == T_BIG || A(0).t == T_STR);
}
BI(np_linspace2)
{
    Value rs = np_arg(x, 4, "retstep");
    Value r = np_linspace(x), tv[2];
    if (np_has(rs) && py_truthy(rs)) {
        Nd *a = AS_ND(r);
        Value nv = x->n > 2 ? A(2) : kwget(x, "num"), ev = x->n > 3 ? A(3) : kwget(x, "endpoint");
        long num = np_has(nv) ? need_long(nv, "num") : 50;
        int endpoint = !np_has(ev) || py_truthy(ev);
        long div = endpoint ? num - 1 : num;
        double step = div > 0 ? (to_dbl(A(1)) - to_dbl(A(0))) / (double)div : HUGE_VAL - HUGE_VAL;
        (void)a;
        tv[0] = r; tv[1] = mkfloat(step);
        return mktuple_from(tv, 2);
    }
    return r;
}
BI(np_tri)
{
    long n, m, k = 0, i, j, sh[2];
    Value mv = np_arg(x, 1, "M"), kv = np_arg(x, 2, "k");
    Nd *r;
    n = need_long(A(0), "N");
    m = np_has(mv) ? need_long(mv, "M") : n;
    if (np_has(kv)) k = need_long(kv, "k");
    sh[0] = n; sh[1] = m;
    r = nd_new(dtype_kw(x, ND_FLOAT), 2, sh);
    for (i = 0; i < n; i++) for (j = 0; j < m; j++) {
        int v = (j - i <= k);
        if (r->dt == ND_FLOAT) r->dv[i * m + j] = v; else r->iv[i * m + j] = v;
    }
    return ndv(r);
}
BI(np_vander)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value nv = np_arg(x, 1, "N"), iv = np_arg(x, 2, "increasing");
    long n = np_has(nv) ? need_long(nv, "N") : a->size, i, j, sh[2];
    int inc = np_has(iv) && py_truthy(iv);
    sh[0] = a->size; sh[1] = n;
    r = nd_new(a->dt == ND_FLOAT ? ND_FLOAT : ND_INT, 2, sh);
    for (i = 0; i < a->size; i++)
        for (j = 0; j < n; j++) {
            long e = inc ? j : n - 1 - j;
            if (r->dt == ND_FLOAT) r->dv[i * n + j] = pow(a->dv[i], (double)e);
            else { long p = 1, q; for (q = 0; q < e; q++) p *= a->iv[i]; r->iv[i * n + j] = p; }
        }
    return ndv(r);
}

static void np_init2(Module *m)
{
    mod_def(m, "arctan2", np_arctan2); mod_def(m, "hypot", np_hypot);
    mod_def(m, "copysign", np_copysign); mod_def(m, "fmax", np_fmax); mod_def(m, "fmin", np_fmin);
    mod_def(m, "isclose", np_isclose);
    mod_def(m, "logical_and", np_logical_and); mod_def(m, "logical_or", np_logical_or);
    mod_def(m, "logical_xor", np_logical_xor); mod_def(m, "logical_not", np_logical_not);
    mod_def(m, "exp2", np_exp2); mod_def(m, "expm1", np_expm1); mod_def(m, "log1p", np_log1p);
    mod_def(m, "cbrt", np_cbrt); mod_def(m, "arcsinh", np_arcsinh);
    mod_def(m, "arccosh", np_arccosh); mod_def(m, "arctanh", np_arctanh);
    mod_def(m, "deg2rad", np_deg2rad); mod_def(m, "radians", np_deg2rad);
    mod_def(m, "rad2deg", np_rad2deg); mod_def(m, "degrees", np_rad2deg);
    mod_def(m, "reciprocal", np_reciprocal); mod_def(m, "trunc", np_trunc);
    mod_def(m, "fix", np_trunc); mod_def(m, "rint", np_rint); mod_def(m, "sinc", np_sinc);
    mod_def(m, "nan_to_num", np_nan_to_num);
    mod_def(m, "nansum", np_nansum); mod_def(m, "nanprod", np_nanprod);
    mod_def(m, "nanmean", np_nanmean); mod_def(m, "nanvar", np_nanvar);
    mod_def(m, "nanstd", np_nanstd); mod_def(m, "nanmax", np_nanmax);
    mod_def(m, "nanmin", np_nanmin); mod_def(m, "nanargmax", np_nanargmax);
    mod_def(m, "nanargmin", np_nanargmin);
    mod_def(m, "ptp", np_ptp); mod_def(m, "average", np_average);
    mod_def(m, "diff", np_diff); mod_def(m, "ediff1d", np_ediff1d);
    mod_def(m, "gradient", np_gradient); mod_def(m, "searchsorted", np_searchsorted);
    mod_def(m, "percentile", np_percentile); mod_def(m, "quantile", np_quantile);
    mod_def(m, "median", np_median2); mod_def(m, "histogram", np_histogram);
    mod_def(m, "bincount", np_bincount); mod_def(m, "cov", np_cov);
    mod_def(m, "corrcoef", np_corrcoef); mod_def(m, "tril", np_tril);
    mod_def(m, "triu", np_triu); mod_def(m, "kron", np_kron); mod_def(m, "outer", np_outer);
    mod_def(m, "inner", np_inner); mod_def(m, "vdot", np_vdot); mod_def(m, "cross", np_cross);
    mod_def(m, "meshgrid", np_meshgrid); mod_def(m, "polyfit", np_polyfit);
    mod_def(m, "polyval", np_polyval); mod_def(m, "convolve", np_convolve);
    mod_def(m, "trapz", np_trapz); mod_def(m, "trapezoid", np_trapz);
    mod_def(m, "flip", np_flip); mod_def(m, "fliplr", np_fliplr); mod_def(m, "flipud", np_flipud);
    mod_def(m, "roll", np_roll); mod_def(m, "tile", np_tile); mod_def(m, "repeat", np_repeat);
    mod_def(m, "squeeze", np_squeeze); mod_def(m, "expand_dims", np_expand_dims);
    mod_def(m, "swapaxes", np_swapaxes); mod_def(m, "argwhere", np_argwhere);
    mod_def(m, "isin", np_isin); mod_def(m, "append", np_append); mod_def(m, "take", np_take);
    mod_def(m, "shape", np_shape); mod_def(m, "ndim", np_ndim); mod_def(m, "size", np_size);
    mod_def(m, "isscalar", np_isscalar); mod_def(m, "tri", np_tri); mod_def(m, "vander", np_vander);
    mod_def(m, "linspace", np_linspace2);
    mod_val(m, "float32", mkobj(cls_float, T_CLASS));
    mod_val(m, "int32", mkobj(cls_int, T_CLASS));
    mod_val(m, "int_", mkobj(cls_int, T_CLASS));
    mod_val(m, "float_", mkobj(cls_float, T_CLASS));
    mod_val(m, "bool", mkobj(cls_bool, T_CLASS));
    mod_val(m, "euler_gamma", mkfloat(0.57721566490153286061));
    mod_val(m, "Inf", mkfloat(HUGE_VAL));
    mod_val(m, "NaN", mkfloat(HUGE_VAL - HUGE_VAL));
    mod_val(m, "PINF", mkfloat(HUGE_VAL));
    mod_val(m, "NINF", mkfloat(-HUGE_VAL));
}
