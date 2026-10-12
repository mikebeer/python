/* =========================================================================
   p7_bytes.c   bytes, bytearray, int.to_bytes / int.from_bytes, struct

   bytes is an immutable Str-like object (T_BYTES), bytearray a growable
   buffer (T_BARR).  Most read-only methods run the str method on a
   temporary str copy of the data (strings are raw bytes in this
   interpreter) and convert the result back.
   ========================================================================= */

static Module *new_module(const char *name);
static void mod_def(Module *m, const char *name, BFn fn);
static void mod_val(Module *m, const char *name, Value v);
static Class *mk_type(const char *name, Class *base, BFn ctor);
static int bx_isbarr(Value v)
{
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET) v = AS_INST(v)->pay;
    return v.t == T_BARR;
}
static Value bx_make(int barr, const char *s, long n)
{
    return barr ? mkbarr(s, n) : mkbytes(s, n);
}
/* bytes-like argument or TypeError */
static void bx_need(Value v, const char **p, long *n)
{
    if (!bytes_get(v, p, n))
        throw_new(cls_TypeError, "a bytes-like object is required, not '%.40s'", type_name(v));
}
static void bx_self(Value v, const char **p, long *n)
{
    if (!bytes_get(v, p, n))
        throw_new(cls_TypeError, "descriptor requires a 'bytes' object but received '%.40s'",
            type_name(v));
}

/* ---- construction ------------------------------------------------------ */
static Value bytes_build(Args *x, int barr)
{
    Value v, ev = kwget(x, "encoding");
    const char *p;
    long n, i;
    Buf b;
    if (x->n == 0) return bx_make(barr, "", 0);
    v = A(0);
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET) v = AS_INST(v)->pay;
    if (v.t == T_STR) {
        Args a2;
        Value av[3], r;
        if (x->n < 2 && ev.t == T_UNSET)
            throw_new(cls_TypeError, "string argument without an encoding");
        av[0] = v;
        av[1] = x->n > 1 ? A(1) : ev;
        av[2] = x->n > 2 ? A(2) : kwget(x, "errors");
        a2.n = av[2].t == T_UNSET ? 2 : 3; a2.a = av; a2.nk = 0; a2.kw = NULL;
        r = s_encode(&a2);
        if (barr) return mkbarr(AS_STR(r)->s, AS_STR(r)->len);
        return r;
    }
    if (x->n > 1 || ev.t != T_UNSET)
        throw_new(cls_TypeError, "encoding or errors without a string argument");
    if (bytes_get(v, &p, &n)) return bx_make(barr, p, n);
    if (v.t == T_INT || v.t == T_BOOL) {
        if (v.u.i < 0) throw_new(cls_ValueError, "negative count");
        if (v.u.i > 400000000L) throw_new(cls_OverflowError,
            "cannot fit 'int' into an index-sized integer");
        buf_init(&b);
        for (i = 0; i < v.u.i; i++) buf_addc(&b, 0);
    } else {
        Value it, y;
        if (v.t == T_FLOAT || v.t == T_NONE || v.t == T_BIG)
            throw_new(cls_TypeError, "cannot convert '%.40s' object to bytes", type_name(v));
        it = get_iter(v);
        VPUSH(it);
        buf_init(&b);
        while (iter_next(it, &y)) {
            if (y.t != T_INT && y.t != T_BOOL) {
                buf_free(&b);
                throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
                    type_name(y));
            }
            if (y.u.i < 0 || y.u.i > 255) {
                buf_free(&b);
                throw_new(cls_ValueError, "bytes must be in range(0, 256)");
            }
            buf_addc(&b, (int)y.u.i);
        }
        vsp--;
    }
    v = bx_make(barr, b.s, b.n);
    buf_free(&b);
    return v;
}
BI(bi_bytes) { return bytes_build(x, 0); }
BI(bi_bytearray) { return bytes_build(x, 1); }

