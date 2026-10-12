/* =========================================================================
   p7_cplx.c   complex numbers: portable math helpers, arithmetic, complex(),
               the cmath module.  The algorithms follow CPython's
               Objects/complexobject.c and Modules/cmathmodule.c so that results
               (and the last digit) agree for finite arguments.  Infinities and
               NaNs are not given the special-value treatment of C99 Annex G.
   ========================================================================= */
static double dbl_arg(Args *x, int i);
#define CX_LN2 0.69314718055994530942
#define CX_LN10 2.30258509299404568402
#define CX_E 2.71828182845904523536
#define CX_PI 3.14159265358979323846

static double d_nan(void)
{
    return HUGE_VAL - HUGE_VAL;
}
static int d_isinf(double d)
{
    return d > DBL_MAX || d < -DBL_MAX;
}
static int d_isnan(double d)
{
    return d != d;
}
static int d_isfinite(double d)
{
    return d == d && d <= DBL_MAX && d >= -DBL_MAX;
}
static double d_copysign(double a, double b)
{
    a = fabs(a);
    return signbit_d(b) ? -a : a;
}

/* ---- portable C89 replacements for hypot / log1p / expm1 / asinh / ... ---- */
/* x + y exactly as hi + lo */
static void two_sum(double x, double y, double *hi, double *lo)
{
    double s = x + y, bb = s - x;
    *hi = s;
    *lo = (x - (s - bb)) + (y - bb);
}
/* x * y exactly as hi + lo (Veltkamp / Dekker splitting; needs |x|, |y| < 2^996) */
static void two_prod(double x, double y, double *hi, double *lo)
{
    double c, xh, xl, yh, yl, p;
    c = 134217729.0 * x; xh = c - (c - x); xl = x - xh;
    c = 134217729.0 * y; yh = c - (c - y); yl = y - yh;
    p = x * y;
    *hi = p;
    *lo = ((xh * yh - p) + xh * yl + xl * yh) + xl * yl;
}
/* hypot with an error below one ulp (usually correctly rounded) */
static double n_hypot(double a, double b)
{
    int ea;
    double t, p, pe, q, qe, s, se, h, hh, he, res;
    a = fabs(a); b = fabs(b);
    if (d_isinf(a) || d_isinf(b)) return HUGE_VAL;
    if (a != a || b != b) return d_nan();
    if (a < b) { t = a; a = b; b = t; }
    if (b == 0.0) return a;
    frexp(a, &ea);
    a = ldexp(a, -ea);
    b = ldexp(b, -ea);
    two_prod(a, a, &p, &pe);
    two_prod(b, b, &q, &qe);
    two_sum(p, q, &s, &se);
    h = sqrt(s);
    two_prod(h, h, &hh, &he);
    res = ((s - hh) - he) + (pe + qe + se);
    h = h + res / (2.0 * h);
    return ldexp(h, ea);
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

/* ---- complex arithmetic ------------------------------------------------- */
typedef struct Cx { double r, i; } Cx;

static Cx cx_mk(double r, double i)
{
    Cx z;
    z.r = r; z.i = i;
    return z;
}
static Cx cx_add(Cx a, Cx b) { return cx_mk(a.r + b.r, a.i + b.i); }
static Cx cx_sub(Cx a, Cx b) { return cx_mk(a.r - b.r, a.i - b.i); }
static Cx cx_neg(Cx a) { return cx_mk(-a.r, -a.i); }
static Cx cx_mul(Cx a, Cx b) { return cx_mk(a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r); }
/* a / b; *err is set for a zero divisor */
static Cx cx_quot(Cx a, Cx b, int *err)
{
    Cx r;
    double abr = fabs(b.r), abi = fabs(b.i);
    if (abr >= abi) {
        if (abr == 0.0) { *err = 1; return cx_mk(0.0, 0.0); }
        {
            double ratio = b.i / b.r, denom = b.r + b.i * ratio;
            r.r = (a.r + a.i * ratio) / denom;
            r.i = (a.i - a.r * ratio) / denom;
        }
    } else if (abi >= abr) {
        double ratio = b.r / b.i, denom = b.r * ratio + b.i;
        r.r = (a.r * ratio + a.i) / denom;
        r.i = (a.i * ratio - a.r) / denom;
    } else {
        r.r = r.i = d_nan();
    }
    return r;
}
static Cx cx_powu(Cx x, long n)
{
    Cx r = cx_mk(1.0, 0.0), p = x;
    long mask = 1;
    while (mask > 0 && n >= mask) {
        if (n & mask) r = cx_mul(r, p);
        mask <<= 1;
        p = cx_mul(p, p);
    }
    return r;
}
static Cx cx_powi(Cx x, long n, int *err)
{
    if (n > 0) return cx_powu(x, n);
    return cx_quot(cx_mk(1.0, 0.0), cx_powu(x, -n), err);
}
/* *err: 1 = ZeroDivisionError ("0.0 to a negative or complex power") */
static Cx cx_pow(Cx a, Cx b, int *err)
{
    double vabs, len, at, phase;
    if (b.r == 0.0 && b.i == 0.0) return cx_mk(1.0, 0.0);
    if (a.r == 0.0 && a.i == 0.0) {
        if (b.i != 0.0 || b.r < 0.0) *err = 1;
        return cx_mk(0.0, 0.0);
    }
    if (b.i == 0.0 && b.r == floor(b.r) && fabs(b.r) <= 100.0) return cx_powi(a, (long)b.r, err);
    vabs = n_hypot(a.r, a.i);
    len = pow(vabs, b.r);
    at = atan2(a.i, a.r);
    phase = at * b.r;
    if (b.i != 0.0) {
        len /= exp(at * b.i);
        phase += b.i * log(vabs);
    }
    return cx_mk(len * cos(phase), len * sin(phase));
}
static Cx cx_sqrt(Cx z)
{
    double ax, ay, s, d;
    Cx r;
    if (z.r == 0.0 && z.i == 0.0) return cx_mk(0.0, z.i);
    ax = fabs(z.r); ay = fabs(z.i);
    if (ax < DBL_MIN && ay < DBL_MIN) {
        ax = ldexp(ax, 53);
        s = ldexp(sqrt(ax + n_hypot(ax, ldexp(ay, 53))), -27);
    } else {
        ax /= 8.0;
        s = 2.0 * sqrt(ax + n_hypot(ax, ay / 8.0));
    }
    d = ay / (2.0 * s);
    if (z.r >= 0.0) { r.r = s; r.i = d_copysign(d, z.i); }
    else { r.r = d; r.i = d_copysign(s, z.i); }
    return r;
}
static Cx cx_exp(Cx z, int *err)
{
    Cx r;
    double l;
    if (z.r > log(DBL_MAX / 4.0)) {
        l = exp(z.r - 1.0);
        r.r = l * cos(z.i) * CX_E;
        r.i = l * sin(z.i) * CX_E;
    } else {
        l = exp(z.r);
        r.r = l * cos(z.i);
        r.i = l * sin(z.i);
    }
    if (d_isinf(r.r) || d_isinf(r.i)) if (d_isfinite(z.r) && d_isfinite(z.i)) *err = 2;
    return r;
}
static Cx cx_log(Cx z, int *err)
{
    Cx r;
    double ax = fabs(z.r), ay = fabs(z.i), h;
    if (ax > DBL_MAX / 4.0 || ay > DBL_MAX / 4.0) {
        r.r = log(n_hypot(ax / 2.0, ay / 2.0)) + CX_LN2;
    } else if (ax < DBL_MIN && ay < DBL_MIN) {
        if (ax > 0.0 || ay > 0.0) {
            r.r = log(n_hypot(ldexp(ax, DBL_MANT_DIG), ldexp(ay, DBL_MANT_DIG))) -
                DBL_MANT_DIG * CX_LN2;
        } else {
            *err = 1;
            return cx_mk(-HUGE_VAL, atan2(z.i, z.r));
        }
    } else {
        h = n_hypot(ax, ay);
        if (0.71 <= h && h <= 1.73) {
            double am = ax > ay ? ax : ay, an = ax > ay ? ay : ax;
            r.r = n_log1p((am - 1.0) * (am + 1.0) + an * an) / 2.0;
        } else r.r = log(h);
    }
    r.i = atan2(z.i, z.r);
    return r;
}
static Cx cx_log10(Cx z, int *err)
{
    Cx r = cx_log(z, err);
    r.r /= CX_LN10;
    r.i /= CX_LN10;
    return r;
}
static Cx cx_sinh(Cx z)
{
    return cx_mk(cos(z.i) * sinh(z.r), sin(z.i) * cosh(z.r));
}
static Cx cx_cosh(Cx z)
{
    return cx_mk(cos(z.i) * cosh(z.r), sin(z.i) * sinh(z.r));
}
static Cx cx_tanh(Cx z)
{
    double tx = tanh(z.r), ty = tan(z.i), cx = 1.0 / cosh(z.r), txty = tx * ty;
    double denom = 1.0 + txty * txty;
    if (fabs(z.r) > log(DBL_MAX / 4.0) / 2.0) {      /* tanh(x) saturates: avoid inf * 0 */
        return cx_mk(d_copysign(1.0, z.r), 4.0 * sin(z.i) * cos(z.i) * exp(-2.0 * fabs(z.r)));
    }
    return cx_mk(tx * (1.0 + ty * ty) / denom, ((ty / denom) * cx) * cx);
}
static Cx cx_sin(Cx z)
{
    Cx s = cx_sinh(cx_mk(-z.i, z.r));
    return cx_mk(s.i, -s.r);
}
static Cx cx_cos(Cx z)
{
    return cx_cosh(cx_mk(-z.i, z.r));
}
static Cx cx_tan(Cx z)
{
    Cx s = cx_tanh(cx_mk(-z.i, z.r));
    return cx_mk(s.i, -s.r);
}
static Cx cx_asin(Cx z)
{
    Cx s1 = cx_sqrt(cx_mk(1.0 - z.r, -z.i)), s2 = cx_sqrt(cx_mk(1.0 + z.r, z.i));
    return cx_mk(atan2(z.r, s1.r * s2.r - s1.i * s2.i), n_asinh(s2.r * s1.i - s2.i * s1.r));
}
static Cx cx_acos(Cx z)
{
    Cx s1 = cx_sqrt(cx_mk(1.0 - z.r, -z.i)), s2 = cx_sqrt(cx_mk(1.0 + z.r, z.i));
    return cx_mk(2.0 * atan2(s1.r, s2.r), n_asinh(s2.r * s1.i - s2.i * s1.r));
}
static Cx cx_asinh(Cx z)
{
    Cx s1 = cx_sqrt(cx_mk(1.0 + z.i, -z.r)), s2 = cx_sqrt(cx_mk(1.0 - z.i, z.r));
    return cx_mk(n_asinh(s1.r * s2.i - s2.r * s1.i), atan2(z.i, s1.r * s2.r - s1.i * s2.i));
}
static Cx cx_acosh(Cx z)
{
    Cx s1 = cx_sqrt(cx_mk(z.r - 1.0, z.i)), s2 = cx_sqrt(cx_mk(z.r + 1.0, z.i));
    return cx_mk(n_asinh(s1.r * s2.r + s1.i * s2.i), 2.0 * atan2(s1.i, s2.r));
}
static Cx cx_atanh(Cx z, int *err)
{
    double ay;
    if (z.r < 0.0) {
        Cx t = cx_atanh(cx_neg(z), err);
        return cx_neg(t);
    }
    ay = fabs(z.i);
    if (z.r == 1.0 && z.i == 0.0) { *err = 1; return cx_mk(HUGE_VAL, 0.0); }
    return cx_mk(n_log1p(4.0 * z.r / ((1.0 - z.r) * (1.0 - z.r) + ay * ay)) / 4.0,
                 -atan2(-2.0 * z.i, (1.0 - z.r) * (1.0 + z.r) - ay * ay) / 2.0);
}
static Cx cx_atan(Cx z, int *err)
{
    Cx s = cx_atanh(cx_mk(-z.i, z.r), err);
    return cx_mk(s.i, -s.r);
}

/* ---- conversions and the binary / unary operators ------------------------ */
static int to_cx(Value v, Cx *z)
{
    if (v.t == T_COMPLEX) { z->r = AS_CPLX(v)->re; z->i = AS_CPLX(v)->im; return 1; }
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET) v = AS_INST(v)->pay;
    if (is_num(v)) { z->r = to_dbl(v); z->i = 0.0; return 1; }
    if (v.t == T_COMPLEX) { z->r = AS_CPLX(v)->re; z->i = AS_CPLX(v)->im; return 1; }
    return 0;
}
static Value cx_value(Cx z)
{
    return mkcomplex(z.r, z.i);
}
static void cx_raise(int err, const char *what)
{
    if (err == 1) throw_new(cls_ValueError, "math domain error");
    if (err == 2) throw_new(cls_OverflowError, "math range error");
    (void)what;
}
static Value cplx_binop(int op, Value a, Value b)
{
    Cx x, y, r;
    int err = 0;
    to_cx(a, &x);
    to_cx(b, &y);
    switch (op) {
    case B_ADD: r = cx_add(x, y); break;
    case B_SUB: r = cx_sub(x, y); break;
    case B_MUL: r = cx_mul(x, y); break;
    case B_DIV:
        r = cx_quot(x, y, &err);
        if (err) throw_new(cls_ZeroDivisionError, "complex division by zero");
        break;
    case B_POW:
        r = cx_pow(x, y, &err);
        if (err) throw_new(cls_ZeroDivisionError, "0.0 to a negative or complex power");
        break;
    default:
        throw_new(cls_TypeError, "unsupported operand type(s) for %s: '%.40s' and '%.40s'",
                  op_str(op), type_name(a), type_name(b));
        return NONE_V;
    }
    return cx_value(r);
}
static Value cplx_unop(int op, Value a)
{
    if (op == '-') return mkcomplex(-AS_CPLX(a)->re, -AS_CPLX(a)->im);
    if (op == '+') return a;
    throw_new(cls_TypeError, "bad operand type for unary ~: 'complex'");
    return NONE_V;
}

