/* =========================================================================
   p3_parse.c   recursive-descent parser: tokens -> AST (with scopes)
   ========================================================================= */
enum {
    N_CONST = 1, N_NAME, N_ATTR, N_SUBSCR, N_SLICE, N_CALL, N_BINOP, N_UNARY, N_AND, N_OR,
    N_NOT, N_CMP, N_IFEXP, N_LIST, N_TUPLE, N_DICT, N_SET, N_LAMBDA, N_COMP, N_FSTR, N_FPART,
    N_STAR, N_DSTAR, N_KW,
    S_EXPR, S_ASSIGN, S_AUG, S_IF, S_WHILE, S_FOR, S_BREAK, S_CONTINUE, S_RETURN, S_PASS,
    S_DEF, S_CLASS, S_TRY, S_EXCEPT, S_RAISE, S_ASSERT, S_DEL, S_IMPORT, S_FROM, S_WITH,
    S_BLOCK, S_YIELD, S_YIELDFROM, S_LISTAPP, S_SETADD, S_DICTSET, S_NOP
};
enum { C_LT = 1, C_LE, C_GT, C_GE, C_EQ, C_NE, C_IN, C_NOTIN, C_IS, C_ISNOT };

static int pp;
static Scope *cur_scope;
static Scope *mod_scope;

#define TOK (toks[pp])

static Node *node_new(int kind, int line)
{
    Node *n = (Node *)xmalloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->kind = (short)kind;
    n->line = line;
    n->val = NONE_V;
    n->cval = UNSET_V;
    return n;
}
static Node *mk(int kind)
{
    return node_new(kind, TOK.line);
}
static void node_add(Node *n, Node *c)
{
    n->list = (Node **)xrealloc(n->list, sizeof(Node *) * (size_t)(n->n + 1));
    n->list[n->n++] = c;
}

/* ---- scopes ----------------------------------------------------------- */
static Scope *scope_new(int kind, Scope *parent)
{
    Scope *s = (Scope *)xmalloc(sizeof(Scope));
    memset(s, 0, sizeof(Scope));
    s->kind = kind;
    s->parent = parent;
    if (parent) {
        if (parent->nkids >= parent->kcap) {
            parent->kcap = parent->kcap ? parent->kcap * 2 : 4;
            parent->kids = (Scope **)xrealloc(parent->kids, sizeof(Scope *) * (size_t)parent->kcap);
        }
        parent->kids[parent->nkids++] = s;
    }
    return s;
}
static Var *var_find(Scope *s, Str *nm)
{
    int i;
    for (i = 0; i < s->nvars; i++)
        if (s->vars[i]->name == nm) return s->vars[i];
    return NULL;
}
static Var *var_new(Str *nm)
{
    Var *v = (Var *)xmalloc(sizeof(Var));
    memset(v, 0, sizeof(Var));
    v->name = nm;
    return v;
}
static Var *scope_var(Scope *s, Str *nm)
{
    Var *v = var_find(s, nm);
    if (v) return v;
    v = var_new(nm);
    if (s->nvars >= s->vcap) {
        s->vcap = s->vcap ? s->vcap * 2 : 8;
        s->vars = (Var **)xrealloc(s->vars, sizeof(Var *) * (size_t)s->vcap);
    }
    s->vars[s->nvars++] = v;
    return v;
}

static void perr(const char *msg)
{
    syntax_error(TOK.line, "SyntaxError: %s", msg);
}
static int at(int type)
{
    return TOK.type == type;
}
static int atkw(int kw)
{
    return TOK.type == TK_KW && TOK.kw == kw;
}
static int accept(int type)
{
    if (TOK.type == type) { pp++; return 1; }
    return 0;
}
static int accept_kw(int kw)
{
    if (atkw(kw)) { pp++; return 1; }
    return 0;
}
static void expect(int type, const char *what)
{
    if (TOK.type != type) {
        char b[120];
        sprintf(b, "expected %s", what);
        perr(b);
    }
    pp++;
}
static void expect_kw(int kw, const char *what)
{
    if (!atkw(kw)) {
        char b[120];
        sprintf(b, "expected '%s'", what);
        perr(b);
    }
    pp++;
}

static Node *mk_name(Str *nm, int line)
{
    Node *n = node_new(N_NAME, line);
    n->name = nm;
    n->var = scope_var(cur_scope, nm);
    return n;
}
static Node *mk_const(Value v, int line)
{
    Node *n = node_new(N_CONST, line);
    n->val = v;
    return n;
}
static Value perm_str(const char *s, long len)
{
    int sp = gc_perm;
    Value v;
    gc_perm = 1;
    v = mkstr(s, len);
    gc_perm = sp;
    return v;
}

/* mark a target expression as assigned */
static void to_target(Node *n, int line)
{
    int i;
    switch (n->kind) {
    case N_NAME: n->var->defined = 1; break;
    case N_ATTR: case N_SUBSCR: break;
    case N_TUPLE: case N_LIST:
        for (i = 0; i < n->n; i++) to_target(n->list[i], line);
        break;
    case N_STAR: to_target(n->a, line); break;
    default: syntax_error(line, "SyntaxError: cannot assign to expression");
    }
}

static Node *new_block(int line);
static Node *parse_test(void);
static Node *parse_or(void);
static Node *parse_bor(void);
static Node *parse_testlist(int allow_star);
static Node *parse_block(void);
static Node *parse_stmt(void);
static Node *parse_atom(void);
static Node *parse_comprehension(int ckind, int closer);
static Node *parse_fstring_tok(Tok *t);

/* is there a 'for' at bracket depth 0 before the closing bracket? */
static int scan_for(int i, int stop_comma)
{
    int depth = 0;
    for (;; i++) {
        int t = toks[i].type;
        if (t == TK_EOF || t == TK_NEWLINE) return 0;
        if (t == TK_KW && toks[i].kw == KW_FOR && depth == 0) return 1;
        if (t == '(' || t == '[' || t == '{') depth++;
        else if (t == ')' || t == ']' || t == '}') {
            if (depth == 0) return 0;
            depth--;
        } else if (t == ',' && depth == 0 && stop_comma) return 0;
    }
}

