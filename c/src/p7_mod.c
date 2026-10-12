/* =========================================================================
   p7_mod.c   builtin modules (math random sys time) and initialisation
   ========================================================================= */
static void re_init(void);
static void json_init(void);
static void sock_init(void);
static Module *new_module(const char *name)
{
    Module *m = (Module *)gc_alloc(sizeof(Module), T_MODULE);
    m->name = intern_c(name);
    m->ns.k = NULL; m->ns.v = NULL; m->ns.n = m->ns.cap = 0;
    m->gp = NULL;
    m->gmap = NULL;
    if (sys_modules_v.t == T_DICT)
        dict_set(AS_DICT(sys_modules_v), mkobj(m->name, T_STR), mkobj(m, T_MODULE));
    return m;
}
static void mod_def(Module *m, const char *name, BFn fn)
{
    amap_set(&m->ns, intern_c(name), mkbuiltin(name, fn));
}
static void mod_val(Module *m, const char *name, Value v)
{
    amap_set(&m->ns, intern_c(name), v);
}

/* ---- math ------------------------------------------------------------- */
static double dbl_arg(Args *x, int i)
{
    if (!is_num(A(i))) {
        if (A(i).t == T_INST) {
            Value *m = class_find(AS_INST(A(i))->cls, S_float);
            if (m) return to_dbl(call1(*m, A(i)));
        }
        throw_new(cls_TypeError, "must be real number, not %.40s", type_name(A(i)));
    }
    return to_dbl(A(i));
}
static void domain_check(int ok)
{
    if (!ok) throw_new(cls_ValueError, "math domain error");
}
#define MATH1(name, expr, dom) \
    BI(name) { double v; \
        need(x, 1, 1, "math"); \
        v = dbl_arg(x, 0); \
        domain_check(dom); \
        return mkfloat(expr); \
    }
MATH1(m_sqrt, sqrt(v), v >= 0)
MATH1(m_sin, sin(v), 1)
MATH1(m_cos, cos(v), 1)
MATH1(m_tan, tan(v), 1)
MATH1(m_asin, asin(v), v >= -1 && v <= 1)
MATH1(m_acos, acos(v), v >= -1 && v <= 1)
MATH1(m_atan, atan(v), 1)
MATH1(m_sinh, sinh(v), 1)
MATH1(m_cosh, cosh(v), 1)
MATH1(m_tanh, tanh(v), 1)
MATH1(m_exp, exp(v), 1)
static double log2_d(double v)
{
    int e;
    double m;
    if (v >= 0.5 && v <= 2.0) return log(v) / log(2.0);
    m = frexp(v, &e);                    /* exact for powers of two */
    return (double)e + log(m) / log(2.0);
}
static double ln_d(double v) { return log(v); }
static double log10_d(double v) { return log10(v); }
/* log of a number that may be an integer too big for a double: f(m) + e * f(2) for x = m * 2**e
   (CPython's loghelper); kind selects f */
static double log_any(Value v, double (*f)(double))
{
    if (v.t == T_BIG && AS_BIG(v)->sign > 0) {
        long bl = int_bitlen(v), sh = bl - 64;
        double m;
        Value top = sh > 0 ? int_floordiv(v, int_pow2(sh)) : v;
        m = ldexp(int_todouble(top), (int)(-(sh > 0 ? 64 : bl)));
        if (m >= 1.0) { m = 0.5; bl++; }
        return f(m) + (double)bl * f(2.0);
    }
    if (v.t == T_BIG) domain_check(0);
    {
        double d = to_dbl(v);
        domain_check(d > 0);
        return f(d);
    }
}
static Value log_arg(Args *x, int i)
{
    Value v = A(i);
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET) v = AS_INST(v)->pay;
    if (is_num(v)) return v;
    return mkfloat(dbl_arg(x, i));
}
BI(m_log2)
{
    need(x, 1, 1, "log2");
    return mkfloat(log_any(log_arg(x, 0), log2_d));
}
BI(m_log10)
{
    need(x, 1, 1, "log10");
    return mkfloat(log_any(log_arg(x, 0), log10_d));
}
MATH1(m_fabs, fabs(v), 1)
MATH1(m_degrees, v * 180.0 / 3.14159265358979323846, 1)
MATH1(m_radians, v * 3.14159265358979323846 / 180.0, 1)
BI(m_log)
{
    double v;
    need(x, 1, 2, "log");
    v = log_any(log_arg(x, 0), ln_d);
    if (x->n == 2) {
        double b = log_any(log_arg(x, 1), ln_d);
        if (b == 0.0) throw_new(cls_ZeroDivisionError, "float division by zero");
        return mkfloat(v / b);
    }
    return mkfloat(v);
}
BI(m_pow)
{
    need(x, 2, 2, "pow");
    return mkfloat(pow(dbl_arg(x, 0), dbl_arg(x, 1)));
}
BI(m_atan2)
{
    need(x, 2, 2, "atan2");
    return mkfloat(atan2(dbl_arg(x, 0), dbl_arg(x, 1)));
}
BI(m_hypot)
{
    need(x, 2, 2, "hypot");
    return mkfloat(sqrt(dbl_arg(x, 0) * dbl_arg(x, 0) + dbl_arg(x, 1) * dbl_arg(x, 1)));
}
BI(m_fmod)
{
    need(x, 2, 2, "fmod");
    return mkfloat(fmod(dbl_arg(x, 0), dbl_arg(x, 1)));
}
BI(m_copysign)
{
    double a, b;
    need(x, 2, 2, "copysign");
    a = fabs(dbl_arg(x, 0));
    b = dbl_arg(x, 1);
    return mkfloat((b < 0 || (b == 0 && 1.0 / b < 0)) ? -a : a);
}
static double next_after(double x, double y)
{
    int e;
    double m, step, r;
    if (x != x || y != y) return x != x ? x : y;
    if (x == y) return y;
    if (x == 0) return y > 0 ? ldexp(1.0, -1074) : -ldexp(1.0, -1074);
    m = frexp(x, &e);
    step = ldexp(1.0, e - 53);
    if (step < ldexp(1.0, -1074)) step = ldexp(1.0, -1074);
    if (((y > x) != (x > 0)) && fabs(m) == 0.5 && e - 54 >= -1074) step = ldexp(1.0, e - 54);
    r = y > x ? x + step : x - step;
    return r;
}
BI(m_nextafter)
{
    need(x, 2, 2, "nextafter");
    return mkfloat(next_after(dbl_arg(x, 0), dbl_arg(x, 1)));
}
BI(m_ulp)
{
    double v;
    need(x, 1, 1, "ulp");
    v = fabs(dbl_arg(x, 0));
    if (v != v || v > DBL_MAX) return mkfloat(v);
    if (v == DBL_MAX) return mkfloat(v - next_after(v, 0.0));
    return mkfloat(next_after(v, HUGE_VAL) - v);
}
static Value dbl_to_int(double d)
{
    return int_from_double(d);
}
BI(m_floor)
{
    need(x, 1, 1, "floor");
    if (is_intv(A(0))) return A(0).t == T_BOOL ? mkint(A(0).u.i) : A(0);
    return dbl_to_int(floor(dbl_arg(x, 0)));
}
BI(m_ceil)
{
    need(x, 1, 1, "ceil");
    if (is_intv(A(0))) return A(0).t == T_BOOL ? mkint(A(0).u.i) : A(0);
    return dbl_to_int(ceil(dbl_arg(x, 0)));
}
BI(m_trunc)
{
    double d;
    need(x, 1, 1, "trunc");
    if (is_intv(A(0))) return A(0);
    d = dbl_arg(x, 0);
    return dbl_to_int(d < 0 ? ceil(d) : floor(d));
}
BI(m_isnan) { need(x, 1, 1, "isnan"); return mkbool(dbl_arg(x, 0) != dbl_arg(x, 0)); }
BI(m_isinf)
{
    double d;
    need(x, 1, 1, "isinf");
    d = dbl_arg(x, 0);
    return mkbool(d == d && (d > DBL_MAX || d < -DBL_MAX));
}
BI(m_isfinite)
{
    double d;
    need(x, 1, 1, "isfinite");
    d = dbl_arg(x, 0);
    return mkbool(d == d && d <= DBL_MAX && d >= -DBL_MAX);
}
static Value int_gcd(Value a, Value b)
{
    if (int_sign(a) < 0) a = int_neg(a);
    if (int_sign(b) < 0) b = int_neg(b);
    while (int_sign(b) != 0) {
        Value t = int_mod(a, b);
        a = b;
        b = t;
    }
    return a;
}
BI(m_gcd)
{
    Value g = mkint(0);
    int i;
    for (i = 0; i < x->n; i++) {
        if (!is_intv(A(i)))
            throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
            type_name(A(i)));
        g = int_gcd(g, A(i));
    }
    return g;
}
BI(m_lcm)
{
    Value l = mkint(1);
    int i;
    for (i = 0; i < x->n; i++) {
        Value g;
        if (int_sign(A(i)) == 0) return mkint(0);
        g = int_gcd(l, A(i));
        l = int_floordiv(int_mul(l, A(i)), g);
        if (int_sign(l) < 0) l = int_neg(l);
    }
    return l;
}
BI(m_factorial)
{
    long n, i;
    Value r = mkint(1);
    need(x, 1, 1, "factorial");
    if (!is_intv(A(0)))
        throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
        type_name(A(0)));
    if (int_sign(A(0)) < 0)
        throw_new(cls_ValueError, "factorial() not defined for negative values");
    n = to_index(A(0));
    for (i = 2; i <= n; i++) r = int_mul(r, mkint(i));
    return r;
}
BI(m_isqrt)
{
    Value n, r, nr;
    need(x, 1, 1, "isqrt");
    n = A(0);
    if (!is_intv(n))
        throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
        type_name(n));
    if (int_sign(n) < 0) throw_new(cls_ValueError, "isqrt() argument must be nonnegative");
    if (n.t != T_BIG) {
        long s = (long)sqrt((double)n.u.i);
        while (s * s > n.u.i) s--;
        while ((s + 1) * (s + 1) <= n.u.i) s++;
        return mkint(s);
    }
    r = int_pow2((int_bitlen(n) + 1) / 2);          /* at least sqrt(n); Newton from above */
    for (;;) {
        nr = int_floordiv(int_add(r, int_floordiv(n, r)), mkint(2));
        if (int_cmp(nr, r) >= 0) break;
        r = nr;
    }
    return r;
}
BI(m_comb)
{
    Value r = mkint(1);
    long n, k, i;
    need(x, 2, 2, "comb");
    n = to_index(A(0)); k = to_index(A(1));
    if (n < 0 || k < 0) throw_new(cls_ValueError, "n and k must be non-negative integers");
    if (k > n) return mkint(0);
    if (k > n - k) k = n - k;
    for (i = 1; i <= k; i++) r = int_floordiv(int_mul(r, mkint(n - k + i)), mkint(i));
    return r;
}
BI(m_prod)
{
    Value r = mkint(1), it, y;
    need(x, 1, 1, "prod");
    it = get_iter(A(0));
    VPUSH(it);
    VPUSH(r);
    while (iter_next(it, &y)) { r = binop(B_MUL, r, y); vsp[-1] = r; }
    vsp -= 2;
    return r;
}

