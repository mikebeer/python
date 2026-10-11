/* =========================================================================
   p7_imp.c   module import from files, sys.path, sys.modules, exec/eval,
              globals()/locals()/dir()
   ========================================================================= */
#ifndef PY_LIBDIR
#define PY_LIBDIR "pylib"
#endif

static Value sys_modules_v = { 0, { 0 } };
static Value sys_path_v = { 0, { 0 } };
static Class *cls_ModuleNotFoundError;
static const char *exe_path = "";

/* the only place that knows how paths are joined (VM/370 may differ) */
static void path_join(Buf *b, const char *dir, long dlen, const char *name, long nlen)
{
    if (dlen > 0) {
        buf_addn(b, dir, dlen);
        if (dir[dlen - 1] != '/') buf_addc(b, '/');
    }
    buf_addn(b, name, nlen);
}
static Value path_join_v(Value dir, const char *name)
{
    Buf b;
    buf_init(&b);
    path_join(&b, AS_STR(dir)->s, AS_STR(dir)->len, name, (long)strlen(name));
    return buf_value(&b);
}

static char *read_whole(const char *path, long *len)
{
    FILE *fp = fopen(path, "rb");
    char *buf;
    long n = 0, cap = 4096;
    int c;
    if (!fp) return NULL;
    buf = (char *)xmalloc((size_t)cap);
    while ((c = getc(fp)) != EOF) {
        if (n + 2 >= cap) { cap *= 2; buf = (char *)xrealloc(buf, (size_t)cap); }
        buf[n++] = (char)c;
    }
    if (ferror(fp)) { fclose(fp); free(buf); return NULL; }
    buf[n] = 0;
    fclose(fp);
    *len = n;
    return buf;
}

/* ---- compile and run -------------------------------------------------- */
static Node *compile_src(const char *src, long len, Str *gp)
{
    Handler h;
    int sgp = gc_perm;
    Str *sg = cur_gpfx;
    Scope *sm = mod_scope, *sc = cur_scope;
    Node *blk = NULL;
    h.prev = cur_handler;
    h.sp = vsp;
    h.depth = call_depth;
    h.frame = cur_frame;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        gc_perm = 1;
        cur_gpfx = gp;
        lex_source(src, len);
        mod_scope = scope_new(SC_MOD, NULL);
        cur_scope = mod_scope;
        blk = parse_module();
        resolve_scope(mod_scope);
        cur_handler = h.prev;
        gc_perm = sgp; cur_gpfx = sg; mod_scope = sm; cur_scope = sc;
        return blk;
    }
    cur_handler = h.prev;
    vsp = h.sp;
    call_depth = h.depth;
    cur_frame = h.frame;
    gc_perm = sgp; cur_gpfx = sg; mod_scope = sm; cur_scope = sc;
    throw_val(cur_exc);
    return NULL;
}

static void init_frame(Frame *fr, Str *gp)
{
    fr->loc = NULL; fr->cells = NULL; fr->fn = NULL; fr->ret = NONE_V; fr->gen = NULL;
    fr->ns = NULL; fr->resuming = 0; fr->gp = gp; fr->up = cur_frame;
}
static void run_block_in(Node *blk, Str *gp)
{
    Frame fr;
    if (call_depth >= MAX_DEPTH) throw_new(cls_RecursionError, "maximum recursion depth exceeded");
    init_frame(&fr, gp);
    cur_frame = &fr;
    call_depth++;
    exec_block(&fr, blk);
    call_depth--;
    cur_frame = fr.up;
}

/* ---- modules written in Python ---------------------------------------- */
static Value mods_get(Str *name)
{
    Value r;
    if (dict_get(AS_DICT(sys_modules_v), mkobj(name, T_STR), &r)) return r;
    return UNSET_V;
}
static Str *concat_dot(Str *a, const char *b, long blen, int dot)
{
    Buf bf;
    Str *r;
    buf_init(&bf);
    buf_addn(&bf, a->s, a->len);
    if (dot) buf_addc(&bf, '.');
    buf_addn(&bf, b, blen);
    r = intern(bf.s, bf.n);
    buf_free(&bf);
    return r;
}
static Module *module_new_py(Str *name)
{
    Module *m = (Module *)gc_alloc(sizeof(Module), T_MODULE);
    m->name = name;
    m->ns.k = NULL; m->ns.v = NULL; m->ns.n = m->ns.cap = 0;
    m->gmap = NULL;
    m->gp = concat_dot(name, "", 0, 1);
    return m;
}
static void mod_gset(Module *m, const char *attr, Value v)
{
    long gi = global_index(prefixed(m->gp, intern_c(attr)));
    G[gi] = v;
}

