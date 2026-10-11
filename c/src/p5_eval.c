/* =========================================================================
   p5_eval.c   evaluator: variables, calls, expressions, statements,
               generators, exceptions
   ========================================================================= */
enum { ST_NORMAL = 0, ST_BREAK, ST_CONTINUE, ST_RETURN, ST_YIELD };

static int repl_echo;
static void exec_import(Frame *f, Node *n);
static void exec_from(Frame *f, Node *n);
static Node *compile_src(const char *src, long len, Str *gp);
static void run_block_in(Node *blk, Str *gp);
static Str *main_gp;
static Value format_value(Value v, const char *spec, long speclen);

static Value eval(Frame *f, Node *n);
static int exec(Frame *f, Node *n);
static int exec_block(Frame *f, Node *b);
static Value call_func(Func *fn, int n, Value *a, int nk, Value *kw);

#define STATE(f, n, k) ((f)->gen->state[(n)->sidx + (k)])

/* ---- protected execution --------------------------------------------- */
static int protected_block(Frame *f, Node *b, int *st, Value *exc)
{
    Handler h;
    h.prev = cur_handler;
    h.sp = vsp;
    h.depth = call_depth;
    h.frame = cur_frame;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        *st = exec_block(f, b);
        cur_handler = h.prev;
        return 0;
    }
    cur_handler = h.prev;
    vsp = h.sp;
    call_depth = h.depth;
    cur_frame = h.frame;
    *exc = cur_exc;
    return 1;
}
static int protected_call(Value fn, int n, Value *a, Value *res, Value *exc)
{
    Handler h;
    Value *base = vsp;
    int i;
    h.prev = cur_handler;
    h.sp = vsp;
    h.depth = call_depth;
    h.frame = cur_frame;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        VPUSH(fn);
        for (i = 0; i < n; i++) VPUSH(a[i]);
        *res = call_value(fn, n, base + 1, 0, NULL);
        vsp = base;
        cur_handler = h.prev;
        return 0;
    }
    cur_handler = h.prev;
    vsp = h.sp;
    call_depth = h.depth;
    cur_frame = h.frame;
    *exc = cur_exc;
    return 1;
}

/* ---- variables -------------------------------------------------------- */
static Cell *new_cell(Value v)
{
    Cell *c = (Cell *)gc_alloc(sizeof(Cell), T_CELL);
    c->v = v;
    return c;
}
static void unbound_error(Var *v)
{
    if (v->kind == VK_GLOBAL || v->kind == VK_NS)
        throw_new(cls_NameError, "name '%.80s' is not defined", v->name->s);
    if (v->kind == VK_FREE)
        throw_new(cls_NameError, "cannot access free variable '%.80s' where it is not associated with a value in enclosing scope", v->name->s);
    throw_new(cls_UnboundLocalError, "cannot access local variable '%.80s' where it is not associated with a value", v->name->s);
}
static Value load_var_slow(Frame *f, Var *v)
{
    Value x;
    switch (v->kind) {
    case VK_LOCAL: x = f->loc[v->slot]; break;
    case VK_CELL: x = ((Cell *)f->loc[v->slot].u.o)->v; break;
    case VK_FREE: x = f->cells[v->slot]->v; break;
    case VK_GLOBAL:
        x = G[v->slot];
        if (x.t == T_UNSET && v->bslot >= 0) x = G[v->bslot];
        break;
    case VK_NS: {
        Value *p = f->ns ? amap_get(&f->ns->ns, v->name) : NULL;
        if (p) return *p;
        return load_var_slow(f, v->fb); }
    default: x = UNSET_V; break;
    }
    if (x.t == T_UNSET) unbound_error(v);
    return x;
}
static void store_var(Frame *f, Var *v, Value x)
{
    switch (v->kind) {
    case VK_LOCAL: f->loc[v->slot] = x; break;
    case VK_CELL: ((Cell *)f->loc[v->slot].u.o)->v = x; break;
    case VK_FREE: f->cells[v->slot]->v = x; break;
    case VK_GLOBAL: G[v->slot] = x; break;
    case VK_NS:
        amap_set(&f->ns->ns, v->name, x);
        class_epoch++;
        break;
    default: break;
    }
}

/* ---- functions -------------------------------------------------------- */
static Func *make_function(Frame *f, Node *def)
{
    FuncDef *fd = def->fd;
    Func *fn = (Func *)gc_alloc(sizeof(Func), T_FUNC);
    int i;
    Value fv = mkobj(fn, T_FUNC);
    fn->def = def;
    fn->name = fd->name;
    fn->cells = NULL;
    fn->defs = NULL;
    fn->ndefs = 0;
    fn->kwdefs = NULL;
    fn->defclass = NULL;
    fn->attrs.k = NULL; fn->attrs.v = NULL; fn->attrs.n = fn->attrs.cap = 0;
    if (fd->nfree > 0) {
        fn->cells = (Cell **)xmalloc(sizeof(Cell *) * (size_t)fd->nfree);
        for (i = 0; i < fd->nfree; i++) {
            Var *src = fd->fv[i]->src;
            if (src->kind == VK_CELL) fn->cells[i] = (Cell *)f->loc[src->slot].u.o;
            else fn->cells[i] = f->cells[src->slot];
        }
    }
    VPUSH(fv);
    if (fd->ndefaults > 0) {
        fn->defs = (Value *)xmalloc(sizeof(Value) * (size_t)fd->ndefaults);
        for (i = 0; i < fd->ndefaults; i++) fn->defs[i] = NONE_V;
        fn->ndefs = fd->ndefaults;
        for (i = 0; i < fd->ndefaults; i++) fn->defs[i] = eval(f, fd->defaults[i]);
    }
    if (fd->nkwonly > 0) {
        fn->kwdefs = (Value *)xmalloc(sizeof(Value) * (size_t)fd->nkwonly);
        for (i = 0; i < fd->nkwonly; i++) fn->kwdefs[i] = UNSET_V;
        for (i = 0; i < fd->nkwonly; i++)
            if (fd->kwdefaults[i]) fn->kwdefs[i] = eval(f, fd->kwdefaults[i]);
    }
    vsp--;
    return fn;
}

static void arg_error(Func *fn, const char *what, int cnt, Buf *names)
{
    char b[600];
    sprintf(b, "%.80s() missing %d required %s argument%s: %.400s", fn->name->s, cnt, what,
            cnt == 1 ? "" : "s", names->s);
    buf_free(names);
    throw_val(new_exc(cls_TypeError, b));
}

