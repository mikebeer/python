/* =========================================================================
   Python 3 subset interpreter in portable C89.

   p0_base.c   types, globals, memory + garbage collector, strings,
               dictionaries, attribute maps, errors.

   Portability rules: strict C89 (no //, no declarations after statements,
   no long long, no stdint.h), all functions static, character classes by
   literal sets.  The standard headers and every host/OS dependency are in
   p0_os.c.  Integers: a small int is a long with |n| <= SI_MAX,
   larger integers are big integers (base 10000 limbs).
   ========================================================================= */
#define PY_VERSION "1.0"

#if LONG_MAX > 2147483647L && !defined(PY_SI32)  /* -DPY_SI32 simulates 32-bit long */
#define SI_MAX 999999999999999999L
#define SI_MULLIM 1000000000L
#else
#define SI_MAX 999999999L
#define SI_MULLIM 31622L
#endif

#ifndef VSTACK_SIZE
#define VSTACK_SIZE 400000L   /* value stack, in Values (16 bytes each): -D to shrink for CMS */
#endif
#ifndef MAX_DEPTH
#define MAX_DEPTH 1000        /* Python recursion limit (also bounds the C stack use) */
#endif

/* ---- values ----------------------------------------------------------- */
enum {
    T_UNSET = 0, T_NONE, T_BOOL, T_INT, T_FLOAT,
    T_STR, T_BIG, T_LIST, T_TUPLE, T_DICT, T_SET, T_RANGE, T_FUNC, T_BUILTIN,
    T_CLASS, T_INST, T_BOUND, T_GEN, T_ITER, T_CELL, T_MODULE, T_PROP,
    T_SMETH, T_CMETH, T_SUPER, T_FILE, T_ND, T_SLICE, T_DVIEW, T_BYTES, T_BARR, T_COMPLEX,
    T_NTYPES
};
#define IS_HEAP(t) ((t) >= T_STR)

typedef struct Hdr {
    struct Hdr *next;
    unsigned char type;
    unsigned char mark;
    unsigned char perm;
} Hdr;

typedef struct Value {
    int t;
    union { long i; double d; Hdr *o; } u;
} Value;

typedef struct Str {
    Hdr h;
    long len;
    unsigned long hash;
    char s[1];
} Str;

typedef struct Big {
    Hdr h;
    int sign;
    int n;
    unsigned short d[1];
} Big;

typedef struct List {
    Hdr h;
    long n, cap;
    Value *a;
} List;

typedef struct DEnt { Value k, v; unsigned long hash; } DEnt;

typedef struct Dict {
    Hdr h;
    long n;          /* live entries */
    long used;       /* entries used, including deleted */
    long cap;        /* capacity of e */
    DEnt *e;
    long *idx;       /* hash index, -1 empty, -2 deleted */
    long isz;
} Dict;

typedef struct AMap {
    struct Str **k;
    Value *v;
    int n, cap;
} AMap;

typedef struct Args {
    int n;
    Value *a;
    int nk;
    Value *kw;       /* nk pairs: name (T_STR), value */
} Args;

typedef Value (*BFn)(Args *x);

typedef struct Class {
    Hdr h;
    Str *name;
    struct Class *base;
    AMap ns;
    BFn ctor;
    int flags;
    struct Class **mro;  /* method resolution order (user classes), NULL = follow base */
    int nmro;
    struct Class **bases; /* all direct bases (user classes) */
    int nbases;
    struct Class *paycls; /* native base type (list dict set tuple str) holding the payload */
    Str *modname;        /* defining module, NULL for builtins */
} Class;
#define CF_EXC 1
#define CF_PROPS 2
#define CF_BUILTIN 4
#define CF_SETATTR 8
#define CF_NPABS 16   /* numpy abstract scalar type (np.floating ...): isinstance by value type */

typedef struct Inst { Hdr h; Class *cls; AMap a; Value pay; } Inst;

typedef struct Cell { Hdr h; Value v; } Cell;

typedef struct Func {
    Hdr h;
    struct Node *def;
    Cell **cells;
    Value *defs;
    int ndefs;
    Value *kwdefs;
    Class *defclass;
    Str *name;
    AMap attrs;          /* function attributes (f.x = 1) */
} Func;

