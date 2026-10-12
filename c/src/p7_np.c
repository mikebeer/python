/* =========================================================================
   p7_np.c   numpy subset: native ndarray (bool/int64/float64, N-d, row-major,
             contiguous copies), indexing, broadcasting, reductions, printing
   ========================================================================= */
enum {
    OP_MAX = 40, OP_MIN = 41, OP_CMP = 50       /* OP_CMP + C_LT .. OP_CMP + C_NE */
};
enum {
    R_SUM, R_PROD, R_MIN, R_MAX, R_MEAN, R_ARGMIN, R_ARGMAX, R_ANY, R_ALL, R_STD, R_VAR,
    R_CUMSUM, R_CUMPROD
};
enum {
    F_SQRT, F_EXP, F_SIN, F_COS, F_TAN, F_LOG, F_LOG10, F_LOG2, F_FLOOR, F_CEIL, F_TANH,
    F_SINH, F_COSH, F_ASIN, F_ACOS, F_ATAN, F_ABS, F_SIGN, F_SQUARE, F_ISNAN, F_ISINF,
    F_ISFINITE, F_NEG
};

static Module *new_module(const char *name);
static void mod_def(Module *m, const char *name, BFn fn);
static void mod_val(Module *m, const char *name, Value v);
static void np_init2(Module *m);
static void la_init(Module *np);
static void rnd_init(Module *np);

typedef struct NdTmp { Nd nd; long i; double d; double c[2]; } NdTmp;

/* ---- construction ----------------------------------------------------- */
static Nd *nd_new(int dt, int nd, const long *shape)
{
    Nd *a = (Nd *)gc_alloc(sizeof(Nd), T_ND);
    int i;
    long n = 1;
    a->dt = dt; a->nd = nd;
    for (i = 0; i < ND_MAXD; i++) a->shape[i] = 0;
    for (i = 0; i < nd; i++) { a->shape[i] = shape[i]; n *= shape[i]; }
    a->size = n;
    a->iv = NULL; a->dv = NULL;
    if (dt == ND_FLOAT) a->dv = (double *)xmalloc(sizeof(double) * (size_t)n);
    else if (dt == ND_COMPLEX) a->dv = (double *)xmalloc(2 * sizeof(double) * (size_t)n);
    else a->iv = (long *)xmalloc(sizeof(long) * (size_t)n);
    GC_ACCOUNT(8L * n);
    return a;
}
static Nd *nd_new1(int dt, long n)
{
    return nd_new(dt, 1, &n);
}
static Value ndv(Nd *a)
{
    return mkobj(a, T_ND);
}
static Value nd_mkint(long v)
{
    if (v <= SI_MAX && v >= -SI_MAX) return mkint(v);
    return int_from_double((double)v);
}
static Value nd_elem(const Nd *a, long i)
{
    if (a->dt == ND_FLOAT) return mkfloat(a->dv[i]);
    if (a->dt == ND_COMPLEX) return mkcomplex(a->dv[2 * i], a->dv[2 * i + 1]);
    if (a->dt == ND_BOOL) return tobool(a->iv[i] != 0);
    return nd_mkint(a->iv[i]);
}
static double nd_d(const Nd *a, long i)
{
    if (a->dt == ND_COMPLEX) return a->dv[2 * i];
    return a->dt == ND_FLOAT ? a->dv[i] : (double)a->iv[i];
}
static Value nd_ret(Nd *r)
{
    if (r->nd == 0) return nd_elem(r, 0);
    return ndv(r);
}
static void nd_cp(Nd *d, long doff, const Nd *s, long soff, long cnt)
{
    long i;
    if (d->dt == ND_COMPLEX) {
        if (s->dt == ND_COMPLEX)
            memmove(d->dv + 2 * doff, s->dv + 2 * soff, 2 * sizeof(double) * (size_t)cnt);
        else for (i = 0; i < cnt; i++) {
            d->dv[2 * (doff + i)] = nd_d(s, soff + i);
            d->dv[2 * (doff + i) + 1] = 0.0;
        }
        return;
    }
    if (s->dt == ND_COMPLEX) {
        if (d->dt == ND_FLOAT) for (i = 0; i < cnt; i++) d->dv[doff + i] = s->dv[2 * (soff + i)];
        else if (d->dt == ND_BOOL)
            for (i = 0; i < cnt; i++)
                d->iv[doff + i] = s->dv[2 * (soff + i)] != 0 || s->dv[2 * (soff + i) + 1] != 0;
        else for (i = 0; i < cnt; i++) d->iv[doff + i] = (long)s->dv[2 * (soff + i)];
        return;
    }
    if (d->dt == ND_FLOAT) {
        if (s->dt == ND_FLOAT) memmove(d->dv + doff, s->dv + soff, sizeof(double) * (size_t)cnt);
        else for (i = 0; i < cnt; i++) d->dv[doff + i] = (double)s->iv[soff + i];
    } else if (d->dt == ND_BOOL) {
        if (s->dt == ND_FLOAT) for (i = 0; i < cnt; i++) d->iv[doff + i] = s->dv[soff + i] != 0;
        else for (i = 0; i < cnt; i++) d->iv[doff + i] = s->iv[soff + i] != 0;
    } else {
        if (s->dt == ND_FLOAT) for (i = 0; i < cnt; i++) d->iv[doff + i] = (long)s->dv[soff + i];
        else memmove(d->iv + doff, s->iv + soff, sizeof(long) * (size_t)cnt);
    }
}
static Nd *nd_astype(const Nd *a, int dt)
{
    Nd *r = nd_new(dt, a->nd, a->shape);
    nd_cp(r, 0, a, 0, a->size);
    return r;
}
static Nd *nd_to_float(Nd *a)
{
    if (a->dt == ND_FLOAT) return a;
    if (a->dt == ND_COMPLEX)
        throw_new(cls_TypeError, "complex dtype is not supported by this function");
    return nd_astype(a, ND_FLOAT);
}
static Nd *nd_reshaped(const Nd *a, int nd, const long *shape)
{
    Nd *r = nd_new(a->dt, nd, shape);
    nd_cp(r, 0, a, 0, a->size);
    return r;
}
static void shape_str(int nd, const long *shape, char *out)
{
    int k;
    char t[32];
    strcpy(out, "(");
    for (k = 0; k < nd; k++) {
        sprintf(t, k ? ",%ld" : "%ld", shape[k]);
        strcat(out, t);
    }
    if (nd == 1) strcat(out, ",");
    strcat(out, ")");
}

/* ---- python values -> arrays ------------------------------------------ */
static int py_dt(Value v)
{
    switch (v.t) {
    case T_BOOL: return ND_BOOL;
    case T_INT: case T_BIG: return ND_INT;
    case T_FLOAT: return ND_FLOAT;
    case T_COMPLEX: return ND_COMPLEX;
    default: return -1;
    }
}
static int seq_len(Value v, long *n)
{
    if (v.t == T_LIST || v.t == T_TUPLE) { *n = AS_LIST(v)->n; return 1; }
    if (v.t == T_ND && AS_ND(v)->nd >= 1) { *n = AS_ND(v)->shape[0]; return 1; }
    return 0;
}
static void nd_scan(Value v, int depth, long *shape, int *ndim, int *dt)
{
    long n, i;
    if (seq_len(v, &n)) {
        if (depth >= ND_MAXD || (*ndim >= 0 && depth >= *ndim))
            throw_new(cls_ValueError, "setting an array element with a sequence. The requested "
                      "array has an inhomogeneous shape");
        if (shape[depth] < 0) shape[depth] = n;
        else if (shape[depth] != n)
            throw_new(cls_ValueError, "setting an array element with a sequence. The requested "
                      "array has an inhomogeneous shape");
        if (n == 0 && *ndim < 0) *ndim = depth + 1;
        for (i = 0; i < n; i++) {
            Value it = v.t == T_ND ? nd_item(v, i) : AS_LIST(v)->a[i];
            nd_scan(it, depth + 1, shape, ndim, dt);
        }
    } else {
        int d = py_dt(v);
        if (d < 0)
            throw_new(cls_TypeError, "unsupported array element of type '%.40s'", type_name(v));
        if (*ndim < 0) *ndim = depth;
        else if (*ndim != depth)
            throw_new(cls_ValueError, "setting an array element with a sequence. The requested "
                      "array has an inhomogeneous shape");
        if (d > *dt) *dt = d;
    }
}
static void nd_fill(Nd *r, Value v, long *pos)
{
    long n, i;
    if (seq_len(v, &n)) {
        for (i = 0; i < n; i++)
            nd_fill(r, v.t == T_ND ? nd_item(v, i) : AS_LIST(v)->a[i], pos);
        return;
    }
    if (r->dt == ND_COMPLEX) {
        Cx z;
        to_cx(v, &z);
        r->dv[2 * *pos] = z.r; r->dv[2 * *pos + 1] = z.i;
    } else if (r->dt == ND_FLOAT) r->dv[*pos] = to_dbl(v);
    else if (r->dt == ND_BOOL) r->iv[*pos] = v.t == T_FLOAT ? v.u.d != 0 : (v.u.i != 0);
    else r->iv[*pos] = v.t == T_FLOAT ? (long)v.u.d : (v.t == T_BIG ? 0 : v.u.i);
    (*pos)++;
}
/* dtype objects (instances of numpy.dtype, one per ND_* code) */
static Class *cls_dtype;
static Value dt_inst[4];
/* ND_* code for a dtype object, type, or name; -1 if not understood */
static int dtype_lookup(Value v)
{
    if (v.t == T_INST && AS_INST(v)->cls == cls_dtype) {
        int k;
        for (k = 0; k < 4; k++) if (dt_inst[k].t == T_INST && dt_inst[k].u.o == v.u.o) return k;
    } else if (v.t == T_CLASS) {
        if (AS_CLASS(v) == cls_float) return ND_FLOAT;
        if (AS_CLASS(v) == cls_int) return ND_INT;
        if (AS_CLASS(v) == cls_bool) return ND_BOOL;
        if (AS_CLASS(v) == cls_complex) return ND_COMPLEX;
    } else if (v.t == T_STR) {
        const char *s = AS_STR(v)->s;
        if (!strcmp(s, "complex128") || !strcmp(s, "complex") || !strcmp(s, "c16") ||
            !strcmp(s, "complex64") || !strcmp(s, "c8") || !strcmp(s, "D") ||
            !strcmp(s, "complex_")) return ND_COMPLEX;
        if (!strcmp(s, "float64") || !strcmp(s, "float") || !strcmp(s, "f8") ||
            !strcmp(s, "float32") || !strcmp(s, "d") || !strcmp(s, "f4") ||
            !strcmp(s, "float16") || !strcmp(s, "f2") || !strcmp(s, "f") ||
            !strcmp(s, "float_") || !strcmp(s, "double")) return ND_FLOAT;
        if (!strcmp(s, "int64") || !strcmp(s, "int") || !strcmp(s, "i8") ||
            !strcmp(s, "int32") || !strcmp(s, "i4") || !strcmp(s, "int16") ||
            !strcmp(s, "int8") || !strcmp(s, "uint8") || !strcmp(s, "i") ||
            !strcmp(s, "uint16") || !strcmp(s, "uint32") || !strcmp(s, "uint64") ||
            !strcmp(s, "uint") || !strcmp(s, "int_") || !strcmp(s, "i2") ||
            !strcmp(s, "i1") || !strcmp(s, "u1") || !strcmp(s, "u2") || !strcmp(s, "u4") ||
            !strcmp(s, "u8") || !strcmp(s, "l")) return ND_INT;
        if (!strcmp(s, "bool") || !strcmp(s, "?") || !strcmp(s, "bool_")) return ND_BOOL;
    }
    return -1;
}
static int parse_dtype(Value v)
{
    int d = dtype_lookup(v);
    if (d < 0) throw_new(cls_TypeError, "data type not understood");
    return d;
}
static const char *dt_name(int dt)
{
    return dt == ND_FLOAT ? "float64" : (dt == ND_INT ? "int64" :
           (dt == ND_COMPLEX ? "complex128" : "bool"));
}
/* dtype methods: ==  !=  hash  repr  str */
static int dt_self(Args *x)
{
    int d = x->n > 0 ? dtype_lookup(A(0)) : -1;
    if (d < 0) throw_new(cls_TypeError, "descriptor requires a 'numpy.dtype' object");
    return d;
}
BI(dt_eq)
{
    int d = dt_self(x), o = x->n > 1 ? dtype_lookup(A(1)) : -1;
    return mkbool(o >= 0 && o == d);
}
BI(dt_ne)
{
    int d = dt_self(x), o = x->n > 1 ? dtype_lookup(A(1)) : -1;
    return mkbool(!(o >= 0 && o == d));
}
BI(dt_hash)
{
    return mkint((long)(py_hash(mkcstr(dt_name(dt_self(x)))) & 0x3fffffffUL));
}
BI(dt_repr)
{
    char t[64];
    sprintf(t, "dtype('%s')", dt_name(dt_self(x)));
    return mkcstr(t);
}
BI(dt_str)
{
    return mkcstr(dt_name(dt_self(x)));
}
BI(dt_new)
{
    if (x->n < 1) throw_new(cls_TypeError, "dtype() missing required argument 'dtype'");
    if (A(0).t == T_NONE) return dt_inst[ND_FLOAT];
    return dt_inst[parse_dtype(A(0))];
}
static void dtype_init(Module *m)
{
    static const char *kind[4] = { "b", "i", "f", "c" };
    static const char *chr[4] = { "?", "l", "d", "D" };
    static const char *str[4] = { "|b1", "<i8", "<f8", "<c16" };
    static const int isz[4] = { 1, 8, 8, 16 };
    Class *c = mod_class("dtype", cls_object, "numpy");
    Class *types[4];
    List *all = tuple_new(4);
    Value allv = mkobj(all, T_TUPLE);
    int k;
    types[0] = cls_bool; types[1] = cls_int; types[2] = cls_float; types[3] = cls_complex;
    c->ctor = dt_new;
    cls_dtype = c;
    mod_val(m, "dtype", mkobj(c, T_CLASS));
    def_bi(c, "__eq__", dt_eq); def_bi(c, "__ne__", dt_ne); def_bi(c, "__hash__", dt_hash);
    def_bi(c, "__repr__", dt_repr); def_bi(c, "__str__", dt_str);
    for (k = 0; k < 4; k++) {
        Inst *in = new_inst(c);
        dt_inst[k] = mkobj(in, T_INST);
        all->a[k] = dt_inst[k];
    }
    amap_set(&c->ns, intern_c("_all"), allv);
    for (k = 0; k < 4; k++) {
        Inst *in = AS_INST(dt_inst[k]);
        amap_set(&in->a, intern_c("name"), mkcstr(dt_name(k)));
        amap_set(&in->a, intern_c("kind"), mkcstr(kind[k]));
        amap_set(&in->a, intern_c("char"), mkcstr(chr[k]));
        amap_set(&in->a, intern_c("str"), mkcstr(str[k]));
        amap_set(&in->a, intern_c("itemsize"), mkint(isz[k]));
        amap_set(&in->a, intern_c("type"), mkobj(types[k], T_CLASS));
        amap_set(&in->a, intern_c("byteorder"), mkcstr(k == 0 ? "|" : "="));
    }
}

/* abstract scalar types: np.generic np.number np.integer ...; Python int/float/complex/bool
   values are their instances (np.float64 etc. are the builtin types) */
enum { NA_GENERIC, NA_NUMBER, NA_INTEGER, NA_SIGNED, NA_UNSIGNED, NA_INEXACT, NA_FLOATING,
       NA_COMPLEXF, NA_N };
static Class *cls_npabs[NA_N];
static int npabs_has(Class *abs, Class *k)
{
    int i, id = -1;
    for (i = 0; i < NA_N; i++) if (cls_npabs[i] == abs) id = i;
    if (id < 0) return 0;
    if (id == NA_GENERIC) return k == cls_bool || k == cls_int || k == cls_float ||
                                 k == cls_complex || k == cls_npabs[NA_GENERIC];
    if (k == cls_int) return id == NA_NUMBER || id == NA_INTEGER || id == NA_SIGNED;
    if (k == cls_float) return id == NA_NUMBER || id == NA_INEXACT || id == NA_FLOATING;
    if (k == cls_complex) return id == NA_NUMBER || id == NA_INEXACT || id == NA_COMPLEXF;
    return 0;
}

