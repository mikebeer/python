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
    case T_SET: return cls_set;
    case T_RANGE: return cls_range;
    case T_FUNC: return cls_func;
    case T_BUILTIN: return cls_builtin;
    case T_CLASS: return cls_type;
    case T_INST: return AS_INST(v)->cls;
    case T_BOUND: return cls_method;
    case T_GEN: return cls_gen;
    case T_ITER: return cls_iter;
    case T_MODULE: return cls_module;
    case T_PROP: return cls_prop;
    case T_SMETH: return cls_smeth;
    case T_CMETH: return cls_cmeth;
    case T_SUPER: return cls_super;
    case T_FILE: return cls_file;
    case T_CELL: return cls_cell;
    default: return cls_object;
    }
}
static Value *class_find(Class *c, Str *nm)
{
    while (c) {
        Value *p = amap_get(&c->ns, nm);
        if (p) return p;
        c = c->base;
    }
    return NULL;
}
static int class_isa(Class *c, Class *base)
{
    while (c) {
        if (c == base) return 1;
        c = c->base;
    }
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
static void throw_val(Value exc)
{
    cur_exc = exc;
    exc_line = cur_line;
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

static Value make_range(long a, long b, long c)
{
    Range *r = (Range *)gc_alloc(sizeof(Range), T_RANGE);
    r->start = a; r->stop = b; r->step = c;
    return mkobj(r, T_RANGE);
}
static long range_len(Range *r)
{
    if (r->step > 0) return r->start < r->stop ? (r->stop - r->start - 1) / r->step + 1 : 0;
    return r->start > r->stop ? (r->start - r->stop - 1) / (-r->step) + 1 : 0;
}

static Value mk_prop(int type, Value g, Value s)
{
    Prop *p = (Prop *)gc_alloc(sizeof(Prop), type);
    p->get = g; p->set = s;
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
    case T_STR: return AS_STR(v)->len != 0;
    case T_LIST: case T_TUPLE: return AS_LIST(v)->n != 0;
    case T_DICT: case T_SET: return AS_DICT(v)->n != 0;
    case T_RANGE: return range_len((Range *)v.u.o) != 0;
    case T_INST: {
        Value *m = class_find(AS_INST(v)->cls, S_bool);
        if (m) return py_truthy(call1(*m, v));
        m = class_find(AS_INST(v)->cls, S_len);
        if (m) return py_len(v) != 0;
        return 1; }
    default: return 1;
    }
}
static long py_len(Value v)
{
    switch (v.t) {
    case T_STR: return AS_STR(v)->len;
    case T_LIST: case T_TUPLE: return AS_LIST(v)->n;
    case T_DICT: case T_SET: return AS_DICT(v)->n;
    case T_RANGE: return range_len((Range *)v.u.o);
    case T_INST: {
        Value *m = class_find(AS_INST(v)->cls, S_len);
        if (m) {
            Value r = call1(*m, v);
            if (r.t != T_INT) throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer", type_name(r));
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
    if (v.t == T_FLOAT) return v.u.d;
    return int_todouble(v);
}
static long need_long(Value v, const char *what)
{
    if (v.t == T_INT || v.t == T_BOOL) return v.u.i;
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_index);
        if (m) { Value r = call1(*m, v); if (r.t == T_INT) return r.u.i; }
    }
    throw_new(cls_TypeError, "%.60s must be an integer, not '%.40s'", what, type_name(v));
    return 0;
}

/* ---- equality --------------------------------------------------------- */
static int num_eq(Value a, Value b)
{
    if (a.t == T_FLOAT || b.t == T_FLOAT) return to_dbl(a) == to_dbl(b);
    return int_cmp(a, b) == 0;
}
static int py_eq(Value a, Value b)
{
    if (is_num(a) && is_num(b)) return num_eq(a, b);
    if (a.t != b.t) {
        if (a.t == T_INST || b.t == T_INST) goto inst;
        return 0;
    }
    switch (a.t) {
    case T_NONE: return 1;
    case T_STR:
        return a.u.o == b.u.o || (AS_STR(a)->len == AS_STR(b)->len &&
            memcmp(AS_STR(a)->s, AS_STR(b)->s, (size_t)AS_STR(a)->len) == 0);
    case T_LIST: case T_TUPLE: {
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
        if (x->n != y->n) return 0;
        for (i = 0; i < x->used; i++) {
            if (x->e[i].k.t == T_UNSET) continue;
            if (!dict_get(y, x->e[i].k, &o)) return 0;
        }
        return 1; }
    case T_RANGE: {
        Range *x = (Range *)a.u.o, *y = (Range *)b.u.o;
        long lx = range_len(x), ly = range_len(y);
        if (lx != ly) return 0;
        if (lx == 0) return 1;
        return x->start == y->start && (lx == 1 || x->step == y->step); }
    case T_INST: goto inst;
    default: return a.u.o == b.u.o;
    }
inst:
    if (a.t == T_INST) {
        Value *m = class_find(AS_INST(a)->cls, S_eq);
        if (m) return py_truthy(call2(*m, a, b));
    }
    if (b.t == T_INST) {
        Value *m = class_find(AS_INST(b)->cls, S_eq);
        if (m) return py_truthy(call2(*m, b, a));
    }
    return a.t == b.t && a.u.o == b.u.o;
}

static unsigned long py_hash(Value v)
{
    switch (v.t) {
    case T_NONE: return 1234567UL;
    case T_BOOL: case T_INT: case T_BIG: return int_hash(v);
    case T_FLOAT: {
        double d = v.u.d;
        if (d == floor(d) && d < 1e17 && d > -1e17) return int_hash(mkint((long)d));
        if (d != d) return 0;
        {
            unsigned char *p = (unsigned char *)&d;
            unsigned long h = 17;
            size_t i;
            for (i = 0; i < sizeof(double); i++) h = h * 31 + p[i];
            return h;
        } }
    case T_STR: return str_hash(AS_STR(v));
    case T_TUPLE: {
        List *l = AS_LIST(v);
        unsigned long h = 0x345678UL;
        long i;
        for (i = 0; i < l->n; i++) h = (h * 1000003UL) ^ py_hash(l->a[i]);
        return h; }
    case T_LIST: case T_DICT: case T_SET:
        throw_new(cls_TypeError, "unhashable type: '%.40s'", type_name(v));
        return 0;
    case T_INST: {
        Value *m = class_find(AS_INST(v)->cls, S_hash);
        if (m) {
            Value r = call1(*m, v);
            if (r.t == T_INT || r.t == T_BOOL) return (unsigned long)r.u.i;
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
static Value propsetter_fn, obj_eq_v, obj_ne_v, obj_hash_v;
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
            double x = to_dbl(a), y = to_dbl(b);
            return x < y ? -1 : (x > y ? 1 : 0);
        }
        return int_cmp(a, b);
    }
    if (a.t == T_STR && b.t == T_STR) {
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
            if (m) return py_truthy(call2(*m, a, b));
        }
        return !py_eq(a, b);
    }
    if (a.t == T_INST) {
        Value *m = class_find(AS_INST(a)->cls, *op_names(op, 0));
        if (m) return py_truthy(call2(*m, a, b));
    }
    if (b.t == T_INST) {
        Value *m = class_find(AS_INST(b)->cls, *op_names(op, 1));
        if (m) return py_truthy(call2(*m, b, a));
    }
    if (a.t == T_FLOAT || b.t == T_FLOAT) {
        if (is_num(a) && is_num(b)) {
            double x = to_dbl(a), y = to_dbl(b);
            switch (op) {
            case C_LT: return x < y; case C_LE: return x <= y;
            case C_GT: return x > y; default: return x >= y;
            }
        }
    }
    if (a.t == T_SET && b.t == T_SET) {
        Dict *x = AS_DICT(a), *y = AS_DICT(b);
        long i;
        Value o;
        int sub = 1, sup = 1;
        for (i = 0; i < x->used; i++)
            if (x->e[i].k.t != T_UNSET && !dict_get(y, x->e[i].k, &o)) sub = 0;
        for (i = 0; i < y->used; i++)
            if (y->e[i].k.t != T_UNSET && !dict_get(x, y->e[i].k, &o)) sup = 0;
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
static int rich_lt_hook(Value a, Value b)
{
    return rich_cmp(C_LT, a, b);
}

/* ---- repr / str ------------------------------------------------------- */
static void repr_buf(Value v, Buf *b, int depth);
static void str_buf(Value v, Buf *b);

static void repr_str_lit(Str *s, Buf *b)
{
    long i;
    int q = '\'';
    char t[8];
    if (memchr(s->s, '\'', (size_t)s->len) && !memchr(s->s, '"', (size_t)s->len)) q = '"';
    buf_addc(b, q);
    for (i = 0; i < s->len; i++) {
        int c = (unsigned char)s->s[i];
        if (c == q || c == '\\') { buf_addc(b, '\\'); buf_addc(b, c); }
        else if (c == '\n') buf_add(b, "\\n");
        else if (c == '\t') buf_add(b, "\\t");
        else if (c == '\r') buf_add(b, "\\r");
        else if (c < 32 || c == 127) { sprintf(t, "\\x%02x", c); buf_add(b, t); }
        else buf_addc(b, c);
    }
    buf_addc(b, q);
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
        if (d->n == 0) { buf_add(b, "set()"); break; }
        buf_addc(b, '{');
        for (i = 0; i < d->used; i++) {
            if (d->e[i].k.t == T_UNSET) continue;
            if (!first) buf_add(b, ", ");
            first = 0;
            repr_buf(d->e[i].k, b, depth + 1);
        }
        buf_addc(b, '}');
        break; }
    case T_RANGE: {
        Range *r = (Range *)v.u.o;
        if (r->step == 1) sprintf(t, "range(%ld, %ld)", r->start, r->stop);
        else sprintf(t, "range(%ld, %ld, %ld)", r->start, r->stop, r->step);
        buf_add(b, t);
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
        buf_add(b, AS_CLASS(v)->name->s);
        buf_add(b, "'>");
        break;
    case T_INST: {
        Inst *in = AS_INST(v);
        Value *m = class_find(in->cls, S_repr);
        if (m) {
            Value r = call1(*m, v);
            if (r.t != T_STR) throw_new(cls_TypeError, "__repr__ returned non-string (type %.40s)", type_name(r));
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
    if (v.t == T_INST) {
        Inst *in = AS_INST(v);
        Value *m = class_find(in->cls, S_str);
        if (m) {
            Value r = call1(*m, v);
            if (r.t != T_STR) throw_new(cls_TypeError, "__str__ returned non-string (type %.40s)", type_name(r));
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
    for (k = c; k; k = k->base) n++;
    t = tuple_new(n);
    n = 0;
    for (k = c; k; k = k->base) t->a[n++] = mkobj(k, T_CLASS);
    return mkobj(t, T_TUPLE);
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
        if (nm == S_dict) {
            Dict *d = dict_new();
            int i;
            Value dv = mkobj(d, T_DICT);
            VPUSH(dv);
            for (i = 0; i < in->a.n; i++) dict_set(d, mkobj(in->a.k[i], T_STR), in->a.v[i]);
            vsp--;
            return dv;
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
            return *p;
        }
        if (nm == S_name) return mkobj(c->name, T_STR);
        if (nm == S_mro) return class_mro_tuple(c);
        if (nm == S_doc) return NONE_V;
        if (nm == S_class) return mkobj(cls_type, T_CLASS);
        if (nm == intern_c("__bases__")) {
            Value bv[1];
            if (!c->base) return mktuple_from(bv, 0);
            bv[0] = mkobj(c->base, T_CLASS);
            return mktuple_from(bv, 1);
        }
        break; }
    case T_MODULE:
        p = amap_get(&((Module *)o.u.o)->ns, nm);
        if (p) return *p;
        if (nm == S_name) return mkobj(((Module *)o.u.o)->name, T_STR);
        break;
    case T_SUPER: {
        Super *s = (Super *)o.u.o;
        p = class_find(s->start, nm);
        if (p) {
            if (s->obj.t == T_CLASS) {
                if (p->t == T_CMETH) return new_bound(s->obj, ((Prop *)p->u.o)->get);
                if (p->t == T_SMETH) return ((Prop *)p->u.o)->get;
                return *p;
            }
            return bind_class_attr(s->obj, p, class_of(s->obj));
        }
        if (nm == S_eq) return new_bound(s->obj, obj_eq_v);
        if (nm == S_ne) return new_bound(s->obj, obj_ne_v);
        if (nm == S_hash) return new_bound(s->obj, obj_hash_v);
        break; }
    case T_FUNC:
        if (nm == S_name) return mkobj(AS_FUNC(o)->name, T_STR);
        if (nm == S_doc) return NONE_V;
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
        break; }
    default: break;
    }
    if (nm == S_class) return mkobj(class_of(o), T_CLASS);
    {
        Class *c = class_of(o);
        p = class_find(c, nm);
        if (p && o.t != T_INST && o.t != T_CLASS && o.t != T_MODULE) return bind_class_attr(o, p, c);
        if (nm == S_name && o.t != T_INST) {
            /* fallthrough to error */
        }
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
        p = class_find(s->start, nm);
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
        if (in->cls->flags & CF_PROPS) {
            Value *p = class_find(in->cls, nm);
            if (p && p->t == T_PROP) {
                Prop *pr = (Prop *)p->u.o;
                if (pr->set.t == T_UNSET) throw_new(cls_AttributeError, "can't set attribute '%.60s'", nm->s);
                call2(pr->set, o, v);
                return;
            }
        }
        amap_set(&in->a, nm, v);
        return; }
    case T_CLASS:
        amap_set(&AS_CLASS(o)->ns, nm, v);
        if (v.t == T_PROP) AS_CLASS(o)->flags |= CF_PROPS;
        class_epoch++;
        return;
    case T_MODULE:
        amap_set(&((Module *)o.u.o)->ns, nm, v);
        return;
    default: break;
    }
    throw_new(cls_AttributeError, "'%.60s' object has no attribute '%.60s' and no __dict__ for setting new attributes", type_name(o), nm->s);
}
static void del_attr(Value o, Str *nm)
{
    if (o.t == T_INST && amap_del(&AS_INST(o)->a, nm)) return;
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
    case T_RANGE: {
        Range *r = (Range *)o.u.o;
        long n = range_len(r);
        slice_adjust(lo, hi, st, n, &start, &stop, &step, &cnt);
        return make_range(r->start + start * r->step, r->start + (start + cnt * step) * r->step,
                          r->step * step); }
    case T_INST: {
        Value *m = class_find(AS_INST(o)->cls, S_getitem);
        if (m) {
            Value args[3];
            Value *base = vsp;
            Value sl, r;
            args[0] = lo; args[1] = hi; args[2] = st;
            sl = mktuple_from(args, 3);
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
static Value subscr(Value o, Value idx)
{
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
    case T_RANGE:
        if (idx.t == T_INT || idx.t == T_BOOL) {
            Range *r = (Range *)o.u.o;
            long i = norm_index(idx.u.i, range_len(r), "range object");
            return mkint(r->start + i * r->step);
        }
        throw_new(cls_TypeError, "range indices must be integers or slices, not %.40s", type_name(idx));
        break;
    case T_INST: {
        Value *m = class_find(AS_INST(o)->cls, S_getitem);
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
                throw_new(cls_ValueError, "attempt to assign sequence of size %ld to extended slice of size %ld", src->n, cnt);
            for (i = 0; i < cnt; i++) l->a[start + i * step] = src->a[i];
        }
        vsp--;
        return;
    }
    throw_new(cls_TypeError, "'%.60s' object does not support slice assignment", type_name(o));
}
static void setitem(Value o, Value idx, Value v)
{
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
        throw_new(cls_TypeError, "list indices must be integers or slices, not %.40s", type_name(idx));
        break;
    case T_DICT: dict_set(AS_DICT(o), idx, v); return;
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
        for (i = cnt - 1; i >= 0; i--) list_splice(l, start + i * step, start + i * step + 1, NULL, 0);
        return;
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
    case T_DICT: it = iter_new(IK_DICT); it->a = o; return mkobj(it, T_ITER);
    case T_SET: it = iter_new(IK_SET); it->a = o; return mkobj(it, T_ITER);
    case T_RANGE: {
        Range *r = (Range *)o.u.o;
        it = iter_new(IK_RANGE);
        it->i = r->start; it->n = r->stop; it->step = r->step;
        return mkobj(it, T_ITER); }
    case T_ITER: case T_GEN: return o;
    case T_FILE: it = iter_new(IK_FILE); it->a = o; return mkobj(it, T_ITER);
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
        if (!m) throw_new(cls_TypeError, "iter() returned non-iterator of type '%.40s'", type_name(it));
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
    case IK_RANGE:
        if (t->step > 0 ? t->i >= t->n : t->i <= t->n) return 0;
        *out = mkint(t->i);
        t->i += t->step;
        return 1;
    case IK_DICT: case IK_DVALS: case IK_DITEMS: case IK_SET: {
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
            *out = mktuple2(mkint(t->i), x);
            vsp = base;
        }
        t->i++;
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
            *out = mkint(r->start + t->i * r->step);
        } else *out = AS_LIST(t->a)->a[t->i];
        t->i--;
        return 1; }
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
    case T_DICT: case T_SET: {
        Value o;
        return dict_get(AS_DICT(c), x, &o); }
    case T_STR: {
        Str *s = AS_STR(c), *u;
        long i;
        if (x.t != T_STR) throw_new(cls_TypeError, "'in <string>' requires string as left operand, not %.40s", type_name(x));
        u = AS_STR(x);
        if (u->len == 0) return 1;
        for (i = 0; i + u->len <= s->len; i++)
            if (s->s[i] == u->s[0] && memcmp(s->s + i, u->s, (size_t)u->len) == 0) return 1;
        return 0; }
    case T_RANGE: {
        Range *r = (Range *)c.u.o;
        if (x.t == T_INT || x.t == T_BOOL) {
            long v = x.u.i;
            if (r->step > 0) return v >= r->start && v < r->stop && (v - r->start) % r->step == 0;
            return v <= r->start && v > r->stop && (r->start - v) % (-r->step) == 0;
        }
        return 0; }
    case T_INST: {
        Value *m = class_find(AS_INST(c)->cls, S_contains);
        if (m) return py_truthy(call2(*m, c, x));
        break; }
    default: break;
    }
    {
        Value it, y;
        if (c.t != T_INST && c.t != T_GEN && c.t != T_ITER)
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
    case B_XOR: return "^"; case B_LSH: return "<<"; default: return ">>";
    }
}
static Str *dunder(int op, int rev)
{
    switch (op) {
    case B_ADD: return rev ? S_radd : S_add;
    case B_SUB: return rev ? S_rsub : S_sub;
    case B_MUL: return rev ? S_rmul : S_mul;
    case B_DIV: return rev ? NULL : S_truediv;
    case B_FDIV: return rev ? NULL : S_floordiv;
    case B_MOD: return rev ? NULL : S_mod;
    case B_POW: return rev ? NULL : S_pow;
    case B_AND: return rev ? NULL : S_and;
    case B_OR: return rev ? NULL : S_or;
    case B_XOR: return rev ? NULL : S_xor;
    case B_LSH: return rev ? NULL : S_lshift;
    default: return rev ? NULL : S_rshift;
    }
}
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
            if (x == 0 && y < 0) throw_new(cls_ZeroDivisionError, "0.0 cannot be raised to a negative power");
            if (x < 0 && y != floor(y)) throw_new(cls_ValueError, "negative number cannot be raised to a fractional power");
            return mkfloat(pow(x, y));
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
        return mkfloat(int_todouble(a) / int_todouble(b)); }
    case B_FDIV: return int_floordiv(a, b);
    case B_MOD: return int_mod(a, b);
    case B_POW: return int_pow_v(a, b);
    case B_AND: case B_OR: case B_XOR:
        if (a.t == T_BIG || b.t == T_BIG)
            throw_new(cls_OverflowError, "bitwise operations on huge integers are not supported");
        if (a.t == T_BOOL && b.t == T_BOOL)
            return tobool(op == B_AND ? (a.u.i & b.u.i) : (op == B_OR ? (a.u.i | b.u.i) : (a.u.i ^ b.u.i)));
        return mkint(op == B_AND ? (a.u.i & b.u.i) : (op == B_OR ? (a.u.i | b.u.i) : (a.u.i ^ b.u.i)));
    case B_LSH: {
        if (int_sign(b) < 0) throw_new(cls_ValueError, "negative shift count");
        if (b.t == T_BIG || b.u.i > 100000000L) throw_new(cls_OverflowError, "shift count too large");
        return int_mul(a, int_pow(mkint(2), b.u.i)); }
    default: {
        if (int_sign(b) < 0) throw_new(cls_ValueError, "negative shift count");
        if (b.t == T_BIG || b.u.i > 100000000L) return mkint(int_sign(a) < 0 ? -1 : 0);
        return int_floordiv(a, int_pow(mkint(2), b.u.i)); }
    }
}
static Value seq_repeat(Value s, long n)
{
    long i;
    if (n < 0) n = 0;
    if (s.t == T_STR) {
        Str *x = AS_STR(s);
        Buf b;
        if (x->len * n > 400000000L) throw_new(cls_OverflowError, "repeated string is too long");
        buf_init(&b);
        for (i = 0; i < n; i++) buf_addn(&b, x->s, x->len);
        return buf_value(&b);
    } else {
        List *l = AS_LIST(s), *r;
        long j;
        r = s.t == T_LIST ? list_new(l->n * n) : tuple_new(l->n * n);
        for (i = 0; i < n; i++)
            for (j = 0; j < l->n; j++) r->a[i * l->n + j] = l->a[j];
        if (s.t == T_LIST) r->n = l->n * n;
        return mkobj(r, s.t);
    }
}
static Value py_format_percent(Value fmt, Value args);
static Value set_op(int op, Value a, Value b)
{
    Dict *x = AS_DICT(a), *y = AS_DICT(b), *r = dict_new_t(T_SET);
    Value rv = mkobj(r, T_SET), o;
    long i;
    VPUSH(rv);
    if (op == B_OR) {
        for (i = 0; i < x->used; i++) if (x->e[i].k.t != T_UNSET) set_add(r, x->e[i].k);
        for (i = 0; i < y->used; i++) if (y->e[i].k.t != T_UNSET) set_add(r, y->e[i].k);
    } else if (op == B_AND) {
        for (i = 0; i < x->used; i++)
            if (x->e[i].k.t != T_UNSET && dict_get(y, x->e[i].k, &o)) set_add(r, x->e[i].k);
    } else if (op == B_SUB) {
        for (i = 0; i < x->used; i++)
            if (x->e[i].k.t != T_UNSET && !dict_get(y, x->e[i].k, &o)) set_add(r, x->e[i].k);
    } else {
        for (i = 0; i < x->used; i++)
            if (x->e[i].k.t != T_UNSET && !dict_get(y, x->e[i].k, &o)) set_add(r, x->e[i].k);
        for (i = 0; i < y->used; i++)
            if (y->e[i].k.t != T_UNSET && !dict_get(x, y->e[i].k, &o)) set_add(r, y->e[i].k);
    }
    vsp--;
    return rv;
}
static Value binop(int op, Value a, Value b)
{
    if (is_num(a) && is_num(b)) return num_binop(op, a, b);
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
        if ((a.t == T_STR || a.t == T_LIST || a.t == T_TUPLE) && (b.t == T_INT || b.t == T_BOOL))
            return seq_repeat(a, b.u.i);
        if ((b.t == T_STR || b.t == T_LIST || b.t == T_TUPLE) && (a.t == T_INT || a.t == T_BOOL))
            return seq_repeat(b, a.u.i);
        if ((a.t == T_STR || a.t == T_LIST || a.t == T_TUPLE) && b.t != T_INST)
            throw_new(cls_TypeError, "can't multiply sequence by non-int of type '%.40s'", type_name(b));
    }
    if (op == B_MOD && a.t == T_STR) return py_format_percent(a, b);
    if (a.t == T_SET && b.t == T_SET && (op == B_OR || op == B_AND || op == B_SUB || op == B_XOR))
        return set_op(op, a, b);
    if (a.t == T_INST) {
        Str *dn = dunder(op, 0);
        Value *m = dn ? class_find(AS_INST(a)->cls, dn) : NULL;
        if (m) return call2(*m, a, b);
    }
    if (b.t == T_INST) {
        Str *dn = dunder(op, 1);
        Value *m = dn ? class_find(AS_INST(b)->cls, dn) : NULL;
        if (m) return call2(*m, b, a);
    }
    if (op == B_ADD && a.t == T_STR)
        throw_new(cls_TypeError, "can only concatenate str (not \"%.40s\") to str", type_name(b));
    if (op == B_ADD && a.t == T_LIST)
        throw_new(cls_TypeError, "can only concatenate list (not \"%.40s\") to list", type_name(b));
    if (op == B_ADD && a.t == T_TUPLE)
        throw_new(cls_TypeError, "can only concatenate tuple (not \"%.40s\") to tuple", type_name(b));
    throw_new(cls_TypeError, "unsupported operand type(s) for %s: '%.40s' and '%.40s'",
              op_str(op), type_name(a), type_name(b));
    return NONE_V;
}
static Value unop(int op, Value a)
{
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
        if (a.t == T_BIG) return int_sub(int_neg(a), mkint(1));
        if (a.t == T_INST) {
            Value *m = class_find(AS_INST(a)->cls, S_invert);
            if (m) return call1(*m, a);
        }
        throw_new(cls_TypeError, "bad operand type for unary ~: '%.40s'", type_name(a));
    }
    return NONE_V;
}
