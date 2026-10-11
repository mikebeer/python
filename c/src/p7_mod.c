/* =========================================================================
   p7_mod.c   builtin modules (math random sys time) and initialisation
   ========================================================================= */
static Module *modules[16];
static int nmodules;

static Module *new_module(const char *name)
{
    Module *m = (Module *)gc_alloc(sizeof(Module), T_MODULE);
    m->name = intern_c(name);
    m->ns.k = NULL; m->ns.v = NULL; m->ns.n = m->ns.cap = 0;
    modules[nmodules++] = m;
    return m;
}
static Value find_module(Str *nm)
{
    int i;
    for (i = 0; i < nmodules; i++)
        if (modules[i]->name == nm) return mkobj(modules[i], T_MODULE);
    return UNSET_V;
}
static void mod_def(Module *m, const char *name, BFn fn)
{
    amap_set(&m->ns, intern_c(name), mkbuiltin(name, fn));
}
static void mod_val(Module *m, const char *name, Value v)
{
    amap_set(&m->ns, intern_c(name), v);
}

/* ---- math ------------------------------------------------------------- */
static double dbl_arg(Args *x, int i)
{
    if (!is_num(A(i))) {
        if (A(i).t == T_INST) {
            Value *m = class_find(AS_INST(A(i))->cls, S_float);
            if (m) return to_dbl(call1(*m, A(i)));
        }
        throw_new(cls_TypeError, "must be real number, not %.40s", type_name(A(i)));
    }
    return to_dbl(A(i));
}
static void domain_check(int ok)
{
    if (!ok) throw_new(cls_ValueError, "math domain error");
}
#define MATH1(name, expr, dom) \
    BI(name) { double v; need(x, 1, 1, "math"); v = dbl_arg(x, 0); domain_check(dom); return mkfloat(expr); }
