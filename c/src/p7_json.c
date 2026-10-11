/* =========================================================================
   p7_json.c   native json module: dumps / dump / loads / load, JSONEncoder, JSONDecoder,
               JSONDecodeError.  Strings are UTF-8 bytes; decode error positions are reported in
               characters like CPython, everything else (spans, raw_decode) in bytes.
   ========================================================================= */

static Class *cls_json_error, *cls_json_encoder, *cls_json_decoder;
static Str *JS_msg, *JS_doc, *JS_pos, *JS_lineno, *JS_colno, *JS_skipkeys, *JS_ascii, *JS_circ,
    *JS_nan, *JS_sortk, *JS_indent, *JS_isep, *JS_ksep, *JS_default, *JS_ohook, *JS_phook,
    *JS_pfloat, *JS_pint, *JS_pconst, *JS_strict, *JS_encode, *JS_decode;

#define JMAXDEPTH 900

/* ---- errors ------------------------------------------------------------ */
static void json_pos_info(Value docv, long bytepos, long *cpos, long *lineno, long *colno)
{
    long i, cp = 0, ln = 1, lastnl = -1, extra = 0;
    if (docv.t == T_STR) {
        Str *d = AS_STR(docv);
        if (bytepos > d->len) { extra = bytepos - d->len; bytepos = d->len; }
        for (i = 0; i < bytepos; i++) {
            if (d->s[i] == '\n') { ln++; lastnl = cp; }
            if (((unsigned char)d->s[i] & 0xC0) != 0x80) cp++;
        }
    } else {
        cp = bytepos;
    }
    cp += extra;
    *cpos = cp; *lineno = ln; *colno = cp - lastnl;
}
static void json_fill_error(Inst *in, Value msgv, Value docv, long pos, long cpos, long lineno,
                            long colno)
{
    Buf b;
    char tmp[96];
    Value full;
    List *t;
    buf_init(&b);
    buf_addn(&b, AS_STR(msgv)->s, AS_STR(msgv)->len);
    sprintf(tmp, ": line %ld column %ld (char %ld)", lineno, colno, cpos);
    buf_add(&b, tmp);
    full = buf_value(&b);
    VPUSH(full);
    t = tuple_new(1);
    t->a[0] = full;
    amap_set(&in->a, S_args, mkobj(t, T_TUPLE));
    amap_set(&in->a, JS_msg, msgv);
    amap_set(&in->a, JS_doc, docv);
    amap_set(&in->a, JS_pos, mkint(cpos));
    amap_set(&in->a, JS_lineno, mkint(lineno));
    amap_set(&in->a, JS_colno, mkint(colno));
    vsp--;
    (void)pos;
}
static void json_throw_decode(const char *msg, Value docv, long bytepos)
{
    long cpos, lineno, colno;
    Inst *in = new_inst(cls_json_error);
    Value iv = mkobj(in, T_INST), mv;
    VPUSH(iv);
    mv = mkcstr(msg);
    VPUSH(mv);
    json_pos_info(docv, bytepos, &cpos, &lineno, &colno);
    json_fill_error(in, mv, docv, bytepos, cpos, lineno, colno);
    vsp -= 2;
    throw_val(iv);
}
BI(jde_init)
{
    Value msg, doc, pos;
    long p, cpos, lineno, colno;
    if (x->n < 1 || A(0).t != T_INST) throw_new(cls_TypeError, "descriptor '__init__' requires an exception");
    need(x, 4, 4, "__init__");
    msg = A(1); doc = A(2); pos = A(3);
    if (msg.t != T_STR) msg = py_str(msg);
    VPUSH(msg);
    p = need_long(pos, "pos");
    json_pos_info(doc, p, &cpos, &lineno, &colno);
    if (doc.t != T_STR) { cpos = p; lineno = 1; colno = p + 1; }
    json_fill_error(AS_INST(A(0)), msg, doc, p, p, lineno, colno);
    vsp--;
    amap_set(&AS_INST(A(0))->a, JS_pos, pos);
    return NONE_V;
}

/* ---- encoder ------------------------------------------------------------ */
typedef struct JEnc {
    int skipkeys, ascii, circ, nan, sortk, hasind;
    Value defv;
    const char *ind, *isep, *ksep;
    long indn, isepn, ksepn;
    int nmk;
    Hdr *mk[JMAXDEPTH + 8];
} JEnc;