/* ---- decode ------------------------------------------------------------ */
/* length of the valid UTF-8 sequence at s[i], or 0 with *blame bytes at fault */
static int utf8_seq(const unsigned char *s, long n, long i, int *blame, const char **why)
{
    unsigned c = s[i];
    int need, k;
    unsigned lo = 0x80, hi = 0xBF;
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) need = 1;
    else if (c >= 0xE0 && c <= 0xEF) {
        need = 2;
        if (c == 0xE0) lo = 0xA0;
        if (c == 0xED) hi = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        need = 3;
        if (c == 0xF0) lo = 0x90;
        if (c == 0xF4) hi = 0x8F;
    } else {
        *blame = 1; *why = "invalid start byte";
        return 0;
    }
    for (k = 1; k <= need; k++) {
        unsigned d;
        if (i + k >= n) { *blame = k; *why = "unexpected end of data"; return 0; }
        d = s[i + k];
        if (d < (k == 1 ? lo : 0x80) || d > (k == 1 ? hi : 0xBF)) {
            *blame = k; *why = "invalid continuation byte";
            return 0;
        }
    }
    return need + 1;
}
static const char *codec_name(const char *enc, char *buf)
{
    long i;
    for (i = 0; enc[i] && i < 30; i++) {
        int c = (unsigned char)enc[i];
        buf[i] = (char)(c == '_' ? '-' : to_low[c]);
    }
    buf[i] = 0;
    return buf;
}
static Value bytes_decode(const char *p, long n, const char *enc, const char *err)
{
    char cn[40];
    long i;
    Buf b;
    codec_name(enc, cn);
    if (strcmp(cn, "utf-8") == 0 || strcmp(cn, "utf8") == 0 || strcmp(cn, "u8") == 0) {
        const unsigned char *s = (const unsigned char *)p;
        buf_init(&b);
        for (i = 0; i < n; ) {
            int blame = 0, len;
            const char *why = "";
            len = utf8_seq(s, n, i, &blame, &why);
            if (len > 0) {
                buf_addn(&b, p + i, len);
                i += len;
                continue;
            }
            if (strcmp(err, "ignore") == 0) { }
            else if (strcmp(err, "replace") == 0) buf_add(&b, "\357\277\275");
            else {
                buf_free(&b);
                if (blame == 1)
                    throw_new(cls_UnicodeDecodeError,
                        "'utf-8' codec can't decode byte 0x%02x in position %ld: %s",
                        s[i], i, why);
                throw_new(cls_UnicodeDecodeError,
                    "'utf-8' codec can't decode bytes in position %ld-%ld: %s",
                    i, i + blame - 1, why);
            }
            i += blame;
        }
        return buf_value(&b);
    }
    if (strcmp(cn, "latin-1") == 0 || strcmp(cn, "latin1") == 0 || strcmp(cn, "iso-8859-1") == 0) {
        buf_init(&b);
        for (i = 0; i < n; i++) put_utf8(&b, (unsigned char)p[i]);
        return buf_value(&b);
    }
    if (strcmp(cn, "ascii") == 0 || strcmp(cn, "us-ascii") == 0) {
        buf_init(&b);
        for (i = 0; i < n; i++) {
            if ((unsigned char)p[i] < 128) buf_addc(&b, p[i]);
            else if (strcmp(err, "ignore") == 0) { }
            else if (strcmp(err, "replace") == 0) buf_add(&b, "\357\277\275");
            else {
                buf_free(&b);
                throw_new(cls_UnicodeDecodeError,
                    "'ascii' codec can't decode byte 0x%02x in position %ld: ordinal not in "
                    "range(128)", (unsigned char)p[i], i);
            }
        }
        return buf_value(&b);
    }
    throw_new(cls_LookupError, "unknown encoding: %.60s", enc);
    return NONE_V;
}
BI(b_decode)
{
    const char *p;
    long n;
    Value ev = x->n > 1 ? A(1) : kwget(x, "encoding");
    Value errv = x->n > 2 ? A(2) : kwget(x, "errors");
    const char *enc = "utf-8", *err = "strict";
    bx_self(A(0), &p, &n);
    if (ev.t == T_STR) enc = AS_STR(ev)->s;
    if (errv.t == T_STR) err = AS_STR(errv)->s;
    return bytes_decode(p, n, enc, err);
}
BI(b_hex)
{
    const char *p;
    long n, i;
    char t[4];
    Buf b;
    Value sepv = x->n > 1 ? A(1) : kwget(x, "sep");
    const char *sep = NULL;
    char sepc[2] = { 0, 0 };
    Value gv = x->n > 2 ? A(2) : kwget(x, "bytes_per_sep");
    long grp = 1;
    bx_self(A(0), &p, &n);
    if (sepv.t == T_STR && AS_STR(sepv)->len > 0) sep = AS_STR(sepv)->s;
    else if (sepv.t != T_UNSET && sepv.t != T_NONE && sepv.t != T_STR) {
        const char *bp;
        long bn;
        if (bytes_get(sepv, &bp, &bn) && bn == 1) { sepc[0] = bp[0]; sep = sepc; }
        else throw_new(cls_ValueError, "sep must be length 1.");
    }
    if (gv.t != T_UNSET && gv.t != T_NONE) grp = to_index(gv);
    buf_init(&b);
    for (i = 0; i < n; i++) {
        if (sep && i) {
            /* positive: groups counted from the right, negative: from the left */
            long k = grp > 0 ? n - i : i;
            long g = grp < 0 ? -grp : grp;
            if (g > 0 && k % g == 0) buf_add(&b, sep);
        }
        sprintf(t, "%02x", (unsigned char)p[i]);
        buf_add(&b, t);
    }
    return buf_value(&b);
}
BI(b_fromhex)
{
    Str *s;
    Buf b;
    long i;
    int hi = -1;
    Value r;
    need(x, 1, 2, "fromhex");
    s = astr(A(x->n - 1), "fromhex() argument");
    buf_init(&b);
    for (i = 0; i < s->len; i++) {
        int c = (unsigned char)s->s[i], d;
        if (ch_space[c] && hi < 0) continue;
        if (!ch_hex[c]) {
            buf_free(&b);
            throw_new(cls_ValueError,
                "non-hexadecimal number found in fromhex() arg at position %ld", i);
        }
        d = digit_val(c);
        if (hi < 0) hi = d; else { buf_addc(&b, hi * 16 + d); hi = -1; }
    }
    if (hi >= 0) {
        buf_free(&b);
        throw_new(cls_ValueError, "non-hexadecimal number found in fromhex() arg at position %ld",
            s->len);
    }
    /* called on the class bytearray gives a bytearray */
    r = (x->n == 2 && bx_isbarr(A(0))) ? mkbarr(b.s, b.n) : mkbytes(b.s, b.n);
    buf_free(&b);
    return r;
}
BI(ba_fromhex)
{
    Value r = b_fromhex(x);
    return mkbarr(AS_STR(r)->s, AS_STR(r)->len);
}