static Value *bind_args(Func *fn, FuncDef *fd, int n, Value *a, int nk, Value *kw)
{
    Value *loc = vsp;
    int i, npos = fd->npos, ncopy;
    Dict *kd = NULL;
    vcheck(fd->nlocals + 8);
    vsp += fd->nlocals;
    for (i = 0; i < fd->nlocals; i++) loc[i] = UNSET_V;
    ncopy = n < npos ? n : npos;
    for (i = 0; i < ncopy; i++) loc[i] = a[i];
    if (n > npos) {
        if (fd->va_slot < 0) {
            if (fd->ndefaults > 0)
                throw_new(cls_TypeError, "%.80s() takes from %d to %d positional arguments but %d %s given",
                          fn->name->s, npos - fd->ndefaults, npos, n, n == 1 ? "was" : "were");
            throw_new(cls_TypeError, "%.80s() takes %d positional argument%s but %d %s given",
                      fn->name->s, npos, npos == 1 ? "" : "s", n, n == 1 ? "was" : "were");
        }
        loc[fd->va_slot] = mktuple_from(a + npos, n - npos);
    } else if (fd->va_slot >= 0) {
        loc[fd->va_slot] = mktuple_from(a, 0);
    }
    if (fd->kw_slot >= 0) {
        kd = dict_new();
        loc[fd->kw_slot] = mkobj(kd, T_DICT);
    }
    for (i = 0; i < nk; i++) {
        Str *nm = AS_STR(kw[2 * i]);
        int j, found = -1;
        for (j = 0; j < npos + fd->nkwonly; j++)
            if (fd->pnames[j] == nm) { found = j; break; }
        if (found >= 0) {
            if (loc[found].t != T_UNSET)
                throw_new(cls_TypeError, "%.80s() got multiple values for argument '%.80s'",
                          fn->name->s, nm->s);
            loc[found] = kw[2 * i + 1];
        } else if (kd) {
            dict_set(kd, kw[2 * i], kw[2 * i + 1]);
        } else {
            throw_new(cls_TypeError, "%.80s() got an unexpected keyword argument '%.80s'",
                      fn->name->s, nm->s);
        }
    }
    if (n < npos || nk > 0) {
        int miss = 0;
        Buf nb;
        buf_init(&nb);
        for (i = 0; i < npos; i++) {
            if (loc[i].t != T_UNSET) continue;
            if (i >= npos - fd->ndefaults) {
                loc[i] = fn->defs[i - (npos - fd->ndefaults)];
            } else {
                if (miss) buf_add(&nb, " and ");
                buf_addc(&nb, '\'');
                buf_add(&nb, fd->pnames[i]->s);
                buf_addc(&nb, '\'');
                miss++;
            }
        }
        if (miss) arg_error(fn, "positional", miss, &nb);
        buf_free(&nb);
    }
    if (fd->nkwonly > 0) {
        int miss = 0;
        Buf nb;
        buf_init(&nb);
        for (i = 0; i < fd->nkwonly; i++) {
            if (loc[npos + i].t != T_UNSET) continue;
            if (fn->kwdefs[i].t != T_UNSET) {
                loc[npos + i] = fn->kwdefs[i];
            } else {
                if (miss) buf_add(&nb, " and ");
                buf_addc(&nb, '\'');
                buf_add(&nb, fd->pnames[npos + i]->s);
                buf_addc(&nb, '\'');
                miss++;
            }
        }
        if (miss) arg_error(fn, "keyword-only", miss, &nb);
        buf_free(&nb);
    }
    return loc;
}

static Value make_gen(Func *fn, FuncDef *fd, Value *loc)
{
    Gen *g = (Gen *)gc_alloc(sizeof(Gen), T_GEN);
    int i;
    g->def = fn->def;
    g->fn = fn;
    g->nloc = fd->nlocals;
    g->loc = (Value *)xmalloc(sizeof(Value) * (size_t)(fd->nlocals + 1));
    for (i = 0; i < fd->nlocals; i++) g->loc[i] = loc[i];
    g->cells = NULL;
    if (fd->nfree > 0) {
        g->cells = (Cell **)xmalloc(sizeof(Cell *) * (size_t)fd->nfree);
        for (i = 0; i < fd->nfree; i++) g->cells[i] = fn->cells[i];
    }
    g->nstate = fd->nstate;
    g->state = (Value *)xmalloc(sizeof(Value) * (size_t)(fd->nstate + 1));
    for (i = 0; i < fd->nstate; i++) g->state[i] = mkint(0);
    g->yielded = NONE_V;
    g->sent = NONE_V;
    g->thrown = NONE_V;
    g->ret = NONE_V;
    g->started = g->done = g->running = 0;
    return mkobj(g, T_GEN);
}

static Value call_func(Func *fn, int n, Value *a, int nk, Value *kw)
{
    FuncDef *fd = fn->def->fd;
    Value *save = vsp, *loc;
    Frame fr;
    int st, i;
    Value ret;
    if (call_depth >= MAX_DEPTH)
        throw_new(cls_RecursionError, "maximum recursion depth exceeded");
    if (nk == 0 && n == fd->npos && fd->nkwonly == 0 && fd->va_slot < 0 && fd->kw_slot < 0 &&
        a + n == vsp) {
        vcheck(fd->nlocals + 8);
        loc = a;
        for (i = n; i < fd->nlocals; i++) loc[i] = UNSET_V;
        vsp = a + fd->nlocals;
    } else {
        loc = bind_args(fn, fd, n, a, nk, kw);
    }
    for (i = 0; i < fd->ncells; i++) {
        int s = fd->cellslots[i];
        loc[s] = mkobj(new_cell(loc[s]), T_CELL);
    }
    if (fd->isgen) {
        ret = make_gen(fn, fd, loc);
        vsp = save;
        return ret;
    }
    fr.loc = loc;
    fr.cells = fn->cells;
    fr.fn = fn;
    fr.ret = NONE_V;
    fr.gen = NULL;
    fr.ns = NULL;
    fr.resuming = 0;
    fr.gp = fd->gpfx;
    fr.up = cur_frame;
    cur_frame = &fr;
    call_depth++;
    GC_POINT();
    st = exec_block(&fr, fd->body);
    ret = st == ST_RETURN ? fr.ret : NONE_V;
    call_depth--;
    cur_frame = fr.up;
    vsp = save;
    return ret;
}

static int gen_resume(Gen *g, Value *out)
{
    Frame fr;
    Handler h;
    Value *save = vsp;
    int depth = call_depth, st = 0;
    if (g->done) return 0;
    if (g->running) throw_new(cls_ValueError, "generator already executing");
    if (call_depth >= MAX_DEPTH)
        throw_new(cls_RecursionError, "maximum recursion depth exceeded");
    fr.loc = g->loc;
    fr.cells = g->cells;
    fr.fn = g->fn;
    fr.ret = NONE_V;
    fr.gen = g;
    fr.ns = NULL;
    fr.resuming = g->started;
    fr.gp = g->def->fd->gpfx;
    fr.up = cur_frame;
    g->started = 1;
    g->running = 1;
    cur_frame = &fr;
    call_depth++;
    h.prev = cur_handler;
    h.sp = save;
    h.depth = depth;
    h.frame = fr.up;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        st = exec_block(&fr, g->def->fd->body);
        cur_handler = h.prev;
    } else {
        cur_handler = h.prev;
        vsp = save;
        call_depth = depth;
        cur_frame = h.frame;
        g->done = 1;
        g->running = 0;
        throw_val(cur_exc);
    }
    call_depth = depth;
    cur_frame = fr.up;
    vsp = save;
    g->running = 0;
    if (st == ST_YIELD) {
        *out = g->yielded;
        return 1;
    }
    g->done = 1;
    g->ret = st == ST_RETURN ? fr.ret : NONE_V;
    return 0;
}