static const char *JHEX = "0123456789abcdef";
static void j_u4(Buf *b, long cp)
{
    char t[8];
    t[0] = '\\'; t[1] = 'u';
    t[2] = JHEX[(cp >> 12) & 15]; t[3] = JHEX[(cp >> 8) & 15];
    t[4] = JHEX[(cp >> 4) & 15]; t[5] = JHEX[cp & 15];
    buf_addn(b, t, 6);
}
static void j_str(Buf *b, const char *s, long n, int ascii)
{
    long i = 0, run = 0;
    buf_addc(b, '"');
    while (i < n) {
        unsigned c = (unsigned char)s[i];
        if (c >= 32 && c != '"' && c != '\\' && (c < 127 || !ascii)) { i++; continue; }
        if (i > run) buf_addn(b, s + run, i - run);
        if (c == '"') buf_add(b, "\\\"");
        else if (c == '\\') buf_add(b, "\\\\");
        else if (c == '\n') buf_add(b, "\\n");
        else if (c == '\r') buf_add(b, "\\r");
        else if (c == '\t') buf_add(b, "\\t");
        else if (c == 8) buf_add(b, "\\b");
        else if (c == 12) buf_add(b, "\\f");
        else if (c < 128) {
            j_u4(b, (long)c);
        } else {
            int l;
            long cp = re_u8dec((const unsigned char *)s, i, n, &l);
            if (l == 1) cp = c;
            if (cp >= 0x10000L) {
                cp -= 0x10000L;
                j_u4(b, 0xD800L | (cp >> 10));
                j_u4(b, 0xDC00L | (cp & 0x3FF));
            } else {
                j_u4(b, cp);
            }
            i += l;
            run = i;
            continue;
        }
        i++;
        run = i;
    }
    if (i > run) buf_addn(b, s + run, i - run);
    buf_addc(b, '"');
}
static void j_float(JEnc *e, double d, Buf *b)
{
    char t[64];
    if (d != d) {
        if (!e->nan) throw_new(cls_ValueError, "Out of range float values are not JSON compliant: nan");
        buf_add(b, "NaN");
    } else if (d > DBL_MAX) {
        if (!e->nan) throw_new(cls_ValueError, "Out of range float values are not JSON compliant: inf");
        buf_add(b, "Infinity");
    } else if (d < -DBL_MAX) {
        if (!e->nan) throw_new(cls_ValueError, "Out of range float values are not JSON compliant: -inf");
        buf_add(b, "-Infinity");
    } else {
        float_repr(d, t);
        buf_add(b, t);
    }
}
static void j_newline(JEnc *e, long level, Buf *b)
{
    long i;
    buf_addc(b, '\n');
    for (i = 0; i < level; i++) buf_addn(b, e->ind, e->indn);
}
static void jenc_value(JEnc *e, Value v, long level, Buf *b);
static void jenc_push(JEnc *e, Value v, long level)
{
    int i;
    if (level > JMAXDEPTH)
        throw_new(cls_RecursionError, "maximum recursion depth exceeded while encoding a JSON object");
    if (e->circ) {
        for (i = 0; i < e->nmk; i++)
            if (e->mk[i] == v.u.o) throw_new(cls_ValueError, "Circular reference detected");
        e->mk[e->nmk++] = v.u.o;
    }
}
static void jenc_list(JEnc *e, Value v, long level, Buf *b)
{
    List *l = AS_LIST(v);
    long i;
    if (l->n == 0) { buf_add(b, "[]"); return; }
    jenc_push(e, v, level);
    buf_addc(b, '[');
    if (e->hasind) j_newline(e, level + 1, b);
    for (i = 0; i < l->n; i++) {
        if (i) {
            buf_addn(b, e->isep, e->isepn);
            if (e->hasind) j_newline(e, level + 1, b);
        }
        jenc_value(e, l->a[i], level + 1, b);
    }
    if (e->hasind) j_newline(e, level, b);
    buf_addc(b, ']');
    if (e->circ) e->nmk--;
}
/* key text: returns 1 and writes the quoted key, or 0 for an unsupported key type */
static int jenc_key(JEnc *e, Value k, Buf *b)
{
    char t[64];
    if (k.t == T_INST && AS_INST(k)->pay.t == T_STR) k = AS_INST(k)->pay;
    switch (k.t) {
    case T_STR: j_str(b, AS_STR(k)->s, AS_STR(k)->len, e->ascii); return 1;
    case T_BOOL: j_str(b, k.u.i ? "true" : "false", k.u.i ? 4 : 5, 0); return 1;
    case T_NONE: j_str(b, "null", 4, 0); return 1;
    case T_INT: sprintf(t, "%ld", k.u.i); j_str(b, t, (long)strlen(t), 0); return 1;
    case T_BIG: {
        Buf tb;
        buf_init(&tb);
        int_str(k, &tb);
        j_str(b, tb.s, tb.n, 0);
        buf_free(&tb);
        return 1; }
    case T_FLOAT: {
        Buf tb;
        buf_init(&tb);
        j_float(e, k.u.d, &tb);
        j_str(b, tb.s, tb.n, 0);
        buf_free(&tb);
        return 1; }
    default: return 0;
    }
}
static void jenc_dict(JEnc *e, Value v, long level, Buf *b)
{
    Dict *d = AS_DICT(v);
    long i, cnt = 0, nk;
    Value keys = UNSET_V;
    Value *base = vsp;
    if (d->n == 0) { buf_add(b, "{}"); return; }
    jenc_push(e, v, level);
    buf_addc(b, '{');
    if (e->hasind) j_newline(e, level + 1, b);
    if (e->sortk) {
        List *kl = list_new(d->n);
        keys = mklist(kl);
        VPUSH(keys);
        for (i = 0; i < d->used; i++) if (d->e[i].k.t != T_UNSET) list_push(kl, d->e[i].k);
        sort_list(kl, NONE_V, 0);
        nk = kl->n;
    } else {
        nk = d->used;
    }
    for (i = 0; i < nk; i++) {
        Value k, val;
        Buf kb;
        int ok;
        if (e->sortk) {
            k = AS_LIST(keys)->a[i];
            if (!dict_get(d, k, &val)) continue;
        } else {
            if (d->e[i].k.t == T_UNSET) continue;
            k = d->e[i].k;
            val = d->e[i].v;
        }
        buf_init(&kb);
        ok = jenc_key(e, k, &kb);
        if (!ok) {
            buf_free(&kb);
            if (e->skipkeys) continue;
            throw_new(cls_TypeError, "keys must be str, int, float, bool or None, not %.40s", type_name(k));
        }
        if (cnt) {
            buf_addn(b, e->isep, e->isepn);
            if (e->hasind) j_newline(e, level + 1, b);
        }
        cnt++;
        buf_addn(b, kb.s, kb.n);
        buf_free(&kb);
        buf_addn(b, e->ksep, e->ksepn);
        jenc_value(e, val, level + 1, b);
    }
    vsp = base;
    if (e->hasind) j_newline(e, level, b);
    buf_addc(b, '}');
    if (e->circ) e->nmk--;
}
static void jenc_value(JEnc *e, Value v, long level, Buf *b)
{
    char t[40];
    if (v.t == T_INST) {
        Value pv = AS_INST(v)->pay;
        if (pv.t == T_STR || pv.t == T_LIST || pv.t == T_TUPLE || pv.t == T_DICT) v = pv;
    }
    switch (v.t) {
    case T_STR: j_str(b, AS_STR(v)->s, AS_STR(v)->len, e->ascii); return;
    case T_NONE: buf_add(b, "null"); return;
    case T_BOOL: buf_add(b, v.u.i ? "true" : "false"); return;
    case T_INT: sprintf(t, "%ld", v.u.i); buf_add(b, t); return;
    case T_BIG: int_str(v, b); return;
    case T_FLOAT: j_float(e, v.u.d, b); return;
    case T_LIST: case T_TUPLE: jenc_list(e, v, level, b); return;
    case T_DICT: jenc_dict(e, v, level, b); return;
    default: break;
    }
    if (e->defv.t == T_UNSET || e->defv.t == T_NONE)
        throw_new(cls_TypeError, "Object of type %.40s is not JSON serializable", type_name(v));
    {
        Value r;
        if (level > JMAXDEPTH)
            throw_new(cls_RecursionError, "maximum recursion depth exceeded while encoding a JSON object");
        if (e->circ && IS_HEAP(v.t)) {
            int i;
            for (i = 0; i < e->nmk; i++)
                if (e->mk[i] == v.u.o) throw_new(cls_ValueError, "Circular reference detected");
            e->mk[e->nmk++] = v.u.o;
        }
        VPUSH(v);
        r = call1(e->defv, v);
        vsp--;
        VPUSH(r);
        jenc_value(e, r, level, b);
        vsp--;
        if (e->circ && IS_HEAP(v.t)) e->nmk--;
    }
}