/* build an array from a python scalar / nested sequence / array (always a fresh copy) */
static Nd *nd_from_value(Value v, int force)
{
    long shape[ND_MAXD];
    int ndim = -1, dt = ND_BOOL, k;
    long pos = 0;
    Nd *r;
    if (v.t == T_ND) return nd_astype(AS_ND(v), force >= 0 ? force : AS_ND(v)->dt);
    for (k = 0; k < ND_MAXD; k++) shape[k] = -1;
    nd_scan(v, 0, shape, &ndim, &dt);
    if (ndim < 0) ndim = 0;
    {
        long n = 1;
        for (k = 0; k < ndim; k++) n *= shape[k];
        if (n == 0) dt = ND_FLOAT;
    }
    if (force >= 0) dt = force;
    r = nd_new(dt, ndim, shape);
    nd_fill(r, v, &pos);
    return r;
}
static Nd *nd_coerce_c(Value v, NdTmp *t)
{
    if (v.t == T_ND) return AS_ND(v);
    if (v.t == T_LIST || v.t == T_TUPLE) return nd_from_value(v, -1);
    if (py_dt(v) < 0) throw_new(cls_TypeError, "unsupported operand type '%.40s'", type_name(v));
    memset(&t->nd, 0, sizeof(Nd));
    t->nd.nd = 0; t->nd.size = 1;
    if (v.t == T_COMPLEX) {
        t->nd.dt = ND_COMPLEX; t->c[0] = AS_CPLX(v)->re; t->c[1] = AS_CPLX(v)->im;
        t->nd.dv = t->c;
    } else if (v.t == T_FLOAT || v.t == T_BIG) {
        t->nd.dt = ND_FLOAT; t->d = to_dbl(v); t->nd.dv = &t->d;
    } else {
        t->nd.dt = v.t == T_BOOL ? ND_BOOL : ND_INT; t->i = v.u.i; t->nd.iv = &t->i;
    }
    return &t->nd;
}
/* real-only variant: functions that do not handle complex arrays reject them */
static Nd *nd_coerce(Value v, NdTmp *t)
{
    Nd *a = nd_coerce_c(v, t);
    if (a->dt == ND_COMPLEX)
        throw_new(cls_TypeError, "complex dtype is not supported by this function");
    return a;
}
static Nd *nd_arg0c(Args *x, NdTmp *t)
{
    if (x->n < 1) throw_new(cls_TypeError, "missing array argument");
    return nd_coerce_c(A(0), t);
}
static Nd *nd_arg0(Args *x, NdTmp *t)
{
    if (x->n < 1) throw_new(cls_TypeError, "missing array argument");
    return nd_coerce(A(0), t);
}

/* ---- broadcasting and elementwise operations -------------------------- */
static int nd_bshape(const Nd *a, const Nd *b, long *rs)
{
    int rnd = a->nd > b->nd ? a->nd : b->nd, k;
    for (k = 0; k < rnd; k++) {
        int ka = k - (rnd - a->nd), kb = k - (rnd - b->nd);
        long da = ka >= 0 ? a->shape[ka] : 1, db = kb >= 0 ? b->shape[kb] : 1;
        if (da == db || db == 1) rs[k] = da;
        else if (da == 1) rs[k] = db;
        else {
            char s1[160], s2[160];
            shape_str(a->nd, a->shape, s1);
            shape_str(b->nd, b->shape, s2);
            throw_new(cls_ValueError, "operands could not be broadcast together with shapes "
                      "%s %s ", s1, s2);
        }
    }
    return rnd;
}
static void nd_bstrides(const Nd *a, int rnd, long *st)
{
    long s = 1;
    int k, off = rnd - a->nd;
    for (k = rnd - 1; k >= 0; k--) {
        int ak = k - off;
        if (ak < 0) st[k] = 0;
        else { st[k] = a->shape[ak] == 1 ? 0 : s; s *= a->shape[ak]; }
    }
}
static void nd_odo(long *idx, int n, const long *rs, const long *sa, const long *sb,
                   long *oa, long *ob)
{
    int k = n - 1;
    while (k >= 0) {
        idx[k]++;
        *oa += sa[k]; *ob += sb[k];
        if (idx[k] < rs[k]) return;
        *oa -= sa[k] * rs[k]; *ob -= sb[k] * rs[k];
        idx[k] = 0;
        k--;
    }
}
static long nd_ifloordiv(long x, long y)
{
    long q;
    if (y == 0) return 0;
    if (y == -1) return (long)(0UL - (unsigned long)x);
    q = x / y;
    if ((x % y != 0) && ((x < 0) != (y < 0))) q--;
    return q;
}
static long nd_imod(long x, long y)
{
    long m;
    if (y == 0 || y == -1) return 0;
    m = x % y;
    if (m != 0 && ((m < 0) != (y < 0))) m += y;
    return m;
}
static long nd_ipow(long x, long y)
{
    unsigned long r = 1, b = (unsigned long)x;
    if (y < 0) throw_new(cls_ValueError, "Integers to negative integer powers are not allowed.");
    while (y > 0) {
        if (y & 1) r *= b;
        b *= b;
        y >>= 1;
    }
    return (long)r;
}
static double nd_fdiv(double x, double y)
{
    if (y == 0) return x / y;
    return fl_floordiv(x, y);
}
static double nd_fmod(double x, double y)
{
    if (y == 0) return x - x;
    return fl_mod(x, y);
}
static double nd_rint(double x)
{
    double r, d;
    if (x != x || x > 1e300 || x < -1e300) return x;
    r = floor(x);
    d = x - r;
    if (d > 0.5) r += 1.0;
    else if (d == 0.5 && fmod(r, 2.0) != 0) r += 1.0;
    if (r == 0 && x < 0) r = -0.0;
    return r;
}

#define NDLOOP(RT, AT, BT, EXPR) \
    do { \
        RT *rp_ = (RT *)rptr; const AT *ap_ = (const AT *)aptr; const BT *bp_ = (const BT *)bptr; \
        long ii_, jj_, oa_ = 0, ob_ = 0; \
        for (k = 0; k < ND_MAXD; k++) idx[k] = 0; \
        for (ii_ = 0; ii_ < nouter; ii_++) { \
            for (jj_ = 0; jj_ < inner; jj_++) { \
                AT x = ap_[oa_ + jj_ * isa]; BT y = bp_[ob_ + jj_ * isb]; \
                *rp_++ = (RT)(EXPR); \
            } \
            nd_odo(idx, rnd - 1, rs, sa, sb, &oa_, &ob_); \
        } \
    } while (0)

static Nd *nd_expand(const Nd *a, int rnd, const long *rs, int dt);
/* complex ordering is lexicographic (real part first) */
static int cx_cmp3(double ar, double ai, double br, double bi)
{
    if (ar < br) return -1;
    if (ar > br) return 1;
    return ai < bi ? -1 : (ai > bi ? 1 : 0);
}
static Cx nd_cpow(Cx a, Cx b)
{
    int err = 0;
    Cx r;
    if (b.r == 0.0 && b.i == 0.0) return cx_mk(1.0, 0.0);
    if (a.r == 0.0 && a.i == 0.0) {
        if (b.r > 0.0 && b.i == 0.0) return cx_mk(0.0, 0.0);
        return cx_mk(d_nan(), d_nan());
    }
    r = cx_pow(a, b, &err);
    return r;
}
static Value nd_binary_c(int op, Nd *a, Nd *b)
{
    long rs[ND_MAXD], i;
    int rnd = nd_bshape(a, b, rs);
    int cmp = op >= OP_CMP;
    Nd *ea, *eb, *r;
    ea = nd_expand(a, rnd, rs, ND_COMPLEX);
    eb = nd_expand(b, rnd, rs, ND_COMPLEX);
    r = nd_new(cmp ? ND_BOOL : ND_COMPLEX, rnd, rs);
    for (i = 0; i < r->size; i++) {
        Cx x = cx_mk(ea->dv[2 * i], ea->dv[2 * i + 1]), y = cx_mk(eb->dv[2 * i], eb->dv[2 * i + 1]),
            z;
        int err = 0;
        if (cmp) {
            int c = op - OP_CMP, k;
            if (c == C_EQ) r->iv[i] = x.r == y.r && x.i == y.i;
            else if (c == C_NE) r->iv[i] = x.r != y.r || x.i != y.i;
            else {
                k = (x.r != x.r || y.r != y.r || x.i != x.i || y.i != y.i) ? 2 :
                    cx_cmp3(x.r, x.i, y.r, y.i);
                if (k == 2) r->iv[i] = 0;
                else r->iv[i] = c == C_LT ? k < 0 :
                    (c == C_LE ? k <= 0 : (c == C_GT ? k > 0 : k >= 0));
            }
            continue;
        }
        switch (op) {
        case B_ADD: z = cx_add(x, y); break;
        case B_SUB: z = cx_sub(x, y); break;
        case B_MUL: z = cx_mul(x, y); break;
        case B_DIV:
            z = cx_quot(x, y, &err);
            if (err) z = cx_mk(x.r / 0.0, x.i / 0.0);
            break;
        case B_POW: z = nd_cpow(x, y); break;
        case OP_MAX: z = cx_cmp3(x.r, x.i, y.r, y.i) >= 0 ? x : y; break;
        case OP_MIN: z = cx_cmp3(x.r, x.i, y.r, y.i) <= 0 ? x : y; break;
        default:
            throw_new(cls_TypeError, "ufunc '%s' not supported for the input types, and the inputs "
                      "could not be safely coerced to any supported types according to the casting "
                      "rule ''safe''", op == B_FDIV ? "floor_divide" : (op == B_MOD ? "remainder" :
                      "bitwise"));
            return NONE_V;
        }
        r->dv[2 * i] = z.r; r->dv[2 * i + 1] = z.i;
    }
    return nd_ret(r);
}
static Value nd_binary(int op, Nd *a, Nd *b)
{
    long rs[ND_MAXD], sa[ND_MAXD], sb[ND_MAXD], idx[ND_MAXD];
    long inner, nouter, isa, isb, n;
    int rnd, kd, rdt, k, cmp = op >= OP_CMP, boolres = 0;
    Nd *r;
    void *rptr, *aptr, *bptr;
    if (a->dt == ND_COMPLEX || b->dt == ND_COMPLEX) return nd_binary_c(op, a, b);
    rnd = nd_bshape(a, b, rs);
    kd = a->dt > b->dt ? a->dt : b->dt;
    if (cmp) { rdt = ND_BOOL; if (kd == ND_BOOL) kd = ND_INT; }
    else {
        switch (op) {
        case B_DIV: kd = ND_FLOAT; rdt = ND_FLOAT; break;
        case B_AND: case B_OR: case B_XOR: case B_LSH: case B_RSH:
            if (kd == ND_FLOAT)
                throw_new(cls_TypeError, "ufunc bitwise operation not supported for float inputs");
            rdt = (a->dt == ND_BOOL && b->dt == ND_BOOL) ? ND_BOOL : ND_INT;
            kd = ND_INT;
            break;
        default:
            if (kd == ND_BOOL) {
                kd = ND_INT;
                rdt = (op == B_ADD || op == B_MUL || op == OP_MAX || op == OP_MIN) ? ND_BOOL
                    : ND_INT;
                boolres = rdt == ND_BOOL;
            } else rdt = kd;
            break;
        }
    }
    if (kd == ND_FLOAT) { a = nd_to_float(a); b = nd_to_float(b); }
    else {
        if (a->dt == ND_FLOAT) a = nd_astype(a, ND_INT);
        if (b->dt == ND_FLOAT) b = nd_astype(b, ND_INT);
    }
    r = nd_new(rdt, rnd, rs);
    if (r->size == 0) return nd_ret(r);
    nd_bstrides(a, rnd, sa);
    nd_bstrides(b, rnd, sb);
    n = r->size;
    if (rnd == 0) { inner = 1; isa = isb = 0; }
    else { inner = rs[rnd - 1]; isa = sa[rnd - 1]; isb = sb[rnd - 1]; }
    nouter = n / inner;
    rptr = rdt == ND_FLOAT ? (void *)r->dv : (void *)r->iv;
    aptr = kd == ND_FLOAT ? (void *)a->dv : (void *)a->iv;
    bptr = kd == ND_FLOAT ? (void *)b->dv : (void *)b->iv;
    if (cmp) {
        int c = op - OP_CMP;
        if (kd == ND_FLOAT) {
            switch (c) {
            case C_LT: NDLOOP(long, double, double, x < y); break;
            case C_LE: NDLOOP(long, double, double, x <= y); break;
            case C_GT: NDLOOP(long, double, double, x > y); break;
            case C_GE: NDLOOP(long, double, double, x >= y); break;
            case C_EQ: NDLOOP(long, double, double, x == y); break;
            default: NDLOOP(long, double, double, x != y); break;
            }
        } else {
            switch (c) {
            case C_LT: NDLOOP(long, long, long, x < y); break;
            case C_LE: NDLOOP(long, long, long, x <= y); break;
            case C_GT: NDLOOP(long, long, long, x > y); break;
            case C_GE: NDLOOP(long, long, long, x >= y); break;
            case C_EQ: NDLOOP(long, long, long, x == y); break;
            default: NDLOOP(long, long, long, x != y); break;
            }
        }
    } else if (kd == ND_FLOAT) {
        switch (op) {
        case B_ADD: NDLOOP(double, double, double, x + y); break;
        case B_SUB: NDLOOP(double, double, double, x - y); break;
        case B_MUL: NDLOOP(double, double, double, x * y); break;
        case B_DIV: NDLOOP(double, double, double, x / y); break;
        case B_FDIV: NDLOOP(double, double, double, nd_fdiv(x, y)); break;
        case B_MOD: NDLOOP(double, double, double, nd_fmod(x, y)); break;
        case B_POW: NDLOOP(double, double, double, pow(x, y)); break;
        case OP_MAX: NDLOOP(double, double, double, (x != x || x > y) ? x : y); break;
        case OP_MIN: NDLOOP(double, double, double, (x != x || x < y) ? x : y); break;
        default: throw_new(cls_TypeError, "unsupported operand type(s) for %s: 'ndarray' and "
                           "'ndarray'", op_str(op));
        }
    } else {
        switch (op) {
        case B_ADD: NDLOOP(long, long, long, (long)((unsigned long)x + (unsigned long)y)); break;
        case B_SUB: NDLOOP(long, long, long, (long)((unsigned long)x - (unsigned long)y)); break;
        case B_MUL: NDLOOP(long, long, long, (long)((unsigned long)x * (unsigned long)y)); break;
        case B_FDIV: NDLOOP(long, long, long, nd_ifloordiv(x, y)); break;
        case B_MOD: NDLOOP(long, long, long, nd_imod(x, y)); break;
        case B_POW: NDLOOP(long, long, long, nd_ipow(x, y)); break;
        case B_AND: NDLOOP(long, long, long, x & y); break;
        case B_OR: NDLOOP(long, long, long, x | y); break;
        case B_XOR: NDLOOP(long, long, long, x ^ y); break;
        case B_LSH: NDLOOP(long, long, long, (y < 0 || y > 62) ? 0 : (long)((unsigned long)x << y));
            break;
        case B_RSH: NDLOOP(long, long, long, (y < 0 || y > 62) ? (x < 0 ? -1 : 0) : (x >> y));
            break;
        case OP_MAX: NDLOOP(long, long, long, x > y ? x : y); break;
        case OP_MIN: NDLOOP(long, long, long, x < y ? x : y); break;
        default: throw_new(cls_TypeError, "unsupported operand type(s) for %s: 'ndarray' and "
                           "'ndarray'", op_str(op));
        }
    }
    if (boolres) for (n = 0; n < r->size; n++) r->iv[n] = r->iv[n] != 0;
    return nd_ret(r);
}
static Value nd_matmul(Nd *a, Nd *b, int isdot);
static Value nd_binop(int op, Value a, Value b)
{
    NdTmp ta, tb;
    Nd *x, *y;
    if (a.t != T_ND && a.t != T_LIST && a.t != T_TUPLE && py_dt(a) < 0)
        throw_new(cls_TypeError, "unsupported operand type(s) for %s: '%.40s' and '%.40s'",
                  op_str(op), type_name(a), type_name(b));
    x = nd_coerce_c(a, &ta);
    y = nd_coerce_c(b, &tb);
    if (op == B_MATMUL) return nd_matmul(x, y, 0);
    return nd_binary(op, x, y);
}
static Value nd_compare(int op, Value a, Value b)
{
    NdTmp ta, tb;
    Nd *x, *y;
    x = nd_coerce_c(a, &ta);
    y = nd_coerce_c(b, &tb);
    return nd_binary(OP_CMP + op, x, y);
}

