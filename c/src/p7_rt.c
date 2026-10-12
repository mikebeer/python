/* =========================================================================
   p7_rt.c   runtime introspection: function __code__ / __defaults__ / __kwdefaults__, code, frame
             and traceback objects, sys._getframe, e.__traceback__, types.GenericAlias

   Frame and traceback objects are snapshots (instances of native classes made by mod_class): the C
   frames live on the C stack and are gone when an exception has been caught.  A frame object has
   f_code, f_lineno, f_back, f_globals, f_locals; a traceback tb_frame, tb_lineno, tb_next.
   ========================================================================= */
static void rt_set(Inst *in, const char *nm, Value v)
{
    amap_set(&in->a, intern_c(nm), v);
}
static Value rt_names(List *l)
{
    Value r = mktuple_from(l->a, l->n);
    return r;
}

/* ---- modules' file names ---------------------------------------------- */
static Value file_of_gp(Str *gp)
{
    long gi;
    if (gp) {
        Str *key = prefixed(gp, intern_c("__file__"));
        gi = global_find(key);
        if (gi >= 0 && G[gi].t == T_STR) return G[gi];
    }
    return mkcstr(script_name);
}

/* ---- code objects ----------------------------------------------------- */
static Value make_code(const char *name, Str *nmstr, Str *gp, int line, FuncDef *fd)
{
    Inst *in = new_inst(cls_code);
    Value iv = mkobj(in, T_INST);
    Value nv = nmstr ? mkobj(nmstr, T_STR) : mkcstr(name);
    List *vn, *cv, *fv;
    Value vnv, cvv, fvv;
    int i, flags = 0, nargs = 0, nkw = 0, nposonly = 0;
    VPUSH(iv);
    VPUSH(nv);
    vn = list_new(8); vnv = mklist(vn); VPUSH(vnv);
    cv = list_new(4); cvv = mklist(cv); VPUSH(cvv);
    fv = list_new(4); fvv = mklist(fv); VPUSH(fvv);
    if (fd) {
        Scope *sc = fd->sc;
        nargs = fd->npos;
        nkw = fd->nkwonly;
        nposonly = fd->nposonly;
        for (i = 0; i < sc->nvars; i++) {
            Var *v = sc->vars[i];
            if (v->name->s[0] == '.') continue;
            if (v->isparam) list_push(vn, mkobj(v->name, T_STR));
            else if (v->kind == VK_LOCAL) list_push(vn, mkobj(v->name, T_STR));
            else if (v->kind == VK_CELL) list_push(cv, mkobj(v->name, T_STR));
            else if (v->kind == VK_FREE) list_push(fv, mkobj(v->name, T_STR));
        }
        flags = 3;
        if (fd->va_slot >= 0) flags |= 4;
        if (fd->kw_slot >= 0) flags |= 8;
        if (sc->parent && sc->parent->kind == SC_FUNC) flags |= 16;
        if (fd->isgen) flags |= 32;
    }
    rt_set(in, "co_name", nv);
    rt_set(in, "co_qualname", nv);
    rt_set(in, "co_filename", file_of_gp(gp));
    rt_set(in, "co_firstlineno", mkint(line));
    rt_set(in, "co_argcount", mkint(nargs));
    rt_set(in, "co_posonlyargcount", mkint(nposonly));
    rt_set(in, "co_kwonlyargcount", mkint(nkw));
    rt_set(in, "co_nlocals", mkint(vn->n));
    rt_set(in, "co_varnames", rt_names(vn));
    rt_set(in, "co_cellvars", rt_names(cv));
    rt_set(in, "co_freevars", rt_names(fv));
    rt_set(in, "co_flags", mkint(flags));
    rt_set(in, "co_names", mktuple_from(NULL, 0));
    rt_set(in, "co_consts", mktuple_from(NULL, 0));
    rt_set(in, "co_code", mkbytes("", 0));
    vsp -= 5;
    return iv;
}
BI(code_repr)
{
    Inst *in = AS_INST(A(0));
    Value *nm = amap_get(&in->a, intern_c("co_name"));
    Value *fl = amap_get(&in->a, intern_c("co_filename"));
    Value *ln = amap_get(&in->a, intern_c("co_firstlineno"));
    char b[500];
    sprintf(b, "<code object %.100s at %p, file \"%.200s\", line %ld>",
            nm && nm->t == T_STR ? AS_STR(*nm)->s : "?", (void *)in,
            fl && fl->t == T_STR ? AS_STR(*fl)->s : "?", ln ? ln->u.i : 0L);
    return mkcstr(b);
}
static Value code_for_func(Func *fn)
{
    Value *c = amap_get(&fn->attrs, intern_c("__code__"));
    Value r;
    if (c) return *c;
    r = make_code(NULL, fn->name, fn->def->fd->gpfx, fn->def->fd->line, fn->def->fd);
    amap_set(&fn->attrs, intern_c("__code__"), r);
    return r;
}