/* ---- methods through str ------------------------------------------------ */
static Value bx_tostr(Value v)
{
    const char *p;
    long n;
    if (bytes_get(v, &p, &n)) return mkstr(p, n);
    return v;
}
static Value bx_back(Value r, int barr)
{
    if (r.t == T_STR) return bx_make(barr, AS_STR(r)->s, AS_STR(r)->len);
    if (r.t == T_LIST || r.t == T_TUPLE) {
        long i;
        for (i = 0; i < AS_LIST(r)->n; i++) {
            Value e = AS_LIST(r)->a[i];
            if (e.t == T_STR) AS_LIST(r)->a[i] = bx_make(barr, AS_STR(e)->s, AS_STR(e)->len);
        }
    }
    return r;
}
static Value via_str(Args *x, BFn fn)
{
    Value *av = (Value *)xmalloc(sizeof(Value) * (size_t)(x->n + 1));
    Args a2;
    Value r;
    int i, barr = bx_isbarr(A(0));
    Value *base = vsp;
    for (i = 0; i < x->n; i++) {
        Value a = x->a[i];
        const char *bp;
        long bn;
        if (a.t == T_INST && AS_INST(a)->pay.t != T_UNSET) a = AS_INST(a)->pay;
        if (bytes_get(a, &bp, &bn)) av[i] = mkstr(bp, bn);
        else if (i == 1 && (fn == s_find || fn == s_count || fn == s_rfind) && a.t == T_INT &&
                 a.u.i >= 0 && a.u.i < 256) {
            char ch = (char)a.u.i;
            av[i] = mkstr(&ch, 1);
        } else if (i > 0 && (a.t == T_LIST || a.t == T_TUPLE || a.t == T_GEN || a.t == T_ITER ||
                             a.t == T_SET || a.t == T_DVIEW)) {
            Value l = iter_to_list(a);
            long k;
            VPUSH(l);
            for (k = 0; k < AS_LIST(l)->n; k++) AS_LIST(l)->a[k] = bx_tostr(AS_LIST(l)->a[k]);
            av[i] = l;
        } else av[i] = a;
        VPUSH(av[i]);
    }
    a2.n = x->n; a2.a = av; a2.nk = x->nk; a2.kw = x->kw;
    r = fn(&a2);
    vsp = base;
    free(av);
    return bx_back(r, barr);
}
#define BVIA(name, fn) BI(name) { return via_str(x, fn); }
BVIA(b_upper, s_upper) BVIA(b_lower, s_lower) BVIA(b_strip, s_strip) BVIA(b_lstrip, s_lstrip)
BVIA(b_rstrip, s_rstrip) BVIA(b_split, s_split) BVIA(b_rsplit, s_rsplit)
BVIA(b_replace, s_replace) BVIA(b_find, s_find) BVIA(b_count, s_count)
BVIA(b_startswith, s_startswith) BVIA(b_endswith, s_endswith) BVIA(b_rfind, s_rfind)
BVIA(b_splitlines, s_splitlines) BVIA(b_isdigit, s_isdigit) BVIA(b_isalpha, s_isalpha)
BVIA(b_isalnum, s_isalnum) BVIA(b_isspace, s_isspace) BVIA(b_isupper, s_isupper)
BVIA(b_islower, s_islower) BVIA(b_center, s_center) BVIA(b_ljust, s_ljust)
BVIA(b_rjust, s_rjust) BVIA(b_zfill, s_zfill) BVIA(b_partition, s_partition)
BVIA(b_rpartition, s_rpartition) BVIA(b_title, s_title) BVIA(b_capitalize, s_capitalize)
BVIA(b_swapcase, s_swapcase) BVIA(b_expandtabs, s_expandtabs)
BVIA(b_removeprefix, s_removeprefix) BVIA(b_removesuffix, s_removesuffix)
BVIA(b_istitle, s_istitle)
BI(b_index)
{
    Value r = via_str(x, s_find);
    if (r.u.i < 0) throw_new(cls_ValueError, "subsection not found");
    return r;
}
BI(b_rindex)
{
    Value r = via_str(x, s_rfind);
    if (r.u.i < 0) throw_new(cls_ValueError, "subsection not found");
    return r;
}
BI(b_join)
{
    const char *sp, *ep;
    long sn, en, k = 0;
    Value it, y;
    Buf b;
    int barr = bx_isbarr(A(0));
    need(x, 2, 2, "join");
    bx_self(A(0), &sp, &sn);
    it = get_iter(A(1));
    VPUSH(it);
    buf_init(&b);
    while (iter_next(it, &y)) {
        if (!bytes_get(y, &ep, &en)) {
            buf_free(&b);
            throw_new(cls_TypeError, "sequence item %ld: expected a bytes-like object, %.40s found",
                k, type_name(y));
        }
        if (k) buf_addn(&b, sp, sn);
        buf_addn(&b, ep, en);
        k++;
    }
    vsp--;
    y = bx_make(barr, b.s, b.n);
    buf_free(&b);
    return y;
}
BI(b_translate)
{
    const char *p, *tp = NULL, *dp = NULL;
    long n, tn = 0, dn = 0, i;
    Value dv = x->n > 2 ? A(2) : kwget(x, "delete");
    Buf b;
    Value r;
    int barr = bx_isbarr(A(0));
    bx_self(A(0), &p, &n);
    need(x, 2, 3, "translate");
    if (A(1).t != T_NONE) {
        bx_need(A(1), &tp, &tn);
        if (tn != 256) throw_new(cls_ValueError, "translation table must be 256 characters long");
    }
    if (dv.t != T_UNSET) bx_need(dv, &dp, &dn);
    buf_init(&b);
    for (i = 0; i < n; i++) {
        if (dp && memchr(dp, p[i], (size_t)dn)) continue;
        buf_addc(&b, tp ? tp[(unsigned char)p[i]] : p[i]);
    }
    r = bx_make(barr, b.s, b.n);
    buf_free(&b);
    return r;
}
BI(b_maketrans)
{
    const char *fp, *tp;
    long fn, tn, i;
    char t[256];
    need(x, 2, 2, "maketrans");
    bx_need(A(0), &fp, &fn);
    bx_need(A(1), &tp, &tn);
    if (fn != tn) throw_new(cls_ValueError, "maketrans arguments must have same length");
    for (i = 0; i < 256; i++) t[i] = (char)i;
    for (i = 0; i < fn; i++) t[(unsigned char)fp[i]] = tp[i];
    return mkbytes(t, 256);
}
BI(b_bytes_dunder)
{
    const char *p;
    long n;
    bx_self(A(0), &p, &n);
    return mkbytes(p, n);
}