/* ---- unary operations and math functions ------------------------------ */
static double nd_f1(int fn, double x)
{
    switch (fn) {
    case F_SQRT: return sqrt(x);
    case F_EXP: return exp(x);
    case F_SIN: return sin(x);
    case F_COS: return cos(x);
    case F_TAN: return tan(x);
    case F_LOG: return log(x);
    case F_LOG10: return log10(x);
    case F_LOG2: return log(x) / log(2.0);
    case F_FLOOR: return floor(x);
    case F_CEIL: return ceil(x);
    case F_TANH: return tanh(x);
    case F_SINH: return sinh(x);
    case F_COSH: return cosh(x);
    case F_ASIN: return asin(x);
    case F_ACOS: return acos(x);
    case F_ATAN: return atan(x);
    default: return x;
    }
}
static Value nd_ufunc1_c(int fn, Nd *a)
{
    long i;
    Nd *r;
    if (fn == F_ISNAN || fn == F_ISINF || fn == F_ISFINITE) {
        r = nd_new(ND_BOOL, a->nd, a->shape);
        for (i = 0; i < a->size; i++) {
            double re = a->dv[2 * i], im = a->dv[2 * i + 1];
            if (fn == F_ISNAN) r->iv[i] = re != re || im != im;
            else if (fn == F_ISINF) r->iv[i] = d_isinf(re) || d_isinf(im);
            else r->iv[i] = d_isfinite(re) && d_isfinite(im);
        }
        return nd_ret(r);
    }
    if (fn == F_ABS) {
        r = nd_new(ND_FLOAT, a->nd, a->shape);
        for (i = 0; i < a->size; i++) r->dv[i] = n_hypot(a->dv[2 * i], a->dv[2 * i + 1]);
        return nd_ret(r);
    }
    if (fn == F_FLOOR || fn == F_CEIL)
        throw_new(cls_TypeError, "ufunc '%s' not supported for the input types, and the inputs "
                  "could not be safely coerced to any supported types according to the casting "
                  "rule ''safe''", fn == F_FLOOR ? "floor" : "ceil");
    r = nd_new(ND_COMPLEX, a->nd, a->shape);
    for (i = 0; i < a->size; i++) {
        Cx z = cx_mk(a->dv[2 * i], a->dv[2 * i + 1]), w;
        int err = 0;
        switch (fn) {
        case F_SQRT: w = cx_sqrt(z); break;
        case F_EXP: w = cx_exp(z, &err); break;
        case F_SIN: w = cx_sin(z); break;
        case F_COS: w = cx_cos(z); break;
        case F_TAN: w = cx_tan(z); break;
        case F_LOG: w = cx_log(z, &err); if (err) w = cx_mk(-HUGE_VAL, 0.0); break;
        case F_LOG10: w = cx_log10(z, &err); if (err) w = cx_mk(-HUGE_VAL, 0.0); break;
        case F_LOG2: w = cx_log(z, &err); if (err) w = cx_mk(-HUGE_VAL, 0.0);
            else w = cx_mk(w.r / CX_LN2, w.i / CX_LN2);
            break;
        case F_TANH: w = cx_tanh(z); break;
        case F_SINH: w = cx_sinh(z); break;
        case F_COSH: w = cx_cosh(z); break;
        case F_ASIN: w = cx_asin(z); break;
        case F_ACOS: w = cx_acos(z); break;
        case F_ATAN: w = cx_atan(z, &err); break;
        case F_NEG: w = cx_neg(z); break;
        case F_SQUARE: w = cx_mul(z, z); break;
        case F_SIGN: {
            double h = n_hypot(z.r, z.i);
            w = h == 0.0 ? cx_mk(0.0, 0.0) : cx_mk(z.r / h, z.i / h);
            break; }
        default: w = z; break;
        }
        r->dv[2 * i] = w.r; r->dv[2 * i + 1] = w.i;
    }
    return nd_ret(r);
}
static Value nd_ufunc1(int fn, Nd *a)
{
    long i;
    Nd *r;
    if (a->dt == ND_COMPLEX) return nd_ufunc1_c(fn, a);
    if (fn == F_ISNAN || fn == F_ISINF || fn == F_ISFINITE) {
        r = nd_new(ND_BOOL, a->nd, a->shape);
        for (i = 0; i < a->size; i++) {
            double d = a->dt == ND_FLOAT ? a->dv[i] : 0.0;
            if (fn == F_ISNAN) r->iv[i] = d != d;
            else if (fn == F_ISINF) r->iv[i] = (d == d) && (d - d != 0);
            else r->iv[i] = (d == d) && (d - d == 0);
        }
        return nd_ret(r);
    }
    if (fn == F_ABS || fn == F_SIGN || fn == F_SQUARE || fn == F_NEG) {
        int dt = a->dt;
        if (fn == F_NEG && dt == ND_BOOL) dt = ND_INT;
        if (fn == F_SQUARE && dt == ND_BOOL) dt = ND_INT;
        r = nd_new(dt, a->nd, a->shape);
        for (i = 0; i < a->size; i++) {
            if (dt == ND_FLOAT) {
                double d = a->dv[i];
                r->dv[i] = fn == F_ABS ? fabs(d) : (fn == F_NEG ? -d : (fn == F_SQUARE ? d * d :
                           (d > 0 ? 1.0 : (d < 0 ? -1.0 : d))));
            } else {
                long v = a->iv[i];
                r->iv[i] = fn == F_ABS ? (v < 0 ? -v : v) : (fn == F_NEG ? -v :
                           (fn == F_SQUARE ? v * v : (v > 0 ? 1 : (v < 0 ? -1 : 0))));
            }
        }
        return nd_ret(r);
    }
    r = nd_new(ND_FLOAT, a->nd, a->shape);
    for (i = 0; i < a->size; i++)
        r->dv[i] = nd_f1(fn, a->dt == ND_FLOAT ? a->dv[i] : (double)a->iv[i]);
    return nd_ret(r);
}
static Value nd_unop(int op, Value av)
{
    Nd *a = AS_ND(av);
    long i;
    Nd *r;
    if (op == '-') return nd_ufunc1(F_NEG, a);
    if (op == '+') return av;
    if (a->dt == ND_FLOAT || a->dt == ND_COMPLEX)
        throw_new(cls_TypeError, "ufunc 'invert' not supported for the input types");
    r = nd_new(a->dt, a->nd, a->shape);
    for (i = 0; i < a->size; i++) r->iv[i] = a->dt == ND_BOOL ? !a->iv[i] : ~a->iv[i];
    return ndv(r);
}

/* ---- reductions ------------------------------------------------------- */
static double pw_sum(const double *a, long n)
{
    long i;
    double res;
    if (n < 8) {
        res = 0.0;
        for (i = 0; i < n; i++) res += a[i];
        return res;
    } else if (n <= 128) {
        double r[8];
        for (i = 0; i < 8; i++) r[i] = a[i];
        for (i = 8; i < n - (n % 8); i += 8) {
            r[0] += a[i + 0]; r[1] += a[i + 1]; r[2] += a[i + 2]; r[3] += a[i + 3];
            r[4] += a[i + 4]; r[5] += a[i + 5]; r[6] += a[i + 6]; r[7] += a[i + 7];
        }
        res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; i++) res += a[i];
        return res;
    } else {
        long n2 = n / 2;
        n2 -= n2 % 8;
        return pw_sum(a, n2) + pw_sum(a + n2, n - n2);
    }
}
/* numpy's pairwise summation for complex data; n counts doubles (2 per element) */
static void pw_sum_c(const double *a, long n, double *rr, double *ri)
{
    long i;
    if (n < 8) {
        *rr = 0.0; *ri = 0.0;
        for (i = 0; i < n; i += 2) { *rr += a[i]; *ri += a[i + 1]; }
    } else if (n <= 128) {
        double r[8];
        for (i = 0; i < 8; i++) r[i] = a[i];
        for (i = 8; i < n - (n % 8); i += 8) {
            r[0] += a[i + 0]; r[1] += a[i + 1]; r[2] += a[i + 2]; r[3] += a[i + 3];
            r[4] += a[i + 4]; r[5] += a[i + 5]; r[6] += a[i + 6]; r[7] += a[i + 7];
        }
        *rr = (r[0] + r[2]) + (r[4] + r[6]);
        *ri = (r[1] + r[3]) + (r[5] + r[7]);
        for (; i < n; i += 2) { *rr += a[i]; *ri += a[i + 1]; }
    } else {
        double rr2, ri2;
        long n2 = n / 2;
        n2 -= n2 % 8;
        pw_sum_c(a, n2, rr, ri);
        pw_sum_c(a + n2, n - n2, &rr2, &ri2);
        *rr += rr2; *ri += ri2;
    }
}
static int axis_norm(long ax, int nd)
{
    if (ax < -nd || ax >= nd)
        throw_new(cls_ValueError, "axis %ld is out of bounds for array of dimension %d", ax, nd);
    return (int)(ax < 0 ? ax + nd : ax);
}
/* axis argument at position pos or keyword; returns 1 and the axis if given */
static int get_axis(Args *x, int pos, Nd *a, int *axis)
{
    Value v = x->n > pos ? A(pos) : kwget(x, "axis");
    if (v.t == T_UNSET || v.t == T_NONE) return 0;
    *axis = axis_norm(need_long(v, "axis"), a->nd);
    return 1;
}
static Value nd_reduce_c(int kind, Nd *a, int has_axis, int axis, long ddof)
{
    long outer = 1, n, inner = 1, o, i, t, rs[ND_MAXD];
    int rnd = 0, k, rdt;
    Nd *r;
    if (!has_axis) n = a->size;
    else {
        for (k = 0; k < axis; k++) outer *= a->shape[k];
        n = a->shape[axis];
        for (k = axis + 1; k < a->nd; k++) inner *= a->shape[k];
        for (k = 0; k < a->nd; k++) if (k != axis) rs[rnd++] = a->shape[k];
    }
    switch (kind) {
    case R_SUM: case R_PROD: case R_MEAN: case R_MIN: case R_MAX: rdt = ND_COMPLEX; break;
    case R_ARGMIN: case R_ARGMAX: rdt = ND_INT; break;
    case R_ANY: case R_ALL: rdt = ND_BOOL; break;
    default: rdt = ND_FLOAT; break;
    }
    if ((kind == R_MIN || kind == R_MAX || kind == R_ARGMIN || kind == R_ARGMAX) && n == 0)
        throw_new(cls_ValueError, "zero-size array to reduction operation which has no identity");
    r = nd_new(rdt, rnd, rs);
    for (o = 0; o < outer; o++)
        for (i = 0; i < inner; i++) {
            long base = o * n * inner + i, ri = o * inner + i;
            const double *p = a->dv;
            switch (kind) {
            case R_SUM: case R_MEAN: {
                double sr = 0.0, si = 0.0;
                if (inner == 1) pw_sum_c(p + 2 * base, 2 * n, &sr, &si);
                else for (t = 0; t < n; t++) {
                    sr += p[2 * (base + t * inner)]; si += p[2 * (base + t * inner) + 1];
                }
                if (kind == R_MEAN) { sr /= (double)n; si /= (double)n; }
                r->dv[2 * ri] = sr; r->dv[2 * ri + 1] = si;
                break; }
            case R_PROD: {
                Cx acc = cx_mk(1.0, 0.0);
                for (t = 0; t < n; t++)
                    acc = cx_mul(acc, cx_mk(p[2 * (base + t * inner)],
                        p[2 * (base + t * inner) + 1]));
                r->dv[2 * ri] = acc.r; r->dv[2 * ri + 1] = acc.i;
                break; }
            case R_MIN: case R_MAX: case R_ARGMIN: case R_ARGMAX: {
                long bi = 0;
                for (t = 1; t < n; t++) {
                    int c = cx_cmp3(p[2 * (base + t * inner)], p[2 * (base + t * inner) + 1],
                                    p[2 * (base + bi * inner)], p[2 * (base + bi * inner) + 1]);
                    if (kind == R_MIN || kind == R_ARGMIN ? c < 0 : c > 0) bi = t;
                }
                if (kind == R_ARGMIN || kind == R_ARGMAX) r->iv[ri] = bi;
                else {
                    r->dv[2 * ri] = p[2 * (base + bi * inner)];
                    r->dv[2 * ri + 1] = p[2 * (base + bi * inner) + 1];
                }
                break; }
            case R_ANY:
                r->iv[ri] = 0;
                for (t = 0; t < n; t++)
                    if (p[2 * (base + t * inner)] != 0 || p[2 * (base + t * inner) + 1] != 0) {
                        r->iv[ri] = 1;
                        break;
                    }
                break;
            case R_ALL:
                r->iv[ri] = 1;
                for (t = 0; t < n; t++)
                    if (p[2 * (base + t * inner)] == 0 && p[2 * (base + t * inner) + 1] == 0) {
                        r->iv[ri] = 0;
                        break;
                    }
                break;
            default: {          /* R_STD, R_VAR */
                double mr = 0.0, mi = 0.0, acc, *tmp;
                tmp = (double *)xmalloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
                if (inner == 1) pw_sum_c(p + 2 * base, 2 * n, &mr, &mi);
                else for (t = 0; t < n; t++) {
                    mr += p[2 * (base + t * inner)]; mi += p[2 * (base + t * inner) + 1];
                }
                mr /= (double)n; mi /= (double)n;
                for (t = 0; t < n; t++) {
                    double dr = p[2 * (base + t * inner)] - mr,
                        di = p[2 * (base + t * inner) + 1] - mi;
                    tmp[t] = dr * dr + di * di;
                }
                if (inner == 1) acc = pw_sum(tmp, n);
                else { acc = 0.0; for (t = 0; t < n; t++) acc += tmp[t]; }
                acc = acc / (double)(n - ddof > 0 ? n - ddof : 0);
                r->dv[ri] = kind == R_STD ? sqrt(acc) : acc;
                free(tmp);
                break; }
            }
        }
    return nd_ret(r);
}
static Value nd_reduce(int kind, Nd *a, int has_axis, int axis, long ddof)
{
    long outer = 1, n, inner = 1, o, i, t, rs[ND_MAXD];
    int rnd = 0, k, rdt;
    Nd *r;
    double *tmp = NULL;
    if (a->dt == ND_COMPLEX) return nd_reduce_c(kind, a, has_axis, axis, ddof);
    if (kind == R_MEAN || kind == R_STD || kind == R_VAR) a = nd_to_float(a);
    if (!has_axis) n = a->size;
    else {
        for (k = 0; k < axis; k++) outer *= a->shape[k];
        n = a->shape[axis];
        for (k = axis + 1; k < a->nd; k++) inner *= a->shape[k];
        for (k = 0; k < a->nd; k++) if (k != axis) rs[rnd++] = a->shape[k];
    }
    switch (kind) {
    case R_SUM: case R_PROD: rdt = a->dt == ND_FLOAT ? ND_FLOAT : ND_INT; break;
    case R_MIN: case R_MAX: rdt = a->dt; break;
    case R_ARGMIN: case R_ARGMAX: rdt = ND_INT; break;
    case R_ANY: case R_ALL: rdt = ND_BOOL; break;
    default: rdt = ND_FLOAT; break;
    }
    if ((kind == R_MIN || kind == R_MAX) && n == 0)
        throw_new(cls_ValueError, "zero-size array to reduction operation %s which has no identity",
                  kind == R_MIN ? "minimum" : "maximum");
    if ((kind == R_ARGMIN || kind == R_ARGMAX) && n == 0)
        throw_new(cls_ValueError, "attempt to get %s of an empty sequence",
                  kind == R_ARGMIN ? "argmin" : "argmax");
    r = nd_new(rdt, rnd, rs);
    if (kind == R_STD || kind == R_VAR) tmp = (double *)
        xmalloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    for (o = 0; o < outer; o++) {
        for (i = 0; i < inner; i++) {
            long base = o * n * inner + i, ri = o * inner + i;
            if (a->dt == ND_FLOAT) {
                const double *p = a->dv;
                double acc;
                switch (kind) {
                case R_SUM: case R_MEAN:
                    if (inner == 1) acc = pw_sum(p + base, n);
                    else { acc = 0.0; for (t = 0; t < n; t++) acc += p[base + t * inner]; }
                    r->dv[ri] = kind == R_MEAN ? acc / (double)n : acc;
                    break;
                case R_PROD:
                    acc = 1.0;
                    for (t = 0; t < n; t++) acc *= p[base + t * inner];
                    r->dv[ri] = acc;
                    break;
                case R_MIN: case R_MAX: {
                    double best = p[base];
                    for (t = 1; t < n; t++) {
                        double v = p[base + t * inner];
                        if (best != best) break;
                        if (v != v || (kind == R_MIN ? v < best : v > best)) best = v;
                    }
                    r->dv[ri] = best;
                    break; }
                case R_ARGMIN: case R_ARGMAX: {
                    long bi = 0;
                    double best = p[base];
                    for (t = 1; t < n; t++) {
                        double v = p[base + t * inner];
                        if (best != best) break;
                        if (v != v || (kind == R_ARGMIN ? v < best : v > best)) { best = v;
                            bi = t;
                        }
                    }
                    r->iv[ri] = bi;
                    break; }
                case R_ANY:
                    r->iv[ri] = 0;
                    for (t = 0; t < n; t++) if (p[base + t * inner] != 0) { r->iv[ri] = 1; break; }
                    break;
                case R_ALL:
                    r->iv[ri] = 1;
                    for (t = 0; t < n; t++) if (p[base + t * inner] == 0) { r->iv[ri] = 0; break; }
                    break;
                default: {          /* R_STD, R_VAR */
                    double mean;
                    if (inner == 1) acc = pw_sum(p + base, n);
                    else { acc = 0.0; for (t = 0; t < n; t++) acc += p[base + t * inner]; }
                    mean = acc / (double)n;
                    for (t = 0; t < n; t++) {
                        double dlt = p[base + t * inner] - mean;
                        tmp[t] = dlt * dlt;
                    }
                    if (inner == 1) acc = pw_sum(tmp, n);
                    else { acc = 0.0; for (t = 0; t < n; t++) acc += tmp[t]; }
                    acc = acc / (double)(n - ddof > 0 ? n - ddof : 0);
                    r->dv[ri] = kind == R_STD ? sqrt(acc) : acc;
                    break; }
                }
            } else {
                const long *p = a->iv;
                long acc;
                switch (kind) {
                case R_SUM:
                    acc = 0;
                    for (t = 0; t < n; t++) acc = (long)
                        ((unsigned long)acc + (unsigned long)p[base + t * inner]);
                    r->iv[ri] = acc;
                    break;
                case R_PROD:
                    acc = 1;
                    for (t = 0; t < n; t++) acc = (long)
                        ((unsigned long)acc * (unsigned long)p[base + t * inner]);
                    r->iv[ri] = acc;
                    break;
                case R_MIN: case R_MAX: {
                    long best = p[base];
                    for (t = 1; t < n; t++) {
                        long v = p[base + t * inner];
                        if (kind == R_MIN ? v < best : v > best) best = v;
                    }
                    r->iv[ri] = best;
                    break; }
                case R_ARGMIN: case R_ARGMAX: {
                    long bi = 0, best = p[base];
                    for (t = 1; t < n; t++) {
                        long v = p[base + t * inner];
                        if (kind == R_ARGMIN ? v < best : v > best) { best = v; bi = t; }
                    }
                    r->iv[ri] = bi;
                    break; }
                case R_ANY:
                    r->iv[ri] = 0;
                    for (t = 0; t < n; t++) if (p[base + t * inner] != 0) { r->iv[ri] = 1; break; }
                    break;
                default:            /* R_ALL */
                    r->iv[ri] = 1;
                    for (t = 0; t < n; t++) if (p[base + t * inner] == 0) { r->iv[ri] = 0; break; }
                    break;
                }
            }
        }
    }
    free(tmp);
    return nd_ret(r);
}
static Value nd_cumulative(int kind, Nd *a, int has_axis, int axis)
{
    long outer = 1, n, inner = 1, o, i, t;
    int k, rdt = a->dt == ND_COMPLEX ? ND_COMPLEX : (a->dt == ND_FLOAT ? ND_FLOAT : ND_INT);
    Nd *r;
    if (!has_axis) { n = a->size; r = nd_new1(rdt, n); }
    else {
        for (k = 0; k < axis; k++) outer *= a->shape[k];
        n = a->shape[axis];
        for (k = axis + 1; k < a->nd; k++) inner *= a->shape[k];
        r = nd_new(rdt, a->nd, a->shape);
    }
    for (o = 0; o < outer; o++)
        for (i = 0; i < inner; i++) {
            long base = o * n * inner + i;
            if (rdt == ND_COMPLEX) {
                Cx acc = cx_mk(kind == R_CUMSUM ? 0.0 : 1.0, 0.0);
                for (t = 0; t < n; t++) {
                    long e = base + t * inner;
                    Cx v = cx_mk(a->dv[2 * e], a->dv[2 * e + 1]);
                    acc = kind == R_CUMSUM ? cx_add(acc, v) : cx_mul(acc, v);
                    r->dv[2 * e] = acc.r; r->dv[2 * e + 1] = acc.i;
                }
            } else if (rdt == ND_FLOAT) {
                double acc = kind == R_CUMSUM ? 0.0 : 1.0;
                for (t = 0; t < n; t++) {
                    if (kind == R_CUMSUM) acc += a->dv[base + t * inner];
                    else acc *= a->dv[base + t * inner];
                    r->dv[base + t * inner] = acc;
                }
            } else {
                unsigned long acc = kind == R_CUMSUM ? 0UL : 1UL;
                for (t = 0; t < n; t++) {
                    if (kind == R_CUMSUM) acc += (unsigned long)a->iv[base + t * inner];
                    else acc *= (unsigned long)a->iv[base + t * inner];
                    r->iv[base + t * inner] = (long)acc;
                }
            }
        }
    return ndv(r);
}
static long ddof_arg(Args *x)
{
    Value v = kwget(x, "ddof");
    if (v.t == T_UNSET || v.t == T_NONE) return 0;
    return need_long(v, "ddof");
}
#define NDRED(fname, kind) \
    BI(fname) { \
        NdTmp t_; Nd *a_ = nd_arg0c(x, &t_); int ax_ = 0, has_ = get_axis(x, 1, a_, &ax_); \
        return nd_reduce(kind, a_, has_, ax_, ddof_arg(x)); }