/* computed function attributes: __code__ __defaults__ __kwdefaults__ __globals__ __closure__ */
static int func_special_attr(Value fv, Str *nm, Value *out)
{
    Func *fn = AS_FUNC(fv);
    FuncDef *fd = fn->def->fd;
    const char *a = nm->s;
    int i;
    if (a[0] != '_' || a[1] != '_') return 0;
    if (strcmp(a, "__code__") == 0) { *out = code_for_func(fn); return 1; }
    if (strcmp(a, "__defaults__") == 0) {
        *out = fn->ndefs > 0 ? mktuple_from(fn->defs, fn->ndefs) : NONE_V;
        return 1;
    }
    if (strcmp(a, "__kwdefaults__") == 0) {
        Dict *d;
        Value dv;
        int cnt = 0;
        for (i = 0; i < fd->nkwonly; i++) if (fn->kwdefs && fn->kwdefs[i].t != T_UNSET) cnt++;
        if (cnt == 0) { *out = NONE_V; return 1; }
        d = dict_new();
        dv = mkobj(d, T_DICT);
        VPUSH(dv);
        for (i = 0; i < fd->nkwonly; i++)
            if (fn->kwdefs[i].t != T_UNSET)
                dict_set(d, mkobj(fd->pnames[fd->npos + i], T_STR), fn->kwdefs[i]);
        vsp--;
        *out = dv;
        return 1;
    }
    if (strcmp(a, "__globals__") == 0) {
        *out = globals_of(fd->gpfx ? fd->gpfx : main_gp);
        return 1;
    }
    if (strcmp(a, "__closure__") == 0) {
        if (fd->nfree == 0) { *out = NONE_V; return 1; }
        {
            List *t = tuple_new(fd->nfree);
            for (i = 0; i < fd->nfree; i++) t->a[i] = mkobj(fn->cells[i], T_CELL);
            *out = mkobj(t, T_TUPLE);
        }
        return 1;
    }
    return 0;
}