static Value pay_ctor(Class *pc, int n, Value *a, int nk, Value *kw)
{
    Args x;
    x.n = n; x.a = a; x.nk = nk; x.kw = kw;
    return pc->ctor(&x);
}
/* run __init__ of instance iv (class c) */
static void run_init(Value iv, Class *c, int n, Value *a, int nk, Value *kw)
{
    Value *init = class_find(c, S_init);
    Value *base = vsp;
    int i;
    if (init) {
        if (init->t == T_BUILTIN && (n > 0 || nk > 0) && init->u.o == object_init_v.u.o
            && !(c->flags & CF_EXC) && !c->paycls) {
            Value *nw = class_find(c, S_new);
            if (!nw || nw->t != T_FUNC)
                throw_new(cls_TypeError, "%.80s() takes no arguments", c->name->s);
        }
        vcheck(n + 8);
        VPUSH(*init);
        VPUSH(iv);
        for (i = 0; i < n; i++) VPUSH(a[i]);
        call_value(*init, n + 1, base + 1, nk, kw);
        vsp = base;
    }
}
static Value call_class(Class *c, int n, Value *a, int nk, Value *kw)
{
    Inst *in;
    Value iv, *nw;
    Value *base = vsp;
    int i;
    if (c->ctor) {
        Args x;
        x.n = n; x.a = a; x.nk = nk; x.kw = kw;
        return c->ctor(&x);
    }
    nw = class_find(c, S_new);
    if (nw && nw->t == T_FUNC) {
        Value r;
        vcheck(n + 8);
        VPUSH(*nw);
        VPUSH(mkobj(c, T_CLASS));
        for (i = 0; i < n; i++) VPUSH(a[i]);
        r = call_value(*nw, n + 1, base + 1, nk, kw);
        vsp = base;
        if (r.t == T_INST && class_isa(AS_INST(r)->cls, c)) {
            VPUSH(r);
            run_init(r, c, n, a, nk, kw);
            vsp = base;
        }
        return r;
    }
    in = new_inst(c);
    iv = mkobj(in, T_INST);
    VPUSH(iv);
    if (c->paycls) {
        if (c->paycls == cls_tuple || c->paycls == cls_str)
            in->pay = pay_ctor(c->paycls, n, a, nk, kw);
        else
            in->pay = pay_ctor(c->paycls, 0, NULL, 0, NULL);
    }
    if (c->flags & CF_EXC) amap_set(&in->a, S_args, mktuple_from(a, n));
    run_init(iv, c, n, a, nk, kw);
    vsp = base;
    return iv;
}

static Value call_value(Value fn, int n, Value *a, int nk, Value *kw)
{
    switch (fn.t) {
    case T_FUNC: return call_func(AS_FUNC(fn), n, a, nk, kw);
    case T_BUILTIN: {
        Args x;
        if (((Builtin *)fn.u.o)->selfpay && n > 0 && a[0].t == T_INST &&
            AS_INST(a[0])->pay.t != T_UNSET)
            a[0] = AS_INST(a[0])->pay;
        x.n = n; x.a = a; x.nk = nk; x.kw = kw;
        return ((Builtin *)fn.u.o)->fn(&x); }
    case T_CLASS: return call_class(AS_CLASS(fn), n, a, nk, kw);
    case T_BOUND: {
        Bound *b = (Bound *)fn.u.o;
        Value *base = vsp, r;
        int i;
        vcheck(n + 8);
        VPUSH(fn);
        VPUSH(b->self);
        for (i = 0; i < n; i++) VPUSH(a[i]);
        r = call_value(b->fn, n + 1, base + 1, nk, kw);
        vsp = base;
        return r; }
    case T_INST: {
        Value *m = class_find(AS_INST(fn)->cls, S_call);
        if (m) {
            Value *base = vsp, r;
            int i;
            vcheck(n + 8);
            VPUSH(fn);
            for (i = 0; i < n; i++) VPUSH(a[i]);
            r = call_value(*m, n + 1, base, nk, kw);
            vsp = base;
            return r;
        }
        break; }
    default: break;
    }
    throw_new(cls_TypeError, "'%.60s' object is not callable", type_name(fn));
    return NONE_V;
}
static Value call0(Value fn)
{
    Value *base = vsp, r;
    VPUSH(fn);
    r = call_value(fn, 0, base + 1, 0, NULL);
    vsp = base;
    return r;
}
static Value call1(Value fn, Value a)
{
    Value *base = vsp, r;
    VPUSH(fn);
    VPUSH(a);
    r = call_value(fn, 1, base + 1, 0, NULL);
    vsp = base;
    return r;
}
static Value call2(Value fn, Value a, Value b)
{
    Value *base = vsp, r;
    VPUSH(fn);
    VPUSH(a);
    VPUSH(b);
    r = call_value(fn, 2, base + 1, 0, NULL);
    vsp = base;
    return r;
}

/* ---- expression helpers ----------------------------------------------- */
static int identical(Value a, Value b)
{
    if (a.t != b.t) return 0;
    switch (a.t) {
    case T_NONE: return 1;
    case T_BOOL: case T_INT: return a.u.i == b.u.i;
    case T_FLOAT: return a.u.d == b.u.d;
    default: return a.u.o == b.u.o;
    }
}
static int cmp_one(int op, Value a, Value b)
{
    switch (op) {
    case C_IS: return identical(a, b);
    case C_ISNOT: return !identical(a, b);
    case C_IN: return contains(b, a);
    case C_NOTIN: return !contains(b, a);
    default: return rich_cmp(op, a, b);
    }
}

static void eval_slice(Frame *f, Node *s, Value *lo, Value *hi, Value *st)
{
    *lo = s->a ? eval(f, s->a) : NONE_V;
    *hi = s->b ? eval(f, s->b) : NONE_V;
    *st = s->c ? eval(f, s->c) : NONE_V;
}

static Value eval_fstr(Frame *f, Node *n)
{
    Buf b;
    int i;
    Value *base = vsp;
    buf_init(&b);
    for (i = 0; i < n->n; i++) {
        Node *p = n->list[i];
        if (p->kind == N_CONST) {
            buf_addn(&b, AS_STR(p->val)->s, AS_STR(p->val)->len);
        } else {
            Value v = eval(f, p->a), sv;
            VPUSH(v);
            if (p->op == 'r' || p->op == 'a') v = py_repr(v);
            else if (p->op == 's') v = py_str(v);
            VPUSH(v);
            if (p->b) {
                sv = eval(f, p->b);
                sv = format_value(v, AS_STR(sv)->s, AS_STR(sv)->len);
            } else if (v.t == T_STR) {
                sv = v;
            } else {
                sv = format_value(v, "", 0);
            }
            buf_addn(&b, AS_STR(sv)->s, AS_STR(sv)->len);
            vsp = base;
        }
    }
    return buf_value(&b);
}

static Value eval_call(Frame *f, Node *n)
{
    Node *cn = n->a;
    Value *fslot = vsp, *args;
    Value fnv, o;
    int i, mode = 0, np, nk = 0;
    Value *kwbase;
    Value r;
    if (cn->kind == N_NAME && cn->var->kind == VK_GLOBAL && cn->name == S_super &&
        n->n == 0 && G[cn->var->bslot >= 0 ? cn->var->bslot : cn->var->slot].t == T_BUILTIN &&
        (cn->var->bslot < 0 || G[cn->var->slot].t == T_UNSET)) {
        Super *s;
        Value self;
        if (!f->fn || !f->fn->defclass || !f->loc || f->fn->def->fd->npos < 1)
            throw_new(cls_RuntimeError, "super(): no arguments");
        self = f->loc[0];
        if (self.t == T_CELL) self = ((Cell *)self.u.o)->v;
        s = (Super *)gc_alloc(sizeof(Super), T_SUPER);
        s->start = f->fn->defclass;
        s->obj = self;
        return mkobj(s, T_SUPER);
    }
    if (cn->kind == N_ATTR) {
        o = eval(f, cn->a);
        VPUSH(o);
        mode = lookup_method(&o, cn->name, &fnv, cn);
        vsp = fslot;
        VPUSH(fnv);
        args = vsp;
        if (mode) VPUSH(o);
    } else {
        fnv = eval(f, cn);
        VPUSH(fnv);
        args = vsp;
    }
    /* positional and starred arguments */
    for (i = 0; i < n->n; i++) {
        Node *an = n->list[i];
        if (an->kind == N_KW || an->kind == N_DSTAR) continue;
        if (an->kind == N_STAR) {
            Value sv = eval(f, an->a), it, x;
            VPUSH(sv);
            it = get_iter(sv);
            vsp[-1] = it;
            {
                /* collect items above the iterator slot, then move down */
                Value *itslot = vsp - 1;
                while (iter_next(it, &x)) {
                    vcheck(4);
                    VPUSH(x);
                }
                {
                    Value *p;
                    for (p = itslot + 1; p < vsp; p++) p[-1] = *p;
                    vsp--;
                }
            }
        } else {
            Value x = eval(f, an);
            vcheck(4);
            VPUSH(x);
        }
    }
    np = (int)(vsp - args);
    kwbase = vsp;
    for (i = 0; i < n->n; i++) {
        Node *an = n->list[i];
        if (an->kind == N_KW) {
            Value x = eval(f, an->a);
            VPUSH(mkobj(an->name, T_STR));
            VPUSH(x);
            nk++;
        } else if (an->kind == N_DSTAR) {
            Value dv = eval(f, an->a);
            Dict *d;
            long k;
            if (dv.t != T_DICT)
                throw_new(cls_TypeError, "argument after ** must be a mapping, not %.40s", type_name(dv));
            d = AS_DICT(dv);
            VPUSH(dv);
            for (k = 0; k < d->used; k++) {
                if (d->e[k].k.t == T_UNSET) continue;
                if (d->e[k].k.t != T_STR) throw_new(cls_TypeError, "keywords must be strings");
                vcheck(4);
                VPUSH(mkobj(intern_str(AS_STR(d->e[k].k)), T_STR));
                VPUSH(d->e[k].v);
                nk++;
            }
        }
    }
    if (nk) {
        /* compact: the dict roots pushed for ** sit between pairs; rebuild pairs contiguously */
        Value *p, *q = kwbase;
        for (p = kwbase; p < vsp; ) {
            if (p->t == T_DICT) { p++; continue; }
            q[0] = p[0]; q[1] = p[1];
            q += 2; p += 2;
        }
        vsp = q;
    }
    r = call_value(fnv, np, args, nk, kwbase);
    vsp = fslot;
    return r;
}