static void load_module_file(Str *full, Value mv, const char *file, const char *src, long len,
                             int ispkg, Value pkgdir, Str *parentname)
{
    Module *m = (Module *)mv.u.o;
    Handler h;
    int saved_echo = repl_echo;
    Node *blk;
    mod_gset(m, "__name__", mkobj(full, T_STR));
    mod_gset(m, "__file__", mkcstr(file));
    if (ispkg) {
        List *pl = list_new(1);
        list_push(pl, pkgdir);
        mod_gset(m, "__path__", mklist(pl));
        mod_gset(m, "__package__", mkobj(full, T_STR));
    } else {
        mod_gset(m, "__package__", parentname ? mkobj(parentname, T_STR) : mkstr("", 0));
    }
    VPUSH(mv);
    h.prev = cur_handler;
    h.sp = vsp;
    h.depth = call_depth;
    h.frame = cur_frame;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        blk = compile_src(src, len, m->gp);
        repl_echo = 0;
        run_block_in(blk, m->gp);
        repl_echo = saved_echo;
        cur_handler = h.prev;
        vsp--;
        return;
    }
    cur_handler = h.prev;
    vsp = h.sp - 1;
    call_depth = h.depth;
    cur_frame = h.frame;
    repl_echo = saved_echo;
    gc_perm = 0;
    dict_del(AS_DICT(sys_modules_v), mkobj(full, T_STR));
    throw_val(cur_exc);
}

static Value import_one(Str *full);

/* look for module `leaf` in directory `dir`; returns the module or UNSET */
static Value try_dir(Value dir, Str *leaf, Str *full, Str *parentname)
{
    Buf pb;
    Value sub, pkgfile, mv;
    char *src;
    long len;
    int ispkg = 1;
    Module *m;
    if (dir.t != T_STR) return UNSET_V;
    sub = path_join_v(dir, leaf->s);
    VPUSH(sub);
    buf_init(&pb);
    path_join(&pb, AS_STR(sub)->s, AS_STR(sub)->len, "__init__.py", 11);
    src = read_whole(pb.s, &len);
    if (!src) {
        buf_free(&pb);
        buf_init(&pb);
        buf_addn(&pb, AS_STR(sub)->s, AS_STR(sub)->len);
        buf_add(&pb, ".py");
        src = read_whole(pb.s, &len);
        ispkg = 0;
    }
    if (!src) { buf_free(&pb); vsp--; return UNSET_V; }
    pkgfile = mkstr(pb.s, pb.n);
    buf_free(&pb);
    VPUSH(pkgfile);
    m = module_new_py(full);
    mv = mkobj(m, T_MODULE);
    VPUSH(mv);
    dict_set(AS_DICT(sys_modules_v), mkobj(full, T_STR), mv);
    load_module_file(full, mv, AS_STR(pkgfile)->s, src, len, ispkg, sub, parentname);
    free(src);
    vsp -= 3;
    return mv;
}

static Value module_getattr_opt(Value mv, Str *nm);

static int import_soft;   /* next import_one returns UNSET instead of raising ModuleNotFoundError */
static Value import_one(Str *full)
{
    Value m = mods_get(full), dirs, r;
    int soft = import_soft;
    Str *leaf, *parent = NULL;
    Value pm = UNSET_V;
    long i, dot = -1;
    import_soft = 0;
    if (m.t != T_UNSET) {
        if (full->len == 5 && memcmp(full->s, "numpy", 5) == 0) np_float_methods();
        return m;
    }
    for (i = full->len - 1; i >= 0; i--) if (full->s[i] == '.') { dot = i; break; }
    if (dot >= 0) {
        parent = intern(full->s, dot);
        leaf = intern(full->s + dot + 1, full->len - dot - 1);
        pm = import_one(parent);
        dirs = module_getattr_opt(pm, intern_c("__path__"));
        if (dirs.t != T_LIST)
            throw_new(cls_ModuleNotFoundError, "No module named '%.200s'; '%.100s' is not a package",
                      full->s, parent->s);
    } else {
        leaf = full;
        dirs = sys_path_v;
    }
    VPUSH(pm);
    VPUSH(dirs);
    for (i = 0; i < AS_LIST(dirs)->n; i++) {
        r = try_dir(AS_LIST(dirs)->a[i], leaf, full, parent);
        if (r.t != T_UNSET) {
            if (pm.t == T_MODULE) set_attr(pm, leaf, r);
            vsp -= 2;
            return r;
        }
    }
    vsp -= 2;
    if (soft) return UNSET_V;
    throw_new(cls_ModuleNotFoundError, "No module named '%.200s'", full->s);
    return UNSET_V;
}