typedef struct Builtin { Hdr h; BFn fn; const char *name; Str *sname; int selfpay; } Builtin;
typedef struct Bound { Hdr h; Value self; Value fn; } Bound;
typedef struct Module {
    Hdr h;
    Str *name;
    AMap ns;
    Str *gp;             /* global name prefix ("pkg.mod.") for modules written in Python */
    struct Dict *gmap;   /* attribute name -> global slot cache */
} Module;
/* start, stop, step as integers; big = 0 when all three are small ints (then .u.i is used) */
typedef struct Range { Hdr h; Value a, b, c; int big; } Range;
#define ND_MAXD 8
enum { ND_BOOL = 0, ND_INT = 1, ND_FLOAT = 2, ND_COMPLEX = 3 };
/* complex arrays keep 2 * size doubles (re, im interleaved) in dv */
typedef struct Nd {
    Hdr h;
    int dt, nd;
    long shape[ND_MAXD];
    long size;
    long *iv;            /* bool and int data */
    double *dv;          /* float data */
} Nd;
#define AS_ND(v) ((Nd *)(v).u.o)
typedef struct BArr { Hdr h; long n, cap; char *s; } BArr;     /* bytearray */
typedef struct Cplx { Hdr h; double re, im; } Cplx;          /* complex */
#define AS_BARR(v) ((BArr *)(v).u.o)
#define AS_CPLX(v) ((Cplx *)(v).u.o)
typedef struct Prop { Hdr h; Value get, set, del; } Prop;
typedef struct DView { Hdr h; Value d; int what; } DView;   /* what: 0 keys 1 values 2 items */
typedef struct Super { Hdr h; Class *start; Value obj; } Super;
typedef struct File { Hdr h; FILE *fp; int closed; int out; } File;

typedef struct Gen {
    Hdr h;
    struct Node *def;
    Value *loc;
    int nloc;
    struct Func *fn;
    Cell **cells;
    Value *state;
    int nstate;
    Value yielded;
    Value sent;          /* value passed in by send() */
    Value thrown;        /* exception to raise at the yield point (throw()) */
    Value ret;           /* return value (StopIteration.value) */
    int started, done, running;
} Gen;

enum { IK_LIST, IK_TUPLE, IK_STR, IK_RANGE, IK_DICT, IK_DVALS, IK_DITEMS, IK_SET,
       IK_ENUM, IK_ZIP, IK_MAP, IK_FILTER, IK_REV, IK_FILE, IK_CALL, IK_LISTREV, IK_ND, IK_BYTES,
       IK_CALLSENT, IK_BRANGE };
typedef struct Iter {
    Hdr h;
    int kind;
    Value a, b, c;
    long i, n, step;
} Iter;

/* ---- AST -------------------------------------------------------------- */
typedef struct Var {
    Str *name;
    int kind;
    int slot;
    unsigned char defined, isglobal, isnonlocal, isparam;
    int bslot;           /* builtin fallback slot for globals of modules, -1 = none */
    struct Var *src;     /* free variable: variable in the parent scope */
    struct Var *fb;      /* class namespace variable: fallback */
} Var;
enum { VK_NONE = 0, VK_LOCAL, VK_CELL, VK_FREE, VK_GLOBAL, VK_NS };

typedef struct Scope {
    int kind;
    struct Scope *parent;
    Var **vars; int nvars, vcap;
    Var **freevars; int nfree, fcap;
    struct Node **refs; int nrefs, rcap;
    struct Scope **kids; int nkids, kcap;
    int nparams, nlocals, nstate;
    int isgen;
    struct FuncDef *fd;
} Scope;
enum { SC_MOD, SC_FUNC, SC_CLASS };

typedef struct Node {
    short kind;
    short op;
    int line;
    struct Node *a, *b, *c, *d;
    struct Node **list;
    int n;
    Value val;
    Str *name;
    Var *var;
    int sidx;
    struct FuncDef *fd;
    Class *ccls;         /* attribute inline cache */
    int cepoch;
    Value cval;
    int level;           /* relative import level */
    Str *pfx;            /* import statements: global prefix of the importing module */
} Node;

typedef struct FuncDef {
    Scope *sc;
    Str *name;
    int npos;            /* positional parameters */
    int ndefaults;
    int nkwonly;
    int va_slot, kw_slot;
    Str **pnames;        /* npos + nkwonly names */
    Node **defaults;     /* ndefaults nodes */
    Node **kwdefaults;   /* nkwonly nodes (NULL entries = required) */
    Node *body;
    int isgen, isclass;
    int nlocals, nfree, nstate;
    Var **fv;
    int *cellslots; int ncells;
    int line;
    Str *gpfx;           /* global name prefix of the module this was compiled in */
} FuncDef;

/* ---- interpreter state ------------------------------------------------ */
typedef struct Frame {
    Value *loc;
    Cell **cells;
    Func *fn;
    Value ret;
    Gen *gen;
    Class *ns;           /* class body being executed */
    int resuming;
    Str *gp;             /* global prefix of the module this frame runs in */
    struct Frame *up;
} Frame;

typedef struct Handler {
    jmp_buf jb;
    struct Handler *prev;
    Value *sp;
    int depth;
    Frame *frame;
} Handler;

static Value NONE_V, TRUE_V, FALSE_V, UNSET_V, NotImpl_V, Ellipsis_V;
static Value object_init_v, obj_str_v;
#define IS_NOTIMPL(v) ((v).t == T_INST && (v).u.o == NotImpl_V.u.o)
static Value *vstack, *vsp, *vlimit;
static Handler *cur_handler;
static Frame *cur_frame;
static int call_depth;
static int cur_line;
static Value cur_exc;
static Value handled_exc;
static int class_epoch = 1;

