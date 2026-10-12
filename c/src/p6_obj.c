/* =========================================================================
   p6_obj.c   object protocols: classes, attributes, repr/str, equality,
              hashing, comparison, arithmetic, indexing, iteration
   ========================================================================= */
static Class *class_of(Value v)
{
    switch (v.t) {
    case T_NONE: return cls_none;
    case T_BOOL: return cls_bool;
    case T_INT: case T_BIG: return cls_int;
    case T_FLOAT: return cls_float;
    case T_STR: return cls_str;
    case T_LIST: return cls_list;
    case T_TUPLE: return cls_tuple;
    case T_DICT: return cls_dict;
    case T_SET: return ((Dict *)v.u.o)->frozen ? cls_frozenset : cls_set;
    case T_RANGE: return cls_range;
    case T_FUNC: return cls_func;
    case T_BUILTIN: return cls_builtin;
    case T_CLASS: return AS_CLASS(v)->meta ? AS_CLASS(v)->meta : cls_type;
    case T_INST: return AS_INST(v)->cls;
    case T_BOUND: return cls_method;
    case T_GEN: return cls_gen;
    case T_ITER: return cls_iter;
    case T_MODULE: return cls_module;
    case T_PROP: return cls_prop;
    case T_SMETH: return cls_smeth;
    case T_CMETH: return cls_cmeth;
    case T_SUPER: return cls_super;
    case T_FILE: return ((File *)v.u.o)->bin == 0 ? cls_file :
        (((File *)v.u.o)->bin == 1 ? cls_fileb : cls_filew);
    case T_CELL: return cls_cell;
    case T_ND: return cls_ndarray;
    case T_SLICE: return cls_slice;
    case T_DVIEW: return cls_dview[((DView *)v.u.o)->what];
    case T_BYTES: return cls_bytes;
    case T_BARR: return cls_bytearray;
    case T_COMPLEX: return cls_complex;
    default: return cls_object;
    }
}
static Str *modname_of_gp(Str *gp)
{
    if (!gp) return NULL;
    return intern(gp->s, gp->len > 0 ? gp->len - 1 : 0);
}
static Value *class_find(Class *c, Str *nm)
{
    if (c->mro) {
        int i;
        for (i = 0; i < c->nmro; i++) {
            Value *p = amap_get(&c->mro[i]->ns, nm);
            if (p) return p;
        }
        return NULL;
    }
    while (c) {
        Value *p = amap_get(&c->ns, nm);
        if (p) return p;
        c = c->base;
    }
    return NULL;
}
static int class_isa(Class *c, Class *base)
{
    if (c->mro) {
        int i;
        for (i = 0; i < c->nmro; i++)
            if (c->mro[i] == base) return 1;
        return 0;
    }
    while (c) {
        if (c == base) return 1;
        c = c->base;
    }
    return 0;
}
/* find nm in the MRO of `type` strictly after class `after` (for super()) */
static Value *class_find_after(Class *type, Class *after, Str *nm)
{
    Class *k;
    int i;
    if (type->mro) {
        for (i = 0; i < type->nmro; i++) {
            if (type->mro[i] == after) {
                for (i++; i < type->nmro; i++) {
                    Value *p = amap_get(&type->mro[i]->ns, nm);
                    if (p) return p;
                }
                return NULL;
            }
        }
        return NULL;
    }
    for (k = type; k && k != after; k = k->base) { }
    if (!k) return NULL;
    for (k = k->base; k; k = k->base) {
        Value *p = amap_get(&k->ns, nm);
        if (p) return p;
    }
    return NULL;
}
static Value unpay(Value v)
{
    return (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET) ? AS_INST(v)->pay : v;
}
/* bytes, bytearray or an instance of a subclass: the data pointer and length */
static int bytes_get(Value v, const char **p, long *n)
{
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET) v = AS_INST(v)->pay;
    if (v.t == T_BYTES) { *p = AS_STR(v)->s; *n = AS_STR(v)->len; return 1; }
    if (v.t == T_BARR) { *p = AS_BARR(v)->s; *n = AS_BARR(v)->n; return 1; }
    return 0;
}
static const char *type_name(Value v)
{
    return class_of(v)->name->s;
}
static Inst *new_inst(Class *c)
{
    Inst *in = (Inst *)gc_alloc(sizeof(Inst), T_INST);
    in->cls = c;
    in->a.k = NULL; in->a.v = NULL; in->a.n = in->a.cap = 0;
    in->pay = UNSET_V;
    in->dct = NULL;
    return in;
}
static Value new_bound(Value self, Value fn)
{
    Bound *b = (Bound *)gc_alloc(sizeof(Bound), T_BOUND);
    b->self = self;
    b->fn = fn;
    return mkobj(b, T_BOUND);
}
static Value new_exc(Class *c, const char *msg)
{
    Inst *in = new_inst(c);
    Value iv = mkobj(in, T_INST);
    List *t;
    VPUSH(iv);
    t = tuple_new(1);
    t->a[0] = mkcstr(msg);
    amap_set(&in->a, S_args, mkobj(t, T_TUPLE));
    vsp--;
    return iv;
}
static Value new_exc_args(Class *c, Value *args, long n)
{
    Inst *in = new_inst(c);
    Value iv = mkobj(in, T_INST);
    VPUSH(iv);
    amap_set(&in->a, S_args, mktuple_from(args, n));
    vsp--;
    return iv;
}
static int exc_line;
/* ---- tracebacks: the frames are recorded when the exception is thrown (the C frames are gone
   once the handler runs); tb_attach turns the part between the catching frame and the raise point
   into the hidden attribute __tbraw__ of the exception (flat tuple: fn, module prefix, class body
   name, line for every frame, outermost first); `e.__traceback__` builds objects from it */
static void tb_add(Frame *f, int line)
{
    if (tb_n >= tb_cap) {
        tb_cap = tb_cap ? tb_cap * 2 : 32;
        tb_buf = (TBEnt *)xrealloc(tb_buf, sizeof(TBEnt) * (size_t)tb_cap);
    }
    tb_buf[tb_n].fn = f->fn;
    tb_buf[tb_n].gp = f->gp;
    tb_buf[tb_n].ns = f->ns;
    tb_buf[tb_n].line = line;
    tb_buf[tb_n].frame = f;
    tb_n++;
}
static void tb_snapshot(Value exc)
{
    Frame *f, *stop = cur_handler ? cur_handler->frame : NULL;
    int line = cur_line;
    if (tb_mode == 0 && tb_live && tb_excv.u.o == exc.u.o && tb_n > 0 && cur_frame &&
        tb_buf[tb_n - 1].frame == cur_frame) {
        /* the same exception propagates on (finally, import cleanup): extend the record */
        if (cur_frame == stop) return;
        f = cur_frame->up;
        line = f ? f->line : 0;
    } else {
        tb_n = 0;
        f = cur_frame;
        if (tb_mode == 2 && f) {   /* bare raise: the frame keeps the entry it already has */
            if (f == stop) return;
            f = f->up;
            line = f ? f->line : 0;
        }
    }
    for (; f; f = f->up) {
        tb_add(f, line);
        if (f == stop) break;
        line = f->up ? f->up->line : 0;
    }
}
/* the exception was caught by an except clause of frame f */
static void tb_attach(Value exc, Frame *f)
{
    Inst *in;
    Value *old;
    List *raw;
    int k, nnew = 0, nold = 0, i, j, total;
    if (exc.t != T_INST || tb_excv.u.o != exc.u.o || !tb_live) return;
    tb_live = 0;
    in = AS_INST(exc);
    for (k = 0; k < tb_n; k++) if (tb_buf[k].frame == f) { nnew = k + 1; break; }
    old = amap_get(&in->a, S_tbraw);
    if (old && old->t == T_TUPLE) nold = (int)(AS_LIST(*old)->n / 4);
    total = nnew + nold;
    if (total > 100) { nold -= total - 100; total = 100; }
    if (total <= 0) return;
    raw = tuple_new(4L * total);
    j = 0;
    for (i = nnew - 1; i >= 0; i--) {   /* outermost first */
        raw->a[j++] = tb_buf[i].fn ? mkobj(tb_buf[i].fn, T_FUNC) : NONE_V;
        raw->a[j++] = tb_buf[i].gp ? mkobj(tb_buf[i].gp, T_STR) : NONE_V;
        raw->a[j++] = tb_buf[i].ns ? mkobj(tb_buf[i].ns->name, T_STR) : NONE_V;
        raw->a[j++] = mkint(tb_buf[i].line);
    }
    for (i = 0; i < nold * 4; i++) raw->a[j++] = AS_LIST(*old)->a[i];
    amap_set(&in->a, S_tbraw, mkobj(raw, T_TUPLE));
    amap_del(&in->a, S_traceback);
}
static void throw_val(Value exc)
{
    cur_exc = exc;
    exc_line = cur_line;
    if (exc.t == T_INST) {
        tb_snapshot(exc);
        if (handled_exc.t == T_INST && handled_exc.u.o != exc.u.o &&
            !amap_get(&AS_INST(exc)->a, S_context))
            amap_set(&AS_INST(exc)->a, S_context, handled_exc);
    }
    tb_mode = 0;
    tb_live = 1;
    tb_excv = exc;
    if (!cur_handler) {
        fflush(stdout);
        fprintf(stderr, "fatal: uncaught exception outside handler\n");
        exit(2);
    }
    longjmp(cur_handler->jb, 1);
}
static void throw_key(Value key)
{
    Value v[1];
    v[0] = key;
    throw_val(new_exc_args(cls_KeyError, v, 1));
}

/* a range of small ints keeps them in a/b/c as T_INT; one with a large bound is "big" and all
   its arithmetic goes through the integer functions */
static Value make_range_v(Value a, Value b, Value c)
{
    Range *r = (Range *)gc_alloc(sizeof(Range), T_RANGE);
    if (a.t == T_BOOL) a = mkint(a.u.i);
    if (b.t == T_BOOL) b = mkint(b.u.i);
    if (c.t == T_BOOL) c = mkint(c.u.i);
    r->a = a; r->b = b; r->c = c;
    r->big = !(a.t == T_INT && b.t == T_INT && c.t == T_INT);
    return mkobj(r, T_RANGE);
}
static Value make_range(long a, long b, long c)
{
    return make_range_v(mkint(a), mkint(b), mkint(c));
}
static long range_len_small(Range *r)
{
    long start = r->a.u.i, stop = r->b.u.i, step = r->c.u.i;
    if (step > 0) return start < stop ? (stop - start - 1) / step + 1 : 0;
    return start > stop ? (start - stop - 1) / (-step) + 1 : 0;
}
/* the length as an integer (any size) */
static Value range_lenv(Range *r)
{
    if (!r->big) return mkint(range_len_small(r));
    if (int_sign(r->c) > 0) {
        if (int_cmp(r->a, r->b) >= 0) return mkint(0);
        return int_add(int_floordiv(int_sub(int_sub(r->b, r->a), mkint(1)), r->c), mkint(1));
    }
    if (int_cmp(r->a, r->b) <= 0) return mkint(0);
    return int_add(int_floordiv(int_sub(int_sub(r->a, r->b), mkint(1)), int_neg(r->c)), mkint(1));
}
static long range_len(Range *r)
{
    Value v;
    if (!r->big) return range_len_small(r);
    {
        long n;
        v = range_lenv(r);
        if (!int_fits_long(v, &n))
            throw_new(cls_OverflowError, "Python int too large to convert to C ssize_t");
        return n;
    }
}
/* start + i * step */
static Value range_item(Range *r, Value i)
{
    return int_add(r->a, int_mul(i, r->c));
}

static Value mk_prop(int type, Value g, Value s)
{
    Prop *p = (Prop *)gc_alloc(sizeof(Prop), type);
    p->get = g; p->set = s; p->del = UNSET_V;
    return mkobj(p, type);
}

/* ---- truthiness / length ---------------------------------------------- */
static long py_len(Value v);
static int py_truthy(Value v)
{
    switch (v.t) {
    case T_NONE: return 0;
    case T_BOOL: case T_INT: return v.u.i != 0;
    case T_FLOAT: return v.u.d != 0;
    case T_STR: case T_BYTES: return AS_STR(v)->len != 0;
    case T_BARR: return AS_BARR(v)->n != 0;
    case T_COMPLEX: return AS_CPLX(v)->re != 0 || AS_CPLX(v)->im != 0;
    case T_LIST: case T_TUPLE: return AS_LIST(v)->n != 0;
    case T_DICT: case T_SET: return AS_DICT(v)->n != 0;
    case T_RANGE: return int_sign(range_lenv((Range *)v.u.o)) != 0;
    case T_ND: return nd_truthy(v);
    case T_DVIEW: return AS_DICT(((DView *)v.u.o)->d)->n != 0;
    case T_INST: {
        Value *m = class_find(AS_INST(v)->cls, S_bool);
        if (m) return py_truthy(call1(*m, v));
        m = class_find(AS_INST(v)->cls, S_len);
        if (m) return py_len(v) != 0;
        if (AS_INST(v)->pay.t == T_INT || AS_INST(v)->pay.t == T_FLOAT ||
            AS_INST(v)->pay.t == T_BIG || AS_INST(v)->pay.t == T_COMPLEX)
            return py_truthy(AS_INST(v)->pay);
        return 1; }
    default: return 1;
    }
}
static long py_len(Value v)
{
    switch (v.t) {
    case T_STR: case T_BYTES: return AS_STR(v)->len;
    case T_BARR: return AS_BARR(v)->n;
    case T_LIST: case T_TUPLE: return AS_LIST(v)->n;
    case T_DICT: case T_SET: return AS_DICT(v)->n;
    case T_RANGE: return range_len((Range *)v.u.o);
    case T_ND: return nd_len(v);
    case T_DVIEW: return AS_DICT(((DView *)v.u.o)->d)->n;
    case T_CLASS: {
        Value *m = AS_CLASS(v)->meta ? class_find(AS_CLASS(v)->meta, S_len) : NULL;
        if (m) {
            Value r = call1(*m, v);
            if (r.t != T_INT)
                throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
                type_name(r));
            return r.u.i;
        }
        break; }
    case T_INST: {
        Value *m = class_find(AS_INST(v)->cls, S_len);
        if (m) {
            Value r = call1(*m, v);
            if (r.t != T_INT)
                throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
                type_name(r));
            if (r.u.i < 0) throw_new(cls_ValueError, "__len__() should return >= 0");
            return r.u.i;
        }
        break; }
    default: break;
    }
    throw_new(cls_TypeError, "object of type '%.60s' has no len()", type_name(v));
    return 0;
}

/* ---- numbers ---------------------------------------------------------- */
static int is_num(Value v)
{
    return v.t == T_INT || v.t == T_BOOL || v.t == T_FLOAT || v.t == T_BIG;
}
static double to_dbl(Value v)
{
    if (v.t == T_INST && AS_INST(v)->pay.t != T_UNSET) v = AS_INST(v)->pay;
    if (v.t == T_FLOAT) return v.u.d;
    return int_todouble(v);
}
/* an integer value (any size) from an int, bool, int subclass or object with __index__ */
static Value need_intv(Value v, const char *what)
{
    if (v.t == T_INST && (AS_INST(v)->pay.t == T_INT || AS_INST(v)->pay.t == T_BIG))
        return AS_INST(v)->pay;
    if (v.t == T_INT || v.t == T_BIG) return v;
    if (v.t == T_BOOL) return mkint(v.u.i);
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_index);
        if (m) { Value r = call1(*m, v); if (r.t == T_INT || r.t == T_BIG) return r; }
    }
    throw_new(cls_TypeError, "%.60s must be an integer, not '%.40s'", what, type_name(v));
    return v;
}
static long need_long(Value v, const char *what)
{
    Value r = need_intv(v, what);
    long n;
    if (!int_fits_long(r, &n))
        throw_new(cls_OverflowError, "Python int too large to convert to C ssize_t");
    return n;
}