MATH1(m_sqrt, sqrt(v), v >= 0)
MATH1(m_sin, sin(v), 1)
MATH1(m_cos, cos(v), 1)
MATH1(m_tan, tan(v), 1)
MATH1(m_asin, asin(v), v >= -1 && v <= 1)
MATH1(m_acos, acos(v), v >= -1 && v <= 1)
MATH1(m_atan, atan(v), 1)
MATH1(m_sinh, sinh(v), 1)
MATH1(m_cosh, cosh(v), 1)
MATH1(m_tanh, tanh(v), 1)
MATH1(m_exp, exp(v), 1)
MATH1(m_log10, log10(v), v > 0)
MATH1(m_log2, log(v) / log(2.0), v > 0)
MATH1(m_fabs, fabs(v), 1)
MATH1(m_degrees, v * 180.0 / 3.14159265358979323846, 1)
MATH1(m_radians, v * 3.14159265358979323846 / 180.0, 1)
BI(m_log)
{
    double v;
    need(x, 1, 2, "log");
    v = dbl_arg(x, 0);
    domain_check(v > 0);
    if (x->n == 2) {
        double b = dbl_arg(x, 1);
        domain_check(b > 0);
        return mkfloat(log(v) / log(b));
    }
    return mkfloat(log(v));
}
BI(m_pow)
{
    need(x, 2, 2, "pow");
    return mkfloat(pow(dbl_arg(x, 0), dbl_arg(x, 1)));
}
BI(m_atan2)
{
    need(x, 2, 2, "atan2");
    return mkfloat(atan2(dbl_arg(x, 0), dbl_arg(x, 1)));
}
BI(m_hypot)
{
    need(x, 2, 2, "hypot");
    return mkfloat(sqrt(dbl_arg(x, 0) * dbl_arg(x, 0) + dbl_arg(x, 1) * dbl_arg(x, 1)));
}
BI(m_fmod)
{
    need(x, 2, 2, "fmod");
    return mkfloat(fmod(dbl_arg(x, 0), dbl_arg(x, 1)));
}
BI(m_copysign)
{
    double a, b;
    need(x, 2, 2, "copysign");
    a = fabs(dbl_arg(x, 0));
    b = dbl_arg(x, 1);
    return mkfloat((b < 0 || (b == 0 && 1.0 / b < 0)) ? -a : a);
}
static Value dbl_to_int(double d)
{
    return int_from_double(d);
}
BI(m_floor)
{
    need(x, 1, 1, "floor");
    if (is_intv(A(0))) return A(0).t == T_BOOL ? mkint(A(0).u.i) : A(0);
    return dbl_to_int(floor(dbl_arg(x, 0)));
}
BI(m_ceil)
{
    need(x, 1, 1, "ceil");
    if (is_intv(A(0))) return A(0).t == T_BOOL ? mkint(A(0).u.i) : A(0);
    return dbl_to_int(ceil(dbl_arg(x, 0)));
}
BI(m_trunc)
{
    double d;
    need(x, 1, 1, "trunc");
    if (is_intv(A(0))) return A(0);
    d = dbl_arg(x, 0);
    return dbl_to_int(d < 0 ? ceil(d) : floor(d));
}
BI(m_isnan) { need(x, 1, 1, "isnan"); return mkbool(dbl_arg(x, 0) != dbl_arg(x, 0)); }
BI(m_isinf)
{
    double d;
    need(x, 1, 1, "isinf");
    d = dbl_arg(x, 0);
    return mkbool(d == d && (d > DBL_MAX || d < -DBL_MAX));
}
BI(m_isfinite)
{
    double d;
    need(x, 1, 1, "isfinite");
    d = dbl_arg(x, 0);
    return mkbool(d == d && d <= DBL_MAX && d >= -DBL_MAX);
}
static Value int_gcd(Value a, Value b)
{
    if (int_sign(a) < 0) a = int_neg(a);
    if (int_sign(b) < 0) b = int_neg(b);
    while (int_sign(b) != 0) {
        Value t = int_mod(a, b);
        a = b;
        b = t;
    }
    return a;
}
BI(m_gcd)
{
    Value g = mkint(0);
    int i;
    for (i = 0; i < x->n; i++) {
        if (!is_intv(A(i))) throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer", type_name(A(i)));
        g = int_gcd(g, A(i));
    }
    return g;
}
BI(m_lcm)
{
    Value l = mkint(1);
    int i;
    for (i = 0; i < x->n; i++) {
        Value g;
        if (int_sign(A(i)) == 0) return mkint(0);
        g = int_gcd(l, A(i));
        l = int_floordiv(int_mul(l, A(i)), g);
        if (int_sign(l) < 0) l = int_neg(l);
    }
    return l;
}
BI(m_factorial)
{
    long n, i;
    Value r = mkint(1);
    need(x, 1, 1, "factorial");
    if (!is_intv(A(0))) throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer", type_name(A(0)));
    if (int_sign(A(0)) < 0) throw_new(cls_ValueError, "factorial() not defined for negative values");
    n = to_index(A(0));
    for (i = 2; i <= n; i++) r = int_mul(r, mkint(i));
    return r;
}
BI(m_isqrt)
{
    Value n, r, nr;
    need(x, 1, 1, "isqrt");
    n = A(0);
    if (!is_intv(n)) throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer", type_name(n));
    if (int_sign(n) < 0) throw_new(cls_ValueError, "isqrt() argument must be nonnegative");
    if (n.t != T_BIG) {
        long s = (long)sqrt((double)n.u.i);
        while (s * s > n.u.i) s--;
        while ((s + 1) * (s + 1) <= n.u.i) s++;
        return mkint(s);
    }
    r = int_from_double(sqrt(int_todouble(n)));
    for (;;) {
        nr = int_floordiv(int_add(r, int_floordiv(n, r)), mkint(2));
        if (int_cmp(nr, r) == 0) break;
        if (int_cmp(int_sub(nr, r), mkint(1)) == 0 && int_cmp(int_mul(nr, nr), n) > 0) break;
        r = nr;
    }
    while (int_cmp(int_mul(r, r), n) > 0) r = int_sub(r, mkint(1));
    while (int_cmp(int_mul(int_add(r, mkint(1)), int_add(r, mkint(1))), n) <= 0) r = int_add(r, mkint(1));
    return r;
}
BI(m_comb)
{
    Value r = mkint(1);
    long n, k, i;
    need(x, 2, 2, "comb");
    n = to_index(A(0)); k = to_index(A(1));
    if (n < 0 || k < 0) throw_new(cls_ValueError, "n and k must be non-negative integers");
    if (k > n) return mkint(0);
    if (k > n - k) k = n - k;
    for (i = 1; i <= k; i++) r = int_floordiv(int_mul(r, mkint(n - k + i)), mkint(i));
    return r;
}
BI(m_prod)
{
    Value r = mkint(1), it, y;
    need(x, 1, 1, "prod");
    it = get_iter(A(0));
    VPUSH(it);
    VPUSH(r);
    while (iter_next(it, &y)) { r = binop(B_MUL, r, y); vsp[-1] = r; }
    vsp -= 2;
    return r;
}