/* ---- bytearray ----------------------------------------------------------- */
static BArr *barr_self(Args *x)
{
    Value v = A(0);
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET) v = AS_INST(v)->pay;
    if (v.t != T_BARR)
        throw_new(cls_TypeError, "descriptor requires a 'bytearray' object but received '%.40s'",
            type_name(A(0)));
    return AS_BARR(v);
}
static int byte_arg(Value v)
{
    if (v.t != T_INT && v.t != T_BOOL)
        throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
            type_name(v));
    if (v.u.i < 0 || v.u.i > 255) throw_new(cls_ValueError, "byte must be in range(0, 256)");
    return (int)v.u.i;
}
BI(ba_append)
{
    BArr *a = barr_self(x);
    int c;
    need(x, 2, 2, "append");
    c = byte_arg(A(1));
    barr_reserve(a, a->n + 1);
    a->s[a->n++] = (char)c;
    return NONE_V;
}
BI(ba_extend)
{
    BArr *a = barr_self(x);
    const char *p;
    long n;
    need(x, 2, 2, "extend");
    if (bytes_get(A(1), &p, &n)) {
        Buf t;
        buf_init(&t);
        buf_addn(&t, p, n);
        barr_reserve(a, a->n + n);
        memcpy(a->s + a->n, t.s, (size_t)n);
        a->n += n;
        buf_free(&t);
    } else {
        Value it, y;
        if (A(1).t == T_STR)
            throw_new(cls_TypeError, "expected iterable of integers; got: 'str'");
        it = get_iter(A(1));
        VPUSH(it);
        while (iter_next(it, &y)) {
            int c = byte_arg(y);
            barr_reserve(a, a->n + 1);
            a->s[a->n++] = (char)c;
        }
        vsp--;
    }
    return NONE_V;
}
BI(ba_pop)
{
    BArr *a = barr_self(x);
    long i;
    int c;
    need(x, 1, 2, "pop");
    if (a->n == 0) throw_new(cls_IndexError, "pop from empty bytearray");
    i = x->n > 1 ? to_index(A(1)) : -1;
    if (i < 0) i += a->n;
    if (i < 0 || i >= a->n) throw_new(cls_IndexError, "pop index out of range");
    c = (unsigned char)a->s[i];
    memmove(a->s + i, a->s + i + 1, (size_t)(a->n - i - 1));
    a->n--;
    return mkint(c);
}
BI(ba_insert)
{
    BArr *a = barr_self(x);
    long i;
    int c;
    need(x, 3, 3, "insert");
    i = to_ssize(A(1));
    c = byte_arg(A(2));
    if (i < 0) { i += a->n; if (i < 0) i = 0; }
    if (i > a->n) i = a->n;
    barr_reserve(a, a->n + 1);
    memmove(a->s + i + 1, a->s + i, (size_t)(a->n - i));
    a->s[i] = (char)c;
    a->n++;
    return NONE_V;
}
BI(ba_remove)
{
    BArr *a = barr_self(x);
    int c;
    char *q;
    need(x, 2, 2, "remove");
    c = byte_arg(A(1));
    q = (char *)memchr(a->s, c, (size_t)a->n);
    if (!q) throw_new(cls_ValueError, "value not found in bytearray");
    memmove(q, q + 1, (size_t)(a->n - (q - a->s) - 1));
    a->n--;
    return NONE_V;
}
BI(ba_clear)
{
    barr_self(x)->n = 0;
    return NONE_V;
}
BI(ba_reverse)
{
    BArr *a = barr_self(x);
    long i, j;
    for (i = 0, j = a->n - 1; i < j; i++, j--) {
        char t = a->s[i]; a->s[i] = a->s[j]; a->s[j] = t;
    }
    return NONE_V;
}
BI(ba_copy)
{
    BArr *a = barr_self(x);
    return mkbarr(a->s, a->n);
}
/* ---- int.to_bytes / int.from_bytes -------------------------------------- */
/* n bytes of v (two's complement if signed); 0 if it does not fit */
static int int_pack_bytes(Value v, long n, int big, int sg, char *out)
{
    long i;
    if (int_sign(v) < 0 && !sg) return -1;
    if (n == 0) return int_sign(v) == 0;
    if (v.t == T_INT && n <= 64) {
        unsigned long u = (unsigned long)v.u.i;
        int neg = v.u.i < 0;
        if (n < (long)sizeof(long)) {
            if (!sg && (u >> (8 * n)) != 0) return 0;
            if (sg) {
                long lim = 1L << (8 * n - 1);
                if (v.u.i >= lim || v.u.i < -lim) return 0;
            }
        }
        for (i = 0; i < n; i++) {
            unsigned c;
            if (i < (long)sizeof(long)) c = (unsigned)((u >> (8 * i)) & 255UL);
            else c = neg ? 255U : 0U;
            out[big ? n - 1 - i : i] = (char)c;
        }
        return 1;
    }
    {
        Value lim;
        if (sg) {
            lim = int_pow(mkint(2), 8 * n - 1);
            if (n == 0 || int_cmp(v, lim) >= 0 || int_cmp(v, int_neg(lim)) < 0) return 0;
            if (int_sign(v) < 0) v = int_add(int_pow(mkint(256), n), v);
        } else {
            lim = int_pow(mkint(256), n);
            if (int_cmp(v, lim) >= 0) return 0;
        }
    }
    for (i = 0; i < n; i++) {
        Value q, r;
        int_divmod(v, mkint(256), &q, &r);
        out[big ? n - 1 - i : i] = (char)r.u.i;
        v = q;
    }
    return 1;
}
static Value int_unpack_bytes(const char *p, long n, int big, int sg)
{
    long i;
    int neg = sg && n > 0 && ((unsigned char)p[big ? 0 : n - 1] & 128);
    if (n < (long)sizeof(long)) {              /* fits a long, even with the sign */
        unsigned long u = 0;
        for (i = 0; i < n; i++) u = (u << 8) | (unsigned char)p[big ? i : n - 1 - i];
        if (neg) return mkint((long)u - (1L << (8 * n)));
        return mkint((long)u);
    }
    {
        Value r = mkint(0);
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)p[big ? i : n - 1 - i];
            r = int_add(int_mul(r, mkint(256)), mkint(c));
        }
        if (neg) r = int_sub(r, int_pow(mkint(256), n));
        return r;
    }
}
BI(i_to_bytes)
{
    Value v = unpay(A(0)), kv;
    long n;
    int big = 1, sg = 0, rc;
    char *out;
    Value r;
    need(x, 1, 3, "to_bytes");
    n = x->n > 1 ? to_index(A(1)) : (kv = kwget(x, "length"), kv.t == T_UNSET ? 1 : to_index(kv));
    kv = x->n > 2 ? A(2) : kwget(x, "byteorder");
    if (kv.t == T_STR) {
        if (strcmp(AS_STR(kv)->s, "little") == 0) big = 0;
        else if (strcmp(AS_STR(kv)->s, "big") != 0)
            throw_new(cls_ValueError, "byteorder must be either 'little' or 'big'");
    }
    kv = kwget(x, "signed");
    if (kv.t != T_UNSET && py_truthy(kv)) sg = 1;
    if (n < 0) throw_new(cls_ValueError, "length argument must be non-negative");
    out = (char *)xmalloc((size_t)n + 1);
    rc = int_pack_bytes(v, n, big, sg, out);
    if (rc < 0) { free(out); throw_new(cls_OverflowError,
        "can't convert negative int to unsigned"); }
    if (rc == 0) { free(out); throw_new(cls_OverflowError, "int too big to convert"); }
    r = mkbytes(out, n);
    free(out);
    return r;
}
BI(i_from_bytes)
{
    Value src = A(0), kv;
    const char *p;
    long n;
    int big = 1, sg = 0;
    if (x->n < 1) throw_new(cls_TypeError,
        "from_bytes() missing required argument 'bytes' (pos 1)");
    if (src.t == T_CLASS) {            /* called through an instance or the class object */
        Args a2;
        if (x->n < 2) throw_new(cls_TypeError, "from_bytes() missing required argument 'bytes'");
        a2 = *x; a2.n = x->n - 1; a2.a = x->a + 1;
        x = &a2;
        src = A(0);
    }
    if (!bytes_get(src, &p, &n)) {
        src = bytes_build(x, 0);
        p = AS_STR(src)->s; n = AS_STR(src)->len;
    }
    kv = x->n > 1 ? A(1) : kwget(x, "byteorder");
    if (kv.t == T_STR) {
        if (strcmp(AS_STR(kv)->s, "little") == 0) big = 0;
        else if (strcmp(AS_STR(kv)->s, "big") != 0)
            throw_new(cls_ValueError, "byteorder must be either 'little' or 'big'");
    }
    kv = kwget(x, "signed");
    if (kv.t != T_UNSET && py_truthy(kv)) sg = 1;
    return int_unpack_bytes(p, n, big, sg);
}