/* ---- functions -------------------------------------------------------- */
typedef struct Params {
    Str **names; Node **defs; int n, ndef;
    Str **kwnames; Node **kwdefs; int nkw;
    Str *vararg, *varkw;
} Params;

static void parse_params(Params *p, int closer, int annot)
{
    int star = 0;
    memset(p, 0, sizeof(Params));
    while (!at(closer)) {
        if (at('*')) {
            pp++;
            star = 1;
            if (at(TK_NAME)) {
                p->vararg = TOK.s; pp++;
                if (annot && accept(':')) parse_test();
            }
        } else if (at(TK_POW)) {
            pp++;
            if (!at(TK_NAME)) perr("expected name");
            p->varkw = TOK.s; pp++;
            if (annot && accept(':')) parse_test();
        } else if (at('/')) {
            pp++;
        } else {
            Str *nm;
            Node *def = NULL;
            if (!at(TK_NAME)) perr("expected parameter name");
            nm = TOK.s; pp++;
            if (annot && accept(':')) parse_test();
            if (accept('=')) def = parse_test();
            if (!star) {
                p->names = (Str **)xrealloc(p->names, sizeof(Str *) * (size_t)(p->n + 1));
                p->names[p->n++] = nm;
                if (def) {
                    p->defs = (Node **)xrealloc(p->defs, sizeof(Node *) * (size_t)(p->ndef + 1));
                    p->defs[p->ndef++] = def;
                } else if (p->ndef > 0) perr("non-default argument follows default argument");
            } else {
                p->kwnames = (Str **)xrealloc(p->kwnames, sizeof(Str *) * (size_t)(p->nkw + 1));
                p->kwdefs = (Node **)xrealloc(p->kwdefs, sizeof(Node *) * (size_t)(p->nkw + 1));
                p->kwnames[p->nkw] = nm;
                p->kwdefs[p->nkw++] = def;
            }
        }
        if (!accept(',')) break;
    }
}

static FuncDef *fd_new(Str *name, Params *p, int line)
{
    FuncDef *fd = (FuncDef *)xmalloc(sizeof(FuncDef));
    Scope *sc = scope_new(SC_FUNC, cur_scope);
    int i, k = 0;
    Var *v;
    memset(fd, 0, sizeof(FuncDef));
    fd->sc = sc;
    fd->name = name;
    fd->line = line;
    fd->npos = p->n;
    fd->ndefaults = p->ndef;
    fd->nkwonly = p->nkw;
    fd->defaults = p->defs;
    fd->kwdefaults = p->kwdefs;
    fd->pnames = (Str **)xmalloc(sizeof(Str *) * (size_t)(p->n + p->nkw + 1));
    fd->va_slot = fd->kw_slot = -1;
    for (i = 0; i < p->n; i++) {
        v = scope_var(sc, p->names[i]); v->defined = 1; v->isparam = 1;
        fd->pnames[k++] = p->names[i];
    }
    for (i = 0; i < p->nkw; i++) {
        v = scope_var(sc, p->kwnames[i]); v->defined = 1; v->isparam = 1;
        fd->pnames[k++] = p->kwnames[i];
    }
    if (p->vararg) {
        v = scope_var(sc, p->vararg); v->defined = 1; v->isparam = 1;
        fd->va_slot = k++;
    }
    if (p->varkw) {
        v = scope_var(sc, p->varkw); v->defined = 1; v->isparam = 1;
        fd->kw_slot = k++;
    }
    sc->nparams = k;
    sc->fd = fd;
    return fd;
}

static Node *parse_funcdef(void)
{
    Node *n = mk(S_DEF);
    Params p;
    Scope *saved;
    Str *name;
    int line = TOK.line;
    pp++;
    if (!at(TK_NAME)) perr("expected function name");
    name = TOK.s; pp++;
    expect('(', "(");
    parse_params(&p, ')', 1);
    expect(')', ")");
    if (accept(TK_ARROW)) parse_test();
    expect(':', ":");
    n->name = name;
    n->var = scope_var(cur_scope, name);
    n->var->defined = 1;
    n->fd = fd_new(name, &p, line);
    saved = cur_scope;
    cur_scope = n->fd->sc;
    n->fd->body = parse_block();
    n->fd->isgen = cur_scope->isgen;
    cur_scope = saved;
    return n;
}

static Node *parse_classdef(void)
{
    Node *n = mk(S_CLASS);
    Scope *saved;
    FuncDef *fd;
    Params p;
    pp++;
    if (!at(TK_NAME)) perr("expected class name");
    n->name = TOK.s; pp++;
    n->var = scope_var(cur_scope, n->name);
    n->var->defined = 1;
    if (accept('(')) {
        while (!at(')')) {
            if (at(TK_NAME) && toks[pp + 1].type == '=') { pp += 2; parse_test(); }
            else node_add(n, parse_test());
            if (!accept(',')) break;
        }
        expect(')', ")");
    }
    expect(':', ":");
    memset(&p, 0, sizeof(p));
    fd = (FuncDef *)xmalloc(sizeof(FuncDef));
    memset(fd, 0, sizeof(FuncDef));
    fd->sc = scope_new(SC_CLASS, cur_scope);
    fd->sc->fd = fd;
    fd->name = n->name;
    fd->isclass = 1;
    fd->va_slot = fd->kw_slot = -1;
    fd->pnames = (Str **)xmalloc(sizeof(Str *));
    n->fd = fd;
    saved = cur_scope;
    cur_scope = fd->sc;
    fd->body = parse_block();
    cur_scope = saved;
    return n;
}