/* ---- random ----------------------------------------------------------- */
static unsigned long rng_state = 2463534242UL;
static unsigned long rng_next(void)
{
    unsigned long s = rng_state & 0xffffffffUL;
    s ^= (s << 13) & 0xffffffffUL;
    s ^= (s >> 17);
    s ^= (s << 5) & 0xffffffffUL;
    rng_state = s & 0xffffffffUL;
    return rng_state;
}
static double rng_double(void)
{
    unsigned long a = rng_next() >> 5, b = rng_next() >> 6;
    return ((double)a * 67108864.0 + (double)b) / 9007199254740992.0;
}
static long rng_below(long n)
{
    if (n <= 0) return 0;
    return (long)(rng_double() * (double)n);
}
BI(r_seed)
{
    unsigned long h = 2463534242UL;
    if (x->n > 0 && A(0).t != T_NONE) h = (py_hash(A(0)) * 2654435761UL + 12345UL) & 0xffffffffUL;
    else h = (unsigned long)time(NULL) & 0xffffffffUL;
    if (h == 0) h = 88172645UL;
    rng_state = h;
    rng_next(); rng_next();
    return NONE_V;
}
BI(r_random) { (void)x; return mkfloat(rng_double()); }
BI(r_randint)
{
    long a, b;
    need(x, 2, 2, "randint");
    a = to_index(A(0)); b = to_index(A(1));
    if (b < a) throw_new(cls_ValueError, "empty range for randrange() (%ld, %ld, %ld)", a, b + 1, b + 1 - a);
    return mkint(a + rng_below(b - a + 1));
}
BI(r_randrange)
{
    long a = 0, b, s = 1, n;
    need(x, 1, 3, "randrange");
    if (x->n == 1) b = to_index(A(0));
    else { a = to_index(A(0)); b = to_index(A(1)); if (x->n == 3) s = to_index(A(2)); }
    if (s == 0) throw_new(cls_ValueError, "zero step for randrange()");
    n = s > 0 ? (b - a + s - 1) / s : (a - b - s - 1) / (-s);
    if (n <= 0) throw_new(cls_ValueError, "empty range for randrange()");
    return mkint(a + s * rng_below(n));
}
BI(r_uniform)
{
    double a, b;
    need(x, 2, 2, "uniform");
    a = dbl_arg(x, 0); b = dbl_arg(x, 1);
    return mkfloat(a + (b - a) * rng_double());
}
BI(r_choice)
{
    Value l, r;
    long n;
    need(x, 1, 1, "choice");
    l = iter_to_list(A(0));
    n = AS_LIST(l)->n;
    if (n == 0) throw_new(cls_IndexError, "Cannot choose from an empty sequence");
    r = AS_LIST(l)->a[rng_below(n)];
    return r;
}
BI(r_shuffle)
{
    List *l;
    long i;
    need(x, 1, 1, "shuffle");
    if (A(0).t != T_LIST) throw_new(cls_TypeError, "shuffle requires a list");
    l = AS_LIST(A(0));
    for (i = l->n - 1; i > 0; i--) {
        long j = rng_below(i + 1);
        Value t = l->a[i];
        l->a[i] = l->a[j];
        l->a[j] = t;
    }
    return NONE_V;
}
BI(r_sample)
{
    Value l;
    long k, n, i;
    List *p;
    need(x, 2, 2, "sample");
    l = iter_to_list(A(0));
    VPUSH(l);
    p = AS_LIST(l);
    n = p->n;
    k = to_index(A(1));
    if (k < 0 || k > n) throw_new(cls_ValueError, "Sample larger than population or is negative");
    for (i = 0; i < k; i++) {
        long j = i + rng_below(n - i);
        Value t = p->a[i];
        p->a[i] = p->a[j];
        p->a[j] = t;
    }
    p->n = k;
    vsp--;
    return l;
}