static void unpack_assign(Frame *f, Node *t, Value x);
static void assign_target(Frame *f, Node *t, Value x)
{
    switch (t->kind) {
    case N_NAME: store_var(f, t->var, x); break;
    case N_ATTR: {
        Value *base = vsp, o;
        VPUSH(x);
        o = eval(f, t->a);
        set_attr(o, t->name, x);
        vsp = base;
        break; }
    case N_SUBSCR: {
        Value *base = vsp, o, idx;
        VPUSH(x);
        o = eval(f, t->a);
        VPUSH(o);
        if (t->b->kind == N_SLICE) {
            Value lo, hi, st;
            eval_slice(f, t->b, &lo, &hi, &st);
            slice_set(o, lo, hi, st, x);
        } else {
            idx = eval(f, t->b);
            VPUSH(idx);
            setitem(o, idx, x);
        }
        vsp = base;
        break; }
    case N_TUPLE: case N_LIST: unpack_assign(f, t, x); break;
    default: throw_new(cls_SyntaxError, "cannot assign to expression");
    }
}
static void unpack_assign(Frame *f, Node *t, Value x)
{
    Value *base = vsp, *items;
    long cnt, i, star = -1, nt = t->n;
    VPUSH(x);
    items = vsp;
    if (x.t == T_LIST || x.t == T_TUPLE) {
        List *l = AS_LIST(x);
        vcheck(l->n + 4);
        for (i = 0; i < l->n; i++) VPUSH(l->a[i]);
    } else {
        Value it, y;
        it = get_iter(x);
        VPUSH(it);
        items = vsp;
        while (iter_next(it, &y)) { vcheck(4); VPUSH(y); }
    }
    cnt = vsp - items;
    for (i = 0; i < nt; i++) if (t->list[i]->kind == N_STAR) star = i;
    if (star < 0) {
        if (cnt < nt)
            throw_new(cls_ValueError, "not enough values to unpack (expected %ld, got %ld)", nt, cnt);
        if (cnt > nt)
            throw_new(cls_ValueError, "too many values to unpack (expected %ld)", nt);
        for (i = 0; i < nt; i++) assign_target(f, t->list[i], items[i]);
    } else {
        long after = nt - star - 1;
        List *mid;
        Value mv;
        if (cnt < nt - 1)
            throw_new(cls_ValueError, "not enough values to unpack (expected at least %ld, got %ld)",
                      nt - 1, cnt);
        for (i = 0; i < star; i++) assign_target(f, t->list[i], items[i]);
        mid = list_new(cnt - star - after);
        for (i = star; i < cnt - after; i++) list_push(mid, items[i]);
        mv = mklist(mid);
        VPUSH(mv);
        assign_target(f, t->list[star]->a, mv);
        for (i = 0; i < after; i++) assign_target(f, t->list[star + 1 + i], items[cnt - after + i]);
    }
    vsp = base;
}

static Value eval_binop_fast(Frame *f, Node *n)
{
    Value l = eval(f, n->a), r;
    if (l.t >= T_STR) {
        VPUSH(l);
        r = eval(f, n->b);
        vsp--;
    } else {
        r = eval(f, n->b);
    }
    if (l.t == T_INT && r.t == T_INT) {
        long x = l.u.i, y = r.u.i, z;
        switch (n->op) {
        case B_ADD: z = x + y; if (z <= SI_MAX && z >= -SI_MAX) return mkint(z); break;
        case B_SUB: z = x - y; if (z <= SI_MAX && z >= -SI_MAX) return mkint(z); break;
        case B_MUL:
            if (x < SI_MULLIM && x > -SI_MULLIM && y < SI_MULLIM && y > -SI_MULLIM)
                return mkint(x * y);
            break;
        case B_FDIV: case B_MOD:
            if (y != 0) {
                long q = x / y, m = x % y;
                if (m != 0 && ((m < 0) != (y < 0))) { q--; m += y; }
                return mkint(n->op == B_FDIV ? q : m);
            }
            break;
        default: break;
        }
    } else if (l.t == T_FLOAT && r.t == T_FLOAT) {
        switch (n->op) {
        case B_ADD: return mkfloat(l.u.d + r.u.d);
        case B_SUB: return mkfloat(l.u.d - r.u.d);
        case B_MUL: return mkfloat(l.u.d * r.u.d);
        default: break;
        }
    }
    return binop(n->op, l, r);
}