/* =========================================================================
   struct: pack unpack calcsize pack_into unpack_from iter_unpack
   Formats: x c b B ? h H i I l L q Q n N f d e s p P, byte order @ = < > !
   Native mode ('@' or none) uses the C sizes and alignment of this host;
   the other modes use the standard sizes without padding.  Floats are
   written through the host's IEEE representation (see PORTING.md).
   ========================================================================= */
static Class *cls_struct_error;
typedef struct SItem { int code; long count; long size; long off; } SItem;
typedef struct SFmt { int native, big; SItem *it; int n, cap; long total; } SFmt;

static int host_is_big(void)
{
    unsigned long one = 1UL;
    return !*(unsigned char *)&one;
}
static void struct_err(const char *msg)
{
    throw_new(cls_struct_error, "%s", msg);
}
/* size and alignment of one format code */
static long struct_size(int code, int native)
{
    switch (code) {
    case 'x': case 'c': case 'b': case 'B': case '?': case 's': case 'p': return 1;
    case 'h': case 'H': case 'e': return 2;
    case 'i': case 'I': case 'f': return 4;
    case 'l': case 'L': return native ? (long)sizeof(long) : 4;
    case 'q': case 'Q': case 'd': return 8;
    case 'n': case 'N': case 'P': return native ? (long)sizeof(void *) : -1;
    default: return -1;
    }
}
static void struct_parse(const char *f, long fl, SFmt *sf)
{
    long i = 0, pos = 0;
    sf->native = 1; sf->big = host_is_big(); sf->n = 0; sf->cap = 8; sf->total = 0;
    sf->it = (SItem *)xmalloc(sizeof(SItem) * (size_t)sf->cap);
    if (fl > 0) {
        int c = (unsigned char)f[0];
        if (c == '@') i = 1;
        else if (c == '=') { sf->native = 0; i = 1; }
        else if (c == '<') { sf->native = 0; sf->big = 0; i = 1; }
        else if (c == '>' || c == '!') { sf->native = 0; sf->big = 1; i = 1; }
    }
    while (i < fl) {
        int c = (unsigned char)f[i];
        long cnt = 0, size, al;
        int hasnum = 0;
        if (ch_space[c]) { i++; continue; }
        while (i < fl && ch_digit[(unsigned char)f[i]]) {
            cnt = cnt * 10 + (f[i] - '0');
            hasnum = 1;
            i++;
            if (cnt > 100000000L) { free(sf->it); struct_err("total struct size too long"); }
        }
        if (i >= fl) { free(sf->it); struct_err("repeat count given without format specifier"); }
        c = (unsigned char)f[i++];
        if (!hasnum) cnt = 1;
        size = struct_size(c, sf->native);
        if (size < 0) { free(sf->it); struct_err("bad char in struct format"); }
        if (c == 'e') { free(sf->it); struct_err("half-float format 'e' is not supported"); }
        al = sf->native ? size : 1;
        if (c == 's' || c == 'p' || c == 'x' || c == 'c' || c == 'b' || c == 'B'
            || c == '?') al = 1;
        if (al > 1) pos = (pos + al - 1) / al * al;
        if (sf->n >= sf->cap) {
            sf->cap *= 2;
            sf->it = (SItem *)xrealloc(sf->it, sizeof(SItem) * (size_t)sf->cap);
        }
        sf->it[sf->n].code = c;
        sf->it[sf->n].count = cnt;
        sf->it[sf->n].size = size;
        sf->it[sf->n].off = pos;
        sf->n++;
        pos += (c == 's' || c == 'p') ? cnt : size * cnt;
        if (pos > 1000000000L) { free(sf->it); struct_err("total struct size too long"); }
    }
    sf->total = pos;
}
static int struct_nvalues(SFmt *sf)
{
    int i, n = 0;
    for (i = 0; i < sf->n; i++) {
        int c = sf->it[i].code;
        if (c == 'x') continue;
        if (c == 's' || c == 'p') n++;
        else n += (int)sf->it[i].count;
    }
    return n;
}
static void struct_put_float(double d, int size, int big, char *out)
{
    unsigned char t[8];
    int i;
    if (size == 4) {
        float f = (float)d;
        memcpy(t, &f, 4);
    } else memcpy(t, &d, 8);
    if (big != host_is_big()) {
        for (i = 0; i < size; i++) out[i] = (char)t[size - 1 - i];
    } else {
        for (i = 0; i < size; i++) out[i] = (char)t[i];
    }
}
static double struct_get_float(const char *p, int size, int big)
{
    unsigned char t[8];
    int i;
    if (big != host_is_big()) for (i = 0; i < size; i++) t[i] = (unsigned char)p[size - 1 - i];
    else for (i = 0; i < size; i++) t[i] = (unsigned char)p[i];
    if (size == 4) {
        float f;
        memcpy(&f, t, 4);
        return (double)f;
    } else {
        double d;
        memcpy(&d, t, 8);
        return d;
    }
}
static const char *struct_range_msg(int code, long size, int sg, char *buf)
{
    static const char *lo[] = { "", "-128", "-32768", "", "-2147483648", "", "", "",
                                "-9223372036854775808" };
    static const char *shi[] = { "", "127", "32767", "", "2147483647", "", "", "",
                                 "9223372036854775807" };
    static const char *uhi[] = { "", "255", "65535", "", "4294967295", "", "", "",
                                 "18446744073709551615" };
    long k = size;
    if (k < 1 || k > 8) k = 8;
    if (sg) sprintf(buf, "'%c' format requires %s <= number <= %s", code, lo[k], shi[k]);
    else sprintf(buf, "'%c' format requires 0 <= number <= %s", code, uhi[k]);
    return buf;
}
/* pack the values vals[0..nv) into out (sf->total bytes, zero filled by the caller) */
static void struct_pack_into(SFmt *sf, Value *vals, char *out)
{
    int i, vi = 0;
    long k;
    char msg[160];
    for (i = 0; i < sf->n; i++) {
        SItem *it = &sf->it[i];
        char *dst = out + it->off;
        switch (it->code) {
        case 'x': break;
        case 's': case 'p': {
            const char *p;
            long n;
            if (!bytes_get(vals[vi], &p, &n)) struct_err("argument for 's' must be a bytes object");
            vi++;
            if (it->code == 'p') {
                long m = it->count > 0 ? it->count - 1 : 0;
                if (n > m) n = m;
                if (it->count > 0) dst[0] = (char)(n > 255 ? 255 : n);
                if (n > 0) memcpy(dst + 1, p, (size_t)n);
            } else {
                if (n > it->count) n = it->count;
                if (n > 0) memcpy(dst, p, (size_t)n);
            }
            break; }
        default:
            for (k = 0; k < it->count; k++, dst += it->size, vi++) {
                Value v = vals[vi];
                switch (it->code) {
                case 'c': {
                    const char *p;
                    long n;
                    if (!bytes_get(v, &p, &n) || n != 1)
                        struct_err("char format requires a bytes object of length 1");
                    dst[0] = p[0];
                    break; }
                case '?': dst[0] = py_truthy(v) ? 1 : 0; break;
                case 'f': case 'd':
                    if (v.t == T_FLOAT || is_num(v)) {
                        struct_put_float(to_dbl(v), (int)it->size, sf->big, dst);
                        if (it->code == 'f' && fabs(to_dbl(v)) > 3.4028235677973366e38 &&
                            to_dbl(v) == to_dbl(v) && to_dbl(v) - to_dbl(v) == 0)
                            struct_err("float too large to pack with f format");
                    } else struct_err("required argument is not a float");
                    break;
                default: {
                    int sg = it->code == 'b' || it->code == 'h' || it->code == 'i' ||
                        it->code == 'l' || it->code == 'q' || it->code == 'n';
                    int rc;
                    if (v.t == T_INST) {
                        Value *m = class_find(AS_INST(v)->cls, S_index);
                        if (m) v = call1(*m, v);
                    }
                    if (!is_intv(v) && v.t != T_BOOL)
                        struct_err("required argument is not an integer");
                    if (v.t == T_BOOL) v = mkint(v.u.i);
                    rc = int_pack_bytes(v, it->size, sf->big, sg, dst);
                    if (rc <= 0) struct_err(struct_range_msg(it->code, it->size, sg, msg));
                    break; }
                }
            }
        }
    }
}
static Value struct_unpack_at(SFmt *sf, const char *p, Value *keep)
{
    int i, nv = struct_nvalues(sf), vi = 0;
    List *t = tuple_new(nv);
    Value tv = mkobj(t, T_TUPLE);
    long k;
    for (i = 0; i < nv; i++) t->a[i] = NONE_V;
    (void)keep;
    VPUSH(tv);
    for (i = 0; i < sf->n; i++) {
        SItem *it = &sf->it[i];
        const char *src = p + it->off;
        switch (it->code) {
        case 'x': break;
        case 's': t->a[vi++] = mkbytes(src, it->count); break;
        case 'p': {
            long n = it->count > 0 ? (unsigned char)src[0] : 0;
            if (n > it->count - 1) n = it->count - 1;
            t->a[vi++] = mkbytes(src + 1, n > 0 ? n : 0);
            break; }
        default:
            for (k = 0; k < it->count; k++, src += it->size) {
                switch (it->code) {
                case 'c': t->a[vi++] = mkbytes(src, 1); break;
                case '?': t->a[vi++] = mkbool(src[0] != 0); break;
                case 'f': case 'd':
                    t->a[vi++] = mkfloat(struct_get_float(src, (int)it->size, sf->big));
                    break;
                default: {
                    int sg = it->code == 'b' || it->code == 'h' || it->code == 'i' ||
                        it->code == 'l' || it->code == 'q' || it->code == 'n';
                    t->a[vi++] = int_unpack_bytes(src, it->size, sf->big, sg);
                    break; }
                }
            }
        }
    }
    vsp--;
    return tv;
}
static Str *struct_fmt_arg(Value v)
{
    if (v.t == T_STR) return AS_STR(v);
    {
        const char *p;
        long n;
        if (bytes_get(v, &p, &n)) return AS_STR(mkstr(p, n));
    }
    throw_new(cls_TypeError, "Struct() argument 1 must be a str or bytes object, not %.40s",
        type_name(v));
    return NULL;
}
BI(st_calcsize)
{
    SFmt sf;
    Str *f;
    long n;
    need(x, 1, 1, "calcsize");
    f = struct_fmt_arg(A(0));
    struct_parse(f->s, f->len, &sf);
    n = sf.total;
    free(sf.it);
    return mkint(n);
}
BI(st_pack)
{
    SFmt sf;
    Str *f;
    char *out;
    Value r;
    int nv;
    if (x->n < 1) throw_new(cls_TypeError, "pack expected at least 1 argument");
    f = struct_fmt_arg(A(0));
    struct_parse(f->s, f->len, &sf);
    nv = struct_nvalues(&sf);
    if (x->n - 1 != nv) {
        free(sf.it);
        throw_new(cls_struct_error, "pack expected %d items for packing (got %d)", nv, x->n - 1);
    }
    out = (char *)xmalloc((size_t)sf.total + 1);
    memset(out, 0, (size_t)sf.total + 1);
    struct_pack_into(&sf, x->a + 1, out);
    r = mkbytes(out, sf.total);
    free(out);
    free(sf.it);
    return r;
}
BI(st_unpack)
{
    SFmt sf;
    Str *f;
    const char *p;
    long n;
    Value r;
    need(x, 2, 2, "unpack");
    f = struct_fmt_arg(A(0));
    if (!bytes_get(A(1), &p, &n)) bx_need(A(1), &p, &n);
    struct_parse(f->s, f->len, &sf);
    if (n != sf.total) {
        long tot = sf.total;
        free(sf.it);
        throw_new(cls_struct_error, "unpack requires a buffer of %ld bytes", tot);
    }
    r = struct_unpack_at(&sf, p, NULL);
    free(sf.it);
    return r;
}
BI(st_unpack_from)
{
    SFmt sf;
    Str *f;
    const char *p;
    long n, off = 0;
    Value r, ov;
    need(x, 2, 3, "unpack_from");
    f = struct_fmt_arg(A(0));
    bx_need(A(1), &p, &n);
    ov = x->n > 2 ? A(2) : kwget(x, "offset");
    if (ov.t != T_UNSET) off = to_index(ov);
    if (off < 0) off += n;
    struct_parse(f->s, f->len, &sf);
    if (off < 0 || off + sf.total > n) {
        long tot = sf.total;
        free(sf.it);
        throw_new(cls_struct_error,
            "unpack_from requires a buffer of at least %ld bytes for unpacking %ld bytes at "
            "offset %ld (actual buffer size is %ld)", off + tot, tot, off, n);
    }
    r = struct_unpack_at(&sf, p + off, NULL);
    free(sf.it);
    return r;
}
BI(st_pack_into)
{
    SFmt sf;
    Str *f;
    BArr *ba;
    long off;
    int nv;
    char *tmp;
    if (x->n < 3) throw_new(cls_TypeError, "pack_into expected buffer and offset");
    f = struct_fmt_arg(A(0));
    if (A(1).t != T_BARR) throw_new(cls_TypeError, "argument must be read-write bytes-like object");
    ba = AS_BARR(A(1));
    off = to_index(A(2));
    if (off < 0) off += ba->n;
    struct_parse(f->s, f->len, &sf);
    nv = struct_nvalues(&sf);
    if (x->n - 3 != nv) {
        free(sf.it);
        throw_new(cls_struct_error, "pack_into expected %d items for packing (got %d)", nv,
            x->n - 3);
    }
    if (off < 0 || off + sf.total > ba->n) {
        long tot = sf.total;
        free(sf.it);
        throw_new(cls_struct_error,
            "pack_into requires a buffer of at least %ld bytes for packing %ld bytes at offset "
            "%ld (actual buffer size is %ld)", off + tot, tot, off, ba->n);
    }
    tmp = (char *)xmalloc((size_t)sf.total + 1);
    memset(tmp, 0, (size_t)sf.total + 1);
    struct_pack_into(&sf, x->a + 3, tmp);
    memcpy(ba->s + off, tmp, (size_t)sf.total);
    free(tmp);
    free(sf.it);
    return NONE_V;
}
BI(st_iter_unpack)
{
    SFmt sf;
    Str *f;
    const char *p;
    long n, off;
    List *res;
    Value rv;
    need(x, 2, 2, "iter_unpack");
    f = struct_fmt_arg(A(0));
    bx_need(A(1), &p, &n);
    struct_parse(f->s, f->len, &sf);
    if (sf.total == 0 || n % sf.total != 0) {
        long tot = sf.total;
        free(sf.it);
        if (tot == 0) throw_new(cls_struct_error,
            "cannot iteratively unpack with a struct of length 0");
        throw_new(cls_struct_error,
            "iterative unpacking requires a buffer of a multiple of %ld bytes", tot);
    }
    res = list_new(n / sf.total);
    rv = mklist(res);
    VPUSH(rv);
    for (off = 0; off < n; off += sf.total) list_push(res, struct_unpack_at(&sf, p + off, NULL));
    vsp--;
    free(sf.it);
    return get_iter(rv);
}