NDRED(np_sum, R_SUM)
NDRED(np_prod, R_PROD)
NDRED(np_min, R_MIN)
NDRED(np_max, R_MAX)
NDRED(np_mean, R_MEAN)
NDRED(np_argmin, R_ARGMIN)
NDRED(np_argmax, R_ARGMAX)
NDRED(np_any, R_ANY)
NDRED(np_all, R_ALL)
NDRED(np_std, R_STD)
NDRED(np_var, R_VAR)
#define NDCUM(fname, kind) \
    BI(fname) { \
        NdTmp t_; Nd *a_ = nd_arg0c(x, &t_); int ax_ = 0, has_ = get_axis(x, 1, a_, &ax_); \
        if (a_ == &t_.nd) a_ = nd_astype(a_, a_->dt); \
        return nd_cumulative(kind, a_, has_, ax_); }
NDCUM(np_cumsum, R_CUMSUM)
NDCUM(np_cumprod, R_CUMPROD)

/* ---- indexing --------------------------------------------------------- */
static Value mkslice(Value lo, Value hi, Value st)
{
    List *l = tuple_new(3);
    l->h.type = T_SLICE;
    l->a[0] = lo; l->a[1] = hi; l->a[2] = st;
    return mkobj(l, T_SLICE);
}
typedef struct NdSel {
    int rnd;
    long rshape[ND_MAXD];
    long *off[ND_MAXD];
    long base;
    long count;
} NdSel;

static long sel_index(long i, long n, int axis)
{
    long j = i < 0 ? i + n : i;
    if (j < 0 || j >= n)
        throw_new(cls_IndexError, "index %ld is out of bounds for axis %d with size %ld", i, axis,
            n);
    return j;
}
static void nd_select(Nd *a, Value idx, NdSel *sel)
{
    Value one[1], *comps;
    long ncomp, strides[ND_MAXD], s = 1, i;
    int k;
    sel->rnd = 0; sel->base = 0; sel->count = 1;
    for (k = 0; k < ND_MAXD; k++) sel->off[k] = NULL;
    for (k = a->nd - 1; k >= 0; k--) { strides[k] = s; s *= a->shape[k]; }
    if (idx.t == T_TUPLE) { comps = AS_LIST(idx)->a; ncomp = AS_LIST(idx)->n; }
    else { one[0] = idx; comps = one; ncomp = 1; }
    if (ncomp > a->nd)
        throw_new(cls_IndexError, "too many indices for array: array is %d-dimensional, but %ld "
                  "were indexed", a->nd, ncomp);
    if (ncomp == 1 && comps[0].t == T_ND && AS_ND(comps[0])->dt == ND_BOOL &&
        AS_ND(comps[0])->nd == a->nd && a->nd > 1) {
        Nd *m = AS_ND(comps[0]);
        long cnt = 0, *o;
        for (k = 0; k < a->nd; k++)
            if (m->shape[k] != a->shape[k])
                throw_new(cls_IndexError, "boolean index did not match indexed array");
        for (i = 0; i < m->size; i++) if (m->iv[i]) cnt++;
        o = (long *)xmalloc(sizeof(long) * (size_t)(cnt > 0 ? cnt : 1));
        cnt = 0;
        for (i = 0; i < m->size; i++) if (m->iv[i]) o[cnt++] = i;
        sel->rnd = 1; sel->rshape[0] = cnt; sel->off[0] = o; sel->count = cnt;
        return;
    }
    for (k = 0; k < a->nd; k++) {
        long n = a->shape[k];
        Value c = k < ncomp ? comps[k] : UNSET_V;
        long *o = NULL, cnt = 0;
        if (c.t == T_INT || c.t == T_BOOL) {
            sel->base += sel_index(c.u.i, n, k) * strides[k];
            continue;
        }
        if (c.t == T_UNSET) c = mkslice(NONE_V, NONE_V, NONE_V);
        if (c.t == T_SLICE) {
            long start, stop, step;
            List *sl = AS_LIST(c);
            slice_adjust(sl->a[0], sl->a[1], sl->a[2], n, &start, &stop, &step, &cnt);
            o = (long *)xmalloc(sizeof(long) * (size_t)(cnt > 0 ? cnt : 1));
            for (i = 0; i < cnt; i++) o[i] = (start + i * step) * strides[k];
        } else if (c.t == T_LIST || c.t == T_TUPLE || c.t == T_ND) {
            Nd *ix = c.t == T_ND ? AS_ND(c) : nd_from_value(c, -1);
            if (ix->nd != 1)
                throw_new(cls_IndexError, "only 1-d index arrays are supported");
            if (ix->dt == ND_BOOL) {
                if (ix->size != n)
                    throw_new(cls_IndexError, "boolean index did not match indexed array along "
                              "axis %d; size of axis is %ld but size of corresponding boolean "
                              "axis is %ld", k, n, ix->size);
                for (i = 0; i < n; i++) if (ix->iv[i]) cnt++;
                o = (long *)xmalloc(sizeof(long) * (size_t)(cnt > 0 ? cnt : 1));
                cnt = 0;
                for (i = 0; i < n; i++) if (ix->iv[i]) o[cnt++] = i * strides[k];
            } else if (ix->dt == ND_INT) {
                cnt = ix->size;
                o = (long *)xmalloc(sizeof(long) * (size_t)(cnt > 0 ? cnt : 1));
                for (i = 0; i < cnt; i++) o[i] = sel_index(ix->iv[i], n, k) * strides[k];
            } else
                throw_new(cls_IndexError,
                    "arrays used as indices must be of integer (or boolean) type");
        } else
            throw_new(cls_IndexError, "only integers, slices (`:`), integer or boolean arrays are "
                      "valid indices");
        sel->rshape[sel->rnd] = cnt;
        sel->off[sel->rnd] = o;
        sel->rnd++;
        sel->count *= cnt;
    }
}
static void sel_free(NdSel *sel)
{
    int k;
    for (k = 0; k < sel->rnd; k++) free(sel->off[k]);
}
static Value nd_row(Nd *a, long i)
{
    Nd *r = nd_new(a->dt, a->nd - 1, a->shape + 1);
    nd_cp(r, 0, a, i * r->size, r->size);
    return ndv(r);
}
static Value nd_item(Value o, long i)
{
    Nd *a = AS_ND(o);
    if (a->nd == 1) return nd_elem(a, i);
    return nd_row(a, i);
}
static Value nd_subscr(Value o, Value idx)
{
    Nd *a = AS_ND(o), *r;
    NdSel sel;
    long ix[ND_MAXD], n, pos = 0, off;
    int k;
    if (idx.t == T_INT && a->nd >= 1) {
        long i = sel_index(idx.u.i, a->shape[0], 0);
        return nd_item(o, i);
    }
    if (a->nd == 0) throw_new(cls_IndexError, "too many indices for array: array is 0-dimensional");
    nd_select(a, idx, &sel);
    r = nd_new(a->dt, sel.rnd, sel.rshape);
    n = sel.count;
    for (k = 0; k < ND_MAXD; k++) ix[k] = 0;
    while (pos < n) {
        off = sel.base;
        for (k = 0; k < sel.rnd; k++) off += sel.off[k][ix[k]];
        if (a->dt == ND_COMPLEX) {
            r->dv[2 * pos] = a->dv[2 * off]; r->dv[2 * pos + 1] = a->dv[2 * off + 1];
        } else if (a->dt == ND_FLOAT) r->dv[pos] = a->dv[off]; else r->iv[pos] = a->iv[off];
        pos++;
        for (k = sel.rnd - 1; k >= 0; k--) {
            if (++ix[k] < sel.rshape[k]) break;
            ix[k] = 0;
        }
    }
    sel_free(&sel);
    return nd_ret(r);
}
static void nd_setitem(Value o, Value idx, Value v)
{
    Nd *a = AS_ND(o), *s;
    NdTmp tv;
    NdSel sel;
    long ix[ND_MAXD], sst[ND_MAXD], n, pos = 0, off, so;
    int k, extra;
    if (a->nd == 0) throw_new(cls_IndexError, "too many indices for array: array is 0-dimensional");
    s = nd_coerce_c(v, &tv);
    if (idx.t == T_INT && a->nd == 1 && s->nd == 0) {
        long i = sel_index(idx.u.i, a->shape[0], 0);
        nd_cp(a, i, s, 0, 1);
        return;
    }
    nd_select(a, idx, &sel);
    n = sel.count;
    if (s->nd > sel.rnd) {
        char s1[160], s2[160];
        shape_str(s->nd, s->shape, s1);
        shape_str(sel.rnd, sel.rshape, s2);
        throw_new(cls_ValueError, "could not broadcast input array from shape %s into shape %s", s1,
            s2);
    }
    extra = sel.rnd - s->nd;
    {
        long st = 1;
        for (k = sel.rnd - 1; k >= 0; k--) {
            int sk = k - extra;
            if (sk < 0) sst[k] = 0;
            else {
                if (s->shape[sk] != 1 && s->shape[sk] != sel.rshape[k]) {
                    char s1[160], s2[160];
                    shape_str(s->nd, s->shape, s1);
                    shape_str(sel.rnd, sel.rshape, s2);
                    sel_free(&sel);
                    throw_new(cls_ValueError, "could not broadcast input array from shape %s into "
                              "shape %s", s1, s2);
                }
                sst[k] = s->shape[sk] == 1 ? 0 : st;
                st *= s->shape[sk];
            }
        }
    }
    for (k = 0; k < ND_MAXD; k++) ix[k] = 0;
    while (pos < n) {
        off = sel.base; so = 0;
        for (k = 0; k < sel.rnd; k++) { off += sel.off[k][ix[k]]; so += ix[k] * sst[k]; }
        nd_cp(a, off, s, so, 1);
        pos++;
        for (k = sel.rnd - 1; k >= 0; k--) {
            if (++ix[k] < sel.rshape[k]) break;
            ix[k] = 0;
        }
    }
    sel_free(&sel);
}
static void nd_assign_all(Value dst, Value src)
{
    if (AS_ND(dst)->nd == 0) { nd_cp(AS_ND(dst), 0, AS_ND(src), 0, 1); return; }
    nd_setitem(dst, mkslice(NONE_V, NONE_V, NONE_V), src);
}
static long nd_len(Value o)
{
    Nd *a = AS_ND(o);
    if (a->nd == 0) throw_new(cls_TypeError, "len() of unsized object");
    return a->shape[0];
}
static int nd_truthy(Value o)
{
    Nd *a = AS_ND(o);
    if (a->size == 1) {
        if (a->dt == ND_COMPLEX) return a->dv[0] != 0 || a->dv[1] != 0;
        return a->dt == ND_FLOAT ? a->dv[0] != 0 : a->iv[0] != 0;
    }
    if (a->size == 0) return 0;
    throw_new(cls_ValueError, "The truth value of an array with more than one element is "
              "ambiguous. Use a.any() or a.all()");
    return 0;
}