/* memory */
static Hdr *heap_list, *perm_list;
static int gc_perm;
#ifndef PY_GC_MIN
#define PY_GC_MIN 32000000L
#endif
static long gc_bytes, gc_limit = PY_GC_MIN, gc_live;
static int gc_enabled = 1;
static Hdr **gray;
static long ngray, graycap;

/* global names */
static Str **gnames;
static Value *G;
static long nglobals, gcap;

/* classes */
static Class *cls_object, *cls_type, *cls_int, *cls_bool, *cls_float, *cls_str, *cls_list,
    *cls_tuple, *cls_dict, *cls_set, *cls_range, *cls_none, *cls_func, *cls_builtin,
    *cls_method, *cls_gen, *cls_module, *cls_iter, *cls_prop, *cls_file, *cls_cell,
    *cls_smeth, *cls_cmeth, *cls_super, *cls_ndarray, *cls_slice, *cls_dview[3], *cls_notimpl,
        *cls_bytes, *cls_bytearray, *cls_complex, *cls_frozenset;
static Class *cls_MemoryError;
static Class *cls_BaseException, *cls_Exception, *cls_ArithmeticError, *cls_ZeroDivisionError,
    *cls_OverflowError, *cls_LookupError, *cls_IndexError, *cls_KeyError, *cls_ValueError,
    *cls_TypeError, *cls_NameError, *cls_UnboundLocalError, *cls_AttributeError,
    *cls_RuntimeError, *cls_RecursionError, *cls_NotImplementedError, *cls_StopIteration,
    *cls_AssertionError, *cls_SystemExit, *cls_ImportError, *cls_OSError, *cls_EOFError,
    *cls_SyntaxError, *cls_KeyboardInterrupt, *cls_GeneratorExit, *cls_FileNotFoundError,
    *cls_UnicodeDecodeError, *cls_ConnectionError, *cls_TimeoutError, *cls_ConnectionRefusedError,
    *cls_ConnectionResetError, *cls_ConnectionAbortedError, *cls_BrokenPipeError,
    *cls_BlockingIOError, *cls_InterruptedError;

/* interned names used by the runtime */
static Str *S_init, *S_str, *S_repr, *S_eq, *S_ne, *S_lt, *S_le, *S_gt, *S_ge, *S_add, *S_sub,
    *S_mul, *S_neg, *S_len, *S_getitem, *S_setitem, *S_delitem, *S_contains, *S_iter, *S_next,
    *S_call, *S_bool, *S_hash, *S_enter, *S_exit, *S_name, *S_class, *S_args, *S_truediv,
    *S_floordiv, *S_mod, *S_pow, *S_radd, *S_rsub, *S_rmul, *S_abs, *S_int, *S_float,
    *S_index, *S_pos, *S_invert, *S_and, *S_or, *S_xor, *S_lshift, *S_rshift, *S_dict,
    *S_mro, *S_doc, *S_setter, *S_main, *S_getattr, *S_format, *S_self, *S_super, *S_setattr,
    *S_new, *S_module, *S_reversed;

static unsigned char ch_digit[256], ch_alpha[256], ch_alnum[256], ch_space[256],
    ch_upper[256], ch_lower[256], ch_hex[256], ch_print[256];
static unsigned char to_up[256], to_low[256];

#define V_ISINT(v) ((v).t == T_INT)
#define AS_STR(v) ((Str *)(v).u.o)
#define AS_LIST(v) ((List *)(v).u.o)
#define AS_DICT(v) ((Dict *)(v).u.o)
#define AS_CLASS(v) ((Class *)(v).u.o)
#define AS_INST(v) ((Inst *)(v).u.o)
#define AS_FUNC(v) ((Func *)(v).u.o)
#define AS_BIG(v) ((Big *)(v).u.o)
#define NOKW(x) do { (void)(x); } while (0)

/* ---- forward declarations (defined in later parts) -------------------- */
static void throw_val(Value exc);
static void throw_new(Class *c, const char *fmt, ...);
static Value call_value(Value fn, int n, Value *a, int nk, Value *kw);
static Value call0(Value fn);
static Value call1(Value fn, Value a);
static Value call2(Value fn, Value a, Value b);
static Value py_repr(Value v);
static Value py_str(Value v);
static int py_eq(Value a, Value b);
static int inst_cmp_value(int op, Value a, Value b, Value *out);
struct Module;
static int module_overlay(struct Module *md, Str *nm, Value *out);
static unsigned long py_hash(Value v);
static Value get_attr(Value o, Str *nm);
static Value get_iter(Value o);
static int iter_next(Value it, Value *out);
static int py_truthy(Value v);
static Class *class_of(Value v);
static Value *class_find(Class *c, Str *nm);
static int class_isa(Class *c, Class *base);
static int npabs_has(Class *abs, Class *k);
static Value new_exc(Class *c, const char *msg);