/* ---- more math: isclose fsum log1p expm1 erf gamma ... (portable C89 versions) ---- */
BI(m_isclose)
{
    double a, b, rel = 1e-9, ab = 0.0, diff;
    Value v;
    need(x, 2, 4, "isclose");
    a = dbl_arg(x, 0); b = dbl_arg(x, 1);
    v = x->n > 2 ? A(2) : kwget(x, "rel_tol");
    if (v.t != T_UNSET) rel = to_dbl(v);
    v = x->n > 3 ? A(3) : kwget(x, "abs_tol");
    if (v.t != T_UNSET) ab = to_dbl(v);
    if (a == b) return TRUE_V;
    if (a - a != 0 || b - b != 0) return FALSE_V;
    diff = fabs(b - a);
    return mkbool(diff <= fabs(rel * b) || diff <= fabs(rel * a) || diff <= ab);
}
BI(m_fsum)
{
    double *p = (double *)xmalloc(sizeof(double) * 64), xv, y, hi, lo = 0.0, yr;
    double special = 0.0, inf_sum = 0.0;
    long np = 0, cap = 64, i, j, n;
    Value it, v;
    need(x, 1, 1, "fsum");
    it = get_iter(A(0));
    VPUSH(it);
    while (iter_next(it, &v)) {
        xv = to_dbl(v);
        if (xv - xv != 0.0) {                    /* infinity or NaN */
            special += xv;
            if (xv == xv) inf_sum += xv;
            continue;
        }
        for (i = j = 0; j < np; j++) {
            y = p[j];
            if (fabs(xv) < fabs(y)) { double t = xv; xv = y; y = t; }
            hi = xv + y;
            if (hi - hi != 0.0) {
                free(p);
                throw_new(cls_OverflowError, "intermediate overflow in fsum");
            }
            yr = hi - xv;
            lo = y - yr;
            if (lo != 0.0) p[i++] = lo;
            xv = hi;
        }
        np = i;
        if (np >= cap) { cap *= 2; p = (double *)xrealloc(p, sizeof(double) * (size_t)cap); }
        p[np++] = xv;
    }
    vsp--;
    if (special != 0.0 || special != special) {
        free(p);
        if (inf_sum != inf_sum) throw_new(cls_ValueError, "-inf + inf in fsum");
        return mkfloat(special);
    }
    n = np;
    hi = 0.0;
    if (n > 0) {
        hi = p[--n];
        lo = 0.0;
        while (n > 0) {
            xv = hi;
            y = p[--n];
            hi = xv + y;
            yr = hi - xv;
            lo = y - yr;
            if (lo != 0.0) break;
        }
        if (n > 0 && ((lo < 0.0 && p[n - 1] < 0.0) || (lo > 0.0 && p[n - 1] > 0.0))) {
            y = lo * 2.0;
            xv = hi + y;
            yr = xv - hi;
            if (y == yr) hi = xv;
        }
    }
    free(p);
    return mkfloat(hi);
}
static double d_log1p(double x) { return n_log1p(x); }
static double d_expm1(double x) { return n_expm1(x); }
static double d_erf_series(double x)
{
    double term = x, sum = x, x2 = x * x;
    int n;
    for (n = 0; n < 400; n++) {
        term *= 2.0 * x2 / (2.0 * n + 3.0);
        sum += term;
        if (fabs(term) < 1e-17 * fabs(sum)) break;
    }
    return 1.1283791670955126 * exp(-x2) * sum;
}
static double d_erfc_cf(double x)
{
    /* continued fraction for x > 0, modified Lentz */
    double tiny = 1e-300, f, c, d, delta;
    int n;
    f = x; if (f == 0) f = tiny;
    c = f; d = 0.0;
    for (n = 1; n < 2000; n++) {
        double an = n * 0.5;
        d = x + an * d; if (d == 0) d = tiny;
        c = x + an / c; if (c == 0) c = tiny;
        d = 1.0 / d;
        delta = c * d;
        f *= delta;
        if (fabs(delta - 1.0) < 1e-16) break;
    }
    return exp(-x * x) / f * 0.5641895835477563;
}
static double d_erf(double x)
{
    double a = fabs(x), r;
    if (x != x) return x;
    if (a < 2.0) r = d_erf_series(a);
    else if (a > 6.0) r = 1.0;
    else r = 1.0 - d_erfc_cf(a);
    return x < 0 ? -r : r;
}
static double d_erfc(double x)
{
    if (x != x) return x;
    if (x < 0.5) return 1.0 - d_erf(x);
    if (x > 27.0) return 0.0;
    return d_erfc_cf(x);
}
static double d_lanczos_sum(double x)
{
    static const double c[9] = { 0.99999999999980993, 676.5203681218851, -1259.1392167224028,
        771.32342877765313, -176.61502916214059, 12.507343278686905, -0.13857109526572012,
        9.9843695780195716e-6, 1.5056327351493116e-7 };
    double a = c[0];
    int i;
    for (i = 1; i < 9; i++) a += c[i] / (x + i);
    return a;
}
static double d_gamma(double x)
{
    double PI = 3.14159265358979323846;
    if (x == floor(x) && x > 0 && x < 171.0) {
        double r = 1.0;
        int i, n = (int)x;
        for (i = 2; i < n; i++) r *= (double)i;
        return r;
    }
    if (x < 0.5) return PI / (sin(PI * x) * d_gamma(1.0 - x));
    x -= 1.0;
    {
        double t = x + 7.5;
        return sqrt(2.0 * PI) * pow(t, x + 0.5) * exp(-t) * d_lanczos_sum(x);
    }
}
static double d_lgamma(double x)
{
    double PI = 3.14159265358979323846;
    if (x < 0.5) return log(PI / fabs(sin(PI * x))) - d_lgamma(1.0 - x);
    x -= 1.0;
    {
        double t = x + 7.5;
        return 0.5 * log(2.0 * PI) + (x + 0.5) * log(t) - t + log(d_lanczos_sum(x));
    }
}
#define MATH1B(name, expr) \
    BI(name) { double v; need(x, 1, 1, "math"); v = dbl_arg(x, 0); return mkfloat(expr); }
