/* =========================================================================
   p4_res.c   scope resolution: locals, cells, free variables, globals
   ========================================================================= */
static long global_index(Str *nm)
{
    long i;
    for (i = 0; i < nglobals; i++)
        if (gnames[i] == nm) return i;
    if (nglobals >= gcap) {
        gcap = gcap ? gcap * 2 : 256;
        gnames = (Str **)xrealloc(gnames, sizeof(Str *) * (size_t)gcap);
        G = (Value *)xrealloc(G, sizeof(Value) * (size_t)gcap);
    }
    gnames[nglobals] = nm;
    G[nglobals] = UNSET_V;
    return nglobals++;
}

static void set_global(Str *nm, Value v)
{
    long i = global_index(nm);
    G[i] = v;
}

static void add_free(Scope *s, Var *v)
{
    if (s->nfree >= s->fcap) {
        s->fcap = s->fcap ? s->fcap * 2 : 4;
        s->freevars = (Var **)xrealloc(s->freevars, sizeof(Var *) * (size_t)s->fcap);
    }
    v->slot = s->nfree;
    s->freevars[s->nfree++] = v;
}
static Var *find_free(Scope *s, Str *nm)
{
    int i;
    for (i = 0; i < s->nfree; i++)
        if (s->freevars[i]->name == nm) return s->freevars[i];
    return NULL;
}

/* variable of the parent scope that supplies cell `name` for scope x, or NULL = global */
static Var *outer_ref(Scope *x, Str *name)
{
    Scope *p = x->parent;
    Var *v, *src, *fv;
    if (!p || p->kind == SC_MOD) return NULL;
    if (p->kind == SC_FUNC) {
        v = var_find(p, name);
        if (v && (v->kind == VK_LOCAL || v->kind == VK_CELL)) {
            v->kind = VK_CELL;
            return v;
        }
        if (v && v->kind == VK_GLOBAL) return NULL;
    }
    fv = find_free(p, name);
    if (fv) return fv;
    src = outer_ref(p, name);
    if (!src) return NULL;
    fv = var_new(name);
    fv->kind = VK_FREE;
    fv->src = src;
    add_free(p, fv);
    return fv;
}

static void resolve_var(Scope *s, Var *v)
{
    Var *src;
    if (s->kind == SC_MOD || v->isglobal) {
        v->kind = VK_GLOBAL;
        v->slot = (int)global_index(v->name);
        return;
    }
    if (v->isnonlocal) {
        src = outer_ref(s, v->name);
        if (!src) syntax_error(0, "SyntaxError: no binding for nonlocal '%.60s' found", v->name->s);
        v->kind = VK_FREE;
        v->src = src;
        add_free(s, v);
        return;
    }
    if (v->defined) {
        if (s->kind == SC_FUNC) { v->kind = VK_LOCAL; return; }
        v->kind = VK_NS;
        {
            Var *fb = var_new(v->name);
            src = outer_ref(s, v->name);
            if (src) {
                Var *ex = find_free(s, v->name);
                if (ex) { free(fb); fb = ex; }
                else { fb->kind = VK_FREE; fb->src = src; add_free(s, fb); }
            } else {
                fb->kind = VK_GLOBAL;
                fb->slot = (int)global_index(v->name);
            }
            v->fb = fb;
        }
        return;
    }
    src = outer_ref(s, v->name);
    if (src) {
        Var *ex = find_free(s, v->name);
        if (ex) {
            v->kind = VK_FREE; v->slot = ex->slot; v->src = ex->src;
        } else {
            v->kind = VK_FREE;
            v->src = src;
            add_free(s, v);
        }
    } else {
        v->kind = VK_GLOBAL;
        v->slot = (int)global_index(v->name);
    }
}

static void resolve_scope(Scope *s)
{
    int i, next = 0, nc = 0;
    for (i = 0; i < s->nvars; i++) resolve_var(s, s->vars[i]);
    for (i = 0; i < s->nkids; i++) resolve_scope(s->kids[i]);
    if (s->kind == SC_FUNC) {
        for (i = 0; i < s->nvars; i++) {
            Var *v = s->vars[i];
            if (v->kind == VK_LOCAL || v->kind == VK_CELL) {
                v->slot = next++;
                if (v->kind == VK_CELL) nc++;
            }
        }
    }
    s->nlocals = next;
    if (s->fd) {
        FuncDef *fd = s->fd;
        int k = 0;
        fd->nlocals = next;
        fd->nfree = s->nfree;
        fd->fv = s->freevars;
        fd->nstate = s->nstate;
        fd->ncells = nc;
        fd->cellslots = (int *)xmalloc(sizeof(int) * (size_t)(nc + 1));
        for (i = 0; i < s->nvars; i++)
            if (s->vars[i]->kind == VK_CELL) fd->cellslots[k++] = s->vars[i]->slot;
    }
}