/* ---- expressions ------------------------------------------------------ */
static Node *parse_lambda(void)
{
    Node *n = mk(N_LAMBDA);
    Params p;
    Scope *saved;
    Node *ret;
    int line = TOK.line;
    pp++;
    parse_params(&p, ':', 0);
    expect(':', ":");
    n->fd = fd_new(intern_c("<lambda>"), &p, line);
    saved = cur_scope;
    cur_scope = n->fd->sc;
    ret = mk(S_RETURN);
    ret->a = parse_test();
    n->fd->body = new_block(line);
    node_add(n->fd->body, ret);
    n->fd->isgen = cur_scope->isgen;
    cur_scope = saved;
    return n;
}

static Node *parse_test(void)
{
    Node *e;
    if (atkw(KW_LAMBDA)) return parse_lambda();
    e = parse_or();
    if (atkw(KW_IF)) {
        Node *n = mk(N_IFEXP);
        pp++;
        n->b = e;
        n->a = parse_or();
        expect_kw(KW_ELSE, "else");
        n->c = parse_test();
        return n;
    }
    return e;
}
static Node *parse_and(void);
static Node *parse_not(void);
static Node *parse_cmp(void);

static Node *parse_or(void)
{
    Node *l = parse_and();
    while (atkw(KW_OR)) {
        Node *n = mk(N_OR);
        pp++;
        n->a = l;
        n->b = parse_and();
        l = n;
    }
    return l;
}
static Node *parse_and(void)
{
    Node *l = parse_not();
    while (atkw(KW_AND)) {
        Node *n = mk(N_AND);
        pp++;
        n->a = l;
        n->b = parse_not();
        l = n;
    }
    return l;
}
static Node *parse_not(void)
{
    if (atkw(KW_NOT)) {
        Node *n = mk(N_NOT);
        pp++;
        n->a = parse_not();
        return n;
    }
    return parse_cmp();
}
static int cmp_op(void)
{
    int t = TOK.type;
    if (t == '<') return C_LT;
    if (t == '>') return C_GT;
    if (t == TK_EQ) return C_EQ;
    if (t == TK_NE) return C_NE;
    if (t == TK_LE) return C_LE;
    if (t == TK_GE) return C_GE;
    if (atkw(KW_IN)) return C_IN;
    if (atkw(KW_IS)) return C_IS;
    if (atkw(KW_NOT) && toks[pp + 1].type == TK_KW && toks[pp + 1].kw == KW_IN) return C_NOTIN;
    return 0;
}
static Node *parse_cmp(void)
{
    Node *l = parse_bor();
    int op = cmp_op();
    Node *n = NULL;
    char ops[64];
    int nops = 0;
    while (op) {
        if (!n) { n = mk(N_CMP); node_add(n, l); }
        pp++;
        if (op == C_NOTIN) pp++;
        if (op == C_IS && atkw(KW_NOT)) { pp++; op = C_ISNOT; }
        if (nops < 60) ops[nops++] = (char)op;
        node_add(n, parse_bor());
        op = cmp_op();
    }
    if (!n) return l;
    n->name = (Str *)str_new(ops, nops);
    n->name->h.perm = 1;
    n->op = (short)nops;
    return n;
}
static Node *binop_node(int op, Node *a, Node *b, int line)
{
    Node *n = node_new(N_BINOP, line);
    n->op = (short)op;
    n->a = a;
    n->b = b;
    return n;
}
static Node *parse_bxor(void);
static Node *parse_band(void);
static Node *parse_shift(void);
static Node *parse_arith(void);
static Node *parse_term(void);
static Node *parse_factor(void);
static Node *parse_power(void);
static Node *parse_primary(void);

static Node *parse_bor(void)
{
    Node *l = parse_bxor();
    while (at('|')) {
        int line = TOK.line;
        pp++;
        l = binop_node(B_OR, l, parse_bxor(), line);
    }
    return l;
}
static Node *parse_bxor(void)
{
    Node *l = parse_band();
    while (at('^')) {
        int line = TOK.line;
        pp++;
        l = binop_node(B_XOR, l, parse_band(), line);
    }
    return l;
}
static Node *parse_band(void)
{
    Node *l = parse_shift();
    while (at('&')) {
        int line = TOK.line;
        pp++;
        l = binop_node(B_AND, l, parse_shift(), line);
    }
    return l;
}
static Node *parse_shift(void)
{
    Node *l = parse_arith();
    while (at(TK_LSH) || at(TK_RSH)) {
        int op = at(TK_LSH) ? B_LSH : B_RSH, line = TOK.line;
        pp++;
        l = binop_node(op, l, parse_arith(), line);
    }
    return l;
}
static Node *parse_arith(void)
{
    Node *l = parse_term();
    while (at('+') || at('-')) {
        int op = at('+') ? B_ADD : B_SUB, line = TOK.line;
        pp++;
        l = binop_node(op, l, parse_term(), line);
    }
    return l;
}
static Node *parse_term(void)
{
    Node *l = parse_factor();
    while (at('*') || at('/') || at('%') || at(TK_FDIV)) {
        int op = at('*') ? B_MUL : (at('/') ? B_DIV : (at('%') ? B_MOD : B_FDIV));
        int line = TOK.line;
        pp++;
        l = binop_node(op, l, parse_factor(), line);
    }
    return l;
}
static Node *parse_factor(void)
{
    if (at('-') || at('+') || at('~')) {
        Node *n = mk(N_UNARY);
        int op = TOK.type;
        pp++;
        n->op = (short)op;
        n->a = parse_factor();
        if (op == '-' && n->a->kind == N_CONST) {
            Value v = n->a->val;
            if (v.t == T_INT) { n->a->val = mkint(-v.u.i); return n->a; }
            if (v.t == T_FLOAT) { n->a->val = mkfloat(-v.u.d); return n->a; }
        }
        return n;
    }
    return parse_power();
}
static Node *parse_power(void)
{
    Node *l = parse_primary();
    if (at(TK_POW)) {
        int line = TOK.line;
        pp++;
        return binop_node(B_POW, l, parse_factor(), line);
    }
    return l;
}