static Value eval(Frame *f, Node *n)
{
    switch (n->kind) {
    case N_CONST: return n->val;
    case N_NAME: {
        Var *v = n->var;
        if (v->kind == VK_LOCAL) {
            Value x = f->loc[v->slot];
            if (x.t != T_UNSET) return x;
        } else if (v->kind == VK_GLOBAL) {
            Value x = G[v->slot];
            if (x.t != T_UNSET) return x;
            if (v->bslot >= 0) {
                x = G[v->bslot];
                if (x.t != T_UNSET) return x;
            }
        }
        return load_var_slow(f, v); }
    case N_ATTR: {
        Value o = eval(f, n->a);
        if (o.t == T_INST) {
            Value *p = amap_get(&AS_INST(o)->a, n->name);
            if (p) return *p;
        }
        return get_attr(o, n->name); }
    case N_SUBSCR: {
        Value o = eval(f, n->a), idx;
        Value *base = vsp;
        VPUSH(o);
        if (n->b->kind == N_SLICE) {
            Value lo, hi, st;
            eval_slice(f, n->b, &lo, &hi, &st);
            vsp = base;
            return slice_get(o, lo, hi, st);
        }
        idx = eval(f, n->b);
        vsp = base;
        if (o.t == T_LIST && idx.t == T_INT) {
            List *l = AS_LIST(o);
            long i = idx.u.i;
            if (i < 0) i += l->n;
            if (i >= 0 && i < l->n) return l->a[i];
        }
        VPUSH(o);
        VPUSH(idx);
        o = subscr(o, idx);
        vsp = base;
        return o; }
    case N_SLICE: {
        Value lo, hi, st;
        eval_slice(f, n, &lo, &hi, &st);
        return mkslice(lo, hi, st); }
    case N_CALL: return eval_call(f, n);
    case N_BINOP: return eval_binop_fast(f, n);
    case N_UNARY: return unop(n->op, eval(f, n->a));
    case N_NOT: return mkbool(!py_truthy(eval(f, n->a)));
    case N_AND: {
        Value l = eval(f, n->a);
        if (!py_truthy(l)) return l;
        return eval(f, n->b); }
    case N_OR: {
        Value l = eval(f, n->a);
        if (py_truthy(l)) return l;
        return eval(f, n->b); }
    case N_WALRUS: {
        Value wv = eval(f, n->b);
        VPUSH(wv);
        store_var(f, n->a->var, wv);
        vsp--;
        return wv;
    }
    case N_IFEXP:
        return py_truthy(eval(f, n->a)) ? eval(f, n->b) : eval(f, n->c);
    case N_CMP: {
        Value left = eval(f, n->list[0]), right;
        int i, nops = n->op;
        for (i = 0; i < nops; i++) {
            Value *base = vsp;
            int r;
            VPUSH(left);
            right = eval(f, n->list[i + 1]);
            VPUSH(right);
            if (left.t == T_INT && right.t == T_INT && n->name->s[i] <= C_NE) {
                long x = left.u.i, y = right.u.i;
                switch (n->name->s[i]) {
                case C_LT: r = x < y; break;
                case C_LE: r = x <= y; break;
                case C_GT: r = x > y; break;
                case C_GE: r = x >= y; break;
                case C_EQ: r = x == y; break;
                default: r = x != y; break;
                }
            } else if ((left.t == T_ND || right.t == T_ND) && left.t != T_INST && right.t != T_INST && n->name->s[i] <= C_NE) {
                Value cv = nd_compare(n->name->s[i], left, right);
                vsp = base;
                return cv;
            } else if (nops == 1 && n->name->s[i] <= C_NE && (left.t == T_INST || right.t == T_INST)) {
                Value rv;
                if (inst_cmp_value(n->name->s[i], left, right, &rv)) {
                    vsp = base;
                    return rv;
                }
                if (left.t == T_ND || right.t == T_ND) {
                    Value cv = nd_compare(n->name->s[i], left, right);
                    vsp = base;
                    return cv;
                }
                r = cmp_one(n->name->s[i], left, right);
            } else {
                r = cmp_one(n->name->s[i], left, right);
            }
            vsp = base;
            if (!r) return FALSE_V;
            left = right;
        }
        return TRUE_V; }
    case N_LIST: {
        List *l = list_new(n->n);
        Value lv = mklist(l);
        int i;
        VPUSH(lv);
        for (i = 0; i < n->n; i++) {
            Node *c = n->list[i];
            if (c->kind == N_STAR) {
                Value sv = eval(f, c->a), it, x;
                VPUSH(sv);
                it = get_iter(sv);
                VPUSH(it);
                while (iter_next(it, &x)) list_push(l, x);
                vsp -= 2;
            } else {
                list_push(l, eval(f, c));
            }
        }
        vsp--;
        return lv; }
    case N_TUPLE: {
        int i, star = 0;
        for (i = 0; i < n->n; i++) if (n->list[i]->kind == N_STAR) star = 1;
        if (!star) {
            List *t = tuple_new(n->n);
            Value tv = mkobj(t, T_TUPLE);
            for (i = 0; i < n->n; i++) t->a[i] = NONE_V;
            VPUSH(tv);
            for (i = 0; i < n->n; i++) t->a[i] = eval(f, n->list[i]);
            vsp--;
            return tv;
        } else {
            List *l = list_new(n->n);
            Value lv = mklist(l), tv;
            VPUSH(lv);
            for (i = 0; i < n->n; i++) {
                Node *c = n->list[i];
                if (c->kind == N_STAR) {
                    Value sv = eval(f, c->a), it, x;
                    VPUSH(sv);
                    it = get_iter(sv);
                    VPUSH(it);
                    while (iter_next(it, &x)) list_push(l, x);
                    vsp -= 2;
                } else list_push(l, eval(f, c));
            }
            tv = mktuple_from(l->a, l->n);
            vsp--;
            return tv;
        } }
    case N_SET: {
        Dict *d = dict_new_t(T_SET);
        Value dv = mkobj(d, T_SET);
        int i;
        VPUSH(dv);
        for (i = 0; i < n->n; i++) {
            Node *c = n->list[i];
            if (c->kind == N_STAR) {
                Value sv = eval(f, c->a), it, x;
                VPUSH(sv);
                it = get_iter(sv);
                VPUSH(it);
                while (iter_next(it, &x)) set_add(d, x);
                vsp -= 2;
            } else set_add(d, eval(f, c));
        }
        vsp--;
        return dv; }
    case N_DICT: {
        Dict *d = dict_new();
        Value dv = mkobj(d, T_DICT);
        int i;
        VPUSH(dv);
        for (i = 0; i < n->n; i += 2) {
            Node *kn = n->list[i];
            if (kn->kind == N_DSTAR) {
                Value sv = eval(f, kn->a);
                long k;
                if (sv.t != T_DICT) throw_new(cls_TypeError, "'%.40s' object is not a mapping", type_name(sv));
                for (k = 0; k < AS_DICT(sv)->used; k++)
                    if (AS_DICT(sv)->e[k].k.t != T_UNSET)
                        dict_set(d, AS_DICT(sv)->e[k].k, AS_DICT(sv)->e[k].v);
            } else {
                Value k = eval(f, kn), v;
                VPUSH(k);
                v = eval(f, n->list[i + 1]);
                vsp--;
                dict_set(d, k, v);
            }
        }
        vsp--;
        return dv; }
    case N_LAMBDA: return mkobj(make_function(f, n), T_FUNC);
    case N_COMP: {
        Value *base = vsp, it, fv, r;
        it = get_iter(eval(f, n->a));
        VPUSH(it);
        fv = mkobj(make_function(f, n), T_FUNC);
        VPUSH(fv);
        VPUSH(it);
        r = call_func(AS_FUNC(fv), 1, vsp - 1, 0, NULL);
        vsp = base;
        return r; }
    case N_FSTR: return eval_fstr(f, n);
    case N_STAR: throw_new(cls_SyntaxError, "can't use starred expression here"); return NONE_V;
    default: break;
    }
    throw_new(cls_RuntimeError, "bad expression node %d", (int)n->kind);
    return NONE_V;
}

/* ---- statements ------------------------------------------------------- */
static int exc_matches(Frame *f, Value exc, Node *tn)
{
    Value t = eval(f, tn);
    Class *ec = class_of(exc);
    if (t.t == T_CLASS) return class_isa(ec, AS_CLASS(t));
    if (t.t == T_TUPLE) {
        List *l = AS_LIST(t);
        long i;
        for (i = 0; i < l->n; i++)
            if (l->a[i].t == T_CLASS && class_isa(ec, AS_CLASS(l->a[i]))) return 1;
        return 0;
    }
    throw_new(cls_TypeError, "catching classes that do not inherit from BaseException is not allowed");
    return 0;
}

static void do_raise(Value v)
{
    if (v.t == T_CLASS) {
        if (!(AS_CLASS(v)->flags & CF_EXC))
            throw_new(cls_TypeError, "exceptions must derive from BaseException");
        v = call0(v);
    }
    if (v.t != T_INST || !(AS_INST(v)->cls->flags & CF_EXC))
        throw_new(cls_TypeError, "exceptions must derive from BaseException");
    throw_val(v);
}

