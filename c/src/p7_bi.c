/* =========================================================================
   p7_bi.c   builtin functions and methods of the builtin types
   ========================================================================= */
#define BI(name) static Value name(Args *x)
#define A(i) (x->a[i])


static Value kwget(Args *x, const char *nm)
{
    int i;
    for (i = 0; i < x->nk; i++)
        if (strcmp(AS_STR(x->kw[2 * i])->s, nm) == 0) return x->kw[2 * i + 1];
    return UNSET_V;
}
static void need(Args *x, int lo, int hi, const char *fname)
{
    if (x->n < lo || x->n > hi) {
        if (lo == hi)
            throw_new(cls_TypeError, "%s() takes exactly %d argument%s (%d given)", fname, lo,
                      lo == 1 ? "" : "s", x->n);
        if (x->n < lo)
            throw_new(cls_TypeError, "%s() takes at least %d argument%s (%d given)", fname, lo,
                      lo == 1 ? "" : "s", x->n);
        throw_new(cls_TypeError, "%s() takes at most %d argument%s (%d given)", fname, hi,
                  hi == 1 ? "" : "s", x->n);
    }
}
static Str *astr(Value v, const char *what)
{
    if (v.t == T_INST && AS_INST(v)->pay.t == T_STR) v = AS_INST(v)->pay;
    if (v.t != T_STR)
        throw_new(cls_TypeError, "%s must be str, not %.40s", what, type_name(v));
    return AS_STR(v);
}
static Value mkbuiltin(const char *name, BFn fn)
{
    Builtin *b = (Builtin *)gc_alloc(sizeof(Builtin), T_BUILTIN);
    b->fn = fn;
    b->name = name;
    b->sname = intern_c(name);
    b->selfpay = 0;
    return mkobj(b, T_BUILTIN);
}
static void def_bi(Class *c, const char *name, BFn fn)
{
    Value b = mkbuiltin(name, fn);
    if (c && (c == cls_str || c == cls_list || c == cls_tuple || c == cls_dict || c == cls_set ||
              c == cls_int || c == cls_float || c == cls_bytes || c == cls_bytearray ||
              c == cls_complex || c == cls_frozenset))
        ((Builtin *)b.u.o)->selfpay = 1;
    if (c) amap_set(&c->ns, intern_c(name), b);
    else set_global(intern_c(name), b);
}
static Class *new_class(const char *name, Class *base, int flags)
{
    Class *c = (Class *)gc_alloc(sizeof(Class), T_CLASS);
    c->name = intern_c(name);
    c->base = base;
    c->ns.k = NULL; c->ns.v = NULL; c->ns.n = c->ns.cap = 0;
    c->ctor = NULL;
    c->mro = NULL; c->nmro = 0; c->bases = NULL; c->nbases = 0; c->paycls = NULL;
    c->modname = NULL; c->meta = NULL;
    c->flags = flags | (base ? (base->flags & CF_EXC) : 0);
    set_global(c->name, mkobj(c, T_CLASS));
    return c;
}
static long to_index(Value v)
{
    return need_long(v, "argument");
}

/* ---- output ----------------------------------------------------------- */
static int try_attr(Value o, Str *nm, Value *out);
static Module *sys_mod;
static Value print_target(Value filev)
{
    if (filev.t != T_UNSET && filev.t != T_NONE) return filev;
    if (sys_mod) {
        Value *p = amap_get(&sys_mod->ns, intern_c("stdout"));
        if (p) return *p;
    }
    return UNSET_V;
}
static void print_write(Value target, const char *s, long n)
{
    Value m, str;
    if (n == 0) return;
    m = get_attr(target, intern_c("write"));
    VPUSH(m);
    str = mkstr(s, n);
    VPUSH(str);
    call1(m, str);
    vsp -= 2;
}
BI(bi_print)
{
    Value sepv = kwget(x, "sep"), endv = kwget(x, "end"), filev = kwget(x, "file");
    Value flv = kwget(x, "flush");
    FILE *fp = stdout;
    const char *sep = " ", *end = "\n";
    long sepn = 1, endn = 1;
    int i;
    Value target = print_target(filev);
    if (sepv.t == T_STR) { sep = AS_STR(sepv)->s; sepn = AS_STR(sepv)->len; }
    if (endv.t == T_STR) { end = AS_STR(endv)->s; endn = AS_STR(endv)->len; }
    if (target.t == T_FILE) fp = ((File *)target.u.o)->fp;
    else if (target.t != T_UNSET) {
        for (i = 0; i < x->n; i++) {
            Value sv = py_str(A(i));
            VPUSH(sv);
            if (i) print_write(target, sep, sepn);
            print_write(target, AS_STR(sv)->s, AS_STR(sv)->len);
            vsp--;
        }
        print_write(target, end, endn);
        if (flv.t != T_UNSET && py_truthy(flv)) {
            Value fm;
            if (try_attr(target, intern_c("flush"), &fm)) call0(fm);
        }
        return NONE_V;
    }
    if (fp == stderr) fflush(stdout);
    for (i = 0; i < x->n; i++) {
        Buf b;
        if (i) fwrite(sep, 1, (size_t)sepn, fp);
        if (A(i).t == T_STR) {
            fwrite(AS_STR(A(i))->s, 1, (size_t)AS_STR(A(i))->len, fp);
        } else {
            buf_init(&b);
            str_buf(A(i), &b);
            fwrite(b.s, 1, (size_t)b.n, fp);
            buf_free(&b);
        }
    }
    fwrite(end, 1, (size_t)endn, fp);
    if (flv.t != T_UNSET && py_truthy(flv)) fflush(fp);
    return NONE_V;
}
BI(bi_input)
{
    Buf b;
    int c;
    if (x->n > 0) {
        Value s = py_str(A(0));
        fwrite(AS_STR(s)->s, 1, (size_t)AS_STR(s)->len, stdout);
    }
    fflush(stdout);
    buf_init(&b);
    c = getc(stdin);
    if (c == EOF) { buf_free(&b); throw_new(cls_EOFError, "EOF when reading a line"); }
    while (c != EOF && c != '\n') { buf_addc(&b, c); c = getc(stdin); }
    if (b.n > 0 && b.s[b.n - 1] == '\r') b.n--;
    return buf_value(&b);
}

/* ---- numbers and conversions ------------------------------------------ */
BI(bi_len)
{
    need(x, 1, 1, "len");
    return mkint(py_len(A(0)));
}
BI(bi_repr)
{
    need(x, 1, 1, "repr");
    return py_repr(A(0));
}
BI(bi_str)
{
    if (x->n == 0) return mkstr("", 0);
    if ((x->n > 1 || x->nk > 0) && (A(0).t == T_BYTES || A(0).t == T_BARR)) {
        Value *m = class_find(class_of(A(0)), intern_c("decode"));
        if (m) return call_value(*m, x->n, x->a, x->nk, x->kw);
    }
    return py_str(A(0));
}
BI(bi_bool)
{
    if (x->n == 0) return FALSE_V;
    return mkbool(py_truthy(A(0)));
}
BI(bi_int)
{
    Value v, bv = kwget(x, "base");
    int base = 10;
    if (x->n == 0) return mkint(0);
    v = A(0);
    if (x->n > 1) bv = A(1);
    if (bv.t != T_UNSET) base = (int)to_index(bv);
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET && !class_find(AS_INST(v)->cls, S_int) &&
        !class_find(AS_INST(v)->cls, S_index))
        v = AS_INST(v)->pay;
    if (v.t == T_BYTES || v.t == T_BARR) {   /* int(b"ff", 16): the text of a bytes-like object */
        const char *bp;
        long bn;
        Value r;
        bytes_get(v, &bp, &bn);
        if (!parse_int(bp, bn, bv.t == T_UNSET ? 10 : base, &r)) {
            Value q = py_repr(v);
            throw_new(cls_ValueError, "invalid literal for int() with base %d: %.200s", base,
                AS_STR(q)->s);
        }
        return r;
    }
    if (v.t == T_STR) {
        Value r;
        if (!parse_int(AS_STR(v)->s, AS_STR(v)->len, bv.t == T_UNSET ? 10 : base, &r)) {
            Value q = py_repr(v);
            throw_new(cls_ValueError, "invalid literal for int() with base %d: %.200s", base,
                AS_STR(q)->s);
        }
        return r;
    }
    if (bv.t != T_UNSET)
        throw_new(cls_TypeError, "int() can't convert non-string with explicit base");
    if (v.t == T_INT || v.t == T_BIG) return v;
    if (v.t == T_BOOL) return mkint(v.u.i);
    if (v.t == T_FLOAT) return int_from_double(v.u.d < 0 ? ceil(v.u.d) : floor(v.u.d));
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_int);
        if (m) return call1(*m, v);
        m = class_find(AS_INST(v)->cls, S_index);
        if (m) return call1(*m, v);
    }
    throw_new(cls_TypeError,
        "int() argument must be a string, a bytes-like object or a real number, not '%.40s'",
        type_name(v));
    return NONE_V;
}
BI(bi_float)
{
    Value v;
    if (x->n == 0) return mkfloat(0.0);
    v = A(0);
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET && !class_find(AS_INST(v)->cls, S_float))
        v = AS_INST(v)->pay;
    if (v.t == T_BYTES) v = mkstr(AS_STR(v)->s, AS_STR(v)->len);
    if (v.t == T_FLOAT) return v;
    if (is_intv(v)) return mkfloat(int_todouble(v));
    if (v.t == T_STR) {
        double d;
        if (!parse_float(AS_STR(v)->s, AS_STR(v)->len, &d)) {
            Value q = py_repr(v);
            throw_new(cls_ValueError, "could not convert string to float: %.200s", AS_STR(q)->s);
        }
        return mkfloat(d);
    }
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_float);
        if (m) return call1(*m, v);
    }
    throw_new(cls_TypeError, "float() argument must be a string or a real number, not '%.40s'",
        type_name(v));
    return NONE_V;
}
BI(bi_abs)
{
    Value v;
    need(x, 1, 1, "abs");
    v = A(0);
    if (v.t == T_INT || v.t == T_BOOL) return mkint(v.u.i < 0 ? -v.u.i : v.u.i);
    if (v.t == T_FLOAT) return mkfloat(fabs(v.u.d));
    if (v.t == T_BIG) return int_sign(v) < 0 ? int_neg(v) : v;
    if (v.t == T_COMPLEX) return mkfloat(n_hypot(AS_CPLX(v)->re, AS_CPLX(v)->im));
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_abs);
        if (m) return call1(*m, v);
    }
    throw_new(cls_TypeError, "bad operand type for abs(): '%.40s'", type_name(v));
    return NONE_V;
}
/* round-half-even of the exact quotient a / b (a >= 0, b > 0) */
static Value int_round_div(Value a, Value b)
{
    Value q, r, h;
    int c;
    int_divmod(a, b, &q, &r);
    h = int_mul(r, mkint(2));
    c = int_cmp(h, b);
    if (c > 0 || (c == 0 && int_sign(int_mod(q, mkint(2))) != 0)) q = int_add(q, mkint(1));
    return q;
}
/* round(d, -m) for m > 0: d rounded to a multiple of 10**m, exactly (bigint arithmetic) */
static double round_float_neg(double d, long m)
{
    double f, mant, r;
    int e, neg = 0;
    Value p10, num, den, q;
    if (m > 400) return d < 0 ? -0.0 : 0.0;
    if (d < 0) { neg = 1; d = -d; }
    p10 = int_pow(mkint(10), m);
    f = frexp(d, &e);
    mant = ldexp(f, 53);
    e -= 53;                                   /* d = mant * 2**e */
    num = int_from_double(mant);
    if (e >= 0) { num = int_mul(num, int_pow2(e)); den = p10; }
    else den = int_mul(int_pow2(-e), p10);
    q = int_round_div(num, den);
    r = int_todouble(int_mul(q, p10));
    return neg ? -r : r;
}
BI(bi_round)
{
    Value v, nd;
    need(x, 1, 2, "round");
    v = A(0);
    nd = x->n > 1 ? A(1) : kwget(x, "ndigits");
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET &&
        !class_find(AS_INST(v)->cls, intern_c("__round__")))
        v = AS_INST(v)->pay;
    if (nd.t == T_BIG && (is_intv(v) || v.t == T_FLOAT)) {
        if (int_sign(nd) > 0) return v.t == T_BOOL ? mkint(v.u.i) : v;
        return v.t == T_FLOAT ? mkfloat(v.u.d < 0 ? -0.0 : 0.0) : mkint(0);
    }
    if (is_intv(v)) {
        if (nd.t == T_UNSET || nd.t == T_NONE || to_index(nd) >= 0)
            return v.t == T_BOOL ? mkint(v.u.i) : v;
        {
            Value p = int_pow(mkint(10), -to_index(nd)), q;
            int neg = int_sign(v) < 0;
            q = int_round_div(neg ? int_neg(v) : v, p);
            q = int_mul(q, p);
            return neg ? int_neg(q) : q;
        }
    }
    if (v.t == T_FLOAT) {
        double d = v.u.d;
        if (nd.t == T_UNSET || nd.t == T_NONE) {
            double r;
            if (d != d) throw_new(cls_ValueError, "cannot convert float NaN to integer");
            if (d > DBL_MAX || d < -DBL_MAX)
                throw_new(cls_OverflowError, "cannot convert float infinity to integer");
            r = floor(d);
            if (d - r > 0.5) r += 1.0;
            else if (d - r == 0.5 && fmod(r, 2.0) != 0) r += 1.0;
            return int_from_double(r);
        } else {
            long k = to_index(nd);
            char tmp[400];
            if (d != d || d > DBL_MAX || d < -DBL_MAX) return v;
            if (k > 300) return v;
            if (k >= 0) {
                sprintf(tmp, "%.*f", (int)k, d);
                return mkfloat(strtod(tmp, NULL));
            }
            return mkfloat(round_float_neg(d, -k));
        }
    }
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, intern_c("__round__"));
        if (m) return x->n > 1 ? call2(*m, v, A(1)) : call1(*m, v);
    }
    throw_new(cls_TypeError, "type %.40s doesn't define __round__ method", type_name(v));
    return NONE_V;
}
BI(bi_divmod)
{
    Value q, r;
    need(x, 2, 2, "divmod");
    if (is_intv(A(0)) && is_intv(A(1))) {
        int_divmod(A(0), A(1), &q, &r);
        return mktuple2(q, r);
    }
    if (is_num(A(0)) && is_num(A(1))) {
        double a = to_dbl(A(0)), b = to_dbl(A(1));
        if (b == 0) throw_new(cls_ZeroDivisionError, "float divmod()");
        return mktuple2(mkfloat(fl_floordiv(a, b)), mkfloat(fl_mod(a, b)));
    }
    if (A(0).t == T_INST) {
        Value *m = class_find(AS_INST(A(0))->cls, intern_c("__divmod__"));
        if (m) {
            q = call2(*m, A(0), A(1));
            if (!IS_NOTIMPL(q)) return q;
        }
    }
    if (A(1).t == T_INST) {
        Value *m = class_find(AS_INST(A(1))->cls, intern_c("__rdivmod__"));
        if (m) {
            q = call2(*m, A(1), A(0));
            if (!IS_NOTIMPL(q)) return q;
        }
    }
    throw_new(cls_TypeError, "unsupported operand type(s) for divmod(): '%.40s' and '%.40s'",
              type_name(A(0)), type_name(A(1)));
    return NONE_V;
}
/* base ** -1 modulo m (m != 0), or ValueError when there is no inverse */
static Value int_modinv(Value a, Value m)
{
    Value am = int_sign(m) < 0 ? int_neg(m) : m, r0, r1, s0, s1, q, t;
    r0 = int_mod(a, am); r1 = am; s0 = mkint(1); s1 = mkint(0);
    while (int_sign(r1) != 0) {
        q = int_floordiv(r0, r1);
        t = int_sub(r0, int_mul(q, r1)); r0 = r1; r1 = t;
        t = int_sub(s0, int_mul(q, s1)); s0 = s1; s1 = t;
    }
    if (!(r0.t == T_INT && r0.u.i == 1))
        throw_new(cls_ValueError, "base is not invertible for the given modulus");
    return int_mod(s0, m);
}
/* pow(base, e, m) for integers of any size */
static Value int_powmod(Value base, Value e, Value m)
{
    Value r, b, q, rem;
    if (int_sign(m) == 0) throw_new(cls_ValueError, "pow() 3rd argument cannot be 0");
    if (int_sign(e) < 0) { b = int_modinv(base, m); e = int_neg(e); }
    else b = int_mod(base, m);
    r = int_mod(mkint(1), m);
    while (int_sign(e) > 0) {
        int_divmod(e, mkint(2), &q, &rem);
        if (int_sign(rem) != 0) r = int_mod(int_mul(r, b), m);
        e = q;
        if (int_sign(e) > 0) b = int_mod(int_mul(b, b), m);
    }
    return r;
}
BI(bi_pow)
{
    need(x, 2, 3, "pow");
    if (x->n == 3 && A(2).t != T_NONE) {
        Value b = unpay(A(0)), e = unpay(A(1)), m = unpay(A(2));
        if (!is_intv(b) || !is_intv(e) || !is_intv(m))
            throw_new(cls_TypeError,
                "pow() 3rd argument not allowed unless all arguments are integers");
        return int_powmod(b.t == T_BOOL ? mkint(b.u.i) : b, e.t == T_BOOL ? mkint(e.u.i) : e,
                          m.t == T_BOOL ? mkint(m.u.i) : m);
    }
    return binop(B_POW, A(0), A(1));
}
BI(bi_chr)
{
    Buf b;
    long c;
    need(x, 1, 1, "chr");
    c = to_index(A(0));
    if (c < 0 || c > 0x10ffffL) throw_new(cls_ValueError, "chr() arg not in range(0x110000)");
    buf_init(&b);
    put_utf8(&b, (unsigned long)c);
    return buf_value(&b);
}
BI(bi_ord)
{
    Str *s;
    unsigned long c;
    need(x, 1, 1, "ord");
    {
        const char *bp;
        long bn;
        if (bytes_get(A(0), &bp, &bn) && bn == 1) return mkint((unsigned char)bp[0]);
    }
    s = astr(A(0), "ord() argument");
    if (s->len == 1) return mkint((unsigned char)s->s[0]);
    c = (unsigned char)s->s[0];
    if (s->len == 2 && (c & 0xE0) == 0xC0)
        return mkint((long)(((c & 0x1F) << 6) | ((unsigned char)s->s[1] & 0x3FUL)));
    if (s->len == 3 && (c & 0xF0) == 0xE0)
        return mkint((long)(((c & 0x0F) << 12) | (((unsigned char)s->s[1] & 0x3FUL) << 6) |
                            ((unsigned char)s->s[2] & 0x3FUL)));
    if (s->len == 4 && (c & 0xF8) == 0xF0)
        return mkint((long)(((c & 0x07) << 18) | (((unsigned char)s->s[1] & 0x3FUL) << 12) |
                            (((unsigned char)s->s[2] & 0x3FUL) << 6)
                                | ((unsigned char)s->s[3] & 0x3FUL)));
    throw_new(cls_TypeError, "ord() expected a character, but string of length %ld found", s->len);
    return NONE_V;
}
static Value radix_str(Value v, int base, const char *prefix)
{
    Buf b;
    if (!is_intv(v))
        throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
        type_name(v));
    if (v.t == T_BOOL) v = mkint(v.u.i);
    buf_init(&b);
    if (int_sign(v) < 0) { buf_addc(&b, '-'); v = int_neg(v); }
    buf_add(&b, prefix);
    int_str_base(v, base, &b);
    return buf_value(&b);
}
BI(bi_hex) { need(x, 1, 1, "hex"); return radix_str(A(0), 16, "0x"); }
BI(bi_oct) { need(x, 1, 1, "oct"); return radix_str(A(0), 8, "0o"); }
BI(bi_bin) { need(x, 1, 1, "bin"); return radix_str(A(0), 2, "0b"); }