static Node *parse_slice_item(void)
{
    Node *a = NULL, *b = NULL, *c = NULL;
    int line = TOK.line;
    if (!at(':')) {
        a = parse_test();
        if (!at(':')) return a;
    }
    expect(':', ":");
    if (!at(':') && !at(']') && !at(',')) b = parse_test();
    if (accept(':')) {
        if (!at(']') && !at(',')) c = parse_test();
    }
    {
        Node *n = node_new(N_SLICE, line);
        n->a = a; n->b = b; n->c = c;
        return n;
    }
}

static Node *parse_call_args(Node *call)
{
    while (!at(')')) {
        Node *arg;
        if (at('*')) {
            arg = mk(N_STAR); pp++;
            arg->a = parse_test();
        } else if (at(TK_POW)) {
            arg = mk(N_DSTAR); pp++;
            arg->a = parse_test();
        } else if (at(TK_NAME) && toks[pp + 1].type == '=') {
            arg = mk(N_KW);
            arg->name = TOK.s;
            pp += 2;
            arg->a = parse_test();
        } else if (scan_for(pp, 1)) {
            arg = parse_comprehension('g', 0);
        } else {
            arg = parse_test();
        }
        node_add(call, arg);
        if (!accept(',')) break;
    }
    expect(')', ")");
    return call;
}

static Node *parse_primary(void)
{
    Node *e = parse_atom();
    for (;;) {
        if (at('.')) {
            Node *n = mk(N_ATTR);
            pp++;
            if (!at(TK_NAME) && !at(TK_KW)) perr("expected attribute name");
            n->a = e;
            n->name = TOK.s ? TOK.s : intern_c(kw_names[TOK.kw]);
            pp++;
            e = n;
        } else if (at('(')) {
            Node *n = mk(N_CALL);
            pp++;
            n->a = e;
            parse_call_args(n);
            e = n;
        } else if (at('[')) {
            Node *n = mk(N_SUBSCR);
            Node *idx;
            pp++;
            idx = parse_slice_item();
            if (at(',')) {
                Node *t = mk(N_TUPLE);
                node_add(t, idx);
                while (accept(',')) {
                    if (at(']')) break;
                    node_add(t, parse_slice_item());
                }
                idx = t;
            }
            expect(']', "]");
            n->a = e;
            n->b = idx;
            e = n;
        } else break;
    }
    return e;
}

/* star-aware expression: *x or test */
static Node *parse_star_or_test(void)
{
    if (at('*')) {
        Node *n = mk(N_STAR);
        pp++;
        n->a = parse_bor();
        return n;
    }
    return parse_test();
}

/* comma separated list; builds a tuple when a comma is present */
static Node *parse_testlist(int allow_star)
{
    Node *first = allow_star ? parse_star_or_test() : parse_test();
    if (at(',')) {
        Node *t = mk(N_TUPLE);
        node_add(t, first);
        while (accept(',')) {
            if (at(TK_NEWLINE) || at('=') || at(')') || at(']') || at(':') || at(TK_AUG) ||
                at(TK_EOF) || at(';') || atkw(KW_IN)) break;
            node_add(t, allow_star ? parse_star_or_test() : parse_test());
        }
        return t;
    }
    if (first->kind == N_STAR) {
        Node *t = mk(N_TUPLE);
        node_add(t, first);
        return t;
    }
    return first;
}

/* targets of for loops (stop before 'in') */
static Node *parse_targets(void)
{
    Node *first, *t;
    if (at('*')) { first = mk(N_STAR); pp++; first->a = parse_bor(); }
    else first = parse_bor();
    if (at(',')) {
        t = mk(N_TUPLE);
        node_add(t, first);
        while (accept(',')) {
            Node *x;
            if (atkw(KW_IN) || at('=')) break;
            if (at('*')) { x = mk(N_STAR); pp++; x->a = parse_bor(); }
            else x = parse_bor();
            node_add(t, x);
        }
        first = t;
    }
    to_target(first, TOK.line);
    return first;
}

/* comprehension: element expr already NOT parsed; we are right after the opener.
   ckind: 'l' list, 's' set, 'd' dict, 'g' generator.  Consumes the closer. */