/* import `dotted` and all its parents; returns the leaf, or the top-level package */
static Value import_dotted(Str *dotted, int want_leaf)
{
    Value leaf = import_one(dotted);
    long i;
    if (want_leaf) return leaf;
    for (i = 0; i < dotted->len; i++)
        if (dotted->s[i] == '.') return import_one(intern(dotted->s, i));
    return leaf;
}

/* ---- module attributes ------------------------------------------------ */
static Value module_getattr_opt(Value mv, Str *nm)
{
    Module *m = (Module *)mv.u.o;
    if (m->gp) {
        long gi = module_slot(m, nm, 0);
        if (gi >= 0 && G[gi].t != T_UNSET) return G[gi];
        return UNSET_V;
    } else {
        Value *p = amap_get(&m->ns, nm);
        if (p) return *p;
        { Value ov; if (module_overlay(m, nm, &ov)) return ov; }
    }
    return UNSET_V;
}

/* native numpy falls back to the pure-Python overlay pylib/numpy/__init__.py (loaded once as the
   hidden module numpy._overlay) for names it does not provide */
static int overlay_state;   /* 0 not tried, 1 loading, 2 loaded, 3 unavailable */
static Value overlay_mod;
static int module_overlay(Module *md, Str *nm, Value *out)
{
    Value r, full;
    if (md->gp || md->name->len != 5 || memcmp(md->name->s, "numpy", 5) != 0) return 0;
    if (nm->len >= 2 && nm->s[0] == '_' && nm->s[1] == '_') return 0;
    if (overlay_state == 1 || overlay_state == 3) return 0;
    if (overlay_state == 0) {
        long k;
        Str *ovn = intern_c("numpy._overlay");
        Str *leaf = intern_c("numpy");
        overlay_state = 1;
        overlay_mod = UNSET_V;
        for (k = 0; k < AS_LIST(sys_path_v)->n && overlay_mod.t == T_UNSET; k++) {
            Value cand = AS_LIST(sys_path_v)->a[k];
            Value sub;
            Buf pb;
            char *src;
            long len;
            if (cand.t != T_STR) continue;
            sub = path_join_v(cand, "numpy");
            VPUSH(sub);
            buf_init(&pb);
            path_join(&pb, AS_STR(sub)->s, AS_STR(sub)->len, "__init__.py", 11);
            src = read_whole(pb.s, &len);
            buf_free(&pb);
            vsp--;
            if (src) { free(src); overlay_mod = try_dir(cand, leaf, ovn, NULL); }
        }
        if (overlay_mod.t == T_UNSET) { overlay_state = 3; return 0; }
        overlay_state = 2;
    }
    full = overlay_mod;
    r = module_getattr_opt(full, nm);
    if (r.t == T_UNSET) return 0;
    amap_set(&md->ns, nm, r);
    *out = r;
    return 1;
}

/* package that the code with global prefix `pfx` belongs to */
static Str *package_of(Str *pfx)
{
    Value m, p;
    if (!pfx) return NULL;
    m = mods_get(intern(pfx->s, pfx->len - 1));
    if (m.t != T_MODULE) return NULL;
    p = module_getattr_opt(m, intern_c("__package__"));
    if (p.t != T_STR || AS_STR(p)->len == 0) return NULL;
    return intern_str(AS_STR(p));
}
static Str *resolve_from_name(Node *n)
{
    Str *base;
    int i;
    if (n->level == 0) return n->name;
    base = package_of(n->pfx);
    if (!base) throw_new(cls_ImportError, "attempted relative import with no known parent package");
    for (i = 1; i < n->level; i++) {
        long k;
        for (k = base->len - 1; k >= 0 && base->s[k] != '.'; k--) { }
        if (k < 0) throw_new(cls_ImportError, "attempted relative import beyond top-level package");
        base = intern(base->s, k);
    }
    if (!n->name) return base;
    return concat_dot(base, n->name->s, n->name->len, 1);
}