MATH1B(m_log1p, d_log1p(v))
MATH1B(m_expm1, d_expm1(v))
MATH1B(m_erf, d_erf(v))
MATH1B(m_erfc, d_erfc(v))
MATH1B(m_exp2, pow(2.0, v))
BI(m_gamma)
{
    double v;
    need(x, 1, 1, "gamma");
    v = dbl_arg(x, 0);
    if (v == floor(v) && v <= 0) throw_new(cls_ValueError, "math domain error");
    if (v > 171.7) throw_new(cls_OverflowError, "math range error");
    return mkfloat(d_gamma(v));
}
BI(m_lgamma)
{
    double v;
    need(x, 1, 1, "lgamma");
    v = dbl_arg(x, 0);
    if (v == floor(v) && v <= 0) throw_new(cls_ValueError, "math domain error");
    return mkfloat(d_lgamma(v));
}
BI(m_asinh)
{
    double v, a, r;
    need(x, 1, 1, "asinh");
    v = dbl_arg(x, 0);
    a = fabs(v);
    if (a < 1e-8) return mkfloat(v);
    r = a > 1e8 ? log(a) + 0.69314718055994530942 : d_log1p(a + a * a / (1.0 + sqrt(1.0 + a * a)));
    return mkfloat(v < 0 ? -r : r);
}
BI(m_acosh)
{
    double v;
    need(x, 1, 1, "acosh");
    v = dbl_arg(x, 0);
    domain_check(v >= 1.0);
    if (v > 1e8) return mkfloat(log(v) + 0.69314718055994530942);
    return mkfloat(d_log1p((v - 1.0) + sqrt((v - 1.0) * (v + 1.0))));
}
BI(m_atanh)
{
    double v;
    need(x, 1, 1, "atanh");
    v = dbl_arg(x, 0);
    domain_check(v > -1.0 && v < 1.0);
    return mkfloat(0.5 * d_log1p(2.0 * v / (1.0 - v)));
}
BI(m_cbrt)
{
    double v, a, r;
    need(x, 1, 1, "cbrt");
    v = dbl_arg(x, 0);
    if (v == 0 || v - v != 0) return mkfloat(v);
    a = fabs(v);
    r = pow(a, 1.0 / 3.0);
    r = r - (r * r * r - a) / (3.0 * r * r);
    return mkfloat(v < 0 ? -r : r);
}
BI(m_remainder)
{
    double a, b, r, q;
    need(x, 2, 2, "remainder");
    a = dbl_arg(x, 0); b = dbl_arg(x, 1);
    if (b == 0) throw_new(cls_ValueError, "math domain error");
    r = fmod(a, b);
    q = (a - r) / b;
    if (fabs(r) * 2.0 > fabs(b) || (fabs(r) * 2.0 == fabs(b) && fmod(q, 2.0) != 0.0))
        r -= (r < 0 ? -fabs(b) : fabs(b));
    return mkfloat(r);
}
BI(m_modf)
{
    double ip, fp;
    need(x, 1, 1, "modf");
    fp = modf(dbl_arg(x, 0), &ip);
    return mktuple2(mkfloat(fp), mkfloat(ip));
}
BI(m_frexp)
{
    int e;
    double m;
    need(x, 1, 1, "frexp");
    m = frexp(dbl_arg(x, 0), &e);
    return mktuple2(mkfloat(m), mkint(e));
}
BI(m_ldexp)
{
    need(x, 2, 2, "ldexp");
    return mkfloat(ldexp(dbl_arg(x, 0), (int)to_index(A(1))));
}
BI(m_perm)
{
    Value r = mkint(1);
    long n, k, i;
    need(x, 1, 2, "perm");
    n = to_index(A(0));
    k = x->n > 1 && A(1).t != T_NONE ? to_index(A(1)) : n;
    if (n < 0 || k < 0) throw_new(cls_ValueError, "n and k must be non-negative integers");
    if (k > n) return mkint(0);
    for (i = 0; i < k; i++) r = int_mul(r, mkint(n - i));
    return r;
}
BI(m_dist)
{
    Value p, q;
    double s = 0.0;
    long i;
    need(x, 2, 2, "dist");
    p = iter_to_list(A(0));
    VPUSH(p);
    q = iter_to_list(A(1));
    VPUSH(q);
    if (AS_LIST(p)->n != AS_LIST(q)->n)
        throw_new(cls_ValueError, "both points must have the same number of dimensions");
    for (i = 0; i < AS_LIST(p)->n; i++) {
        double d = to_dbl(AS_LIST(p)->a[i]) - to_dbl(AS_LIST(q)->a[i]);
        s += d * d;
    }
    vsp -= 2;
    return mkfloat(sqrt(s));
}

/* ---- sys / time ------------------------------------------------------- */
static Value std_file(FILE *fp, int out)
{
    return new_file_obj(fp, out, 0, NONE_V, fp == stdin ? "r" : "w");
}
BI(t_time)
{
    (void)x;
    return mkfloat(host_time());
}
BI(t_clock)
{
    (void)x;
    return mkfloat(host_clock());
}
/* nanoseconds as an exact integer: whole seconds * 10**9 + the fraction */
static Value ns_value(double t)
{
    double secs = floor(t);
    return int_add(int_mul(int_from_double(secs), mkint(1000000000L)),
                   mkint((long)((t - secs) * 1e9)));
}
BI(t_time_ns)
{
    (void)x;
    return ns_value(host_time());
}
BI(t_clock_ns)
{
    (void)x;
    return ns_value(host_clock());
}
/* _gmtime(secs) / _localtime(secs): (year, month, day, hour, minute, second, weekday, yearday,
   isdst, zone, offset) like the sequence of time.struct_time; the pure Python overlay pylib/time.py
   wraps them */