/* ---- frame objects ---------------------------------------------------- */
/* pay = (function or None, module prefix or None, class body name or None) */
static Value make_frame_obj(Value fnv, Value gpv, Value clsname, int line, Value back, Value locals)
{
    Inst *in = new_inst(cls_frame);
    Value iv = mkobj(in, T_INST), pv[3];
    VPUSH(iv);
    pv[0] = fnv; pv[1] = gpv; pv[2] = clsname;
    in->pay = mktuple_from(pv, 3);
    rt_set(in, "f_lineno", mkint(line));
    rt_set(in, "f_back", back);
    rt_set(in, "f_locals", locals);
    rt_set(in, "f_trace", NONE_V);
    rt_set(in, "f_lasti", mkint(0));
    vsp--;
    return iv;
}
BI(frame_repr)
{
    Inst *in = AS_INST(A(0));
    Value *ln = amap_get(&in->a, intern_c("f_lineno"));
    char b[200];
    sprintf(b, "<frame at %p, line %ld>", (void *)in, ln ? ln->u.i : 0L);
    return mkcstr(b);
}
/* the local variables of a live frame as a dict */
static Value frame_locals_dict(Frame *f)
{
    Dict *d = dict_new();
    Value dv = mkobj(d, T_DICT);
    int i;
    VPUSH(dv);
    if (f->fn && f->loc) {
        Scope *sc = f->fn->def->fd->sc;
        for (i = 0; i < sc->nvars; i++) {
            Var *v = sc->vars[i];
            Value val = UNSET_V;
            if (v->kind == VK_LOCAL) val = f->loc[v->slot];
            else if (v->kind == VK_CELL) val = ((Cell *)f->loc[v->slot].u.o)->v;
            else if (v->kind == VK_FREE && f->cells) val = f->cells[v->slot]->v;
            if (val.t != T_UNSET && v->name->s[0] != '.') dict_set(d, mkobj(v->name, T_STR), val);
        }
    } else if (f->ns) {
        for (i = 0; i < f->ns->ns.n; i++) dict_set(d, mkobj(f->ns->ns.k[i], T_STR), f->ns->ns.v[i]);
    } else {
        vsp--;
        return globals_of(f->gp ? f->gp : main_gp);
    }
    vsp--;
    return dv;
}
BI(sys_getframe)
{
    long depth = x->n > 0 ? to_index(A(0)) : 0, n = 0, i;
    Frame *f, **fa;
    Value prev = NONE_V;
    for (f = cur_frame; f; f = f->up) n++;
    if (depth < 0 || depth >= n) throw_new(cls_ValueError, "call stack is not deep enough");
    fa = (Frame **)xmalloc(sizeof(Frame *) * (size_t)n);
    for (f = cur_frame, i = 0; f; f = f->up) fa[i++] = f;
    for (i = n - 1; i >= depth; i--) {
        Value fnv, gpv, cn, ld;
        int line = i == 0 ? cur_line : fa[i]->line;
        fnv = fa[i]->fn ? mkobj(fa[i]->fn, T_FUNC) : NONE_V;
        gpv = fa[i]->gp ? mkobj(fa[i]->gp, T_STR) : NONE_V;
        cn = fa[i]->ns ? mkobj(fa[i]->ns->name, T_STR) : NONE_V;
        VPUSH(prev);
        ld = frame_locals_dict(fa[i]);
        VPUSH(ld);
        prev = make_frame_obj(fnv, gpv, cn, line, prev, ld);
        vsp -= 2;
    }
    free(fa);
    return prev;
}

/* ---- traceback objects ------------------------------------------------ */
static Value make_tb_obj(Value frame, int line, Value next)
{
    Inst *in = new_inst(cls_tb);
    Value iv = mkobj(in, T_INST);
    rt_set(in, "tb_frame", frame);
    rt_set(in, "tb_lineno", mkint(line));
    rt_set(in, "tb_next", next);
    rt_set(in, "tb_lasti", mkint(0));
    return iv;
}
BI(tb_repr)
{
    Inst *in = AS_INST(A(0));
    Value *ln = amap_get(&in->a, intern_c("tb_lineno"));
    char b[200];
    sprintf(b, "<traceback object at %p>", (void *)in);
    (void)ln;
    return mkcstr(b);
}
/* the traceback of an exception from its hidden raw entries (outermost first) */
static Value tb_chain(List *raw)
{
    long n = raw->n / 4, i;
    List *fl = list_new(n);
    Value flv = mklist(fl), prev = NONE_V, tb = NONE_V, e;
    VPUSH(flv);
    for (i = 0; i < n; i++) {
        Value ld = mkobj(dict_new(), T_DICT);
        VPUSH(ld);
        prev = make_frame_obj(raw->a[4 * i], raw->a[4 * i + 1], raw->a[4 * i + 2],
                              (int)raw->a[4 * i + 3].u.i, prev, ld);
        vsp--;
        list_push(fl, prev);
    }
    for (i = n - 1; i >= 0; i--) {
        e = raw->a[4 * i + 3];
        tb = make_tb_obj(fl->a[i], (int)e.u.i, tb);
        VPUSH(tb);
        vsp--;
    }
    vsp--;
    return tb;
}