/* ---- collections ------------------------------------------------------ */
BI(bi_list)
{
    if (x->n == 0) return mklist(list_new(4));
    return iter_to_list(A(0));
}
BI(bi_tuple)
{
    Value l, t;
    if (x->n == 0) return mktuple_from(NULL, 0);
    if (A(0).t == T_TUPLE) return A(0);
    l = iter_to_list(A(0));
    VPUSH(l);
    t = mktuple_from(AS_LIST(l)->a, AS_LIST(l)->n);
    vsp--;
    return t;
}
static void dict_update_from(Dict *d, Value src)
{
    src = unpay(src);
    if (src.t == T_DICT) {
        Dict *s = AS_DICT(src);
        long i;
        for (i = 0; i < s->used; i++)
            if (s->e[i].k.t != T_UNSET) dict_set(d, s->e[i].k, s->e[i].v);
    } else if (src.t == T_INST && class_find(AS_INST(src)->cls, intern_c("keys"))) {
        Value *base = vsp;
        Value ks, it, k;
        VPUSH(src);
        ks = call0(get_attr(src, intern_c("keys")));
        VPUSH(ks);
        it = get_iter(ks);
        VPUSH(it);
        while (iter_next(it, &k)) {
            VPUSH(k);
            dict_set(d, k, subscr(src, k));
            vsp--;
        }
        vsp = base;
    } else {
        Value it, item;
        Value *base = vsp;
        VPUSH(src);
        it = get_iter(src);
        VPUSH(it);
        while (iter_next(it, &item)) {
            Value pair;
            VPUSH(item);
            pair = item;
            if (pair.t != T_LIST && pair.t != T_TUPLE) pair = iter_to_list(item);
            VPUSH(pair);
            if (AS_LIST(pair)->n != 2)
                throw_new(cls_ValueError,
                    "dictionary update sequence element has length %ld; 2 is required",
                    AS_LIST(pair)->n);
            dict_set(d, AS_LIST(pair)->a[0], AS_LIST(pair)->a[1]);
            vsp -= 2;
        }
        vsp = base;
    }
}
BI(bi_dict)
{
    Dict *d = dict_new();
    Value dv = mkobj(d, T_DICT);
    int i;
    VPUSH(dv);
    if (x->n > 0) dict_update_from(d, A(0));
    for (i = 0; i < x->nk; i++) dict_set(d, x->kw[2 * i], x->kw[2 * i + 1]);
    vsp--;
    return dv;
}
BI(bi_set)
{
    Value dv = set_new_v(0);
    need(x, 0, 1, "set");
    if (x->n > 0) {
        VPUSH(dv);
        set_update_value(AS_DICT(dv), A(0));
        vsp--;
    }
    return dv;
}
BI(bi_frozenset)
{
    Value dv;
    need(x, 0, 1, "frozenset");
    if (x->n > 0 && A(0).t == T_SET && AS_DICT(A(0))->frozen) return A(0);   /* idempotent */
    dv = set_new_v(1);
    if (x->n > 0) {
        VPUSH(dv);
        set_update_value(AS_DICT(dv), A(0));
        vsp--;
    }
    return dv;
}
BI(bi_range)
{
    Value a = mkint(0), b, c = mkint(1);
    need(x, 1, 3, "range");
    if (x->n == 1) b = need_intv(A(0), "argument");
    else {
        a = need_intv(A(0), "argument"); b = need_intv(A(1), "argument");
        if (x->n == 3) c = need_intv(A(2), "argument");
    }
    if (int_sign(c) == 0) throw_new(cls_ValueError, "range() arg 3 must not be zero");
    return make_range_v(a, b, c);
}
/* range.index / range.count */
static Value range_self(Args *x, const char *what)
{
    if (x->n < 1 || x->a[0].t != T_RANGE)
        throw_new(cls_TypeError, "descriptor '%s' requires a 'range' object", what);
    return x->a[0];
}
BI(rg_count)
{
    Value r = range_self(x, "count");
    need(x, 2, 2, "count");
    return mkint(contains(r, A(1)) ? 1 : 0);
}
BI(rg_index)
{
    Value rv = range_self(x, "index"), v = A(1), i;
    Range *r = (Range *)rv.u.o;
    need(x, 2, 2, "index");
    if (!contains(rv, v)) {
        Buf b;
        buf_init(&b);
        repr_buf(v, &b, 0);
        buf_add(&b, " is not in range");
        throw_new(cls_ValueError, "%.200s", b.s);
    }
    if (v.t == T_FLOAT) v = int_from_double(v.u.d);
    i = int_floordiv(int_sub(v, r->a), r->c);
    return i;
}
BI(bi_enumerate)
{
    Iter *it;
    Value iv;
    long start = 0;
    Value sv = kwget(x, "start");
    need(x, 1, 2, "enumerate");
    if (x->n > 1) sv = A(1);
    if (sv.t != T_UNSET && sv.t != T_BIG) start = to_index(sv);
    iv = get_iter(A(0));
    VPUSH(iv);
    it = iter_new(IK_ENUM);
    it->a = iv;
    it->i = start;
    it->b = sv.t == T_BIG ? sv : NONE_V;
    vsp--;
    return mkobj(it, T_ITER);
}
BI(bi_zip)
{
    List *its = list_new(x->n);
    Value lv = mklist(its);
    Iter *it;
    int i;
    VPUSH(lv);
    for (i = 0; i < x->n; i++) list_push(its, get_iter(A(i)));
    it = iter_new(IK_ZIP);
    it->a = lv;
    vsp--;
    return mkobj(it, T_ITER);
}
BI(bi_map)
{
    List *its = list_new(x->n);
    Value lv = mklist(its);
    Iter *it;
    int i;
    if (x->n < 2) throw_new(cls_TypeError, "map() must have at least two arguments.");
    VPUSH(lv);
    for (i = 1; i < x->n; i++) list_push(its, get_iter(A(i)));
    it = iter_new(IK_MAP);
    it->a = A(0);
    it->b = lv;
    vsp--;
    return mkobj(it, T_ITER);
}
BI(bi_filter)
{
    Iter *it;
    Value iv;
    need(x, 2, 2, "filter");
    iv = get_iter(A(1));
    VPUSH(iv);
    it = iter_new(IK_FILTER);
    it->a = A(0);
    it->b = iv;
    vsp--;
    return mkobj(it, T_ITER);
}
BI(bi_reversed)
{
    Iter *it;
    Value v;
    need(x, 1, 1, "reversed");
    v = A(0);
    if (v.t == T_DICT || v.t == T_DVIEW) v = iter_to_list(v);
    if (v.t == T_RANGE) {
        Range *r = (Range *)v.u.o;
        Value len = range_lenv(r);
        if (int_sign(len) == 0) return get_iter(make_range(0, 0, 1));
        return get_iter(make_range_v(range_item(r, int_sub(len, mkint(1))), int_sub(r->a, r->c),
                                     int_neg(r->c)));
    }
    if (v.t == T_BYTES || v.t == T_BARR) v = iter_to_list(v);
    if (v.t == T_LIST || v.t == T_TUPLE || v.t == T_STR) {
        it = iter_new(IK_REV);
        it->a = v;
        it->i = py_len(v) - 1;
        return mkobj(it, T_ITER);
    }
    if (v.t == T_CLASS && AS_CLASS(v)->meta) {
        Value *m = class_find(AS_CLASS(v)->meta, S_reversed);
        if (m) return call1(*m, v);
    }
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, intern_c("__reversed__"));
        if (m) return call1(*m, v);
        v = iter_to_list(v);
        it = iter_new(IK_REV);
        it->a = v;
        it->i = AS_LIST(v)->n - 1;
        return mkobj(it, T_ITER);
    }
    throw_new(cls_TypeError, "'%.40s' object is not reversible", type_name(v));
    return NONE_V;
}
BI(bi_iter)
{
    need(x, 1, 2, "iter");
    if (x->n == 2) {
        Iter *it = iter_new(IK_CALLSENT);
        it->a = A(0);
        it->b = A(1);
        return mkobj(it, T_ITER);
    }
    return get_iter(A(0));
}
BI(bi_next)
{
    Value out;
    need(x, 1, 2, "next");
    if (iter_next(A(0), &out)) return out;
    if (x->n > 1) return A(1);
    throw_val(new_exc_args(cls_StopIteration, NULL, 0));
    return NONE_V;
}
BI(bi_any)
{
    Value it, y;
    need(x, 1, 1, "any");
    it = get_iter(A(0));
    VPUSH(it);
    while (iter_next(it, &y)) if (py_truthy(y)) { vsp--; return TRUE_V; }
    vsp--;
    return FALSE_V;
}
BI(bi_all)
{
    Value it, y;
    need(x, 1, 1, "all");
    it = get_iter(A(0));
    VPUSH(it);
    while (iter_next(it, &y)) if (!py_truthy(y)) { vsp--; return FALSE_V; }
    vsp--;
    return TRUE_V;
}
BI(bi_sum)
{
    Value acc = mkint(0), it, y, sv = kwget(x, "start");
    double comp = 0.0;
    need(x, 1, 2, "sum");
    if (x->n > 1) sv = A(1);
    if (sv.t != T_UNSET) acc = sv;
    it = get_iter(A(0));
    VPUSH(it);
    VPUSH(acc);
    while (iter_next(it, &y)) {
        if ((acc.t == T_FLOAT || acc.t == T_INT) &&
            (y.t == T_FLOAT || ((y.t == T_INT || y.t == T_BOOL) && acc.t == T_FLOAT) ||
             (y.t == T_FLOAT && acc.t == T_INT))) {
            double a = acc.t == T_FLOAT ? acc.u.d : (double)acc.u.i;
            double v = y.t == T_FLOAT ? y.u.d : (double)y.u.i;
            double t = a + v;
            if (fabs(a) >= fabs(v)) comp += (a - t) + v;
            else comp += (v - t) + a;
            acc = mkfloat(t);
        } else {
            if (acc.t == T_FLOAT && comp != 0) { acc = mkfloat(acc.u.d + comp); }
            comp = 0.0;
            acc = binop(B_ADD, acc, y);
        }
        vsp[-1] = acc;
    }
    if (acc.t == T_FLOAT && comp != 0 && comp == comp && comp <= DBL_MAX && comp >= -DBL_MAX)
        acc = mkfloat(acc.u.d + comp);
    vsp -= 2;
    return acc;
}
static Value minmax(Args *x, int ismax)
{
    Value key = kwget(x, "key"), dflt = kwget(x, "default"), best = UNSET_V, bestk = UNSET_V;
    Value it, y, k;
    const char *nm = ismax ? "max" : "min";
    Value *base = vsp;
    if (x->n == 0) throw_new(cls_TypeError, "%s expected at least 1 argument, got 0", nm);
    if (x->n == 1) it = get_iter(A(0));
    else it = get_iter(mktuple_from(x->a, x->n));
    VPUSH(it);
    VPUSH(best);
    VPUSH(bestk);
    while (iter_next(it, &y)) {
        VPUSH(y);
        k = (key.t == T_UNSET || key.t == T_NONE) ? y : call1(key, y);
        vsp--;
        if (best.t == T_UNSET || (ismax ? rich_cmp(C_GT, k, bestk) : rich_cmp(C_LT, k, bestk))) {
            best = y; bestk = k;
            base[1] = best; base[2] = bestk;
        }
    }
    vsp = base;
    if (best.t == T_UNSET) {
        if (dflt.t != T_UNSET) return dflt;
        throw_new(cls_ValueError, "%s() iterable argument is empty", nm);
    }
    return best;
}
BI(bi_min) { return minmax(x, 0); }
BI(bi_max) { return minmax(x, 1); }