static void exec_import(Frame *f, Node *n)
{
    int i;
    for (i = 0; i < n->n; i++) {
        Node *it = n->list[i];
        Value m = import_dotted(it->name, it->op);
        store_var(f, it->b->var, m);
    }
}

/* names a module exports for "from m import *" */
static Value module_public_names(Value mv)
{
    Module *m = (Module *)mv.u.o;
    List *l = list_new(16);
    Value lv = mklist(l), all;
    long i;
    VPUSH(lv);
    all = module_getattr_opt(mv, intern_c("__all__"));
    if (all.t == T_LIST || all.t == T_TUPLE) {
        for (i = 0; i < AS_LIST(all)->n; i++) list_push(l, AS_LIST(all)->a[i]);
    } else if (m->gp) {
        for (i = 0; i < nglobals; i++) {
            Str *g = gnames[i];
            if (g->len > m->gp->len && memcmp(g->s, m->gp->s, (size_t)m->gp->len) == 0 &&
                G[i].t != T_UNSET && g->s[m->gp->len] != '_' &&
                !memchr(g->s + m->gp->len, '.', (size_t)(g->len - m->gp->len)))
                list_push(l, mkstr(g->s + m->gp->len, g->len - m->gp->len));
        }
    } else {
        for (i = 0; i < m->ns.n; i++)
            if (m->ns.k[i]->s[0] != '_') list_push(l, mkobj(m->ns.k[i], T_STR));
    }
    vsp--;
    return lv;
}

static void exec_from(Frame *f, Node *n)
{
    Str *full = resolve_from_name(n);
    Value m = import_one(full), names;
    int i;
    long gi;
    if (n->op == 1) {
        long k;
        Module *md = (Module *)m.u.o;
        VPUSH(m);
        names = module_public_names(m);
        VPUSH(names);
        for (k = 0; k < AS_LIST(names)->n; k++) {
            Value nm = AS_LIST(names)->a[k], v;
            if (nm.t != T_STR) throw_new(cls_TypeError, "Item in __all__ must be str");
            v = module_getattr_opt(m, intern_str(AS_STR(nm)));
            if (v.t == T_UNSET) {
                v = mods_get(concat_dot(md->name, AS_STR(nm)->s, AS_STR(nm)->len, 1));
                if (v.t == T_UNSET)
                    throw_new(cls_AttributeError, "module '%.100s' has no attribute '%.100s'",
                              md->name->s, AS_STR(nm)->s);
            }
            gi = global_index(prefixed(n->pfx, intern_str(AS_STR(nm))));
            G[gi] = v;
        }
        vsp -= 2;
        return;
    }
    VPUSH(m);
    for (i = 0; i < n->n; i++) {
        Node *it = n->list[i];
        Value v = module_getattr_opt(m, it->name);
        if (v.t == T_UNSET) {
            Value path = module_getattr_opt(m, intern_c("__path__"));
            if (path.t == T_LIST) {
                import_soft = 1;
                v = import_one(concat_dot(full, it->name->s, it->name->len, 1));
                import_soft = 0;
            }
        }
        if (v.t == T_UNSET) {
            Value part = module_getattr_opt(m, intern_c("__file__"));
            throw_new(cls_ImportError, "cannot import name '%.100s' from '%.100s' (%.200s)",
                      it->name->s, full->s, part.t == T_STR ? AS_STR(part)->s : "unknown location");
        }
        store_var(f, it->b->var, v);
    }
    vsp--;
}

/* ---- sys.path --------------------------------------------------------- */
static void path_append(List *l, const char *s, long n)
{
    list_push(l, mkstr(s, n));
}
static void init_sys_path(const char *script)
{
    List *l = list_new(8);
    const char *env = getenv("PYLIB");
    long i, last = -1, n = (long)strlen(script);
    Buf b;
    for (i = 0; i < n; i++) if (script[i] == '/') last = i;
    path_append(l, script, last < 0 ? 0 : (last == 0 ? 1 : last));
    if (env) {
        const char *p = env;
        while (*p) {
            const char *q = p;
            while (*q && *q != ':') q++;
            if (q > p) path_append(l, p, (long)(q - p));
            p = *q ? q + 1 : q;
        }
    }
    n = (long)strlen(exe_path);
    last = -1;
    for (i = 0; i < n; i++) if (exe_path[i] == '/') last = i;
    buf_init(&b);
    path_join(&b, exe_path, last < 0 ? 0 : last, "pylib", 5);
    path_append(l, b.s, b.n);
    buf_free(&b);
    buf_init(&b);
    path_join(&b, exe_path, last < 0 ? 0 : last, "../pylib", 8);
    path_append(l, b.s, b.n);
    buf_free(&b);
    path_append(l, PY_LIBDIR, (long)strlen(PY_LIBDIR));
    sys_path_v = mklist(l);
}