static Value tm_tuple(Args *x, int local)
{
    long f[10];
    char zone[64];
    Value v[11];
    double secs;
    int i;
    need(x, 1, 1, "gmtime");
    secs = floor(dbl_arg(x, 0));
    if (!host_timeparts(secs, local, f, zone, (int)sizeof(zone)))
        throw_new(cls_OverflowError, "timestamp out of range for platform time_t");
    for (i = 0; i < 9; i++) v[i] = mkint(f[i]);
    v[9] = mkcstr(zone);
    v[10] = mkint(f[9]);
    return mktuple_from(v, 11);
}
BI(t_gmtime_raw) { return tm_tuple(x, 0); }
BI(t_localtime_raw) { return tm_tuple(x, 1); }
BI(t_mktime_raw)
{
    long f[9];
    double out;
    int i;
    Value t;
    need(x, 1, 1, "mktime");
    t = unpay(A(0));
    if (t.t != T_TUPLE || AS_LIST(t)->n < 9)
        throw_new(cls_TypeError, "mktime(): illegal time tuple argument");
    for (i = 0; i < 9; i++) f[i] = to_index(AS_LIST(t)->a[i]);
    if (!host_mktime(f, &out)) throw_new(cls_OverflowError, "mktime argument out of range");
    return mkfloat(out);
}
BI(t_tzinfo_raw)
{
    long tz, alt;
    int dst;
    char sn[64], dn[64];
    Value v[5];
    (void)x;
    if (!host_tzinfo(&tz, &alt, &dst, sn, dn, (int)sizeof(sn)))
        throw_new(cls_OSError, "time zone information is not available");
    v[0] = mkint(tz); v[1] = mkint(alt); v[2] = mkint(dst); v[3] = mkcstr(sn); v[4] = mkcstr(dn);
    return mktuple_from(v, 5);
}
BI(t_sleep)
{
    double d = dbl_arg(x, 0);
    fflush(stdout);
    host_sleep(d);
    return NONE_V;
}
BI(sys_getrec)
{
    (void)x;
    return mkint(MAX_DEPTH);
}
BI(sys_setrec)
{
    (void)x;
    return NONE_V;
}

/* ---- initialisation --------------------------------------------------- */
static Class *cls_dummy_exc[40];
static struct { const char *name; Class **var; Class **base; } exc_table[] = {
    { "BaseException", &cls_BaseException, NULL },
    { "Exception", &cls_Exception, &cls_BaseException },
    { "ArithmeticError", &cls_ArithmeticError, &cls_Exception },
    { "ZeroDivisionError", &cls_ZeroDivisionError, &cls_ArithmeticError },
    { "OverflowError", &cls_OverflowError, &cls_ArithmeticError },
    { "LookupError", &cls_LookupError, &cls_Exception },
    { "IndexError", &cls_IndexError, &cls_LookupError },
    { "KeyError", &cls_KeyError, &cls_LookupError },
    { "ValueError", &cls_ValueError, &cls_Exception },
    { "TypeError", &cls_TypeError, &cls_Exception },
    { "NameError", &cls_NameError, &cls_Exception },
    { "UnboundLocalError", &cls_UnboundLocalError, &cls_NameError },
    { "AttributeError", &cls_AttributeError, &cls_Exception },
    { "RuntimeError", &cls_RuntimeError, &cls_Exception },
    { "RecursionError", &cls_RecursionError, &cls_RuntimeError },
    { "NotImplementedError", &cls_NotImplementedError, &cls_RuntimeError },
    { "StopIteration", &cls_StopIteration, &cls_Exception },
    { "AssertionError", &cls_AssertionError, &cls_Exception },
    { "SystemExit", &cls_SystemExit, &cls_BaseException },
    { "KeyboardInterrupt", &cls_KeyboardInterrupt, &cls_BaseException },
    { "GeneratorExit", &cls_GeneratorExit, &cls_BaseException },
    { "ImportError", &cls_ImportError, &cls_Exception },
    { "OSError", &cls_OSError, &cls_Exception },
    { "EOFError", &cls_EOFError, &cls_Exception },
    { "SyntaxError", &cls_SyntaxError, &cls_Exception },
    { "FileNotFoundError", &cls_FileNotFoundError, &cls_OSError },
    { "ModuleNotFoundError", &cls_ModuleNotFoundError, &cls_ImportError },
    { "FloatingPointError", &cls_dummy_exc[0], &cls_ArithmeticError },
    { "UnicodeError", &cls_dummy_exc[1], &cls_ValueError },
    { "UnicodeDecodeError", &cls_UnicodeDecodeError, &cls_dummy_exc[1] },
    { "UnicodeEncodeError", &cls_dummy_exc[3], &cls_dummy_exc[1] },
    { "MemoryError", &cls_MemoryError, &cls_Exception },
    { "ReferenceError", &cls_dummy_exc[5], &cls_Exception },
    { "BufferError", &cls_dummy_exc[6], &cls_Exception },
    { "SystemError", &cls_dummy_exc[7], &cls_Exception },
    { "StopAsyncIteration", &cls_dummy_exc[8], &cls_Exception },
    { "PermissionError", &cls_PermissionError, &cls_OSError },
    { "FileExistsError", &cls_FileExistsError, &cls_OSError },
    { "IsADirectoryError", &cls_IsADirectoryError, &cls_OSError },
    { "NotADirectoryError", &cls_NotADirectoryError, &cls_OSError },
    { "TimeoutError", &cls_TimeoutError, &cls_OSError },
    { "ConnectionError", &cls_ConnectionError, &cls_OSError },
    { "ConnectionRefusedError", &cls_ConnectionRefusedError, &cls_ConnectionError },
    { "ConnectionResetError", &cls_ConnectionResetError, &cls_ConnectionError },
    { "ConnectionAbortedError", &cls_ConnectionAbortedError, &cls_ConnectionError },
    { "BrokenPipeError", &cls_BrokenPipeError, &cls_ConnectionError },
    { "BlockingIOError", &cls_BlockingIOError, &cls_OSError },
    { "InterruptedError", &cls_InterruptedError, &cls_OSError },
    { "IndentationError", &cls_dummy_exc[15], &cls_SyntaxError },
    { "TabError", &cls_dummy_exc[16], &cls_dummy_exc[15] },
    { "Warning", &cls_dummy_exc[17], &cls_Exception },
    { "UserWarning", &cls_dummy_exc[18], &cls_dummy_exc[17] },
    { "DeprecationWarning", &cls_dummy_exc[19], &cls_dummy_exc[17] },
    { "PendingDeprecationWarning", &cls_dummy_exc[20], &cls_dummy_exc[17] },
    { "RuntimeWarning", &cls_dummy_exc[21], &cls_dummy_exc[17] },
    { "FutureWarning", &cls_dummy_exc[22], &cls_dummy_exc[17] },
    { "SyntaxWarning", &cls_dummy_exc[23], &cls_dummy_exc[17] },
    { "ImportWarning", &cls_dummy_exc[24], &cls_dummy_exc[17] },
    { "UnicodeWarning", &cls_dummy_exc[25], &cls_dummy_exc[17] },
    { "BytesWarning", &cls_dummy_exc[26], &cls_dummy_exc[17] },
    { "ResourceWarning", &cls_dummy_exc[27], &cls_dummy_exc[17] },
    { NULL, NULL, NULL }
};

static Class *mk_type(const char *name, Class *base, BFn ctor)
{
    Class *c = new_class(name, base, CF_BUILTIN);
    c->ctor = ctor;
    return c;
}