/* sorting */
static int sort_lt(Value a, Value b)
{
    if (a.t == T_INT && b.t == T_INT) return a.u.i < b.u.i;
    if (a.t == T_STR && b.t == T_STR) {
        Str *p = AS_STR(a), *q = AS_STR(b);
        long n = p->len < q->len ? p->len : q->len;
        int c = memcmp(p->s, q->s, (size_t)n);
        if (c) return c < 0;
        return p->len < q->len;
    }
    if (a.t == T_FLOAT && b.t == T_FLOAT) return a.u.d < b.u.d;
    return rich_cmp(C_LT, a, b);
}
static void msort(Value *k, Value *v, Value *tk, Value *tv, long lo, long hi)
{
    long mid, i, j, p;
    if (hi - lo < 2) return;
    if (hi - lo <= 6) {
        for (i = lo + 1; i < hi; i++) {
            Value kk = k[i], vv = v[i];
            j = i - 1;
            while (j >= lo && sort_lt(kk, k[j])) { k[j + 1] = k[j]; v[j + 1] = v[j]; j--; }
            k[j + 1] = kk; v[j + 1] = vv;
        }
        return;
    }
    mid = (lo + hi) / 2;
    msort(k, v, tk, tv, lo, mid);
    msort(k, v, tk, tv, mid, hi);
    if (!sort_lt(k[mid], k[mid - 1])) return;
    for (i = lo; i < mid; i++) { tk[i] = k[i]; tv[i] = v[i]; }
    i = lo; j = mid; p = lo;
    while (i < mid && j < hi) {
        if (sort_lt(k[j], tk[i])) { k[p] = k[j]; v[p] = v[j]; j++; }
        else { k[p] = tk[i]; v[p] = tv[i]; i++; }
        p++;
    }
    while (i < mid) { k[p] = tk[i]; v[p] = tv[i]; i++; p++; }
}
static void reverse_vals(Value *a, long n)
{
    long i;
    for (i = 0; i < n / 2; i++) { Value t = a[i]; a[i] = a[n - 1 - i]; a[n - 1 - i] = t; }
}
/* sort list l in place with optional key function */
static void sort_list(List *l, Value key, int reverse)
{
    long n = l->n, i;
    List *kl, *t1, *t2;
    Value *base = vsp;
    if (n < 2) return;
    if (reverse) reverse_vals(l->a, n);
    t1 = list_new(n); t1->n = n;
    for (i = 0; i < n; i++) t1->a[i] = NONE_V;
    VPUSH(mklist(t1));
    t2 = list_new(n); t2->n = n;
    for (i = 0; i < n; i++) t2->a[i] = NONE_V;
    VPUSH(mklist(t2));
    if (key.t != T_NONE && key.t != T_UNSET) {
        kl = list_new(n); kl->n = n;
        for (i = 0; i < n; i++) kl->a[i] = NONE_V;
        VPUSH(mklist(kl));
        for (i = 0; i < n; i++) kl->a[i] = call1(key, l->a[i]);
        msort(kl->a, l->a, t1->a, t2->a, 0, n);
    } else {
        msort(l->a, l->a, t1->a, t2->a, 0, n);
    }
    if (reverse) reverse_vals(l->a, n);
    vsp = base;
}
BI(bi_sorted)
{
    Value lv;
    need(x, 1, 1, "sorted");
    lv = iter_to_list(A(0));
    VPUSH(lv);
    sort_list(AS_LIST(lv), kwget(x, "key"),
        py_truthy(kwget(x, "reverse").t == T_UNSET ? FALSE_V : kwget(x, "reverse")));
    vsp--;
    return lv;
}

/* ---- objects ---------------------------------------------------------- */
static int isinst(Value v, Value c)
{
    if (c.t == T_CLASS) {
        if (AS_CLASS(c)->flags & CF_NPABS) return npabs_has(AS_CLASS(c), class_of(v));
        return class_isa(class_of(v), AS_CLASS(c)) || AS_CLASS(c) == cls_object;
    }
    if (c.t == T_TUPLE) {
        long i;
        for (i = 0; i < AS_LIST(c)->n; i++) if (isinst(v, AS_LIST(c)->a[i])) return 1;
        return 0;
    }
    throw_new(cls_TypeError, "isinstance() arg 2 must be a type, a tuple of types, or a union");
    return 0;
}
BI(bi_isinstance)
{
    need(x, 2, 2, "isinstance");
    return mkbool(isinst(A(0), A(1)));
}
BI(bi_issubclass)
{
    need(x, 2, 2, "issubclass");
    if (A(0).t != T_CLASS) throw_new(cls_TypeError, "issubclass() arg 1 must be a class");
    if (A(1).t == T_CLASS && (AS_CLASS(A(1))->flags & CF_NPABS))
        return mkbool(npabs_has(AS_CLASS(A(1)), AS_CLASS(A(0))));
    if (A(1).t == T_CLASS) return mkbool(class_isa(AS_CLASS(A(0)), AS_CLASS(A(1))));
    if (A(1).t == T_TUPLE) {
        long i;
        for (i = 0; i < AS_LIST(A(1))->n; i++)
            if (AS_LIST(A(1))->a[i].t == T_CLASS
                && class_isa(AS_CLASS(A(0)), AS_CLASS(AS_LIST(A(1))->a[i])))
                return TRUE_V;
        return FALSE_V;
    }
    throw_new(cls_TypeError, "issubclass() arg 2 must be a class or tuple of classes");
    return NONE_V;
}
static Str *cur_gp(void);
/* type(name, bases, ns) with metaclass `meta` (NULL: type): the class is made, its namespace filled
   from ns, __init_subclass__ of the next base runs with the class keywords kw */
static Value type_create(Class *meta, Value namev, Value basesv, Value nsv, int nk, Value *kw)
{
    Class **bl, *c;
    List *bt;
    Dict *ns;
    long i, nb;
    Value cv;
    Str *nm = intern_str(astr(namev, "type.__new__() argument 1"));
    if (basesv.t != T_TUPLE)
        throw_new(cls_TypeError, "type.__new__() argument 2 must be tuple, not %.40s",
        type_name(basesv));
    if (nsv.t != T_DICT)
        throw_new(cls_TypeError, "type.__new__() argument 3 must be dict, not %.40s",
        type_name(nsv));
    bt = AS_LIST(basesv);
    ns = AS_DICT(nsv);
    bl = (Class **)xmalloc(sizeof(Class *) * (size_t)(bt->n + 1));
    nb = 0;
    for (i = 0; i < bt->n; i++) {
        if (bt->a[i].t != T_CLASS) { free(bl);
            throw_new(cls_TypeError, "bases must be types");
        }
        bl[nb++] = AS_CLASS(bt->a[i]);
    }
    if (nb == 0) bl[nb++] = cls_object;
    c = make_class(nm, bl, (int)nb, modname_of_gp(cur_gp()));
    if (meta && meta != cls_type) c->meta = meta;
    cv = mkobj(c, T_CLASS);
    VPUSH(cv);
    for (i = 0; i < ns->used; i++) {
        if (ns->e[i].k.t != T_STR) continue;
        amap_set(&c->ns, intern_str(AS_STR(ns->e[i].k)), ns->e[i].v);
    }
    finish_class(c);
    run_init_subclass(c, cv, nk, kw);
    vsp--;
    return cv;
}
BI(bi_type)
{
    need(x, 1, 3, "type");
    if (x->n == 2) throw_new(cls_TypeError, "type() takes 1 or 3 arguments");
    if (x->n == 3) return type_create(NULL, A(0), A(1), A(2), x->nk, x->kw);
    return mkobj(class_of(A(0)), T_CLASS);
}
/* type.__new__(mcs, name, bases, ns, **kw), type.__call__(cls, *args), type.__init__(...) */
BI(type_new)
{
    if (x->n != 4 || A(0).t != T_CLASS)
        throw_new(cls_TypeError, "type.__new__() takes exactly 4 arguments (%d given)", x->n);
    return type_create(AS_CLASS(A(0)), A(1), A(2), A(3), x->nk, x->kw);
}
BI(type_call)
{
    if (x->n < 1 || A(0).t != T_CLASS)
        throw_new(cls_TypeError, "descriptor '__call__' needs a type");
    return call_class_plain(AS_CLASS(A(0)), x->n - 1, x->a + 1, x->nk, x->kw);
}
BI(type_init)
{
    (void)x;
    return NONE_V;
}
/* object.__init_subclass__(cls): takes no keyword arguments */
BI(obj_init_subclass)
{
    if (x->nk > 0)
        throw_new(cls_TypeError, "%.80s.__init_subclass__() takes no keyword arguments",
            x->n > 0 && A(0).t == T_CLASS ? AS_CLASS(A(0))->name->s : "object");
    return NONE_V;
}
static int try_attr(Value o, Str *nm, Value *out)
{
    Handler h;
    Value *save = vsp;
    int depth = call_depth;
    Frame *fr = cur_frame;
    h.prev = cur_handler;
    h.sp = save;
    h.depth = depth;
    h.frame = fr;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        *out = get_attr(o, nm);
        cur_handler = h.prev;
        return 1;
    }
    cur_handler = h.prev;
    vsp = save;
    call_depth = depth;
    cur_frame = fr;
    if (class_isa(AS_INST(cur_exc)->cls, cls_AttributeError)) return 0;
    throw_val(cur_exc);
    return 0;
}
BI(bi_hasattr)
{
    Value r;
    need(x, 2, 2, "hasattr");
    return mkbool(try_attr(A(0), intern_str(astr(A(1), "attribute name")), &r));
}
BI(bi_getattr)
{
    Value r;
    need(x, 2, 3, "getattr");
    if (x->n == 3) {
        if (try_attr(A(0), intern_str(astr(A(1), "attribute name")), &r)) return r;
        return A(2);
    }
    return get_attr(A(0), intern_str(astr(A(1), "attribute name")));
}
BI(bi_setattr)
{
    need(x, 3, 3, "setattr");
    set_attr(A(0), intern_str(astr(A(1), "attribute name")), A(2));
    return NONE_V;
}
BI(bi_delattr)
{
    need(x, 2, 2, "delattr");
    del_attr(A(0), intern_str(astr(A(1), "attribute name")));
    return NONE_V;
}
BI(bi_callable)
{
    Value v;
    need(x, 1, 1, "callable");
    v = A(0);
    if (v.t == T_FUNC || v.t == T_BUILTIN || v.t == T_CLASS || v.t == T_BOUND) return TRUE_V;
    if (v.t == T_INST) return mkbool(class_find(AS_INST(v)->cls, S_call) != NULL);
    return FALSE_V;
}
BI(bi_id)
{
    Value v;
    need(x, 1, 1, "id");
    v = A(0);
    if (IS_HEAP(v.t)) return mkint((long)((size_t)v.u.o >> 2));
    if (v.t == T_FLOAT) return mkint((long)v.u.d + 7);
    return mkint(v.u.i * 4 + v.t);
}
BI(bi_hash)
{
    Value v;
    need(x, 1, 1, "hash");
    v = A(0);
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_hash);
        if (!m || m->t == T_BUILTIN) v = unpay(v);
    }
    switch (v.t) {
    case T_BOOL: case T_INT: case T_BIG: return int_hash_mod(v);
    case T_FLOAT: return float_hash_exact(v.u.d);
    case T_COMPLEX: return complex_hash_exact(AS_CPLX(v)->re, AS_CPLX(v)->im);
    case T_NONE: return int_add(int_mul(mkint(42388L), mkint(100000L)), mkint(94112L));
    case T_TUPLE: case T_SET: return mkint((long)py_hash(v));
    case T_INST: {
        Value *m = class_find(AS_INST(v)->cls, S_hash);
        if (m) {
            Value r = call1(*m, v);
            if (r.t == T_BOOL || r.t == T_INT || r.t == T_BIG) return user_hash_val(r);
            throw_new(cls_TypeError, "__hash__ method should return an integer");
        }
        break; }
    default: break;
    }
    return mkint((long)(py_hash(v) & 0x3fffffffUL));
}
BI(bi_format)
{
    need(x, 1, 2, "format");
    if (x->n == 1) return py_str(A(0));
    return format_value(A(0), astr(A(1), "format_spec")->s, AS_STR(A(1))->len);
}
BI(bi_object)
{
    Inst *in = new_inst(cls_object);
    (void)x;
    return mkobj(in, T_INST);
}
BI(bi_super)
{
    Super *s;
    need(x, 0, 2, "super");
    if (x->n < 2) throw_new(cls_RuntimeError, "super(): no arguments");
    if (A(0).t != T_CLASS) throw_new(cls_TypeError, "super() argument 1 must be a type");
    s = (Super *)gc_alloc(sizeof(Super), T_SUPER);
    s->start = AS_CLASS(A(0));
    s->obj = A(1);
    return mkobj(s, T_SUPER);
}
BI(bi_property)
{
    Value g = x->n > 0 ? A(0) : kwget(x, "fget"), s = x->n > 1 ? A(1) : kwget(x, "fset");
    return mk_prop(T_PROP, g.t == T_UNSET ? UNSET_V : g, s.t == T_UNSET ? UNSET_V : s);
}
BI(bi_propsetter)
{
    Prop *p;
    need(x, 2, 2, "setter");
    p = (Prop *)A(0).u.o;
    {
        Value r = mk_prop(T_PROP, p->get, A(1));
        ((Prop *)r.u.o)->del = p->del;
        return r;
    }
}
BI(bi_propdeleter)
{
    Prop *p;
    Value r;
    need(x, 2, 2, "deleter");
    p = (Prop *)A(0).u.o;
    r = mk_prop(T_PROP, p->get, p->set);
    ((Prop *)r.u.o)->del = A(1);
    return r;
}
BI(bi_propgetter)
{
    Prop *p;
    Value r;
    need(x, 2, 2, "getter");
    p = (Prop *)A(0).u.o;
    r = mk_prop(T_PROP, A(1), p->set);
    ((Prop *)r.u.o)->del = p->del;
    return r;
}
BI(bi_staticmethod) { need(x, 1, 1, "staticmethod"); return mk_prop(T_SMETH, A(0), UNSET_V); }
BI(bi_classmethod) { need(x, 1, 1, "classmethod"); return mk_prop(T_CMETH, A(0), UNSET_V); }
BI(bi_exit_fn)
{
    Value a0 = x->n > 0 ? A(0) : NONE_V;
    throw_val(new_exc_args(cls_SystemExit, &a0, x->n > 0 ? 1 : 0));
    return NONE_V;
}