/* ---- printing (numpy's array2string layout) --------------------------- */
#define NDW 128
typedef struct NdR {
    Nd *a;
    char *strs;          /* formatted elements, NDW bytes each, in display order */
    long next;
    int summ;
    int comma;           /* separator ", " (repr) instead of " " */
    long *ids;           /* flat indices of displayed elements, in display order */
    long nids;
} NdR;

static void nd_collect(NdR *r, int axis, long off)
{
    Nd *a = r->a;
    long n = a->shape[axis], i, stride = 1;
    int k;
    for (k = axis + 1; k < a->nd; k++) stride *= a->shape[k];
    for (i = 0; i < n; i++) {
        if (r->summ && n > 6 && i >= 3 && i < n - 3) { i = n - 4; continue; }
        if (axis == a->nd - 1) r->ids[r->nids++] = off + i;
        else nd_collect(r, axis + 1, off + i * stride);
    }
}
/* shortest repr with at most 8 fractional digits: digits after the point in *fr */
static void fl_fixed(double d, char *ip, char *fr)
{
    char t[400];
    int p;
    char *dot;
    for (p = 0; p <= 8; p++) {
        sprintf(t, "%.*f", p, d);
        if (strtod(t, NULL) == d) break;
    }
    if (p > 8) {
        int n;
        sprintf(t, "%.8f", d);
        n = (int)strlen(t);
        while (t[n - 1] == '0') t[--n] = 0;
    }
    dot = strchr(t, '.');
    if (dot) { *dot = 0; strcpy(ip, t); strcpy(fr, dot + 1); }
    else { strcpy(ip, t); fr[0] = 0; }
}
/* scientific: mantissa digits after the point, exponent value */
static int fl_sci_digits(double d)
{
    char t[64];
    int p;
    for (p = 0; p <= 8; p++) {
        sprintf(t, "%.*e", p, d);
        if (strtod(t, NULL) == d) return p;
    }
    return 8;
}
/* format n doubles like numpy's FloatingFormat (sign '-' or, with plus, '+') into out (NDW each) */
static void fmt_floats(const double *vv, long n, int plus, char *out)
{
    double mx = 0, mn = 0;
    long i;
    char *sp = out;
    int have = 0, expf = 0, padl = 0, padr = 0, prec = 0, expw = 2, nonfin = 0, neginf = plus;
    for (i = 0; i < n; i++) {
        double d = vv[i], ad = fabs(d);
        if (d != d || ad - ad != 0 || d == 0) continue;
        if (!have) { mx = mn = ad; have = 1; }
        else { if (ad > mx) mx = ad; if (ad < mn) mn = ad; }
    }
    if (have && (mx >= 1e8 || mn < 1e-4 || mx / mn > 1000.0)) expf = 1;
    for (i = 0; i < n; i++) {
        double d = vv[i];
        if (d != d || d - d != 0) { nonfin = 1; if (d < 0) neginf = 1; }
    }
    if (!expf) {
        for (i = 0; i < n; i++) {
            double d = vv[i];
            char ip[400], fr[400];
            int il;
            if (d != d || d - d != 0) continue;
            fl_fixed(d, ip, fr);
            il = (int)strlen(ip);
            if (plus && ip[0] != '-') il++;
            if (il > padl) padl = il;
            if ((int)strlen(fr) > padr) padr = (int)strlen(fr);
        }
        if (nonfin) {
            int offs = padr + 1;
            if (3 - offs > padl) padl = 3 - offs;
            if (3 + neginf - offs > padl) padl = 3 + neginf - offs;
        }
        for (i = 0; i < n; i++, sp += NDW) {
            double d = vv[i];
            char ip[400], fr[400], sg[420];
            if (d != d) sprintf(sp, "%*s", padl + padr + 1, plus ? "+nan" : "nan");
            else if (d - d != 0)
                sprintf(sp, "%*s", padl + padr + 1, d > 0 ? (plus ? "+inf" : "inf") : "-inf");
            else {
                fl_fixed(d, ip, fr);
                if (plus && ip[0] != '-') { sg[0] = '+'; strcpy(sg + 1, ip); }
                else strcpy(sg, ip);
                sprintf(sp, "%*s.%s%*s", padl, sg, fr, padr - (int)strlen(fr), "");
            }
        }
    } else {
        for (i = 0; i < n; i++) {
            double d = vv[i];
            char t[64];
            int p, e;
            if (d != d || d - d != 0) continue;
            p = fl_sci_digits(d);
            if (p > prec) prec = p;
            sprintf(t, "%.0e", d);
            e = atoi(strchr(t, 'e') + 1);
            if (e < 0) e = -e;
            if (e >= 100 && expw < 3) expw = 3;
            if (t[0] == '-' || plus) { if (padl < 2) padl = 2; }
            else if (padl < 1) padl = 1;
        }
        if (nonfin) {
            int offs = expw + 2 + prec + 1;
            if (3 - offs > padl) padl = 3 - offs;
            if (3 + neginf - offs > padl) padl = 3 + neginf - offs;
        }
        for (i = 0; i < n; i++, sp += NDW) {
            double d = vv[i];
            char t[64], m[70];
            char *ep;
            int e, w;
            if (d != d) {
                sprintf(sp, "%*s", padl + prec + 2 + expw + 2 - 1, plus ? "+nan" : "nan");
                continue;
            }
            if (d - d != 0) {
                sprintf(sp, "%*s", padl + prec + 2 + expw + 2 - 1,
                        d > 0 ? (plus ? "+inf" : "inf") : "-inf");
                continue;
            }
            sprintf(t, "%.*e", prec, d);
            ep = strchr(t, 'e');
            e = atoi(ep + 1);
            *ep = 0;
            if (plus && t[0] != '-') { m[0] = '+'; strcpy(m + 1, t); }
            else strcpy(m, t);
            if (prec == 0) strcat(m, ".");
            w = padl - (int)(strchr(m, '.') - m);
            sprintf(sp, "%*s%se%c%0*d", w > 0 ? w : 0, "", m, e < 0 ? '-' : '+', expw,
                    e < 0 ? -e : e);
        }
    }
}
static void nd_format_elems(NdR *r)
{
    Nd *a = r->a;
    long i;
    char *sp = r->strs;
    if (a->dt == ND_BOOL) {
        int w = a->nd == 0 ? 4 : 5;
        for (i = 0; i < r->nids; i++) if (!a->iv[r->ids[i]]) w = 5;
        for (i = 0; i < r->nids; i++, sp += NDW)
            sprintf(sp, "%*s", w, a->iv[r->ids[i]] ? "True" : "False");
    } else if (a->dt == ND_INT) {
        int w = 1, l;
        for (i = 0; i < r->nids; i++) {
            sprintf(sp, "%ld", a->iv[r->ids[i]]);
            l = (int)strlen(sp);
            if (l > w) w = l;
        }
        for (i = 0; i < r->nids; i++, sp += NDW) {
            char t[NDW];
            sprintf(t, "%ld", a->iv[r->ids[i]]);
            sprintf(sp, "%*s", w, t);
        }
    } else if (a->dt == ND_COMPLEX) {
        double *vv = (double *)xmalloc(sizeof(double) * (size_t)(r->nids + 1));
        char *ri = (char *)xmalloc((size_t)NDW * (size_t)(r->nids + 1));
        char *ii = (char *)xmalloc((size_t)NDW * (size_t)(r->nids + 1));
        for (i = 0; i < r->nids; i++) vv[i] = a->dv[2 * r->ids[i]];
        fmt_floats(vv, r->nids, 0, ri);
        for (i = 0; i < r->nids; i++) vv[i] = a->dv[2 * r->ids[i] + 1];
        fmt_floats(vv, r->nids, 1, ii);
        for (i = 0; i < r->nids; i++, sp += NDW) {
            char *is = ii + NDW * i;
            long sl = (long)strlen(is);
            while (sl > 0 && is[sl - 1] == ' ') sl--;
            sprintf(sp, "%s%.*sj%s", ri + NDW * i, (int)sl, is, is + sl);
        }
        free(vv); free(ri); free(ii);
    } else {
        double *vv = (double *)xmalloc(sizeof(double) * (size_t)(r->nids + 1));
        for (i = 0; i < r->nids; i++) vv[i] = a->dv[r->ids[i]];
        fmt_floats(vv, r->nids, 0, sp);
        free(vv);
    }
}
static void nd_extend(Buf *s, Buf *line, const char *word, int elemw, int hang)
{
    int wl = (int)strlen(word);
    int wrap = line->n + wl > elemw;
    if (line->n <= hang) wrap = 0;
    if (wrap) {
        while (line->n > 0 && line->s[line->n - 1] == ' ') line->n--;
        line->s[line->n] = 0;
        buf_addn(s, line->s, line->n);
        buf_addc(s, '\n');
        line->n = 0; line->s[0] = 0;
        {
            int i;
            for (i = 0; i < hang; i++) buf_addc(line, ' ');
        }
    }
    buf_add(line, word);
}
static void nd_linesep(NdR *r, Buf *s, int axes_left)
{
    int i;
    if (r->comma) buf_addc(s, ',');
    for (i = 0; i < axes_left - 1; i++) buf_addc(s, '\n');
}
static void nd_rec(NdR *r, int axis, int hang, int curw, Buf *out)
{
    Nd *a = r->a;
    int axes_left = a->nd - axis, i;
    long n = a->shape[axis], j, lead, trail;
    int summ = r->summ && n > 6;
    Buf s;
    buf_init(&s);
    if (summ) { lead = 3; trail = 3; } else { lead = 0; trail = n; }
    if (axes_left == 1) {
        int elemw = curw - 1;
        Buf line;
        buf_init(&line);
        for (i = 0; i < hang; i++) buf_addc(&line, ' ');
        for (j = 0; j < lead; j++) {
            nd_extend(&s, &line, r->strs + NDW * (r->next++), elemw, hang);
            buf_add(&line, r->comma ? ", " : " ");
        }
        if (summ) { nd_extend(&s, &line, "...", elemw, hang);
            buf_add(&line, r->comma ? ", " : " ");
        }
        for (j = trail; j > 1; j--) {
            nd_extend(&s, &line, r->strs + NDW * (r->next++), elemw, hang);
            buf_add(&line, r->comma ? ", " : " ");
        }
        nd_extend(&s, &line, r->strs + NDW * (r->next++), elemw, hang);
        buf_addn(&s, line.s, line.n);
        buf_free(&line);
    } else {
        for (j = 0; j < lead; j++) {
            for (i = 0; i < hang; i++) buf_addc(&s, ' ');
            nd_rec(r, axis + 1, hang + 1, curw - 1, &s);
            nd_linesep(r, &s, axes_left);
        }
        if (summ) {
            for (i = 0; i < hang; i++) buf_addc(&s, ' ');
            buf_add(&s, "...");
            nd_linesep(r, &s, axes_left);
        }
        for (j = trail; j > 1; j--) {
            for (i = 0; i < hang; i++) buf_addc(&s, ' ');
            nd_rec(r, axis + 1, hang + 1, curw - 1, &s);
            nd_linesep(r, &s, axes_left);
        }
        for (i = 0; i < hang; i++) buf_addc(&s, ' ');
        nd_rec(r, axis + 1, hang + 1, curw - 1, &s);
    }
    buf_addc(out, '[');
    buf_addn(out, s.s + hang, s.n - hang);
    buf_addc(out, ']');
    buf_free(&s);
}
static void nd_format(Value o, Buf *b, int repr)
{
    Nd *a = AS_ND(o);
    NdR r;
    if (repr) buf_add(b, "array(");
    if (a->nd == 0) {
        r.a = a; r.nids = 1;
        r.ids = (long *)xmalloc(sizeof(long));
        r.ids[0] = 0;
        r.strs = (char *)xmalloc(NDW);
        nd_format_elems(&r);
        {
            char *p = r.strs;
            while (*p == ' ') p++;
            buf_add(b, p);
        }
        free(r.ids); free(r.strs);
    } else if (a->size == 0) {
        buf_add(b, "[]");
        if (repr) {
            buf_add(b, ", dtype=");
            buf_add(b, dt_name(a->dt));
        }
    } else {
        Buf body;
        r.a = a; r.summ = a->size > 1000; r.next = 0; r.nids = 0; r.comma = repr;
        {
            long cap = 1;
            int k;
            for (k = 0; k < a->nd; k++)
                cap *= (r.summ && a->shape[k] > 6) ? 6 : a->shape[k];
            r.ids = (long *)xmalloc(sizeof(long) * (size_t)cap + 8);
        }
        nd_collect(&r, 0, 0);
        r.strs = (char *)xmalloc((size_t)NDW * (size_t)(r.nids > 0 ? r.nids : 1));
        nd_format_elems(&r);
        r.next = 0;
        buf_init(&body);
        nd_rec(&r, 0, repr ? 7 : 1, repr ? 74 : 75, &body);
        buf_addn(b, body.s, body.n);
        buf_free(&body);
        free(r.ids); free(r.strs);
    }
    if (repr) buf_addc(b, ')');
}