/* numpy subset (p7_np.c) hooks */
static Value nd_binop(int op, Value a, Value b);
static Value nd_unop(int op, Value a);
static Value nd_compare(int op, Value a, Value b);
static Value nd_subscr(Value o, Value idx);
static void nd_setitem(Value o, Value idx, Value v);
static long nd_len(Value o);
static int nd_truthy(Value o);
static Value nd_item(Value o, long i);
static int nd_getattr(Value o, Str *nm, Value *out);
static void nd_assign_all(Value dst, Value src);
static Value mkslice(Value lo, Value hi, Value st);
/* complex numbers (p7_cplx.c) */
static Value cplx_binop(int op, Value a, Value b);
static Value cplx_unop(int op, Value a);
static double n_hypot(double a, double b);

/* ---- value constructors ----------------------------------------------- */
static Value big_from_long(long n);
/* a long beyond +-SI_MAX (possible with a 64-bit long under -DPY_SI32 and for every
   host value that is not an index or a count) becomes a big integer, so that a
   T_INT always satisfies |n| <= SI_MAX and equal integers have equal representations */
static Value mkint(long n)
{
    Value v;
    if (n > SI_MAX || n < -SI_MAX) return big_from_long(n);
    v.t = T_INT; v.u.i = n;
    return v;
}
static Value mkfloat(double d)
{
    Value v;
    v.t = T_FLOAT; v.u.d = d;
    return v;
}
static Value mkbool(int b)
{
    return b ? TRUE_V : FALSE_V;
}
static Value mkobj(void *o, int t)
{
    Value v;
    v.t = t; v.u.o = (Hdr *)o;
    return v;
}
static Value tobool(int b)
{
    Value v;
    v.t = T_BOOL; v.u.i = b ? 1 : 0;
    return v;
}

/* ---- value stack ------------------------------------------------------ */
#define VPUSH(x) do { *vsp++ = (x); } while (0)
static void vcheck(long n)
{
    if (vsp + n >= vlimit)
        throw_new(cls_RecursionError, "maximum recursion depth exceeded");
}

/* ---- memory ----------------------------------------------------------- */
static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "MemoryError: out of memory\n");
        exit(1);
    }
    return p;
}
static void *xrealloc(void *q, size_t n)
{
    void *p = realloc(q, n ? n : 1);
    if (!p) {
        fprintf(stderr, "MemoryError: out of memory\n");
        exit(1);
    }
    return p;
}

static void *gc_alloc(size_t sz, int type)
{
    Hdr *h = (Hdr *)xmalloc(sz);
    h->type = (unsigned char)type;
    h->mark = 0;
    h->perm = (unsigned char)gc_perm;
    if (gc_perm) {
        h->next = perm_list;
        perm_list = h;
    } else {
        h->next = heap_list;
        heap_list = h;
        gc_bytes += (long)sz;
    }
    return h;
}
#define GC_ACCOUNT(n) do { gc_bytes += (long)(n); } while (0)

/* ---- strings ---------------------------------------------------------- */
static Str *str_new(const char *s, long n)
{
    Str *r = (Str *)gc_alloc(sizeof(Str) + (size_t)n, T_STR);
    r->len = n;
    r->hash = 0;
    if (n > 0) memcpy(r->s, s, (size_t)n);
    r->s[n] = 0;
    return r;
}
static Value mkstr(const char *s, long n)
{
    return mkobj(str_new(s, n), T_STR);
}
static Value mkbytes(const char *s, long n)
{
    Str *r = (Str *)gc_alloc(sizeof(Str) + (size_t)n, T_BYTES);
    r->len = n;
    r->hash = 0;
    if (n > 0) memcpy(r->s, s, (size_t)n);
    r->s[n] = 0;
    return mkobj(r, T_BYTES);
}
static Value mkbarr(const char *s, long n)
{
    BArr *r = (BArr *)gc_alloc(sizeof(BArr), T_BARR);
    r->n = n;
    r->cap = n < 8 ? 8 : n;
    r->s = (char *)xmalloc((size_t)r->cap);
    GC_ACCOUNT(r->cap);
    if (n > 0) memcpy(r->s, s, (size_t)n);
    return mkobj(r, T_BARR);
}
static void barr_reserve(BArr *r, long need)
{
    if (need > r->cap) {
        long nc = r->cap * 2;
        if (nc < need) nc = need;
        GC_ACCOUNT(nc - r->cap);
        r->s = (char *)xrealloc(r->s, (size_t)nc);
        r->cap = nc;
    }
}
static Value mkcomplex(double re, double im)
{
    Cplx *c = (Cplx *)gc_alloc(sizeof(Cplx), T_COMPLEX);
    c->re = re; c->im = im;
    return mkobj(c, T_COMPLEX);
}
static Value mkcstr(const char *s)
{
    return mkstr(s, (long)strlen(s));
}
static unsigned long str_hash(Str *s)
{
    unsigned long h;
    long i;
    if (s->hash) return s->hash;
    if (s->len == 0) return 0;          /* hash('') == 0 as in CPython (not cached) */
    h = 5381UL;
    for (i = 0; i < s->len; i++) h = ((h << 5) + h) ^ (unsigned char)s->s[i];
    h &= 0x7fffffffUL;
    if (h == 0) h = 1;
    s->hash = h;
    return h;
}