static Node *parse_comprehension(int ckind, int closer)
{
    Node *comp = mk(N_COMP);
    Scope *outer = cur_scope, *sc;
    FuncDef *fd;
    Params p;
    Node *elt = NULL, *key = NULL, *val = NULL;
    Node *clauses[64];
    int ctype[64], nc = 0, i, line = TOK.line;
    Node *iter0 = NULL;
    Node *body, *block, *rname;
    Str *s0 = intern_c(".0"), *sr = intern_c(".r");

    memset(&p, 0, sizeof(p));
    p.names = (Str **)xmalloc(sizeof(Str *));
    p.names[0] = s0;
    p.n = 1;
    fd = fd_new(intern_c(ckind == 'g' ? "<genexpr>" : "<comp>"), &p, line);
    sc = fd->sc;
    cur_scope = sc;
    if (ckind == 'd') {
        key = parse_test();
        expect(':', ":");
        val = parse_test();
    } else {
        elt = parse_test();
    }
    /* clauses */
    while (atkw(KW_FOR) || atkw(KW_IF)) {
        if (nc >= 60) perr("too many comprehension clauses");
        if (accept_kw(KW_FOR)) {
            Node *f = mk(S_FOR);
            f->a = parse_targets();
            expect_kw(KW_IN, "in");
            if (nc == 0) {
                cur_scope = outer;
                iter0 = parse_or();
                cur_scope = sc;
                f->b = mk_name(s0, line);
            } else {
                f->b = parse_or();
            }
            f->sidx = sc->nstate;
            sc->nstate += 2;
            clauses[nc] = f; ctype[nc++] = 1;
        } else {
            Node *c = mk(S_IF);
            pp++;
            c->a = parse_or();
            c->sidx = sc->nstate++;
            while (atkw(KW_IF)) {   /* chained ifs: and */
                Node *a2 = mk(N_AND);
                pp++;
                a2->a = c->a;
                a2->b = parse_or();
                c->a = a2;
            }
            clauses[nc] = c; ctype[nc++] = 2;
        }
    }
    if (closer) expect(closer, ")");
    /* innermost statement */
    rname = mk_name(sr, line);
    rname->var->defined = 1;
    if (ckind == 'g') {
        body = node_new(S_YIELD, line);
        body->a = elt;
        body->sidx = sc->nstate++;
        sc->isgen = 1;
    } else if (ckind == 'l') {
        body = node_new(S_LISTAPP, line);
        body->a = mk_name(sr, line); body->b = elt;
    } else if (ckind == 's') {
        body = node_new(S_SETADD, line);
        body->a = mk_name(sr, line); body->b = elt;
    } else {
        body = node_new(S_DICTSET, line);
        body->a = mk_name(sr, line); body->b = key; body->c = val;
    }
    for (i = nc - 1; i >= 0; i--) {
        Node *bl = node_new(S_BLOCK, line);
        bl->sidx = sc->nstate++;
        node_add(bl, body);
        if (ctype[i] == 1) clauses[i]->c = bl; else clauses[i]->b = bl;
        body = clauses[i];
    }
    block = node_new(S_BLOCK, line);
    block->sidx = sc->nstate++;
    if (ckind != 'g') {
        Node *as = node_new(S_ASSIGN, line);
        Node *init = node_new(ckind == 'l' ? N_LIST : (ckind == 's' ? N_SET : N_DICT), line);
        as->a = init;
        node_add(as, rname);
        node_add(block, as);
    }
    node_add(block, body);
    if (ckind != 'g') {
        Node *r = node_new(S_RETURN, line);
        r->a = mk_name(sr, line);
        node_add(block, r);
    }
    fd->body = block;
    fd->isgen = sc->isgen;
    cur_scope = outer;
    comp->fd = fd;
    comp->a = iter0;
    comp->op = (short)ckind;
    return comp;
}

static Node *parse_fstring_tok(Tok *t);

static Node *parse_atom(void)
{
    int line = TOK.line;
    int t = TOK.type;
    Node *n;
    if (t == TK_NAME) {
        n = mk_name(TOK.s, line);
        pp++;
        return n;
    }
    if (t == TK_NUM) { n = mk_const(TOK.val, line); pp++; return n; }
    if (t == TK_FLOAT) { n = mk_const(mkfloat(TOK.fval), line); pp++; return n; }
    if (t == TK_STR || t == TK_FSTR) {
        int anyf = 0, i = pp;
        while (toks[i].type == TK_STR || toks[i].type == TK_FSTR) {
            if (toks[i].type == TK_FSTR) anyf = 1;
            i++;
        }
        if (!anyf) {
            Buf b;
            buf_init(&b);
            while (pp < i) { buf_addn(&b, TOK.s->s, TOK.s->len); pp++; }
            n = mk_const(perm_str(b.s, b.n), line);
            buf_free(&b);
            return n;
        } else {
            Node *f = node_new(N_FSTR, line);
            while (pp < i) {
                if (TOK.type == TK_STR) {
                    node_add(f, mk_const(perm_str(TOK.s->s, TOK.s->len), line));
                    pp++;
                } else {
                    Node *sub = parse_fstring_tok(&TOK);
                    int k;
                    pp++;
                    for (k = 0; k < sub->n; k++) node_add(f, sub->list[k]);
                }
            }
            return f;
        }
    }
    if (t == TK_KW) {
        if (TOK.kw == KW_NONE_) { pp++; return mk_const(NONE_V, line); }
        if (TOK.kw == KW_TRUE) { pp++; return mk_const(TRUE_V, line); }
        if (TOK.kw == KW_FALSE) { pp++; return mk_const(FALSE_V, line); }
        if (TOK.kw == KW_LAMBDA) return parse_lambda();
    }
    if (t == TK_ELLIPSIS) { pp++; return mk_const(NONE_V, line); }
    if (t == '(') {
        pp++;
        if (at(')')) { pp++; return node_new(N_TUPLE, line); }
        if (scan_for(pp, 0)) return parse_comprehension('g', ')');
        {
            Node *e = parse_star_or_test();
            if (at(',')) {
                n = node_new(N_TUPLE, line);
                node_add(n, e);
                while (accept(',')) {
                    if (at(')')) break;
                    node_add(n, parse_star_or_test());
                }
                expect(')', ")");
                return n;
            }
            expect(')', ")");
            if (e->kind == N_STAR) perr("cannot use starred expression here");
            return e;
        }
    }
    if (t == '[') {
        pp++;
        if (at(']')) { pp++; return node_new(N_LIST, line); }
        if (scan_for(pp, 0)) return parse_comprehension('l', ']');
        n = node_new(N_LIST, line);
        while (!at(']')) {
            node_add(n, parse_star_or_test());
            if (!accept(',')) break;
        }
        expect(']', "]");
        return n;
    }
    if (t == '{') {
        pp++;
        if (at('}')) { pp++; return node_new(N_DICT, line); }
        if (scan_for(pp, 0)) {
            /* decide dict or set comprehension: look for ':' at depth 0 before 'for' */
            int i = pp, depth = 0, isd = 0;
            for (;; i++) {
                int tt = toks[i].type;
                if (tt == TK_EOF) break;
                if (tt == '(' || tt == '[' || tt == '{') depth++;
                else if (tt == ')' || tt == ']' || tt == '}') depth--;
                else if (depth == 0 && tt == TK_KW && toks[i].kw == KW_FOR) break;
                else if (depth == 0 && tt == TK_KW && toks[i].kw == KW_LAMBDA) { /* skip */ }
                else if (depth == 0 && tt == ':') { isd = 1; break; }
            }
            return parse_comprehension(isd ? 'd' : 's', '}');
        }
        {
            Node *first;
            if (at(TK_POW)) {
                n = node_new(N_DICT, line);
                pp++;
                {
                    Node *ds = node_new(N_DSTAR, line);
                    ds->a = parse_bor();
                    node_add(n, ds);
                    node_add(n, NULL);
                }
            } else {
                first = parse_star_or_test();
                if (at(':')) {
                    n = node_new(N_DICT, line);
                    pp++;
                    node_add(n, first);
                    node_add(n, parse_test());
                } else {
                    n = node_new(N_SET, line);
                    node_add(n, first);
                    while (accept(',')) {
                        if (at('}')) break;
                        node_add(n, parse_star_or_test());
                    }
                    expect('}', "}");
                    return n;
                }
            }
            while (accept(',')) {
                if (at('}')) break;
                if (at(TK_POW)) {
                    Node *ds = node_new(N_DSTAR, line);
                    pp++;
                    ds->a = parse_bor();
                    node_add(n, ds);
                    node_add(n, NULL);
                } else {
                    node_add(n, parse_test());
                    expect(':', ":");
                    node_add(n, parse_test());
                }
            }
            expect('}', "}");
            return n;
        }
    }
    perr("invalid syntax");
    return NULL;
}