/* struct.Struct objects: instances of cls_Struct with the attributes format and size */
static Class *cls_Struct;
static Value struct_obj_call(Args *x, BFn fn)
{
    Value *fmt, *av, r;
    int i;
    Value *base = vsp;
    fmt = NULL;
    if (x->n >= 1 && A(0).t == T_INST) fmt = amap_get(&AS_INST(A(0))->a, intern_c("format"));
    if (!fmt)
        throw_new(cls_TypeError, "descriptor requires a 'Struct' object");
    av = (Value *)xmalloc(sizeof(Value) * (size_t)(x->n + 1));
    av[0] = *fmt;
    for (i = 1; i < x->n; i++) av[i] = x->a[i];
    for (i = 0; i < x->n; i++) VPUSH(av[i]);
    {
        Args a2;
        a2.n = x->n; a2.a = av; a2.nk = x->nk; a2.kw = x->kw;
        r = fn(&a2);
    }
    vsp = base;
    free(av);
    return r;
}
BI(sto_pack) { return struct_obj_call(x, st_pack); }
BI(sto_unpack) { return struct_obj_call(x, st_unpack); }
BI(sto_unpack_from) { return struct_obj_call(x, st_unpack_from); }
BI(sto_pack_into) { return struct_obj_call(x, st_pack_into); }
BI(sto_iter_unpack) { return struct_obj_call(x, st_iter_unpack); }
BI(sto_new)
{
    Inst *in;
    Value iv, sz;
    Args a2;
    Value fv;
    need(x, 1, 1, "Struct");
    if (A(0).t == T_BYTES) fv = mkstr(AS_STR(A(0))->s, AS_STR(A(0))->len);
    else fv = A(0);
    a2.n = 1; a2.a = &fv; a2.nk = 0; a2.kw = NULL;
    sz = st_calcsize(&a2);
    in = new_inst(cls_Struct);
    iv = mkobj(in, T_INST);
    VPUSH(iv);
    amap_set(&in->a, intern_c("format"), fv);
    amap_set(&in->a, intern_c("size"), sz);
    vsp--;
    return iv;
}