/* interned strings are permanent */
static Str **itab;
static long isize, icount;
static Str *intern(const char *s, long n)
{
    unsigned long h = 5381UL;
    long i, k;
    Str *r;
    int sp;
    if (!itab) {
        isize = 1024;
        itab = (Str **)xmalloc(sizeof(Str *) * (size_t)isize);
        for (i = 0; i < isize; i++) itab[i] = NULL;
    }
    for (i = 0; i < n; i++) h = ((h << 5) + h) ^ (unsigned char)s[i];
    h &= 0x7fffffffUL;
    if (h == 0) h = 1;
    k = (long)(h % (unsigned long)isize);
    while (itab[k]) {
        if (itab[k]->len == n && memcmp(itab[k]->s, s, (size_t)n) == 0) return itab[k];
        k = (k + 1) % isize;
    }
    sp = gc_perm;
    gc_perm = 1;
    r = str_new(s, n);
    gc_perm = sp;
    r->hash = h;
    itab[k] = r;
    icount++;
    if (icount * 2 > isize) {
        Str **old = itab;
        long osz = isize;
        isize *= 2;
        itab = (Str **)xmalloc(sizeof(Str *) * (size_t)isize);
        for (i = 0; i < isize; i++) itab[i] = NULL;
        for (i = 0; i < osz; i++) {
            if (old[i]) {
                k = (long)(old[i]->hash % (unsigned long)isize);
                while (itab[k]) k = (k + 1) % isize;
                itab[k] = old[i];
            }
        }
        free(old);
    }
    return r;
}
static Str *intern_c(const char *s)
{
    return intern(s, (long)strlen(s));
}
static Str *intern_str(Str *s)
{
    return intern(s->s, s->len);
}

/* ---- growable byte buffer --------------------------------------------- */
typedef struct Buf { char *s; long n, cap; } Buf;
static void buf_init(Buf *b)
{
    b->cap = 64; b->n = 0;
    b->s = (char *)xmalloc((size_t)b->cap);
    b->s[0] = 0;
}
static void buf_grow(Buf *b, long need)
{
    if (b->n + need + 1 > b->cap) {
        while (b->n + need + 1 > b->cap) b->cap *= 2;
        b->s = (char *)xrealloc(b->s, (size_t)b->cap);
    }
}
static void buf_addn(Buf *b, const char *s, long n)
{
    buf_grow(b, n);
    if (n > 0) memcpy(b->s + b->n, s, (size_t)n);
    b->n += n;
    b->s[b->n] = 0;
}
static void buf_add(Buf *b, const char *s)
{
    buf_addn(b, s, (long)strlen(s));
}
static void buf_addc(Buf *b, int c)
{
    buf_grow(b, 1);
    b->s[b->n++] = (char)c;
    b->s[b->n] = 0;
}
static Value buf_value(Buf *b)
{
    Value v = mkstr(b->s, b->n);
    free(b->s);
    b->s = NULL;
    return v;
}
static void buf_free(Buf *b)
{
    free(b->s);
    b->s = NULL;
}
static void nd_format(Value o, Buf *b, int repr);

/* ---- lists ------------------------------------------------------------ */
static List *list_new(long cap)
{
    List *l = (List *)gc_alloc(sizeof(List), T_LIST);
    l->n = 0;
    l->cap = cap < 4 ? 4 : cap;
    l->a = (Value *)xmalloc(sizeof(Value) * (size_t)l->cap);
    GC_ACCOUNT(sizeof(Value) * (size_t)l->cap);
    return l;
}
static void list_push(List *l, Value v)
{
    if (l->n >= l->cap) {
        GC_ACCOUNT(sizeof(Value) * (size_t)l->cap);
        l->cap *= 2;
        l->a = (Value *)xrealloc(l->a, sizeof(Value) * (size_t)l->cap);
    }
    l->a[l->n++] = v;
}
static Value mklist(List *l)
{
    return mkobj(l, T_LIST);
}
static List *tuple_new(long n)
{
    List *l = (List *)gc_alloc(sizeof(List), T_TUPLE);
    l->n = n;
    l->cap = n < 1 ? 1 : n;
    l->a = (Value *)xmalloc(sizeof(Value) * (size_t)l->cap);
    GC_ACCOUNT(sizeof(Value) * (size_t)l->cap);
    return l;
}
static Value mktuple2(Value a, Value b)
{
    List *t = tuple_new(2);
    t->a[0] = a; t->a[1] = b;
    return mkobj(t, T_TUPLE);
}
static Value mktuple_from(Value *a, long n)
{
    List *t = tuple_new(n);
    long i;
    for (i = 0; i < n; i++) t->a[i] = a[i];
    return mkobj(t, T_TUPLE);
}