/* run the encoder, freeing the buffer when an exception passes through */
static Value jenc_run(JEnc *e, Value obj)
{
    Handler h;
    Value *save = vsp;
    int depth = call_depth;
    Frame *fr = cur_frame;
    Buf b;
    buf_init(&b);
    h.prev = cur_handler;
    h.sp = save;
    h.depth = depth;
    h.frame = fr;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        jenc_value(e, obj, 0, &b);
        cur_handler = h.prev;
        return buf_value(&b);
    }
    cur_handler = h.prev;
    vsp = save;
    call_depth = depth;
    cur_frame = fr;
    buf_free(&b);
    throw_val(cur_exc);
    return NONE_V;
}

static Value jarg(Args *x, int i, const char *nm)
{
    if (i < x->n) return x->a[i];
    return kwget(x, nm);
}
static int jtruth(Value v, int dflt)
{
    if (v.t == T_UNSET) return dflt;
    return py_truthy(v);
}
/* fill an encoder configuration; temporaries are pushed on the value stack (caller resets vsp) */
static void jenc_config(JEnc *e, Value skipkeys, Value ascii, Value circ, Value nan, Value sortk,
                        Value indent, Value seps, Value defv)
{
    Value isepv, ksepv;
    e->skipkeys = jtruth(skipkeys, 0);
    e->ascii = jtruth(ascii, 1);
    e->circ = jtruth(circ, 1);
    e->nan = jtruth(nan, 1);
    e->sortk = jtruth(sortk, 0);
    e->defv = defv;
    e->nmk = 0;
    e->hasind = 0;
    e->ind = ""; e->indn = 0;
    if (indent.t != T_UNSET && indent.t != T_NONE) {
        Value iv = indent;
        if (iv.t == T_INST && AS_INST(iv)->pay.t == T_STR) iv = AS_INST(iv)->pay;
        if (iv.t == T_INT || iv.t == T_BOOL) {
            long n = iv.u.i < 0 ? 0 : iv.u.i;
            Buf sb;
            buf_init(&sb);
            while (n-- > 0) buf_addc(&sb, ' ');
            iv = buf_value(&sb);
        } else if (iv.t != T_STR) {
            throw_new(cls_TypeError, "can't multiply sequence by non-int of type '%.40s'", type_name(iv));
        }
        VPUSH(iv);
        e->hasind = 1;
        e->ind = AS_STR(iv)->s; e->indn = AS_STR(iv)->len;
    }
    if (seps.t != T_UNSET && seps.t != T_NONE) {
        if ((seps.t != T_TUPLE && seps.t != T_LIST) || AS_LIST(seps)->n != 2)
            throw_new(cls_TypeError, "separators must be a tuple of two strings");
        isepv = AS_LIST(seps)->a[0]; ksepv = AS_LIST(seps)->a[1];
        if (isepv.t != T_STR || ksepv.t != T_STR)
            throw_new(cls_TypeError, "separators must be strings");
    } else if (e->hasind) {
        isepv = mkcstr(","); ksepv = mkcstr(": ");
        VPUSH(isepv); VPUSH(ksepv);
    } else {
        isepv = mkcstr(", "); ksepv = mkcstr(": ");
        VPUSH(isepv); VPUSH(ksepv);
    }
    e->isep = AS_STR(isepv)->s; e->isepn = AS_STR(isepv)->len;
    e->ksep = AS_STR(ksepv)->s; e->ksepn = AS_STR(ksepv)->len;
}

static const char *jenc_names[] = { "skipkeys", "ensure_ascii", "check_circular", "allow_nan", "cls",
                                    "indent", "separators", "default", "sort_keys", NULL };
/* positions: 0 skipkeys 1 ensure_ascii 2 check_circular 3 allow_nan 4 cls 5 indent 6 separators
   7 default 8 sort_keys (obj is argument 0) */