/* ---- globals() locals() dir() exec() eval() --------------------------- */
static Str *cur_gp(void)
{
    Frame *f;
    for (f = cur_frame; f; f = f->up) if (f->gp) return f->gp;
    return main_gp;
}
static Value globals_of(Str *gp)
{
    Dict *d = dict_new();
    Value dv = mkobj(d, T_DICT);
    long i;
    VPUSH(dv);
    for (i = 0; i < nglobals; i++) {
        Str *g = gnames[i];
        if (gp ? (g->len > gp->len && memcmp(g->s, gp->s, (size_t)gp->len) == 0 &&
                  G[i].t != T_UNSET &&
                  !memchr(g->s + gp->len, '.', (size_t)(g->len - gp->len)))
               : 0) {
            Value k = mkstr(g->s + gp->len, g->len - gp->len);
            VPUSH(k);
            dict_set(d, k, G[i]);
            vsp--;
        }
    }
    vsp--;
    return dv;
}
BI(bi_globals)
{
    (void)x;
    return globals_of(cur_gp());
}
static Value locals_dict(void)
{
    Frame *f = cur_frame;
    Dict *d;
    Value dv;
    int i;
    if (!f || !f->fn || !f->loc) return globals_of(cur_gp());
    d = dict_new();
    dv = mkobj(d, T_DICT);
    VPUSH(dv);
    {
        Scope *sc = f->fn->def->fd->sc;
        for (i = 0; i < sc->nvars; i++) {
            Var *v = sc->vars[i];
            Value val = UNSET_V;
            if (v->kind == VK_LOCAL) val = f->loc[v->slot];
            else if (v->kind == VK_CELL) val = ((Cell *)f->loc[v->slot].u.o)->v;
            else if (v->kind == VK_FREE && f->cells) val = f->cells[v->slot]->v;
            if (val.t != T_UNSET && v->name->s[0] != '.') dict_set(d, mkobj(v->name, T_STR), val);
        }
    }
    vsp--;
    return dv;
}
BI(bi_locals)
{
    (void)x;
    return locals_dict();
}
BI(bi_vars2)
{
    if (x->n == 0) return locals_dict();
    if (A(0).t == T_MODULE && ((Module *)A(0).u.o)->gp) return globals_of(((Module *)A(0).u.o)->gp);
    return get_attr(A(0), S_dict);
}
static void dir_add(List *l, Str *nm)
{
    long i;
    for (i = 0; i < l->n; i++) if (AS_STR(l->a[i]) == nm) return;
    list_push(l, mkobj(nm, T_STR));
}
BI(bi_dir)
{
    List *l = list_new(32);
    Value lv = mklist(l), o;
    Value *base = vsp;
    long i;
    VPUSH(lv);
    if (x->n == 0) {
        Value d = locals_dict();
        VPUSH(d);
        for (i = 0; i < AS_DICT(d)->used; i++)
            if (AS_DICT(d)->e[i].k.t != T_UNSET) dir_add(l, intern_str(AS_STR(AS_DICT(d)->e[i].k)));
    } else {
        o = A(0);
        if (o.t == T_MODULE) {
            Module *m = (Module *)o.u.o;
            if (m->gp) {
                Value d = globals_of(m->gp);
                VPUSH(d);
                for (i = 0; i < AS_DICT(d)->used; i++)
                    if (AS_DICT(d)->e[i].k.t != T_UNSET)
                        dir_add(l, intern_str(AS_STR(AS_DICT(d)->e[i].k)));
            } else {
                for (i = 0; i < m->ns.n; i++) dir_add(l, m->ns.k[i]);
            }
        } else {
            Class *c = o.t == T_CLASS ? AS_CLASS(o) : class_of(o);
            int k, n;
            Class **mro = class_mro_array(c, &n);
            if (o.t == T_INST)
                for (i = 0; i < AS_INST(o)->a.n; i++) dir_add(l, AS_INST(o)->a.k[i]);
            for (k = 0; k < n; k++)
                for (i = 0; i < mro[k]->ns.n; i++) dir_add(l, mro[k]->ns.k[i]);
            free(mro);
        }
    }
    sort_list(l, NONE_V, 0);
    vsp = base;
    return lv;
}
BI(bi_exc_info)
{
    Value r[3];
    (void)x;
    if (handled_exc.t == T_INST) {
        r[0] = mkobj(AS_INST(handled_exc)->cls, T_CLASS);
        r[1] = handled_exc;
        r[2] = NONE_V;
    } else {
        r[0] = r[1] = r[2] = NONE_V;
    }
    return mktuple_from(r, 3);
}