/* ---- dict views ------------------------------------------------------- */
static Value mkdview(Value d, int what)
{
    DView *v = (DView *)gc_alloc(sizeof(DView), T_DVIEW);
    v->d = d;
    v->what = what;
    return mkobj(v, T_DVIEW);
}
/* snapshot of a dict view as a set (keys and items only) */
static Value dview_set(Value v)
{
    DView *dv = (DView *)v.u.o;
    Dict *d = AS_DICT(dv->d), *r = dict_new_t(T_SET);
    Value rv = mkobj(r, T_SET);
    long i;
    VPUSH(rv);
    for (i = 0; i < d->used; i++) {
        if (d->e[i].k.t == T_UNSET) continue;
        if (dv->what == 0) set_add(r, d->e[i].k);
        else if (dv->what == 2) set_add(r, mktuple2(d->e[i].k, d->e[i].v));
        else set_add(r, d->e[i].v);
    }
    vsp--;
    return rv;
}

/* ---- equality --------------------------------------------------------- */
/* real and imaginary part of a complex or real number; 0 for anything else */
static int cplx_parts(Value v, double *re, double *im)
{
    if (v.t == T_COMPLEX) { *re = AS_CPLX(v)->re; *im = AS_CPLX(v)->im; return 1; }
    if (is_num(v)) { *re = to_dbl(v); *im = 0.0; return 1; }
    return 0;
}
/* exact order of two real numbers (ints compare to floats without rounding the int);
   *unord is set for NaN */