static int exec_try(Frame *f, Node *n)
{
    int phase = 0, pend = 0, st = 0, k;
    Value *sp0 = vsp, exc;
    Value *slot;
    VPUSH(NONE_V);
    slot = sp0;
    if (f->resuming) {
        phase = (int)STATE(f, n, 0).u.i;
        pend = (int)STATE(f, n, 1).u.i;
        *slot = STATE(f, n, 2);
    }
#define TRY_SAVE() do { if (f->gen) { STATE(f, n, 0) = mkint(phase); \
        STATE(f, n, 1) = mkint(pend); STATE(f, n, 2) = *slot; } } while (0)
    if (phase == 0) {
        if (protected_block(f, n->a, &st, &exc)) {
            Value saved_h = handled_exc;
            int matched = -1;
            *slot = exc;
            for (k = 0; k < n->n; k++) {
                Node *h = n->list[k];
                if (!h->a || exc_matches(f, exc, h->a)) { matched = k; break; }
            }
            if (matched < 0) {
                pend = 100;
                phase = 3;
                if (!n->d) { vsp = sp0; throw_val(exc); }
            } else {
                Node *h = n->list[matched];
                phase = 10 + matched;
                if (h->b) store_var(f, h->b->var, exc);
            }
            (void)saved_h;
        } else if (st == ST_YIELD) {
            TRY_SAVE();
            vsp = sp0;
            return ST_YIELD;
        } else if (st == 0 && n->c) {
            phase = 2;
        } else {
            phase = 3;
            if (st) { pend = 10 + st; *slot = f->ret; }
        }
    }
    if (phase >= 10) {
        Node *h = n->list[phase - 10];
        Value saved_h = handled_exc;
        if (!f->resuming || 1) handled_exc = *slot;
        if (n->d) {
            if (protected_block(f, h->c, &st, &exc)) {
                handled_exc = saved_h;
                pend = 100; *slot = exc; phase = 3;
            } else if (st == ST_YIELD) {
                TRY_SAVE();
                vsp = sp0;
                return ST_YIELD;
            } else {
                handled_exc = saved_h;
                if (h->b) store_var(f, h->b->var, UNSET_V);
                phase = 3;
                pend = 0;
                if (st) { pend = 10 + st; *slot = f->ret; }
            }
        } else {
            st = exec_block(f, h->c);
            if (st == ST_YIELD) {
                TRY_SAVE();
                vsp = sp0;
                return ST_YIELD;
            }
            handled_exc = saved_h;
            if (h->b) store_var(f, h->b->var, UNSET_V);
            vsp = sp0;
            return st;
        }
    }
    if (phase == 2) {
        if (n->d) {
            if (protected_block(f, n->c, &st, &exc)) {
                pend = 100; *slot = exc; phase = 3;
            } else if (st == ST_YIELD) {
                TRY_SAVE();
                vsp = sp0;
                return ST_YIELD;
            } else {
                phase = 3;
                pend = 0;
                if (st) { pend = 10 + st; *slot = f->ret; }
            }
        } else {
            st = exec_block(f, n->c);
            if (st == ST_YIELD) { TRY_SAVE(); vsp = sp0; return ST_YIELD; }
            vsp = sp0;
            return st;
        }
    }
    /* finally */
    if (n->d) {
        st = exec_block(f, n->d);
        if (st == ST_YIELD) {
            TRY_SAVE();
            vsp = sp0;
            return ST_YIELD;
        }
        if (st != 0) { vsp = sp0; return st; }
    }
    if (pend == 100) {
        exc = *slot;
        vsp = sp0;
        throw_val(exc);
    }
    if (pend >= 10) {
        f->ret = *slot;
        vsp = sp0;
        return pend - 10;
    }
    vsp = sp0;
    return 0;
#undef TRY_SAVE
}

static int exec_with(Frame *f, Node *n)
{
    Value *sp0 = vsp, mgr, exc, *m;
    int st = 0;
    VPUSH(NONE_V);
    if (f->resuming) {
        mgr = STATE(f, n, 1);
    } else {
        Value ent;
        mgr = eval(f, n->a);
        sp0[0] = mgr;
        m = class_find(class_of(mgr), S_enter);
        if (!m) throw_new(cls_AttributeError, "__enter__");
        ent = call1(*m, mgr);
        if (f->gen) STATE(f, n, 1) = mgr;
        if (n->b) assign_target(f, n->b, ent);
    }
    sp0[0] = mgr;
    if (protected_block(f, n->c, &st, &exc)) {
        Value args[3], r;
        m = class_find(class_of(mgr), S_exit);
        args[0] = mkobj(AS_INST(exc)->cls, T_CLASS);
        args[1] = exc;
        args[2] = NONE_V;
        VPUSH(exc);
        {
            Value *base = vsp;
            VPUSH(*m); VPUSH(mgr); VPUSH(args[0]); VPUSH(args[1]); VPUSH(args[2]);
            r = call_value(*m, 4, base + 1, 0, NULL);
            vsp = base;
        }
        if (py_truthy(r)) { vsp = sp0; return 0; }
        vsp = sp0;
        throw_val(exc);
    }
    if (st == ST_YIELD) { vsp = sp0; return ST_YIELD; }
    {
        Value *base = vsp;
        m = class_find(class_of(mgr), S_exit);
        VPUSH(*m); VPUSH(mgr); VPUSH(NONE_V); VPUSH(NONE_V); VPUSH(NONE_V);
        call_value(*m, 4, base + 1, 0, NULL);
        vsp = base;
    }
    vsp = sp0;
    return st;
}

/* create a class object; `bl` (malloc'ed, nb >= 1 entries) is owned by the class */
static Class *make_class(Str *name, Class **bl, int nb, Str *modname)
{
    Class *c;
    int i, fl = 0;
    for (i = 0; i < nb; i++) fl |= bl[i]->flags & (CF_EXC | CF_PROPS | CF_SETATTR);
    c = (Class *)gc_alloc(sizeof(Class), T_CLASS);
    c->name = name;
    c->base = bl[0];
    c->ns.k = NULL; c->ns.v = NULL; c->ns.n = c->ns.cap = 0;
    c->ctor = NULL;
    c->flags = fl;
    c->mro = NULL; c->nmro = 0; c->bases = NULL; c->nbases = 0; c->paycls = NULL;
    c->modname = modname;
    c->bases = bl;
    c->nbases = nb;
    VPUSH(mkobj(c, T_CLASS));
    c3_build(c, bl, nb);
    vsp--;
    for (i = 0; i < c->nmro && !c->paycls; i++) {
        Class *k = c->mro[i];
        if (k == cls_list || k == cls_dict || k == cls_set || k == cls_tuple || k == cls_str)
            c->paycls = k;
    }
    return c;
}
/* after the namespace is filled: bind methods to the class, set flags */
static void finish_class(Class *c)
{
    int i;
    for (i = 0; i < c->ns.n; i++) {
        Value v = c->ns.v[i];
        if (v.t == T_FUNC) AS_FUNC(v)->defclass = c;
        else if (v.t == T_SMETH || v.t == T_CMETH || v.t == T_PROP) {
            Prop *p = (Prop *)v.u.o;
            if (p->get.t == T_FUNC) AS_FUNC(p->get)->defclass = c;
            if (p->set.t == T_FUNC) AS_FUNC(p->set)->defclass = c;
            if (v.t == T_PROP) c->flags |= CF_PROPS;
        }
    }
    if (amap_get(&c->ns, S_setattr)) c->flags |= CF_SETATTR;
    class_epoch++;
}
/* call __init_subclass__ of the first base (implicit classmethod) */
static void run_init_subclass(Class *c, Class *base, Value cv)
{
    Value *m = class_find(base, intern_c("__init_subclass__"));
    if (!m) return;
    if (m->t == T_CMETH) call1(((Prop *)m->u.o)->get, cv);
    else if (m->t == T_FUNC) call1(*m, cv);
    (void)c;
}
static void exec_class(Frame *f, Node *n)
{
    FuncDef *fd = n->fd;
    Class *c;
    Value cv, *basev = vsp;
    int i, nbases = n->n - n->op, nb = 0;
    Frame cf;
    Cell **cells = NULL;
    Class **bl;
    bl = (Class **)xmalloc(sizeof(Class *) * (size_t)(nbases + 1));
    for (i = 0; i < nbases; i++) {
        Value b = eval(f, n->list[i]);
        VPUSH(b);
        if (b.t != T_CLASS) { free(bl); throw_new(cls_TypeError, "bases must be types"); }
        bl[nb++] = AS_CLASS(b);
    }
    if (nb == 0) bl[nb++] = cls_object;
    c = make_class(n->name, bl, nb, modname_of_gp(f->gp));
    cv = mkobj(c, T_CLASS);
    VPUSH(cv);
    if (fd->nfree > 0) {
        cells = (Cell **)xmalloc(sizeof(Cell *) * (size_t)fd->nfree);
        for (i = 0; i < fd->nfree; i++) {
            Var *src = fd->fv[i]->src;
            if (src->kind == VK_CELL) cells[i] = (Cell *)f->loc[src->slot].u.o;
            else cells[i] = f->cells[src->slot];
        }
    }
    cf.loc = NULL;
    cf.cells = cells;
    cf.fn = NULL;
    cf.ret = NONE_V;
    cf.gen = NULL;
    cf.ns = c;
    cf.resuming = 0;
    cf.gp = fd->gpfx;
    cf.up = cur_frame;
    cur_frame = &cf;
    exec_block(&cf, fd->body);
    cur_frame = cf.up;
    free(cells);
    finish_class(c);
    run_init_subclass(c, c->base, cv);
    /* decorators (rare) */
    for (i = n->n - 1; i >= nbases; i--) {
        Value d = eval(f, n->list[i]);
        cv = call1(d, cv);
        vsp[-1] = cv;
    }
    store_var(f, n->var, cv);
    vsp = basev;
}