/* ---- complex() ------------------------------------------------------------ */
static int ci_prefix(const char *s, long n, long i, const char *w)
{
    long k;
    for (k = 0; w[k]; k++)
        if (i + k >= n || to_low[(unsigned char)s[i + k]] != (unsigned char)w[k]) return 0;
    return 1;
}
/* unsigned floating point number at s[*pi]: digits . exponent, inf, infinity, nan */
static int cx_scan_num(const char *s, long n, long *pi, double *out)
{
    long i = *pi, st = i;
    int nd = 0;
    if (ci_prefix(s, n, i, "infinity")) i += 8;
    else if (ci_prefix(s, n, i, "inf")) i += 3;
    else if (ci_prefix(s, n, i, "nan")) i += 3;
    else {
        while (i < n && (ch_digit[(unsigned char)s[i]] || s[i] == '_')) { i++; nd++; }
        if (i < n && s[i] == '.') {
            i++;
            while (i < n && (ch_digit[(unsigned char)s[i]] || s[i] == '_')) { i++; nd++; }
        }
        if (nd == 0) return 0;
        if (i < n && (s[i] == 'e' || s[i] == 'E')) {
            long sv = i;
            i++;
            if (i < n && (s[i] == '+' || s[i] == '-')) i++;
            if (i < n && ch_digit[(unsigned char)s[i]]) {
                while (i < n && ch_digit[(unsigned char)s[i]]) i++;
            } else i = sv;
        }
    }
    if (!parse_float(s + st, i - st, out)) return 0;
    *pi = i;
    return 1;
}
static int cx_parse(const char *s, long n, Cx *z)
{
    long i = 0, e = n;
    double x, y;
    int neg1 = 0, neg2 = 0;
    while (i < e && ch_space[(unsigned char)s[i]]) i++;
    while (e > i && ch_space[(unsigned char)s[e - 1]]) e--;
    if (i < e && s[i] == '(') {
        if (s[e - 1] != ')') return 0;
        i++; e--;
        while (i < e && ch_space[(unsigned char)s[i]]) i++;
        while (e > i && ch_space[(unsigned char)s[e - 1]]) e--;
    }
    if (i >= e) return 0;
    if (s[i] == '+' || s[i] == '-') { neg1 = s[i] == '-'; i++; }
    if (i < e && (s[i] == 'j' || s[i] == 'J')) {
        if (i + 1 != e) return 0;
        *z = cx_mk(0.0, neg1 ? -1.0 : 1.0);
        return 1;
    }
    if (!cx_scan_num(s, e, &i, &x)) return 0;
    if (neg1) x = -x;
    if (i == e) { *z = cx_mk(x, 0.0); return 1; }
    if (s[i] == 'j' || s[i] == 'J') {
        if (i + 1 != e) return 0;
        *z = cx_mk(0.0, x);
        return 1;
    }
    if (s[i] != '+' && s[i] != '-') return 0;
    neg2 = s[i] == '-';
    i++;
    if (i < e && (s[i] == 'j' || s[i] == 'J')) {
        if (i + 1 != e) return 0;
        y = 1.0;
    } else {
        if (!cx_scan_num(s, e, &i, &y)) return 0;
        if (i >= e || (s[i] != 'j' && s[i] != 'J') || i + 1 != e) return 0;
    }
    *z = cx_mk(x, neg2 ? -y : y);
    return 1;
}
BI(bi_complex)
{
    Value rv = x->n > 0 ? A(0) : kwget(x, "real"), iv = x->n > 1 ? A(1) : kwget(x, "imag");
    Cx r, i, res;
    int rc = 0, ic = 0, given;
    if (x->n > 2) throw_new(cls_TypeError, "complex expected at most 2 arguments, got %d", x->n);
    if (rv.t == T_UNSET) rv = mkint(0);
    given = iv.t != T_UNSET;
    if (rv.t == T_INST && AS_INST(rv)->pay.t != T_UNSET) rv = AS_INST(rv)->pay;
    if (iv.t == T_INST && AS_INST(iv)->pay.t != T_UNSET) iv = AS_INST(iv)->pay;
    if (rv.t == T_STR) {
        if (given) throw_new(cls_TypeError, "complex() can't take second arg if first is a string");
        if (!cx_parse(AS_STR(rv)->s, AS_STR(rv)->len, &res))
            throw_new(cls_ValueError, "complex() arg is a malformed string");
        return cx_value(res);
    }
    if (given && iv.t == T_STR)
        throw_new(cls_TypeError, "complex() second arg can't be a string");
    if (!to_cx(rv, &r)) {
        Value *m = rv.t == T_INST ? class_find(AS_INST(rv)->cls, intern_c("__complex__")) : NULL;
        if (m) {
            Value cv = call1(*m, rv);
            if (cv.t == T_COMPLEX) {
                if (!given) return cv;
                to_cx(cv, &r);
            } else throw_new(cls_TypeError, "__complex__ returned non-complex (type %.40s)",
                type_name(cv));
        } else
            throw_new(cls_TypeError,
                "complex() first argument must be a string or a number, not '%.40s'",
                type_name(rv));
    }
    rc = rv.t == T_COMPLEX;
    if (!given) {
        if (rc) return rv;
        return mkcomplex(r.r, 0.0);
    }
    if (!to_cx(iv, &i))
        throw_new(cls_TypeError, "complex() second argument must be a number, not '%.40s'",
            type_name(iv));
    ic = iv.t == T_COMPLEX;
    res.r = ic ? r.r - i.i : r.r;
    res.i = rc ? i.r + r.i : i.r;
    return cx_value(res);
}
BI(cx_conjugate)
{
    return mkcomplex(AS_CPLX(unpay(A(0)))->re, -AS_CPLX(unpay(A(0)))->im);
}
BI(cx_abs_m)
{
    Value v = unpay(A(0));
    return mkfloat(n_hypot(AS_CPLX(v)->re, AS_CPLX(v)->im));
}
BI(cx_complex_m)
{
    return unpay(A(0));
}
BI(cx_getnewargs)
{
    Value v = unpay(A(0));
    Value t[2];
    t[0] = mkfloat(AS_CPLX(v)->re); t[1] = mkfloat(AS_CPLX(v)->im);
    return mktuple_from(t, 2);
}