static int num_cmp(Value a, Value b, int *unord)
{
    *unord = 0;
    if (a.t == T_FLOAT && b.t == T_FLOAT) {
        double x = a.u.d, y = b.u.d;
        if (x != x || y != y) { *unord = 1; return 0; }
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (a.t == T_FLOAT) {
        if (a.u.d != a.u.d) { *unord = 1; return 0; }
        return -int_cmp_dbl(b, a.u.d);
    }
    if (b.t == T_FLOAT) {
        if (b.u.d != b.u.d) { *unord = 1; return 0; }
        return int_cmp_dbl(a, b.u.d);
    }
    return int_cmp(a, b);
}
static int num_eq(Value a, Value b)
{
    int u, c;
    if (a.t == T_FLOAT || b.t == T_FLOAT) {
        c = num_cmp(a, b, &u);
        return !u && c == 0;
    }
    return int_cmp(a, b) == 0;
}
static int py_eq(Value a, Value b)
{
    if (is_num(a) && is_num(b)) return num_eq(a, b);
    if (a.t == T_COMPLEX || b.t == T_COMPLEX) {
        double ar, ai, br, bi;
        if (a.t == T_COMPLEX && is_num(b))
            return AS_CPLX(a)->im == 0.0 && num_eq(mkfloat(AS_CPLX(a)->re), b);
        if (b.t == T_COMPLEX && is_num(a))
            return AS_CPLX(b)->im == 0.0 && num_eq(mkfloat(AS_CPLX(b)->re), a);
        if (cplx_parts(a, &ar, &ai) && cplx_parts(b, &br, &bi)) return ar == br && ai == bi;
        if (a.t != T_INST && b.t != T_INST) return 0;
    }
    if ((a.t == T_BYTES || a.t == T_BARR) && (b.t == T_BYTES || b.t == T_BARR)) {
        const char *pa, *pb;
        long na, nb;
        bytes_get(a, &pa, &na); bytes_get(b, &pb, &nb);
        return na == nb && memcmp(pa, pb, (size_t)na) == 0;
    }
    if (a.t == T_DVIEW || b.t == T_DVIEW) {
        Value x = a, y = b;
        int r;
        Value *base = vsp;
        if ((a.t == T_DVIEW && ((DView *)a.u.o)->what == 1) ||
            (b.t == T_DVIEW && ((DView *)b.u.o)->what == 1)) return a.t == b.t && a.u.o == b.u.o;
        if (a.t == T_DVIEW) x = dview_set(a);
        VPUSH(x);
        if (b.t == T_DVIEW) y = dview_set(b);
        if (x.t != T_SET || y.t != T_SET) { vsp = base; return 0; }
        r = py_eq(x, y);
        vsp = base;
        return r;
    }
    if (a.t != b.t) {
        if (a.t == T_INST || b.t == T_INST) goto inst;
        return 0;
    }
    switch (a.t) {
    case T_NONE: return 1;
    case T_STR: case T_BYTES:
        return a.u.o == b.u.o || (AS_STR(a)->len == AS_STR(b)->len &&
            memcmp(AS_STR(a)->s, AS_STR(b)->s, (size_t)AS_STR(a)->len) == 0);
    case T_LIST: case T_TUPLE: case T_SLICE: {
        List *x = AS_LIST(a), *y = AS_LIST(b);
        long i;
        if (x == y) return 1;
        if (x->n != y->n) return 0;
        for (i = 0; i < x->n && i < y->n; i++)
            if (!py_eq(x->a[i], y->a[i])) return 0;
        return 1; }
    case T_DICT: {
        Dict *x = AS_DICT(a), *y = AS_DICT(b);
        long i;
        Value o;
        if (x->n != y->n) return 0;
        for (i = 0; i < x->used; i++) {
            if (x->e[i].k.t == T_UNSET) continue;
            if (!dict_get(y, x->e[i].k, &o)) return 0;
            if (!py_eq(x->e[i].v, o)) return 0;
        }
        return 1; }
    case T_SET: {
        Dict *x = AS_DICT(a), *y = AS_DICT(b);
        long i;
        Value o;
        (void)o;
        if (x->n != y->n) return 0;
        for (i = 0; i < x->used; i++) {
            if (x->e[i].k.t == T_UNSET) continue;
            if (!set_has_h(y, x->e[i].k, x->e[i].hash)) return 0;
        }
        return 1; }
    case T_RANGE: {
        Range *x = (Range *)a.u.o, *y = (Range *)b.u.o;
        Value lx = range_lenv(x), ly = range_lenv(y);
        if (int_cmp(lx, ly) != 0) return 0;
        if (int_sign(lx) == 0) return 1;
        if (int_cmp(x->a, y->a) != 0) return 0;
        return (lx.t == T_INT && lx.u.i == 1) || int_cmp(x->c, y->c) == 0; }
    case T_INST: goto inst;
    default: return a.u.o == b.u.o;
    }
inst:
    if (a.t == T_INST) {
        Value *m = class_find(AS_INST(a)->cls, S_eq);
        if (m) {
            Value r = call2(*m, a, b);
            if (!IS_NOTIMPL(r)) return py_truthy(r);
        }
    }
    if (b.t == T_INST) {
        Value *m = class_find(AS_INST(b)->cls, S_eq);
        if (m) {
            Value r = call2(*m, b, a);
            if (!IS_NOTIMPL(r)) return py_truthy(r);
        }
    }
    return a.t == b.t && a.u.o == b.u.o;
}

/* CPython's tuple hash (xxHash based since 3.8): exact when unsigned long has 64 bits */
#if ULONG_MAX > 0xffffffffUL
#define XXP1 0x9E3779B185EBCA87UL
#define XXP2 0xC2B2AE3D27D4EB4FUL
#define XXP5 0x27D4EB2F165667C5UL
#define XXROT(x) (((x) << 31) | ((x) >> 33))
#else
#define XXP1 0x9E3779B1UL
#define XXP2 0x85EBCA77UL
#define XXP5 0x165667B1UL
#define XXROT(x) (((x) << 13) | ((x) >> 19))
#endif
static unsigned long tuple_hash(List *l)
{
    unsigned long acc = XXP5, lane;
    long i;
    for (i = 0; i < l->n; i++) {
        lane = py_hash(l->a[i]);
        acc += lane * XXP2;
        acc = XXROT(acc);
        acc *= XXP1;
    }
    acc += (unsigned long)l->n ^ (XXP5 ^ 3527539UL);
    if (acc == (unsigned long)-1L) return 1546275796UL;
    return acc;
}
static unsigned long py_hash(Value v)
{
    switch (v.t) {
    case T_NONE: return 4238894112UL;
    case T_BOOL: case T_INT: case T_BIG: return int_hash(v);
    case T_FLOAT: return hash_fold(float_hash_exact(v.u.d));
    case T_STR: case T_BYTES: return str_hash(AS_STR(v));
    case T_TUPLE: return tuple_hash(AS_LIST(v));
    case T_COMPLEX: return hash_fold(complex_hash_exact(AS_CPLX(v)->re, AS_CPLX(v)->im));
    case T_SET:
        if (AS_DICT(v)->frozen) return fset_hash(AS_DICT(v));
        throw_new(cls_TypeError, "unhashable type: '%.40s'", type_name(v));
        return 0;
    case T_LIST: case T_DICT: case T_BARR:
        throw_new(cls_TypeError, "unhashable type: '%.40s'", type_name(v));
        return 0;
    case T_INST: {
        Value *m = class_find(AS_INST(v)->cls, S_hash);
        if (m) {
            Value r = call1(*m, v);
            if (r.t == T_INT || r.t == T_BOOL || r.t == T_BIG) return hash_fold(user_hash_val(r));
            throw_new(cls_TypeError, "__hash__ method should return an integer");
        }
        if (class_find(AS_INST(v)->cls, S_eq))
            throw_new(cls_TypeError, "unhashable type: '%.40s'", type_name(v));
        return (unsigned long)(size_t)v.u.o >> 3; }
    case T_RANGE: return 99UL;
    default: return (unsigned long)(size_t)v.u.o >> 3;
    }
}

/* ---- ordering --------------------------------------------------------- */
static int cmp3(Value a, Value b, int *ok);
static int rich_lt_hook(Value a, Value b);
static Value propdeleter_fn, propgetter_fn, propsetter_fn, obj_eq_v, obj_ne_v, obj_hash_v;
static Str **op_names(int op, int rev)
{
    static Str *fwd[11], *bwd[11];
    if (!fwd[C_LT]) {
        fwd[C_LT] = S_lt; fwd[C_LE] = S_le; fwd[C_GT] = S_gt; fwd[C_GE] = S_ge;
        fwd[C_EQ] = S_eq; fwd[C_NE] = S_ne;
        bwd[C_LT] = S_gt; bwd[C_LE] = S_ge; bwd[C_GT] = S_lt; bwd[C_GE] = S_le;
        bwd[C_EQ] = S_eq; bwd[C_NE] = S_ne;
    }
    return rev ? &bwd[op] : &fwd[op];
}
static const char *op_sym(int op)
{
    switch (op) {
    case C_LT: return "<"; case C_LE: return "<="; case C_GT: return ">";
    case C_GE: return ">="; case C_EQ: return "=="; default: return "!=";
    }
}
/* three way compare of builtin values; *ok = 0 if unorderable */
static int cmp3(Value a, Value b, int *ok)
{
    *ok = 1;
    if (is_num(a) && is_num(b)) {
        if (a.t == T_FLOAT || b.t == T_FLOAT) {
            int u;
            return num_cmp(a, b, &u);
        }
        return int_cmp(a, b);
    }
    if ((a.t == T_BARR || b.t == T_BARR) && (a.t == T_BYTES || a.t == T_BARR) &&
        (b.t == T_BYTES || b.t == T_BARR)) {
        const char *pa, *pb;
        long na, nb, n;
        int c;
        bytes_get(a, &pa, &na); bytes_get(b, &pb, &nb);
        n = na < nb ? na : nb;
        c = memcmp(pa, pb, (size_t)n);
        if (c) return c < 0 ? -1 : 1;
        return na < nb ? -1 : (na > nb ? 1 : 0);
    }
    if ((a.t == T_STR && b.t == T_STR) || (a.t == T_BYTES && b.t == T_BYTES)) {
        Str *x = AS_STR(a), *y = AS_STR(b);
        long n = x->len < y->len ? x->len : y->len;
        int c = memcmp(x->s, y->s, (size_t)n);
        if (c) return c < 0 ? -1 : 1;
        return x->len < y->len ? -1 : (x->len > y->len ? 1 : 0);
    }
    if ((a.t == T_LIST && b.t == T_LIST) || (a.t == T_TUPLE && b.t == T_TUPLE)) {
        List *x = AS_LIST(a), *y = AS_LIST(b);
        long i;
        for (i = 0; i < x->n && i < y->n; i++) {
            if (!py_eq(x->a[i], y->a[i])) {
                if (rich_lt_hook(x->a[i], y->a[i])) return -1;
                return 1;
            }
        }
        return x->n < y->n ? -1 : (x->n > y->n ? 1 : 0);
    }
    *ok = 0;
    return 0;
}
static int rich_cmp(int op, Value a, Value b)
{
    if (op == C_EQ) {
        if (a.t == T_INST || b.t == T_INST) return py_eq(a, b);
        return py_eq(a, b);
    }
    if (op == C_NE) {
        if (a.t == T_INST) {
            Value *m = class_find(AS_INST(a)->cls, S_ne);
            if (m) {
                Value r = call2(*m, a, b);
                if (!IS_NOTIMPL(r)) return py_truthy(r);
            }
        }
        return !py_eq(a, b);
    }
    if (a.t == T_INST) {
        Value *m = class_find(AS_INST(a)->cls, *op_names(op, 0));
        if (m) {
            Value r = call2(*m, a, b);
            if (!IS_NOTIMPL(r)) return py_truthy(r);
        }
    }
    if (b.t == T_INST) {
        Value *m = class_find(AS_INST(b)->cls, *op_names(op, 1));
        if (m) {
            Value r = call2(*m, b, a);
            if (!IS_NOTIMPL(r)) return py_truthy(r);
        }
    }
    if (a.t == T_FLOAT || b.t == T_FLOAT) {
        if (is_num(a) && is_num(b)) {
            int u, c = num_cmp(a, b, &u);
            if (u) return 0;
            switch (op) {
            case C_LT: return c < 0; case C_LE: return c <= 0;
            case C_GT: return c > 0; default: return c >= 0;
            }
        }
    }
    if ((a.t == T_DVIEW || b.t == T_DVIEW)
        && (a.t == T_SET || b.t == T_SET || (a.t == T_DVIEW && b.t == T_DVIEW))) {
        Value *base = vsp;
        if (a.t == T_DVIEW) a = dview_set(a);
        VPUSH(a);
        if (b.t == T_DVIEW) b = dview_set(b);
        VPUSH(b);
        op = rich_cmp(op, a, b);
        vsp = base;
        return op;
    }
    if (a.t == T_SET && b.t == T_SET) {
        Dict *x = AS_DICT(a), *y = AS_DICT(b);
        long i;
        Value o;
        int sub = 1, sup = 1;
        (void)o;
        for (i = 0; i < x->used; i++)
            if (x->e[i].k.t != T_UNSET && !set_has_h(y, x->e[i].k, x->e[i].hash)) sub = 0;
        for (i = 0; i < y->used; i++)
            if (y->e[i].k.t != T_UNSET && !set_has_h(x, y->e[i].k, y->e[i].hash)) sup = 0;
        switch (op) {
        case C_LE: return sub;
        case C_LT: return sub && x->n < y->n;
        case C_GE: return sup;
        default: return sup && x->n > y->n;
        }
    }
    {
        int ok, c = cmp3(a, b, &ok);
        if (!ok)
            throw_new(cls_TypeError, "'%s' not supported between instances of '%.40s' and '%.40s'",
                      op_sym(op), type_name(a), type_name(b));
        switch (op) {
        case C_LT: return c < 0; case C_LE: return c <= 0;
        case C_GT: return c > 0; default: return c >= 0;
        }
    }
}
/* rich comparison on instances: return the raw result of the dunder method (pandas style masks) */
static int inst_cmp_value(int op, Value a, Value b, Value *out)
{
    Value *m, r;
    if (a.t == T_INST) {
        m = class_find(AS_INST(a)->cls, *op_names(op, 0));
        if (m) {
            r = call2(*m, a, b);
            if (!IS_NOTIMPL(r)) { *out = r; return 1; }
        }
    }
    if (b.t == T_INST) {
        m = class_find(AS_INST(b)->cls, *op_names(op, 1));
        if (m) {
            r = call2(*m, b, a);
            if (!IS_NOTIMPL(r)) { *out = r; return 1; }
        }
    }
    return 0;
}
static int rich_lt_hook(Value a, Value b)
{
    return rich_cmp(C_LT, a, b);
}

/* ---- repr / str ------------------------------------------------------- */
static void repr_buf(Value v, Buf *b, int depth);
static void str_buf(Value v, Buf *b);

/* decode one UTF-8 sequence at s[i..n): the code point, and its length in *len */
static unsigned long u8_next(const unsigned char *s, long i, long n, int *len)
{
    unsigned long c = s[i];
    int k = 1;
    if (c >= 0xF0 && i + 3 < n && (s[i + 1] & 0xC0) == 0x80) {
        c = ((c & 0x07UL) << 18) | ((s[i + 1] & 0x3FUL) << 12) | ((s[i + 2] & 0x3FUL) << 6)
            | (s[i + 3] & 0x3FUL);
        k = 4;
    } else if (c >= 0xE0 && c < 0xF0 && i + 2 < n && (s[i + 1] & 0xC0) == 0x80) {
        c = ((c & 0x0FUL) << 12) | ((s[i + 1] & 0x3FUL) << 6) | (s[i + 2] & 0x3FUL);
        k = 3;
    } else if (c >= 0xC0 && c < 0xE0 && i + 1 < n && (s[i + 1] & 0xC0) == 0x80) {
        c = ((c & 0x1FUL) << 6) | (s[i + 1] & 0x3FUL);
        k = 2;
    }
    *len = k;
    return c;
}
/* code points that str.isprintable()/repr() treat as not printable (categories C* and Z*
   except the space), as far as can be told without the whole Unicode table */
static int uni_nonprint(unsigned long c)
{
    static const unsigned long rg[][2] = {
        { 0x80UL, 0xA0UL }, { 0xADUL, 0xADUL }, { 0x378UL, 0x379UL }, { 0x600UL, 0x605UL },
        { 0x61CUL, 0x61CUL }, { 0x6DDUL, 0x6DDUL }, { 0x70FUL, 0x70FUL }, { 0x8E2UL, 0x8E2UL },
        { 0x1680UL, 0x1680UL }, { 0x180EUL, 0x180EUL }, { 0x2000UL, 0x200FUL },
        { 0x2028UL, 0x202FUL }, { 0x205FUL, 0x206FUL }, { 0x3000UL, 0x3000UL },
        { 0xD800UL, 0xF8FFUL }, { 0xFEFFUL, 0xFEFFUL }, { 0xFFF0UL, 0xFFFBUL },
        { 0xFFFEUL, 0xFFFFUL }, { 0x1D173UL, 0x1D17AUL }, { 0xE0000UL, 0xE00FFUL },
        { 0xE01F0UL, 0xE0FFFUL }, { 0xF0000UL, 0x10FFFFUL }
    };
    int i;
    for (i = 0; i < (int)(sizeof(rg) / sizeof(rg[0])); i++)
        if (c >= rg[i][0] && c <= rg[i][1]) return 1;
    return 0;
}
static void repr_str_lit(Str *s, Buf *b)
{
    long i;
    int q = '\'';
    char t[40];
    if (memchr(s->s, '\'', (size_t)s->len) && !memchr(s->s, '"', (size_t)s->len)) q = '"';
    buf_addc(b, q);
    for (i = 0; i < s->len; i++) {
        int c = (unsigned char)s->s[i];
        if (c == q || c == '\\') { buf_addc(b, '\\'); buf_addc(b, c); }
        else if (c == '\n') buf_add(b, "\\n");
        else if (c == '\t') buf_add(b, "\\t");
        else if (c == '\r') buf_add(b, "\\r");
        else if (c < 0x80 && !ch_print[c]) { sprintf(t, "\\x%02x", c); buf_add(b, t); }
        else if (c >= 0xC2) {
            int k;
            unsigned long u = u8_next((const unsigned char *)s->s, i, s->len, &k);
            if (k > 1 && uni_nonprint(u)) {
                if (u < 256UL) sprintf(t, "\\x%02lx", u);
                else if (u < 65536UL) sprintf(t, "\\u%04lx", u);
                else sprintf(t, "\\U%08lx", u);
                buf_add(b, t);
            } else {
                int j;
                for (j = 0; j < k; j++) buf_addc(b, (unsigned char)s->s[i + j]);
            }
            i += k - 1;
        }
        else buf_addc(b, c);
    }
    buf_addc(b, q);
}
static void repr_bytes_lit(const char *s, long n, Buf *b)
{
    long i;
    int q = '\'';
    char tt[8];
    if (memchr(s, '\'', (size_t)n) && !memchr(s, '"', (size_t)n)) q = '"';
    buf_addc(b, 'b');
    buf_addc(b, q);
    for (i = 0; i < n; i++) {
        int c = (unsigned char)s[i];
        if (c == q || c == '\\') { buf_addc(b, '\\'); buf_addc(b, c); }
        else if (c == '\n') buf_add(b, "\\n");
        else if (c == '\t') buf_add(b, "\\t");
        else if (c == '\r') buf_add(b, "\\r");
        else if (!ch_print[c]) { sprintf(tt, "\\x%02x", c); buf_add(b, tt); }
        else buf_addc(b, c);
    }
    buf_addc(b, q);
}
/* one component of a complex repr: the float repr without a trailing ".0" */
static void complex_part(double d, Buf *b)
{
    char t[64];
    long n;
    float_repr(d, t);
    n = (long)strlen(t);
    if (n > 2 && t[n - 1] == '0' && t[n - 2] == '.') t[n - 2] = 0;
    buf_add(b, t);
}
static int signbit_d(double d)
{
    if (d < 0) return 1;
    if (d == 0) return 1.0 / d < 0;
    return 0;
}
static void complex_repr(double re, double im, Buf *b)
{
    int paren = !(re == 0.0 && !signbit_d(re));
    if (paren) { buf_addc(b, '('); complex_part(re, b); }
    if (im != im || !signbit_d(im)) { if (paren) buf_addc(b, '+'); complex_part(im, b); }
    else { if (paren) { buf_addc(b, '-'); complex_part(-im, b); } else complex_part(im, b); }
    buf_addc(b, 'j');
    if (paren) buf_addc(b, ')');
}
static void exc_str_buf(Inst *in, Buf *b, int repr);
static void repr_buf(Value v, Buf *b, int depth)
{
    char t[64];
    long i;
    if (depth > 60) { buf_add(b, "..."); return; }
    switch (v.t) {
    case T_UNSET: buf_add(b, "<unset>"); break;
    case T_NONE: buf_add(b, "None"); break;
    case T_BOOL: buf_add(b, v.u.i ? "True" : "False"); break;
    case T_INT: case T_BIG: int_str(v, b); break;
    case T_FLOAT: float_repr(v.u.d, t); buf_add(b, t); break;
    case T_STR: repr_str_lit(AS_STR(v), b); break;
    case T_BYTES: repr_bytes_lit(AS_STR(v)->s, AS_STR(v)->len, b); break;
    case T_BARR:
        buf_add(b, "bytearray(");
        repr_bytes_lit(AS_BARR(v)->s, AS_BARR(v)->n, b);
        buf_addc(b, ')');
        break;
    case T_COMPLEX: complex_repr(AS_CPLX(v)->re, AS_CPLX(v)->im, b); break;
    case T_LIST: case T_TUPLE: {
        List *l = AS_LIST(v);
        buf_addc(b, v.t == T_LIST ? '[' : '(');
        for (i = 0; i < l->n; i++) {
            if (i) buf_add(b, ", ");
            repr_buf(l->a[i], b, depth + 1);
        }
        if (v.t == T_TUPLE && l->n == 1) buf_addc(b, ',');
        buf_addc(b, v.t == T_LIST ? ']' : ')');
        break; }
    case T_DICT: {
        Dict *d = AS_DICT(v);
        int first = 1;
        buf_addc(b, '{');
        for (i = 0; i < d->used; i++) {
            if (d->e[i].k.t == T_UNSET) continue;
            if (!first) buf_add(b, ", ");
            first = 0;
            repr_buf(d->e[i].k, b, depth + 1);
            buf_add(b, ": ");
            repr_buf(d->e[i].v, b, depth + 1);
        }
        buf_addc(b, '}');
        break; }
    case T_SET: {
        Dict *d = AS_DICT(v);
        int first = 1;
        if (d->n == 0) { buf_add(b, d->frozen ? "frozenset()" : "set()"); break; }
        if (d->frozen) buf_add(b, "frozenset(");
        buf_addc(b, '{');
        for (i = 0; i < d->used; i++) {
            if (d->e[i].k.t == T_UNSET) continue;
            if (!first) buf_add(b, ", ");
            first = 0;
            repr_buf(d->e[i].k, b, depth + 1);
        }
        buf_addc(b, '}');
        if (d->frozen) buf_addc(b, ')');
        break; }
    case T_RANGE: {
        Range *r = (Range *)v.u.o;
        buf_add(b, "range(");
        int_str(r->a, b);
        buf_add(b, ", ");
        int_str(r->b, b);
        if (!(r->c.t == T_INT && r->c.u.i == 1)) { buf_add(b, ", "); int_str(r->c, b); }
        buf_addc(b, ')');
        break; }
    case T_FUNC:
        buf_add(b, "<function ");
        buf_add(b, AS_FUNC(v)->name->s);
        sprintf(t, " at 0x%lx>", (unsigned long)(size_t)v.u.o);
        buf_add(b, t);
        break;
    case T_BUILTIN:
        buf_add(b, "<built-in function ");
        buf_add(b, ((Builtin *)v.u.o)->name);
        buf_addc(b, '>');
        break;
    case T_CLASS:
        buf_add(b, "<class '");
        if (!(AS_CLASS(v)->flags & CF_BUILTIN)) buf_add(b, "__main__.");
        else if (AS_CLASS(v)->modname && strcmp(AS_CLASS(v)->modname->s, "builtins") != 0) {
            buf_add(b, AS_CLASS(v)->modname->s);
            buf_addc(b, '.');
        }
        buf_add(b, AS_CLASS(v)->name->s);
        buf_add(b, "'>");
        break;
    case T_INST: {
        Inst *in = AS_INST(v);
        Value *m = class_find(in->cls, S_repr);
        if (m) {
            Value r = call1(*m, v);
            if (r.t != T_STR)
                throw_new(cls_TypeError, "__repr__ returned non-string (type %.40s)", type_name(r));
            buf_addn(b, AS_STR(r)->s, AS_STR(r)->len);
        } else {
            buf_add(b, "<__main__.");
            buf_add(b, in->cls->name->s);
            sprintf(t, " object at 0x%lx>", (unsigned long)(size_t)v.u.o);
            buf_add(b, t);
        }
        break; }
    case T_BOUND:
        buf_add(b, "<bound method ");
        {
            Value fn = ((Bound *)v.u.o)->fn;
            if (fn.t == T_FUNC) buf_add(b, AS_FUNC(fn)->name->s);
            else if (fn.t == T_BUILTIN) buf_add(b, ((Builtin *)fn.u.o)->name);
        }
        buf_add(b, " of ");
        repr_buf(((Bound *)v.u.o)->self, b, depth + 1);
        buf_addc(b, '>');
        break;
    case T_GEN:
        sprintf(t, "<generator object at 0x%lx>", (unsigned long)(size_t)v.u.o);
        buf_add(b, t);
        break;
    case T_MODULE:
        buf_add(b, "<module '");
        buf_add(b, ((Module *)v.u.o)->name->s);
        buf_add(b, "'>");
        break;
    case T_FILE: buf_add(b, "<_io.TextIOWrapper>"); break;
    case T_ND: nd_format(v, b, 1); break;
    case T_SLICE: {
        List *sl = AS_LIST(v);
        buf_add(b, "slice(");
        repr_buf(sl->a[0], b, depth + 1); buf_add(b, ", ");
        repr_buf(sl->a[1], b, depth + 1); buf_add(b, ", ");
        repr_buf(sl->a[2], b, depth + 1); buf_addc(b, ')');
        break; }
    case T_DVIEW: {
        DView *dv = (DView *)v.u.o;
        Dict *d = AS_DICT(dv->d);
        int first = 1;
        buf_add(b,
            dv->what == 0 ? "dict_keys([" : (dv->what == 1 ? "dict_values([" : "dict_items(["));
        for (i = 0; i < d->used; i++) {
            if (d->e[i].k.t == T_UNSET) continue;
            if (!first) buf_add(b, ", ");
            first = 0;
            if (dv->what == 0) repr_buf(d->e[i].k, b, depth + 1);
            else if (dv->what == 1) repr_buf(d->e[i].v, b, depth + 1);
            else {
                buf_addc(b, '(');
                repr_buf(d->e[i].k, b, depth + 1); buf_add(b, ", ");
                repr_buf(d->e[i].v, b, depth + 1); buf_addc(b, ')');
            }
        }
        buf_add(b, "])");
        break; }
    default:
        sprintf(t, "<object at 0x%lx>", (unsigned long)(size_t)v.u.o);
        buf_add(b, t);
        break;
    }
}
static void exc_str_buf(Inst *in, Buf *b, int repr)
{
    Value *ap = amap_get(&in->a, S_args);
    List *l;
    long i;
    if (repr) {
        buf_add(b, in->cls->name->s);
        buf_addc(b, '(');
    }
    if (ap && ap->t == T_TUPLE) {
        l = AS_LIST(*ap);
        if (repr) {
            for (i = 0; i < l->n; i++) {
                if (i) buf_add(b, ", ");
                repr_buf(l->a[i], b, 1);
            }
        } else if ((l->n == 2 || l->n == 3) && l->a[0].t == T_INT && class_isa(in->cls,
            cls_OSError)) {
            char t[40];
            sprintf(t, "[Errno %ld] ", l->a[0].u.i);
            buf_add(b, t);
            str_buf(l->a[1], b);
            if (l->n == 3) { buf_add(b, ": "); repr_buf(l->a[2], b, 1); }
        } else if (l->n == 1) {
            if (class_isa(in->cls, cls_KeyError)) repr_buf(l->a[0], b, 1);
            else str_buf(l->a[0], b);
        } else if (l->n > 1) {
            repr_buf(*ap, b, 1);
        }
    }
    if (repr) buf_addc(b, ')');
}
static void str_buf(Value v, Buf *b)
{
    if (v.t == T_STR) { buf_addn(b, AS_STR(v)->s, AS_STR(v)->len); return; }
    if (v.t == T_ND) { nd_format(v, b, 0); return; }
    if (v.t == T_INST) {
        Inst *in = AS_INST(v);
        Value *m = class_find(in->cls, S_str);
        if (m && m->t == T_BUILTIN && m->u.o == obj_str_v.u.o && in->pay.t == T_STR) {
            buf_addn(b, AS_STR(in->pay)->s, AS_STR(in->pay)->len);
            return;
        }
        if (m) {
            Value r = call1(*m, v);
            if (r.t != T_STR)
                throw_new(cls_TypeError, "__str__ returned non-string (type %.40s)", type_name(r));
            buf_addn(b, AS_STR(r)->s, AS_STR(r)->len);
            return;
        }
    }
    repr_buf(v, b, 0);
}
static Value py_repr(Value v)
{
    Buf b;
    buf_init(&b);
    repr_buf(v, &b, 0);
    return buf_value(&b);
}
static Value py_str(Value v)
{
    Buf b;
    if (v.t == T_STR) return v;
    buf_init(&b);
    str_buf(v, &b);
    return buf_value(&b);
}

/* ---- attributes ------------------------------------------------------- */
static void attr_error(Value o, Str *nm)
{
    if (o.t == T_CLASS)
        throw_new(cls_AttributeError, "type object '%.60s' has no attribute '%.60s'",
                  AS_CLASS(o)->name->s, nm->s);
    if (o.t == T_MODULE)
        throw_new(cls_AttributeError, "module '%.60s' has no attribute '%.60s'",
                  ((Module *)o.u.o)->name->s, nm->s);
    throw_new(cls_AttributeError, "'%.60s' object has no attribute '%.60s'", type_name(o), nm->s);
}
/* global slot of attribute `nm` of a module written in Python (-1 if none and !create) */
static long module_slot(Module *m, Str *nm, int create)
{
    Value r;
    long gi;
    if (m->gmap && dict_get(m->gmap, mkobj(nm, T_STR), &r)) return r.u.i;
    gi = create ? global_index(prefixed(m->gp, nm)) : global_find(prefixed(m->gp, nm));
    if (gi >= 0) {
        if (!m->gmap) m->gmap = dict_new();
        dict_set(m->gmap, mkobj(nm, T_STR), mkint(gi));
    }
    return gi;
}
static Value bind_class_attr(Value o, Value *cv, Class *owner_for_cls)
{
    switch (cv->t) {
    case T_FUNC: case T_BUILTIN: return new_bound(o, *cv);
    case T_PROP: {
        Prop *p = (Prop *)cv->u.o;
        if (p->get.t == T_UNSET) throw_new(cls_AttributeError, "unreadable attribute");
        return call1(p->get, o); }
    case T_SMETH: return ((Prop *)cv->u.o)->get;
    case T_CMETH: return new_bound(mkobj(owner_for_cls, T_CLASS), ((Prop *)cv->u.o)->get);
    default: return *cv;
    }
}
static Value class_mro_tuple(Class *c)
{
    List *t;
    Class *k;
    long n = 0;
    if (c->mro) {
        int i;
        t = tuple_new(c->nmro);
        for (i = 0; i < c->nmro; i++) t->a[i] = mkobj(c->mro[i], T_CLASS);
        return mkobj(t, T_TUPLE);
    }
    for (k = c; k; k = k->base) n++;
    t = tuple_new(n);
    n = 0;
    for (k = c; k; k = k->base) t->a[n++] = mkobj(k, T_CLASS);
    return mkobj(t, T_TUPLE);
}

/* C3 linearisation: returns a malloc'ed array of *n classes */
typedef struct MSeq { Class **a; int n, pos; } MSeq;
static Class **class_mro_array(Class *c, int *n)
{
    Class **r;
    int i;
    if (c->mro) {
        r = (Class **)xmalloc(sizeof(Class *) * (size_t)(c->nmro + 1));
        for (i = 0; i < c->nmro; i++) r[i] = c->mro[i];
        *n = c->nmro;
        return r;
    }
    {
        Class *k;
        int cnt = 0;
        for (k = c; k; k = k->base) cnt++;
        r = (Class **)xmalloc(sizeof(Class *) * (size_t)(cnt + 1));
        cnt = 0;
        for (k = c; k; k = k->base) r[cnt++] = k;
        *n = cnt;
    }
    return r;
}
static void c3_build(Class *c, Class **bases, int nb)
{
    MSeq *sq = (MSeq *)xmalloc(sizeof(MSeq) * (size_t)(nb + 1));
    Class **res = (Class **)xmalloc(sizeof(Class *) * 64);
    int nres = 0, rcap = 64, i, j, k, remaining;
    res[nres++] = c;
    for (i = 0; i < nb; i++) { sq[i].a = class_mro_array(bases[i], &sq[i].n); sq[i].pos = 0; }
    sq[nb].a = (Class **)xmalloc(sizeof(Class *) * (size_t)(nb + 1));
    for (i = 0; i < nb; i++) sq[nb].a[i] = bases[i];
    sq[nb].n = nb; sq[nb].pos = 0;
    for (;;) {
        Class *cand = NULL;
        remaining = 0;
        for (i = 0; i <= nb; i++) if (sq[i].pos < sq[i].n) remaining = 1;
        if (!remaining) break;
        for (i = 0; i <= nb && !cand; i++) {
            Class *t;
            int ok = 1;
            if (sq[i].pos >= sq[i].n) continue;
            t = sq[i].a[sq[i].pos];
            for (j = 0; j <= nb && ok; j++)
                for (k = sq[j].pos + 1; k < sq[j].n; k++)
                    if (sq[j].a[k] == t) { ok = 0; break; }
            if (ok) cand = t;
        }
        if (!cand) {
            for (i = 0; i <= nb; i++) free(sq[i].a);
            free(sq); free(res);
            throw_new(cls_TypeError,
                "Cannot create a consistent method resolution order (MRO) for bases");
        }
        if (nres >= rcap) { rcap *= 2;
            res = (Class **)xrealloc(res, sizeof(Class *) * (size_t)rcap);
        }
        res[nres++] = cand;
        for (i = 0; i <= nb; i++)
            if (sq[i].pos < sq[i].n && sq[i].a[sq[i].pos] == cand) sq[i].pos++;
    }
    for (i = 0; i <= nb; i++) free(sq[i].a);
    free(sq);
    c->mro = res;
    c->nmro = nres;
}
/* obj.__dict__: one dict per instance, refreshed from the attributes at every access; changes made
   through it (update, item assignment, del, clear) are mirrored into the attributes (Dict.owner) */
static Value inst_dict_get(Inst *in)
{
    Dict *d = in->dct;
    int i;
    if (!d) { d = dict_new(); in->dct = d; }
    d->owner = NULL;
    dict_clear(d);
    for (i = 0; i < in->a.n; i++)
        if (in->a.k[i] != S_tbraw && in->a.k[i] != S_traceback && in->a.k[i] != S_cause &&
            in->a.k[i] != S_context && in->a.k[i] != S_suppress)
            dict_set(d, mkobj(in->a.k[i], T_STR), in->a.v[i]);
    d->owner = in;
    return mkobj(d, T_DICT);
}
static void inst_attrs_clear(Inst *in)
{
    int i, j = 0;
    for (i = 0; i < in->a.n; i++)   /* keep the hidden traceback attribute */
        if (in->a.k[i] == S_tbraw || in->a.k[i] == S_cause || in->a.k[i] == S_context ||
            in->a.k[i] == S_suppress) { in->a.k[j] = in->a.k[i]; in->a.v[j++] = in->a.v[i]; }
    in->a.n = j;
}
static void inst_dict_mirror(Dict *d, Value k, Value v, int del)
{
    Inst *in = d->owner;
    if (del == 2) { inst_attrs_clear(in); return; }
    if (k.t != T_STR) return;
    if (del) amap_del(&in->a, intern_str(AS_STR(k)));
    else amap_set(&in->a, intern_str(AS_STR(k)), v);
}
/* obj.__dict__ = d */
static void inst_dict_assign(Inst *in, Value v)
{
    Dict *d;
    long i;
    if (v.t != T_DICT)
        throw_new(cls_TypeError, "__dict__ must be set to a dictionary, not a '%.60s'",
                  type_name(v));
    d = AS_DICT(v);
    inst_attrs_clear(in);
    for (i = 0; i < d->used; i++)
        if (d->e[i].k.t == T_STR) amap_set(&in->a, intern_str(AS_STR(d->e[i].k)), d->e[i].v);
    if (in->dct && in->dct != d) in->dct->owner = NULL;
    in->dct = d;
    d->owner = in;
}
/* the class whose MRO a super object searches: the type of the object, or for a class the class
   itself (classmethods) unless the starting class is one of its metaclasses (metaclass methods) */
static Class *super_type(Super *s)
{
    if (s->obj.t != T_CLASS) return class_of(s->obj);
    if (AS_CLASS(s->obj)->meta && !class_isa(AS_CLASS(s->obj), s->start) &&
        class_isa(AS_CLASS(s->obj)->meta, s->start))
        return AS_CLASS(s->obj)->meta;
    return AS_CLASS(s->obj);
}
static Value get_attr(Value o, Str *nm)
{
    Value *p;
    switch (o.t) {
    case T_INST: {
        Inst *in = AS_INST(o);
        p = amap_get(&in->a, nm);
        if (p) return *p;
        p = class_find(in->cls, nm);
        if (p) return bind_class_attr(o, p, in->cls);
        if (nm == S_class) return mkobj(in->cls, T_CLASS);
        if (nm == S_dict) return inst_dict_get(in);
        if ((in->cls->flags & CF_EXC) && (nm == S_cause || nm == S_context)) return NONE_V;
        if ((in->cls->flags & CF_EXC) && nm == S_suppress) return FALSE_V;
        if ((in->cls->flags & CF_EXC) ? nm == S_traceback : in->cls == cls_frame) {
            Value r;
            if (rt_inst_attr(in, nm, &r)) return r;
        }
        p = class_find(in->cls, S_getattr);
        if (p) return call2(*p, o, mkobj(nm, T_STR));
        break; }
    case T_CLASS: {
        Class *c = AS_CLASS(o);
        p = class_find(c, nm);
        if (p) {
            if (p->t == T_CMETH) return new_bound(o, ((Prop *)p->u.o)->get);
            if (p->t == T_SMETH) return ((Prop *)p->u.o)->get;
            if (p->t == T_FUNC && (nm == S_init_subclass || nm == S_class_getitem))
                return new_bound(o, *p);   /* implicit classmethods */
            return *p;
        }
        if (c->meta) {   /* attributes (methods, properties) of the metaclass */
            p = class_find(c->meta, nm);
            if (p) return bind_class_attr(o, p, c->meta);
        }
        if (nm == S_name) return mkobj(c->name, T_STR);
        if (nm == S_dict) {
            Dict *d = dict_new();
            int k;
            Value dv = mkobj(d, T_DICT);
            VPUSH(dv);
            for (k = 0; k < c->ns.n; k++) dict_set(d, mkobj(c->ns.k[k], T_STR), c->ns.v[k]);
            vsp--;
            return dv;
        }
        if (nm == intern_c("__qualname__")) return mkobj(c->name, T_STR);
        if (nm == S_module) return c->modname ? mkobj(c->modname, T_STR) : mkcstr("builtins");
        if (nm == S_mro) return class_mro_tuple(c);
        if (nm == S_doc) return NONE_V;
        if (nm == S_class) return mkobj(c->meta ? c->meta : cls_type, T_CLASS);
        if (nm == intern_c("__bases__")) {
            Value bv[1];
            int k;
            List *bt;
            if (c->nbases > 0) {
                bt = tuple_new(c->nbases);
                for (k = 0; k < c->nbases; k++) bt->a[k] = mkobj(c->bases[k], T_CLASS);
                return mkobj(bt, T_TUPLE);
            }
            if (!c->base) return mktuple_from(bv, 0);
            bv[0] = mkobj(c->base, T_CLASS);
            return mktuple_from(bv, 1);
        }
        break; }
    case T_MODULE: {
        Module *md = (Module *)o.u.o;
        if (md->gp) {
            long gi = module_slot(md, nm, 0);
            if (gi >= 0 && G[gi].t != T_UNSET) return G[gi];
        } else {
            p = amap_get(&md->ns, nm);
            if (p) return *p;
            { Value ov; if (module_overlay(md, nm, &ov)) return ov; }
        }
        if (nm == S_name) return mkobj(md->name, T_STR);
        break; }
    case T_SLICE: {
        List *sl = AS_LIST(o);
        if (nm == intern_c("start")) return sl->a[0];
        if (nm == intern_c("stop")) return sl->a[1];
        if (nm == intern_c("step")) return sl->a[2];
        break; }
    case T_SUPER: {
        Super *s = (Super *)o.u.o;
        p = class_find_after(super_type(s), s->start, nm);
        if (p) {
            if (s->obj.t == T_CLASS &&
                (AS_CLASS(s->obj)->meta == NULL || class_isa(AS_CLASS(s->obj), s->start))) {
                if (p->t == T_CMETH) return new_bound(s->obj, ((Prop *)p->u.o)->get);
                if (p->t == T_SMETH) return ((Prop *)p->u.o)->get;
                if (p->t == T_FUNC && (nm == S_init_subclass || nm == S_class_getitem))
                    return new_bound(s->obj, *p);
                return *p;
            }
            return bind_class_attr(s->obj, p, class_of(s->obj));
        }
        if (nm == S_eq) return new_bound(s->obj, obj_eq_v);
        if (nm == S_ne) return new_bound(s->obj, obj_ne_v);
        if (nm == S_hash) return new_bound(s->obj, obj_hash_v);
        break; }
    case T_FUNC:
        p = amap_get(&AS_FUNC(o)->attrs, nm);
        if (p) return *p;
        { Value r; if (func_special_attr(o, nm, &r)) return r; }
        if (nm == intern_c("__annotations__")) {
            Value av = mkobj(dict_new(), T_DICT);
            amap_set(&AS_FUNC(o)->attrs, nm, av);
            return av;
        }
        if (nm == S_dict) {
            Dict *d = dict_new();
            int k;
            Value dv = mkobj(d, T_DICT);
            VPUSH(dv);
            for (k = 0; k < AS_FUNC(o)->attrs.n; k++) {
                if (AS_FUNC(o)->attrs.k[k] == intern_c("__annotations__")) continue;
                if (AS_FUNC(o)->attrs.k[k] == intern_c("__code__")) continue;
                dict_set(d, mkobj(AS_FUNC(o)->attrs.k[k], T_STR), AS_FUNC(o)->attrs.v[k]);
            }
            vsp--;
            return dv;
        }
        if (nm == S_name || nm == intern_c("__qualname__")) return mkobj(AS_FUNC(o)->name, T_STR);
        if (nm == S_doc) return NONE_V;
        if (nm == S_module) {
            Str *mn = modname_of_gp(AS_FUNC(o)->def->fd->gpfx);
            return mn ? mkobj(mn, T_STR) : mkcstr("builtins");
        }
        break;
    case T_BUILTIN:
        if (nm == S_name) return mkobj(((Builtin *)o.u.o)->sname, T_STR);
        break;
    case T_BOUND:
        if (nm == intern_c("__self__")) return ((Bound *)o.u.o)->self;
        if (nm == intern_c("__func__")) return ((Bound *)o.u.o)->fn;
        return get_attr(((Bound *)o.u.o)->fn, nm);
    case T_PROP: {
        if (nm == S_setter) {
            return new_bound(o, propsetter_fn);
        }
        if (nm == intern_c("deleter")) return new_bound(o, propdeleter_fn);
        if (nm == intern_c("getter")) return new_bound(o, propgetter_fn);
        if (nm == intern_c("fget")) return ((Prop *)o.u.o)
            ->get.t == T_UNSET ? NONE_V : ((Prop *)o.u.o)->get;
        if (nm == intern_c("fset")) return ((Prop *)o.u.o)
            ->set.t == T_UNSET ? NONE_V : ((Prop *)o.u.o)->set;
        if (nm == intern_c("fdel")) return ((Prop *)o.u.o)
            ->del.t == T_UNSET ? NONE_V : ((Prop *)o.u.o)->del;
        break; }
    case T_ND: {
        Value r;
        if (nd_getattr(o, nm, &r)) return r;
        break; }
    case T_FILE: {
        Value r;
        if (file_attr((File *)o.u.o, nm, &r)) return r;
        break; }
    default: break;
    }
    if (nm == S_class) return mkobj(class_of(o), T_CLASS);
    if (o.t == T_RANGE) {
        Range *r = (Range *)o.u.o;
        if (nm == intern_c("start")) return r->a;
        if (nm == intern_c("stop")) return r->b;
        if (nm == intern_c("step")) return r->c;
    }
    if (o.t == T_COMPLEX) {
        if (nm == intern_c("real")) return mkfloat(AS_CPLX(o)->re);
        if (nm == intern_c("imag")) return mkfloat(AS_CPLX(o)->im);
    }
    if (o.t == T_INT || o.t == T_BOOL || o.t == T_BIG || o.t == T_FLOAT) {
        const char *a = nm->s;
        if (!strcmp(a, "real") || (o.t != T_FLOAT && !strcmp(a, "numerator")))
            return o.t == T_BOOL ? mkint(o.u.i) : o;
        if (!strcmp(a, "imag")) return o.t == T_FLOAT ? mkfloat(0.0) : mkint(0);
        if (o.t != T_FLOAT && !strcmp(a, "denominator")) return mkint(1);
    }
    {
        Class *c = class_of(o);
        p = class_find(c, nm);
        if (p && o.t != T_INST && o.t != T_CLASS && o.t != T_MODULE)
            return bind_class_attr(o, p, c);
        if (nm == S_name && o.t != T_INST) {
            /* fallthrough to error */
        }
    }
    if (o.t == T_INST
        && ((nm == intern_c("value") && class_isa(AS_INST(o)->cls, cls_StopIteration)) ||
                          (nm == intern_c("code") && class_isa(AS_INST(o)->cls, cls_SystemExit)))) {
        Value *av = amap_get(&AS_INST(o)->a, S_args);
        if (av && av->t == T_TUPLE && AS_LIST(*av)->n > 0) return AS_LIST(*av)->a[0];
        return NONE_V;
    }
    attr_error(o, nm);
    return NONE_V;
}

/* method lookup for calls: returns 1 if *out is a function needing *o as first argument */
static int lookup_method(Value *o, Str *nm, Value *out, Node *cache)
{
    Value *p;
    switch (o->t) {
    case T_INST: {
        Inst *in = AS_INST(*o);
        p = amap_get(&in->a, nm);
        if (p) { *out = *p; return 0; }
        if (cache->ccls == in->cls && cache->cepoch == class_epoch) {
            p = &cache->cval;
        } else {
            p = class_find(in->cls, nm);
            if (p && (p->t == T_FUNC || p->t == T_BUILTIN)) {
                cache->ccls = in->cls; cache->cepoch = class_epoch; cache->cval = *p;
            }
        }
        if (p && (p->t == T_FUNC || p->t == T_BUILTIN)) { *out = *p; return 1; }
        break; }
    case T_SUPER: {
        Super *s = (Super *)o->u.o;
        p = class_find_after(super_type(s), s->start, nm);
        if (p && (p->t == T_FUNC || p->t == T_BUILTIN) && s->obj.t != T_CLASS) {
            *out = *p;
            *o = s->obj;
            return 1;
        }
        break; }
    case T_CLASS: case T_MODULE: case T_BOUND: case T_FUNC: case T_BUILTIN: case T_PROP:
        break;
    default: {
        Class *c = class_of(*o);
        if (cache->ccls == c && cache->cepoch == class_epoch) {
            p = &cache->cval;
        } else {
            p = class_find(c, nm);
            if (p && p->t == T_BUILTIN) {
                cache->ccls = c; cache->cepoch = class_epoch; cache->cval = *p;
            }
        }
        if (p && p->t == T_BUILTIN) { *out = *p; return 1; }
        break; }
    }
    *out = get_attr(*o, nm);
    return 0;
}

static void set_attr(Value o, Str *nm, Value v)
{
    switch (o.t) {
    case T_INST: {
        Inst *in = AS_INST(o);
        if (nm == S_dict) { inst_dict_assign(in, v); return; }
        if (in->cls->flags & CF_SETATTR) {
            Value *sa = class_find(in->cls, S_setattr);
            if (sa && sa->t == T_FUNC) {
                Value *base = vsp;
                VPUSH(*sa); VPUSH(o); VPUSH(mkobj(nm, T_STR)); VPUSH(v);
                call_value(*sa, 3, base + 1, 0, NULL);
                vsp = base;
                return;
            }
        }
        if (in->cls->flags & CF_PROPS) {
            Value *p = class_find(in->cls, nm);
            if (p && p->t == T_PROP) {
                Prop *pr = (Prop *)p->u.o;
                if (pr->set.t == T_UNSET)
                    throw_new(cls_AttributeError, "can't set attribute '%.60s'", nm->s);
                call2(pr->set, o, v);
                return;
            }
        }
        amap_set(&in->a, nm, v);
        if (in->dct) {
            in->dct->owner = NULL;
            dict_set(in->dct, mkobj(nm, T_STR), v);
            in->dct->owner = in;
        }
        return; }
    case T_CLASS:
        amap_set(&AS_CLASS(o)->ns, nm, v);
        if (v.t == T_PROP) AS_CLASS(o)->flags |= CF_PROPS;
        class_epoch++;
        return;
    case T_FUNC:
        amap_set(&AS_FUNC(o)->attrs, nm, v);
        return;
    case T_MODULE: {
        Module *md = (Module *)o.u.o;
        if (md->gp) { long gs = module_slot(md, nm, 1); G[gs] = v; }
        else amap_set(&md->ns, nm, v);
        return; }
    default: break;
    }
    throw_new(cls_AttributeError,
        "'%.60s' object has no attribute '%.60s' and no __dict__ for setting new attributes",
        type_name(o), nm->s);
}
static void del_attr(Value o, Str *nm)
{
    if (o.t == T_INST && (AS_INST(o)->cls->flags & CF_PROPS)) {
        Value *p = class_find(AS_INST(o)->cls, nm);
        if (p && p->t == T_PROP) {
            Prop *pr = (Prop *)p->u.o;
            if (pr->del.t == T_UNSET)
                throw_new(cls_AttributeError, "can't delete attribute '%.60s'", nm->s);
            call1(pr->del, o);
            return;
        }
    }
    if (o.t == T_INST && amap_del(&AS_INST(o)->a, nm)) {
        Inst *in = AS_INST(o);
        if (in->dct) {
            in->dct->owner = NULL;
            dict_del(in->dct, mkobj(nm, T_STR));
            in->dct->owner = in;
        }
        return;
    }
    if (o.t == T_FUNC && amap_del(&AS_FUNC(o)->attrs, nm)) return;
    if (o.t == T_CLASS && amap_del(&AS_CLASS(o)->ns, nm)) { class_epoch++; return; }
    attr_error(o, nm);
}

/* ---- slices and indexing ---------------------------------------------- */
static long slice_part(Value v)
{
    if (v.t == T_INT || v.t == T_BOOL) return v.u.i;
    if (v.t == T_BIG) return AS_BIG(v)->sign < 0 ? -SI_MAX : SI_MAX;
    throw_new(cls_TypeError, "slice indices must be integers or None or have an __index__ method");
    return 0;
}
static void slice_adjust(Value lo, Value hi, Value st, long len, long *start, long *stop,
                         long *step, long *cnt)
{
    long s = 1, lower, upper, a, b;
    if (st.t != T_NONE) {
        s = slice_part(st);
        if (s == 0) throw_new(cls_ValueError, "slice step cannot be zero");
    }
    if (s < 0) { lower = -1; upper = len - 1; } else { lower = 0; upper = len; }
    if (lo.t == T_NONE) a = s < 0 ? upper : lower;
    else {
        a = slice_part(lo);
        if (a < 0) { a += len; if (a < lower) a = lower; }
        else if (a > upper) a = upper;
    }
    if (hi.t == T_NONE) b = s < 0 ? lower : upper;
    else {
        b = slice_part(hi);
        if (b < 0) { b += len; if (b < lower) b = lower; }
        else if (b > upper) b = upper;
    }
    *start = a; *stop = b; *step = s;
    if (s < 0) *cnt = a > b ? (a - b - 1) / (-s) + 1 : 0;
    else *cnt = a < b ? (b - a - 1) / s + 1 : 0;
}
/* slice_adjust for lengths and bounds of any size (ranges); start/stop/step as integers */
static int slice_isint(Value v)
{
    return v.t == T_INT || v.t == T_BOOL || v.t == T_BIG;
}
static void slice_adjust_v(Value lo, Value hi, Value st, Value len, Value *start, Value *stop,
                           Value *step)
{
    Value s = mkint(1), lower, upper, a, b;
    if (st.t != T_NONE) {
        if (!slice_isint(st))
            throw_new(cls_TypeError,
                      "slice indices must be integers or None or have an __index__ method");
        s = st;
        if (int_sign(s) == 0) throw_new(cls_ValueError, "slice step cannot be zero");
    }
    if (int_sign(s) < 0) { lower = mkint(-1); upper = int_sub(len, mkint(1)); }
    else { lower = mkint(0); upper = len; }
    if (lo.t == T_NONE) a = int_sign(s) < 0 ? upper : lower;
    else {
        if (!slice_isint(lo))
            throw_new(cls_TypeError,
                      "slice indices must be integers or None or have an __index__ method");
        a = lo;
        if (int_sign(a) < 0) { a = int_add(a, len); if (int_cmp(a, lower) < 0) a = lower; }
        else if (int_cmp(a, upper) > 0) a = upper;
    }
    if (hi.t == T_NONE) b = int_sign(s) < 0 ? lower : upper;
    else {
        if (!slice_isint(hi))
            throw_new(cls_TypeError,
                      "slice indices must be integers or None or have an __index__ method");
        b = hi;
        if (int_sign(b) < 0) { b = int_add(b, len); if (int_cmp(b, lower) < 0) b = lower; }
        else if (int_cmp(b, upper) > 0) b = upper;
    }
    *start = a; *stop = b; *step = s;
}
static Value slice_get(Value o, Value lo, Value hi, Value st)
{
    long start, stop, step, cnt, i, len;
    switch (o.t) {
    case T_LIST: case T_TUPLE: {
        List *l = AS_LIST(o), *r;
        len = l->n;
        slice_adjust(lo, hi, st, len, &start, &stop, &step, &cnt);
        if (o.t == T_LIST) r = list_new(cnt); else r = tuple_new(cnt);
        if (o.t == T_LIST) {
            for (i = 0; i < cnt; i++) r->a[i] = l->a[start + i * step];
            r->n = cnt;
        } else {
            for (i = 0; i < cnt; i++) r->a[i] = l->a[start + i * step];
        }
        return mkobj(r, o.t); }
    case T_STR: {
        Str *s = AS_STR(o);
        Buf b;
        slice_adjust(lo, hi, st, s->len, &start, &stop, &step, &cnt);
        if (step == 1) return mkstr(s->s + start, cnt);
        buf_init(&b);
        for (i = 0; i < cnt; i++) buf_addc(&b, s->s[start + i * step]);
        return buf_value(&b); }
    case T_BYTES: {
        Str *s = AS_STR(o);
        Buf b;
        slice_adjust(lo, hi, st, s->len, &start, &stop, &step, &cnt);
        buf_init(&b);
        for (i = 0; i < cnt; i++) buf_addc(&b, s->s[start + i * step]);
        { Value r = mkbytes(b.s, b.n); buf_free(&b); return r; } }
    case T_BARR: {
        BArr *s = AS_BARR(o);
        Buf b;
        slice_adjust(lo, hi, st, s->n, &start, &stop, &step, &cnt);
        buf_init(&b);
        for (i = 0; i < cnt; i++) buf_addc(&b, s->s[start + i * step]);
        { Value r = mkbarr(b.s, b.n); buf_free(&b); return r; } }
    case T_ND: return nd_subscr(o, mkslice(lo, hi, st));
    case T_RANGE: {
        Range *r = (Range *)o.u.o;
        Value gs, ge, gt;
        slice_adjust_v(lo, hi, st, range_lenv(r), &gs, &ge, &gt);
        return make_range_v(range_item(r, gs), range_item(r, ge), int_mul(r->c, gt)); }
    case T_INST: {
        Value *m = class_find(AS_INST(o)->cls, S_getitem);
        if (m) {
            Value *base = vsp;
            Value sl, r;
            sl = mkslice(lo, hi, st);
            VPUSH(*m); VPUSH(o); VPUSH(sl);
            r = call_value(*m, 2, base + 1, 0, NULL);
            vsp = base;
            return r;
        }
        break; }
    default: break;
    }
    throw_new(cls_TypeError, "'%.60s' object is not subscriptable", type_name(o));
    return NONE_V;
}
static long norm_index(long i, long n, const char *what)
{
    if (i < 0) i += n;
    if (i < 0 || i >= n) throw_new(cls_IndexError, "%s index out of range", what);
    return i;
}
static void slice_del(Value o, Value lo, Value hi, Value st);
static Value subscr(Value o, Value idx)
{
    if (idx.t == T_BIG && (o.t == T_LIST || o.t == T_TUPLE || o.t == T_STR || o.t == T_BYTES ||
                           o.t == T_BARR))
        throw_new(cls_IndexError, "cannot fit 'int' into an index-sized integer");
    if (idx.t == T_SLICE && o.t != T_INST && o.t != T_ND && o.t != T_DICT) {
        List *sl = AS_LIST(idx);
        return slice_get(o, sl->a[0], sl->a[1], sl->a[2]);
    }
    switch (o.t) {
    case T_LIST: case T_TUPLE:
        if (idx.t == T_INT || idx.t == T_BOOL) {
            List *l = AS_LIST(o);
            long i = idx.u.i;
            if (i < 0) i += l->n;
            if (i < 0 || i >= l->n)
                throw_new(cls_IndexError, o.t == T_LIST ? "list index out of range" :
                          "tuple index out of range");
            return l->a[i];
        }
        throw_new(cls_TypeError, "%s indices must be integers or slices, not %.40s",
                  o.t == T_LIST ? "list" : "tuple", type_name(idx));
        break;
    case T_STR:
        if (idx.t == T_INT || idx.t == T_BOOL) {
            Str *s = AS_STR(o);
            long i = norm_index(idx.u.i, s->len, "string");
            return mkstr(s->s + i, 1);
        }
        throw_new(cls_TypeError, "string indices must be integers, not '%.40s'", type_name(idx));
        break;
    case T_DICT: {
        Value r;
        if (dict_get(AS_DICT(o), idx, &r)) return r;
        throw_key(idx);
        break; }
    case T_BYTES:
        if (idx.t == T_INT || idx.t == T_BOOL) {
            Str *s = AS_STR(o);
            long i = idx.u.i;
            if (i < 0) i += s->len;
            if (i < 0 || i >= s->len) throw_new(cls_IndexError, "index out of range");
            return mkint((unsigned char)s->s[i]);
        }
        throw_new(cls_TypeError, "byte indices must be integers or slices, not %.40s",
            type_name(idx));
        break;
    case T_BARR:
        if (idx.t == T_INT || idx.t == T_BOOL) {
            BArr *s = AS_BARR(o);
            long i = idx.u.i;
            if (i < 0) i += s->n;
            if (i < 0 || i >= s->n) throw_new(cls_IndexError, "index out of range");
            return mkint((unsigned char)s->s[i]);
        }
        throw_new(cls_TypeError, "bytearray indices must be integers or slices, not %.40s",
            type_name(idx));
        break;
    case T_ND: return nd_subscr(o, idx);
    case T_RANGE:
        if (idx.t == T_INT || idx.t == T_BOOL || idx.t == T_BIG) {
            Range *r = (Range *)o.u.o;
            Value i = idx, len = range_lenv(r);
            if (int_sign(i) < 0) i = int_add(i, len);
            if (int_sign(i) < 0 || int_cmp(i, len) >= 0)
                throw_new(cls_IndexError, "range object index out of range");
            return range_item(r, i);
        }
        throw_new(cls_TypeError, "range indices must be integers or slices, not %.40s",
            type_name(idx));
        break;
    case T_CLASS: {
        Class *c = AS_CLASS(o);
        Value *m = class_find(c, S_class_getitem);
        if (m) return call2(m->t == T_CMETH ? ((Prop *)m->u.o)->get : *m, o, idx);
        m = c->meta ? class_find(c->meta, S_getitem) : NULL;
        if (m) return call2(*m, o, idx);
        if (c == cls_list || c == cls_dict || c == cls_set || c == cls_frozenset ||
            c == cls_tuple || c == cls_type)
            return generic_alias(o, idx);
        throw_new(cls_TypeError, "type '%.60s' is not subscriptable", c->name->s);
        break; }
    case T_INST: {
        Value *m = class_find(AS_INST(o)->cls, S_getitem);
        if (m && m->t == T_BUILTIN && AS_INST(o)->pay.t == T_DICT) {
            Value r, *mm;
            if (dict_get(AS_DICT(AS_INST(o)->pay), idx, &r)) return r;
            mm = class_find(AS_INST(o)->cls, intern_c("__missing__"));
            if (mm) return call2(*mm, o, idx);
            throw_key(idx);
        }
        if (m) return call2(*m, o, idx);
        break; }
    default: break;
    }
    throw_new(cls_TypeError, "'%.60s' object is not subscriptable", type_name(o));
    return NONE_V;
}
static Value iter_to_list(Value it);
static void list_splice(List *l, long start, long stop, Value *items, long ni)
{
    long old = stop - start, i, newn = l->n - old + ni;
    if (newn > l->cap) {
        l->cap = newn + 4;
        l->a = (Value *)xrealloc(l->a, sizeof(Value) * (size_t)l->cap);
    }
    if (ni != old) memmove(l->a + start + ni, l->a + stop, sizeof(Value) * (size_t)(l->n - stop));
    for (i = 0; i < ni; i++) l->a[start + i] = items[i];
    l->n = newn;
}
static void bytes_slice_assign(BArr *a, long start, long stop, long step, long cnt,
                               const char *src, long sn)
{
    long i;
    if (step == 1) {
        long old, newn;
        if (stop < start) stop = start;
        old = stop - start;
        newn = a->n - old + sn;
        barr_reserve(a, newn);
        memmove(a->s + start + sn, a->s + stop, (size_t)(a->n - stop));
        if (sn > 0) memcpy(a->s + start, src, (size_t)sn);
        a->n = newn;
        return;
    }
    if (sn != cnt)
        throw_new(cls_ValueError,
            "attempt to assign bytes of size %ld to extended slice of size %ld", sn, cnt);
    for (i = 0; i < cnt; i++) a->s[start + i * step] = src[i];
}
static void slice_set(Value o, Value lo, Value hi, Value st, Value v)
{
    long start, stop, step, cnt, i;
    if (o.t == T_LIST) {
        List *l = AS_LIST(o), *src;
        Value lv = iter_to_list(v);
        VPUSH(lv);
        src = AS_LIST(lv);
        slice_adjust(lo, hi, st, l->n, &start, &stop, &step, &cnt);
        if (step == 1) {
            if (stop < start) stop = start;
            list_splice(l, start, stop, src->a, src->n);
        } else {
            if (src->n != cnt)
                throw_new(cls_ValueError,
                    "attempt to assign sequence of size %ld to extended slice of size %ld", src->n,
                    cnt);
            for (i = 0; i < cnt; i++) l->a[start + i * step] = src->a[i];
        }
        vsp--;
        return;
    }
    if (o.t == T_BARR) {
        BArr *a = AS_BARR(o);
        Buf nb;
        const char *sp;
        long sn;
        slice_adjust(lo, hi, st, a->n, &start, &stop, &step, &cnt);
        if (!bytes_get(v, &sp, &sn)) {
            Value lv = iter_to_list(v);
            VPUSH(lv);
            buf_init(&nb);
            for (i = 0; i < AS_LIST(lv)->n; i++) {
                Value y = AS_LIST(lv)->a[i];
                if (y.t != T_INT || y.u.i < 0 || y.u.i > 255) {
                    buf_free(&nb);
                    throw_new(cls_ValueError, "byte must be in range(0, 256)");
                }
                buf_addc(&nb, (int)y.u.i);
            }
            vsp--;
            bytes_slice_assign(a, start, stop, step, cnt, nb.s, nb.n);
            buf_free(&nb);
            return;
        }
        buf_init(&nb);
        buf_addn(&nb, sp, sn);
        bytes_slice_assign(a, start, stop, step, cnt, nb.s, nb.n);
        buf_free(&nb);
        return;
    }
    if (o.t == T_ND) { nd_setitem(o, mkslice(lo, hi, st), v); return; }
    if (o.t == T_INST) {
        Value *m = class_find(AS_INST(o)->cls, S_setitem);
        if (m) {
            Value *base = vsp;
            VPUSH(*m); VPUSH(o); VPUSH(mkslice(lo, hi, st)); VPUSH(v);
            call_value(*m, 3, base + 1, 0, NULL);
            vsp = base;
            return;
        }
    }
    throw_new(cls_TypeError, "'%.60s' object does not support slice assignment", type_name(o));
}
static void setitem(Value o, Value idx, Value v)
{
    if (idx.t == T_BIG && (o.t == T_LIST || o.t == T_TUPLE || o.t == T_STR || o.t == T_BYTES ||
                           o.t == T_BARR))
        throw_new(cls_IndexError, "cannot fit 'int' into an index-sized integer");
    if (idx.t == T_SLICE && o.t != T_INST && o.t != T_ND && o.t != T_DICT) {
        List *sl = AS_LIST(idx);
        slice_set(o, sl->a[0], sl->a[1], sl->a[2], v);
        return;
    }
    switch (o.t) {
    case T_LIST:
        if (idx.t == T_INT || idx.t == T_BOOL) {
            List *l = AS_LIST(o);
            long i = idx.u.i;
            if (i < 0) i += l->n;
            if (i < 0 || i >= l->n) throw_new(cls_IndexError, "list assignment index out of range");
            l->a[i] = v;
            return;
        }
        throw_new(cls_TypeError, "list indices must be integers or slices, not %.40s",
            type_name(idx));
        break;
    case T_DICT: dict_set(AS_DICT(o), idx, v); return;
    case T_BARR:
        if (idx.t == T_INT || idx.t == T_BOOL) {
            BArr *a = AS_BARR(o);
            long i = idx.u.i;
            if (i < 0) i += a->n;
            if (i < 0 || i >= a->n)
                throw_new(cls_IndexError, "bytearray assignment index out of range");
            if (v.t != T_INT && v.t != T_BOOL)
                throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
                    type_name(v));
            if (v.u.i < 0 || v.u.i > 255)
                throw_new(cls_ValueError, "byte must be in range(0, 256)");
            a->s[i] = (char)v.u.i;
            return;
        }
        throw_new(cls_TypeError, "bytearray indices must be integers or slices, not %.40s",
            type_name(idx));
        break;
    case T_ND: nd_setitem(o, idx, v); return;
    case T_INST: {
        Value *m = class_find(AS_INST(o)->cls, S_setitem);
        if (m) {
            Value *base = vsp;
            VPUSH(*m); VPUSH(o); VPUSH(idx); VPUSH(v);
            call_value(*m, 3, base + 1, 0, NULL);
            vsp = base;
            return;
        }
        break; }
    default: break;
    }
    throw_new(cls_TypeError, "'%.60s' object does not support item assignment", type_name(o));
}
static void delitem(Value o, Value idx)
{
    if (idx.t == T_BIG && (o.t == T_LIST || o.t == T_TUPLE || o.t == T_STR || o.t == T_BYTES ||
                           o.t == T_BARR))
        throw_new(cls_IndexError, "cannot fit 'int' into an index-sized integer");
    if (idx.t == T_SLICE && o.t != T_INST && o.t != T_DICT) {
        List *sl = AS_LIST(idx);
        slice_del(o, sl->a[0], sl->a[1], sl->a[2]);
        return;
    }
    switch (o.t) {
    case T_LIST:
        if (idx.t == T_INT || idx.t == T_BOOL) {
            List *l = AS_LIST(o);
            long i = idx.u.i;
            if (i < 0) i += l->n;
            if (i < 0 || i >= l->n) throw_new(cls_IndexError, "list assignment index out of range");
            list_splice(l, i, i + 1, NULL, 0);
            return;
        }
        break;
    case T_DICT:
        if (!dict_del(AS_DICT(o), idx)) throw_key(idx);
        return;
    case T_BARR:
        if (idx.t == T_INT || idx.t == T_BOOL) {
            BArr *a = AS_BARR(o);
            long i = idx.u.i;
            if (i < 0) i += a->n;
            if (i < 0 || i >= a->n)
                throw_new(cls_IndexError, "bytearray index out of range");
            memmove(a->s + i, a->s + i + 1, (size_t)(a->n - i - 1));
            a->n--;
            return;
        }
        break;
    case T_SET:
        break;
    case T_INST: {
        Value *m = class_find(AS_INST(o)->cls, S_delitem);
        if (m) { call2(*m, o, idx); return; }
        break; }
    default: break;
    }
    throw_new(cls_TypeError, "'%.60s' object doesn't support item deletion", type_name(o));
}
static void slice_del(Value o, Value lo, Value hi, Value st)
{
    long start, stop, step, cnt, i;
    if (o.t == T_LIST) {
        List *l = AS_LIST(o);
        slice_adjust(lo, hi, st, l->n, &start, &stop, &step, &cnt);
        if (step == 1) { if (stop > start) list_splice(l, start, stop, NULL, 0); return; }
        if (step < 0) { start = start + (cnt - 1) * step; step = -step; }
        for (i = cnt - 1; i >= 0; i--)
            list_splice(l, start + i * step, start + i * step + 1, NULL, 0);
        return;
    }
    if (o.t == T_BARR) {
        BArr *a = AS_BARR(o);
        slice_adjust(lo, hi, st, a->n, &start, &stop, &step, &cnt);
        if (step == 1) {
            if (stop > start) bytes_slice_assign(a, start, stop, 1, cnt, NULL, 0);
            return;
        }
        if (step < 0) { start = start + (cnt - 1) * step; step = -step; }
        for (i = cnt - 1; i >= 0; i--) {
            long k = start + i * step;
            memmove(a->s + k, a->s + k + 1, (size_t)(a->n - k - 1));
            a->n--;
        }
        return;
    }
    if (o.t == T_INST) {
        Value *m = class_find(AS_INST(o)->cls, S_delitem);
        if (m) { call2(*m, o, mkslice(lo, hi, st)); return; }
    }
    throw_new(cls_TypeError, "'%.60s' object doesn't support slice deletion", type_name(o));
}