/* ---- creation, shape manipulation ------------------------------------- */
static int shape_arg(Value v, long *shape)
{
    int nd = 0, k;
    if (v.t == T_INT || v.t == T_BOOL) { shape[0] = v.u.i; nd = 1; }
    else if (v.t == T_TUPLE || v.t == T_LIST) {
        List *l = AS_LIST(v);
        long i;
        if (l->n > ND_MAXD)
            throw_new(cls_ValueError, "maximum supported dimension for an ndarray is %d", ND_MAXD);
        for (i = 0; i < l->n; i++) shape[i] = need_long(l->a[i], "shape");
        nd = (int)l->n;
    } else
        throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
            type_name(v));
    for (k = 0; k < nd; k++)
        if (shape[k] < 0) throw_new(cls_ValueError, "negative dimensions are not allowed");
    return nd;
}
static int dtype_kw(Args *x, int dflt)
{
    Value d = kwget(x, "dtype");
    if (d.t == T_UNSET || d.t == T_NONE) return dflt;
    return parse_dtype(d);
}
static void nd_fillv(Nd *r, Value v)
{
    long i;
    if (r->dt == ND_COMPLEX) {
        Cx z;
        to_cx(v, &z);
        for (i = 0; i < r->size; i++) { r->dv[2 * i] = z.r; r->dv[2 * i + 1] = z.i; }
    } else if (r->dt == ND_FLOAT) {
        double d = to_dbl(v);
        for (i = 0; i < r->size; i++) r->dv[i] = d;
    }
    else {
        long n = v.t == T_FLOAT ? (long)v.u.d : v.u.i;
        if (r->dt == ND_BOOL) n = v.t == T_FLOAT ? v.u.d != 0 : n != 0;
        for (i = 0; i < r->size; i++) r->iv[i] = n;
    }
}
static Value nd_filled(Args *x, Value fill, int dflt)
{
    long shape[ND_MAXD];
    int nd, dt;
    Nd *r;
    if (x->n < 1) throw_new(cls_TypeError, "missing shape argument");
    nd = shape_arg(A(0), shape);
    dt = dtype_kw(x, dflt);
    r = nd_new(dt, nd, shape);
    nd_fillv(r, fill);
    return ndv(r);
}
BI(np_zeros) { return nd_filled(x, mkint(0), ND_FLOAT); }
BI(np_ones) { return nd_filled(x, mkint(1), ND_FLOAT); }
BI(np_full)
{
    long shape[ND_MAXD];
    int nd, dflt;
    Nd *r;
    Value fv;
    if (x->n < 2) throw_new(cls_TypeError, "full() missing required arguments");
    fv = A(1);
    dflt = py_dt(fv) < 0 ? ND_FLOAT : py_dt(fv);
    nd = shape_arg(A(0), shape);
    r = nd_new(dtype_kw(x, dflt), nd, shape);
    nd_fillv(r, fv);
    return ndv(r);
}
static Value like_fill(Args *x, Value fill)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t), *r;
    r = nd_new(dtype_kw(x, a->dt), a->nd, a->shape);
    nd_fillv(r, fill);
    return ndv(r);
}
BI(np_zeros_like) { return like_fill(x, mkint(0)); }
BI(np_ones_like) { return like_fill(x, mkint(1)); }
BI(np_full_like)
{
    if (x->n < 2) throw_new(cls_TypeError, "full_like() missing required arguments");
    return like_fill(x, A(1));
}
BI(np_eye)
{
    long n, m, kk = 0, i, sh[2];
    Value mv = x->n > 1 ? A(1) : kwget(x, "M"), kv = x->n > 2 ? A(2) : kwget(x, "k");
    Nd *r;
    if (x->n < 1) throw_new(cls_TypeError, "eye() missing required argument 'N'");
    n = need_long(A(0), "N");
    m = (mv.t == T_UNSET || mv.t == T_NONE) ? n : need_long(mv, "M");
    if (kv.t != T_UNSET && kv.t != T_NONE) kk = need_long(kv, "k");
    sh[0] = n; sh[1] = m;
    r = nd_new(dtype_kw(x, ND_FLOAT), 2, sh);
    nd_fillv(r, mkint(0));
    for (i = 0; i < n; i++)
        if (i + kk >= 0 && i + kk < m) {
            if (r->dt == ND_COMPLEX) r->dv[2 * (i * m + i + kk)] = 1.0;
            else if (r->dt == ND_FLOAT) r->dv[i * m + i + kk] = 1.0; else r->iv[i * m + i + kk] = 1;
        }
    return ndv(r);
}
BI(np_arange)
{
    Value sv, ev, tv;
    int allint = 1, i, dt;
    double start = 0, stop, step = 1;
    long n, k;
    Nd *r;
    if (x->n < 1 || x->n > 3)
        throw_new(cls_TypeError, "arange() takes 1 to 3 positional arguments");
    for (i = 0; i < x->n; i++) {
        if (py_dt(A(i)) < 0) throw_new(cls_TypeError, "arange() arguments must be numbers");
        if (A(i).t == T_FLOAT) allint = 0;
    }
    if (x->n == 1) { sv = mkint(0); ev = A(0); tv = mkint(1); }
    else { sv = A(0); ev = A(1); tv = x->n > 2 ? A(2) : mkint(1); }
    start = to_dbl(sv); stop = to_dbl(ev); step = to_dbl(tv);
    if (step == 0) throw_new(cls_ZeroDivisionError, "Maximum allowed size exceeded");
    n = (long)ceil((stop - start) / step);
    if (n < 0) n = 0;
    dt = dtype_kw(x, allint ? ND_INT : ND_FLOAT);
    r = nd_new(dt, 1, &n);
    if (allint) {
        long s0 = sv.u.i, st = tv.u.i;
        for (k = 0; k < n; k++) {
            long v = s0 + k * st;
            if (dt == ND_FLOAT) r->dv[k] = (double)v; else r->iv[k] = dt == ND_BOOL ? v != 0 : v;
        }
    } else {
        double delta = (start + step) - start;
        for (k = 0; k < n; k++) {
            double v = start + (double)k * delta;
            if (dt == ND_FLOAT) r->dv[k] = v; else r->iv[k] = dt == ND_BOOL ? v != 0 : (long)v;
        }
    }
    return ndv(r);
}
BI(np_linspace)
{
    Value nv = x->n > 2 ? A(2) : kwget(x, "num"), ev = x->n > 3 ? A(3) : kwget(x, "endpoint");
    double start, stop, step, delta;
    long num = 50, i, div;
    int endpoint = 1;
    Nd *r;
    if (x->n < 2) throw_new(cls_TypeError, "linspace() missing required arguments");
    start = to_dbl(A(0)); stop = to_dbl(A(1));
    if (nv.t != T_UNSET) num = need_long(nv, "num");
    if (ev.t != T_UNSET) endpoint = py_truthy(ev);
    if (num < 0) throw_new(cls_ValueError, "Number of samples, %ld, must be non-negative.", num);
    r = nd_new1(ND_FLOAT, num);
    div = endpoint ? num - 1 : num;
    delta = stop - start;
    if (div > 0) {
        step = delta / (double)div;
        for (i = 0; i < num; i++) r->dv[i] = (double)i * step + start;
    } else {
        for (i = 0; i < num; i++) r->dv[i] = start;
    }
    if (endpoint && num > 1) r->dv[num - 1] = stop;
    if (dtype_kw(x, ND_FLOAT) != ND_FLOAT) return ndv(nd_astype(r, dtype_kw(x, ND_FLOAT)));
    return ndv(r);
}
BI(np_array)
{
    Value d = x->n > 1 ? A(1) : kwget(x, "dtype");
    int dt = -1;
    if (x->n < 1) throw_new(cls_TypeError, "array() missing required argument 'object'");
    if (d.t != T_UNSET && d.t != T_NONE) dt = parse_dtype(d);
    return ndv(nd_from_value(A(0), dt));
}
BI(np_asarray)
{
    Value d = x->n > 1 ? A(1) : kwget(x, "dtype");
    if (x->n < 1) throw_new(cls_TypeError, "asarray() missing required argument 'a'");
    if (A(0).t == T_ND && (d.t == T_UNSET || d.t == T_NONE)) return A(0);
    return np_array(x);
}
BI(nd_copy)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    return ndv(nd_astype(a, a->dt));
}
BI(nd_astype_m)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    Value d = x->n > 1 ? A(1) : kwget(x, "dtype");
    if (d.t == T_UNSET) throw_new(cls_TypeError, "astype() missing required argument 'dtype'");
    return ndv(nd_astype(a, parse_dtype(d)));
}
static Value nd_tolist_rec(Nd *a, int axis, long off)
{
    long n, i, stride = 1;
    int k;
    List *l;
    Value lv;
    if (a->nd == 0) return nd_elem(a, 0);
    n = a->shape[axis];
    for (k = axis + 1; k < a->nd; k++) stride *= a->shape[k];
    l = list_new(n);
    lv = mklist(l);
    for (i = 0; i < n; i++) {
        if (axis == a->nd - 1) list_push(l, nd_elem(a, off + i));
        else list_push(l, nd_tolist_rec(a, axis + 1, off + i * stride));
    }
    return lv;
}
BI(nd_tolist)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    return nd_tolist_rec(a, 0, 0);
}
BI(nd_item_m)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    if (a->size != 1)
        throw_new(cls_ValueError, "can only convert an array of size 1 to a Python scalar");
    return nd_elem(a, 0);
}
BI(nd_reshape)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    long shape[ND_MAXD], known = 1;
    int nd, k, unk = -1;
    if (x->n == 2 && (A(1).t == T_TUPLE || A(1).t == T_LIST)) {
        List *l = AS_LIST(A(1));
        if (l->n > ND_MAXD) throw_new(cls_ValueError, "too many dimensions");
        for (k = 0; k < l->n; k++) shape[k] = need_long(l->a[k], "shape");
        nd = (int)l->n;
    } else {
        if (x->n - 1 > ND_MAXD) throw_new(cls_ValueError, "too many dimensions");
        for (k = 1; k < x->n; k++) shape[k - 1] = need_long(A(k), "shape");
        nd = x->n - 1;
    }
    for (k = 0; k < nd; k++) {
        if (shape[k] == -1) {
            if (unk >= 0) throw_new(cls_ValueError, "can only specify one unknown dimension");
            unk = k;
        } else known *= shape[k];
    }
    if (unk >= 0) {
        if (known == 0 || a->size % known != 0)
            throw_new(cls_ValueError, "cannot reshape array of size %ld into shape %ld", a->size,
                known);
        shape[unk] = a->size / known;
    } else if (known != a->size) {
        char s1[160];
        shape_str(nd, shape, s1);
        throw_new(cls_ValueError, "cannot reshape array of size %ld into shape %s", a->size, s1);
    }
    return ndv(nd_reshaped(a, nd, shape));
}
BI(nd_flatten)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    long n = a->size;
    return ndv(nd_reshaped(a, 1, &n));
}
static Nd *nd_transposed(Nd *a)
{
    long rs[ND_MAXD], sst[ND_MAXD], idx[ND_MAXD], st = 1, i, off = 0;
    int k;
    Nd *r;
    for (k = 0; k < a->nd; k++) rs[k] = a->shape[a->nd - 1 - k];
    for (k = a->nd - 1; k >= 0; k--) { sst[a->nd - 1 - k] = st; st *= a->shape[k]; }
    r = nd_new(a->dt, a->nd, rs);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        nd_cp(r, i, a, off, 1);
        for (k = a->nd - 1; k >= 0; k--) {
            idx[k]++; off += sst[k];
            if (idx[k] < rs[k]) break;
            off -= sst[k] * rs[k]; idx[k] = 0;
        }
    }
    return r;
}
BI(nd_transpose)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    return ndv(nd_transposed(a));
}
static Nd *nd_expand(const Nd *a, int rnd, const long *rs, int dt)
{
    Nd *r = nd_new(dt, rnd, rs);
    long st[ND_MAXD], idx[ND_MAXD], i, off = 0;
    int k;
    nd_bstrides(a, rnd, st);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        nd_cp(r, i, a, off, 1);
        for (k = rnd - 1; k >= 0; k--) {
            idx[k]++; off += st[k];
            if (idx[k] < rs[k]) break;
            off -= st[k] * rs[k]; idx[k] = 0;
        }
    }
    return r;
}