/* code object of a frame object: function, class body or module */
static Value frame_code(Inst *in)
{
    List *p = AS_LIST(in->pay);
    Value *ln = amap_get(&in->a, intern_c("f_lineno"));
    if (p->a[0].t == T_FUNC) return code_for_func(AS_FUNC(p->a[0]));
    if (p->a[2].t == T_STR)
        return make_code(NULL, AS_STR(p->a[2]), p->a[1].t == T_STR ? AS_STR(p->a[1]) : main_gp,
                         ln ? (int)ln->u.i : 1, NULL);
    return make_code("<module>", NULL, p->a[1].t == T_STR ? AS_STR(p->a[1]) : main_gp, 1, NULL);
}
/* attributes computed on demand: e.__traceback__ and f_code / f_globals / f_builtins of frames */
static int rt_inst_attr(Inst *in, Str *nm, Value *out)
{
    if (in->cls != cls_frame) {
        Value *raw = amap_get(&in->a, S_tbraw), tb;
        if (!raw || raw->t != T_TUPLE) { *out = NONE_V; return 1; }
        tb = tb_chain(AS_LIST(*raw));
        amap_set(&in->a, S_traceback, tb);
        *out = tb;
        return 1;
    }
    if (strcmp(nm->s, "f_code") == 0) {
        Value c = frame_code(in);
        amap_set(&in->a, nm, c);
        *out = c;
        return 1;
    }
    if (strcmp(nm->s, "f_globals") == 0) {
        List *p = AS_LIST(in->pay);
        *out = globals_of(p->a[1].t == T_STR ? AS_STR(p->a[1]) : main_gp);
        return 1;
    }
    if (strcmp(nm->s, "f_builtins") == 0) {
        *out = mkobj(dict_new(), T_DICT);
        return 1;
    }
    return 0;
}
/* BaseException.with_traceback(tb) */
BI(exc_with_traceback)
{
    need(x, 2, 2, "with_traceback");
    if (A(0).t == T_INST) amap_set(&AS_INST(A(0))->a, S_traceback, A(1));
    return A(0);
}