static void init_runtime(int argc, char **argv)
{
    const char *sargv0 = argc > 0 ? argv[0] : "";
    int i;
    Module *m;
    List *av;
    init_chars();
    NONE_V.t = T_NONE; NONE_V.u.i = 0;
    TRUE_V.t = T_BOOL; TRUE_V.u.i = 1;
    FALSE_V.t = T_BOOL; FALSE_V.u.i = 0;
    UNSET_V.t = T_UNSET; UNSET_V.u.i = 0;
    cur_exc = NONE_V;
    handled_exc = NONE_V;
    vstack = (Value *)xmalloc(sizeof(Value) * (size_t)VSTACK_SIZE);
    vsp = vstack;
    vlimit = vstack + VSTACK_SIZE - 512;
    gc_perm = 1;
#define SN(v, s) v = intern_c(s)
    SN(S_init, "__init__"); SN(S_str, "__str__"); SN(S_repr, "__repr__"); SN(S_eq, "__eq__");
    SN(S_ne, "__ne__"); SN(S_lt, "__lt__"); SN(S_le, "__le__"); SN(S_gt, "__gt__");
    SN(S_ge, "__ge__"); SN(S_add, "__add__"); SN(S_sub, "__sub__"); SN(S_mul, "__mul__");
    SN(S_neg, "__neg__"); SN(S_len, "__len__"); SN(S_getitem, "__getitem__");
    SN(S_setitem, "__setitem__"); SN(S_delitem, "__delitem__"); SN(S_contains, "__contains__");
    SN(S_iter, "__iter__"); SN(S_next, "__next__"); SN(S_call, "__call__");
    SN(S_bool, "__bool__"); SN(S_hash, "__hash__"); SN(S_enter, "__enter__");
    SN(S_exit, "__exit__"); SN(S_name, "__name__"); SN(S_class, "__class__");
    SN(S_args, "args"); SN(S_truediv, "__truediv__"); SN(S_floordiv, "__floordiv__");
    SN(S_mod, "__mod__"); SN(S_pow, "__pow__"); SN(S_radd, "__radd__"); SN(S_rsub, "__rsub__");
    SN(S_rmul, "__rmul__"); SN(S_abs, "__abs__"); SN(S_int, "__int__"); SN(S_float, "__float__");
    SN(S_index, "__index__"); SN(S_pos, "__pos__"); SN(S_invert, "__invert__");
    SN(S_and, "__and__"); SN(S_or, "__or__"); SN(S_xor, "__xor__"); SN(S_lshift, "__lshift__");
    SN(S_rshift, "__rshift__"); SN(S_dict, "__dict__"); SN(S_mro, "__mro__");
    SN(S_doc, "__doc__"); SN(S_setter, "setter"); SN(S_main, "__main__");
    SN(S_getattr, "__getattr__"); SN(S_format, "__format__"); SN(S_self, "self");
    SN(S_super, "super"); SN(S_setattr, "__setattr__"); SN(S_new, "__new__");
    SN(S_module, "__module__"); SN(S_reversed, "__reversed__");
    SN(S_tbraw, "__tbraw__"); SN(S_traceback, "__traceback__");
    SN(S_cause, "__cause__"); SN(S_context, "__context__"); SN(S_suppress, "__suppress_context__");
    SN(S_init_subclass, "__init_subclass__"); SN(S_class_getitem, "__class_getitem__");
    SN(S_metaclass, "metaclass");
#undef SN
    /* classes */
    cls_object = new_class("object", NULL, CF_BUILTIN);
    cls_type = mk_type("type", cls_object, bi_type);
    cls_int = mk_type("int", cls_object, bi_int);
    cls_bool = mk_type("bool", cls_int, bi_bool);
    cls_float = mk_type("float", cls_object, bi_float);
    cls_str = mk_type("str", cls_object, bi_str);
    cls_list = mk_type("list", cls_object, bi_list);
    cls_tuple = mk_type("tuple", cls_object, bi_tuple);
    cls_dict = mk_type("dict", cls_object, bi_dict);
    cls_set = mk_type("set", cls_object, bi_set);
    cls_frozenset = mk_type("frozenset", cls_object, bi_frozenset);
    cls_range = mk_type("range", cls_object, bi_range);
    cls_none = mk_type("NoneType", cls_object, NULL);
    cls_func = mk_type("function", cls_object, NULL);
    cls_builtin = mk_type("builtin_function_or_method", cls_object, NULL);
    cls_method = mk_type("method", cls_object, NULL);
    cls_gen = mk_type("generator", cls_object, NULL);
    cls_module = mk_type("module", cls_object, NULL);
    cls_iter = mk_type("iterator", cls_object, NULL);
    cls_prop = mk_type("property", cls_object, bi_property);
    cls_file = mk_type("TextIOWrapper", cls_object, NULL);
    cls_fileb = mk_type("BufferedReader", cls_object, NULL);
    cls_filew = mk_type("BufferedWriter", cls_object, NULL);
    cls_cell = mk_type("cell", cls_object, NULL);
    cls_smeth = mk_type("staticmethod", cls_object, bi_staticmethod);
    cls_cmeth = mk_type("classmethod", cls_object, bi_classmethod);
    cls_super = mk_type("super", cls_object, bi_super);
    complex_init();
    cls_object->ctor = bi_object;
    for (i = 0; exc_table[i].name; i++) {
        Class *base = exc_table[i].base ? *exc_table[i].base : cls_object;
        *exc_table[i].var = new_class(exc_table[i].name, base, CF_BUILTIN | CF_EXC);
    }
    cls_BaseException->flags |= CF_EXC;
    eg_init_classes();
    set_global(intern_c("IOError"), mkobj(cls_OSError, T_CLASS));
    set_global(intern_c("None"), NONE_V);
    set_global(intern_c("True"), TRUE_V);
    set_global(intern_c("False"), FALSE_V);

    /* builtin functions */
    def_bi(NULL, "print", bi_print); def_bi(NULL, "len", bi_len); def_bi(NULL, "repr", bi_repr);
    def_bi(NULL, "abs", bi_abs); def_bi(NULL, "min", bi_min); def_bi(NULL, "max", bi_max);
    def_bi(NULL, "sum", bi_sum); def_bi(NULL, "sorted", bi_sorted);
    def_bi(NULL, "reversed", bi_reversed); def_bi(NULL, "enumerate", bi_enumerate);
    def_bi(NULL, "zip", bi_zip); def_bi(NULL, "map", bi_map); def_bi(NULL, "filter", bi_filter);
    def_bi(NULL, "any", bi_any); def_bi(NULL, "all", bi_all); def_bi(NULL, "round", bi_round);
    def_bi(NULL, "divmod", bi_divmod); def_bi(NULL, "pow", bi_pow); def_bi(NULL, "chr", bi_chr);
    def_bi(NULL, "ord", bi_ord); def_bi(NULL, "hex", bi_hex); def_bi(NULL, "oct", bi_oct);
    def_bi(NULL, "bin", bi_bin); def_bi(NULL, "isinstance", bi_isinstance);
    def_bi(NULL, "issubclass", bi_issubclass); def_bi(NULL, "hasattr", bi_hasattr);
    def_bi(NULL, "getattr", bi_getattr); def_bi(NULL, "setattr", bi_setattr);
    def_bi(NULL, "delattr", bi_delattr); def_bi(NULL, "callable", bi_callable);
    def_bi(NULL, "iter", bi_iter); def_bi(NULL, "next", bi_next); def_bi(NULL, "id", bi_id);
    def_bi(NULL, "hash", bi_hash); def_bi(NULL, "input", bi_input); def_bi(NULL, "open", bi_open);
    def_bi(NULL, "format", bi_format);
    def_bi(NULL, "exit", bi_exit_fn); def_bi(NULL, "quit", bi_exit_fn);
    propsetter_fn = mkbuiltin("setter", bi_propsetter);
    propdeleter_fn = mkbuiltin("deleter", bi_propdeleter);
    propgetter_fn = mkbuiltin("getter", bi_propgetter);
    set_global(S_super, mkbuiltin("super", bi_super));

    /* exception methods */
    def_bi(cls_BaseException, "__init__", exc_init);
    def_bi(cls_BaseException, "__str__", exc_str);
    def_bi(cls_BaseException, "__repr__", exc_repr);
    object_init_v = mkbuiltin("__init__", obj_init);
    amap_set(&cls_object->ns, S_init, object_init_v);
    obj_eq_v = mkbuiltin("__eq__", obj_eq);
    obj_ne_v = mkbuiltin("__ne__", obj_ne);
    obj_hash_v = mkbuiltin("__hash__", obj_hash);
    def_bi(cls_object, "__repr__", obj_repr);
    def_bi(cls_object, "__str__", obj_str);
    def_bi(cls_object, "__new__", nat_new);
    def_bi(cls_object, "__setattr__", obj_setattr);
    obj_str_v = *amap_get(&cls_object->ns, S_str);
    amap_set(&cls_type->ns, S_new, mkbuiltin("__new__", type_new));
    def_bi(cls_type, "__call__", type_call); def_bi(cls_type, "__init__", type_init);
    amap_set(&cls_object->ns, S_init_subclass,
             mk_prop(T_CMETH, mkbuiltin("__init_subclass__", obj_init_subclass), UNSET_V));

    /* str */
    def_bi(cls_str, "upper", s_upper); def_bi(cls_str, "lower", s_lower);
    def_bi(cls_str, "capitalize", s_capitalize); def_bi(cls_str, "title", s_title);
    def_bi(cls_str, "swapcase", s_swapcase);
    def_bi(cls_str, "translate", s_translate); def_bi(cls_str, "maketrans", s_maketrans);
    def_bi(NULL, "ascii", bi_ascii); def_bi(cls_str, "strip", s_strip);
    def_bi(cls_str, "lstrip", s_lstrip); def_bi(cls_str, "rstrip", s_rstrip);
    def_bi(cls_str, "split", s_split); def_bi(cls_str, "rsplit", s_rsplit);
    def_bi(cls_str, "splitlines", s_splitlines); def_bi(cls_str, "join", s_join);
    def_bi(cls_str, "replace", s_replace); def_bi(cls_str, "find", s_find);
    def_bi(cls_str, "rfind", s_rfind); def_bi(cls_str, "index", s_index);
    def_bi(cls_str, "rindex", s_rindex); def_bi(cls_str, "count", s_count);
    def_bi(cls_str, "startswith", s_startswith); def_bi(cls_str, "endswith", s_endswith);
    def_bi(cls_str, "isalpha", s_isalpha); def_bi(cls_str, "isdigit", s_isdigit);
    def_bi(cls_str, "isnumeric", s_isdigit); def_bi(cls_str, "isdecimal", s_isdigit);
    def_bi(cls_str, "isalnum", s_isalnum); def_bi(cls_str, "isspace", s_isspace);
    def_bi(cls_str, "isupper", s_isupper); def_bi(cls_str, "islower", s_islower);
    def_bi(cls_str, "center", s_center); def_bi(cls_str, "ljust", s_ljust);
    def_bi(cls_str, "rjust", s_rjust); def_bi(cls_str, "zfill", s_zfill);
    def_bi(cls_str, "partition", s_partition); def_bi(cls_str, "rpartition", s_rpartition);
    def_bi(cls_str, "format", s_format); def_bi(cls_str, "encode", s_encode);
    def_bi(cls_str, "removeprefix", s_removeprefix);
    def_bi(cls_str, "removesuffix", s_removesuffix);
    def_bi(cls_str, "expandtabs", s_expandtabs); def_bi(cls_str, "casefold", s_lower);
    def_bi(cls_str, "isidentifier", s_isidentifier); def_bi(cls_str, "istitle", s_istitle);
    def_bi(cls_str, "isprintable", s_isprintable); def_bi(cls_str, "isascii", s_isascii);
    /* protocol methods of the native base types (also used by subclasses) */
    {
        Class *nc[6];
        int k;
        nc[0] = cls_str; nc[1] = cls_list; nc[2] = cls_tuple; nc[3] = cls_dict; nc[4] = cls_set;
        nc[5] = cls_frozenset;
        for (k = 0; k < 6; k++) {
            def_bi(nc[k], "__len__", nat_len); def_bi(nc[k], "__iter__", nat_iter);
            def_bi(nc[k], "__contains__", nat_contains); def_bi(nc[k], "__repr__", nat_repr);
            def_bi(nc[k], "__eq__", nat_eq); def_bi(nc[k], "__ne__", nat_ne);
            if (nc[k] != cls_set && nc[k] != cls_frozenset && nc[k] != cls_dict) {
                def_bi(nc[k], "__getitem__", nat_getitem);
                def_bi(nc[k], "__add__", nat_add); def_bi(nc[k], "__mul__", nat_mul);
                def_bi(nc[k], "__rmul__", nat_mul);
            }
            if (nc[k] != cls_dict) {
                def_bi(nc[k], "__lt__", nat_lt); def_bi(nc[k], "__le__", nat_le);
                def_bi(nc[k], "__gt__", nat_gt); def_bi(nc[k], "__ge__", nat_ge);
            }
            if (nc[k] == cls_str || nc[k] == cls_tuple || nc[k] == cls_frozenset)
                def_bi(nc[k], "__hash__", nat_hash);
        }
        def_bi(cls_str, "__str__", nat_str); def_bi(cls_str, "__mod__", nat_mod);
        def_bi(cls_str, "__format__", nat_format);
        def_bi(cls_list, "__setitem__", nat_setitem); def_bi(cls_list, "__delitem__", nat_delitem);
        def_bi(cls_dict, "__getitem__", nat_getitem);
        def_bi(cls_dict, "__setitem__", nat_setitem); def_bi(cls_dict, "__delitem__", nat_delitem);
        def_bi(cls_dict, "__or__", nat_or);
        for (k = 4; k < 6; k++) {
            def_bi(nc[k], "__or__", nat_or); def_bi(nc[k], "__and__", nat_and);
            def_bi(nc[k], "__sub__", nat_sub); def_bi(nc[k], "__xor__", nat_xor);
            amap_set(&nc[k]->ns, S_repr, mkbuiltin("__repr__", nat_set_repr));
        }
        def_bi(cls_dict, "__init__", d_update);
        def_bi(cls_list, "__init__", list_init);
        def_bi(cls_set, "__init__", set_init);
        amap_set(&cls_set->ns, intern_c("__ior__"), mkbuiltin("__ior__", nat_ior));
        amap_set(&cls_set->ns, intern_c("__iand__"), mkbuiltin("__iand__", nat_iand));
        amap_set(&cls_set->ns, intern_c("__isub__"), mkbuiltin("__isub__", nat_isub));
        amap_set(&cls_set->ns, intern_c("__ixor__"), mkbuiltin("__ixor__", nat_ixor));
    }
    /* list, tuple */
    def_bi(cls_list, "append", l_append); def_bi(cls_list, "extend", l_extend);
    def_bi(cls_list, "insert", l_insert); def_bi(cls_list, "pop", l_pop);
    def_bi(cls_list, "remove", l_remove); def_bi(cls_list, "index", l_index);
    def_bi(cls_list, "count", l_count); def_bi(cls_list, "sort", l_sort);
    def_bi(cls_list, "reverse", l_reverse); def_bi(cls_list, "copy", l_copy);
    def_bi(cls_list, "clear", l_clear);
    def_bi(cls_tuple, "index", t_index); def_bi(cls_tuple, "count", l_count);
    /* dict */
    def_bi(cls_dict, "get", d_get); def_bi(cls_dict, "keys", d_keys);
    def_bi(cls_dict, "values", d_values); def_bi(cls_dict, "items", d_items);
    def_bi(cls_dict, "update", d_update); def_bi(cls_dict, "pop", d_pop);
    def_bi(cls_dict, "setdefault", d_setdefault); def_bi(cls_dict, "copy", d_copy);
    def_bi(cls_dict, "clear", d_clear); def_bi(cls_dict, "popitem", d_popitem);
    def_bi(cls_dict, "fromkeys", d_fromkeys);
    /* set */
    def_bi(cls_set, "add", st_add); def_bi(cls_set, "discard", st_discard);
    def_bi(cls_set, "remove", st_remove); def_bi(cls_set, "pop", st_pop);
    def_bi(cls_set, "union", st_union); def_bi(cls_set, "intersection", st_intersection);
    def_bi(cls_set, "difference", st_difference);
    def_bi(cls_set, "symmetric_difference", st_symdiff);
    def_bi(cls_set, "update", st_update); def_bi(cls_set, "issubset", st_issubset);
    def_bi(cls_set, "issuperset", st_issuperset); def_bi(cls_set, "isdisjoint", st_isdisjoint);
    def_bi(cls_set, "copy", st_copy); def_bi(cls_set, "clear", st_clear);
    def_bi(cls_set, "intersection_update", st_intersection_update);
    def_bi(cls_set, "difference_update", st_difference_update);
    def_bi(cls_set, "symmetric_difference_update", st_symdiff_update);
    def_bi(cls_frozenset, "union", st_union);
    def_bi(cls_frozenset, "intersection", st_intersection);
    def_bi(cls_frozenset, "difference", st_difference);
    def_bi(cls_frozenset, "symmetric_difference", st_symdiff);
    def_bi(cls_frozenset, "issubset", st_issubset);
    def_bi(cls_frozenset, "issuperset", st_issuperset);
    def_bi(cls_frozenset, "isdisjoint", st_isdisjoint);
    def_bi(cls_frozenset, "copy", st_copy);
    {
        Class *nk[3];
        int k;
        nk[0] = cls_int; nk[1] = cls_float; nk[2] = cls_complex;
        for (k = 0; k < 3; k++) {
            def_bi(nk[k], "__add__", num_add); def_bi(nk[k], "__radd__", num_radd);
            def_bi(nk[k], "__sub__", num_sub); def_bi(nk[k], "__rsub__", num_rsub);
            def_bi(nk[k], "__mul__", num_mul); def_bi(nk[k], "__rmul__", num_rmul);
            def_bi(nk[k], "__truediv__", num_truediv); def_bi(nk[k], "__rtruediv__", num_rtruediv);
            def_bi(nk[k], "__floordiv__", num_floordiv);
            def_bi(nk[k], "__rfloordiv__", num_rfloordiv);
            def_bi(nk[k], "__mod__", num_mod); def_bi(nk[k], "__rmod__", num_rmod);
            def_bi(nk[k], "__pow__", num_pow); def_bi(nk[k], "__rpow__", num_rpow);
            def_bi(nk[k], "__neg__", num_neg); def_bi(nk[k], "__pos__", num_pos);
            def_bi(nk[k], "__abs__", num_abs); def_bi(nk[k], "conjugate", num_conj);
            def_bi(nk[k], "__eq__", nat_eq); def_bi(nk[k], "__ne__", nat_ne);
            def_bi(nk[k], "__lt__", nat_lt); def_bi(nk[k], "__le__", nat_le);
            def_bi(nk[k], "__gt__", nat_gt); def_bi(nk[k], "__ge__", nat_ge);
            def_bi(nk[k], "__repr__", nat_repr);
            def_bi(nk[k], "__hash__", nat_hash); def_bi(nk[k], "__format__", nat_format);
        }
        def_bi(cls_int, "__and__", num_and); def_bi(cls_int, "__rand__", num_rand);
        def_bi(cls_int, "__or__", num_or); def_bi(cls_int, "__ror__", num_ror);
        def_bi(cls_int, "__xor__", num_xor); def_bi(cls_int, "__rxor__", num_rxor);
        def_bi(cls_int, "__lshift__", num_lshift); def_bi(cls_int, "__rlshift__", num_rlshift);
        def_bi(cls_int, "__rshift__", num_rshift); def_bi(cls_int, "__rrshift__", num_rrshift);
        def_bi(cls_int, "__invert__", num_invert);
    }
    complex_methods();
    /* numbers */
    def_bi(cls_int, "bit_length", i_bit_length); def_bi(cls_int, "__index__", i_index);
    def_bi(cls_float, "is_integer", f_is_integer);
    def_bi(cls_float, "as_integer_ratio", f_as_integer_ratio);
    def_bi(cls_float, "hex", f_hex); def_bi(cls_float, "fromhex", f_fromhex);
    def_bi(cls_int, "bit_count", i_bit_count);
    def_bi(cls_int, "as_integer_ratio", i_as_integer_ratio);
    def_bi(cls_int, "is_integer", i_is_integer);
    def_bi(cls_range, "count", rg_count); def_bi(cls_range, "index", rg_index);
    /* generators / iterators */
    def_bi(cls_gen, "__next__", g_next); def_bi(cls_gen, "__iter__", g_iter);
    def_bi(cls_gen, "close", g_close); def_bi(cls_gen, "send", g_send);
    def_bi(cls_gen, "throw", g_throw);
    def_bi(cls_iter, "__next__", g_next); def_bi(cls_iter, "__iter__", g_iter);
    /* files */
    def_bi(cls_file, "read", fl_read); def_bi(cls_file, "readline", fl_readline);
    def_bi(cls_file, "readlines", fl_readlines); def_bi(cls_file, "write", fl_write);
    def_bi(cls_file, "writelines", fl_writelines); def_bi(cls_file, "flush", fl_flush);
    def_bi(cls_file, "close", fl_close); def_bi(cls_file, "__enter__", fl_enter);
    def_bi(cls_file, "__exit__", fl_exit);
    {
        Class *fk[3];
        int k;
        fk[0] = cls_file; fk[1] = cls_fileb; fk[2] = cls_filew;
        for (k = 0; k < 3; k++) {
            if (k) {
                def_bi(fk[k], "read", fl_read); def_bi(fk[k], "readline", fl_readline);
                def_bi(fk[k], "readlines", fl_readlines); def_bi(fk[k], "write", fl_write);
                def_bi(fk[k], "writelines", fl_writelines); def_bi(fk[k], "flush", fl_flush);
                def_bi(fk[k], "close", fl_close); def_bi(fk[k], "__enter__", fl_enter);
                def_bi(fk[k], "__exit__", fl_exit);
            }
            def_bi(fk[k], "seek", fl_seek); def_bi(fk[k], "tell", fl_tell);
            def_bi(fk[k], "readable", fl_readable); def_bi(fk[k], "writable", fl_writable);
            def_bi(fk[k], "seekable", fl_seekable); def_bi(fk[k], "isatty", fl_isatty);
            def_bi(fk[k], "fileno", fl_fileno); def_bi(fk[k], "readinto", fl_readinto);
        }
    }

    bytes_init();
    set_global(intern_c("bytearray"), mkobj(cls_bytearray, T_CLASS));
    {
        Class *bk[2];
        int k;
        bk[0] = cls_bytes; bk[1] = cls_bytearray;
        for (k = 0; k < 2; k++) {
            def_bi(bk[k], "__len__", nat_len); def_bi(bk[k], "__iter__", nat_iter);
            def_bi(bk[k], "__contains__", nat_contains); def_bi(bk[k], "__repr__", nat_repr);
            def_bi(bk[k], "__eq__", nat_eq); def_bi(bk[k], "__ne__", nat_ne);
            def_bi(bk[k], "__lt__", nat_lt); def_bi(bk[k], "__le__", nat_le);
            def_bi(bk[k], "__gt__", nat_gt); def_bi(bk[k], "__ge__", nat_ge);
            def_bi(bk[k], "__getitem__", nat_getitem); def_bi(bk[k], "__add__", nat_add);
            def_bi(bk[k], "__mul__", nat_mul); def_bi(bk[k], "__rmul__", nat_mul);
        }
        def_bi(cls_bytes, "__hash__", nat_hash);
        amap_set(&cls_bytearray->ns, S_repr, mkbuiltin("__repr__", nat_set_repr));
        def_bi(cls_bytearray, "__setitem__", nat_setitem);
        def_bi(cls_bytearray, "__delitem__", nat_delitem);
    }
    /* misc types */
    cls_slice = mk_type("slice", cls_object, bi_slice);
    def_bi(cls_slice, "indices", sl_indices);
    cls_notimpl = mk_type("NotImplementedType", cls_object, NULL);
    NotImpl_V = mkobj(new_inst(cls_notimpl), T_INST);
    set_global(intern_c("NotImplemented"), NotImpl_V);
    {
        Class *ce = mk_type("ellipsis", cls_object, NULL);
        Ellipsis_V = mkobj(new_inst(ce), T_INST);
        set_global(intern_c("Ellipsis"), Ellipsis_V);
        def_bi(ce, "__repr__", ellipsis_repr);
    }
    cls_dview[0] = mk_type("dict_keys", cls_object, NULL);
    cls_dview[1] = mk_type("dict_values", cls_object, NULL);
    cls_dview[2] = mk_type("dict_items", cls_object, NULL);
    def_bi(cls_notimpl, "__repr__", notimpl_repr);
    def_bi(NULL, "globals", bi_globals); def_bi(NULL, "locals", bi_locals);
    def_bi(NULL, "vars", bi_vars2); def_bi(NULL, "dir", bi_dir);
    def_bi(NULL, "exec", bi_exec); def_bi(NULL, "eval", bi_eval);
    def_bi(NULL, "__import__", bi_import);

    /* modules */
    sys_modules_v = mkobj(dict_new(), T_DICT);
    m = new_module("math");
    mod_def(m, "sqrt", m_sqrt); mod_def(m, "sin", m_sin); mod_def(m, "cos", m_cos);
    mod_def(m, "tan", m_tan); mod_def(m, "asin", m_asin); mod_def(m, "acos", m_acos);
    mod_def(m, "atan", m_atan); mod_def(m, "atan2", m_atan2); mod_def(m, "sinh", m_sinh);
    mod_def(m, "cosh", m_cosh); mod_def(m, "tanh", m_tanh); mod_def(m, "exp", m_exp);
    mod_def(m, "log", m_log); mod_def(m, "log10", m_log10); mod_def(m, "log2", m_log2);
    mod_def(m, "pow", m_pow); mod_def(m, "fabs", m_fabs); mod_def(m, "hypot", m_hypot);
    mod_def(m, "floor", m_floor); mod_def(m, "ceil", m_ceil); mod_def(m, "trunc", m_trunc);
    mod_def(m, "gcd", m_gcd); mod_def(m, "lcm", m_lcm); mod_def(m, "factorial", m_factorial);
    mod_def(m, "isqrt", m_isqrt); mod_def(m, "isnan", m_isnan); mod_def(m, "isinf", m_isinf);
    mod_def(m, "isfinite", m_isfinite); mod_def(m, "fmod", m_fmod);
    mod_def(m, "copysign", m_copysign); mod_def(m, "nextafter", m_nextafter);
    mod_def(m, "ulp", m_ulp); mod_def(m, "degrees", m_degrees);
    mod_def(m, "radians", m_radians); mod_def(m, "comb", m_comb); mod_def(m, "prod", m_prod);
    mod_def(m, "isclose", m_isclose); mod_def(m, "fsum", m_fsum); mod_def(m, "log1p", m_log1p);
    mod_def(m, "expm1", m_expm1); mod_def(m, "erf", m_erf); mod_def(m, "erfc", m_erfc);
    mod_def(m, "exp2", m_exp2); mod_def(m, "gamma", m_gamma); mod_def(m, "lgamma", m_lgamma);
    mod_def(m, "asinh", m_asinh); mod_def(m, "acosh", m_acosh); mod_def(m, "atanh", m_atanh);
    mod_def(m, "cbrt", m_cbrt); mod_def(m, "remainder", m_remainder); mod_def(m, "modf", m_modf);
    mod_def(m, "frexp", m_frexp); mod_def(m, "ldexp", m_ldexp); mod_def(m, "perm", m_perm);
    mod_def(m, "dist", m_dist);
    mod_val(m, "pi", mkfloat(3.14159265358979323846));
    mod_val(m, "e", mkfloat(2.71828182845904523536));
    mod_val(m, "tau", mkfloat(6.28318530717958647692));
    mod_val(m, "inf", mkfloat(HUGE_VAL));
    mod_val(m, "nan", mkfloat(HUGE_VAL - HUGE_VAL));
    np_init();
    pyrand_init();
    re_init();
    json_init();
    struct_init();
    cmath_init();
    sock_init();
    introspect_init();
    m = new_module("time");
    mod_def(m, "time", t_time); mod_def(m, "perf_counter", t_clock);
    mod_def(m, "monotonic", t_clock); mod_def(m, "process_time", t_clock);
    mod_def(m, "sleep", t_sleep);
    mod_def(m, "time_ns", t_time_ns); mod_def(m, "perf_counter_ns", t_clock_ns);
    mod_def(m, "monotonic_ns", t_clock_ns); mod_def(m, "process_time_ns", t_clock_ns);
    mod_def(m, "_gmtime", t_gmtime_raw); mod_def(m, "_localtime", t_localtime_raw);
    mod_def(m, "_mktime", t_mktime_raw); mod_def(m, "_tzinfo", t_tzinfo_raw);
    m = new_module("sys");
    sys_mod = m;
    av = list_new(argc + 1);
    for (i = 0; i < argc; i++) list_push(av, mkcstr(argv[i]));
    mod_val(m, "argv", mklist(av));
    mod_def(m, "exit", bi_exit_fn);
    mod_def(m, "getrecursionlimit", sys_getrec); mod_def(m, "setrecursionlimit", sys_setrec);
    mod_val(m, "stdout", std_file(stdout, 1));
    mod_val(m, "stderr", std_file(stderr, 1));
    mod_val(m, "stdin", std_file(stdin, 1));
    mod_val(m, "maxsize", mkint(SI_MAX));
    mod_val(m, "version", mkcstr("3.12.0 (C89 subset interpreter)"));
    mod_val(m, "platform", mkcstr("c89"));
    mod_def(m, "exc_info", bi_exc_info);
    mod_def(m, "_getframe", sys_getframe);
    mod_val(m, "modules", sys_modules_v);
    init_sys_path(sargv0);
    mod_val(m, "path", sys_path_v);
    {
        Value vi[5];
        vi[0] = mkint(3);
        vi[1] = mkint(12);
        vi[2] = mkint(0);
        vi[3] = mkcstr("final");
        vi[4] = mkint(0);
        mod_val(m, "version_info", mktuple_from(vi, 5));
    }
    {
        unsigned long one = 1UL;
        mod_val(m, "byteorder", mkcstr(*(unsigned char *)&one ? "little" : "big"));
    }
    mod_val(m, "executable", mkcstr(exe_path));
    mod_val(m, "builtin_module_names", mktuple_from(NULL, 0));
    m = new_module("os");
    {
        Module *pm = new_module("os.path");
        mod_def(m, "getenv", os_getenv); mod_def(m, "getcwd", os_getcwd);
        mod_def(m, "remove", os_remove); mod_def(m, "unlink", os_remove);
        mod_def(m, "rename", os_rename);
        mod_def(m, "urandom", os_urandom);
        mod_def(m, "listdir", os_listdir); mod_def(m, "mkdir", os_mkdir);
        mod_def(m, "rmdir", os_rmdir); mod_def(m, "chdir", os_chdir);
        mod_def(m, "getpid", os_getpid); mod_def(m, "getppid", os_getppid);
        mod_val(m, "sep", mkcstr("/")); mod_val(m, "pathsep", mkcstr(":"));
        mod_val(m, "linesep", mkcstr("\n")); mod_val(m, "name", mkcstr("posix"));
        mod_val(m, "curdir", mkcstr(".")); mod_val(m, "pardir", mkcstr(".."));
        mod_val(m, "devnull", mkcstr("/dev/null"));
        mod_val(m, "environ", init_environ());
        mod_val(m, "path", mkobj(pm, T_MODULE));
        mod_def(pm, "join", osp_join); mod_def(pm, "exists", osp_exists);
        mod_def(pm, "basename", osp_basename); mod_def(pm, "dirname", osp_dirname);
        mod_def(pm, "split", osp_split); mod_def(pm, "splitext", osp_splitext);
        mod_def(pm, "normcase", osp_normcase);
        mod_def(pm, "isabs", osp_isabs); mod_def(pm, "normpath", osp_normpath);
        mod_def(pm, "abspath", osp_abspath); mod_def(pm, "expanduser", osp_expanduser);
        mod_def(pm, "isdir", osp_isdir); mod_def(pm, "isfile", osp_isfile);
        mod_def(pm, "getsize", osp_getsize);
        mod_val(pm, "sep", mkcstr("/")); mod_val(pm, "curdir", mkcstr("."));
        mod_val(pm, "pardir", mkcstr("..")); mod_val(pm, "pathsep", mkcstr(":"));
    }
    {
        Module *mm = module_new_py(S_main);
        Value mv = mkobj(mm, T_MODULE);
        main_gp = mm->gp;
        dict_set(AS_DICT(sys_modules_v), mkobj(S_main, T_STR), mv);
        mod_gset(mm, "__name__", mkobj(S_main, T_STR));
        mod_gset(mm, "__doc__", NONE_V);
        mod_gset(mm, "__package__", NONE_V);
    }
    gc_perm = 0;
}