/* ---- exceptions ------------------------------------------------------- */
BI(exc_init)
{
    if (x->n < 1 || A(0).t != T_INST)
        throw_new(cls_TypeError, "descriptor '__init__' requires an exception");
    amap_set(&AS_INST(A(0))->a, S_args, mktuple_from(x->a + 1, x->n - 1));
    if (class_isa(AS_INST(A(0))->cls, cls_OSError)) {
        int two = x->n >= 3 && A(1).t == T_INT;
        amap_set(&AS_INST(A(0))->a, intern_c("errno"), two ? A(1) : NONE_V);
        amap_set(&AS_INST(A(0))->a, intern_c("strerror"), two ? A(2) : NONE_V);
        amap_set(&AS_INST(A(0))->a, intern_c("filename"), two && x->n >= 4 ? A(3) : NONE_V);
    }
    return NONE_V;
}
BI(exc_str)
{
    Buf b;
    buf_init(&b);
    exc_str_buf(AS_INST(A(0)), &b, 0);
    return buf_value(&b);
}
BI(exc_repr)
{
    Buf b;
    buf_init(&b);
    exc_str_buf(AS_INST(A(0)), &b, 1);
    return buf_value(&b);
}
/* ---- BaseExceptionGroup / ExceptionGroup ------------------------------------------ */
static int eg_is_group(Value v)
{
    return v.t == T_INST && class_isa(AS_INST(v)->cls, cls_BaseExceptionGroup);
}
static Value eg_attr(Value eg, const char *nm)
{
    Value *p = amap_get(&AS_INST(eg)->a, intern_c(nm));
    return p ? *p : NONE_V;
}
/* validate (message, exceptions); returns the exceptions as a tuple (all Exception: *all_exc) */
static Value eg_check(Class *c, Value msg, Value seq, int *all_exc)
{
    long i;
    List *l;
    if (msg.t != T_STR)
        throw_new(cls_TypeError, "argument 1 must be str, not %.40s", type_name(msg));
    if (seq.t != T_LIST && seq.t != T_TUPLE)
        throw_new(cls_TypeError, "second argument (exceptions) must be a sequence");
    l = AS_LIST(seq);
    if (l->n == 0)
        throw_new(cls_ValueError, "second argument (exceptions) must be a non-empty sequence");
    *all_exc = 1;
    for (i = 0; i < l->n; i++) {
        Value e = l->a[i];
        if (e.t != T_INST || !(AS_INST(e)->cls->flags & CF_EXC))
            throw_new(cls_ValueError,
                "Item %ld of second argument (exceptions) is not an exception",
                i);
        if (!class_isa(AS_INST(e)->cls, cls_Exception)) *all_exc = 0;
    }
    if (c && class_isa(c, cls_ExceptionGroup) && !*all_exc)
        throw_new(cls_TypeError, "Cannot nest BaseExceptions in an ExceptionGroup");
    return mktuple_from(l->a, l->n);
}
static void eg_fill(Value iv, Value msg, Value seq, Value tup)
{
    amap_set(&AS_INST(iv)->a, intern_c("message"), msg);
    amap_set(&AS_INST(iv)->a, intern_c("exceptions"), tup);
    amap_set(&AS_INST(iv)->a, S_args, mktuple2(msg, seq));
}
static Value eg_new(Class *c, Value msg, Value seq)
{
    int all;
    Value tup, iv;
    Value *base = vsp;
    tup = eg_check(c, msg, seq, &all);
    VPUSH(tup);
    if (c == cls_BaseExceptionGroup && all) c = cls_ExceptionGroup;
    iv = mkobj(new_inst(c), T_INST);
    VPUSH(iv);
    eg_fill(iv, msg, seq, tup);
    vsp = base;
    return iv;
}
BI(beg_ctor)
{
    need(x, 2, 2, "BaseExceptionGroup");
    return eg_new(cls_BaseExceptionGroup, A(0), A(1));
}
BI(eg_ctor)
{
    need(x, 2, 2, "ExceptionGroup");
    return eg_new(cls_ExceptionGroup, A(0), A(1));
}
BI(beg_init)
{
    int all;
    Value tup;
    if (x->n != 3 || A(0).t != T_INST)
        throw_new(cls_TypeError, "BaseExceptionGroup.__init__ takes exactly 2 arguments");
    tup = eg_check(AS_INST(A(0))->cls, A(1), A(2), &all);
    VPUSH(tup);
    eg_fill(A(0), A(1), A(2), tup);
    vsp--;
    return NONE_V;
}
BI(beg_str)
{
    Value ex = eg_attr(A(0), "exceptions");
    Buf b;
    char t[48];
    long n = ex.t == T_TUPLE ? AS_LIST(ex)->n : 0;
    buf_init(&b);
    str_buf(eg_attr(A(0), "message"), &b);
    sprintf(t, " (%ld sub-exception%s)", n, n == 1 ? "" : "s");
    buf_add(&b, t);
    return buf_value(&b);
}
BI(beg_derive)
{
    need(x, 2, 2, "derive");
    if (A(1).t != T_LIST && A(1).t != T_TUPLE)
        throw_new(cls_TypeError, "second argument (exceptions) must be a sequence");
    return eg_new(cls_BaseExceptionGroup, eg_attr(A(0), "message"), A(1));
}
static int eg_cond(Value cond, Value exc)
{
    if (cond.t == T_CLASS || cond.t == T_TUPLE) return isinst(exc, cond);
    return py_truthy(call1(cond, exc));
}
static Value eg_derive(Value eg, List *items)
{
    Value *base = vsp, fn, lv, r;
    List *l = list_new(items->n);
    long i;
    for (i = 0; i < items->n; i++) list_push(l, items->a[i]);
    lv = mklist(l);
    VPUSH(lv);
    fn = get_attr(eg, intern_c("derive"));
    VPUSH(fn);
    r = call1(fn, lv);
    vsp = base;
    return r;
}
/* split an exception (group or not) by cond into *m and *r (NONE_V when empty) */
static void eg_split(Value eg, Value cond, Value *m, Value *r)
{
    Value *base = vsp;
    VPUSH(eg);
    if (eg_cond(cond, eg)) { *m = eg; *r = NONE_V; vsp = base; return; }
    if (!eg_is_group(eg)) { *m = NONE_V; *r = eg; vsp = base; return; }
    {
        Value ex = eg_attr(eg, "exceptions"), mv, rv;
        List *ml = list_new(4), *rl = list_new(4);
        long i;
        VPUSH(mklist(ml));
        VPUSH(mklist(rl));
        for (i = 0; ex.t == T_TUPLE && i < AS_LIST(ex)->n; i++) {
            Value a, b;
            eg_split(AS_LIST(ex)->a[i], cond, &a, &b);
            if (a.t != T_NONE) list_push(ml, a);
            if (b.t != T_NONE) list_push(rl, b);
        }
        if (ml->n == 0) { *m = NONE_V; *r = eg; vsp = base; return; }
        if (rl->n == 0) { *m = eg; *r = NONE_V; vsp = base; return; }
        mv = eg_derive(eg, ml);
        VPUSH(mv);
        rv = rl->n ? eg_derive(eg, rl) : NONE_V;
        *m = mv; *r = rv;
    }
    vsp = base;
}
BI(beg_split)
{
    Value m, r;
    need(x, 2, 2, "split");
    eg_split(A(0), A(1), &m, &r);
    return mktuple2(m, r);
}
BI(beg_subgroup)
{
    Value m, r;
    need(x, 2, 2, "subgroup");
    eg_split(A(0), A(1), &m, &r);
    return m;
}
static void eg_init_classes(void)
{
    Class **bl;
    cls_BaseExceptionGroup = new_class("BaseExceptionGroup", cls_BaseException,
        CF_BUILTIN | CF_EXC);
    bl = (Class **)xmalloc(sizeof(Class *) * 2);
    bl[0] = cls_BaseExceptionGroup;
    bl[1] = cls_Exception;
    cls_ExceptionGroup = make_class(intern_c("ExceptionGroup"), bl, 2, NULL);
    cls_ExceptionGroup->flags |= CF_BUILTIN;
    set_global(intern_c("ExceptionGroup"), mkobj(cls_ExceptionGroup, T_CLASS));
    cls_BaseExceptionGroup->ctor = beg_ctor;
    cls_ExceptionGroup->ctor = eg_ctor;
    def_bi(cls_BaseExceptionGroup, "__init__", beg_init);
    def_bi(cls_BaseExceptionGroup, "__str__", beg_str);
    def_bi(cls_BaseExceptionGroup, "derive", beg_derive);
    def_bi(cls_BaseExceptionGroup, "split", beg_split);
    def_bi(cls_BaseExceptionGroup, "subgroup", beg_subgroup);
}
BI(obj_init)
{
    (void)x;
    return NONE_V;
}
BI(obj_repr)
{
    Buf b;
    char t[64];
    Value v = A(0);
    buf_init(&b);
    if (v.t == T_INST) {
        buf_add(&b, "<__main__.");
        buf_add(&b, AS_INST(v)->cls->name->s);
        sprintf(t, " object at 0x%lx>", (unsigned long)(size_t)v.u.o);
        buf_add(&b, t);
    } else {
        repr_buf(v, &b, 0);
    }
    return buf_value(&b);
}
BI(obj_str)
{
    return py_repr(A(0));
}
BI(obj_eq)
{
    return mkbool(identical(A(0), A(1)));
}
BI(obj_ne)
{
    return mkbool(!identical(A(0), A(1)));
}
BI(obj_hash)
{
    return mkint((long)(((unsigned long)(size_t)A(0).u.o >> 3) & 0x3fffffffUL));
}

/* ---- string methods --------------------------------------------------- */
#define SELF astr(A(0), "self")
static int ws(int c) { return ch_space[(unsigned char)c]; }

/* two-byte UTF-8 case mapping (Latin-1, Greek, Cyrillic basics); returns 1 if mapped */
static int u8_case(unsigned char *p, int up)
{
    unsigned char c = p[0], d = p[1];
    if (up) {
        if (c == 0xC3 && d >= 0xA0 && d <= 0xBE && d != 0xB7) { p[1] = (unsigned char)(d - 0x20);
            return 1;
        }
        if (c == 0xC3 && d == 0xBF) { p[0] = 0xC5; p[1] = 0xB8; return 1; }
        if (c == 0xCE && d >= 0xB1 && d <= 0xBF) { p[1] = (unsigned char)(d - 0x20); return 1; }
        if (c == 0xCF && d >= 0x80 && d <= 0x89 && d != 0x82) {
            p[0] = (unsigned char)(d >= 0x80 && d <= 0x89 ? 0xCE : c);
            p[1] = (unsigned char)(d + 0x20); return 1; }
        if (c == 0xD0 && d >= 0xB0 && d <= 0xBF) { p[1] = (unsigned char)(d - 0x20); return 1; }
        if (c == 0xD1 && d >= 0x80 && d <= 0x8F) { p[0] = 0xD0;
            p[1] = (unsigned char)(d + 0x20);
            return 1;
        }
    } else {
        if (c == 0xC3 && d >= 0x80 && d <= 0x9E && d != 0x97) { p[1] = (unsigned char)(d + 0x20);
            return 1;
        }
        if (c == 0xCE && d >= 0x91 && d <= 0x9F) { p[1] = (unsigned char)(d + 0x20); return 1; }
        if (c == 0xCE && d >= 0xA0 && d <= 0xA9 && d != 0xA2) {
            p[0] = 0xCF; p[1] = (unsigned char)(d - 0x20); return 1; }
        if (c == 0xD0 && d >= 0x90 && d <= 0x9F) { p[1] = (unsigned char)(d + 0x20); return 1; }
        if (c == 0xD0 && d >= 0xA0 && d <= 0xAF) { p[0] = 0xD1;
            p[1] = (unsigned char)(d - 0x20);
            return 1;
        }
    }
    return 0;
}
static Value str_case(Str *s, int up)
{
    Value r = mkstr(s->s, s->len);
    unsigned char *p = (unsigned char *)AS_STR(r)->s;
    long i;
    for (i = 0; i < s->len; i++) {
        if (p[i] >= 0xC3 && p[i] <= 0xD1 && i + 1 < s->len && (p[i + 1] & 0xC0) == 0x80 &&
            u8_case(p + i, up)) { i++; continue; }
        p[i] = up ? to_up[p[i]] : to_low[p[i]];
    }
    return r;
}
BI(s_upper) { return str_case(SELF, 1); }
BI(s_lower) { return str_case(SELF, 0); }
BI(bi_ascii)
{
    Value r;
    Buf b;
    Str *s;
    long i;
    int k;
    char t[40];
    need(x, 1, 1, "ascii");
    r = py_repr(A(0));
    s = AS_STR(r);
    buf_init(&b);
    for (i = 0; i < s->len; i += k) {
        unsigned long c = u8_next((const unsigned char *)s->s, i, s->len, &k);
        if (c < 128UL) buf_addc(&b, (int)c);
        else {
            if (c < 256UL) sprintf(t, "\\x%02lx", c);
            else if (c < 65536UL) sprintf(t, "\\u%04lx", c);
            else sprintf(t, "\\U%08lx", c);
            buf_add(&b, t);
        }
    }
    r = buf_value(&b);
    return r;
}
/* str.translate(table): table maps ordinals to a str, an int, or None (delete) */
BI(s_translate)
{
    Str *s = SELF;
    Value tab, res, v;
    Buf b;
    long i;
    int k;
    need(x, 2, 2, "translate");
    tab = A(1);
    buf_init(&b);
    for (i = 0; i < s->len; i += k) {
        unsigned long c = u8_next((const unsigned char *)s->s, i, s->len, &k);
        int have = 0;
        if (tab.t == T_DICT) have = dict_get(AS_DICT(tab), mkint((long)c), &v);
        else if (tab.t == T_LIST || tab.t == T_TUPLE) {
            if ((long)c < AS_LIST(tab)->n) { v = AS_LIST(tab)->a[c]; have = 1; }
        } else if (tab.t == T_STR) {
            long q = 0, pos = 0;
            int kk;
            const unsigned char *ts = (const unsigned char *)AS_STR(tab)->s;
            long tn = AS_STR(tab)->len;
            while (pos < tn && (unsigned long)q < c) {
                (void)u8_next(ts, pos, tn, &kk); pos += kk; q++;
            }
            if (pos < tn && (unsigned long)q == c) {
                (void)u8_next(ts, pos, tn, &kk);
                v = mkstr(AS_STR(tab)->s + pos, kk);
                have = 1;
            }
        } else
            throw_new(cls_TypeError, "translate table must be a mapping or sequence");
        if (!have || v.t == T_UNSET) { buf_addn(&b, s->s + i, k); continue; }
        if (v.t == T_NONE) continue;
        if (v.t == T_INT) {
            if (v.u.i < 0 || v.u.i > 0x10FFFFL)
                throw_new(cls_ValueError, "character mapping must be in range(0x110000)");
            put_utf8(&b, (unsigned long)v.u.i);
        } else if (v.t == T_STR) buf_addn(&b, AS_STR(v)->s, AS_STR(v)->len);
        else throw_new(cls_TypeError, "character mapping must return integer, None or str");
    }
    res = buf_value(&b);
    return res;
}
BI(s_maketrans)
{
    Dict *d = dict_new();
    Value dv = mkobj(d, T_DICT);
    int off = (x->n > 0 && A(0).t == T_CLASS) ? 1 : 0;
    Args a2 = *x;
    a2.n = x->n - off; a2.a = x->a + off;
    x = &a2;
    VPUSH(dv);
    if (x->n == 1) {
        Dict *src;
        long i;
        if (A(0).t != T_DICT)
            throw_new(cls_TypeError,
                      "if you give only one argument to maketrans it must be a dict");
        src = AS_DICT(A(0));
        for (i = 0; i < src->used; i++) {
            Value k = src->e[i].k;
            if (k.t == T_UNSET) continue;
            if (k.t == T_STR) {
                int kk;
                if (AS_STR(k)->len == 0 ||
                    (u8_next((const unsigned char *)AS_STR(k)->s, 0, AS_STR(k)->len, &kk),
                     kk != AS_STR(k)->len))
                    throw_new(cls_ValueError, "string keys in translate table must be of length 1");
                k = mkint((long)u8_next((const unsigned char *)AS_STR(k)->s, 0,
                                        AS_STR(k)->len, &kk));
            }
            dict_set(d, k, src->e[i].v);
        }
    } else if (x->n >= 2) {
        Str *f = astr(A(0), "maketrans"), *t = astr(A(1), "maketrans");
        long i = 0, j = 0;
        int k1, k2;
        while (i < f->len && j < t->len) {
            unsigned long a = u8_next((const unsigned char *)f->s, i, f->len, &k1);
            unsigned long b = u8_next((const unsigned char *)t->s, j, t->len, &k2);
            dict_set(d, mkint((long)a), mkint((long)b));
            i += k1; j += k2;
        }
        if (i < f->len || j < t->len)
            throw_new(cls_ValueError, "the first two maketrans arguments must have equal length");
        if (x->n == 3) {
            Str *z = astr(A(2), "maketrans");
            long q = 0;
            int kk;
            while (q < z->len) {
                dict_set(d, mkint((long)u8_next((const unsigned char *)z->s, q, z->len, &kk)),
                         NONE_V);
                q += kk;
            }
        }
    } else
        throw_new(cls_TypeError, "maketrans expected at least 1 argument, got 0");
    vsp--;
    return dv;
}
BI(s_swapcase)
{
    Str *s = SELF;
    Value r = mkstr(s->s, s->len);
    long i;
    for (i = 0; i < s->len; i++) {
        int c = (unsigned char)s->s[i];
        AS_STR(r)->s[i] = (char)(ch_upper[c] ? to_low[c] : to_up[c]);
    }
    return r;
}
BI(s_capitalize)
{
    Str *s = SELF;
    Value r = mkstr(s->s, s->len);
    long i;
    for (i = 0; i < s->len; i++)
        AS_STR(r)->s[i]
            = (char)(i == 0 ? to_up[(unsigned char)s->s[i]] : to_low[(unsigned char)s->s[i]]);
    return r;
}
BI(s_title)
{
    Str *s = SELF;
    Value r = mkstr(s->s, s->len);
    long i;
    int prev = 0;
    for (i = 0; i < s->len; i++) {
        int c = (unsigned char)s->s[i];
        AS_STR(r)->s[i] = (char)(prev ? to_low[c] : to_up[c]);
        prev = ch_alpha[c] && c != '_';
    }
    return r;
}
static Value strip_impl(Args *x, int left, int right)
{
    Str *s = SELF;
    const char *chars = NULL;
    long clen = 0, a = 0, b = s->len;
    if (x->n > 1 && A(1).t == T_STR) { chars = AS_STR(A(1))->s; clen = AS_STR(A(1))->len; }
#define STRIPC(c) (chars ? memchr(chars, (c), (size_t)clen) != NULL : ws(c))
    if (left) while (a < b && STRIPC((unsigned char)s->s[a])) a++;
    if (right) while (b > a && STRIPC((unsigned char)s->s[b - 1])) b--;
#undef STRIPC
    return mkstr(s->s + a, b - a);
}
BI(s_strip) { return strip_impl(x, 1, 1); }
BI(s_lstrip) { return strip_impl(x, 1, 0); }
BI(s_rstrip) { return strip_impl(x, 0, 1); }