static void json_check_kw(Args *x, const char **names, int nnames, const char *who)
{
    int i, k;
    for (i = 0; i < x->nk; i++) {
        int ok = 0;
        for (k = 0; k < nnames; k++) if (strcmp(AS_STR(x->kw[2 * i])->s, names[k]) == 0) ok = 1;
        if (!ok)
            throw_new(cls_TypeError, "%s got an unexpected keyword argument '%.60s'", who,
                      AS_STR(x->kw[2 * i])->s);
    }
}
static Value json_dumps_core(Args *x)
{
    Value cls, obj, r;
    JEnc e;
    Value *base = vsp;
    if (x->n < 1) throw_new(cls_TypeError, "dumps() missing 1 required positional argument: 'obj'");
    if (x->n > 10) throw_new(cls_TypeError, "dumps() takes at most 10 positional arguments");
    obj = A(0);
    cls = jarg(x, 5, "cls");
    if (cls.t != T_UNSET && cls.t != T_NONE && !(cls.t == T_CLASS && AS_CLASS(cls) == cls_json_encoder)) {
        /* custom encoder class: cls(**options).encode(obj) */
        Value kw[2 * 16], inst, enc;
        int nk = 0, i;
        for (i = 1; i < 10; i++) {
            Value v;
            if (i == 5) continue;
            v = jarg(x, i, jenc_names[i - 1]);
            if (v.t == T_UNSET) continue;
            kw[2 * nk] = mkcstr(jenc_names[i - 1]);
            kw[2 * nk + 1] = v;
            VPUSH(kw[2 * nk]);
            nk++;
        }
        for (i = 0; i < x->nk && nk < 16; i++) {
            int k, known = 0;
            for (k = 0; k < 9; k++) if (strcmp(AS_STR(x->kw[2 * i])->s, jenc_names[k]) == 0) known = 1;
            if (known) continue;
            kw[2 * nk] = x->kw[2 * i];
            kw[2 * nk + 1] = x->kw[2 * i + 1];
            nk++;
        }
        inst = call_value(cls, 0, NULL, nk, kw);
        VPUSH(inst);
        enc = get_attr(inst, JS_encode);
        VPUSH(enc);
        r = call1(enc, obj);
        vsp = base;
        return r;
    }
    {
        static const char *okn[] = { "skipkeys", "ensure_ascii", "check_circular", "allow_nan",
                                     "cls", "indent", "separators", "default", "sort_keys" };
        json_check_kw(x, okn, 9, "JSONEncoder.__init__()");
    }
    jenc_config(&e, jarg(x, 1, "skipkeys"), jarg(x, 2, "ensure_ascii"), jarg(x, 3, "check_circular"),
                jarg(x, 4, "allow_nan"), jarg(x, 9, "sort_keys"), jarg(x, 6, "indent"),
                jarg(x, 7, "separators"), jarg(x, 8, "default"));
    r = jenc_run(&e, obj);
    vsp = base;
    return r;
}
BI(json_dumps)
{
    return json_dumps_core(x);
}
BI(json_dump)
{
    Args y;
    Value fp, s, wr;
    if (x->n < 2) throw_new(cls_TypeError, "dump() missing required positional arguments: 'obj' and 'fp'");
    fp = A(1);
    y.n = 1; y.a = x->a; y.nk = x->nk; y.kw = x->kw;
    s = json_dumps_core(&y);
    VPUSH(s);
    wr = get_attr(fp, intern_c("write"));
    VPUSH(wr);
    call1(wr, s);
    vsp -= 2;
    return NONE_V;
}

/* ---- decoder ------------------------------------------------------------ */
typedef struct JDec {
    const unsigned char *s;
    long n;
    Value docv;
    int strict, depth;
    Value ohook, phook, pfloat, pint, pconst;
} JDec;