/* ---- iteration -------------------------------------------------------- */
static Iter *iter_new(int kind)
{
    Iter *it = (Iter *)gc_alloc(sizeof(Iter), T_ITER);
    it->kind = kind;
    it->a = it->b = it->c = NONE_V;
    it->i = it->n = 0;
    it->step = 1;
    return it;
}
static Value get_iter(Value o)
{
    Iter *it;
    switch (o.t) {
    case T_LIST: it = iter_new(IK_LIST); it->a = o; return mkobj(it, T_ITER);
    case T_TUPLE: it = iter_new(IK_TUPLE); it->a = o; return mkobj(it, T_ITER);
    case T_STR: it = iter_new(IK_STR); it->a = o; return mkobj(it, T_ITER);
    case T_BYTES: case T_BARR: it = iter_new(IK_BYTES); it->a = o; return mkobj(it, T_ITER);
    case T_DICT: it = iter_new(IK_DICT); it->a = o; return mkobj(it, T_ITER);
    case T_SET: it = iter_new(IK_SET); it->a = o; it->n = AS_DICT(o)->n; return mkobj(it, T_ITER);
    case T_RANGE: {
        Range *r = (Range *)o.u.o;
        if (r->big) {
            it = iter_new(IK_BRANGE);
            it->a = r->a; it->b = r->b; it->c = r->c;
            return mkobj(it, T_ITER);
        }
        it = iter_new(IK_RANGE);
        it->i = r->a.u.i; it->n = r->b.u.i; it->step = r->c.u.i;
        return mkobj(it, T_ITER); }
    case T_DVIEW: {
        DView *dv = (DView *)o.u.o;
        it = iter_new(dv->what == 0 ? IK_DICT : (dv->what == 1 ? IK_DVALS : IK_DITEMS));
        it->a = dv->d;
        return mkobj(it, T_ITER); }
    case T_ITER: case T_GEN: return o;
    case T_FILE: it = iter_new(IK_FILE); it->a = o; return mkobj(it, T_ITER);
    case T_ND:
        nd_len(o);
        it = iter_new(IK_ND); it->a = o; return mkobj(it, T_ITER);
    case T_CLASS: {
        Value *m = AS_CLASS(o)->meta ? class_find(AS_CLASS(o)->meta, S_iter) : NULL;
        if (m) return call1(*m, o);
        break; }
    case T_INST: {
        Value *m = class_find(AS_INST(o)->cls, S_iter);
        if (m) return call1(*m, o);
        m = class_find(AS_INST(o)->cls, S_getitem);
        if (m) {
            it = iter_new(IK_CALL); it->a = o; it->i = 0;
            return mkobj(it, T_ITER);
        }
        break; }
    default: break;
    }
    throw_new(cls_TypeError, "'%.60s' object is not iterable", type_name(o));
    return NONE_V;
}
static int gen_resume(Gen *g, Value *out);
static int protected_call(Value fn, int n, Value *a, Value *res, Value *exc);
static Value file_readline(File *f);
static int iter_next(Value it, Value *out)
{
    Iter *t;
    if (it.t == T_GEN) return gen_resume((Gen *)it.u.o, out);
    if (it.t == T_INST) {
        Value *m = class_find(AS_INST(it)->cls, S_next);
        Value res, exc;
        if (!m)
            throw_new(cls_TypeError, "iter() returned non-iterator of type '%.40s'", type_name(it));
        if (protected_call(*m, 1, &it, &res, &exc)) {
            if (class_isa(AS_INST(exc)->cls, cls_StopIteration)) return 0;
            throw_val(exc);
        }
        *out = res;
        return 1;
    }
    if (it.t != T_ITER) {
        throw_new(cls_TypeError, "'%.60s' object is not an iterator", type_name(it));
    }
    t = (Iter *)it.u.o;
    switch (t->kind) {
    case IK_LIST: case IK_TUPLE: {
        List *l = AS_LIST(t->a);
        if (t->i >= l->n) return 0;
        *out = l->a[t->i++];
        return 1; }
    case IK_STR: {
        Str *s = AS_STR(t->a);
        if (t->i >= s->len) return 0;
        *out = mkstr(s->s + t->i, 1);
        t->i++;
        return 1; }
    case IK_BYTES: {
        const char *bp = "";
        long bn = 0;
        bytes_get(t->a, &bp, &bn);
        if (t->i >= bn) return 0;
        *out = mkint((unsigned char)bp[t->i++]);
        return 1; }
    case IK_CALLSENT: {
        Value r = call0(t->a);
        if (py_eq(r, t->b)) return 0;
        *out = r;
        return 1; }
    case IK_RANGE:
        if (t->step > 0 ? t->i >= t->n : t->i <= t->n) return 0;
        *out = mkint(t->i);
        t->i += t->step;
        return 1;
    case IK_BRANGE: {
        int c = int_cmp(t->a, t->b);
        if (int_sign(t->c) > 0 ? c >= 0 : c <= 0) return 0;
        *out = t->a;
        t->a = int_add(t->a, t->c);
        return 1; }
    case IK_SET: {
        Dict *d = AS_DICT(t->a);
        if (!t->step) return 0;                 /* exhausted */
        if (t->n != d->n)
            throw_new(cls_RuntimeError, "Set changed size during iteration");
        while (t->i < d->used && d->e[t->i].k.t == T_UNSET) t->i++;
        if (t->i >= d->used) { t->step = 0; return 0; }
        *out = d->e[t->i].k;
        t->i++;
        return 1; }
    case IK_DICT: case IK_DVALS: case IK_DITEMS: {
        Dict *d = AS_DICT(t->a);
        while (t->i < d->used && d->e[t->i].k.t == T_UNSET) t->i++;
        if (t->i >= d->used) return 0;
        if (t->kind == IK_DVALS) *out = d->e[t->i].v;
        else if (t->kind == IK_DITEMS) *out = mktuple2(d->e[t->i].k, d->e[t->i].v);
        else *out = d->e[t->i].k;
        t->i++;
        return 1; }
    case IK_ENUM: {
        Value x;
        if (!iter_next(t->a, &x)) return 0;
        {
            Value *base = vsp;
            VPUSH(x);
            *out = mktuple2(t->b.t == T_BIG ? t->b : mkint(t->i), x);
            vsp = base;
        }
        if (t->b.t == T_BIG) t->b = int_add(t->b, mkint(1));
        else t->i++;
        return 1; }
    case IK_ZIP: {
        List *its = AS_LIST(t->a);
        List *r;
        long k;
        Value rv;
        if (its->n == 0) return 0;
        r = tuple_new(its->n);
        rv = mkobj(r, T_TUPLE);
        for (k = 0; k < r->n; k++) r->a[k] = NONE_V;
        VPUSH(rv);
        for (k = 0; k < its->n; k++) {
            Value x;
            if (!iter_next(its->a[k], &x)) { vsp--; return 0; }
            r->a[k] = x;
        }
        vsp--;
        *out = rv;
        return 1; }
    case IK_MAP: {
        List *its = AS_LIST(t->b);
        Value *base = vsp;
        long k;
        VPUSH(t->a);
        for (k = 0; k < its->n; k++) {
            Value x;
            if (!iter_next(its->a[k], &x)) { vsp = base; return 0; }
            VPUSH(x);
        }
        *out = call_value(t->a, (int)its->n, base + 1, 0, NULL);
        vsp = base;
        return 1; }
    case IK_FILTER: {
        Value x;
        for (;;) {
            int ok;
            if (!iter_next(t->b, &x)) return 0;
            if (t->a.t == T_NONE) ok = py_truthy(x);
            else { Value *base = vsp; VPUSH(x); ok = py_truthy(call1(t->a, x)); vsp = base; }
            if (ok) { *out = x; return 1; }
        } }
    case IK_REV: {
        if (t->i < 0) return 0;
        if (t->a.t == T_STR) *out = mkstr(AS_STR(t->a)->s + t->i, 1);
        else if (t->a.t == T_RANGE) {
            Range *r = (Range *)t->a.u.o;
            *out = mkint(r->a.u.i + t->i * r->c.u.i);
        } else *out = AS_LIST(t->a)->a[t->i];
        t->i--;
        return 1; }
    case IK_ND:
        if (t->i >= nd_len(t->a)) return 0;
        *out = nd_item(t->a, t->i++);
        return 1;
    case IK_FILE: {
        File *f = (File *)t->a.u.o;
        Value ln = file_readline(f);
        if (AS_STR(ln)->len == 0) return 0;
        *out = ln;
        return 1; }
    case IK_CALL: {
        Value res, exc, args[2];
        Value *m = class_find(AS_INST(t->a)->cls, S_getitem);
        args[0] = t->a; args[1] = mkint(t->i);
        if (protected_call(*m, 2, args, &res, &exc)) {
            if (class_isa(AS_INST(exc)->cls, cls_IndexError) ||
                class_isa(AS_INST(exc)->cls, cls_StopIteration)) return 0;
            throw_val(exc);
        }
        t->i++;
        *out = res;
        return 1; }
    default: break;
    }
    return 0;
}
/* collect an iterable into a new list */
static Value iter_to_list(Value o)
{
    List *l;
    Value lv, it, x;
    if (o.t == T_LIST || o.t == T_TUPLE) {
        List *s = AS_LIST(o);
        long i;
        l = list_new(s->n);
        for (i = 0; i < s->n; i++) l->a[i] = s->a[i];
        l->n = s->n;
        return mklist(l);
    }
    l = list_new(8);
    lv = mklist(l);
    VPUSH(lv);
    VPUSH(o);
    it = get_iter(o);
    VPUSH(it);
    while (iter_next(it, &x)) list_push(l, x);
    vsp -= 3;
    return lv;
}