/* ---- attribute maps (keys are interned strings) ----------------------- */
static Value *amap_get(AMap *m, Str *k)
{
    int i;
    for (i = 0; i < m->n; i++)
        if (m->k[i] == k) return &m->v[i];
    return NULL;
}
static void amap_set(AMap *m, Str *k, Value v)
{
    int i;
    for (i = 0; i < m->n; i++)
        if (m->k[i] == k) { m->v[i] = v; return; }
    if (m->n >= m->cap) {
        m->cap = m->cap ? m->cap * 2 : 4;
        m->k = (Str **)xrealloc(m->k, sizeof(Str *) * (size_t)m->cap);
        m->v = (Value *)xrealloc(m->v, sizeof(Value) * (size_t)m->cap);
    }
    m->k[m->n] = k;
    m->v[m->n] = v;
    m->n++;
}
static int amap_del(AMap *m, Str *k)
{
    int i;
    for (i = 0; i < m->n; i++) {
        if (m->k[i] == k) {
            for (; i + 1 < m->n; i++) { m->k[i] = m->k[i + 1]; m->v[i] = m->v[i + 1]; }
            m->n--;
            return 1;
        }
    }
    return 0;
}

/* ---- dictionaries and sets -------------------------------------------- */
static Dict *dict_new_t(int type)
{
    Dict *d = (Dict *)gc_alloc(sizeof(Dict), type);
    d->n = 0; d->used = 0; d->cap = 0; d->e = NULL; d->idx = NULL; d->isz = 0;
    return d;
}
static Dict *dict_new(void)
{
    return dict_new_t(T_DICT);
}
static void dict_rebuild(Dict *d, long newcap)
{
    long i, j, k, isz;
    DEnt *ne;
    isz = 8;
    while (isz < newcap * 2) isz *= 2;
    ne = (DEnt *)xmalloc(sizeof(DEnt) * (size_t)newcap);
    j = 0;
    for (i = 0; i < d->used; i++)
        if (d->e[i].k.t != T_UNSET) ne[j++] = d->e[i];
    free(d->e);
    free(d->idx);
    GC_ACCOUNT(sizeof(DEnt) * (size_t)newcap + sizeof(long) * (size_t)isz);
    d->e = ne;
    d->used = j;
    d->cap = newcap;
    d->isz = isz;
    d->idx = (long *)xmalloc(sizeof(long) * (size_t)isz);
    for (i = 0; i < isz; i++) d->idx[i] = -1;
    for (i = 0; i < j; i++) {
        k = (long)(ne[i].hash % (unsigned long)isz);
        while (d->idx[k] != -1) k = (k + 1) % isz;
        d->idx[k] = i;
    }
}
static long dict_find(Dict *d, Value k, unsigned long h)
{
    long i, e;
    if (!d->idx) return -1;
    i = (long)(h % (unsigned long)d->isz);
    for (;;) {
        e = d->idx[i];
        if (e == -1) return -1;
        if (e >= 0 && d->e[e].hash == h) {
            Value ek = d->e[e].k;
            if (ek.t == k.t && ek.t == T_INT ? ek.u.i == k.u.i :
                (ek.t == T_STR && k.t == T_STR) ?
                (AS_STR(ek)->len == AS_STR(k)->len &&
                 memcmp(AS_STR(ek)->s, AS_STR(k)->s, (size_t)AS_STR(k)->len) == 0) :
                py_eq(ek, k))
                return e;
        }
        i = (i + 1) % d->isz;
    }
}
static int dict_get(Dict *d, Value k, Value *out)
{
    long e = dict_find(d, k, py_hash(k));
    if (e < 0) return 0;
    *out = d->e[e].v;
    return 1;
}
static void dict_set(Dict *d, Value k, Value v)
{
    unsigned long h = py_hash(k);
    long e = dict_find(d, k, h);
    long i;
    if (e >= 0) { d->e[e].v = v; return; }
    if (d->used >= d->cap) {
        if (d->n * 2 < d->used) dict_rebuild(d, d->cap);
        else dict_rebuild(d, d->cap ? d->cap * 2 : 4);
    }
    d->e[d->used].k = k;
    d->e[d->used].v = v;
    d->e[d->used].hash = h;
    i = (long)(h % (unsigned long)d->isz);
    while (d->idx[i] >= 0) i = (i + 1) % d->isz;
    d->idx[i] = d->used;
    d->used++;
    d->n++;
}
static int dict_del(Dict *d, Value k)
{
    unsigned long h = py_hash(k);
    long e = dict_find(d, k, h);
    long i;
    if (e < 0) return 0;
    i = (long)(h % (unsigned long)d->isz);
    while (d->idx[i] != e) i = (i + 1) % d->isz;
    d->idx[i] = -2;
    d->e[e].k = UNSET_V;
    d->e[e].v = NONE_V;
    d->n--;
    return 1;
}
static void dict_clear(Dict *d)
{
    free(d->e); free(d->idx);
    d->e = NULL; d->idx = NULL; d->n = d->used = d->cap = d->isz = 0;
}
static void set_add(Dict *d, Value k)
{
    dict_set(d, k, NONE_V);
}