static int jd_isws(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
static long jd_ws(JDec *D, long i)
{
    while (i < D->n && jd_isws(D->s[i])) i++;
    return i;
}
static int jd_hasv(Value v)
{
    return v.t != T_UNSET && v.t != T_NONE;
}
static int j_encode_cp(long cp, unsigned char *o)
{
    if (cp < 0x80) { o[0] = (unsigned char)cp; return 1; }
    if (cp < 0x800) {
        o[0] = (unsigned char)(0xC0 | (cp >> 6)); o[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000L) {
        o[0] = (unsigned char)(0xE0 | (cp >> 12));
        o[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (unsigned char)(0xF0 | (cp >> 18));
    o[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}
static int jd_hex4(const unsigned char *s, long *out)
{
    long v = 0;
    int k;
    for (k = 0; k < 4; k++) {
        int c = s[k], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return 0;
        v = v * 16 + d;
    }
    *out = v;
    return 1;
}
/* i is just after the opening quote; returns the string, *end just after the closing quote */
static Value jd_string(JDec *D, long i, long *end)
{
    long begin = i - 1, j, n = D->n;
    const unsigned char *s = D->s;
    unsigned char *tmp = NULL;
    long tl = 0, run;
    Value r;
    for (j = i; j < n; j++) {
        int c = s[j];
        if (c == '"' || c == '\\') break;
        if (c <= 0x1f && D->strict) json_throw_decode("Invalid control character at", D->docv, j);
    }
    if (j >= n) json_throw_decode("Unterminated string starting at", D->docv, begin);
    if (s[j] == '"') {
        *end = j + 1;
        return mkstr((const char *)s + i, j - i);
    }
    tmp = (unsigned char *)xmalloc((size_t)(n - i) + 8);
    run = i;
    for (;;) {
        int c;
        long nx;
        /* copy the plain run [run, j) */
        memcpy(tmp + tl, s + run, (size_t)(j - run));
        tl += j - run;
        if (j >= n) { free(tmp); json_throw_decode("Unterminated string starting at", D->docv, begin); }
        if (s[j] == '"') { j++; break; }
        /* backslash */
        nx = j + 1;
        if (nx >= n) { free(tmp); json_throw_decode("Unterminated string starting at", D->docv, begin); }
        c = s[nx];
        if (c != 'u') {
            switch (c) {
            case '"': tmp[tl++] = '"'; break;
            case '\\': tmp[tl++] = '\\'; break;
            case '/': tmp[tl++] = '/'; break;
            case 'b': tmp[tl++] = 8; break;
            case 'f': tmp[tl++] = 12; break;
            case 'n': tmp[tl++] = 10; break;
            case 'r': tmp[tl++] = 13; break;
            case 't': tmp[tl++] = 9; break;
            default: free(tmp); json_throw_decode("Invalid \\escape", D->docv, nx - 1); break;
            }
            j = nx + 1;
        } else {
            long cp, endx = nx + 1 + 4;
            if (endx > n) { free(tmp); json_throw_decode("Invalid \\uXXXX escape", D->docv, nx); }
            if (!jd_hex4(s + nx + 1, &cp)) {
                free(tmp);
                json_throw_decode("Invalid \\uXXXX escape", D->docv, nx);
            }
            j = endx;
            if (cp >= 0xD800L && cp < 0xDC00L && endx + 6 < n && s[endx] == '\\' && s[endx + 1] == 'u') {
                long c2;
                if (!jd_hex4(s + endx + 2, &c2)) {
                    free(tmp);
                    json_throw_decode("Invalid \\uXXXX escape", D->docv, endx + 1);
                }
                if (c2 >= 0xDC00L && c2 < 0xE000L) {
                    cp = 0x10000L + (((cp - 0xD800L) << 10) | (c2 - 0xDC00L));
                    j = endx + 6;
                }
            }
            tl += j_encode_cp(cp, tmp + tl);
        }
        /* next plain run */
        run = j;
        while (j < n) {
            int cc = s[j];
            if (cc == '"' || cc == '\\') break;
            if (cc <= 0x1f && D->strict) { free(tmp); json_throw_decode("Invalid control character at", D->docv, j); }
            j++;
        }
    }
    *end = j;
    r = mkstr((const char *)tmp, tl);
    free(tmp);
    return r;
}
static Value jd_value(JDec *D, long i, long *end);
static Value jd_call_hook(Value hook, Value arg)
{
    Value r;
    VPUSH(arg);
    r = call1(hook, arg);
    vsp--;
    return r;
}
static Value jd_array(JDec *D, long i, long *end)
{
    List *l = list_new(8);
    Value lv = mklist(l), v;
    long n = D->n;
    const unsigned char *s = D->s;
    VPUSH(lv);
    if (++D->depth > JMAXDEPTH)
        throw_new(cls_RecursionError, "maximum recursion depth exceeded while decoding a JSON array from a unicode string");
    i = jd_ws(D, i);
    if (i < n && s[i] == ']') { *end = i + 1; D->depth--; vsp--; return lv; }
    for (;;) {
        v = jd_value(D, i, &i);
        list_push(l, v);
        i = jd_ws(D, i);
        if (i < n && s[i] == ']') { i++; break; }
        if (i >= n || s[i] != ',') json_throw_decode("Expecting ',' delimiter", D->docv, i);
        i = jd_ws(D, i + 1);
    }
    D->depth--;
    vsp--;
    *end = i;
    return lv;
}
static Value jd_object(JDec *D, long i, long *end)
{
    Dict *d = NULL;
    List *pairs = NULL;
    Value ov, k, v;
    long n = D->n;
    const unsigned char *s = D->s;
    int usepairs = jd_hasv(D->phook);
    if (++D->depth > JMAXDEPTH)
        throw_new(cls_RecursionError, "maximum recursion depth exceeded while decoding a JSON object from a unicode string");
    if (usepairs) { pairs = list_new(8); ov = mklist(pairs); }
    else { d = dict_new(); ov = mkobj(d, T_DICT); }
    VPUSH(ov);
    i = jd_ws(D, i);
    if (i < n && s[i] == '}') {
        i++;
    } else {
        if (i >= n || s[i] != '"')
            json_throw_decode("Expecting property name enclosed in double quotes", D->docv, i);
        i++;
        for (;;) {
            k = jd_string(D, i, &i);
            VPUSH(k);
            i = jd_ws(D, i);
            if (i >= n || s[i] != ':') json_throw_decode("Expecting ':' delimiter", D->docv, i);
            i = jd_ws(D, i + 1);
            v = jd_value(D, i, &i);
            if (usepairs) list_push(pairs, mktuple2(k, v));
            else dict_set(d, k, v);
            vsp--;
            i = jd_ws(D, i);
            if (i < n && s[i] == '}') { i++; break; }
            if (i >= n || s[i] != ',') json_throw_decode("Expecting ',' delimiter", D->docv, i);
            i = jd_ws(D, i + 1);
            if (i >= n || s[i] != '"')
                json_throw_decode("Expecting property name enclosed in double quotes", D->docv, i);
            i++;
        }
    }
    D->depth--;
    *end = i;
    if (usepairs) ov = jd_call_hook(D->phook, ov);
    else if (jd_hasv(D->ohook)) ov = jd_call_hook(D->ohook, ov);
    vsp--;
    return ov;
}
static int jd_isdig(int c)
{
    return c >= '0' && c <= '9';
}
static Value jd_number(JDec *D, long i, long *end, int *ok)
{
    long j = i, n = D->n, k;
    const unsigned char *s = D->s;
    int isf = 0;
    Value r;
    *ok = 0;
    if (j < n && s[j] == '-') j++;
    if (j >= n) return NONE_V;
    if (s[j] >= '1' && s[j] <= '9') { while (j < n && jd_isdig(s[j])) j++; }
    else if (s[j] == '0') j++;
    else return NONE_V;
    if (j + 1 < n && s[j] == '.' && jd_isdig(s[j + 1])) {
        j++;
        while (j < n && jd_isdig(s[j])) j++;
        isf = 1;
    }
    if (j < n && (s[j] == 'e' || s[j] == 'E')) {
        k = j + 1;
        if (k < n && (s[k] == '-' || s[k] == '+')) k++;
        if (k < n && jd_isdig(s[k])) {
            while (k < n && jd_isdig(s[k])) k++;
            j = k;
            isf = 1;
        }
    }
    *ok = 1;
    *end = j;
    if (isf) {
        if (jd_hasv(D->pfloat)) {
            Value t = mkstr((const char *)s + i, j - i);
            return jd_call_hook(D->pfloat, t);
        } else {
            char tmp[400];
            long ln = j - i;
            if (ln > 390) {
                char *big = (char *)xmalloc((size_t)ln + 1);
                double dv;
                memcpy(big, s + i, (size_t)ln);
                big[ln] = 0;
                dv = strtod(big, NULL);
                free(big);
                return mkfloat(dv);
            }
            memcpy(tmp, s + i, (size_t)ln);
            tmp[ln] = 0;
            return mkfloat(strtod(tmp, NULL));
        }
    }
    if (jd_hasv(D->pint)) {
        Value t = mkstr((const char *)s + i, j - i);
        return jd_call_hook(D->pint, t);
    }
    if (!parse_int((const char *)s + i, j - i, 10, &r)) return NONE_V;
    return r;
}
static Value jd_constant(JDec *D, const char *name, double val)
{
    if (jd_hasv(D->pconst)) return jd_call_hook(D->pconst, mkcstr(name));
    return mkfloat(val);
}
static Value jd_value(JDec *D, long i, long *end)
{
    const unsigned char *s = D->s;
    long n = D->n;
    int c;
    if (i >= n) json_throw_decode("Expecting value", D->docv, i);
    c = s[i];
    if (c == '"') return jd_string(D, i + 1, end);
    if (c == '{') return jd_object(D, i + 1, end);
    if (c == '[') return jd_array(D, i + 1, end);
    if (c == 'n' && i + 4 <= n && memcmp(s + i, "null", 4) == 0) { *end = i + 4; return NONE_V; }
    if (c == 't' && i + 4 <= n && memcmp(s + i, "true", 4) == 0) { *end = i + 4; return TRUE_V; }
    if (c == 'f' && i + 5 <= n && memcmp(s + i, "false", 5) == 0) { *end = i + 5; return FALSE_V; }
    {
        int ok;
        Value r = jd_number(D, i, end, &ok);
        if (ok) return r;
    }
    if (c == 'N' && i + 3 <= n && memcmp(s + i, "NaN", 3) == 0) {
        *end = i + 3;
        return jd_constant(D, "NaN", HUGE_VAL - HUGE_VAL);
    }
    if (c == 'I' && i + 8 <= n && memcmp(s + i, "Infinity", 8) == 0) {
        *end = i + 8;
        return jd_constant(D, "Infinity", HUGE_VAL);
    }
    if (c == '-' && i + 9 <= n && memcmp(s + i, "-Infinity", 9) == 0) {
        *end = i + 9;
        return jd_constant(D, "-Infinity", -HUGE_VAL);
    }
    json_throw_decode("Expecting value", D->docv, i);
    return NONE_V;
}
static Value json_doc_arg(Value s)
{
    if (s.t == T_INST && AS_INST(s)->pay.t == T_STR) s = AS_INST(s)->pay;
    if (s.t == T_BYTES) return mkstr(AS_STR(s)->s, AS_STR(s)->len);
    if (s.t != T_STR)
        throw_new(cls_TypeError, "the JSON object must be str, bytes or bytearray, not %.40s", type_name(s));
    return s;
}
static void jd_config(JDec *D, Value ohook, Value phook, Value pfloat, Value pint, Value pconst,
                      Value strict)
{
    D->ohook = ohook; D->phook = phook; D->pconst = pconst;
    D->pfloat = pfloat; D->pint = pint;
    if (pfloat.t == T_CLASS && AS_CLASS(pfloat) == cls_float) D->pfloat = NONE_V;
    if (pint.t == T_CLASS && AS_CLASS(pint) == cls_int) D->pint = NONE_V;
    D->strict = jtruth(strict, 1);
    D->depth = 0;
}
static Value jd_decode(JDec *D, int raw, long idx, long *rawend)
{
    long i, end;
    Value r;
    i = raw ? idx : jd_ws(D, idx);
    r = jd_value(D, i, &end);
    if (raw) { *rawend = end; return r; }
    VPUSH(r);
    end = jd_ws(D, end);
    if (end != D->n) json_throw_decode("Extra data", D->docv, end);
    vsp--;
    return r;
}
static const char *jdec_names[] = { "cls", "object_hook", "parse_float", "parse_int", "parse_constant",
                                    "object_pairs_hook", "strict", NULL };
BI(json_loads)
{
    Value s, cls, doc;
    JDec D;
    Value *base = vsp;
    Value r;
    if (x->n < 1) throw_new(cls_TypeError, "loads() missing 1 required positional argument: 's'");
    if (x->n > 1) throw_new(cls_TypeError, "loads() takes 1 positional argument but %d were given", x->n);
    s = A(0);
    cls = kwget(x, "cls");
    doc = json_doc_arg(s);
    VPUSH(doc);
    if (jd_hasv(cls) && !(cls.t == T_CLASS && AS_CLASS(cls) == cls_json_decoder)) {
        Value kw[2 * 12], inst, dec;
        int nk = 0, i;
        for (i = 0; i < x->nk && nk < 12; i++) {
            if (strcmp(AS_STR(x->kw[2 * i])->s, "cls") == 0) continue;
            kw[2 * nk] = x->kw[2 * i];
            kw[2 * nk + 1] = x->kw[2 * i + 1];
            nk++;
        }
        inst = call_value(cls, 0, NULL, nk, kw);
        VPUSH(inst);
        dec = get_attr(inst, JS_decode);
        VPUSH(dec);
        r = call1(dec, doc);
        vsp = base;
        return r;
    }
    json_check_kw(x, jdec_names, 7, "JSONDecoder.__init__()");
    D.s = (const unsigned char *)AS_STR(doc)->s;
    D.n = AS_STR(doc)->len;
    D.docv = doc;
    jd_config(&D, kwget(x, "object_hook"), kwget(x, "object_pairs_hook"), kwget(x, "parse_float"),
              kwget(x, "parse_int"), kwget(x, "parse_constant"), kwget(x, "strict"));
    r = jd_decode(&D, 0, 0, NULL);
    vsp = base;
    return r;
}
BI(json_load)
{
    Args y;
    Value rd, s, r;
    Value *base = vsp;
    if (x->n < 1) throw_new(cls_TypeError, "load() missing 1 required positional argument: 'fp'");
    rd = get_attr(A(0), intern_c("read"));
    VPUSH(rd);
    s = call0(rd);
    VPUSH(s);
    {
        Value av[1];
        av[0] = s;
        y.n = 1; y.a = av; y.nk = x->nk; y.kw = x->kw;
        r = json_loads(&y);
    }
    vsp = base;
    return r;
}

/* ---- JSONEncoder / JSONDecoder classes --------------------------------- */
BI(je_init)
{
    Inst *in;
    static const char *nm[] = { "skipkeys", "ensure_ascii", "check_circular", "allow_nan",
                                "sort_keys", "indent", "separators", "default", NULL };
    Value skipkeys, ascii, circ, nan, sortk, indent, seps, defv;
    Value isep, ksep;
    if (x->n < 1 || A(0).t != T_INST) throw_new(cls_TypeError, "descriptor '__init__' requires a JSONEncoder");
    if (x->n > 1) throw_new(cls_TypeError, "JSONEncoder.__init__() takes 1 positional argument but %d were given", x->n);
    json_check_kw(x, nm, 8, "JSONEncoder.__init__()");
    in = AS_INST(A(0));
    skipkeys = kwget(x, "skipkeys"); ascii = kwget(x, "ensure_ascii");
    circ = kwget(x, "check_circular"); nan = kwget(x, "allow_nan"); sortk = kwget(x, "sort_keys");
    indent = kwget(x, "indent"); seps = kwget(x, "separators"); defv = kwget(x, "default");
    amap_set(&in->a, JS_skipkeys, mkbool(jtruth(skipkeys, 0)));
    amap_set(&in->a, JS_ascii, mkbool(jtruth(ascii, 1)));
    amap_set(&in->a, JS_circ, mkbool(jtruth(circ, 1)));
    amap_set(&in->a, JS_nan, mkbool(jtruth(nan, 1)));
    amap_set(&in->a, JS_sortk, mkbool(jtruth(sortk, 0)));
    amap_set(&in->a, JS_indent, indent.t == T_UNSET ? NONE_V : indent);
    if (jd_hasv(seps)) {
        if ((seps.t != T_TUPLE && seps.t != T_LIST) || AS_LIST(seps)->n != 2)
            throw_new(cls_TypeError, "separators must be a tuple of two strings");
        isep = AS_LIST(seps)->a[0]; ksep = AS_LIST(seps)->a[1];
    } else if (jd_hasv(indent)) {
        isep = mkcstr(","); ksep = mkcstr(": ");
    } else {
        isep = mkcstr(", "); ksep = mkcstr(": ");
    }
    amap_set(&in->a, JS_isep, isep);
    amap_set(&in->a, JS_ksep, ksep);
    if (jd_hasv(defv)) amap_set(&in->a, JS_default, defv);
    return NONE_V;
}
BI(je_default)
{
    need(x, 2, 2, "default");
    throw_new(cls_TypeError, "Object of type %.40s is not JSON serializable", type_name(A(1)));
    return NONE_V;
}
BI(je_encode)
{
    JEnc e;
    Value self, defv, seps, r, ind;
    Value *base = vsp;
    need(x, 2, 2, "encode");
    self = A(0);
    if (self.t != T_INST) throw_new(cls_TypeError, "descriptor 'encode' requires a JSONEncoder");
    defv = get_attr(self, JS_default);
    VPUSH(defv);
    seps = mktuple2(re_attr(self, JS_isep), re_attr(self, JS_ksep));
    VPUSH(seps);
    ind = re_attr(self, JS_indent);
    jenc_config(&e, re_attr(self, JS_skipkeys), re_attr(self, JS_ascii), re_attr(self, JS_circ),
                re_attr(self, JS_nan), re_attr(self, JS_sortk), ind, seps, defv);
    r = jenc_run(&e, A(1));
    vsp = base;
    return r;
}
BI(je_iterencode)
{
    Value r, lst;
    List *l;
    if (x->n < 2) throw_new(cls_TypeError, "iterencode() missing 1 required positional argument: 'o'");
    {
        Value enc = get_attr(A(0), JS_encode);
        VPUSH(enc);
        r = call1(enc, A(1));
        vsp--;
    }
    VPUSH(r);
    l = list_new(1);
    lst = mklist(l);
    list_push(l, r);
    vsp--;
    return get_iter(lst);
}
BI(jd_init)
{
    Inst *in;
    static const char *nm[] = { "object_hook", "parse_float", "parse_int", "parse_constant", "strict",
                                "object_pairs_hook", NULL };
    Value v;
    if (x->n < 1 || A(0).t != T_INST) throw_new(cls_TypeError, "descriptor '__init__' requires a JSONDecoder");
    if (x->n > 1) throw_new(cls_TypeError, "JSONDecoder.__init__() takes 1 positional argument but %d were given", x->n);
    json_check_kw(x, nm, 6, "JSONDecoder.__init__()");
    in = AS_INST(A(0));
    v = kwget(x, "object_hook"); amap_set(&in->a, JS_ohook, v.t == T_UNSET ? NONE_V : v);
    v = kwget(x, "object_pairs_hook"); amap_set(&in->a, JS_phook, v.t == T_UNSET ? NONE_V : v);
    v = kwget(x, "parse_float");
    amap_set(&in->a, JS_pfloat, jd_hasv(v) ? v : mkobj(cls_float, T_CLASS));
    v = kwget(x, "parse_int");
    amap_set(&in->a, JS_pint, jd_hasv(v) ? v : mkobj(cls_int, T_CLASS));
    v = kwget(x, "parse_constant"); amap_set(&in->a, JS_pconst, v.t == T_UNSET ? NONE_V : v);
    v = kwget(x, "strict"); amap_set(&in->a, JS_strict, mkbool(jtruth(v, 1)));
    return NONE_V;
}
static void jd_from_self(JDec *D, Value self, Value doc)
{
    D->s = (const unsigned char *)AS_STR(doc)->s;
    D->n = AS_STR(doc)->len;
    D->docv = doc;
    jd_config(D, re_attr(self, JS_ohook), re_attr(self, JS_phook), re_attr(self, JS_pfloat),
              re_attr(self, JS_pint), re_attr(self, JS_pconst), re_attr(self, JS_strict));
}
BI(jd_decode_m)
{
    JDec D;
    Value doc, r;
    Value *base = vsp;
    need(x, 2, 2, "decode");
    doc = json_doc_arg(A(1));
    VPUSH(doc);
    jd_from_self(&D, A(0), doc);
    r = jd_decode(&D, 0, 0, NULL);
    vsp = base;
    return r;
}
BI(jd_raw_decode)
{
    JDec D;
    Value doc, r, idxv;
    long idx = 0, end = 0;
    Value *base = vsp;
    need(x, 2, 3, "raw_decode");
    idxv = x->n > 2 ? A(2) : kwget(x, "idx");
    doc = json_doc_arg(A(1));
    VPUSH(doc);
    if (idxv.t != T_UNSET) idx = need_long(idxv, "idx");
    jd_from_self(&D, A(0), doc);
    if (idx < 0 || idx > D.n) json_throw_decode("Expecting value", doc, idx < 0 ? 0 : idx);
    r = jd_decode(&D, 1, idx, &end);
    VPUSH(r);
    r = mktuple2(r, mkint(end));
    vsp = base;
    return r;
}

static void json_init(void)
{
    Module *m = new_module("json"), *me, *md;
    Class **b;
    JS_msg = intern_c("msg"); JS_doc = intern_c("doc"); JS_pos = intern_c("pos");
    JS_lineno = intern_c("lineno"); JS_colno = intern_c("colno");
    JS_skipkeys = intern_c("skipkeys"); JS_ascii = intern_c("ensure_ascii");
    JS_circ = intern_c("check_circular"); JS_nan = intern_c("allow_nan");
    JS_sortk = intern_c("sort_keys"); JS_indent = intern_c("indent");
    JS_isep = intern_c("item_separator"); JS_ksep = intern_c("key_separator");
    JS_default = intern_c("default"); JS_ohook = intern_c("object_hook");
    JS_phook = intern_c("object_pairs_hook"); JS_pfloat = intern_c("parse_float");
    JS_pint = intern_c("parse_int"); JS_pconst = intern_c("parse_constant");
    JS_strict = intern_c("strict"); JS_encode = intern_c("encode"); JS_decode = intern_c("decode");

    b = (Class **)xmalloc(sizeof(Class *));
    b[0] = cls_ValueError;
    cls_json_error = make_class(intern_c("JSONDecodeError"), b, 1, intern_c("json.decoder"));
    def_bi(cls_json_error, "__init__", jde_init);
    b = (Class **)xmalloc(sizeof(Class *));
    b[0] = cls_object;
    cls_json_encoder = make_class(intern_c("JSONEncoder"), b, 1, intern_c("json.encoder"));
    def_bi(cls_json_encoder, "__init__", je_init); def_bi(cls_json_encoder, "default", je_default);
    def_bi(cls_json_encoder, "encode", je_encode); def_bi(cls_json_encoder, "iterencode", je_iterencode);
    b = (Class **)xmalloc(sizeof(Class *));
    b[0] = cls_object;
    cls_json_decoder = make_class(intern_c("JSONDecoder"), b, 1, intern_c("json.decoder"));
    def_bi(cls_json_decoder, "__init__", jd_init); def_bi(cls_json_decoder, "decode", jd_decode_m);
    def_bi(cls_json_decoder, "raw_decode", jd_raw_decode);

    mod_def(m, "dumps", json_dumps); mod_def(m, "dump", json_dump);
    mod_def(m, "loads", json_loads); mod_def(m, "load", json_load);
    mod_val(m, "JSONDecodeError", mkobj(cls_json_error, T_CLASS));
    mod_val(m, "JSONEncoder", mkobj(cls_json_encoder, T_CLASS));
    mod_val(m, "JSONDecoder", mkobj(cls_json_decoder, T_CLASS));
    mod_val(m, "__version__", mkcstr("2.0.9"));
    me = new_module("json.encoder");
    mod_val(me, "JSONEncoder", mkobj(cls_json_encoder, T_CLASS));
    md = new_module("json.decoder");
    mod_val(md, "JSONDecoder", mkobj(cls_json_decoder, T_CLASS));
    mod_val(md, "JSONDecodeError", mkobj(cls_json_error, T_CLASS));
    mod_val(m, "encoder", mkobj(me, T_MODULE));
    mod_val(m, "decoder", mkobj(md, T_MODULE));
}