/* ---- types.GenericAlias ----------------------------------------------- */
static void ga_type_repr(Value v, Buf *b)
{
    if (v.t == T_CLASS) {
        Class *c = AS_CLASS(v);
        if (c->modname && !(c->modname->len == 8 && memcmp(c->modname->s, "builtins", 8) == 0)) {
            buf_addn(b, c->modname->s, c->modname->len);
            buf_addc(b, '.');
        }
        buf_addn(b, c->name->s, c->name->len);
    } else if (v.t == T_INST && v.u.o == Ellipsis_V.u.o) {
        buf_add(b, "...");
    } else if (v.t == T_LIST) {
        long i;
        buf_addc(b, '[');
        for (i = 0; i < AS_LIST(v)->n; i++) {
            if (i) buf_add(b, ", ");
            ga_type_repr(AS_LIST(v)->a[i], b);
        }
        buf_addc(b, ']');
    } else {
        Value r = py_repr(v);
        buf_addn(b, AS_STR(r)->s, AS_STR(r)->len);
    }
}
static Value generic_alias(Value origin, Value params)
{
    Inst *in = new_inst(cls_GenericAlias);
    Value iv = mkobj(in, T_INST), args;
    VPUSH(iv);
    if (params.t == T_TUPLE) args = params;
    else args = mktuple_from(&params, 1);
    rt_set(in, "__origin__", origin);
    rt_set(in, "__args__", args);
    rt_set(in, "__parameters__", mktuple_from(NULL, 0));
    rt_set(in, "__unpacked__", FALSE_V);
    vsp--;
    return iv;
}
BI(ga_new)
{
    need(x, 2, 2, "GenericAlias");
    return generic_alias(A(0), A(1));
}
BI(ga_repr)
{
    Inst *in = AS_INST(A(0));
    Value *o = amap_get(&in->a, intern_c("__origin__"));
    Value *a = amap_get(&in->a, intern_c("__args__"));
    Buf b;
    long i;
    buf_init(&b);
    ga_type_repr(*o, &b);
    buf_addc(&b, '[');
    if (AS_LIST(*a)->n == 0) buf_add(&b, "()");
    for (i = 0; i < AS_LIST(*a)->n; i++) {
        if (i) buf_add(&b, ", ");
        ga_type_repr(AS_LIST(*a)->a[i], &b);
    }
    buf_addc(&b, ']');
    return buf_value(&b);
}
BI(ga_eq)
{
    Value *o1, *a1, *o2, *a2;
    if (A(1).t != T_INST || AS_INST(A(1))->cls != cls_GenericAlias) return FALSE_V;
    o1 = amap_get(&AS_INST(A(0))->a, intern_c("__origin__"));
    a1 = amap_get(&AS_INST(A(0))->a, intern_c("__args__"));
    o2 = amap_get(&AS_INST(A(1))->a, intern_c("__origin__"));
    a2 = amap_get(&AS_INST(A(1))->a, intern_c("__args__"));
    return mkbool(py_eq(*o1, *o2) && py_eq(*a1, *a2));
}
BI(ga_hash)
{
    Value *o = amap_get(&AS_INST(A(0))->a, intern_c("__origin__"));
    Value *a = amap_get(&AS_INST(A(0))->a, intern_c("__args__"));
    return mkint((long)(py_hash(*o) ^ (py_hash(*a) * 3UL)) & 0x3fffffffL);
}
BI(ga_call)
{
    Value *o = amap_get(&AS_INST(A(0))->a, intern_c("__origin__"));
    return call_value(*o, x->n - 1, x->a + 1, x->nk, x->kw);
}
/* attributes not found on the alias come from the origin (list[int].__name__) */
BI(ga_getattr)
{
    Value *o = amap_get(&AS_INST(A(0))->a, intern_c("__origin__"));
    if (A(1).t != T_STR) return NONE_V;
    return get_attr(*o, intern_str(AS_STR(A(1))));
}
BI(ga_mro_entries)
{
    Value *o = amap_get(&AS_INST(A(0))->a, intern_c("__origin__"));
    return mktuple_from(o, 1);
}

static void introspect_init(void)
{
    cls_code = mod_class("code", cls_object, "builtins");
    cls_frame = mod_class("frame", cls_object, "builtins");
    cls_tb = mod_class("traceback", cls_object, "builtins");
    cls_GenericAlias = mod_class("GenericAlias", cls_object, "types");
    cls_GenericAlias->ctor = ga_new;
    def_bi(cls_code, "__repr__", code_repr);
    def_bi(cls_frame, "__repr__", frame_repr);
    def_bi(cls_tb, "__repr__", tb_repr);
    def_bi(cls_GenericAlias, "__repr__", ga_repr);
    def_bi(cls_GenericAlias, "__eq__", ga_eq);
    def_bi(cls_GenericAlias, "__hash__", ga_hash);
    def_bi(cls_GenericAlias, "__call__", ga_call);
    def_bi(cls_GenericAlias, "__getattr__", ga_getattr);
    def_bi(cls_GenericAlias, "__mro_entries__", ga_mro_entries);
    def_bi(cls_BaseException, "with_traceback", exc_with_traceback);
}