/* ---- sys / time ------------------------------------------------------- */
static Value std_file(FILE *fp, int out)
{
    File *f = (File *)gc_alloc(sizeof(File), T_FILE);
    f->fp = fp;
    f->closed = 0;
    f->out = out;
    return mkobj(f, T_FILE);
}
BI(t_time)
{
    (void)x;
#ifdef PY_POSIX
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return mkfloat((double)tv.tv_sec + (double)tv.tv_usec / 1000000.0);
    }
#else
    return mkfloat((double)time(NULL));
#endif
}
BI(t_clock)
{
    (void)x;
    return mkfloat((double)clock() / (double)CLOCKS_PER_SEC);
}
BI(t_sleep)
{
    double d = dbl_arg(x, 0);
    fflush(stdout);
#ifdef PY_POSIX
    {
        struct timespec ts;
        ts.tv_sec = (time_t)d;
        ts.tv_nsec = (long)((d - (double)ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
    }
#else
    {
        clock_t end = clock() + (clock_t)(d * (double)CLOCKS_PER_SEC);
        while (clock() < end) { }
    }
#endif
    return NONE_V;
}
BI(sys_getrec)
{
    (void)x;
    return mkint(MAX_DEPTH);
}
BI(sys_setrec)
{
    (void)x;
    return NONE_V;
}

/* ---- initialisation --------------------------------------------------- */
static struct { const char *name; Class **var; Class **base; } exc_table[] = {
    { "BaseException", &cls_BaseException, NULL },
    { "Exception", &cls_Exception, &cls_BaseException },
    { "ArithmeticError", &cls_ArithmeticError, &cls_Exception },
    { "ZeroDivisionError", &cls_ZeroDivisionError, &cls_ArithmeticError },
    { "OverflowError", &cls_OverflowError, &cls_ArithmeticError },
    { "LookupError", &cls_LookupError, &cls_Exception },
    { "IndexError", &cls_IndexError, &cls_LookupError },
    { "KeyError", &cls_KeyError, &cls_LookupError },
    { "ValueError", &cls_ValueError, &cls_Exception },
    { "TypeError", &cls_TypeError, &cls_Exception },
    { "NameError", &cls_NameError, &cls_Exception },
    { "UnboundLocalError", &cls_UnboundLocalError, &cls_NameError },
    { "AttributeError", &cls_AttributeError, &cls_Exception },
    { "RuntimeError", &cls_RuntimeError, &cls_Exception },
    { "RecursionError", &cls_RecursionError, &cls_RuntimeError },
    { "NotImplementedError", &cls_NotImplementedError, &cls_RuntimeError },
    { "StopIteration", &cls_StopIteration, &cls_Exception },
    { "AssertionError", &cls_AssertionError, &cls_Exception },
    { "SystemExit", &cls_SystemExit, &cls_BaseException },
    { "KeyboardInterrupt", &cls_KeyboardInterrupt, &cls_BaseException },
    { "GeneratorExit", &cls_GeneratorExit, &cls_BaseException },
    { "ImportError", &cls_ImportError, &cls_Exception },
    { "OSError", &cls_OSError, &cls_Exception },
    { "EOFError", &cls_EOFError, &cls_Exception },
    { "SyntaxError", &cls_SyntaxError, &cls_Exception },
    { "FileNotFoundError", &cls_FileNotFoundError, &cls_OSError },
    { NULL, NULL, NULL }
};

static Class *mk_type(const char *name, Class *base, BFn ctor)
{
    Class *c = new_class(name, base, CF_BUILTIN);
    c->ctor = ctor;
    return c;
}

static void init_runtime(int argc, char **argv)
{
    int i;
    Module *m;
    List *av;
    init_chars();
    NONE_V.t = T_NONE; NONE_V.u.i = 0;
    TRUE_V.t = T_BOOL; TRUE_V.u.i = 1;
    FALSE_V.t = T_BOOL; FALSE_V.u.i = 0;
    UNSET_V.t = T_UNSET; UNSET_V.u.i = 0;
    cur_exc = NONE_V;
    handled_exc = NONE_V;
    vstack = (Value *)xmalloc(sizeof(Value) * (size_t)VSTACK_SIZE);
    vsp = vstack;
    vlimit = vstack + VSTACK_SIZE - 512;
    gc_perm = 1;
#define SN(v, s) v = intern_c(s)
    SN(S_init, "__init__"); SN(S_str, "__str__"); SN(S_repr, "__repr__"); SN(S_eq, "__eq__");
    SN(S_ne, "__ne__"); SN(S_lt, "__lt__"); SN(S_le, "__le__"); SN(S_gt, "__gt__");
    SN(S_ge, "__ge__"); SN(S_add, "__add__"); SN(S_sub, "__sub__"); SN(S_mul, "__mul__");
    SN(S_neg, "__neg__"); SN(S_len, "__len__"); SN(S_getitem, "__getitem__");
    SN(S_setitem, "__setitem__"); SN(S_delitem, "__delitem__"); SN(S_contains, "__contains__");
    SN(S_iter, "__iter__"); SN(S_next, "__next__"); SN(S_call, "__call__");
    SN(S_bool, "__bool__"); SN(S_hash, "__hash__"); SN(S_enter, "__enter__");
    SN(S_exit, "__exit__"); SN(S_name, "__name__"); SN(S_class, "__class__");
    SN(S_args, "args"); SN(S_truediv, "__truediv__"); SN(S_floordiv, "__floordiv__");
    SN(S_mod, "__mod__"); SN(S_pow, "__pow__"); SN(S_radd, "__radd__"); SN(S_rsub, "__rsub__");
    SN(S_rmul, "__rmul__"); SN(S_abs, "__abs__"); SN(S_int, "__int__"); SN(S_float, "__float__");
    SN(S_index, "__index__"); SN(S_pos, "__pos__"); SN(S_invert, "__invert__");
    SN(S_and, "__and__"); SN(S_or, "__or__"); SN(S_xor, "__xor__"); SN(S_lshift, "__lshift__");
    SN(S_rshift, "__rshift__"); SN(S_dict, "__dict__"); SN(S_mro, "__mro__");
    SN(S_doc, "__doc__"); SN(S_setter, "setter"); SN(S_main, "__main__");
    SN(S_getattr, "__getattr__"); SN(S_format, "__format__"); SN(S_self, "self");
    SN(S_super, "super");
#undef SN
    /* classes */
    cls_object = new_class("object", NULL, CF_BUILTIN);
    cls_type = mk_type("type", cls_object, bi_type);
    cls_int = mk_type("int", cls_object, bi_int);
    cls_bool = mk_type("bool", cls_int, bi_bool);
    cls_float = mk_type("float", cls_object, bi_float);
    cls_str = mk_type("str", cls_object, bi_str);
    cls_list = mk_type("list", cls_object, bi_list);
    cls_tuple = mk_type("tuple", cls_object, bi_tuple);
    cls_dict = mk_type("dict", cls_object, bi_dict);
    cls_set = mk_type("set", cls_object, bi_set);
    cls_range = mk_type("range", cls_object, bi_range);
    cls_none = mk_type("NoneType", cls_object, NULL);
    cls_func = mk_type("function", cls_object, NULL);
    cls_builtin = mk_type("builtin_function_or_method", cls_object, NULL);
    cls_method = mk_type("method", cls_object, NULL);
    cls_gen = mk_type("generator", cls_object, NULL);
    cls_module = mk_type("module", cls_object, NULL);
    cls_iter = mk_type("iterator", cls_object, NULL);
    cls_prop = mk_type("property", cls_object, bi_property);
    cls_file = mk_type("TextIOWrapper", cls_object, NULL);
    cls_cell = mk_type("cell", cls_object, NULL);
    cls_smeth = mk_type("staticmethod", cls_object, bi_staticmethod);
    cls_cmeth = mk_type("classmethod", cls_object, bi_classmethod);
    cls_super = mk_type("super", cls_object, bi_super);
    { long si = global_index(intern_c("set")); set_global(intern_c("frozenset"), G[si]); }
    cls_object->ctor = bi_object;
    for (i = 0; exc_table[i].name; i++) {
        Class *base = exc_table[i].base ? *exc_table[i].base : cls_object;
        *exc_table[i].var = new_class(exc_table[i].name, base, CF_BUILTIN | CF_EXC);
    }
    cls_BaseException->flags |= CF_EXC;
    set_global(intern_c("IOError"), mkobj(cls_OSError, T_CLASS));
    set_global(intern_c("__name__"), mkobj(S_main, T_STR));
    set_global(intern_c("__doc__"), NONE_V);
    set_global(intern_c("None"), NONE_V);
    set_global(intern_c("True"), TRUE_V);
    set_global(intern_c("False"), FALSE_V);

    /* builtin functions */
    def_bi(NULL, "print", bi_print); def_bi(NULL, "len", bi_len); def_bi(NULL, "repr", bi_repr);
    def_bi(NULL, "abs", bi_abs); def_bi(NULL, "min", bi_min); def_bi(NULL, "max", bi_max);
    def_bi(NULL, "sum", bi_sum); def_bi(NULL, "sorted", bi_sorted);
    def_bi(NULL, "reversed", bi_reversed); def_bi(NULL, "enumerate", bi_enumerate);
    def_bi(NULL, "zip", bi_zip); def_bi(NULL, "map", bi_map); def_bi(NULL, "filter", bi_filter);
    def_bi(NULL, "any", bi_any); def_bi(NULL, "all", bi_all); def_bi(NULL, "round", bi_round);
    def_bi(NULL, "divmod", bi_divmod); def_bi(NULL, "pow", bi_pow); def_bi(NULL, "chr", bi_chr);
    def_bi(NULL, "ord", bi_ord); def_bi(NULL, "hex", bi_hex); def_bi(NULL, "oct", bi_oct);
    def_bi(NULL, "bin", bi_bin); def_bi(NULL, "isinstance", bi_isinstance);
    def_bi(NULL, "issubclass", bi_issubclass); def_bi(NULL, "hasattr", bi_hasattr);
    def_bi(NULL, "getattr", bi_getattr); def_bi(NULL, "setattr", bi_setattr);
    def_bi(NULL, "delattr", bi_delattr); def_bi(NULL, "callable", bi_callable);
    def_bi(NULL, "iter", bi_iter); def_bi(NULL, "next", bi_next); def_bi(NULL, "id", bi_id);
    def_bi(NULL, "hash", bi_hash); def_bi(NULL, "input", bi_input); def_bi(NULL, "open", bi_open);
    def_bi(NULL, "format", bi_format); def_bi(NULL, "vars", bi_vars);
    def_bi(NULL, "exit", bi_exit_fn); def_bi(NULL, "quit", bi_exit_fn);
    propsetter_fn = mkbuiltin("setter", bi_propsetter);
    set_global(S_super, mkbuiltin("super", bi_super));

    /* exception methods */
    def_bi(cls_BaseException, "__init__", exc_init);
    def_bi(cls_BaseException, "__str__", exc_str);
    def_bi(cls_BaseException, "__repr__", exc_repr);
    object_init_v = mkbuiltin("__init__", obj_init);
    amap_set(&cls_object->ns, S_init, object_init_v);
    obj_eq_v = mkbuiltin("__eq__", obj_eq);
    obj_ne_v = mkbuiltin("__ne__", obj_ne);
    obj_hash_v = mkbuiltin("__hash__", obj_hash);
    def_bi(cls_object, "__repr__", obj_repr);
    def_bi(cls_object, "__str__", obj_str);

    /* str */
    def_bi(cls_str, "upper", s_upper); def_bi(cls_str, "lower", s_lower);
    def_bi(cls_str, "capitalize", s_capitalize); def_bi(cls_str, "title", s_title);
    def_bi(cls_str, "swapcase", s_swapcase); def_bi(cls_str, "strip", s_strip);
    def_bi(cls_str, "lstrip", s_lstrip); def_bi(cls_str, "rstrip", s_rstrip);
    def_bi(cls_str, "split", s_split); def_bi(cls_str, "rsplit", s_rsplit);
    def_bi(cls_str, "splitlines", s_splitlines); def_bi(cls_str, "join", s_join);
    def_bi(cls_str, "replace", s_replace); def_bi(cls_str, "find", s_find);
    def_bi(cls_str, "rfind", s_rfind); def_bi(cls_str, "index", s_index);
    def_bi(cls_str, "rindex", s_rindex); def_bi(cls_str, "count", s_count);
    def_bi(cls_str, "startswith", s_startswith); def_bi(cls_str, "endswith", s_endswith);
    def_bi(cls_str, "isalpha", s_isalpha); def_bi(cls_str, "isdigit", s_isdigit);
    def_bi(cls_str, "isnumeric", s_isdigit); def_bi(cls_str, "isdecimal", s_isdigit);
    def_bi(cls_str, "isalnum", s_isalnum); def_bi(cls_str, "isspace", s_isspace);
    def_bi(cls_str, "isupper", s_isupper); def_bi(cls_str, "islower", s_islower);
    def_bi(cls_str, "center", s_center); def_bi(cls_str, "ljust", s_ljust);
    def_bi(cls_str, "rjust", s_rjust); def_bi(cls_str, "zfill", s_zfill);
    def_bi(cls_str, "partition", s_partition); def_bi(cls_str, "rpartition", s_rpartition);
    def_bi(cls_str, "format", s_format); def_bi(cls_str, "encode", s_encode);
    def_bi(cls_str, "removeprefix", s_removeprefix); def_bi(cls_str, "removesuffix", s_removesuffix);
    def_bi(cls_str, "expandtabs", s_expandtabs); def_bi(cls_str, "casefold", s_lower);
    /* list, tuple */
    def_bi(cls_list, "append", l_append); def_bi(cls_list, "extend", l_extend);
    def_bi(cls_list, "insert", l_insert); def_bi(cls_list, "pop", l_pop);
    def_bi(cls_list, "remove", l_remove); def_bi(cls_list, "index", l_index);
    def_bi(cls_list, "count", l_count); def_bi(cls_list, "sort", l_sort);
    def_bi(cls_list, "reverse", l_reverse); def_bi(cls_list, "copy", l_copy);
    def_bi(cls_list, "clear", l_clear);
    def_bi(cls_tuple, "index", t_index); def_bi(cls_tuple, "count", l_count);
    /* dict */
    def_bi(cls_dict, "get", d_get); def_bi(cls_dict, "keys", d_keys);
    def_bi(cls_dict, "values", d_values); def_bi(cls_dict, "items", d_items);
    def_bi(cls_dict, "update", d_update); def_bi(cls_dict, "pop", d_pop);
    def_bi(cls_dict, "setdefault", d_setdefault); def_bi(cls_dict, "copy", d_copy);
    def_bi(cls_dict, "clear", d_clear); def_bi(cls_dict, "popitem", d_popitem);
    def_bi(cls_dict, "fromkeys", d_fromkeys);
    /* set */
    def_bi(cls_set, "add", st_add); def_bi(cls_set, "discard", st_discard);
    def_bi(cls_set, "remove", st_remove); def_bi(cls_set, "pop", st_pop);
    def_bi(cls_set, "union", st_union); def_bi(cls_set, "intersection", st_intersection);
    def_bi(cls_set, "difference", st_difference); def_bi(cls_set, "symmetric_difference", st_symdiff);
    def_bi(cls_set, "update", st_update); def_bi(cls_set, "issubset", st_issubset);
    def_bi(cls_set, "issuperset", st_issuperset); def_bi(cls_set, "isdisjoint", st_isdisjoint);
    def_bi(cls_set, "copy", st_copy); def_bi(cls_set, "clear", st_clear);
    /* numbers */
    def_bi(cls_int, "bit_length", i_bit_length);
    def_bi(cls_float, "is_integer", f_is_integer);
    /* generators / iterators */
    def_bi(cls_gen, "__next__", g_next); def_bi(cls_gen, "__iter__", g_iter);
    def_bi(cls_gen, "close", g_close); def_bi(cls_gen, "send", g_next);
    def_bi(cls_iter, "__next__", g_next); def_bi(cls_iter, "__iter__", g_iter);
    /* files */
    def_bi(cls_file, "read", fl_read); def_bi(cls_file, "readline", fl_readline);
    def_bi(cls_file, "readlines", fl_readlines); def_bi(cls_file, "write", fl_write);
    def_bi(cls_file, "writelines", fl_writelines); def_bi(cls_file, "flush", fl_flush);
    def_bi(cls_file, "close", fl_close); def_bi(cls_file, "__enter__", fl_enter);
    def_bi(cls_file, "__exit__", fl_exit);

    /* modules */
    m = new_module("math");
    mod_def(m, "sqrt", m_sqrt); mod_def(m, "sin", m_sin); mod_def(m, "cos", m_cos);
    mod_def(m, "tan", m_tan); mod_def(m, "asin", m_asin); mod_def(m, "acos", m_acos);
    mod_def(m, "atan", m_atan); mod_def(m, "atan2", m_atan2); mod_def(m, "sinh", m_sinh);
    mod_def(m, "cosh", m_cosh); mod_def(m, "tanh", m_tanh); mod_def(m, "exp", m_exp);
    mod_def(m, "log", m_log); mod_def(m, "log10", m_log10); mod_def(m, "log2", m_log2);
    mod_def(m, "pow", m_pow); mod_def(m, "fabs", m_fabs); mod_def(m, "hypot", m_hypot);
    mod_def(m, "floor", m_floor); mod_def(m, "ceil", m_ceil); mod_def(m, "trunc", m_trunc);
    mod_def(m, "gcd", m_gcd); mod_def(m, "lcm", m_lcm); mod_def(m, "factorial", m_factorial);
    mod_def(m, "isqrt", m_isqrt); mod_def(m, "isnan", m_isnan); mod_def(m, "isinf", m_isinf);
    mod_def(m, "isfinite", m_isfinite); mod_def(m, "fmod", m_fmod);
    mod_def(m, "copysign", m_copysign); mod_def(m, "degrees", m_degrees);
    mod_def(m, "radians", m_radians); mod_def(m, "comb", m_comb); mod_def(m, "prod", m_prod);
    mod_val(m, "pi", mkfloat(3.14159265358979323846));
    mod_val(m, "e", mkfloat(2.71828182845904523536));
    mod_val(m, "tau", mkfloat(6.28318530717958647692));
    mod_val(m, "inf", mkfloat(HUGE_VAL));
    mod_val(m, "nan", mkfloat(HUGE_VAL - HUGE_VAL));
    m = new_module("random");
    mod_def(m, "seed", r_seed); mod_def(m, "random", r_random); mod_def(m, "randint", r_randint);
    mod_def(m, "randrange", r_randrange); mod_def(m, "uniform", r_uniform);
    mod_def(m, "choice", r_choice); mod_def(m, "shuffle", r_shuffle); mod_def(m, "sample", r_sample);
    {
        Args seedargs;
        seedargs.n = 0; seedargs.a = NULL; seedargs.nk = 0; seedargs.kw = NULL;
        r_seed(&seedargs);
    }
    m = new_module("time");
    mod_def(m, "time", t_time); mod_def(m, "perf_counter", t_clock);
    mod_def(m, "monotonic", t_clock); mod_def(m, "process_time", t_clock);
    mod_def(m, "sleep", t_sleep);
    m = new_module("sys");
    av = list_new(argc + 1);
    for (i = 0; i < argc; i++) list_push(av, mkcstr(argv[i]));
    mod_val(m, "argv", mklist(av));
    mod_def(m, "exit", bi_exit_fn);
    mod_def(m, "getrecursionlimit", sys_getrec); mod_def(m, "setrecursionlimit", sys_setrec);
    mod_val(m, "stdout", std_file(stdout, 1));
    mod_val(m, "stderr", std_file(stderr, 1));
    mod_val(m, "stdin", std_file(stdin, 1));
    mod_val(m, "maxsize", mkint(SI_MAX));
    mod_val(m, "version", mkcstr("3.12.0 (C89 subset interpreter)"));
    mod_val(m, "platform", mkcstr("c89"));
    mod_val(m, "path", mklist(list_new(1)));
    gc_perm = 0;
}