static int exec_ns_serial;
/* exec(src[, globals[, locals]]) and eval(src[, globals[, locals]]) */
static Value exec_eval(Args *x, int is_eval)
{
    Value srcv, gd = UNSET_V, ld = UNSET_V, result = NONE_V;
    Str *gp;
    Node *blk;
    Buf b;
    long i, k;
    int usedict = 0, pushed = 0;
    need(x, 1, 3, is_eval ? "eval" : "exec");
    srcv = A(0);
    if (srcv.t != T_STR) throw_new(cls_TypeError, "%s() arg 1 must be a string", is_eval ? "eval" : "exec");
    if (x->n > 1) gd = A(1); else gd = kwget(x, "globals");
    if (x->n > 2) ld = A(2); else ld = kwget(x, "locals");
    if (gd.t != T_DICT && ld.t != T_DICT && cur_frame && cur_frame->fn && cur_frame->loc) {
        /* eval/exec inside a function: see its locals through a scratch namespace */
        ld = locals_dict();
        gd = globals_of(cur_gp());
        VPUSH(gd);
        VPUSH(ld);
        pushed = 2;
    }
    if (gd.t == T_DICT) {
        usedict = 1;
        gp = intern_c("<exec>.");
        /* clear the scratch namespace, then load the dicts */
        for (i = 0; i < nglobals; i++)
            if (gnames[i]->len > gp->len && memcmp(gnames[i]->s, gp->s, (size_t)gp->len) == 0)
                G[i] = UNSET_V;
        for (k = 0; k < 2; k++) {
            Value dd = k == 0 ? gd : ld;
            if (dd.t != T_DICT) continue;
            for (i = 0; i < AS_DICT(dd)->used; i++) {
                DEnt *e = &AS_DICT(dd)->e[i];
                if (e->k.t == T_STR)
                    { long gi2 = global_index(prefixed(gp, intern_str(AS_STR(e->k)))); G[gi2] = e->v; }
            }
        }
    } else {
        gp = cur_gp();
        if (ld.t == T_DICT) {
            for (i = 0; i < AS_DICT(ld)->used; i++) {
                DEnt *e = &AS_DICT(ld)->e[i];
                if (e->k.t == T_STR)
                    { long gi2 = global_index(prefixed(gp, intern_str(AS_STR(e->k)))); G[gi2] = e->v; }
            }
        }
    }
    buf_init(&b);
    if (is_eval) {
        const char *s = AS_STR(srcv)->s;
        long n = AS_STR(srcv)->len;
        while (n > 0 && (*s == ' ' || *s == '\t')) { s++; n--; }
        buf_addc(&b, '(');
        buf_addn(&b, s, n);
        buf_add(&b, "\n)\n");
    } else {
        buf_addn(&b, AS_STR(srcv)->s, AS_STR(srcv)->len);
        buf_addc(&b, '\n');
    }
    exec_ns_serial++;
    VPUSH(srcv);
    blk = compile_src(b.s, b.n, gp);
    buf_free(&b);
    if (is_eval) {
        Frame fr;
        if (blk->n < 1 || blk->list[0]->kind != S_EXPR) throw_new(cls_SyntaxError, "invalid syntax");
        init_frame(&fr, gp);
        cur_frame = &fr;
        call_depth++;
        result = eval(&fr, blk->list[0]->a);
        call_depth--;
        cur_frame = fr.up;
    } else {
        run_block_in(blk, gp);
    }
    vsp--;
    if (usedict) {
        VPUSH(result);
        for (i = 0; i < nglobals; i++) {
            Str *g = gnames[i];
            if (g->len > gp->len && memcmp(g->s, gp->s, (size_t)gp->len) == 0 && G[i].t != T_UNSET &&
                !memchr(g->s + gp->len, '.', (size_t)(g->len - gp->len)) && g->s[gp->len] != '.')
                dict_set(AS_DICT(gd), mkstr(g->s + gp->len, g->len - gp->len), G[i]);
        }
        vsp--;
    }
    vsp -= pushed;
    return result;
}
BI(bi_exec) { exec_eval(x, 0); return NONE_V; }
BI(bi_eval) { return exec_eval(x, 1); }
BI(bi_import)
{
    need(x, 1, 5, "__import__");
    return import_dotted(intern_str(astr(A(0), "name")), x->n > 3 && A(3).t == T_TUPLE && AS_LIST(A(3))->n > 0);
}