/* ---- garbage collector (mark and sweep) ------------------------------- */
static void gc_push(Value v)
{
    Hdr *h;
    if (!IS_HEAP(v.t)) return;
    h = v.u.o;
    if (!h || h->mark) return;
    h->mark = 1;
    if (ngray >= graycap) {
        graycap = graycap ? graycap * 2 : 4096;
        gray = (Hdr **)xrealloc(gray, sizeof(Hdr *) * (size_t)graycap);
    }
    gray[ngray++] = h;
}
static void gc_pushp(void *p, int t)
{
    if (p) gc_push(mkobj(p, t));
}
static void gc_mark_amap(AMap *m)
{
    int i;
    for (i = 0; i < m->n; i++) { gc_pushp(m->k[i], T_STR); gc_push(m->v[i]); }
}
static long gc_obj_size(Hdr *h)
{
    switch (h->type) {
    case T_STR: case T_BYTES: return (long)sizeof(Str) + ((Str *)h)->len;
    case T_BIG: return (long)sizeof(Big) + 2L * ((Big *)h)->n;
    case T_LIST: case T_TUPLE: case T_SLICE:
        return (long)sizeof(List) + (long)sizeof(Value) * ((List *)h)->cap;
    case T_ND: return (long)sizeof(Nd) + 16L * ((Nd *)h)->size;
    case T_BARR: return (long)sizeof(BArr) + ((BArr *)h)->cap;
    case T_DICT: case T_SET:
        return (long)sizeof(Dict) + (long)sizeof(DEnt) * ((Dict *)h)->cap +
            (long)sizeof(long) * ((Dict *)h)->isz;
    case T_INST: return (long)sizeof(Inst) + 24L * ((Inst *)h)->a.cap;
    case T_CLASS: return (long)sizeof(Class) + 24L * ((Class *)h)->ns.cap;
    case T_GEN
        : return (long)sizeof(Gen) + (long)sizeof(Value) * (((Gen *)h)->nloc + ((Gen *)h)->nstate);
    default: return 48;
    }
}
static void gc_scan(Hdr *h)
{
    long i;
    gc_live += gc_obj_size(h);
    switch (h->type) {
    case T_LIST: case T_TUPLE: case T_SLICE: {
        List *l = (List *)h;
        for (i = 0; i < l->n; i++) gc_push(l->a[i]);
        break; }
    case T_DICT: case T_SET: {
        Dict *d = (Dict *)h;
        for (i = 0; i < d->used; i++)
            if (d->e[i].k.t != T_UNSET) { gc_push(d->e[i].k); gc_push(d->e[i].v); }
        break; }
    case T_CLASS: {
        Class *c = (Class *)h;
        int k;
        gc_pushp(c->name, T_STR); gc_pushp(c->base, T_CLASS); gc_mark_amap(&c->ns);
        for (k = 0; k < c->nmro; k++) gc_pushp(c->mro[k], T_CLASS);
        for (k = 0; k < c->nbases; k++) gc_pushp(c->bases[k], T_CLASS);
        gc_pushp(c->paycls, T_CLASS); gc_pushp(c->modname, T_STR);
        break; }
    case T_INST: {
        Inst *n = (Inst *)h;
        gc_pushp(n->cls, T_CLASS); gc_mark_amap(&n->a); gc_push(n->pay);
        break; }
    case T_FUNC: {
        Func *f = (Func *)h;
        int k;
        for (k = 0; f->cells && k < f->def->fd->nfree; k++) gc_pushp(f->cells[k], T_CELL);
        for (k = 0; k < f->ndefs; k++) gc_push(f->defs[k]);
        for (k = 0; f->kwdefs && k < f->def->fd->nkwonly; k++) gc_push(f->kwdefs[k]);
        gc_pushp(f->defclass, T_CLASS); gc_pushp(f->name, T_STR);
        gc_mark_amap(&f->attrs);
        break; }
    case T_BOUND: {
        Bound *b = (Bound *)h;
        gc_push(b->self); gc_push(b->fn);
        break; }
    case T_CELL: gc_push(((Cell *)h)->v); break;
    case T_RANGE:
        gc_push(((Range *)h)->a); gc_push(((Range *)h)->b); gc_push(((Range *)h)->c);
        break;
    case T_MODULE:
        gc_pushp(((Module *)h)->name, T_STR); gc_mark_amap(&((Module *)h)->ns);
        gc_pushp(((Module *)h)->gp, T_STR); gc_pushp(((Module *)h)->gmap, T_DICT);
        break;
    case T_PROP: case T_SMETH: case T_CMETH:
        gc_push(((Prop *)h)->get); gc_push(((Prop *)h)->set); gc_push(((Prop *)h)->del);
        break;
    case T_SUPER: gc_pushp(((Super *)h)->start, T_CLASS); gc_push(((Super *)h)->obj); break;
    case T_GEN: {
        Gen *g = (Gen *)h;
        int k;
        for (k = 0; k < g->nloc; k++) gc_push(g->loc[k]);
        for (k = 0; k < g->nstate; k++) gc_push(g->state[k]);
        for (k = 0; g->cells && k < g->def->fd->nfree; k++) gc_pushp(g->cells[k], T_CELL);
        gc_push(g->yielded); gc_push(g->sent); gc_push(g->thrown); gc_push(g->ret);
        gc_pushp(g->fn, T_FUNC);
        break; }
    case T_ITER: {
        Iter *t = (Iter *)h;
        gc_push(t->a); gc_push(t->b); gc_push(t->c);
        break; }
    case T_BUILTIN: gc_pushp(((Builtin *)h)->sname, T_STR); break;
    case T_DVIEW: gc_push(((DView *)h)->d); break;
    default: break;
    }
}
static void gc_free_obj(Hdr *h)
{
    switch (h->type) {
    case T_LIST: case T_TUPLE: case T_SLICE: free(((List *)h)->a); break;
    case T_ND: free(((Nd *)h)->iv); free(((Nd *)h)->dv); break;
    case T_BARR: free(((BArr *)h)->s); break;
    case T_DICT: case T_SET: free(((Dict *)h)->e); free(((Dict *)h)->idx); break;
    case T_CLASS: free(((Class *)h)->ns.k); free(((Class *)h)->ns.v);
        free(((Class *)h)->mro); free(((Class *)h)->bases); break;
    case T_INST: free(((Inst *)h)->a.k); free(((Inst *)h)->a.v); break;
    case T_FUNC: free(((Func *)h)->cells); free(((Func *)h)->defs); free(((Func *)h)->kwdefs);
        free(((Func *)h)->attrs.k); free(((Func *)h)->attrs.v);
        break;
    case T_MODULE: free(((Module *)h)->ns.k); free(((Module *)h)->ns.v); break;
    case T_GEN: free(((Gen *)h)->loc); free(((Gen *)h)->state); free(((Gen *)h)->cells); break;
    case T_FILE: {
        File *f = (File *)h;
        if (f->fp && !f->closed && !f->out) fclose(f->fp);
        break; }
    default: break;
    }
    free(h);
}
static void gc_collect(void)
{
    Hdr *h, **pp;
    Value *p;
    Frame *f;
    long i, live;
    ngray = 0;
    gc_live = 0;
    for (h = perm_list; h; h = h->next) { h->mark = 0; }
    for (h = perm_list; h; h = h->next) { h->mark = 1; if (ngray >= graycap) {
            graycap = graycap ? graycap * 2 : 4096;
            gray = (Hdr **)xrealloc(gray, sizeof(Hdr *) * (size_t)graycap); }
        gray[ngray++] = h; }
    for (p = vstack; p < vsp; p++) gc_push(*p);
    for (i = 0; i < nglobals; i++) gc_push(G[i]);
    for (f = cur_frame; f; f = f->up) {
        gc_push(f->ret);
        gc_pushp(f->gen, T_GEN);
        gc_pushp(f->fn, T_FUNC);
        gc_pushp(f->ns, T_CLASS);
        if (f->cells && f->fn) {
            int k;
            for (k = 0; k < f->fn->def->fd->nfree; k++) gc_pushp(f->cells[k], T_CELL);
        }
    }
    gc_push(cur_exc);
    gc_push(handled_exc);
    while (ngray > 0) gc_scan(gray[--ngray]);
    live = 0;
    pp = &heap_list;
    while (*pp) {
        h = *pp;
        if (h->mark) { h->mark = 0; pp = &h->next; live++; }
        else { *pp = h->next; gc_free_obj(h); }
    }
    for (h = perm_list; h; h = h->next) h->mark = 0;
    (void)live;
    gc_bytes = gc_live;
    gc_limit = gc_bytes * 2 > PY_GC_MIN ? gc_bytes * 2 : PY_GC_MIN;
}
#ifdef PY_GC_STRESS
#define GC_POINT() do { if (gc_enabled) gc_collect(); } while (0)
#else
#define GC_POINT() do { if (gc_bytes > gc_limit && gc_enabled) gc_collect(); } while (0)
#endif

/* ---- errors ----------------------------------------------------------- */
static void throw_new(Class *c, const char *fmt, ...)
{
    char buf[1200];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    buf[1000] = 0;
    throw_val(new_exc(c, buf));
}