static long find_sub(Str *s, Str *u, long start, long end, int rev)
{
    long i;
    if (end > s->len) end = s->len;
    if (start < 0) start = 0;
    if (u->len > end - start) return -1;
    if (!rev) {
        for (i = start; i + u->len <= end; i++)
            if (memcmp(s->s + i, u->s, (size_t)u->len) == 0) return i;
    } else {
        for (i = end - u->len; i >= start; i--)
            if (memcmp(s->s + i, u->s, (size_t)u->len) == 0) return i;
    }
    return -1;
}
/* a slice-style bound: any integer, clamped to +-SI_MAX like CPython clamps to +-PY_SSIZE_T_MAX */
static long to_slice_index(Value v)
{
    return slice_part(need_intv(v, "slice indices"));
}
/* a count or position that CPython holds in a C ssize_t: huge values are an OverflowError,
   anything beyond this build's small range is clamped */
static long to_ssize(Value v)
{
    Value b = need_intv(v, "argument");
    if (b.t == T_BIG && (int_cmp(b, int_pow2(63)) >= 0 || int_cmp(b, int_neg(int_pow2(63))) < 0))
        throw_new(cls_OverflowError, "Python int too large to convert to C ssize_t");
    return slice_part(b);
}
static void se_range(Args *x, int first, long len, long *st, long *en)
{
    *st = 0; *en = len;
    if (x->n > first && A(first).t != T_NONE) {
        *st = to_slice_index(A(first));
        if (*st < 0) { *st += len; if (*st < 0) *st = 0; }
    }
    if (x->n > first + 1 && A(first + 1).t != T_NONE) {
        *en = to_slice_index(A(first + 1));
        if (*en < 0) { *en += len; if (*en < 0) *en = 0; }
        if (*en > len) *en = len;
    }
}
static Value find_impl(Args *x, int rev, int raise, const char *nm)
{
    Str *s = SELF, *u;
    long st, en, r;
    need(x, 2, 4, nm);
    u = astr(A(1), "argument");
    se_range(x, 2, s->len, &st, &en);
    r = find_sub(s, u, st, en, rev);
    if (r < 0 && raise) throw_new(cls_ValueError, "substring not found");
    return mkint(r);
}
BI(s_find) { return find_impl(x, 0, 0, "find"); }
BI(s_rfind) { return find_impl(x, 1, 0, "rfind"); }
BI(s_index) { return find_impl(x, 0, 1, "index"); }
BI(s_rindex) { return find_impl(x, 1, 1, "rindex"); }
BI(s_count)
{
    Str *s = SELF, *u;
    long st, en, n = 0, i;
    need(x, 2, 4, "count");
    u = astr(A(1), "argument");
    se_range(x, 2, s->len, &st, &en);
    if (u->len == 0) return mkint(st > en || st > s->len ? 0 : en - st + 1);
    for (i = st; i + u->len <= en; ) {
        if (memcmp(s->s + i, u->s, (size_t)u->len) == 0) { n++; i += u->len; }
        else i++;
    }
    return mkint(n);
}
static Value startend(Args *x, int atend)
{
    Str *s = SELF;
    long st, en;
    Value p;
    need(x, 2, 4, atend ? "endswith" : "startswith");
    p = A(1);
    se_range(x, 2, s->len, &st, &en);
    if (p.t == T_TUPLE) {
        long i;
        for (i = 0; i < AS_LIST(p)->n; i++) {
            Str *u = astr(AS_LIST(p)->a[i], "tuple item");
            if (en - st >= u->len
                && memcmp(s->s + (atend ? en - u->len : st), u->s, (size_t)u->len) == 0)
                return TRUE_V;
        }
        return FALSE_V;
    }
    {
        Str *u = astr(p, "startswith first arg");
        if (en - st < u->len) return FALSE_V;
        return mkbool(memcmp(s->s + (atend ? en - u->len : st), u->s, (size_t)u->len) == 0);
    }
}
BI(s_startswith) { return startend(x, 0); }
BI(s_endswith) { return startend(x, 1); }

static Value split_impl(Args *x, int rev)
{
    Str *s = SELF;
    Value sepv = x->n > 1 ? A(1) : kwget(x, "sep"), mv = x->n > 2 ? A(2) : kwget(x, "maxsplit");
    long maxs = -1, i, j, n = s->len;
    List *l = list_new(8);
    Value lv = mklist(l);
    VPUSH(lv);
    if (mv.t != T_UNSET) maxs = to_ssize(mv);
    if (sepv.t == T_UNSET || sepv.t == T_NONE) {
        if (!rev) {
            i = 0;
            while (1) {
                while (i < n && ws((unsigned char)s->s[i])) i++;
                if (i >= n) break;
                if (maxs >= 0 && l->n >= maxs) {
                    list_push(l, mkstr(s->s + i, n - i));
                    break;
                }
                j = i;
                while (j < n && !ws((unsigned char)s->s[j])) j++;
                list_push(l, mkstr(s->s + i, j - i));
                i = j;
            }
        } else {
            long e = n, cnt = 0;
            List *t = list_new(8);
            Value tv = mklist(t);
            VPUSH(tv);
            while (1) {
                while (e > 0 && ws((unsigned char)s->s[e - 1])) e--;
                if (e <= 0) break;
                if (maxs >= 0 && cnt >= maxs) {
                    list_push(t, mkstr(s->s, e));
                    break;
                }
                j = e;
                while (j > 0 && !ws((unsigned char)s->s[j - 1])) j--;
                list_push(t, mkstr(s->s + j, e - j));
                cnt++;
                e = j;
            }
            for (i = t->n - 1; i >= 0; i--) list_push(l, t->a[i]);
            vsp--;
        }
    } else {
        Str *sep = astr(sepv, "sep");
        if (sep->len == 0) throw_new(cls_ValueError, "empty separator");
        if (!rev) {
            i = 0;
            while (1) {
                long f = (maxs >= 0 && l->n >= maxs) ? -1 : find_sub(s, sep, i, n, 0);
                if (f < 0) { list_push(l, mkstr(s->s + i, n - i)); break; }
                list_push(l, mkstr(s->s + i, f - i));
                i = f + sep->len;
            }
        } else {
            List *t = list_new(8);
            Value tv = mklist(t);
            long e = n, cnt = 0;
            VPUSH(tv);
            while (1) {
                long f = (maxs >= 0 && cnt >= maxs) ? -1 : find_sub(s, sep, 0, e, 1);
                if (f < 0) { list_push(t, mkstr(s->s, e)); break; }
                list_push(t, mkstr(s->s + f + sep->len, e - f - sep->len));
                e = f;
                cnt++;
            }
            for (i = t->n - 1; i >= 0; i--) list_push(l, t->a[i]);
            vsp--;
        }
    }
    vsp--;
    return lv;
}
BI(s_split) { return split_impl(x, 0); }
BI(s_rsplit) { return split_impl(x, 1); }
BI(s_splitlines)
{
    Str *s = SELF;
    List *l = list_new(8);
    Value lv = mklist(l);
    long i = 0, st = 0;
    Value kv = kwget(x, "keepends");
    int keep = (x->n > 1 && py_truthy(A(1))) || (kv.t != T_UNSET && py_truthy(kv));
    VPUSH(lv);
    while (i < s->len) {
        if (s->s[i] == '\n' || s->s[i] == '\r') {
            long e = i;
            if (s->s[i] == '\r' && i + 1 < s->len && s->s[i + 1] == '\n') i++;
            i++;
            list_push(l, mkstr(s->s + st, (keep ? i : e) - st));
            st = i;
        } else i++;
    }
    if (st < s->len) list_push(l, mkstr(s->s + st, s->len - st));
    vsp--;
    return lv;
}
BI(s_join)
{
    Str *sep = SELF;
    Value lv, r;
    List *l;
    Buf b;
    long i;
    need(x, 2, 2, "join");
    lv = iter_to_list(A(1));
    VPUSH(lv);
    l = AS_LIST(lv);
    buf_init(&b);
    for (i = 0; i < l->n; i++) {
        if (l->a[i].t != T_STR) {
            buf_free(&b);
            throw_new(cls_TypeError, "sequence item %ld: expected str instance, %.40s found", i,
                type_name(l->a[i]));
        }
        if (i) buf_addn(&b, sep->s, sep->len);
        buf_addn(&b, AS_STR(l->a[i])->s, AS_STR(l->a[i])->len);
    }
    r = buf_value(&b);
    vsp--;
    return r;
}
BI(s_replace)
{
    Str *s = SELF, *o, *nw;
    long cnt = -1, i = 0, f;
    Buf b;
    need(x, 3, 4, "replace");
    o = astr(A(1), "old");
    nw = astr(A(2), "new");
    if (x->n > 3) cnt = to_index(A(3));
    buf_init(&b);
    if (o->len == 0) {
        long k = 0;
        for (i = 0; i <= s->len; i++) {
            if (cnt < 0 || k < cnt) { buf_addn(&b, nw->s, nw->len); k++; }
            if (i < s->len) buf_addc(&b, s->s[i]);
        }
        return buf_value(&b);
    }
    while (1) {
        f = cnt == 0 ? -1 : find_sub(s, o, i, s->len, 0);
        if (f < 0) break;
        buf_addn(&b, s->s + i, f - i);
        buf_addn(&b, nw->s, nw->len);
        i = f + o->len;
        if (cnt > 0) cnt--;
    }
    buf_addn(&b, s->s + i, s->len - i);
    return buf_value(&b);
}
static int all_chars(Str *s, const unsigned char *tab)
{
    long i;
    if (s->len == 0) return 0;
    for (i = 0; i < s->len; i++) if (!tab[(unsigned char)s->s[i]]) return 0;
    return 1;
}
BI(s_isalpha) {
    return mkbool(all_chars(SELF, ch_alpha) && !memchr(SELF->s, '_', (size_t)SELF->len));
}
BI(s_isdigit) { return mkbool(all_chars(SELF, ch_digit)); }
BI(s_isalnum) {
    return mkbool(all_chars(SELF, ch_alnum) && !memchr(SELF->s, '_', (size_t)SELF->len));
}
BI(s_isspace) { return mkbool(all_chars(SELF, ch_space)); }
BI(s_isupper)
{
    Str *s = SELF;
    long i;
    int any = 0;
    for (i = 0; i < s->len; i++) {
        if (ch_lower[(unsigned char)s->s[i]]) return FALSE_V;
        if (ch_upper[(unsigned char)s->s[i]]) any = 1;
    }
    return mkbool(any);
}
BI(s_islower)
{
    Str *s = SELF;
    long i;
    int any = 0;
    for (i = 0; i < s->len; i++) {
        if (ch_upper[(unsigned char)s->s[i]]) return FALSE_V;
        if (ch_lower[(unsigned char)s->s[i]]) any = 1;
    }
    return mkbool(any);
}
static Value justify(Args *x, int mode)
{
    Str *s = SELF;
    long w, pad, i, l;
    int fill = ' ';
    Buf b;
    need(x, 2, 3, "ljust");
    w = to_index(A(1));
    if (x->n > 2) { Str *f = astr(A(2), "fillchar");
        if (f->len != 1)
            throw_new(cls_TypeError, "The fill character must be exactly one character long");
        fill = f->s[0];
    }
    pad = w - s->len;
    if (pad <= 0) return A(0);
    buf_init(&b);
    l = mode == 0 ? 0 : (mode == 1 ? pad : pad / 2);
    if (mode == 2) l = pad / 2 + (pad & w & 1);
    for (i = 0; i < l; i++) buf_addc(&b, fill);
    buf_addn(&b, s->s, s->len);
    for (i = 0; i < pad - l; i++) buf_addc(&b, fill);
    return buf_value(&b);
}
BI(s_ljust) { return justify(x, 0); }
BI(s_rjust) { return justify(x, 1); }
BI(s_center) { return justify(x, 2); }
BI(s_zfill)
{
    Str *s = SELF;
    long w = to_index(A(1)), pad = w - s->len, i;
    Buf b;
    if (pad <= 0) return A(0);
    buf_init(&b);
    i = 0;
    if (s->len > 0 && (s->s[0] == '-' || s->s[0] == '+')) { buf_addc(&b, s->s[0]); i = 1; }
    for (; pad > 0; pad--) buf_addc(&b, '0');
    buf_addn(&b, s->s + i, s->len - i);
    return buf_value(&b);
}
static Value partition_impl(Args *x, int rev)
{
    Str *s = SELF, *sep = astr(A(1), "sep");
    long f;
    Value t[3];
    Value *base = vsp;
    if (sep->len == 0) throw_new(cls_ValueError, "empty separator");
    f = find_sub(s, sep, 0, s->len, rev);
    if (f < 0) {
        t[0] = rev ? mkstr("", 0) : A(0);
        VPUSH(t[0]);
        t[1] = mkstr("", 0);
        VPUSH(t[1]);
        t[2] = rev ? A(0) : mkstr("", 0);
    } else {
        t[0] = mkstr(s->s, f);
        VPUSH(t[0]);
        t[1] = A(1);
        t[2] = mkstr(s->s + f + sep->len, s->len - f - sep->len);
    }
    vsp = base;
    return mktuple_from(t, 3);
}
BI(s_partition) { return partition_impl(x, 0); }
BI(s_rpartition) { return partition_impl(x, 1); }
BI(s_format)
{
    return format_template(SELF, x->n - 1, x->a + 1, x->nk, x->kw, 0);
}
BI(s_encode)
{
    Str *s = SELF;
    Value ev = x->n > 1 ? A(1) : kwget(x, "encoding");
    Value errv = x->n > 2 ? A(2) : kwget(x, "errors");
    const char *enc = "utf-8", *err = "strict";
    long i;
    Buf b;
    if (ev.t == T_STR) enc = AS_STR(ev)->s;
    if (errv.t == T_STR) err = AS_STR(errv)->s;
    if (strcmp(enc, "utf-8") == 0 || strcmp(enc, "utf8") == 0 || strcmp(enc, "UTF-8") == 0)
        return mkbytes(s->s, s->len);
    buf_init(&b);
    for (i = 0; i < s->len; ) {
        unsigned long c = (unsigned char)s->s[i];
        int n = 1;
        if (c >= 0xF0) { n = 4; c &= 7; } else if (c >= 0xE0) { n = 3; c &= 15; }
        else if (c >= 0xC0) { n = 2; c &= 31; }
        if (n > 1) { int k;
            for (k = 1; k < n && i + k < s->len; k++) c = (c << 6)
                | ((unsigned char)s->s[i + k] & 63);
        }
        i += n;
        if (c < 128 || (c < 256 && (strcmp(enc, "latin-1") == 0 || strcmp(enc, "latin1") == 0 ||
                                    strcmp(enc, "iso-8859-1") == 0))) buf_addc(&b, (int)c);
        else if (strcmp(err, "ignore") == 0) { }
        else if (strcmp(err, "replace") == 0) buf_addc(&b, '?');
        else {
            buf_free(&b);
            throw_new(cls_ValueError, "'%s' codec can't encode character", enc);
        }
    }
    { Value r = mkbytes(b.s, b.n); buf_free(&b); return r; }
}
BI(s_removeprefix)
{
    Str *s = SELF, *p = astr(A(1), "prefix");
    if (s->len >= p->len && memcmp(s->s, p->s, (size_t)p->len) == 0)
        return mkstr(s->s + p->len, s->len - p->len);
    return A(0);
}
BI(s_removesuffix)
{
    Str *s = SELF, *p = astr(A(1), "suffix");
    if (p->len > 0 && s->len >= p->len && memcmp(s->s + s->len - p->len, p->s, (size_t)p->len) == 0)
        return mkstr(s->s, s->len - p->len);
    return A(0);
}
BI(s_expandtabs)
{
    Str *s = SELF;
    long tab = x->n > 1 ? to_index(A(1)) : 8, i, col = 0;
    Buf b;
    buf_init(&b);
    for (i = 0; i < s->len; i++) {
        if (s->s[i] == '\t') {
            long k = tab > 0 ? tab - col % tab : 0;
            while (k-- > 0) { buf_addc(&b, ' '); col++; }
        } else {
            buf_addc(&b, s->s[i]);
            col = s->s[i] == '\n' ? 0 : col + 1;
        }
    }
    return buf_value(&b);
}