/* ---- f-strings -------------------------------------------------------- */
static Node *parse_sub_expr(const char *text, long len, int line)
{
    Tok *s_toks = toks;
    int s_n = ntoks, s_cap = tokcap, s_pp = pp;
    Buf b;
    Node *e;
    buf_init(&b);
    buf_addc(&b, '(');
    buf_addn(&b, text, len);
    buf_addc(&b, ')');
    toks = NULL; ntoks = 0; tokcap = 0;
    lex_source(b.s, b.n);
    buf_free(&b);
    pp = 0;
    e = parse_testlist(1);
    if (!at(TK_NEWLINE) && !at(TK_EOF)) syntax_error(line, "SyntaxError: f-string: invalid syntax");
    free(toks);
    toks = s_toks; ntoks = s_n; tokcap = s_cap; pp = s_pp;
    return e;
}

static Node *fstring_parts(const char *s, long n, int raw, int line)
{
    Node *f = node_new(N_FSTR, line);
    Buf lit;
    long i = 0;
    buf_init(&lit);
    while (i < n) {
        int c = s[i];
        if (c == '{' && i + 1 < n && s[i + 1] == '{') { buf_addc(&lit, '{'); i += 2; continue; }
        if (c == '}' && i + 1 < n && s[i + 1] == '}') { buf_addc(&lit, '}'); i += 2; continue; }
        if (c == '\\' && !raw && i + 1 < n) { buf_addc(&lit, c); buf_addc(&lit, s[i + 1]); i += 2; continue; }
        if (c == '{') {
            long st = i + 1, j = st;
            int depth = 0, q = 0, conv = 0;
            long eend, sst = -1, send = -1;
            Node *part;
            /* flush literal */
            {
                Buf u;
                buf_init(&u);
                if (raw) buf_addn(&u, lit.s, lit.n); else unescape(lit.s, lit.n, &u);
                if (u.n > 0) node_add(f, mk_const(perm_str(u.s, u.n), line));
                buf_free(&u);
                lit.n = 0; lit.s[0] = 0;
            }
            while (j < n) {
                int d = s[j];
                if (q) { if (d == q) q = 0; j++; continue; }
                if (d == '\'' || d == '"') { q = d; j++; continue; }
                if (d == '(' || d == '[' || d == '{') depth++;
                else if (d == ')' || d == ']') depth--;
                else if (d == '}') { if (depth == 0) break; depth--; }
                else if (depth == 0 && d == '!' && j + 1 < n && s[j + 1] != '=') break;
                else if (depth == 0 && d == ':') break;
                j++;
            }
            if (j >= n) syntax_error(line, "SyntaxError: f-string: expecting '}'");
            eend = j;
            if (s[j] == '!') {
                conv = s[j + 1];
                j += 2;
            }
            if (j < n && s[j] == ':') {
                int dd = 0;
                j++;
                sst = j;
                while (j < n) {
                    if (s[j] == '{') dd++;
                    else if (s[j] == '}') { if (dd == 0) break; dd--; }
                    j++;
                }
                send = j;
            }
            if (j >= n || s[j] != '}') syntax_error(line, "SyntaxError: f-string: expecting '}'");
            part = node_new(N_FPART, line);
            {
                long te = eend;
                while (te > st && s[te - 1] == ' ') te--;
                if (te > st && s[te - 1] == '=' && (te - 1 == st || !strchr("=!<>", s[te - 2]))) {
                    Buf u;
                    buf_init(&u);
                    buf_addn(&u, s + st, eend - st);
                    node_add(f, mk_const(perm_str(u.s, u.n), line));
                    buf_free(&u);
                    if (!conv && sst < 0) conv = 'r';
                    eend = te - 1;
                }
            }
            part->a = parse_sub_expr(s + st, eend - st, line);
            part->op = (short)conv;
            if (sst >= 0) part->b = fstring_parts(s + sst, send - sst, raw, line);
            node_add(f, part);
            i = j + 1;
            continue;
        }
        buf_addc(&lit, c);
        i++;
    }
    {
        Buf u;
        buf_init(&u);
        if (raw) buf_addn(&u, lit.s, lit.n); else unescape(lit.s, lit.n, &u);
        if (u.n > 0) node_add(f, mk_const(perm_str(u.s, u.n), line));
        buf_free(&u);
    }
    buf_free(&lit);
    return f;
}
static Node *parse_fstring_tok(Tok *t)
{
    return fstring_parts(t->s->s, t->s->len, t->raw, t->line);
}