static int contains(Value c, Value x)
{
    switch (c.t) {
    case T_LIST: case T_TUPLE: {
        List *l = AS_LIST(c);
        long i;
        for (i = 0; i < l->n; i++) if (py_eq(l->a[i], x)) return 1;
        return 0; }
    case T_DICT: {
        Value o;
        return dict_get(AS_DICT(c), x, &o); }
    case T_SET: return set_has(AS_DICT(c), x);
    case T_STR: {
        Str *s = AS_STR(c), *u;
        long i;
        if (x.t != T_STR)
            throw_new(cls_TypeError, "'in <string>' requires string as left operand, not %.40s",
            type_name(x));
        u = AS_STR(x);
        if (u->len == 0) return 1;
        for (i = 0; i + u->len <= s->len; i++)
            if (s->s[i] == u->s[0] && memcmp(s->s + i, u->s, (size_t)u->len) == 0) return 1;
        return 0; }
    case T_BYTES: case T_BARR: {
        const char *sp, *up;
        long sn, un, i;
        bytes_get(c, &sp, &sn);
        if (x.t == T_INT) {
            if (x.u.i < 0 || x.u.i > 255)
                throw_new(cls_ValueError, "byte must be in range(0, 256)");
            return memchr(sp, (int)x.u.i, (size_t)sn) != NULL;
        }
        if (!bytes_get(x, &up, &un))
            throw_new(cls_TypeError, "a bytes-like object is required, not '%.40s'", type_name(x));
        if (un == 0) return 1;
        for (i = 0; i + un <= sn; i++)
            if (sp[i] == up[0] && memcmp(sp + i, up, (size_t)un) == 0) return 1;
        return 0; }
    case T_RANGE: {
        Range *r = (Range *)c.u.o;
        if (x.t == T_FLOAT) {
            if (x.u.d != floor(x.u.d) || x.u.d - x.u.d != 0.0) return 0;
            x = int_from_double(x.u.d);
        }
        if (x.t == T_INT || x.t == T_BOOL || x.t == T_BIG) {
            if (!r->big && x.t != T_BIG) {
                long v = x.u.i, start = r->a.u.i, stop = r->b.u.i, step = r->c.u.i;
                if (step > 0) return v >= start && v < stop && (v - start) % step == 0;
                return v <= start && v > stop && (start - v) % (-step) == 0;
            }
            if (int_sign(r->c) > 0) {
                if (int_cmp(x, r->a) < 0 || int_cmp(x, r->b) >= 0) return 0;
            } else if (int_cmp(x, r->a) > 0 || int_cmp(x, r->b) <= 0) return 0;
            return int_sign(int_mod(int_sub(x, r->a), r->c)) == 0;
        }
        return 0; }
    case T_DVIEW: {
        DView *dv = (DView *)c.u.o;
        Value o;
        if (dv->what == 0) return dict_get(AS_DICT(dv->d), x, &o);
        if (dv->what == 2) {
            if (x.t != T_TUPLE || AS_LIST(x)->n != 2) return 0;
            return dict_get(AS_DICT(dv->d), AS_LIST(x)->a[0], &o) && py_eq(o, AS_LIST(x)->a[1]);
        }
        break; }
    case T_INST: {
        Value *m = class_find(AS_INST(c)->cls, S_contains);
        if (m) return py_truthy(call2(*m, c, x));
        break; }
    case T_CLASS: {
        Value *m = AS_CLASS(c)->meta ? class_find(AS_CLASS(c)->meta, S_contains) : NULL;
        if (m) return py_truthy(call2(*m, c, x));
        if (AS_CLASS(c)->meta && class_find(AS_CLASS(c)->meta, S_iter)) break;
        throw_new(cls_TypeError, "argument of type 'type' is not iterable");
        break; }
    default: break;
    }
    {
        Value it, y;
        if (c.t != T_DVIEW && c.t != T_INST && c.t != T_CLASS && c.t != T_GEN &&
            c.t != T_ITER && c.t != T_ND)
            throw_new(cls_TypeError, "argument of type '%.60s' is not iterable", type_name(c));
        it = get_iter(c);
        VPUSH(it);
        while (iter_next(it, &y)) {
            if (py_eq(y, x)) { vsp--; return 1; }
        }
        vsp--;
        return 0;
    }
}