/* ---- list methods ----------------------------------------------------- */
static List *self_list(Args *x)
{
    if (A(0).t != T_LIST)
        throw_new(cls_TypeError, "descriptor requires a 'list' object but received '%.40s'",
        type_name(A(0)));
    return AS_LIST(A(0));
}
BI(l_append)
{
    need(x, 2, 2, "append");
    list_push(self_list(x), A(1));
    return NONE_V;
}
BI(l_extend)
{
    List *l = self_list(x);
    Value it, y;
    need(x, 2, 2, "extend");
    if (A(1).t == T_LIST || A(1).t == T_TUPLE) {
        List *s = AS_LIST(A(1));
        long i, n = s->n;
        for (i = 0; i < n; i++) list_push(l, s->a[i]);
        return NONE_V;
    }
    it = get_iter(A(1));
    VPUSH(it);
    while (iter_next(it, &y)) list_push(l, y);
    vsp--;
    return NONE_V;
}
BI(l_insert)
{
    List *l = self_list(x);
    long i;
    need(x, 3, 3, "insert");
    i = to_ssize(A(1));
    if (i < 0) { i += l->n; if (i < 0) i = 0; }
    if (i > l->n) i = l->n;
    list_push(l, NONE_V);
    memmove(l->a + i + 1, l->a + i, sizeof(Value) * (size_t)(l->n - 1 - i));
    l->a[i] = A(2);
    return NONE_V;
}
BI(l_pop)
{
    List *l = self_list(x);
    long i;
    Value r;
    need(x, 1, 2, "pop");
    if (l->n == 0) throw_new(cls_IndexError, "pop from empty list");
    i = x->n > 1 ? to_index(A(1)) : l->n - 1;
    if (i < 0) i += l->n;
    if (i < 0 || i >= l->n) throw_new(cls_IndexError, "pop index out of range");
    r = l->a[i];
    list_splice(l, i, i + 1, NULL, 0);
    return r;
}
BI(l_remove)
{
    List *l = self_list(x);
    long i;
    need(x, 2, 2, "remove");
    for (i = 0; i < l->n; i++)
        if (py_eq(l->a[i], A(1))) { list_splice(l, i, i + 1, NULL, 0); return NONE_V; }
    throw_new(cls_ValueError, "list.remove(x): x not in list");
    return NONE_V;
}
static Value seq_index(Args *x, int istuple)
{
    List *l = AS_LIST(A(0));
    long i, st = 0, en = l->n;
    need(x, 2, 4, "index");
    if (x->n > 2) { st = to_slice_index(A(2)); if (st < 0) st += l->n; if (st < 0) st = 0; }
    if (x->n > 3) { en = to_slice_index(A(3)); if (en < 0) en += l->n; }
    for (i = st; i < en && i < l->n; i++)
        if (py_eq(l->a[i], A(1))) return mkint(i);
    if (istuple) throw_new(cls_ValueError, "tuple.index(x): x not in tuple");
    {
        Value r = py_repr(A(1));
        throw_new(cls_ValueError, "%.200s is not in list", AS_STR(r)->s);
    }
    return NONE_V;
}
BI(l_index) { self_list(x); return seq_index(x, 0); }
BI(t_index) { return seq_index(x, 1); }
BI(l_count)
{
    List *l = AS_LIST(A(0));
    long i, n = 0;
    need(x, 2, 2, "count");
    for (i = 0; i < l->n; i++) if (py_eq(l->a[i], A(1))) n++;
    return mkint(n);
}
BI(l_sort)
{
    List *l = self_list(x);
    Value rv = kwget(x, "reverse");
    sort_list(l, kwget(x, "key"), rv.t != T_UNSET && py_truthy(rv));
    return NONE_V;
}
BI(l_reverse)
{
    List *l = self_list(x);
    reverse_vals(l->a, l->n);
    return NONE_V;
}
BI(l_copy)
{
    return iter_to_list(A(0));
}
BI(l_clear)
{
    self_list(x)->n = 0;
    return NONE_V;
}

/* ---- dict methods ----------------------------------------------------- */
static Dict *self_dict(Args *x)
{
    if (A(0).t != T_DICT)
        throw_new(cls_TypeError, "descriptor requires a 'dict' object but received '%.40s'",
        type_name(A(0)));
    return AS_DICT(A(0));
}
BI(d_get)
{
    Value r;
    need(x, 2, 3, "get");
    if (dict_get(self_dict(x), A(1), &r)) return r;
    return x->n > 2 ? A(2) : NONE_V;
}
BI(d_keys) { self_dict(x); return mkdview(A(0), 0); }
BI(d_values) { self_dict(x); return mkdview(A(0), 1); }
BI(d_items) { self_dict(x); return mkdview(A(0), 2); }
BI(d_update)
{
    Dict *d = self_dict(x);
    int i;
    if (x->n > 1) dict_update_from(d, A(1));
    for (i = 0; i < x->nk; i++) dict_set(d, x->kw[2 * i], x->kw[2 * i + 1]);
    return NONE_V;
}
BI(d_pop)
{
    Dict *d = self_dict(x);
    Value r;
    need(x, 2, 3, "pop");
    if (dict_get(d, A(1), &r)) { dict_del(d, A(1)); return r; }
    if (x->n > 2) return A(2);
    throw_key(A(1));
    return NONE_V;
}
BI(d_setdefault)
{
    Dict *d = self_dict(x);
    Value r;
    need(x, 2, 3, "setdefault");
    if (dict_get(d, A(1), &r)) return r;
    r = x->n > 2 ? A(2) : NONE_V;
    dict_set(d, A(1), r);
    return r;
}
BI(d_copy)
{
    Dict *n = dict_new();
    Value nv = mkobj(n, T_DICT);
    VPUSH(nv);
    dict_update_from(n, A(0));
    vsp--;
    return nv;
}
BI(d_clear)
{
    dict_clear(self_dict(x));
    return NONE_V;
}
BI(d_popitem)
{
    Dict *d = self_dict(x);
    long i;
    for (i = d->used - 1; i >= 0; i--) {
        if (d->e[i].k.t != T_UNSET) {
            Value r = mktuple2(d->e[i].k, d->e[i].v);
            dict_del(d, d->e[i].k);
            return r;
        }
    }
    throw_new(cls_KeyError, "popitem(): dictionary is empty");
    return NONE_V;
}
BI(d_fromkeys)
{
    Dict *d = dict_new();
    Value dv = mkobj(d, T_DICT), it, y;
    VPUSH(dv);
    it = get_iter(A(0));
    VPUSH(it);
    while (iter_next(it, &y)) dict_set(d, y, x->n > 1 ? A(1) : NONE_V);
    vsp -= 2;
    return dv;
}

/* ---- set methods ------------------------------------------------------ */
static Dict *self_set(Args *x)
{
    if (A(0).t != T_SET)
        throw_new(cls_TypeError, "descriptor requires a 'set' object but received '%.40s'",
        type_name(A(0)));
    return AS_DICT(A(0));
}
static Dict *self_mset(Args *x, const char *nm)
{
    Dict *d = self_set(x);
    if (d->frozen)
        throw_new(cls_TypeError,
                  "descriptor '%s' for 'set' objects doesn't apply to a 'frozenset' object", nm);
    return d;
}
BI(st_add) { need(x, 2, 2, "add"); set_add(self_mset(x, "add"), A(1)); return NONE_V; }
BI(st_discard)
{
    need(x, 2, 2, "discard");
    set_discard(self_mset(x, "discard"), A(1));
    return NONE_V;
}
BI(st_remove)
{
    need(x, 2, 2, "remove");
    if (!set_discard(self_mset(x, "remove"), A(1))) throw_key(A(1));
    return NONE_V;
}
BI(st_pop)
{
    Dict *d = self_mset(x, "pop");
    Value k;
    if (set_pop(d, &k)) return k;
    throw_new(cls_KeyError, "pop from an empty set");
    return NONE_V;
}
BI(st_union)
{
    Dict *d = self_set(x);
    Value r = set_copy_v(d, d->frozen);
    int i;
    VPUSH(r);
    for (i = 1; i < x->n; i++)
        if (as_anyset(A(i)) != d) set_update_value(AS_DICT(r), A(i));
    vsp--;
    return r;
}
static Value set_intersection_multi(Args *x, Dict *d)
{
    Value r = A(0);
    int i;
    if (x->n == 1) return set_copy_v(d, d->frozen);
    for (i = 1; i < x->n; i++) {
        VPUSH(r);
        r = set_intersection_v(AS_DICT(r), A(i));
        vsp--;
    }
    return r;
}
BI(st_intersection) { return set_intersection_multi(x, self_set(x)); }
BI(st_difference)
{
    Dict *d = self_set(x);
    Value r;
    int i;
    if (x->n == 1) return set_copy_v(d, d->frozen);
    r = set_difference_v(d, A(1));
    VPUSH(r);
    for (i = 2; i < x->n; i++) set_diff_update(AS_DICT(r), A(i));
    vsp--;
    return r;
}
BI(st_symdiff)
{
    Dict *d = self_set(x);
    need(x, 2, 2, "symmetric_difference");
    return set_symdiff_v(d, A(1));
}
BI(st_update)
{
    Dict *d = self_mset(x, "update");
    int i;
    for (i = 1; i < x->n; i++) set_update_value(d, A(i));
    return NONE_V;
}
BI(st_intersection_update)
{
    Dict *d = self_mset(x, "intersection_update");
    Value r = set_intersection_multi(x, d);
    set_swap(d, AS_DICT(r));
    return NONE_V;
}
BI(st_difference_update)
{
    Dict *d = self_mset(x, "difference_update");
    int i;
    for (i = 1; i < x->n; i++) set_diff_update(d, A(i));
    return NONE_V;
}
BI(st_symdiff_update)
{
    Dict *d = self_mset(x, "symmetric_difference_update");
    need(x, 2, 2, "symmetric_difference_update");
    set_symdiff_update(d, A(1));
    return NONE_V;
}
/* a <= b for two sets */
static int set_subset(Dict *a, Dict *b)
{
    long i;
    if (a->n > b->n) return 0;
    for (i = 0; i < a->used; i++)
        if (a->e[i].k.t != T_UNSET && !set_has_h(b, a->e[i].k, a->e[i].hash)) return 0;
    return 1;
}
BI(st_issubset)
{
    Dict *d = self_set(x), *o;
    Value tmp;
    int r;
    need(x, 2, 2, "issubset");
    o = as_anyset(A(1));
    if (o) return mkbool(set_subset(d, o));
    tmp = set_intersection_v(d, A(1));
    r = AS_DICT(tmp)->n == d->n;
    return mkbool(r);
}
BI(st_issuperset)
{
    Dict *d = self_set(x), *o;
    Value it, y;
    Value *base = vsp;
    need(x, 2, 2, "issuperset");
    o = as_anyset(A(1));
    if (o) return mkbool(set_subset(o, d));
    VPUSH(A(1));
    it = get_iter(A(1));
    VPUSH(it);
    while (iter_next(it, &y)) {
        if (!set_has(d, y)) { vsp = base; return FALSE_V; }
    }
    vsp = base;
    return TRUE_V;
}
BI(st_isdisjoint)
{
    Dict *d = self_set(x), *o;
    Value it, y;
    Value *base = vsp;
    long i;
    need(x, 2, 2, "isdisjoint");
    o = as_anyset(A(1));
    if (o == d) return mkbool(d->n == 0);
    if (o) {
        Dict *a = d, *b = o;
        if (b->n > a->n) { a = o; b = d; }
        for (i = 0; i < b->used; i++)
            if (b->e[i].k.t != T_UNSET && set_has_h(a, b->e[i].k, b->e[i].hash)) return FALSE_V;
        return TRUE_V;
    }
    VPUSH(A(1));
    it = get_iter(A(1));
    VPUSH(it);
    while (iter_next(it, &y)) {
        if (set_has(d, y)) { vsp = base; return FALSE_V; }
    }
    vsp = base;
    return TRUE_V;
}
BI(st_copy)
{
    Dict *d = self_set(x);
    if (d->frozen) return A(0);            /* frozenset.copy() returns the object itself */
    return set_copy_v(d, 0);
}
BI(st_clear)
{
    set_clear(self_mset(x, "clear"));
    return NONE_V;
}