/* ---- cmath ----------------------------------------------------------------- */
static Cx cm_arg(Args *x, int k)
{
    Cx z;
    if (k >= x->n) throw_new(cls_TypeError, "%s() takes exactly one argument (0 given)", "cmath");
    if (!to_cx(A(k), &z)) {
        if (A(k).t == T_INST) {
            Value *m = class_find(AS_INST(A(k))->cls, intern_c("__complex__"));
            if (m) {
                Value cv = call1(*m, A(k));
                if (cv.t == T_COMPLEX && to_cx(cv, &z)) return z;
            }
            m = class_find(AS_INST(A(k))->cls, S_float);
            if (m) {
                Value fv = call1(*m, A(k));
                if (fv.t == T_FLOAT) return cx_mk(fv.u.d, 0.0);
            }
        }
        throw_new(cls_TypeError, "must be real number, not %.40s", type_name(A(k)));
    }
    return z;
}
#define CM1(name, expr) BI(name) { int err = 0; Cx z = cm_arg(x, 0), r; r = expr; \
    cx_raise(err, #name); return cx_value(r); }
CM1(cm_sqrt, cx_sqrt(z))
CM1(cm_exp, cx_exp(z, &err))
CM1(cm_log10, cx_log10(z, &err))
CM1(cm_sin, cx_sin(z))
CM1(cm_cos, cx_cos(z))
CM1(cm_tan, cx_tan(z))
CM1(cm_asin, cx_asin(z))
CM1(cm_acos, cx_acos(z))
CM1(cm_atan, cx_atan(z, &err))
CM1(cm_sinh, cx_sinh(z))
CM1(cm_cosh, cx_cosh(z))
CM1(cm_tanh, cx_tanh(z))
CM1(cm_asinh, cx_asinh(z))
CM1(cm_acosh, cx_acosh(z))
CM1(cm_atanh, cx_atanh(z, &err))
BI(cm_log)
{
    int err = 0;
    Cx z = cm_arg(x, 0), r;
    need(x, 1, 2, "log");
    r = cx_log(z, &err);
    cx_raise(err, "log");
    if (x->n > 1) {
        Cx b = cm_arg(x, 1), lb;
        lb = cx_log(b, &err);
        cx_raise(err, "log");
        r = cx_quot(r, lb, &err);
        if (err) throw_new(cls_ZeroDivisionError, "complex division by zero");
    }
    return cx_value(r);
}
BI(cm_phase)
{
    Cx z = cm_arg(x, 0);
    return mkfloat(atan2(z.i, z.r));
}
BI(cm_polar)
{
    Cx z = cm_arg(x, 0);
    Value t[2];
    double h = n_hypot(z.r, z.i);
    if (d_isinf(h) && d_isfinite(z.r) && d_isfinite(z.i))
        throw_new(cls_OverflowError, "math range error");
    t[0] = mkfloat(h);
    t[1] = mkfloat(atan2(z.i, z.r));
    return mktuple_from(t, 2);
}
BI(cm_rect)
{
    double r, phi;
    need(x, 2, 2, "rect");
    r = dbl_arg(x, 0);
    phi = dbl_arg(x, 1);
    if (phi == 0.0 && d_isfinite(r)) return mkcomplex(r, phi * r);
    return mkcomplex(r * cos(phi), r * sin(phi));
}
BI(cm_isfinite)
{
    Cx z = cm_arg(x, 0);
    return mkbool(d_isfinite(z.r) && d_isfinite(z.i));
}
BI(cm_isnan)
{
    Cx z = cm_arg(x, 0);
    return mkbool(d_isnan(z.r) || d_isnan(z.i));
}
BI(cm_isinf)
{
    Cx z = cm_arg(x, 0);
    return mkbool(d_isinf(z.r) || d_isinf(z.i));
}
BI(cm_isclose)
{
    Cx a, b;
    Value rv = kwget(x, "rel_tol"), av = kwget(x, "abs_tol");
    double rel = 1e-9, abst = 0.0, diff, ma, mb;
    need(x, 2, 2, "isclose");
    a = cm_arg(x, 0);
    b = cm_arg(x, 1);
    if (rv.t != T_UNSET) rel = to_dbl(rv);
    if (av.t != T_UNSET) abst = to_dbl(av);
    if (rel < 0.0 || abst < 0.0) throw_new(cls_ValueError, "tolerances must be non-negative");
    if (a.r == b.r && a.i == b.i) return TRUE_V;
    if (d_isinf(a.r) || d_isinf(a.i) || d_isinf(b.r) || d_isinf(b.i)) return FALSE_V;
    diff = n_hypot(a.r - b.r, a.i - b.i);
    ma = n_hypot(a.r, a.i);
    mb = n_hypot(b.r, b.i);
    return mkbool(diff <= rel * mb || diff <= rel * ma || diff <= abst);
}

static void complex_init(void)
{
    cls_complex = mk_type("complex", cls_object, bi_complex);
}
static void complex_methods(void)
{
    def_bi(cls_complex, "conjugate", cx_conjugate);
    def_bi(cls_complex, "__abs__", cx_abs_m);
    def_bi(cls_complex, "__complex__", cx_complex_m);
    def_bi(cls_complex, "__getnewargs__", cx_getnewargs);
}
static void cmath_init(void)
{
    Module *m = new_module("cmath");
    mod_def(m, "sqrt", cm_sqrt); mod_def(m, "exp", cm_exp); mod_def(m, "log", cm_log);
    mod_def(m, "log10", cm_log10); mod_def(m, "sin", cm_sin); mod_def(m, "cos", cm_cos);
    mod_def(m, "tan", cm_tan); mod_def(m, "asin", cm_asin); mod_def(m, "acos", cm_acos);
    mod_def(m, "atan", cm_atan); mod_def(m, "sinh", cm_sinh); mod_def(m, "cosh", cm_cosh);
    mod_def(m, "tanh", cm_tanh); mod_def(m, "asinh", cm_asinh); mod_def(m, "acosh", cm_acosh);
    mod_def(m, "atanh", cm_atanh); mod_def(m, "phase", cm_phase); mod_def(m, "polar", cm_polar);
    mod_def(m, "rect", cm_rect); mod_def(m, "isfinite", cm_isfinite);
    mod_def(m, "isnan", cm_isnan); mod_def(m, "isinf", cm_isinf);
    mod_def(m, "isclose", cm_isclose);
    mod_val(m, "pi", mkfloat(CX_PI));
    mod_val(m, "e", mkfloat(CX_E));
    mod_val(m, "tau", mkfloat(2.0 * CX_PI));
    mod_val(m, "inf", mkfloat(HUGE_VAL));
    mod_val(m, "nan", mkfloat(d_nan()));
    mod_val(m, "infj", mkcomplex(0.0, HUGE_VAL));
    mod_val(m, "nanj", mkcomplex(0.0, d_nan()));
}