/* ---- arithmetic ------------------------------------------------------- */
static const char *op_str(int op)
{
    switch (op) {
    case B_ADD: return "+"; case B_SUB: return "-"; case B_MUL: return "*";
    case B_DIV: return "/"; case B_FDIV: return "//"; case B_MOD: return "%";
    case B_POW: return "** or pow()"; case B_AND: return "&"; case B_OR: return "|";
    case B_XOR: return "^"; case B_LSH: return "<<"; case B_MATMUL: return "@";
    default: return ">>";
    }
}
static Str *dunder(int op, int rev)
{
    static Str *tab[2][16];
    static const char *nm[] = { "", "add", "sub", "mul", "truediv", "floordiv", "mod", "pow",
        "and", "or", "xor", "lshift", "rshift", "matmul" };
    if (op < 1 || op > 13) return NULL;
    if (!tab[rev][op]) {
        char b[40];
        sprintf(b, "__%s%s__", rev ? "r" : "", nm[op]);
        tab[rev][op] = intern_c(b);
    }
    return tab[rev][op];
}
/* in-place dunder name (__iadd__ ...) */
static Str *idunder(int op)
{
    static Str *tab[16];
    static const char *nm[] = { "", "add", "sub", "mul", "truediv", "floordiv", "mod", "pow",
        "and", "or", "xor", "lshift", "rshift", "matmul" };
    if (op < 1 || op > 13) return NULL;
    if (!tab[op]) {
        char b[40];
        sprintf(b, "__i%s__", nm[op]);
        tab[op] = intern_c(b);
    }
    return tab[op];
}
static void dict_update_from(Dict *d, Value src);
#define dict_update_from_hook dict_update_from
static Value int_pow_v(Value a, Value b)
{
    if (int_sign(b) < 0) {
        double x = to_dbl(a), y = to_dbl(b);
        if (x == 0) throw_new(cls_ZeroDivisionError, "0.0 cannot be raised to a negative power");
        return mkfloat(pow(x, y));
    }
    if (b.t == T_BIG) {
        if (a.t != T_BIG && (a.u.i == 0 || a.u.i == 1)) return a;
        throw_new(cls_OverflowError, "exponent too large");
    }
    if (a.t != T_BIG && (a.u.i > 1 || a.u.i < -1) && b.u.i > 100000000L)
        throw_new(cls_OverflowError, "exponent too large");
    return int_pow(a, b.u.i);
}
static Value num_binop(int op, Value a, Value b)
{
    if (a.t == T_FLOAT || b.t == T_FLOAT) {
        double x = to_dbl(a), y = to_dbl(b);
        switch (op) {
        case B_ADD: return mkfloat(x + y);
        case B_SUB: return mkfloat(x - y);
        case B_MUL: return mkfloat(x * y);
        case B_DIV:
            if (y == 0) throw_new(cls_ZeroDivisionError, "float division by zero");
            return mkfloat(x / y);
        case B_FDIV:
            if (y == 0) throw_new(cls_ZeroDivisionError, "float floor division by zero");
            return mkfloat(fl_floordiv(x, y));
        case B_MOD:
            if (y == 0) throw_new(cls_ZeroDivisionError, "float modulo by zero");
            return mkfloat(fl_mod(x, y));
        case B_POW:
            if (x == 0 && y < 0)
                throw_new(cls_ZeroDivisionError, "0.0 cannot be raised to a negative power");
            if (x < 0 && y != floor(y))
                return cplx_binop(B_POW, mkcomplex(x, 0.0), mkcomplex(y, 0.0));
            {
                double r = pow(x, y);
                if ((r > DBL_MAX || r < -DBL_MAX) && x - x == 0 && y - y == 0)
                    throw_new(cls_OverflowError, "(34, 'Numerical result out of range')");
                return mkfloat(r);
            }
        default: break;
        }
        throw_new(cls_TypeError, "unsupported operand type(s) for %s: '%.40s' and '%.40s'",
                  op_str(op), type_name(a), type_name(b));
    }
    switch (op) {
    case B_ADD: return int_add(a, b);
    case B_SUB: return int_sub(a, b);
    case B_MUL: return int_mul(a, b);
    case B_DIV: {
        if (int_sign(b) == 0) throw_new(cls_ZeroDivisionError, "division by zero");
        return mkfloat(int_truediv(a, b)); }
    case B_FDIV: return int_floordiv(a, b);
    case B_MOD: return int_mod(a, b);
    case B_POW: return int_pow_v(a, b);
    case B_AND: case B_OR: case B_XOR:
        if (a.t == T_BOOL && b.t == T_BOOL)
            return tobool
                ((int)(op == B_AND ? (a.u.i & b.u.i)
                : (op == B_OR ? (a.u.i | b.u.i) : (a.u.i ^ b.u.i))));
        return int_bitop(op == B_AND ? '&' : (op == B_OR ? '|' : '^'), a, b);
    case B_LSH: {
        if (int_sign(b) < 0) throw_new(cls_ValueError, "negative shift count");
        if (int_sign(a) == 0) return mkint(0);
        if (b.t == T_BIG || b.u.i > 100000000L)
            throw_new(cls_OverflowError, "shift count too large");
        if (a.t != T_BIG && b.u.i < 30L && a.u.i < (1L << (30 - b.u.i)) &&
            a.u.i > -(1L << (30 - b.u.i)))
            return mkint(a.u.i * (1L << b.u.i));
        return int_mul(a, int_pow2(b.u.i)); }
    default: {
        if (int_sign(b) < 0) throw_new(cls_ValueError, "negative shift count");
        if (int_sign(a) == 0) return mkint(0);
        /* a limb (base 10000) has fewer than 14 bits, a small int fewer than 70 */
        if (b.t == T_BIG || b.u.i > 14L * (a.t == T_BIG ? (long)AS_BIG(a)->n : 5L))
            return mkint(int_sign(a) < 0 ? -1 : 0);
        if (a.t != T_BIG && b.u.i < 31L) {
            long dv = 1L << b.u.i, q = a.u.i / dv;
            if (a.u.i < 0 && a.u.i % dv != 0) q--;       /* floor, not truncation */
            return mkint(q);
        }
        return int_floordiv(a, int_pow2(b.u.i)); }
    }
}
/* repetition count of "seq * n" for an integer n of any size: clamps to 0 for negative and
   raises the errors CPython gives for sizes that cannot be allocated */