static int exec_for(Frame *f, Node *n)
{
    Value it, x;
    Value *sp0 = vsp;
    int st, skip = 0, inelse = 0;
    if (f->resuming) {
        inelse = (int)STATE(f, n, 0).u.i;
        it = STATE(f, n, 1);
        if (!inelse) skip = 1;
    } else {
        it = get_iter(eval(f, n->b));
        if (f->gen) { STATE(f, n, 1) = it; STATE(f, n, 0) = mkint(0); }
    }
    if (!f->gen) VPUSH(it);
    if (!inelse) {
        for (;;) {
            if (!skip) {
                GC_POINT();
                if (!iter_next(it, &x)) break;
                if (n->a->kind == N_NAME && n->a->var->kind == VK_LOCAL)
                    f->loc[n->a->var->slot] = x;
                else
                    assign_target(f, n->a, x);
            }
            skip = 0;
            st = exec_block(f, n->c);
            if (st == ST_BREAK) { vsp = sp0; return 0; }
            if (st == ST_CONTINUE || st == 0) continue;
            vsp = sp0;
            return st;
        }
    }
    if (n->d) {
        if (f->gen) STATE(f, n, 0) = mkint(1);
        st = exec_block(f, n->d);
        vsp = sp0;
        return st;
    }
    vsp = sp0;
    return 0;
}

static int exec_while(Frame *f, Node *n)
{
    int st, skip = 0, inelse = 0;
    if (f->resuming) {
        inelse = (int)STATE(f, n, 0).u.i;
        if (!inelse) skip = 1;
    } else if (f->gen) STATE(f, n, 0) = mkint(0);
    if (!inelse) {
        for (;;) {
            if (!skip) {
                GC_POINT();
                if (!py_truthy(eval(f, n->a))) break;
            }
            skip = 0;
            st = exec_block(f, n->b);
            if (st == ST_BREAK) return 0;
            if (st == ST_CONTINUE || st == 0) continue;
            return st;
        }
    }
    if (n->c) {
        if (f->gen) STATE(f, n, 0) = mkint(1);
        return exec_block(f, n->c);
    }
    return 0;
}

static void exec_aug(Frame *f, Node *n)
{
    Node *t = n->a;
    Value *base = vsp, cur, rhs, r;
    switch (t->kind) {
    case N_NAME: {
        cur = load_var_slow(f, t->var);
        VPUSH(cur);
        rhs = eval(f, n->b);
        VPUSH(rhs);
        if (cur.t == T_LIST && n->op == B_ADD && (rhs.t == T_LIST || rhs.t == T_TUPLE)) {
            List *l = AS_LIST(cur), *s = AS_LIST(rhs);
            long i, m = s->n;
            for (i = 0; i < m; i++) list_push(l, s->a[i]);
            r = cur;
        } else r = inplace_binop(n->op, cur, rhs);
        if (cur.t == T_ND && r.t == T_ND && n->op != B_MATMUL) { nd_assign_all(cur, r); r = cur; }
        store_var(f, t->var, r);
        break; }
    case N_ATTR: {
        Value o = eval(f, t->a);
        VPUSH(o);
        cur = get_attr(o, t->name);
        VPUSH(cur);
        rhs = eval(f, n->b);
        VPUSH(rhs);
        r = inplace_binop(n->op, cur, rhs);
        VPUSH(r);
        set_attr(o, t->name, r);
        break; }
    case N_SUBSCR: {
        Value o = eval(f, t->a), idx;
        VPUSH(o);
        idx = eval(f, t->b);
        VPUSH(idx);
        cur = subscr(o, idx);
        VPUSH(cur);
        rhs = eval(f, n->b);
        VPUSH(rhs);
        r = inplace_binop(n->op, cur, rhs);
        VPUSH(r);
        setitem(o, idx, r);
        break; }
    default: break;
    }
    vsp = base;
}

static void exec_del(Frame *f, Node *t)
{
    Value *base = vsp;
    switch (t->kind) {
    case N_NAME: store_var(f, t->var, UNSET_V); break;
    case N_ATTR: {
        Value o = eval(f, t->a);
        del_attr(o, t->name);
        break; }
    case N_SUBSCR: {
        Value o = eval(f, t->a);
        VPUSH(o);
        if (t->b->kind == N_SLICE) {
            Value lo, hi, st;
            eval_slice(f, t->b, &lo, &hi, &st);
            slice_del(o, lo, hi, st);
        } else {
            Value idx = eval(f, t->b);
            VPUSH(idx);
            delitem(o, idx);
        }
        break; }
    case N_TUPLE: case N_LIST: {
        int i;
        for (i = 0; i < t->n; i++) exec_del(f, t->list[i]);
        break; }
    default: break;
    }
    vsp = base;
}

static int exec_yieldfrom(Frame *f, Node *n)
{
    Value it, x;
    Gen *g = f->gen;
    int more;
    if (f->resuming) {
        it = STATE(f, n, 0);
        f->resuming = 0;
        if (g->thrown.t == T_INST) {
            Value e = g->thrown;
            g->thrown = NONE_V;
            if (it.t != T_GEN) throw_val(e);
            ((Gen *)it.u.o)->thrown = e;
        }
        if (it.t == T_GEN) {
            ((Gen *)it.u.o)->sent = g->sent;
            g->sent = NONE_V;
            more = gen_resume((Gen *)it.u.o, &x);
        } else {
            g->sent = NONE_V;
            more = iter_next(it, &x);
        }
    } else {
        it = get_iter(eval(f, n->a));
        STATE(f, n, 0) = it;
        more = iter_next(it, &x);
    }
    if (more) {
        g->yielded = x;
        return ST_YIELD;
    }
    if (n->b) {
        Value rv = NONE_V;
        if (it.t == T_GEN) rv = ((Gen *)it.u.o)->ret;
        assign_target(f, n->b, rv);
    }
    return 0;
}