/* ---- os and os.path --------------------------------------------------- */
BI(os_getenv)
{
    const char *v;
    need(x, 1, 2, "getenv");
    v = getenv(astr(A(0), "key")->s);
    if (v) return mkcstr(v);
    return x->n > 1 ? A(1) : NONE_V;
}
BI(os_getcwd)
{
    (void)x;
#ifdef PY_POSIX
    {
        char b[4096];
        if (getcwd(b, sizeof(b))) return mkcstr(b);
    }
#endif
    return mkcstr(".");
}
BI(osp_join)
{
    Buf b;
    int i;
    need(x, 1, 1000, "join");
    buf_init(&b);
    for (i = 0; i < x->n; i++) {
        Str *p = astr(A(i), "path");
        if (p->len > 0 && p->s[0] == '/') b.n = 0;
        if (b.n > 0 && b.s[b.n - 1] != '/' && p->len > 0) buf_addc(&b, '/');
        else if (b.n > 0 && b.s[b.n - 1] != '/' && i == x->n - 1 && p->len == 0) buf_addc(&b, '/');
        buf_addn(&b, p->s, p->len);
    }
    return buf_value(&b);
}
static long last_slash(Str *s)
{
    long i;
    for (i = s->len - 1; i >= 0; i--) if (s->s[i] == '/') return i;
    return -1;
}
BI(osp_basename)
{
    Str *s;
    need(x, 1, 1, "basename");
    s = astr(A(0), "path");
    return mkstr(s->s + last_slash(s) + 1, s->len - last_slash(s) - 1);
}
BI(osp_dirname)
{
    Str *s;
    long k, e;
    need(x, 1, 1, "dirname");
    s = astr(A(0), "path");
    k = last_slash(s);
    if (k < 0) return mkstr("", 0);
    e = k;
    while (e > 0 && s->s[e - 1] == '/') e--;
    if (e == 0) e = 1;
    return mkstr(s->s, e);
}
BI(osp_split)
{
    Value h = osp_dirname(x), t = osp_basename(x);
    return mktuple2(h, t);
}
BI(osp_splitext)
{
    Str *s;
    long i, slash;
    need(x, 1, 1, "splitext");
    s = astr(A(0), "path");
    slash = last_slash(s);
    for (i = s->len - 1; i > slash + 1; i--) {
        if (s->s[i] == '.') {
            long j = slash + 1;
            while (j < i && s->s[j] == '.') j++;
            if (j < i) return mktuple2(mkstr(s->s, i), mkstr(s->s + i, s->len - i));
            break;
        }
    }
    return mktuple2(A(0), mkstr("", 0));
}
BI(osp_isabs)
{
    Str *s;
    need(x, 1, 1, "isabs");
    s = astr(A(0), "path");
    return mkbool(s->len > 0 && s->s[0] == '/');
}
BI(osp_normpath)
{
    Str *s;
    Buf out;
    List *parts = list_new(8);
    Value pv = mklist(parts);
    long i = 0, k;
    int abs;
    need(x, 1, 1, "normpath");
    s = astr(A(0), "path");
    abs = s->len > 0 && s->s[0] == '/';
    VPUSH(pv);
    while (i < s->len) {
        long j = i;
        while (j < s->len && s->s[j] != '/') j++;
        if (j > i) {
            if (j - i == 1 && s->s[i] == '.') { }
            else if (j - i == 2 && s->s[i] == '.' && s->s[i + 1] == '.') {
                if (parts->n > 0 && !(AS_STR(parts->a[parts->n - 1])->len == 2 &&
                                      AS_STR(parts->a[parts->n - 1])->s[0] == '.' &&
                                      AS_STR(parts->a[parts->n - 1])->s[1] == '.')) parts->n--;
                else if (!abs) list_push(parts, mkstr("..", 2));
            } else list_push(parts, mkstr(s->s + i, j - i));
        }
        i = j + 1;
    }
    buf_init(&out);
    if (abs) buf_addc(&out, '/');
    for (k = 0; k < parts->n; k++) {
        if (k) buf_addc(&out, '/');
        buf_addn(&out, AS_STR(parts->a[k])->s, AS_STR(parts->a[k])->len);
    }
    if (out.n == 0) buf_addc(&out, '.');
    vsp--;
    return buf_value(&out);
}
BI(osp_abspath)
{
    Value cw, j[2], p;
    Args a2;
    need(x, 1, 1, "abspath");
    p = A(0);
    if (astr(p, "path")->len == 0 || AS_STR(p)->s[0] != '/') {
        Args none;
        none.n = 0; none.a = NULL; none.nk = 0; none.kw = NULL;
        cw = os_getcwd(&none);
        j[0] = cw; j[1] = p;
        a2.n = 2; a2.a = j; a2.nk = 0; a2.kw = NULL;
        VPUSH(cw);
        p = osp_join(&a2);
        vsp--;
    }
    VPUSH(p);
    a2.n = 1; a2.a = &p; a2.nk = 0; a2.kw = NULL;
    p = osp_normpath(&a2);
    vsp--;
    return p;
}
BI(osp_expanduser)
{
    Str *s;
    const char *home;
    need(x, 1, 1, "expanduser");
    s = astr(A(0), "path");
    if (s->len > 0 && s->s[0] == '~' && (s->len == 1 || s->s[1] == '/')) {
        home = getenv("HOME");
        if (home) {
            Buf b;
            buf_init(&b);
            buf_add(&b, home);
            buf_addn(&b, s->s + 1, s->len - 1);
            return buf_value(&b);
        }
    }
    return A(0);
}
BI(osp_exists)
{
    FILE *fp;
    need(x, 1, 1, "exists");
    fp = fopen(astr(A(0), "path")->s, "rb");
    if (!fp) return FALSE_V;
    fclose(fp);
    return TRUE_V;
}
static int path_is_dir(const char *p)
{
    FILE *fp = fopen(p, "rb");
    int isdir = 0;
    if (!fp) return 0;
    if (getc(fp) == EOF && ferror(fp)) isdir = 1;
    fclose(fp);
    return isdir;
}
BI(osp_isdir)
{
    need(x, 1, 1, "isdir");
    return mkbool(path_is_dir(astr(A(0), "path")->s));
}
BI(osp_isfile)
{
    FILE *fp;
    int ok;
    need(x, 1, 1, "isfile");
    fp = fopen(astr(A(0), "path")->s, "rb");
    if (!fp) return FALSE_V;
    ok = !(getc(fp) == EOF && ferror(fp));
    fclose(fp);
    return mkbool(ok);
}
BI(osp_getsize)
{
    FILE *fp;
    long n;
    need(x, 1, 1, "getsize");
    fp = fopen(astr(A(0), "path")->s, "rb");
    if (!fp) throw_new(cls_FileNotFoundError, "[Errno 2] No such file or directory: '%.200s'", AS_STR(A(0))->s);
    fseek(fp, 0L, SEEK_END);
    n = ftell(fp);
    fclose(fp);
    return mkint(n);
}
BI(os_remove)
{
    need(x, 1, 1, "remove");
    if (remove(astr(A(0), "path")->s) != 0)
        throw_new(cls_FileNotFoundError, "[Errno 2] No such file or directory: '%.200s'", AS_STR(A(0))->s);
    return NONE_V;
}
BI(os_rename)
{
    need(x, 2, 2, "rename");
    if (rename(astr(A(0), "src")->s, astr(A(1), "dst")->s) != 0)
        throw_new(cls_OSError, "rename failed: '%.200s'", AS_STR(A(0))->s);
    return NONE_V;
}


#ifdef PY_POSIX
extern char **environ;
#endif
static Value init_environ(void)
{
    Dict *d = dict_new();
    Value dv = mkobj(d, T_DICT);
#ifdef PY_POSIX
    {
        char **e;
        for (e = environ; e && *e; e++) {
            const char *eq = strchr(*e, '=');
            if (eq) dict_set(d, mkstr(*e, (long)(eq - *e)), mkcstr(eq + 1));
        }
    }
#endif
    return dv;
}