static long repeat_count(Value n, long unit)
{
    if (n.t == T_BIG && int_bitlen(n) > 63)
        throw_new(cls_OverflowError, "cannot fit 'int' into an index-sized integer");
    if (int_sign(n) <= 0) return 0;
    if (unit == 0) return 0;
    if (n.t == T_BIG) throw_new(cls_MemoryError, "");
    if (n.u.i > 400000000L / unit) throw_new(cls_MemoryError, "");
    return n.u.i;
}
static Value seq_repeat(Value s, Value nv)
{
    long i, n;
    if (s.t == T_STR) {
        Str *x = AS_STR(s);
        Buf b;
        n = x->len == 0 ? 0 : repeat_count(nv, x->len);
        buf_init(&b);
        for (i = 0; i < n; i++) buf_addn(&b, x->s, x->len);
        return buf_value(&b);
    } else {
        List *l = AS_LIST(s), *r;
        long j;
        n = l->n == 0 ? 0 : repeat_count(nv, l->n);
        r = s.t == T_LIST ? list_new(l->n * n) : tuple_new(l->n * n);
        for (i = 0; i < n; i++)
            for (j = 0; j < l->n; j++) r->a[i * l->n + j] = l->a[j];
        if (s.t == T_LIST) r->n = l->n * n;
        return mkobj(r, s.t);
    }
}
static Value py_format_percent(Value fmt, Value args);
/* ---- set operations: the algorithms of CPython's setobject.c (same iteration order) ----- */
static Value set_new_v(int frozen)
{
    Dict *d = dict_new_t(T_SET);
    d->frozen = frozen;
    return mkobj(d, T_SET);
}
/* the set behind v (a set, or an instance of a set subclass), else NULL */
static Dict *as_anyset(Value v)
{
    if (v.t == T_INST && AS_INST(v)->pay.t == T_SET) v = AS_INST(v)->pay;
    return v.t == T_SET ? AS_DICT(v) : NULL;
}
/* set_update_internal: add the elements of any iterable (sets are merged, dicts add their keys) */
static void set_update_value(Dict *so, Value other)
{
    Dict *os = as_anyset(other);
    Value *base = vsp, it, y;
    long i;
    if (os) { set_merge(so, os); return; }
    if (other.t == T_DICT) {
        Dict *od = AS_DICT(other);
        if (!SET_ORDERED(so) && (unsigned long)(so->fill + od->n) * 5UL >= set_mask(so) * 3UL)
            set_resize(so, (so->n + od->n) * 2);
        for (i = 0; i < od->used; i++)
            if (od->e[i].k.t != T_UNSET) set_add_h(so, od->e[i].k, od->e[i].hash);
        return;
    }
    VPUSH(other);
    it = get_iter(other);
    VPUSH(it);
    while (iter_next(it, &y)) set_add(so, y);
    vsp = base;
}
static Value set_copy_v(Dict *so, int frozen)
{
    Value rv = set_new_v(frozen);
    set_merge(AS_DICT(rv), so);
    return rv;
}
/* set_intersection: a new set with the type of so */
static Value set_intersection_v(Dict *so, Value other)
{
    Dict *os = as_anyset(other);
    Value rv = set_new_v(so->frozen), it, y;
    Dict *r = AS_DICT(rv);
    Value *base = vsp;
    long i;
    if (os == so) { set_merge(r, so); return rv; }
    VPUSH(rv);
    if (os) {
        Dict *a = so, *b = os;
        if (b->n > a->n) { a = os; b = so; }
        for (i = 0; i < b->used; i++)
            if (b->e[i].k.t != T_UNSET && set_has_h(a, b->e[i].k, b->e[i].hash))
                set_add_h(r, b->e[i].k, b->e[i].hash);
        vsp = base;
        return rv;
    }
    VPUSH(other);
    it = get_iter(other);
    VPUSH(it);
    while (iter_next(it, &y)) {
        unsigned long h = py_hash(y);
        if (set_has_h(so, y, h)) {
            set_add_h(r, y, h);
            if (r->n >= so->n) break;
        }
    }
    vsp = base;
    return rv;
}
/* set_difference_update_internal */
static void set_diff_update(Dict *so, Value other)
{
    Dict *os = as_anyset(other);
    Value *base = vsp, it, y;
    long i;
    if (os == so) { set_clear(so); return; }
    if (os) {
        Dict *od = os;
        if ((os->n >> 3) > so->n) {
            Value tmp = set_intersection_v(so, other);
            VPUSH(tmp);
            od = AS_DICT(tmp);
        }
        for (i = 0; i < od->used; i++)
            if (od->e[i].k.t != T_UNSET) set_discard_h(so, od->e[i].k, od->e[i].hash);
    } else {
        VPUSH(other);
        it = get_iter(other);
        VPUSH(it);
        while (iter_next(it, &y)) set_discard(so, y);
    }
    vsp = base;
    if (SET_ORDERED(so)) return;
    if ((unsigned long)(so->fill - so->n) <= set_mask(so) / 4UL) return;
    set_resize(so, so->n > 50000L ? so->n * 2 : so->n * 4);
}
static Value set_difference_v(Dict *so, Value other)
{
    Dict *os = as_anyset(other);
    Value rv;
    Dict *r;
    long i, osz = -1;
    if (os) osz = os->n;
    else if (other.t == T_DICT) osz = AS_DICT(other)->n;
    if (osz < 0 || (so->n >> 2) > osz) {
        rv = set_copy_v(so, so->frozen);
        VPUSH(rv);
        set_diff_update(AS_DICT(rv), other);
        vsp--;
        return rv;
    }
    rv = set_new_v(so->frozen);
    r = AS_DICT(rv);
    VPUSH(rv);
    for (i = 0; i < so->used; i++) {
        DEnt *e = so->e + i;
        if (e->k.t == T_UNSET) continue;
        if (os ? !set_has_h(os, e->k, e->hash) : dict_find(AS_DICT(other), e->k, e->hash) < 0)
            set_add_h(r, e->k, e->hash);
    }
    vsp--;
    return rv;
}
static void set_symdiff_loop(Dict *so, Dict *other)
{
    long i;
    for (i = 0; i < other->used; i++)
        if (other->e[i].k.t != T_UNSET && !set_discard_h(so, other->e[i].k, other->e[i].hash))
            set_add_h(so, other->e[i].k, other->e[i].hash);
}
/* set_symmetric_difference_update */
static void set_symdiff_update(Dict *so, Value other)
{
    Dict *os = as_anyset(other);
    Value *base = vsp;
    if (os == so) { set_clear(so); return; }
    if (other.t == T_DICT) {
        Dict *od = AS_DICT(other);
        long i;
        for (i = 0; i < od->used; i++)
            if (od->e[i].k.t != T_UNSET && !set_discard_h(so, od->e[i].k, od->e[i].hash))
                set_add_h(so, od->e[i].k, od->e[i].hash);
    } else if (os) set_symdiff_loop(so, os);
    else {
        Value tmp = set_new_v(0);
        VPUSH(tmp);
        set_update_value(AS_DICT(tmp), other);
        set_symdiff_loop(so, AS_DICT(tmp));
    }
    vsp = base;
}
static Value set_symdiff_v(Dict *so, Value other)
{
    Value rv = set_new_v(so->frozen);
    VPUSH(rv);
    set_update_value(AS_DICT(rv), other);
    set_symdiff_loop(AS_DICT(rv), so);
    vsp--;
    return rv;
}
static Value set_op(int op, Value a, Value b)
{
    Dict *x = AS_DICT(a);
    Value r;
    switch (op) {
    case B_OR:
        r = set_copy_v(x, x->frozen);
        if (as_anyset(b) != x) {
            VPUSH(r);
            set_update_value(AS_DICT(r), b);
            vsp--;
        }
        return r;
    case B_AND: return set_intersection_v(x, b);
    case B_SUB: return set_difference_v(x, b);
    default: return set_symdiff_v(x, b);
    }
}
static Value to_set_any(Value v)
{
    Value dv = set_new_v(0);
    Value *base = vsp;
    VPUSH(dv);
    VPUSH(v);
    set_update_value(AS_DICT(dv), v);
    vsp = base;
    return dv;
}
static Value binop(int op, Value a, Value b)
{
    if (is_num(a) && is_num(b)) return num_binop(op, a, b);
    if ((a.t == T_COMPLEX && (b.t == T_COMPLEX || is_num(b))) ||
        (b.t == T_COMPLEX && is_num(a))) return cplx_binop(op, a, b);
    if ((a.t == T_ND || b.t == T_ND) && a.t != T_INST && b.t != T_INST) return nd_binop(op, a, b);
    if (a.t == T_STR && b.t == T_STR && op == B_ADD) {
        Str *x = AS_STR(a), *y = AS_STR(b);
        Str *r = (Str *)gc_alloc(sizeof(Str) + (size_t)(x->len + y->len), T_STR);
        r->len = x->len + y->len;
        r->hash = 0;
        memcpy(r->s, x->s, (size_t)x->len);
        memcpy(r->s + x->len, y->s, (size_t)y->len);
        r->s[r->len] = 0;
        return mkobj(r, T_STR);
    }
    if ((a.t == T_BYTES || a.t == T_BARR) && (op == B_ADD || op == B_MUL)) {
        const char *pa, *pb;
        long na, nb, i;
        Buf bf;
        Value r;
        if (op == B_ADD && bytes_get(b, &pb, &nb) && (b.t == T_BYTES || b.t == T_BARR)) {
            bytes_get(a, &pa, &na);
            buf_init(&bf);
            buf_addn(&bf, pa, na);
            buf_addn(&bf, pb, nb);
            r = a.t == T_BARR ? mkbarr(bf.s, bf.n) : mkbytes(bf.s, bf.n);
            buf_free(&bf);
            return r;
        }
        if (op == B_MUL && (b.t == T_INT || b.t == T_BOOL || b.t == T_BIG)) {
            long cnt;
            bytes_get(a, &pa, &na);
            cnt = na == 0 ? 0 : repeat_count(b, na);
            buf_init(&bf);
            for (i = 0; i < cnt; i++) buf_addn(&bf, pa, na);
            r = a.t == T_BARR ? mkbarr(bf.s, bf.n) : mkbytes(bf.s, bf.n);
            buf_free(&bf);
            return r;
        }
    }
    if (op == B_MUL && (b.t == T_BYTES || b.t == T_BARR) &&
        (a.t == T_INT || a.t == T_BOOL || a.t == T_BIG))
        return binop(op, b, a);
    if (op == B_ADD && a.t == b.t && (a.t == T_LIST || a.t == T_TUPLE)) {
        List *x = AS_LIST(a), *y = AS_LIST(b), *r;
        long i;
        if (a.t == T_LIST) { r = list_new(x->n + y->n); r->n = x->n + y->n; }
        else r = tuple_new(x->n + y->n);
        for (i = 0; i < x->n; i++) r->a[i] = x->a[i];
        for (i = 0; i < y->n; i++) r->a[x->n + i] = y->a[i];
        return mkobj(r, a.t);
    }
    if (op == B_MUL) {
        if ((a.t == T_STR || a.t == T_LIST || a.t == T_TUPLE) &&
            (b.t == T_INT || b.t == T_BOOL || b.t == T_BIG))
            return seq_repeat(a, b);
        if ((b.t == T_STR || b.t == T_LIST || b.t == T_TUPLE) &&
            (a.t == T_INT || a.t == T_BOOL || a.t == T_BIG))
            return seq_repeat(b, a);
        if ((a.t == T_STR || a.t == T_LIST || a.t == T_TUPLE) && b.t != T_INST)
            throw_new(cls_TypeError, "can't multiply sequence by non-int of type '%.40s'",
                type_name(b));
    }
    if (op == B_MOD && a.t == T_STR) return py_format_percent(a, b);
    if (op == B_OR && a.t == T_DICT && b.t == T_DICT) {
        Dict *nd = dict_new();
        Value nv = mkobj(nd, T_DICT);
        VPUSH(nv);
        dict_update_from_hook(nd, a);
        dict_update_from_hook(nd, b);
        vsp--;
        return nv;
    }
    if (a.t == T_SET && (op == B_OR || op == B_AND || op == B_SUB || op == B_XOR)) {
        Dict *ob = as_anyset(b);
        if (ob) return set_op(op, a, mkobj(ob, T_SET));
    }
    if ((a.t == T_DVIEW || b.t == T_DVIEW)
        && (op == B_OR || op == B_AND || op == B_SUB || op == B_XOR)) {
        Value x = a, y = b, r;
        Value *base = vsp;
        if (a.t == T_DVIEW) x = ((DView *)a.u.o)->what == 1 ? a : dview_set(a);
        VPUSH(x);
        if (b.t == T_DVIEW) y = ((DView *)b.u.o)->what == 1 ? b : dview_set(b);
        VPUSH(y);
        if (x.t != T_SET) x = to_set_any(x);
        VPUSH(x);
        if (y.t != T_SET) y = to_set_any(y);
        VPUSH(y);
        r = set_op(op, x, y);
        vsp = base;
        return r;
    }
    if (a.t == T_INST) {
        Str *dn = dunder(op, 0);
        Value *m = dn ? class_find(AS_INST(a)->cls, dn) : NULL;
        if (m) {
            Value r = call2(*m, a, b);
            if (!IS_NOTIMPL(r)) return r;
        }
    }
    if (b.t == T_INST) {
        Str *dn = dunder(op, 1);
        Value *m = dn ? class_find(AS_INST(b)->cls, dn) : NULL;
        if (m) {
            Value r = call2(*m, b, a);
            if (!IS_NOTIMPL(r)) return r;
        }
    }
    if (b.t == T_INST && a.t != T_INST && AS_INST(b)->pay.t != T_UNSET)
        return binop(op, a, AS_INST(b)->pay);
    if (a.t == T_ND || b.t == T_ND) return nd_binop(op, a, b);
    if (op == B_ADD && a.t == T_STR)
        throw_new(cls_TypeError, "can only concatenate str (not \"%.40s\") to str", type_name(b));
    if (op == B_ADD && a.t == T_LIST)
        throw_new(cls_TypeError, "can only concatenate list (not \"%.40s\") to list", type_name(b));
    if (op == B_ADD && a.t == T_TUPLE)
        throw_new(cls_TypeError, "can only concatenate tuple (not \"%.40s\") to tuple",
            type_name(b));
    throw_new(cls_TypeError, "unsupported operand type(s) for %s: '%.40s' and '%.40s'",
              op_str(op), type_name(a), type_name(b));
    return NONE_V;
}
static Value inplace_binop(int op, Value a, Value b)
{
    if (a.t == T_BARR && op == B_ADD) {
        const char *pb;
        long nb;
        BArr *ba = AS_BARR(a);
        if (!bytes_get(b, &pb, &nb))
            throw_new(cls_TypeError, "can't concat %.40s to bytearray", type_name(b));
        if (pb >= ba->s && pb < ba->s + ba->n) {   /* b += b */
            Buf t;
            buf_init(&t);
            buf_addn(&t, pb, nb);
            barr_reserve(ba, ba->n + nb);
            memcpy(ba->s + ba->n, t.s, (size_t)nb);
            buf_free(&t);
        } else {
            barr_reserve(ba, ba->n + nb);
            memcpy(ba->s + ba->n, pb, (size_t)nb);
        }
        ba->n += nb;
        return a;
    }
    if (a.t == T_BARR && op == B_MUL && (b.t == T_INT || b.t == T_BOOL)) {
        BArr *ba = AS_BARR(a);
        long i, n0 = ba->n;
        if (b.u.i <= 0) { ba->n = 0; return a; }
        barr_reserve(ba, n0 * b.u.i);
        for (i = 1; i < b.u.i; i++) memcpy(ba->s + i * n0, ba->s, (size_t)n0);
        ba->n = n0 * b.u.i;
        return a;
    }
    if (a.t == T_SET && !AS_DICT(a)->frozen &&
        (op == B_OR || op == B_AND || op == B_SUB || op == B_XOR)) {
        Dict *x = AS_DICT(a), *ob = as_anyset(b);
        if (ob) {
            Value ov = mkobj(ob, T_SET);
            if (op == B_OR) set_update_value(x, ov);
            else if (op == B_SUB) set_diff_update(x, ov);
            else if (op == B_XOR) set_symdiff_update(x, ov);
            else {
                Value tmp = set_intersection_v(x, ov);
                set_swap(x, AS_DICT(tmp));
            }
            return a;
        }
    }
    if (a.t == T_INST) {
        Str *dn = idunder(op);
        Value *m = dn ? class_find(AS_INST(a)->cls, dn) : NULL;
        if (m) {
            Value r = call2(*m, a, b);
            if (!IS_NOTIMPL(r)) return r;
        }
    }
    return binop(op, a, b);
}
static Value unop(int op, Value a)
{
    if (a.t == T_ND) return nd_unop(op, a);
    if (a.t == T_COMPLEX) return cplx_unop(op, a);
    if (op == '-') {
        if (a.t == T_INT || a.t == T_BOOL) return mkint(-a.u.i);
        if (a.t == T_FLOAT) return mkfloat(-a.u.d);
        if (a.t == T_BIG) return int_neg(a);
        if (a.t == T_INST) {
            Value *m = class_find(AS_INST(a)->cls, S_neg);
            if (m) return call1(*m, a);
        }
        throw_new(cls_TypeError, "bad operand type for unary -: '%.40s'", type_name(a));
    } else if (op == '+') {
        if (a.t == T_BOOL) return mkint(a.u.i);
        if (is_num(a)) return a;
        if (a.t == T_INST) {
            Value *m = class_find(AS_INST(a)->cls, S_pos);
            if (m) return call1(*m, a);
        }
        throw_new(cls_TypeError, "bad operand type for unary +: '%.40s'", type_name(a));
    } else {
        if (a.t == T_INT || a.t == T_BOOL) return mkint(-a.u.i - 1);
        if (a.t == T_BIG) return int_invert(a);
        if (a.t == T_INST) {
            Value *m = class_find(AS_INST(a)->cls, S_invert);
            if (m) return call1(*m, a);
        }
        throw_new(cls_TypeError, "bad operand type for unary ~: '%.40s'", type_name(a));
    }
    return NONE_V;
}