/* ---- statements ------------------------------------------------------- */
static Node *new_block(int line)
{
    Node *b = node_new(S_BLOCK, line);
    b->sidx = cur_scope->nstate++;
    return b;
}

static void end_simple(void)
{
    if (accept(';')) {
        if (at(TK_NEWLINE)) pp++;
        return;
    }
    if (at(TK_NEWLINE)) { pp++; return; }
    if (at(TK_EOF) || at(TK_DEDENT)) return;
    perr("invalid syntax");
}

static void check_yield_scope(void)
{
    if (cur_scope->kind != SC_FUNC) perr("'yield' outside function");
    cur_scope->isgen = 1;
}

static Node *parse_import(void)
{
    int line = TOK.line;
    if (atkw(KW_IMPORT)) {
        Node *n = node_new(S_IMPORT, line);
        pp++;
        do {
            Node *it = mk(N_NAME);
            Buf b;
            Str *first = NULL;
            buf_init(&b);
            if (!at(TK_NAME)) perr("expected module name");
            first = TOK.s;
            buf_add(&b, TOK.s->s); pp++;
            while (at('.')) {
                pp++;
                buf_addc(&b, '.');
                if (!at(TK_NAME)) perr("expected name");
                buf_add(&b, TOK.s->s); pp++;
            }
            it->name = intern(b.s, b.n);
            buf_free(&b);
            if (accept_kw(KW_AS)) {
                if (!at(TK_NAME)) perr("expected name");
                it->b = mk_name(TOK.s, line); pp++;
                it->op = 1;
            } else {
                it->b = mk_name(first, line);
                it->op = 0;
            }
            it->b->var->defined = 1;
            node_add(n, it);
        } while (accept(','));
        return n;
    } else {
        Node *n = node_new(S_FROM, line);
        Buf b;
        int paren;
        pp++;
        buf_init(&b);
        while (at('.')) pp++;
        if (!at(TK_NAME)) perr("expected module name");
        buf_add(&b, TOK.s->s); pp++;
        while (at('.')) { pp++; buf_addc(&b, '.'); buf_add(&b, TOK.s->s); pp++; }
        n->name = intern(b.s, b.n);
        buf_free(&b);
        expect_kw(KW_IMPORT, "import");
        if (at('*')) { pp++; n->op = 1; return n; }
        paren = accept('(');
        for (;;) {
            Node *it = mk(N_NAME);
            if (!at(TK_NAME)) perr("expected name");
            it->name = TOK.s; pp++;
            if (accept_kw(KW_AS)) {
                if (!at(TK_NAME)) perr("expected name");
                it->b = mk_name(TOK.s, line); pp++;
            } else it->b = mk_name(it->name, line);
            it->b->var->defined = 1;
            node_add(n, it);
            if (!accept(',')) break;
            if (paren && at(')')) break;
        }
        if (paren) expect(')', ")");
        return n;
    }
}

static Node *parse_simple(void)
{
    int line = TOK.line;
    Node *n;
    if (TOK.type == TK_KW) {
        switch (TOK.kw) {
        case KW_PASS: pp++; return node_new(S_PASS, line);
        case KW_BREAK: pp++; return node_new(S_BREAK, line);
        case KW_CONTINUE: pp++; return node_new(S_CONTINUE, line);
        case KW_RETURN:
            n = node_new(S_RETURN, line);
            pp++;
            if (!at(TK_NEWLINE) && !at(';') && !at(TK_EOF) && !at(TK_DEDENT))
                n->a = parse_testlist(1);
            return n;
        case KW_RAISE:
            n = node_new(S_RAISE, line);
            pp++;
            if (!at(TK_NEWLINE) && !at(';') && !at(TK_EOF)) {
                n->a = parse_test();
                if (accept_kw(KW_FROM)) parse_test();
            }
            return n;
        case KW_ASSERT:
            n = node_new(S_ASSERT, line);
            pp++;
            n->a = parse_test();
            if (accept(',')) n->b = parse_test();
            return n;
        case KW_DEL:
            n = node_new(S_DEL, line);
            pp++;
            do {
                Node *t = parse_bor();
                to_target(t, line);
                node_add(n, t);
            } while (accept(',') && !at(TK_NEWLINE));
            return n;
        case KW_GLOBAL: case KW_NONLOCAL: {
            int isg = TOK.kw == KW_GLOBAL;
            pp++;
            do {
                Var *v;
                if (!at(TK_NAME)) perr("expected name");
                v = scope_var(cur_scope, TOK.s);
                if (isg) v->isglobal = 1; else v->isnonlocal = 1;
                pp++;
            } while (accept(','));
            return node_new(S_NOP, line);
        }
        case KW_IMPORT: case KW_FROM:
            return parse_import();
        case KW_YIELD:
            pp++;
            check_yield_scope();
            if (accept_kw(KW_FROM)) {
                n = node_new(S_YIELDFROM, line);
                n->a = parse_test();
            } else {
                n = node_new(S_YIELD, line);
                if (!at(TK_NEWLINE) && !at(';') && !at(TK_EOF) && !at(TK_DEDENT))
                    n->a = parse_testlist(1);
            }
            n->sidx = cur_scope->nstate++;
            return n;
        default: break;
        }
    }
    /* expression / assignment */
    {
        Node *e = parse_testlist(1);
        if (at('=')) {
            Node *as = node_new(S_ASSIGN, line);
            Node *val = e;
            while (accept('=')) {
                to_target(val, line);
                node_add(as, val);
                if (atkw(KW_YIELD)) perr("yield in assignment is not supported");
                val = parse_testlist(1);
            }
            as->a = val;
            return as;
        }
        if (at(TK_AUG)) {
            Node *a = node_new(S_AUG, line);
            a->op = (short)TOK.op;
            pp++;
            if (e->kind != N_NAME && e->kind != N_ATTR && e->kind != N_SUBSCR)
                perr("illegal expression for augmented assignment");
            to_target(e, line);
            a->a = e;
            a->b = parse_testlist(0);
            return a;
        }
        if (at(':') ) {
            /* annotation: x: int [= v] */
            pp++;
            parse_test();
            if (accept('=')) {
                Node *as = node_new(S_ASSIGN, line);
                to_target(e, line);
                node_add(as, e);
                as->a = parse_testlist(1);
                return as;
            }
            return node_new(S_NOP, line);
        }
        n = node_new(S_EXPR, line);
        n->a = e;
        return n;
    }
}