/* ---- numbers ---------------------------------------------------------- */
BI(i_index)
{
    Value v = unpay(A(0));
    return v.t == T_BOOL ? mkint(v.u.i) : v;
}
BI(i_bit_length)
{
    Value v = unpay(A(0));
    return mkint(int_bitlen(v));
}
BI(i_bit_count)
{
    Value v = unpay(A(0));
    if (int_sign(v) < 0) v = int_neg(v);
    return mkint(int_bitcount(v));
}
BI(i_as_integer_ratio)
{
    Value r[2];
    r[0] = unpay(A(0));
    if (r[0].t == T_BOOL) r[0] = mkint(r[0].u.i);
    r[1] = mkint(1);
    return mktuple_from(r, 2);
}
BI(i_is_integer)
{
    (void)x;
    return TRUE_V;
}
BI(f_is_integer)
{
    double d = unpay(A(0)).u.d;
    return mkbool(d - d == 0.0 && d == floor(d));
}
BI(f_as_integer_ratio)
{
    double d = unpay(A(0)).u.d, f, m;
    int e, neg = 0;
    Value r[2];
    if (d != d) throw_new(cls_ValueError, "cannot convert NaN to integer ratio");
    if (d - d != 0.0) throw_new(cls_OverflowError, "cannot convert Infinity to integer ratio");
    if (d < 0) { neg = 1; d = -d; }
    if (d == 0.0) { r[0] = mkint(0); r[1] = mkint(1); return mktuple_from(r, 2); }
    f = frexp(d, &e);
    m = ldexp(f, 53);                    /* an integer below 2**53 */
    e -= 53;
    while (e < 0 && fmod(m, 2.0) == 0.0) { m /= 2.0; e++; }
    r[0] = int_from_double(m);
    if (neg) r[0] = int_neg(r[0]);
    if (e >= 0) { r[0] = int_mul(r[0], int_pow2(e)); r[1] = mkint(1); }
    else r[1] = int_pow2(-e);
    return mktuple_from(r, 2);
}
BI(f_hex)
{
    double d = unpay(A(0)).u.d, f, m;
    int e, i;
    char b[64], *q = b;
    static const char hx[] = "0123456789abcdef";
    if (d != d) return mkcstr("nan");
    if (d > DBL_MAX) return mkcstr("inf");
    if (d < -DBL_MAX) return mkcstr("-inf");
    if (d == 0.0) return mkcstr(1.0 / d < 0 ? "-0x0.0p+0" : "0x0.0p+0");
    if (d < 0) { *q++ = '-'; d = -d; }
    f = frexp(d, &e);                    /* 0.5 <= f < 1 */
    m = ldexp(f, 53);                    /* 53 bit integer, top bit set */
    e -= 1;                              /* d = 1.xxx * 2**e */
    if (e < -1022) {                     /* subnormal: 0.xxx * 2**-1022 */
        m = ldexp(m, e + 1022);
        *q++ = '0'; *q++ = 'x'; *q++ = '0'; *q++ = '.';
        e = -1022;
    } else {
        *q++ = '0'; *q++ = 'x'; *q++ = '1'; *q++ = '.';
        m = m - ldexp(1.0, 52);
    }
    for (i = 13; i > 0; i--) {           /* 13 hex digits = 52 bits */
        double w = ldexp(1.0, 4 * (i - 1));
        double dg = floor(m / w);
        *q++ = hx[(int)dg];
        m -= dg * w;
    }
    sprintf(q, "p%c%d", e < 0 ? '-' : '+', e < 0 ? -e : e);
    return mkcstr(b);
}
BI(f_fromhex)
{
    Value sv = A(x->n > 0 && A(0).t == T_CLASS ? 1 : 0);
    const char *p, *t;
    double v = 0.0, scale = 1.0;
    int neg = 0, any = 0, e = 0, eneg = 0, seen_dot = 0, dg;
    if (sv.t != T_STR) throw_new(cls_TypeError, "fromhex() argument must be str");
    p = AS_STR(sv)->s;
    while (ch_space[(unsigned char)*p]) p++;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    t = p;
    if (strncmp(p, "inf", 3) == 0 || strncmp(p, "INF", 3) == 0)
        return mkfloat(neg ? -HUGE_VAL : HUGE_VAL);
    if (strncmp(p, "nan", 3) == 0 || strncmp(p, "NAN", 3) == 0) return mkfloat(HUGE_VAL - HUGE_VAL);
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    for (;; p++) {
        if (*p == '.' && !seen_dot) { seen_dot = 1; continue; }
        dg = digit_val((unsigned char)*p);
        if (dg < 0 || dg > 15) break;
        any = 1;
        if (seen_dot) { scale /= 16.0; v += dg * scale; }
        else v = v * 16.0 + dg;
    }
    if (!any) throw_new(cls_ValueError, "invalid hexadecimal floating-point string");
    if (*p == 'p' || *p == 'P') {
        p++;
        if (*p == '-') { eneg = 1; p++; } else if (*p == '+') p++;
        if (!ch_digit[(unsigned char)*p])
            throw_new(cls_ValueError, "invalid hexadecimal floating-point string");
        while (ch_digit[(unsigned char)*p]) { if (e < 100000) e = e * 10 + (*p - '0'); p++; }
    }
    while (ch_space[(unsigned char)*p]) p++;
    if (*p) throw_new(cls_ValueError, "invalid hexadecimal floating-point string");
    (void)t;
    v = ldexp(v, eneg ? -e : e);
    if (v > DBL_MAX)
        throw_new(cls_OverflowError, "hexadecimal value too large to represent as a float");
    return mkfloat(neg ? -v : v);
}

/* ---- generators and iterators ----------------------------------------- */
static void throw_stop(Value g)
{
    Value rv;
    if (g.t == T_GEN && ((Gen *)g.u.o)->ret.t != T_NONE) {
        rv = ((Gen *)g.u.o)->ret;
        ((Gen *)g.u.o)->ret = NONE_V;
        throw_val(new_exc_args(cls_StopIteration, &rv, 1));
    }
    throw_val(new_exc_args(cls_StopIteration, NULL, 0));
}
BI(g_next)
{
    Value out;
    if (iter_next(A(0), &out)) return out;
    throw_stop(A(0));
    return NONE_V;
}
BI(g_iter) { return A(0); }
static int protected_gen(Gen *g, Value *out, int *got, Value *exc)
{
    Handler h;
    Value *save = vsp;
    int depth = call_depth;
    Frame *fr = cur_frame;
    h.prev = cur_handler;
    h.sp = save;
    h.depth = depth;
    h.frame = fr;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        *got = gen_resume(g, out);
        cur_handler = h.prev;
        return 0;
    }
    cur_handler = h.prev;
    vsp = save;
    call_depth = depth;
    cur_frame = fr;
    *exc = cur_exc;
    return 1;
}
BI(g_send)
{
    Value out;
    need(x, 2, 2, "send");
    if (A(0).t != T_GEN) {
        if (A(1).t != T_NONE)
            throw_new(cls_AttributeError, "'%.40s' object has no attribute 'send'",
            type_name(A(0)));
        if (iter_next(A(0), &out)) return out;
        throw_stop(A(0));
    }
    if (!((Gen *)A(0).u.o)->started && A(1).t != T_NONE)
        throw_new(cls_TypeError, "can't send non-None value to a just-started generator");
    ((Gen *)A(0).u.o)->sent = A(1);
    if (gen_resume((Gen *)A(0).u.o, &out)) return out;
    throw_stop(A(0));
    return NONE_V;
}
BI(g_throw)
{
    Gen *g;
    Value out, e;
    need(x, 2, 4, "throw");
    if (A(0).t != T_GEN)
        throw_new(cls_AttributeError, "'%.40s' object has no attribute 'throw'", type_name(A(0)));
    g = (Gen *)A(0).u.o;
    e = A(1);
    if (e.t == T_CLASS) e = call0(e);
    if (e.t != T_INST || !(AS_INST(e)->cls->flags & CF_EXC))
        throw_new(cls_TypeError, "exceptions must derive from BaseException");
    if (!g->started || g->done) {
        g->done = 1;
        throw_val(e);
    }
    g->thrown = e;
    if (gen_resume(g, &out)) return out;
    throw_stop(A(0));
    return NONE_V;
}
BI(g_close)
{
    Gen *g;
    Value out, exc;
    int got = 0;
    if (A(0).t != T_GEN) return NONE_V;
    g = (Gen *)A(0).u.o;
    if (g->started && !g->done && !g->running) {
        g->thrown = new_exc_args(cls_GeneratorExit, NULL, 0);
        if (protected_gen(g, &out, &got, &exc)) {
            g->done = 1;
            if (!class_isa(AS_INST(exc)->cls, cls_GeneratorExit) &&
                !class_isa(AS_INST(exc)->cls, cls_StopIteration))
                throw_val(exc);
            return NONE_V;
        }
        if (got) {
            g->done = 1;
            throw_new(cls_RuntimeError, "generator ignored GeneratorExit");
        }
    }
    g->done = 1;
    return NONE_V;
}

/* ---- files ------------------------------------------------------------ */
/* the exception class for a host error number (errno) */
static Class *oserror_class(int err)
{
    if (err == ENOENT) return cls_FileNotFoundError;
    if (err == EACCES || err == EPERM) return cls_PermissionError;
    if (err == EEXIST) return cls_FileExistsError;
    if (err == EISDIR) return cls_IsADirectoryError;
    if (err == ENOTDIR) return cls_NotADirectoryError;
    return cls_OSError;
}
/* raise OSError(errno, strerror, filename) as the matching subclass */
static void throw_oserror(int err, Value filename)
{
    Value v[3];
    Class *c = oserror_class(err);
    long n = 2;
    v[0] = mkint(err);
    v[1] = mkcstr(strerror(err));
    if (filename.t != T_UNSET && filename.t != T_NONE) { v[2] = filename; n = 3; }
    {
        Value ev = new_exc_args(c, v, n);
        VPUSH(ev);
        amap_set(&AS_INST(ev)->a, intern_c("errno"), v[0]);
        amap_set(&AS_INST(ev)->a, intern_c("strerror"), v[1]);
        amap_set(&AS_INST(ev)->a, intern_c("filename"), n == 3 ? v[2] : NONE_V);
        vsp--;
        throw_val(ev);
    }
}
static Value new_file_obj(FILE *fp, int out, int bin, Value name, const char *mode)
{
    File *f = (File *)gc_alloc(sizeof(File), T_FILE);
    f->fp = fp;
    f->closed = 0;
    f->out = out;
    f->bin = bin;
    f->name = name.t == T_STR ? AS_STR(name) : NULL;
    f->mode = intern_c(mode);
    return mkobj(f, T_FILE);
}
static File *self_file(Args *x)
{
    File *f;
    if (A(0).t != T_FILE) throw_new(cls_TypeError, "descriptor requires a file object");
    f = (File *)A(0).u.o;
    if (f->closed) throw_new(cls_ValueError, "I/O operation on closed file");
    return f;
}
/* a chunk read from a file: str in text mode, bytes in binary mode */
static Value file_value(File *f, Buf *b)
{
    if (f->bin) {
        Value r = mkbytes(b->s, b->n);
        buf_free(b);
        return r;
    }
    return buf_value(b);
}
static Value file_readline(File *f)
{
    Buf b;
    int c;
    buf_init(&b);
    while ((c = getc(f->fp)) != EOF) {
        buf_addc(&b, c);
        if (c == '\n') break;
    }
    return file_value(f, &b);
}
static long file_value_len(Value v)
{
    return v.t == T_BYTES || v.t == T_STR ? AS_STR(v)->len : 0;
}
BI(bi_open)
{
    Str *path, *mode;
    FILE *fp;
    char m[8];
    int k = 0, i, bin = 0, plus = 0, excl = 0;
    char base = 'r';
    need(x, 1, 2, "open");
    if (A(0).t == T_INST && class_find(AS_INST(A(0))->cls, intern_c("__fspath__"))) {
        Value fs = call0(get_attr(A(0), intern_c("__fspath__")));
        if (fs.t == T_STR) x->a[0] = fs;
    }
    path = astr(A(0), "file");
    mode = x->n > 1 ? astr(A(1), "mode") : NULL;
    if (!mode && kwget(x, "mode").t == T_STR) mode = AS_STR(kwget(x, "mode"));
    if (mode) {
        int seen = 0;
        for (i = 0; i < mode->len; i++) {
            char ch = mode->s[i];
            if (ch == 'r' || ch == 'w' || ch == 'a' || ch == 'x') {
                if (seen)
                    throw_new(cls_ValueError,
                              "must have exactly one of create/read/write/append mode");
                base = ch; seen = 1;
            } else if (ch == '+') plus = 1;
            else if (ch == 'b') bin = 1;
            else if (ch != 't' && ch != 'U')
                throw_new(cls_ValueError, "invalid mode: '%.20s'", mode->s);
        }
        if (!seen) base = 'r';
    }
    if (base == 'x') {
        if (host_file_exists(path->s)) throw_oserror(EEXIST, A(0));
        base = 'w';
        excl = 1;
    }
    m[k++] = base;
    if (plus) m[k++] = '+';
    if (bin) m[k++] = 'b';
    m[k] = 0;
    errno = 0;
    fp = host_fopen(path->s, m);
    if (!fp) throw_oserror(errno ? errno : ENOENT, A(0));
    if (base == 'r' && !plus && host_file_isdir(path->s)) {
        fclose(fp);
        throw_oserror(EISDIR, A(0));
    }
    {
        char md[8];
        k = 0;
        md[k++] = excl ? 'x' : base;
        if (plus) md[k++] = '+';
        if (bin) md[k++] = 'b';
        md[k] = 0;
        return new_file_obj(fp, 0, bin ? (base == 'r' && !plus ? 1 : 2) : 0, A(0), md);
    }
}
BI(fl_read)
{
    File *f = self_file(x);
    Buf b;
    int c;
    long n = x->n > 1 && A(1).t != T_NONE ? to_index(A(1)) : -1;
    buf_init(&b);
    if (n < 0) {
        while ((c = getc(f->fp)) != EOF) buf_addc(&b, c);
    } else {
        while (b.n < n && (c = getc(f->fp)) != EOF) buf_addc(&b, c);
    }
    return file_value(f, &b);
}
BI(fl_readline) { return file_readline(self_file(x)); }
BI(fl_readlines)
{
    File *f = self_file(x);
    List *l = list_new(8);
    Value lv = mklist(l), ln;
    VPUSH(lv);
    for (;;) {
        ln = file_readline(f);
        if (file_value_len(ln) == 0) break;
        list_push(l, ln);
    }
    vsp--;
    return lv;
}
/* the bytes of a binary write, or the text of a text write */
static void file_put(File *f, Value v)
{
    const char *p;
    long n = 0;
    if (f->bin) {
        if (!bytes_get(v, &p, &n))
            throw_new(cls_TypeError, "a bytes-like object is required, not '%.40s'", type_name(v));
        fwrite(p, 1, (size_t)n, f->fp);
    } else {
        Str *s = astr(v, "write() argument");
        fwrite(s->s, 1, (size_t)s->len, f->fp);
    }
}
BI(fl_write)
{
    File *f = self_file(x);
    const char *p;
    long n = 0;
    need(x, 2, 2, "write");
    if (f->fp == stderr) fflush(stdout);
    file_put(f, A(1));
    if (f->bin) { bytes_get(A(1), &p, &n); return mkint(n); }
    return mkint(AS_STR(A(1).t == T_INST ? AS_INST(A(1))->pay : A(1))->len);
}
BI(fl_writelines)
{
    File *f = self_file(x);
    Value it, y;
    it = get_iter(A(1));
    VPUSH(it);
    while (iter_next(it, &y)) file_put(f, y);
    vsp--;
    return NONE_V;
}
BI(fl_flush)
{
    fflush(self_file(x)->fp);
    return NONE_V;
}
BI(fl_close)
{
    File *f = (File *)A(0).u.o;
    if (!f->closed && !f->out) fclose(f->fp);
    f->closed = 1;
    return NONE_V;
}
BI(fl_enter)
{
    (void)self_file(x);
    return A(0);
}
BI(fl_exit)
{
    File *f = (File *)A(0).u.o;
    if (!f->closed && !f->out) fclose(f->fp);
    f->closed = 1;
    return FALSE_V;
}
BI(fl_seek)
{
    File *f = self_file(x);
    long off = to_index(A(1)), wh = x->n > 2 ? to_index(A(2)) : 0;
    need(x, 2, 3, "seek");
    if (wh < 0 || wh > 2)
        throw_new(cls_ValueError, "invalid whence (%ld, should be 0, 1 or 2)", wh);
    if (fseek(f->fp, off, wh == 0 ? SEEK_SET : (wh == 1 ? SEEK_CUR : SEEK_END)) != 0)
        throw_oserror(errno ? errno : EINVAL, NONE_V);
    return mkint(ftell(f->fp));
}
BI(fl_tell)
{
    File *f = self_file(x);
    long p = ftell(f->fp);
    if (p < 0) throw_oserror(errno ? errno : ESPIPE, NONE_V);
    return mkint(p);
}
BI(fl_readable)
{
    File *f = self_file(x);
    const char *m = f->mode->s;
    return mkbool(m[0] == 'r' || strchr(m, '+') != NULL);
}
BI(fl_writable)
{
    File *f = self_file(x);
    const char *m = f->mode->s;
    return mkbool(m[0] != 'r' || strchr(m, '+') != NULL);
}
BI(fl_seekable)
{
    File *f = self_file(x);
    return mkbool(f->fp != stdin && f->fp != stdout && f->fp != stderr);
}
BI(fl_isatty)
{
    (void)self_file(x);
    return FALSE_V;
}
BI(fl_fileno)
{
    File *f = self_file(x);
    int fd = host_fileno(f->fp);
    if (fd < 0) throw_new(cls_OSError, "fileno() is not available");
    return mkint(fd);
}
/* readinto(bytearray): fills the buffer, returns the number of bytes read */
BI(fl_readinto)
{
    File *f = self_file(x);
    size_t got;
    need(x, 2, 2, "readinto");
    if (A(1).t != T_BARR) throw_new(cls_TypeError, "readinto() argument must be a bytearray");
    got = fread(AS_BARR(A(1))->s, 1, (size_t)AS_BARR(A(1))->n, f->fp);
    return mkint((long)got);
}
/* attributes of file objects: name, mode, closed, encoding, buffer */
static int file_attr(File *f, Str *nm, Value *out)
{
    const char *a = nm->s;
    if (strcmp(a, "closed") == 0) { *out = mkbool(f->closed); return 1; }
    if (strcmp(a, "mode") == 0) {
        *out = mkcstr(f->mode ? f->mode->s : "r");
        return 1;
    }
    if (strcmp(a, "name") == 0) {
        if (f->name) *out = mkobj(f->name, T_STR);
        else *out = mkcstr(f->fp == stdin ? "<stdin>" :
                           (f->fp == stdout ? "<stdout>" : "<stderr>"));
        return 1;
    }
    if (strcmp(a, "encoding") == 0 && !f->bin) { *out = mkcstr("utf-8"); return 1; }
    if (strcmp(a, "errors") == 0 && !f->bin) { *out = mkcstr("strict"); return 1; }
    if (strcmp(a, "buffer") == 0 && !f->bin) {
        *out = new_file_obj(f->fp, 1, f->fp == stdin ? 1 : 2, NONE_V, f->fp == stdin ? "rb" : "wb");
        return 1;
    }
    return 0;
}