/* ---- joining, selection, sorting -------------------------------------- */
static Nd *nd_concat(Nd **arr, int cnt, int axis)
{
    long rs[ND_MAXD], outer = 1, inner = 1, o, off = 0, tot = 0;
    int i, k, dt = ND_BOOL, nd = arr[0]->nd;
    Nd *r;
    for (i = 0; i < cnt; i++) {
        if (arr[i]->nd != nd || nd == 0)
            throw_new(cls_ValueError, "all the input array dimensions except for the concatenation "
                      "axis must match exactly");
        for (k = 0; k < nd; k++)
            if (k != axis && arr[i]->shape[k] != arr[0]->shape[k])
                throw_new(cls_ValueError, "all the input array dimensions except for the "
                          "concatenation axis must match exactly, but along dimension %d, the "
                          "array at index 0 has size %ld and the array at index %d has size %ld",
                          k, arr[0]->shape[k], i, arr[i]->shape[k]);
        if (arr[i]->dt > dt) dt = arr[i]->dt;
        tot += arr[i]->shape[axis];
    }
    for (k = 0; k < nd; k++) rs[k] = arr[0]->shape[k];
    rs[axis] = tot;
    for (k = 0; k < axis; k++) outer *= rs[k];
    for (k = axis + 1; k < nd; k++) inner *= rs[k];
    r = nd_new(dt, nd, rs);
    for (o = 0; o < outer; o++)
        for (i = 0; i < cnt; i++) {
            long blk = arr[i]->shape[axis] * inner;
            nd_cp(r, off, arr[i], o * blk, blk);
            off += blk;
        }
    return r;
}
static int seq_arrays(Value v, Nd ***out)
{
    long n, i;
    Nd **arr;
    if (v.t != T_LIST && v.t != T_TUPLE && v.t != T_ND)
        throw_new(cls_TypeError, "expected a sequence of arrays");
    if (v.t == T_ND) n = nd_len(v); else n = AS_LIST(v)->n;
    if (n == 0) throw_new(cls_ValueError, "need at least one array to concatenate");
    arr = (Nd **)xmalloc(sizeof(Nd *) * (size_t)n);
    for (i = 0; i < n; i++) {
        Value it = v.t == T_ND ? nd_item(v, i) : AS_LIST(v)->a[i];
        arr[i] = it.t == T_ND ? AS_ND(it) : nd_from_value(it, -1);
    }
    *out = arr;
    return (int)n;
}
BI(np_concatenate)
{
    Nd **arr;
    int n, axis = 0;
    Value av = x->n > 1 ? A(1) : kwget(x, "axis"), r;
    if (x->n < 1) throw_new(cls_TypeError, "concatenate() missing required argument");
    n = seq_arrays(A(0), &arr);
    if (av.t != T_UNSET && av.t != T_NONE) axis = axis_norm(need_long(av, "axis"), arr[0]->nd);
    r = ndv(nd_concat(arr, n, axis));
    free(arr);
    return r;
}
static void nd_atleast2(Nd **arr, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        long sh[2];
        if (arr[i]->nd == 1) { sh[0] = 1;
            sh[1] = arr[i]->shape[0];
            arr[i] = nd_reshaped(arr[i], 2, sh);
        }
        else if (arr[i]->nd == 0) { sh[0] = 1; sh[1] = 1; arr[i] = nd_reshaped(arr[i], 2, sh); }
    }
}
BI(np_vstack)
{
    Nd **arr;
    int n;
    Value r;
    if (x->n < 1) throw_new(cls_TypeError, "vstack() missing required argument");
    n = seq_arrays(A(0), &arr);
    nd_atleast2(arr, n);
    r = ndv(nd_concat(arr, n, 0));
    free(arr);
    return r;
}
BI(np_hstack)
{
    Nd **arr;
    int n, i, one = 1;
    Value r;
    if (x->n < 1) throw_new(cls_TypeError, "hstack() missing required argument");
    n = seq_arrays(A(0), &arr);
    for (i = 0; i < n; i++) if (arr[i]->nd != 1) one = 0;
    if (!one) {
        for (i = 0; i < n; i++)
            if (arr[i]->nd == 0) { long sh = 1; arr[i] = nd_reshaped(arr[i], 1, &sh); }
    }
    r = ndv(nd_concat(arr, n, one || arr[0]->nd == 1 ? 0 : 1));
    free(arr);
    return r;
}
BI(np_stack)
{
    Nd **arr;
    int n, i, axis = 0;
    Value av = x->n > 1 ? A(1) : kwget(x, "axis"), r;
    if (x->n < 1) throw_new(cls_TypeError, "stack() missing required argument");
    n = seq_arrays(A(0), &arr);
    if (av.t != T_UNSET && av.t != T_NONE) axis = axis_norm(need_long(av, "axis"), arr[0]->nd + 1);
    for (i = 1; i < n; i++)
        if (arr[i]->nd != arr[0]->nd
            || memcmp(arr[i]->shape, arr[0]->shape, sizeof(long) * ND_MAXD))
            throw_new(cls_ValueError, "all input arrays must have the same shape");
    for (i = 0; i < n; i++) {
        long sh[ND_MAXD];
        int k, m = 0;
        for (k = 0; k <= arr[i]->nd; k++) sh[k] = k == axis ? 1 : arr[i]->shape[m++];
        arr[i] = nd_reshaped(arr[i], arr[i]->nd + 1, sh);
    }
    r = ndv(nd_concat(arr, n, axis));
    free(arr);
    return r;
}
static Value np_nonzero(Args *x);
BI(np_where)
{
    NdTmp tc, tx, ty, tp;
    Nd *c, *xx, *yy, *r;
    long rs1[ND_MAXD], rs[ND_MAXD], i;
    int r1, rnd, dt;
    if (x->n == 1) return np_nonzero(x);
    if (x->n != 3) throw_new(cls_TypeError, "where() requires 3 arguments");
    c = nd_coerce(A(0), &tc); xx = nd_coerce(A(1), &tx); yy = nd_coerce(A(2), &ty);
    r1 = nd_bshape(xx, yy, rs1);
    memset(&tp, 0, sizeof(tp));
    tp.nd.nd = r1;
    for (i = 0; i < r1; i++) tp.nd.shape[i] = rs1[i];
    rnd = nd_bshape(c, &tp.nd, rs);
    dt = xx->dt > yy->dt ? xx->dt : yy->dt;
    c = nd_expand(c, rnd, rs, c->dt);
    xx = nd_expand(xx, rnd, rs, dt);
    yy = nd_expand(yy, rnd, rs, dt);
    r = nd_new(dt, rnd, rs);
    for (i = 0; i < r->size; i++) {
        int t = c->dt == ND_FLOAT ? c->dv[i] != 0 : c->iv[i] != 0;
        nd_cp(r, i, t ? xx : yy, i, 1);
    }
    return nd_ret(r);
}
BI(np_nonzero)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long cnt = 0, i, pos = 0, idx[ND_MAXD];
    int k;
    Nd *outs[ND_MAXD];
    Value tv[ND_MAXD];
    if (a->nd == 0) throw_new(cls_ValueError, "Calling nonzero on 0d arrays is not allowed");
    for (i = 0; i < a->size; i++) if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) cnt++;
    for (k = 0; k < a->nd; k++) outs[k] = nd_new1(ND_INT, cnt);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < a->size; i++) {
        if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) {
            for (k = 0; k < a->nd; k++) outs[k]->iv[pos] = idx[k];
            pos++;
        }
        for (k = a->nd - 1; k >= 0; k--) {
            if (++idx[k] < a->shape[k]) break;
            idx[k] = 0;
        }
    }
    for (k = 0; k < a->nd; k++) tv[k] = ndv(outs[k]);
    return mktuple_from(tv, a->nd);
}
static int cmp_long(const void *p, const void *q)
{
    long a = *(const long *)p, b = *(const long *)q;
    return a < b ? -1 : (a > b ? 1 : 0);
}
static int cmp_dbl(const void *p, const void *q)
{
    double a = *(const double *)p, b = *(const double *)q;
    if (a != a) return b != b ? 0 : 1;
    if (b != b) return -1;
    return a < b ? -1 : (a > b ? 1 : 0);
}
static void nd_sort_rows(Nd *r, long n)
{
    long o;
    if (n <= 0) return;
    for (o = 0; o + n <= r->size; o += n) {
        if (r->dt == ND_FLOAT) qsort(r->dv + o, (size_t)n, sizeof(double), cmp_dbl);
        else qsort(r->iv + o, (size_t)n, sizeof(long), cmp_long);
    }
}
BI(np_sort)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value av = x->n > 1 ? A(1) : kwget(x, "axis");
    if (av.t == T_NONE) {
        long n = a->size;
        r = nd_reshaped(a, 1, &n);
    } else {
        if (av.t != T_UNSET && a->nd > 0 && axis_norm(need_long(av, "axis"), a->nd) != a->nd - 1)
            throw_new(cls_NotImplementedError, "sort: only the last axis is supported");
        r = nd_astype(a, a->dt);
    }
    if (r->nd == 0) return nd_ret(r);
    nd_sort_rows(r, r->shape[r->nd - 1]);
    return ndv(r);
}
BI(nd_sort_m)
{
    Nd *a;
    if (x->n < 1 || A(0).t != T_ND) throw_new(cls_TypeError, "sort() requires an array");
    a = AS_ND(A(0));
    if (a->nd > 0) nd_sort_rows(a, a->shape[a->nd - 1]);
    return NONE_V;
}
static const double *srt_d;
static const long *srt_i;
static int cmp_arg(const void *p, const void *q)
{
    long a = *(const long *)p, b = *(const long *)q;
    int c;
    if (srt_d) c = cmp_dbl(srt_d + a, srt_d + b); else c = cmp_long(srt_i + a, srt_i + b);
    if (c) return c;
    return a < b ? -1 : (a > b ? 1 : 0);
}
BI(np_argsort)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long n, o, i, *tmp;
    if (a->nd == 0) throw_new(cls_ValueError, "argsort of a 0-d array");
    n = a->shape[a->nd - 1];
    r = nd_new(ND_INT, a->nd, a->shape);
    tmp = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    for (o = 0; n > 0 && o + n <= a->size; o += n) {
        for (i = 0; i < n; i++) tmp[i] = i;
        srt_d = a->dt == ND_FLOAT ? a->dv + o : NULL;
        srt_i = a->dt == ND_FLOAT ? NULL : a->iv + o;
        qsort(tmp, (size_t)n, sizeof(long), cmp_arg);
        for (i = 0; i < n; i++) r->iv[o + i] = tmp[i];
    }
    free(tmp);
    return ndv(r);
}
BI(np_unique)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *s, *r;
    long n = a->size, i, m = 0;
    s = nd_reshaped(a, 1, &n);
    nd_sort_rows(s, n);
    for (i = 0; i < n; i++)
        if (i == 0 || (s->dt == ND_FLOAT ? s->dv[i] != s->dv[i - 1] : s->iv[i] != s->iv[i - 1]))
            m++;
    r = nd_new1(s->dt, m);
    m = 0;
    for (i = 0; i < n; i++)
        if (i == 0 || (s->dt == ND_FLOAT ? s->dv[i] != s->dv[i - 1] : s->iv[i] != s->iv[i - 1])) {
            nd_cp(r, m, s, i, 1);
            m++;
        }
    return ndv(r);
}
BI(np_median)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *s;
    long n = a->size;
    if (n == 0) return mkfloat(HUGE_VAL - HUGE_VAL);
    s = nd_astype(a, ND_FLOAT);
    s = nd_reshaped(s, 1, &n);
    nd_sort_rows(s, n);
    if (n % 2) return mkfloat(s->dv[n / 2]);
    return mkfloat((s->dv[n / 2 - 1] + s->dv[n / 2]) / 2.0);
}

/* ---- linear algebra ---------------------------------------------------- */
static Value nd_matmul(Nd *a, Nd *b, int isdot)
{
    long m, kk, n, i, k, j, rs[2];
    int rnd = 0, fl;
    Nd *r;
    if (a->nd == 0 || b->nd == 0) {
        if (!isdot)
            throw_new(cls_ValueError, "matmul: Input operand does not have enough dimensions");
        return nd_binary(B_MUL, a, b);
    }
    if (a->nd > 2 || b->nd > 2)
        throw_new(cls_NotImplementedError, "matmul: only 1-d and 2-d arrays are supported");
    if (a->nd == 1) { m = 1; kk = a->shape[0]; } else { m = a->shape[0]; kk = a->shape[1]; }
    if (b->nd == 1) { n = 1; if (b->shape[0] != kk) goto mismatch; }
    else { n = b->shape[1]; if (b->shape[0] != kk) goto mismatch; }
    if (a->nd == 2) rs[rnd++] = m;
    if (b->nd == 2) rs[rnd++] = n;
    if (a->dt == ND_COMPLEX || b->dt == ND_COMPLEX) {
        a = nd_astype(a, ND_COMPLEX); b = nd_astype(b, ND_COMPLEX);
        r = nd_new(ND_COMPLEX, rnd, rs);
        for (i = 0; i < 2 * m * n; i++) r->dv[i] = 0.0;
        for (i = 0; i < m; i++)
            for (k = 0; k < kk; k++) {
                double ar = a->dv[2 * (i * kk + k)], ai = a->dv[2 * (i * kk + k) + 1];
                for (j = 0; j < n; j++) {
                    double br = b->dv[2 * (k * n + j)], bi = b->dv[2 * (k * n + j) + 1];
                    r->dv[2 * (i * n + j)] += ar * br - ai * bi;
                    r->dv[2 * (i * n + j) + 1] += ar * bi + ai * br;
                }
            }
        return nd_ret(r);
    }
    fl = a->dt == ND_FLOAT || b->dt == ND_FLOAT;
    if (fl) { a = nd_to_float(a); b = nd_to_float(b); }
    r = nd_new(fl ? ND_FLOAT : ND_INT, rnd, rs);
    if (fl) {
        for (i = 0; i < m * n; i++) r->dv[i] = 0.0;
        for (i = 0; i < m; i++)
            for (k = 0; k < kk; k++) {
                double aik = a->dv[i * kk + k];
                const double *bp = b->dv + k * n;
                double *cp = r->dv + i * n;
                for (j = 0; j < n; j++) cp[j] += aik * bp[j];
            }
    } else {
        for (i = 0; i < m * n; i++) r->iv[i] = 0;
        for (i = 0; i < m; i++)
            for (k = 0; k < kk; k++) {
                unsigned long aik = (unsigned long)a->iv[i * kk + k];
                const long *bp = b->iv + k * n;
                long *cp = r->iv + i * n;
                for (j = 0; j < n; j++) cp[j] = (long)
                    ((unsigned long)cp[j] + aik * (unsigned long)bp[j]);
            }
    }
    return nd_ret(r);
mismatch:
    {
        char s1[160], s2[160];
        shape_str(a->nd, a->shape, s1);
        shape_str(b->nd, b->shape, s2);
        throw_new(cls_ValueError, "shapes %s and %s not aligned: %ld (dim %d) != %ld (dim 0)", s1,
            s2,
                  kk, a->nd - 1, b->shape[0]);
    }
    return NONE_V;
}
BI(np_dot)
{
    NdTmp ta, tb;
    if (x->n != 2) throw_new(cls_TypeError, "dot() takes exactly 2 arguments");
    return nd_matmul(nd_coerce_c(A(0), &ta), nd_coerce_c(A(1), &tb), 1);
}
BI(np_matmul)
{
    NdTmp ta, tb;
    if (x->n != 2) throw_new(cls_TypeError, "matmul() takes exactly 2 arguments");
    return nd_matmul(nd_coerce_c(A(0), &ta), nd_coerce_c(A(1), &tb), 0);
}
BI(np_diag)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long i, n, sh[2];
    if (a->nd == 1) {
        n = a->shape[0]; sh[0] = n; sh[1] = n;
        r = nd_new(a->dt, 2, sh);
        for (i = 0; i < n * n; i++) { if (r->dt == ND_FLOAT) r->dv[i] = 0; else r->iv[i] = 0; }
        for (i = 0; i < n; i++) nd_cp(r, i * n + i, a, i, 1);
        return ndv(r);
    }
    if (a->nd != 2) throw_new(cls_ValueError, "Input must be 1- or 2-d.");
    n = a->shape[0] < a->shape[1] ? a->shape[0] : a->shape[1];
    r = nd_new1(a->dt, n);
    for (i = 0; i < n; i++) nd_cp(r, i, a, i * a->shape[1] + i, 1);
    return ndv(r);
}
BI(np_trace)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *d;
    Args y;
    Value dv;
    if (a->nd != 2) throw_new(cls_ValueError, "diag requires an array of at least two dimensions");
    y.n = 1; dv = ndv(a); y.a = &dv; y.nk = 0; y.kw = NULL;
    d = AS_ND(np_diag(&y));
    return nd_reduce(R_SUM, d, 0, 0, 0);
}

/* ---- rounding, comparison helpers, ufunc wrappers ---------------------- */
static double round_dec(double v, long dec)
{
    double f;
    if (dec >= 0) {
        f = pow(10.0, (double)dec);
        return nd_rint(v * f) / f;
    }
    f = pow(10.0, (double)-dec);
    return nd_rint(v / f) * f;
}
BI(np_round)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t), *r;
    Value dv = x->n > 1 ? A(1) : kwget(x, "decimals");
    long dec = 0, i;
    if (dv.t != T_UNSET && dv.t != T_NONE) dec = need_long(dv, "decimals");
    if (a->dt == ND_COMPLEX) {
        r = nd_new(ND_COMPLEX, a->nd, a->shape);
        for (i = 0; i < 2 * a->size; i++) r->dv[i] = round_dec(a->dv[i], dec);
        return nd_ret(r);
    }
    if (a->dt != ND_FLOAT) {
        if (dec >= 0) return nd_ret(nd_astype(a, a->dt));
        r = nd_astype(a, ND_FLOAT);
        for (i = 0; i < r->size; i++) r->dv[i] = round_dec(r->dv[i], dec);
        return ndv(nd_astype(r, ND_INT));
    }
    r = nd_new(ND_FLOAT, a->nd, a->shape);
    for (i = 0; i < a->size; i++) r->dv[i] = round_dec(a->dv[i], dec);
    return nd_ret(r);
}
/* float.round(ndigits) - numpy float64 scalars have this method */
BI(f_np_round)
{
    long dec = 0;
    if (x->n > 1 && A(1).t != T_NONE) dec = need_long(A(1), "decimals");
    return mkfloat(round_dec(A(0).u.d, dec));
}
BI(np_clip)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    Value lo = x->n > 1 ? A(1) : kwget(x, "a_min"), hi = x->n > 2 ? A(2) : kwget(x, "a_max");
    Value r = ndv(a);
    NdTmp tl, th;
    if (lo.t == T_UNSET) lo = kwget(x, "min");
    if (hi.t == T_UNSET) hi = kwget(x, "max");
    if (lo.t != T_UNSET && lo.t != T_NONE)
        r = nd_binary(OP_MAX, nd_coerce(r, &t), nd_coerce(lo, &tl));
    if (hi.t != T_UNSET && hi.t != T_NONE)
        r = nd_binary(OP_MIN, nd_coerce(r, &t), nd_coerce(hi, &th));
    if (r.t == T_ND && r.u.o == (Hdr *)a) return ndv(nd_astype(a, a->dt));
    return r;
}
BI(np_allclose)
{
    NdTmp ta, tb;
    Nd *a, *b;
    double rtol = 1e-5, atol = 1e-8;
    Value rv = x->n > 2 ? A(2) : kwget(x, "rtol"), av = x->n > 3 ? A(3) : kwget(x, "atol");
    long rs[ND_MAXD], i;
    int rnd;
    if (x->n < 2) throw_new(cls_TypeError, "allclose() missing required arguments");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (rv.t != T_UNSET) rtol = to_dbl(rv);
    if (av.t != T_UNSET) atol = to_dbl(av);
    rnd = nd_bshape(a, b, rs);
    a = nd_expand(a, rnd, rs, ND_FLOAT);
    b = nd_expand(b, rnd, rs, ND_FLOAT);
    for (i = 0; i < a->size; i++) {
        double p = a->dv[i], q = b->dv[i];
        if (p != p || q != q) return FALSE_V;
        if (p == q) continue;
        if (p - p != 0 || q - q != 0) return FALSE_V;
        if (!(fabs(p - q) <= atol + rtol * fabs(q))) return FALSE_V;
    }
    return TRUE_V;
}
BI(np_array_equal)
{
    NdTmp ta, tb;
    Nd *a, *b;
    long i;
    int k;
    if (x->n != 2) throw_new(cls_TypeError, "array_equal() takes exactly 2 arguments");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->nd != b->nd) return FALSE_V;
    for (k = 0; k < a->nd; k++) if (a->shape[k] != b->shape[k]) return FALSE_V;
    for (i = 0; i < a->size; i++) {
        if (a->dt == ND_FLOAT || b->dt == ND_FLOAT) {
            if (nd_d(a, i) != nd_d(b, i)) return FALSE_V;
        } else if (a->iv[i] != b->iv[i]) return FALSE_V;
    }
    return TRUE_V;
}
BI(np_count_nonzero)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long i, c = 0;
    for (i = 0; i < a->size; i++) if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) c++;
    return mkint(c);
}
#define NDF1(fname, fn) \
    BI(fname) { NdTmp t_; Nd *a_ = nd_arg0c(x, &t_); return nd_ufunc1(fn, a_); }