static Node *parse_suite(void)
{
    Node *b = new_block(TOK.line);
    if (accept(TK_NEWLINE)) {
        if (!accept(TK_INDENT)) perr("expected an indented block");
        while (!at(TK_DEDENT) && !at(TK_EOF)) node_add(b, parse_stmt());
        accept(TK_DEDENT);
    } else {
        for (;;) {
            node_add(b, parse_simple());
            if (accept(';')) {
                if (at(TK_NEWLINE)) { pp++; break; }
                continue;
            }
            if (at(TK_NEWLINE)) pp++;
            break;
        }
    }
    return b;
}
static Node *parse_block(void)
{
    return parse_suite();
}

static Node *parse_if(void)
{
    Node *n = mk(S_IF);
    n->sidx = cur_scope->nstate++;
    pp++;
    n->a = parse_test();
    expect(':', ":");
    n->b = parse_block();
    if (atkw(KW_ELIF)) {
        Node *eb = new_block(TOK.line);
        node_add(eb, parse_if());
        n->c = eb;
    } else if (atkw(KW_ELSE)) {
        pp++;
        expect(':', ":");
        n->c = parse_block();
    }
    return n;
}

static Node *parse_stmt(void)
{
    int line = TOK.line;
    Node *n;
    if (TOK.type == TK_KW) {
        switch (TOK.kw) {
        case KW_IF:
            return parse_if();
        case KW_WHILE:
            n = mk(S_WHILE);
            n->sidx = cur_scope->nstate;
            cur_scope->nstate += 2;
            pp++;
            n->a = parse_test();
            expect(':', ":");
            n->b = parse_block();
            if (accept_kw(KW_ELSE)) { expect(':', ":"); n->c = parse_block(); }
            return n;
        case KW_FOR:
            n = mk(S_FOR);
            n->sidx = cur_scope->nstate;
            cur_scope->nstate += 2;
            pp++;
            n->a = parse_targets();
            expect_kw(KW_IN, "in");
            n->b = parse_testlist(1);
            expect(':', ":");
            n->c = parse_block();
            if (accept_kw(KW_ELSE)) { expect(':', ":"); n->d = parse_block(); }
            return n;
        case KW_TRY: {
            n = mk(S_TRY);
            n->sidx = cur_scope->nstate;
            cur_scope->nstate += 3;
            pp++;
            expect(':', ":");
            n->a = parse_block();
            while (atkw(KW_EXCEPT)) {
                Node *h = mk(S_EXCEPT);
                pp++;
                if (!at(':')) {
                    h->a = parse_test();
                    if (accept_kw(KW_AS)) {
                        if (!at(TK_NAME)) perr("expected name");
                        h->b = mk_name(TOK.s, line);
                        h->b->var->defined = 1;
                        pp++;
                    }
                }
                expect(':', ":");
                h->c = parse_block();
                node_add(n, h);
            }
            if (atkw(KW_ELSE)) { pp++; expect(':', ":"); n->c = parse_block(); }
            if (atkw(KW_FINALLY)) { pp++; expect(':', ":"); n->d = parse_block(); }
            if (n->n == 0 && !n->d) perr("expected 'except' or 'finally' block");
            return n;
        }
        case KW_WITH: {
            Node *first = NULL, *last = NULL;
            pp++;
            do {
                Node *w = mk(S_WITH);
                w->sidx = cur_scope->nstate;
                cur_scope->nstate += 2;
                w->a = parse_test();
                if (accept_kw(KW_AS)) {
                    w->b = parse_bor();
                    to_target(w->b, line);
                }
                if (last) {
                    Node *bl = new_block(line);
                    node_add(bl, w);
                    last->c = bl;
                } else first = w;
                last = w;
            } while (accept(','));
            expect(':', ":");
            last->c = parse_block();
            return first;
        }
        case KW_DEF:
            return parse_funcdef();
        case KW_CLASS:
            return parse_classdef();
        default: break;
        }
    }
    if (at('@')) {
        Node *decs[16];
        int nd = 0, i;
        Node *d;
        while (at('@')) {
            pp++;
            if (nd >= 16) perr("too many decorators");
            decs[nd++] = parse_test();
            if (!accept(TK_NEWLINE)) perr("expected newline after decorator");
        }
        if (atkw(KW_DEF)) d = parse_funcdef();
        else if (atkw(KW_CLASS)) d = parse_classdef();
        else { perr("expected def or class after decorator"); return NULL; }
        for (i = 0; i < nd; i++) node_add(d, decs[i]);
        d->op = (short)nd;
        return d;
    }
    if (at(TK_INDENT)) perr("unexpected indent");
    n = parse_simple();
    if (n->kind == S_DEF || n->kind == S_CLASS) return n;
    end_simple();
    return n;
}

/* parse a whole module (token array already filled) */
static Node *parse_module(void)
{
    Node *b = new_block(1);
    pp = 0;
    while (!at(TK_EOF)) {
        if (at(TK_NEWLINE)) { pp++; continue; }
        if (at(TK_DEDENT)) { pp++; continue; }
        node_add(b, parse_stmt());
    }
    return b;
}