/* ---- protocol methods of the native base types (list dict set tuple str) ----------
   On subclass instances the self argument is replaced by the payload (Builtin.selfpay). */
BI(nat_new)
{
    Class *c;
    Inst *in;
    Value iv;
    if (x->n < 1 || A(0).t != T_CLASS)
        throw_new(cls_TypeError, "__new__(X): X is not a type object");
    c = AS_CLASS(A(0));
    in = new_inst(c);
    iv = mkobj(in, T_INST);
    VPUSH(iv);
    if (c->paycls) {
        if (pay_takes_args(c->paycls))
            in->pay = pay_ctor(c->paycls, x->n - 1, x->a + 1, x->nk, x->kw);
        else
            in->pay = pay_ctor(c->paycls, 0, NULL, 0, NULL);
    }
    vsp--;
    return iv;
}
BI(nat_getitem) { need(x, 2, 2, "__getitem__"); return subscr(A(0), A(1)); }
BI(nat_setitem) { need(x, 3, 3, "__setitem__"); setitem(A(0), A(1), A(2)); return NONE_V; }
BI(nat_delitem) { need(x, 2, 2, "__delitem__"); delitem(A(0), A(1)); return NONE_V; }
BI(nat_len) { return mkint(py_len(A(0))); }
BI(nat_iter) { return get_iter(A(0)); }
BI(nat_contains) { need(x, 2, 2, "__contains__"); return mkbool(contains(A(0), A(1))); }
BI(nat_repr) { return py_repr(A(0)); }
BI(nat_str) { return py_str(A(0)); }
BI(nat_eq) { need(x, 2, 2, "__eq__"); return mkbool(py_eq(A(0), unpay(A(1)))); }
BI(nat_ne) { need(x, 2, 2, "__ne__"); return mkbool(!py_eq(A(0), unpay(A(1)))); }
BI(nat_lt) { need(x, 2, 2, "__lt__"); return mkbool(rich_cmp(C_LT, A(0), unpay(A(1)))); }
BI(nat_le) { need(x, 2, 2, "__le__"); return mkbool(rich_cmp(C_LE, A(0), unpay(A(1)))); }
BI(nat_gt) { need(x, 2, 2, "__gt__"); return mkbool(rich_cmp(C_GT, A(0), unpay(A(1)))); }
BI(nat_ge) { need(x, 2, 2, "__ge__"); return mkbool(rich_cmp(C_GE, A(0), unpay(A(1)))); }
/* set subclass repr: ClassName({...}), ClassName() when empty (no selfpay: needs the class) */
BI(nat_set_repr)
{
    Value self = A(0), pay = unpay(self), r;
    Buf b;
    Str *rs;
    if (self.t != T_INST || (pay.t != T_SET && pay.t != T_BARR)) return py_repr(pay);
    r = py_repr(pay);
    rs = AS_STR(r);
    buf_init(&b);
    buf_add(&b, AS_INST(self)->cls->name->s);
    buf_addc(&b, '(');
    if (pay.t == T_BARR) buf_addn(&b, rs->s + 10, rs->len - 11);   /* inside bytearray(...) */
    else if (AS_DICT(pay)->n > 0 && AS_DICT(pay)->frozen)
        buf_addn(&b, rs->s + 10, rs->len - 11);        /* inside frozenset(...) */
    else if (AS_DICT(pay)->n > 0) buf_addn(&b, rs->s, rs->len);
    buf_addc(&b, ')');
    r = buf_value(&b);
    return r;
}
/* in-place operators of set subclass instances: no selfpay, the instance itself is returned */
static Value nat_iop(Args *x, int op)
{
    Value self = A(0), r;
    need(x, 2, 2, "__ior__");
    r = inplace_binop(op, unpay(self), unpay(A(1)));
    return r.t == T_SET && r.u.o == unpay(self).u.o ? self : r;
}
BI(nat_ior) { return nat_iop(x, B_OR); }
BI(nat_iand) { return nat_iop(x, B_AND); }
BI(nat_isub) { return nat_iop(x, B_SUB); }
BI(nat_ixor) { return nat_iop(x, B_XOR); }
BI(nat_hash) { return mkint((long)py_hash(A(0))); }
BI(nat_add) { need(x, 2, 2, "__add__"); return binop(B_ADD, A(0), unpay(A(1))); }
BI(nat_mul) { need(x, 2, 2, "__mul__"); return binop(B_MUL, A(0), A(1)); }
BI(nat_mod) { need(x, 2, 2, "__mod__"); return binop(B_MOD, A(0), A(1)); }
BI(nat_or) { need(x, 2, 2, "__or__"); return binop(B_OR, A(0), unpay(A(1))); }
BI(nat_and) { need(x, 2, 2, "__and__"); return binop(B_AND, A(0), unpay(A(1))); }
BI(nat_sub) { need(x, 2, 2, "__sub__"); return binop(B_SUB, A(0), unpay(A(1))); }
BI(nat_xor) { need(x, 2, 2, "__xor__"); return binop(B_XOR, A(0), unpay(A(1))); }
/* arithmetic dunders of int/float (called as methods) */
BI(num_add) { need(x, 2, 2, "__add__"); return binop(B_ADD, unpay(A(0)), unpay(A(1))); }
BI(num_radd) { need(x, 2, 2, "__radd__"); return binop(B_ADD, unpay(A(1)), unpay(A(0))); }
BI(num_sub) { need(x, 2, 2, "__sub__"); return binop(B_SUB, unpay(A(0)), unpay(A(1))); }
BI(num_rsub) { need(x, 2, 2, "__rsub__"); return binop(B_SUB, unpay(A(1)), unpay(A(0))); }
BI(num_mul) { need(x, 2, 2, "__mul__"); return binop(B_MUL, unpay(A(0)), unpay(A(1))); }
BI(num_rmul) { need(x, 2, 2, "__rmul__"); return binop(B_MUL, unpay(A(1)), unpay(A(0))); }
BI(num_truediv) { need(x, 2, 2, "__truediv__"); return binop(B_DIV, unpay(A(0)), unpay(A(1))); }
BI(num_rtruediv) { need(x, 2, 2, "__rtruediv__"); return binop(B_DIV, unpay(A(1)), unpay(A(0))); }
BI(num_floordiv) { need(x, 2, 2, "__floordiv__"); return binop(B_FDIV, unpay(A(0)), unpay(A(1))); }
BI(num_rfloordiv) { need(x, 2, 2, "__rfloordiv__");
    return binop(B_FDIV, unpay(A(1)), unpay(A(0)));
}
BI(num_mod) { need(x, 2, 2, "__mod__"); return binop(B_MOD, unpay(A(0)), unpay(A(1))); }
BI(num_rmod) { need(x, 2, 2, "__rmod__"); return binop(B_MOD, unpay(A(1)), unpay(A(0))); }
BI(num_pow) { need(x, 2, 2, "__pow__"); return binop(B_POW, unpay(A(0)), unpay(A(1))); }
BI(num_rpow) { need(x, 2, 2, "__rpow__"); return binop(B_POW, unpay(A(1)), unpay(A(0))); }
BI(num_and) { need(x, 2, 2, "__and__"); return binop(B_AND, unpay(A(0)), unpay(A(1))); }
BI(num_rand) { need(x, 2, 2, "__rand__"); return binop(B_AND, unpay(A(1)), unpay(A(0))); }
BI(num_or) { need(x, 2, 2, "__or__"); return binop(B_OR, unpay(A(0)), unpay(A(1))); }
BI(num_ror) { need(x, 2, 2, "__ror__"); return binop(B_OR, unpay(A(1)), unpay(A(0))); }
BI(num_xor) { need(x, 2, 2, "__xor__"); return binop(B_XOR, unpay(A(0)), unpay(A(1))); }
BI(num_rxor) { need(x, 2, 2, "__rxor__"); return binop(B_XOR, unpay(A(1)), unpay(A(0))); }
BI(num_lshift) { need(x, 2, 2, "__lshift__"); return binop(B_LSH, unpay(A(0)), unpay(A(1))); }
BI(num_rlshift) { need(x, 2, 2, "__rlshift__"); return binop(B_LSH, unpay(A(1)), unpay(A(0))); }
BI(num_rshift) { need(x, 2, 2, "__rshift__"); return binop(B_RSH, unpay(A(0)), unpay(A(1))); }
BI(num_rrshift) { need(x, 2, 2, "__rrshift__"); return binop(B_RSH, unpay(A(1)), unpay(A(0))); }
BI(num_neg) { return unop('-', unpay(A(0))); }
BI(num_pos) { return unop('+', unpay(A(0))); }
BI(num_invert) { return unop('~', unpay(A(0))); }
BI(num_abs) { Value v = unpay(A(0));
    return rich_cmp(C_LT, v, mkint(0)) ? unop('-', v) : unop('+', v);
}
BI(num_conj) { return unpay(A(0)); }
BI(nat_format)
{
    need(x, 2, 2, "__format__");
    return format_value(A(0), astr(A(1), "format_spec")->s, AS_STR(A(1))->len);
}
BI(list_init)
{
    List *l = self_list(x);
    need(x, 1, 2, "__init__");
    l->n = 0;
    if (x->n > 1) {
        Value it, y;
        VPUSH(A(1));
        it = get_iter(A(1));
        VPUSH(it);
        while (iter_next(it, &y)) list_push(l, y);
        vsp -= 2;
    }
    return NONE_V;
}
BI(set_init)
{
    Dict *d = self_mset(x, "__init__");
    need(x, 1, 2, "__init__");
    set_clear(d);
    if (x->n > 1) set_update_value(d, A(1));
    return NONE_V;
}
BI(obj_setattr)
{
    need(x, 3, 3, "__setattr__");
    if (A(0).t != T_INST)
        throw_new(cls_TypeError, "can't apply this __setattr__ to a non-instance");
    amap_set(&AS_INST(A(0))->a, intern_str(astr(A(1), "attribute name")), A(2));
    return NONE_V;
}
BI(bi_slice)
{
    need(x, 1, 3, "slice");
    if (x->n == 1) return mkslice(NONE_V, A(0), NONE_V);
    return mkslice(A(0), A(1), x->n > 2 ? A(2) : NONE_V);
}
BI(sl_indices)
{
    List *sl;
    Value r[3];
    need(x, 2, 2, "indices");
    sl = AS_LIST(A(0));
    {
        Value len = need_intv(A(1), "length");
        if (int_sign(len) < 0) throw_new(cls_ValueError, "length should not be negative");
        slice_adjust_v(sl->a[0], sl->a[1], sl->a[2], len, &r[0], &r[1], &r[2]);
    }
    return mktuple_from(r, 3);
}

BI(s_isidentifier)
{
    Str *s = SELF;
    long i;
    if (s->len == 0 || ch_digit[(unsigned char)s->s[0]]) return FALSE_V;
    for (i = 0; i < s->len; i++)
        if (!ch_alnum[(unsigned char)s->s[i]] && (unsigned char)s->s[i] < 128) return FALSE_V;
    return TRUE_V;
}
BI(s_istitle)
{
    Str *s = SELF;
    long i;
    int prev = 0, any = 0;
    for (i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->s[i];
        if (ch_upper[c]) { if (prev) return FALSE_V; prev = 1; any = 1; }
        else if (ch_lower[c]) { if (!prev) return FALSE_V; }
        else prev = 0;
    }
    return mkbool(any);
}
BI(s_isprintable)
{
    Str *s = SELF;
    long i;
    int k;
    for (i = 0; i < s->len; i += k) {
        unsigned long c = u8_next((const unsigned char *)s->s, i, s->len, &k);
        if (c < 128UL ? !ch_print[c] : uni_nonprint(c)) return FALSE_V;
    }
    return TRUE_V;
}
BI(s_isascii)
{
    Str *s = SELF;
    long i;
    for (i = 0; i < s->len; i++) if ((unsigned char)s->s[i] >= 128) return FALSE_V;
    return TRUE_V;
}
BI(notimpl_repr)
{
    (void)x;
    return mkcstr("NotImplemented");
}
BI(ellipsis_repr)
{
    (void)x;
    return mkcstr("Ellipsis");
}