/* a class that lives in a native module (not a builtin name) */
static Class *mod_class(const char *name, Class *base, const char *modname)
{
    Class *c = (Class *)gc_alloc(sizeof(Class), T_CLASS);
    c->name = intern_c(name);
    c->base = base;
    c->ns.k = NULL; c->ns.v = NULL; c->ns.n = c->ns.cap = 0;
    c->ctor = NULL;
    c->mro = NULL; c->nmro = 0; c->bases = NULL; c->nbases = 0; c->paycls = NULL;
    c->modname = intern_c(modname);
    c->flags = CF_BUILTIN | (base ? (base->flags & CF_EXC) : 0);
    return c;
}

static void bytes_init(void)
{
    Class *bc[2];
    int k;
    cls_bytes = mk_type("bytes", cls_object, bi_bytes);
    cls_bytearray = mk_type("bytearray", cls_object, bi_bytearray);
    bc[0] = cls_bytes; bc[1] = cls_bytearray;
    for (k = 0; k < 2; k++) {
        Class *c = bc[k];
        def_bi(c, "decode", b_decode); def_bi(c, "hex", b_hex);
        def_bi(c, "upper", b_upper); def_bi(c, "lower", b_lower);
        def_bi(c, "strip", b_strip); def_bi(c, "lstrip", b_lstrip);
        def_bi(c, "rstrip", b_rstrip); def_bi(c, "split", b_split);
        def_bi(c, "rsplit", b_rsplit); def_bi(c, "join", b_join);
        def_bi(c, "replace", b_replace); def_bi(c, "find", b_find);
        def_bi(c, "count", b_count); def_bi(c, "startswith", b_startswith);
        def_bi(c, "endswith", b_endswith); def_bi(c, "rfind", b_rfind);
        def_bi(c, "index", b_index); def_bi(c, "rindex", b_rindex);
        def_bi(c, "splitlines", b_splitlines); def_bi(c, "isdigit", b_isdigit);
        def_bi(c, "isalpha", b_isalpha); def_bi(c, "isalnum", b_isalnum);
        def_bi(c, "isspace", b_isspace); def_bi(c, "isupper", b_isupper);
        def_bi(c, "islower", b_islower); def_bi(c, "istitle", b_istitle);
        def_bi(c, "center", b_center); def_bi(c, "ljust", b_ljust);
        def_bi(c, "rjust", b_rjust); def_bi(c, "zfill", b_zfill);
        def_bi(c, "partition", b_partition); def_bi(c, "rpartition", b_rpartition);
        def_bi(c, "title", b_title); def_bi(c, "capitalize", b_capitalize);
        def_bi(c, "swapcase", b_swapcase); def_bi(c, "expandtabs", b_expandtabs);
        def_bi(c, "removeprefix", b_removeprefix); def_bi(c, "removesuffix", b_removesuffix);
        def_bi(c, "translate", b_translate); def_bi(c, "maketrans", b_maketrans);
        def_bi(c, "__len__", nat_len); def_bi(c, "__iter__", nat_iter);
        def_bi(c, "__contains__", nat_contains); def_bi(c, "__repr__", nat_repr);
        def_bi(c, "__eq__", nat_eq); def_bi(c, "__ne__", nat_ne);
        def_bi(c, "__lt__", nat_lt); def_bi(c, "__le__", nat_le);
        def_bi(c, "__gt__", nat_gt); def_bi(c, "__ge__", nat_ge);
        def_bi(c, "__getitem__", nat_getitem);
        def_bi(c, "__add__", nat_add); def_bi(c, "__mul__", nat_mul);
        def_bi(c, "__rmul__", nat_mul);
    }
    def_bi(cls_bytes, "fromhex", b_fromhex);
    def_bi(cls_bytearray, "fromhex", ba_fromhex);
    def_bi(cls_bytes, "__hash__", nat_hash);
    def_bi(cls_bytes, "__bytes__", b_bytes_dunder);
    def_bi(cls_bytearray, "append", ba_append); def_bi(cls_bytearray, "extend", ba_extend);
    def_bi(cls_bytearray, "pop", ba_pop); def_bi(cls_bytearray, "insert", ba_insert);
    def_bi(cls_bytearray, "remove", ba_remove); def_bi(cls_bytearray, "clear", ba_clear);
    def_bi(cls_bytearray, "reverse", ba_reverse); def_bi(cls_bytearray, "copy", ba_copy);
    def_bi(cls_bytearray, "__setitem__", nat_setitem);
    def_bi(cls_bytearray, "__delitem__", nat_delitem);
    def_bi(cls_int, "to_bytes", i_to_bytes); def_bi(cls_int, "from_bytes", i_from_bytes);
}
static void struct_init(void)
{
    Module *m = new_module("struct");
    cls_struct_error = mod_class("error", cls_Exception, "struct");
    cls_Struct = mod_class("Struct", cls_object, "struct");
    cls_Struct->ctor = sto_new;
    def_bi(cls_Struct, "pack", sto_pack); def_bi(cls_Struct, "unpack", sto_unpack);
    def_bi(cls_Struct, "unpack_from", sto_unpack_from);
    def_bi(cls_Struct, "pack_into", sto_pack_into);
    def_bi(cls_Struct, "iter_unpack", sto_iter_unpack);
    mod_def(m, "pack", st_pack); mod_def(m, "unpack", st_unpack);
    mod_def(m, "calcsize", st_calcsize); mod_def(m, "pack_into", st_pack_into);
    mod_def(m, "unpack_from", st_unpack_from); mod_def(m, "iter_unpack", st_iter_unpack);
    mod_val(m, "error", mkobj(cls_struct_error, T_CLASS));
    mod_val(m, "Struct", mkobj(cls_Struct, T_CLASS));
}