static int exec_block(Frame *f, Node *b)
{
    int i = 0, st;
    if (f->resuming) i = (int)STATE(f, b, 0).u.i;
    for (; i < b->n; i++) {
        if (f->gen) STATE(f, b, 0) = mkint(i);
        st = exec(f, b->list[i]);
        if (st) return st;
    }
    return 0;
}

static int exec(Frame *f, Node *n)
{
    cur_line = n->line;
    switch (n->kind) {
    case S_EXPR: {
        Value v = eval(f, n->a);
        if (repl_echo && !f->fn && !f->ns && v.t != T_NONE) {
            Value r = py_repr(v);
            fwrite(AS_STR(r)->s, 1, (size_t)AS_STR(r)->len, stdout);
            fputc('\n', stdout);
        }
        return 0; }
    case S_ASSIGN: {
        Value v = eval(f, n->a);
        int i;
        if (n->n == 1) {
            Node *t = n->list[0];
            if (t->kind == N_NAME && t->var->kind == VK_LOCAL) f->loc[t->var->slot] = v;
            else assign_target(f, t, v);
        } else {
            Value *base = vsp;
            VPUSH(v);
            for (i = 0; i < n->n; i++) assign_target(f, n->list[i], v);
            vsp = base;
        }
        return 0; }
    case S_AUG:
        if (n->a->kind == N_NAME && n->a->var->kind == VK_LOCAL) {
            Value cur = f->loc[n->a->var->slot];
            if (cur.t == T_INT) {
                Value rhs = eval(f, n->b);
                if (rhs.t == T_INT && (n->op == B_ADD || n->op == B_SUB)) {
                    long z = n->op == B_ADD ? cur.u.i + rhs.u.i : cur.u.i - rhs.u.i;
                    if (z <= SI_MAX && z >= -SI_MAX) { f->loc[n->a->var->slot] = mkint(z); return 0; }
                }
                {
                    Value *base = vsp;
                    VPUSH(rhs);
                    f->loc[n->a->var->slot] = binop(n->op, cur, rhs);
                    vsp = base;
                }
                return 0;
            }
        }
        exec_aug(f, n);
        return 0;
    case S_IF: {
        int br;
        if (f->resuming) br = (int)STATE(f, n, 0).u.i;
        else {
            br = py_truthy(eval(f, n->a)) ? 1 : 2;
            if (f->gen) STATE(f, n, 0) = mkint(br);
        }
        if (br == 1) return exec_block(f, n->b);
        if (n->c) return exec_block(f, n->c);
        return 0; }
    case S_WHILE: return exec_while(f, n);
    case S_FOR: return exec_for(f, n);
    case S_BREAK: return ST_BREAK;
    case S_CONTINUE: return ST_CONTINUE;
    case S_RETURN:
        f->ret = n->a ? eval(f, n->a) : NONE_V;
        return ST_RETURN;
    case S_PASS: case S_NOP: return 0;
    case S_DEF: {
        Value fv = mkobj(make_function(f, n), T_FUNC);
        Value *base = vsp;
        int i;
        VPUSH(fv);
        for (i = n->n - 1; i >= 0; i--) {
            Value d = eval(f, n->list[i]);
            fv = call1(d, fv);
            vsp[-1] = fv;
        }
        store_var(f, n->var, fv);
        vsp = base;
        return 0; }
    case S_CLASS: exec_class(f, n); return 0;
    case S_TRY: return exec_try(f, n);
    case S_RAISE:
        if (n->a) do_raise(eval(f, n->a));
        if (handled_exc.t == T_NONE || handled_exc.t == T_UNSET)
            throw_new(cls_RuntimeError, "No active exception to reraise");
        throw_val(handled_exc);
        return 0;
    case S_ASSERT:
        if (!py_truthy(eval(f, n->a))) {
            if (n->b) {
                Value m = eval(f, n->b);
                throw_val(new_exc_args(cls_AssertionError, &m, 1));
            }
            throw_val(new_exc_args(cls_AssertionError, NULL, 0));
        }
        return 0;
    case S_DEL: {
        int i;
        for (i = 0; i < n->n; i++) exec_del(f, n->list[i]);
        return 0; }
    case S_IMPORT: exec_import(f, n); return 0;
    case S_FROM: exec_from(f, n); return 0;
    case S_WITH: return exec_with(f, n);
    case S_BLOCK: return exec_block(f, n);
    case S_YIELD:
        if (f->resuming) {
            Gen *g = f->gen;
            f->resuming = 0;
            if (g->thrown.t == T_INST) {
                Value e = g->thrown;
                g->thrown = NONE_V;
                throw_val(e);
            }
            if (n->b) {
                Value sv = g->sent;
                g->sent = NONE_V;
                assign_target(f, n->b, sv);
            }
            return 0;
        }
        f->gen->yielded = n->a ? eval(f, n->a) : NONE_V;
        return ST_YIELD;
    case S_YIELDFROM: return exec_yieldfrom(f, n);
    case S_LISTAPP: {
        Value l = eval(f, n->a);
        list_push(AS_LIST(l), eval(f, n->b));
        return 0; }
    case S_SETADD: {
        Value l = eval(f, n->a);
        set_add(AS_DICT(l), eval(f, n->b));
        return 0; }
    case S_DICTSET: {
        Value l = eval(f, n->a), k = eval(f, n->b), v;
        VPUSH(k);
        v = eval(f, n->c);
        vsp--;
        dict_set(AS_DICT(l), k, v);
        return 0; }
    default: break;
    }
    throw_new(cls_RuntimeError, "bad statement node %d", (int)n->kind);
    return 0;
}

/* ---- running source text ---------------------------------------------- */
static const char *script_name = "<stdin>";

static void print_exception(Value exc)
{
    Buf b;
    Inst *in = AS_INST(exc);
    fflush(stdout);
    fprintf(stderr, "Traceback (most recent call last):\n  File \"%s\", line %d\n",
            script_name, exc_line);
    buf_init(&b);
    str_buf(exc, &b);
    if (b.n > 0) fprintf(stderr, "%s: %s\n", in->cls->name->s, b.s);
    else fprintf(stderr, "%s\n", in->cls->name->s);
    buf_free(&b);
}

static void run_body(const char *src, long len)
{
    Node *blk = compile_src(src, len, main_gp);
    run_block_in(blk, main_gp);
}

/* returns 0 on success, 1 on an uncaught exception (already reported) */
static int run_source(const char *src, long len)
{
    Handler h;
    h.prev = cur_handler;
    h.sp = vsp;
    h.depth = call_depth;
    h.frame = NULL;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        run_body(src, len);
        cur_handler = h.prev;
        return 0;
    }
    cur_handler = h.prev;
    vsp = h.sp;
    call_depth = h.depth;
    cur_frame = NULL;
    gc_perm = 0;
    {
        Value exc = cur_exc;
        if (AS_INST(exc)->cls == cls_SystemExit || class_isa(AS_INST(exc)->cls, cls_SystemExit)) {
            Value *ap = amap_get(&AS_INST(exc)->a, S_args);
            int code = 0;
            fflush(stdout);
            if (ap && ap->t == T_TUPLE && AS_LIST(*ap)->n > 0) {
                Value a0 = AS_LIST(*ap)->a[0];
                if (a0.t == T_INT) code = (int)a0.u.i;
                else if (a0.t != T_NONE) {
                    Value s = py_str(a0);
                    fprintf(stderr, "%s\n", AS_STR(s)->s);
                    code = 1;
                }
            }
            exit(code);
        }
        print_exception(exc);
    }
    return 1;
}