NDF1(np_sqrt, F_SQRT)
NDF1(np_exp, F_EXP)
NDF1(np_sin, F_SIN)
NDF1(np_cos, F_COS)
NDF1(np_tan, F_TAN)
NDF1(np_log, F_LOG)
NDF1(np_log10, F_LOG10)
NDF1(np_log2, F_LOG2)
NDF1(np_floor, F_FLOOR)
NDF1(np_ceil, F_CEIL)
NDF1(np_tanh, F_TANH)
NDF1(np_sinh, F_SINH)
NDF1(np_cosh, F_COSH)
NDF1(np_arcsin, F_ASIN)
NDF1(np_arccos, F_ACOS)
NDF1(np_arctan, F_ATAN)
NDF1(np_abs, F_ABS)
NDF1(np_sign, F_SIGN)
NDF1(np_square, F_SQUARE)
NDF1(np_isnan, F_ISNAN)
NDF1(np_isinf, F_ISINF)
NDF1(np_isfinite, F_ISFINITE)
NDF1(np_negative, F_NEG)
#define NDB2(fname, op) \
    BI(fname) { \
        NdTmp ta_, tb_; \
        if (x->n != 2) throw_new(cls_TypeError, "function takes exactly 2 arguments"); \
        return nd_binary(op, nd_coerce_c(A(0), &ta_), nd_coerce_c(A(1), &tb_)); }
NDB2(np_add, B_ADD)
NDB2(np_subtract, B_SUB)
NDB2(np_multiply, B_MUL)
NDB2(np_divide, B_DIV)
NDB2(np_floor_divide, B_FDIV)
NDB2(np_mod, B_MOD)
NDB2(np_power, B_POW)
NDB2(np_maximum, OP_MAX)
NDB2(np_minimum, OP_MIN)

/* ---- complex helpers: real/imag/conj/angle ------------------------------ */
static Nd *nd_part(Nd *a, int which)
{
    Nd *r;
    long i;
    if (a->dt != ND_COMPLEX) {
        if (which == 0) return a;
        r = nd_new(a->dt, a->nd, a->shape);
        for (i = 0; i < a->size; i++) {
            if (r->dt == ND_FLOAT) r->dv[i] = 0.0; else r->iv[i] = 0;
        }
        return r;
    }
    r = nd_new(ND_FLOAT, a->nd, a->shape);
    for (i = 0; i < a->size; i++) r->dv[i] = a->dv[2 * i + which];
    return r;
}
static Nd *nd_conj(Nd *a)
{
    Nd *r;
    long i;
    if (a->dt != ND_COMPLEX) return nd_astype(a, a->dt);
    r = nd_new(ND_COMPLEX, a->nd, a->shape);
    for (i = 0; i < a->size; i++) {
        r->dv[2 * i] = a->dv[2 * i];
        r->dv[2 * i + 1] = -a->dv[2 * i + 1];
    }
    return r;
}
BI(np_conj)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    if (x->n > 0 && A(0).t == T_COMPLEX) { Cplx *c = AS_CPLX(A(0)); return mkcomplex(c->re,
        -c->im); }
    return nd_ret(nd_conj(a));
}
BI(np_real)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    return nd_ret(nd_part(a, 0));
}
BI(np_imag)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t);
    return nd_ret(nd_part(a, 1));
}
BI(np_angle)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t), *r;
    Value dg = x->n > 1 ? A(1) : kwget(x, "deg");
    long i;
    double k = (dg.t != T_UNSET && py_truthy(dg)) ? 180.0 / 3.14159265358979323846 : 1.0;
    r = nd_new(ND_FLOAT, a->nd, a->shape);
    for (i = 0; i < a->size; i++) {
        double re = a->dt == ND_COMPLEX ? a->dv[2 * i] : nd_d(a, i);
        double im = a->dt == ND_COMPLEX ? a->dv[2 * i + 1] : 0.0;
        r->dv[i] = atan2(im, re) * k;
    }
    return nd_ret(r);
}
BI(np_iscomplexobj)
{
    NdTmp t;
    if (x->n > 0 && A(0).t == T_COMPLEX) return mkbool(1);
    return mkbool(nd_arg0c(x, &t)->dt == ND_COMPLEX);
}
BI(np_iscomplex)
{
    NdTmp t;
    Nd *a = nd_arg0c(x, &t), *r;
    long i;
    r = nd_new(ND_BOOL, a->nd, a->shape);
    for (i = 0; i < a->size; i++) r->iv[i] = a->dt == ND_COMPLEX && a->dv[2 * i + 1] != 0.0;
    return nd_ret(r);
}
BI(np_isreal)
{
    Value v = np_iscomplex(x);
    Nd *a = AS_ND(v);
    long i;
    for (i = 0; i < a->size; i++) a->iv[i] = !a->iv[i];
    return v;
}

/* ---- attributes -------------------------------------------------------- */
static int nd_getattr(Value o, Str *nm, Value *out)
{
    Nd *a = AS_ND(o);
    const char *s = nm->s;
    if (!strcmp(s, "shape")) {
        Value tv[ND_MAXD];
        int k;
        for (k = 0; k < a->nd; k++) tv[k] = mkint(a->shape[k]);
        *out = mktuple_from(tv, a->nd);
        return 1;
    }
    if (!strcmp(s, "ndim")) { *out = mkint(a->nd); return 1; }
    if (!strcmp(s, "size")) { *out = mkint(a->size); return 1; }
    if (!strcmp(s, "dtype")) { *out = dt_inst[a->dt]; return 1; }
    if (!strcmp(s, "itemsize")) {
        *out = mkint(a->dt == ND_BOOL ? 1 : a->dt == ND_COMPLEX ? 16 : 8);
        return 1;
    }
    if (!strcmp(s, "nbytes")) {
        *out = mkint(a->size * (a->dt == ND_BOOL ? 1 : a->dt == ND_COMPLEX ? 16 : 8));
        return 1;
    }
    if (!strcmp(s, "T")) { *out = ndv(nd_transposed(a)); return 1; }
    if (!strcmp(s, "real")) { *out = ndv(nd_part(a, 0)); return 1; }
    if (!strcmp(s, "imag")) { *out = ndv(nd_part(a, 1)); return 1; }
    if (!strcmp(s, "flat")) { long n = a->size; *out = ndv(nd_reshaped(a, 1, &n)); return 1; }
    return 0;
}

/* ---- module ------------------------------------------------------------ */
static int np_float_done;
static void np_float_methods(void)
{
    if (np_float_done) return;
    np_float_done = 1;
    def_bi(cls_float, "round", f_np_round);
    class_epoch++;
}
static void np_init(void)
{
    Module *m;
    Class *c;
    c = (Class *)gc_alloc(sizeof(Class), T_CLASS);
    c->name = intern_c("ndarray"); c->base = cls_object;
    c->ns.k = NULL; c->ns.v = NULL; c->ns.n = c->ns.cap = 0;
    c->ctor = np_array; c->flags = CF_BUILTIN;
    c->mro = NULL; c->nmro = 0; c->bases = NULL; c->nbases = 0; c->paycls = NULL; c->modname = NULL;
    c->meta = NULL;
    cls_ndarray = c;
    def_bi(cls_ndarray, "tolist", nd_tolist); def_bi(cls_ndarray, "copy", nd_copy);
    def_bi(cls_ndarray, "reshape", nd_reshape); def_bi(cls_ndarray, "flatten", nd_flatten);
    def_bi(cls_ndarray, "ravel", nd_flatten); def_bi(cls_ndarray, "astype", nd_astype_m);
    def_bi(cls_ndarray, "item", nd_item_m); def_bi(cls_ndarray, "transpose", nd_transpose);
    def_bi(cls_ndarray, "sort", nd_sort_m); def_bi(cls_ndarray, "argsort", np_argsort);
    def_bi(cls_ndarray, "sum", np_sum); def_bi(cls_ndarray, "mean", np_mean);
    def_bi(cls_ndarray, "min", np_min); def_bi(cls_ndarray, "max", np_max);
    def_bi(cls_ndarray, "prod", np_prod); def_bi(cls_ndarray, "argmax", np_argmax);
    def_bi(cls_ndarray, "argmin", np_argmin); def_bi(cls_ndarray, "cumsum", np_cumsum);
    def_bi(cls_ndarray, "cumprod", np_cumprod); def_bi(cls_ndarray, "std", np_std);
    def_bi(cls_ndarray, "var", np_var); def_bi(cls_ndarray, "round", np_round);
    def_bi(cls_ndarray, "dot", np_dot); def_bi(cls_ndarray, "any", np_any);
    def_bi(cls_ndarray, "all", np_all); def_bi(cls_ndarray, "clip", np_clip);
    def_bi(cls_ndarray, "conj", np_conj); def_bi(cls_ndarray, "conjugate", np_conj);
    def_bi(cls_ndarray, "nonzero", np_nonzero); def_bi(cls_ndarray, "trace", np_trace);
    m = new_module("numpy");
    mod_def(m, "array", np_array); mod_def(m, "asarray", np_asarray);
    mod_def(m, "zeros", np_zeros); mod_def(m, "ones", np_ones); mod_def(m, "empty", np_zeros);
    mod_def(m, "full", np_full); mod_def(m, "zeros_like", np_zeros_like);
    mod_def(m, "ones_like", np_ones_like); mod_def(m, "empty_like", np_zeros_like);
    mod_def(m, "full_like", np_full_like); mod_def(m, "eye", np_eye);
    mod_def(m, "identity", np_eye);
    mod_def(m, "arange", np_arange); mod_def(m, "linspace", np_linspace);
    mod_def(m, "sqrt", np_sqrt); mod_def(m, "abs", np_abs); mod_def(m, "absolute", np_abs);
    mod_def(m, "exp", np_exp); mod_def(m, "sin", np_sin); mod_def(m, "cos", np_cos);
    mod_def(m, "tan", np_tan); mod_def(m, "log", np_log); mod_def(m, "log10", np_log10);
    mod_def(m, "log2", np_log2); mod_def(m, "floor", np_floor); mod_def(m, "ceil", np_ceil);
    mod_def(m, "tanh", np_tanh); mod_def(m, "sinh", np_sinh); mod_def(m, "cosh", np_cosh);
    mod_def(m, "arcsin", np_arcsin); mod_def(m, "arccos", np_arccos);
    mod_def(m, "arctan", np_arctan); mod_def(m, "sign", np_sign);
    mod_def(m, "square", np_square); mod_def(m, "negative", np_negative);
    mod_def(m, "isnan", np_isnan); mod_def(m, "isinf", np_isinf);
    mod_def(m, "isfinite", np_isfinite);
    mod_def(m, "add", np_add); mod_def(m, "subtract", np_subtract);
    mod_def(m, "multiply", np_multiply); mod_def(m, "divide", np_divide);
    mod_def(m, "true_divide", np_divide); mod_def(m, "floor_divide", np_floor_divide);
    mod_def(m, "mod", np_mod); mod_def(m, "remainder", np_mod); mod_def(m, "power", np_power);
    mod_def(m, "maximum", np_maximum); mod_def(m, "minimum", np_minimum);
    mod_def(m, "where", np_where); mod_def(m, "nonzero", np_nonzero);
    mod_def(m, "concatenate", np_concatenate); mod_def(m, "vstack", np_vstack);
    mod_def(m, "hstack", np_hstack); mod_def(m, "stack", np_stack);
    mod_def(m, "sort", np_sort); mod_def(m, "argsort", np_argsort);
    mod_def(m, "unique", np_unique); mod_def(m, "median", np_median);
    mod_def(m, "dot", np_dot); mod_def(m, "matmul", np_matmul);
    mod_def(m, "transpose", nd_transpose); mod_def(m, "diag", np_diag);
    mod_def(m, "trace", np_trace); mod_def(m, "cumsum", np_cumsum);
    mod_def(m, "cumprod", np_cumprod);
    mod_def(m, "clip", np_clip); mod_def(m, "any", np_any); mod_def(m, "all", np_all);
    mod_def(m, "allclose", np_allclose); mod_def(m, "array_equal", np_array_equal);
    mod_def(m, "std", np_std); mod_def(m, "var", np_var);
    mod_def(m, "conj", np_conj); mod_def(m, "conjugate", np_conj);
    mod_def(m, "real", np_real); mod_def(m, "imag", np_imag); mod_def(m, "angle", np_angle);
    mod_def(m, "iscomplex", np_iscomplex); mod_def(m, "isreal", np_isreal);
    mod_def(m, "iscomplexobj", np_iscomplexobj);
    mod_val(m, "complex128", mkobj(cls_complex, T_CLASS)); mod_val(m, "complex_", mkobj(cls_complex,
        T_CLASS));
    mod_val(m, "complex64", mkobj(cls_complex, T_CLASS));
    mod_def(m, "round", np_round); mod_def(m, "around", np_round);
    mod_def(m, "sum", np_sum); mod_def(m, "mean", np_mean); mod_def(m, "prod", np_prod);
    mod_def(m, "min", np_min); mod_def(m, "max", np_max); mod_def(m, "amin", np_min);
    mod_def(m, "amax", np_max); mod_def(m, "argmin", np_argmin); mod_def(m, "argmax", np_argmax);
    mod_def(m, "reshape", nd_reshape); mod_def(m, "ravel", nd_flatten);
    mod_def(m, "copy", nd_copy); mod_def(m, "count_nonzero", np_count_nonzero);
    mod_val(m, "pi", mkfloat(3.14159265358979323846));
    mod_val(m, "e", mkfloat(2.71828182845904523536));
    mod_val(m, "inf", mkfloat(HUGE_VAL));
    mod_val(m, "nan", mkfloat(HUGE_VAL - HUGE_VAL));
    mod_val(m, "newaxis", NONE_V);
    mod_val(m, "ndarray", mkobj(cls_ndarray, T_CLASS));
    mod_val(m, "float64", mkobj(cls_float, T_CLASS));
    mod_val(m, "int64", mkobj(cls_int, T_CLASS));
    mod_val(m, "bool_", mkobj(cls_bool, T_CLASS));
    dtype_init(m);
    {
        static const char *an[NA_N] = { "generic", "number", "integer", "signedinteger",
            "unsignedinteger", "inexact", "floating", "complexfloating" };
        int i;
        for (i = 0; i < NA_N; i++) {
            cls_npabs[i] = mod_class(an[i], cls_object, "numpy");
            cls_npabs[i]->flags |= CF_NPABS;
            mod_val(m, an[i], mkobj(cls_npabs[i], T_CLASS));
        }
    }
    {
        static const char *ia[] = { "int8", "int16", "uint8", "uint16", "uint32", "uint64", "uint",
                                    "intp", "uintp", "intc", "short", "ushort", "byte", "ubyte",
                                    "longlong", "ulonglong", "long", NULL };
        static const char *fa[] = { "float16", "half", "single", "double", "longdouble",
                                    "float128", NULL };
        static const char *ca[] = { "csingle", "cdouble", "clongdouble", "complex256", NULL };
        int i;
        for (i = 0; ia[i]; i++) mod_val(m, ia[i], mkobj(cls_int, T_CLASS));
        for (i = 0; fa[i]; i++) mod_val(m, fa[i], mkobj(cls_float, T_CLASS));
        for (i = 0; ca[i]; i++) mod_val(m, ca[i], mkobj(cls_complex, T_CLASS));
    }
    np_init2(m);
    la_init(m);
    rnd_init(m);
}
