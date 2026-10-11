/* Python 3 subset interpreter in portable C89 - the parts are inlined by tools/amalg.sh */
/* ===== p0_base.c ===== */
/* =========================================================================
   Python 3 subset interpreter in portable C89.

   p0_base.c   types, globals, memory + garbage collector, strings,
               dictionaries, attribute maps, errors.

   Portability rules: strict C89 (no //, no declarations after statements,
   no long long, no stdint.h), all functions static, character classes by
   literal sets.  Integers: a small int is a long with |n| <= SI_MAX,
   larger integers are big integers (base 10000 limbs).
   ========================================================================= */
#ifdef PY_POSIX
#define _XOPEN_SOURCE 600
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>
#include <float.h>

#ifdef PY_POSIX
#include <unistd.h>
#include <sys/time.h>
#endif

#define PY_VERSION "1.0"

#if LONG_MAX > 2147483647L
#define SI_MAX 999999999999999999L
#define SI_MULLIM 1000000000L
#else
#define SI_MAX 999999999L
#define SI_MULLIM 31622L
#endif

#define VSTACK_SIZE 400000L
#define MAX_DEPTH 1000

/* ---- values ----------------------------------------------------------- */
enum {
    T_UNSET = 0, T_NONE, T_BOOL, T_INT, T_FLOAT,
    T_STR, T_BIG, T_LIST, T_TUPLE, T_DICT, T_SET, T_RANGE, T_FUNC, T_BUILTIN,
    T_CLASS, T_INST, T_BOUND, T_GEN, T_ITER, T_CELL, T_MODULE, T_PROP,
    T_SMETH, T_CMETH, T_SUPER, T_FILE, T_ND, T_SLICE, T_DVIEW, T_BYTES, T_NTYPES
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
typedef struct Range { Hdr h; long start, stop, step; } Range;
#define ND_MAXD 8
enum { ND_BOOL = 0, ND_INT = 1, ND_FLOAT = 2 };
typedef struct Nd {
    Hdr h;
    int dt, nd;
    long shape[ND_MAXD];
    long size;
    long *iv;            /* bool and int data */
    double *dv;          /* float data */
} Nd;
#define AS_ND(v) ((Nd *)(v).u.o)
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
       IK_CALLSENT };
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
    *cls_smeth, *cls_cmeth, *cls_super, *cls_ndarray, *cls_slice, *cls_dview[3], *cls_notimpl, *cls_bytes;
static Class *cls_BaseException, *cls_Exception, *cls_ArithmeticError, *cls_ZeroDivisionError,
    *cls_OverflowError, *cls_LookupError, *cls_IndexError, *cls_KeyError, *cls_ValueError,
    *cls_TypeError, *cls_NameError, *cls_UnboundLocalError, *cls_AttributeError,
    *cls_RuntimeError, *cls_RecursionError, *cls_NotImplementedError, *cls_StopIteration,
    *cls_AssertionError, *cls_SystemExit, *cls_ImportError, *cls_OSError, *cls_EOFError,
    *cls_SyntaxError, *cls_KeyboardInterrupt, *cls_GeneratorExit, *cls_FileNotFoundError;

/* interned names used by the runtime */
static Str *S_init, *S_str, *S_repr, *S_eq, *S_ne, *S_lt, *S_le, *S_gt, *S_ge, *S_add, *S_sub,
    *S_mul, *S_neg, *S_len, *S_getitem, *S_setitem, *S_delitem, *S_contains, *S_iter, *S_next,
    *S_call, *S_bool, *S_hash, *S_enter, *S_exit, *S_name, *S_class, *S_args, *S_truediv,
    *S_floordiv, *S_mod, *S_pow, *S_radd, *S_rsub, *S_rmul, *S_abs, *S_int, *S_float,
    *S_index, *S_pos, *S_invert, *S_and, *S_or, *S_xor, *S_lshift, *S_rshift, *S_dict,
    *S_mro, *S_doc, *S_setter, *S_main, *S_getattr, *S_format, *S_self, *S_super, *S_setattr,
    *S_new, *S_module, *S_reversed;

static unsigned char ch_digit[256], ch_alpha[256], ch_alnum[256], ch_space[256],
    ch_upper[256], ch_lower[256], ch_hex[256];
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

/* ---- value constructors ----------------------------------------------- */
static Value mkint(long n)
{
    Value v;
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
static Value mkcstr(const char *s)
{
    return mkstr(s, (long)strlen(s));
}
static unsigned long str_hash(Str *s)
{
    unsigned long h;
    long i;
    if (s->hash) return s->hash;
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
    GC_ACCOUNT(sizeof(Value) * l->cap);
    return l;
}
static void list_push(List *l, Value v)
{
    if (l->n >= l->cap) {
        GC_ACCOUNT(sizeof(Value) * l->cap);
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
    GC_ACCOUNT(sizeof(Value) * l->cap);
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
    GC_ACCOUNT(sizeof(DEnt) * newcap + sizeof(long) * isz);
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
    case T_ND: return (long)sizeof(Nd) + 8L * ((Nd *)h)->size;
    case T_DICT: case T_SET:
        return (long)sizeof(Dict) + (long)sizeof(DEnt) * ((Dict *)h)->cap +
            (long)sizeof(long) * ((Dict *)h)->isz;
    case T_INST: return (long)sizeof(Inst) + 24L * ((Inst *)h)->a.cap;
    case T_CLASS: return (long)sizeof(Class) + 24L * ((Class *)h)->ns.cap;
    case T_GEN: return (long)sizeof(Gen) + (long)sizeof(Value) * (((Gen *)h)->nloc + ((Gen *)h)->nstate);
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
/* ===== p1_num.c ===== */
/* =========================================================================
   p1_num.c   integers (small + big), floats, number <-> text
   ========================================================================= */
#define BASE 10000L

typedef struct BN { int sign; int n; long *d; } BN;

static int is_intv(Value v)
{
    return v.t == T_INT || v.t == T_BOOL || v.t == T_BIG;
}

static void bn_trim(BN *b)
{
    while (b->n > 0 && b->d[b->n - 1] == 0) b->n--;
    if (b->n == 0) b->sign = 0;
}
static void bn_alloc(BN *b, int n)
{
    b->d = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    b->n = n;
    b->sign = 1;
}
static void bn_from_long(BN *b, long v)
{
    unsigned long u;
    int i = 0;
    bn_alloc(b, 6);
    if (v < 0) { b->sign = -1; u = (unsigned long)(-v); }
    else { b->sign = 1; u = (unsigned long)v; }
    while (u > 0) { b->d[i++] = (long)(u % (unsigned long)BASE); u /= (unsigned long)BASE; }
    b->n = i;
    bn_trim(b);
}
static void bn_from_value(BN *b, Value v)
{
    if (v.t == T_BIG) {
        Big *g = AS_BIG(v);
        int i;
        bn_alloc(b, g->n);
        b->sign = g->sign;
        for (i = 0; i < g->n; i++) b->d[i] = g->d[i];
    } else {
        bn_from_long(b, v.u.i);
    }
}
static Value bn_to_value(BN *b)
{
    bn_trim(b);
    {
        unsigned long u = 0;
        int i, ok = 1;
        for (i = b->n - 1; i >= 0; i--) {
            if (u > (ULONG_MAX - 9999UL) / 10000UL) { ok = 0; break; }
            u = u * 10000UL + (unsigned long)b->d[i];
        }
        if (ok && u <= (unsigned long)SI_MAX) {
            Value r = mkint(b->sign < 0 ? -(long)u : (long)u);
            free(b->d);
            return r;
        }
    }
    {
        Big *g = (Big *)gc_alloc(sizeof(Big) + sizeof(unsigned short) * (size_t)b->n, T_BIG);
        int i;
        g->sign = b->sign;
        g->n = b->n;
        for (i = 0; i < b->n; i++) g->d[i] = (unsigned short)b->d[i];
        free(b->d);
        return mkobj(g, T_BIG);
    }
}
static int mag_cmp(const long *a, int na, const long *b, int nb)
{
    int i;
    if (na != nb) return na < nb ? -1 : 1;
    for (i = na - 1; i >= 0; i--)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
/* r = a + b magnitudes */
static void mag_add(BN *r, const BN *a, const BN *b)
{
    int n = (a->n > b->n ? a->n : b->n), i;
    long c = 0, s;
    bn_alloc(r, n + 1);
    for (i = 0; i < n; i++) {
        s = (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0) + c;
        if (s >= BASE) { s -= BASE; c = 1; } else c = 0;
        r->d[i] = s;
    }
    r->d[n] = c;
    bn_trim(r);
}
/* r = a - b magnitudes, requires |a| >= |b| */
static void mag_sub(BN *r, const BN *a, const BN *b)
{
    int i;
    long c = 0, s;
    bn_alloc(r, a->n);
    for (i = 0; i < a->n; i++) {
        s = a->d[i] - (i < b->n ? b->d[i] : 0) - c;
        if (s < 0) { s += BASE; c = 1; } else c = 0;
        r->d[i] = s;
    }
    bn_trim(r);
}
static void mag_mul(BN *r, const BN *a, const BN *b)
{
    int i, j;
    long c, t;
    bn_alloc(r, a->n + b->n + 1);
    for (i = 0; i < r->n; i++) r->d[i] = 0;
    for (i = 0; i < a->n; i++) {
        c = 0;
        for (j = 0; j < b->n; j++) {
            t = r->d[i + j] + a->d[i] * b->d[j] + c;
            c = t / BASE;
            r->d[i + j] = t % BASE;
        }
        j = i + b->n;
        while (c) { t = r->d[j] + c; c = t / BASE; r->d[j] = t % BASE; j++; }
    }
    bn_trim(r);
}
/* short division of magnitude by small s (< BASE*... up to 32767) */
static long mag_divsmall(BN *a, long s)
{
    int i;
    long rem = 0, t;
    for (i = a->n - 1; i >= 0; i--) {
        t = rem * BASE + a->d[i];
        a->d[i] = t / s;
        rem = t % s;
    }
    bn_trim(a);
    return rem;
}
/* Knuth algorithm D: q = a / b, r = a % b (magnitudes) */
static void mag_divmod(const BN *a, const BN *b, BN *q, BN *r)
{
    int m = a->n, n = b->n, i, j;
    long d, qhat, rhat, num, borrow, carry, p, sub, t;
    long *un, *vn;
    if (mag_cmp(a->d, a->n, b->d, b->n) < 0) {
        bn_alloc(q, 1); q->d[0] = 0; q->n = 0; q->sign = 0;
        bn_alloc(r, a->n);
        for (i = 0; i < a->n; i++) r->d[i] = a->d[i];
        bn_trim(r);
        return;
    }
    if (n == 1) {
        long rem = 0;
        bn_alloc(q, m);
        for (i = m - 1; i >= 0; i--) {
            t = rem * BASE + a->d[i];
            q->d[i] = t / b->d[0];
            rem = t % b->d[0];
        }
        bn_trim(q);
        bn_from_long(r, rem);
        return;
    }
    un = (long *)xmalloc(sizeof(long) * (size_t)(m + 1));
    vn = (long *)xmalloc(sizeof(long) * (size_t)n);
    d = BASE / (b->d[n - 1] + 1);
    carry = 0;
    for (i = 0; i < m; i++) { t = a->d[i] * d + carry; un[i] = t % BASE; carry = t / BASE; }
    un[m] = carry;
    carry = 0;
    for (i = 0; i < n; i++) { t = b->d[i] * d + carry; vn[i] = t % BASE; carry = t / BASE; }
    bn_alloc(q, m - n + 1);
    for (j = m - n; j >= 0; j--) {
        num = un[j + n] * BASE + un[j + n - 1];
        qhat = num / vn[n - 1];
        rhat = num % vn[n - 1];
        while (qhat >= BASE || qhat * vn[n - 2] > rhat * BASE + un[j + n - 2]) {
            qhat--;
            rhat += vn[n - 1];
            if (rhat >= BASE) break;
        }
        borrow = 0; carry = 0;
        for (i = 0; i < n; i++) {
            p = qhat * vn[i] + carry;
            carry = p / BASE;
            sub = un[i + j] - (p % BASE) - borrow;
            if (sub < 0) { sub += BASE; borrow = 1; } else borrow = 0;
            un[i + j] = sub;
        }
        sub = un[j + n] - carry - borrow;
        if (sub < 0) { sub += BASE; borrow = 1; } else borrow = 0;
        un[j + n] = sub;
        if (borrow) {
            qhat--;
            carry = 0;
            for (i = 0; i < n; i++) {
                t = un[i + j] + vn[i] + carry;
                un[i + j] = t % BASE;
                carry = t / BASE;
            }
            un[j + n] = (un[j + n] + carry) % BASE;
        }
        q->d[j] = qhat;
    }
    bn_trim(q);
    bn_alloc(r, n);
    for (i = 0; i < n; i++) r->d[i] = un[i];
    bn_trim(r);
    mag_divsmall(r, d);
    free(un);
    free(vn);
}

static Value big_add(Value a, Value b, int sub)
{
    BN x, y, r;
    Value res;
    bn_from_value(&x, a);
    bn_from_value(&y, b);
    if (sub) y.sign = -y.sign;
    if (y.sign == 0) { free(y.d); free(x.d); return a; }
    if (x.sign == 0 || x.n == 0) {
        free(x.d);
        res = bn_to_value(&y);
        return res;
    }
    if (x.sign == y.sign) {
        mag_add(&r, &x, &y);
        r.sign = x.sign;
    } else {
        int c = mag_cmp(x.d, x.n, y.d, y.n);
        if (c == 0) { bn_alloc(&r, 1); r.d[0] = 0; r.n = 0; r.sign = 0; }
        else if (c > 0) { mag_sub(&r, &x, &y); r.sign = x.sign; }
        else { mag_sub(&r, &y, &x); r.sign = y.sign; }
    }
    free(x.d); free(y.d);
    return bn_to_value(&r);
}
static Value int_add(Value a, Value b)
{
    if (a.t != T_BIG && b.t != T_BIG) {
        long r = a.u.i + b.u.i;
        if (r <= SI_MAX && r >= -SI_MAX) return mkint(r);
    }
    return big_add(a, b, 0);
}
static Value int_sub(Value a, Value b)
{
    if (a.t != T_BIG && b.t != T_BIG) {
        long r = a.u.i - b.u.i;
        if (r <= SI_MAX && r >= -SI_MAX) return mkint(r);
    }
    return big_add(a, b, 1);
}
static Value int_mul(Value a, Value b)
{
    BN x, y, r;
    if (a.t != T_BIG && b.t != T_BIG &&
        a.u.i < SI_MULLIM && a.u.i > -SI_MULLIM && b.u.i < SI_MULLIM && b.u.i > -SI_MULLIM)
        return mkint(a.u.i * b.u.i);
    bn_from_value(&x, a);
    bn_from_value(&y, b);
    if (x.sign == 0 || y.sign == 0) { free(x.d); free(y.d); return mkint(0); }
    mag_mul(&r, &x, &y);
    r.sign = x.sign * y.sign;
    free(x.d); free(y.d);
    return bn_to_value(&r);
}
static Value int_neg(Value a)
{
    BN x;
    if (a.t != T_BIG) return mkint(-a.u.i);
    bn_from_value(&x, a);
    x.sign = -x.sign;
    return bn_to_value(&x);
}
static int int_sign(Value a)
{
    if (a.t == T_BIG) return AS_BIG(a)->sign;
    return a.u.i > 0 ? 1 : (a.u.i < 0 ? -1 : 0);
}
static int int_cmp(Value a, Value b)
{
    if (a.t != T_BIG && b.t != T_BIG)
        return a.u.i < b.u.i ? -1 : (a.u.i > b.u.i ? 1 : 0);
    {
        BN x, y;
        int c;
        bn_from_value(&x, a);
        bn_from_value(&y, b);
        if (x.sign != y.sign) c = x.sign < y.sign ? -1 : 1;
        else {
            c = mag_cmp(x.d, x.n, y.d, y.n);
            if (x.sign < 0) c = -c;
        }
        free(x.d); free(y.d);
        return c;
    }
}
/* floor division and modulo */
static void int_divmod(Value a, Value b, Value *q, Value *r)
{
    if (int_sign(b) == 0)
        throw_new(cls_ZeroDivisionError, "integer division or modulo by zero");
    if (a.t != T_BIG && b.t != T_BIG) {
        long x = a.u.i, y = b.u.i, qq = x / y, rr = x % y;
        if (rr != 0 && ((rr < 0) != (y < 0))) { qq--; rr += y; }
        *q = mkint(qq); *r = mkint(rr);
        return;
    }
    {
        BN x, y, qq, rr;
        int sx, sy;
        Value vq, vr;
        bn_from_value(&x, a);
        bn_from_value(&y, b);
        sx = x.sign; sy = y.sign;
        if (sx == 0) {
            free(x.d); free(y.d);
            *q = mkint(0); *r = mkint(0);
            return;
        }
        mag_divmod(&x, &y, &qq, &rr);
        if (sx == sy) {
            qq.sign = qq.n ? 1 : 0;
            rr.sign = rr.n ? sy : 0;
        } else if (rr.n == 0) {
            qq.sign = qq.n ? -1 : 0;
            rr.sign = 0;
        } else {
            BN one, t, nq, nr;
            bn_from_long(&one, 1);
            qq.sign = 1;
            mag_add(&nq, &qq, &one);
            nq.sign = -1;
            mag_sub(&nr, &y, &rr);
            nr.sign = sy;
            (void)t;
            free(one.d); free(qq.d); free(rr.d);
            qq = nq; rr = nr;
        }
        free(x.d); free(y.d);
        vq = bn_to_value(&qq);
        vr = bn_to_value(&rr);
        *q = vq; *r = vr;
    }
}
static Value int_floordiv(Value a, Value b)
{
    Value q, r;
    int_divmod(a, b, &q, &r);
    return q;
}
static Value int_mod(Value a, Value b)
{
    Value q, r;
    if (int_sign(b) == 0) throw_new(cls_ZeroDivisionError, "integer modulo by zero");
    int_divmod(a, b, &q, &r);
    return r;
}
static void int_str(Value v, Buf *b);
static double int_todouble(Value a)
{
    if (a.t != T_BIG) return (double)a.u.i;
    {
        Buf b;
        double d;
        buf_init(&b);
        int_str(a, &b);
        d = strtod(b.s, NULL);
        buf_free(&b);
        if (d > DBL_MAX || d < -DBL_MAX)
            throw_new(cls_OverflowError, "int too large to convert to float");
        return d;
    }
}
static Value int_pow(Value a, long e)
{
    Value r = mkint(1), base = a;
    while (e > 0) {
        if (e & 1) r = int_mul(r, base);
        e >>= 1;
        if (e) base = int_mul(base, base);
    }
    return r;
}
static void int_str(Value v, Buf *b)
{
    char t[40];
    if (v.t != T_BIG) {
        sprintf(t, "%ld", v.u.i);
        buf_add(b, t);
    } else {
        Big *g = AS_BIG(v);
        int i;
        if (g->sign < 0) buf_addc(b, '-');
        sprintf(t, "%d", (int)g->d[g->n - 1]);
        buf_add(b, t);
        for (i = g->n - 2; i >= 0; i--) {
            sprintf(t, "%04d", (int)g->d[i]);
            buf_add(b, t);
        }
    }
}
/* digits in base 2, 8, 16 (no prefix) */
static void int_str_base(Value v, int base, Buf *b)
{
    static const char dg[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[400];
    int n = 0, neg = 0, i;
    if (int_sign(v) < 0) { neg = 1; v = int_neg(v); }
    if (int_sign(v) == 0) { buf_addc(b, '0'); return; }
    if (v.t != T_BIG) {
        long x = v.u.i;
        while (x > 0 && n < 399) { tmp[n++] = dg[x % base]; x /= base; }
    } else {
        BN x;
        bn_from_value(&x, v);
        while (x.n > 0 && n < 399) tmp[n++] = dg[mag_divsmall(&x, base)];
        free(x.d);
    }
    if (neg) buf_addc(b, '-');
    for (i = n - 1; i >= 0; i--) buf_addc(b, tmp[i]);
}
static int digit_val(int c)
{
    const char *p;
    static const char lo[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    static const char up[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (c == 0) return -1;
    p = strchr(lo, c);
    if (p) return (int)(p - lo);
    p = strchr(up, c);
    if (p) return (int)(p - up);
    return -1;
}
/* parse an integer from text; returns 0 on error */
static int parse_int(const char *s, long n, int base, Value *out)
{
    long i = 0, e = n;
    int neg = 0, d, any = 0, lastus = 0;
    Value acc = mkint(0), bv;
    while (i < e && ch_space[(unsigned char)s[i]]) i++;
    while (e > i && ch_space[(unsigned char)s[e - 1]]) e--;
    if (i < e && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); i++; }
    if (i + 1 < e && s[i] == '0') {
        int c = s[i + 1];
        if ((c == 'x' || c == 'X') && (base == 16 || base == 0)) { base = 16; i += 2; }
        else if ((c == 'o' || c == 'O') && (base == 8 || base == 0)) { base = 8; i += 2; }
        else if ((c == 'b' || c == 'B') && (base == 2 || base == 0)) { base = 2; i += 2; }
    }
    if (base == 0) base = 10;
    bv = mkint(base);
    for (; i < e; i++) {
        if (s[i] == '_' && any && !lastus) { lastus = 1; continue; }
        d = digit_val((unsigned char)s[i]);
        if (d < 0 || d >= base) return 0;
        lastus = 0;
        any = 1;
        if (acc.t != T_BIG && acc.u.i < SI_MAX / 40 / 1)
            acc = mkint(acc.u.i * base + d);
        else
            acc = int_add(int_mul(acc, bv), mkint(d));
    }
    if (!any || lastus) return 0;
    *out = neg ? int_neg(acc) : acc;
    return 1;
}
static Value int_from_double(double d)
{
    char b[400];
    Value r;
    if (d != d) throw_new(cls_ValueError, "cannot convert float NaN to integer");
    if (d > DBL_MAX || d < -DBL_MAX)
        throw_new(cls_OverflowError, "cannot convert float infinity to integer");
    if (d < 1e17 && d > -1e17) return mkint((long)d);
    sprintf(b, "%.0f", d);
    if (parse_int(b, (long)strlen(b), 10, &r)) return r;
    return mkint(0);
}
static unsigned long int_hash(Value v)
{
    if (v.t != T_BIG) return (unsigned long)(v.u.i < 0 ? -v.u.i : v.u.i) * 2654435761UL
                              + (v.u.i < 0 ? 1UL : 0UL);
    {
        Big *g = AS_BIG(v);
        unsigned long h = 7;
        int i;
        for (i = 0; i < g->n; i++) h = h * 31 + g->d[i];
        return h;
    }
}
static double fl_mod(double a, double b)
{
    double r = fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}
static double fl_floordiv(double a, double b)
{
    double mod = fmod(a, b), div = (a - mod) / b, fd;
    if (mod != 0 && ((b < 0) != (mod < 0))) div -= 1.0;
    if (div != 0) {
        fd = floor(div);
        if (div - fd > 0.5) fd += 1.0;
    } else {
        fd = a / b < 0 ? -0.0 : 0.0;
    }
    return fd;
}
/* shortest round-trip repr of a float */
static void float_repr(double d, char *out)
{
    char b[48], digits[32];
    int p, nd = 0, exp10, neg = 0, decpt, i;
    char *q;
    if (d != d) { strcpy(out, "nan"); return; }
    if (d > DBL_MAX) { strcpy(out, "inf"); return; }
    if (d < -DBL_MAX) { strcpy(out, "-inf"); return; }
    if (d == 0) { strcpy(out, (1.0 / d < 0) ? "-0.0" : "0.0"); return; }
    for (p = 1; p <= 17; p++) {
        sprintf(b, "%.*e", p - 1, d);
        if (strtod(b, NULL) == d) break;
    }
    q = b;
    if (*q == '-') { neg = 1; q++; }
    while (*q && *q != 'e') {
        if (*q != '.') digits[nd++] = *q;
        q++;
    }
    digits[nd] = 0;
    exp10 = atoi(q + 1);
    while (nd > 1 && digits[nd - 1] == '0') digits[--nd] = 0;
    decpt = exp10 + 1;
    q = out;
    if (neg) *q++ = '-';
    if (decpt > 16 || decpt < -3) {
        *q++ = digits[0];
        if (nd > 1) { *q++ = '.'; for (i = 1; i < nd; i++) *q++ = digits[i]; }
        sprintf(q, "e%c%02d", exp10 < 0 ? '-' : '+', exp10 < 0 ? -exp10 : exp10);
    } else if (decpt <= 0) {
        *q++ = '0'; *q++ = '.';
        for (i = 0; i < -decpt; i++) *q++ = '0';
        for (i = 0; i < nd; i++) *q++ = digits[i];
        *q = 0;
    } else if (decpt >= nd) {
        for (i = 0; i < nd; i++) *q++ = digits[i];
        for (i = 0; i < decpt - nd; i++) *q++ = '0';
        *q++ = '.'; *q++ = '0'; *q = 0;
    } else {
        for (i = 0; i < decpt; i++) *q++ = digits[i];
        *q++ = '.';
        for (i = decpt; i < nd; i++) *q++ = digits[i];
        *q = 0;
    }
}
static int parse_float(const char *s, long n, double *out)
{
    char tmp[400];
    char *end;
    long i = 0, e = n;
    int k;
    while (i < e && ch_space[(unsigned char)s[i]]) i++;
    while (e > i && ch_space[(unsigned char)s[e - 1]]) e--;
    if (e - i <= 0 || e - i > 380) return 0;
    memcpy(tmp, s + i, (size_t)(e - i));
    tmp[e - i] = 0;
    for (k = 0; tmp[k]; k++) tmp[k] = (char)to_low[(unsigned char)tmp[k]];
    {
        const char *t = tmp;
        int sg = 1;
        if (*t == '-') { sg = -1; t++; } else if (*t == '+') t++;
        if (strcmp(t, "inf") == 0 || strcmp(t, "infinity") == 0) { *out = sg * HUGE_VAL; return 1; }
        if (strcmp(t, "nan") == 0) { *out = sg * HUGE_VAL - HUGE_VAL; return 1; }
        if (!(ch_digit[(unsigned char)*t] || *t == '.')) return 0;
        if (strchr(t, 'x') || strchr(t, 'n') || strchr(t, 'i')) return 0;
    }
    *out = strtod(tmp, &end);
    return *end == 0;
}
/* ===== p2_lex.c ===== */
/* =========================================================================
   p2_lex.c   tokens, character tables, lexer with INDENT/DEDENT
   ========================================================================= */
enum {
    TK_EOF = 1000, TK_NEWLINE, TK_INDENT, TK_DEDENT, TK_NAME, TK_NUM, TK_FLOAT, TK_STR,
    TK_FSTR, TK_KW, TK_EQ, TK_NE, TK_LE, TK_GE, TK_POW, TK_FDIV, TK_LSH, TK_RSH,
    TK_ARROW, TK_ELLIPSIS, TK_AUG, TK_WALRUS
};
enum {
    B_NONE = 0, B_ADD, B_SUB, B_MUL, B_DIV, B_FDIV, B_MOD, B_POW, B_AND, B_OR, B_XOR,
    B_LSH, B_RSH, B_MATMUL
};
enum {
    KW_NONE = 0, KW_FALSE, KW_NONE_, KW_TRUE, KW_AND, KW_AS, KW_ASSERT, KW_BREAK, KW_CLASS,
    KW_CONTINUE, KW_DEF, KW_DEL, KW_ELIF, KW_ELSE, KW_EXCEPT, KW_FINALLY, KW_FOR, KW_FROM,
    KW_GLOBAL, KW_IF, KW_IMPORT, KW_IN, KW_IS, KW_LAMBDA, KW_NONLOCAL, KW_NOT, KW_OR,
    KW_PASS, KW_RAISE, KW_RETURN, KW_TRY, KW_WHILE, KW_WITH, KW_YIELD
};
static const char *kw_names[] = {
    "", "False", "None", "True", "and", "as", "assert", "break", "class", "continue", "def",
    "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in",
    "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while",
    "with", "yield", NULL
};

typedef struct Tok {
    int type;
    int line;
    int kw;
    int op;
    int raw;
    double fval;
    Value val;
    Str *s;
} Tok;

static Tok *toks;
static int ntoks, tokcap;

static const char *CS_DIGIT = "0123456789";
static const char *CS_UP = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char *CS_LOW = "abcdefghijklmnopqrstuvwxyz";
static const char *CS_HEX = "0123456789abcdefABCDEF";

static void init_chars(void)
{
    const char *p;
    int i;
    for (i = 0; i < 256; i++) {
        ch_digit[i] = ch_alpha[i] = ch_alnum[i] = ch_space[i] = ch_upper[i] = ch_lower[i] = 0;
        ch_hex[i] = 0;
        to_up[i] = to_low[i] = (unsigned char)i;
    }
    for (p = CS_DIGIT; *p; p++) { ch_digit[(unsigned char)*p] = 1; ch_alnum[(unsigned char)*p] = 1; }
    for (p = CS_UP; *p; p++) {
        ch_upper[(unsigned char)*p] = 1; ch_alpha[(unsigned char)*p] = 1;
        ch_alnum[(unsigned char)*p] = 1;
    }
    for (p = CS_LOW; *p; p++) {
        ch_lower[(unsigned char)*p] = 1; ch_alpha[(unsigned char)*p] = 1;
        ch_alnum[(unsigned char)*p] = 1;
    }
    for (i = 0; CS_UP[i] && CS_LOW[i]; i++) {
        to_up[(unsigned char)CS_LOW[i]] = (unsigned char)CS_UP[i];
        to_low[(unsigned char)CS_UP[i]] = (unsigned char)CS_LOW[i];
    }
    for (p = CS_HEX; *p; p++) ch_hex[(unsigned char)*p] = 1;
    ch_alpha[(unsigned char)'_'] = 1;
    ch_alnum[(unsigned char)'_'] = 1;
    for (p = " \t\n\r\f\v"; *p; p++) ch_space[(unsigned char)*p] = 1;
}

static void syntax_error(int line, const char *fmt, ...)
{
    char buf[400];
    char buf2[460];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    if (strncmp(buf, "SyntaxError: ", 13) == 0) memmove(buf, buf + 13, strlen(buf) - 12);
    cur_line = line;
    sprintf(buf2, "%.380s (line %d)", buf, line);
    throw_val(new_exc(cls_SyntaxError, buf2));
}

static Tok *tok_new(int type, int line)
{
    Tok *t;
    if (ntoks >= tokcap) {
        tokcap = tokcap ? tokcap * 2 : 256;
        toks = (Tok *)xrealloc(toks, sizeof(Tok) * (size_t)tokcap);
    }
    t = &toks[ntoks++];
    t->type = type; t->line = line; t->kw = 0; t->op = 0; t->raw = 0;
    t->fval = 0; t->val = NONE_V; t->s = NULL;
    return t;
}

static void put_utf8(Buf *b, unsigned long c)
{
    if (c < 0x80UL) buf_addc(b, (int)c);
    else if (c < 0x800UL) {
        buf_addc(b, (int)(0xC0UL | (c >> 6)));
        buf_addc(b, (int)(0x80UL | (c & 0x3FUL)));
    } else if (c < 0x10000UL) {
        buf_addc(b, (int)(0xE0UL | (c >> 12)));
        buf_addc(b, (int)(0x80UL | ((c >> 6) & 0x3FUL)));
        buf_addc(b, (int)(0x80UL | (c & 0x3FUL)));
    } else {
        buf_addc(b, (int)(0xF0UL | (c >> 18)));
        buf_addc(b, (int)(0x80UL | ((c >> 12) & 0x3FUL)));
        buf_addc(b, (int)(0x80UL | ((c >> 6) & 0x3FUL)));
        buf_addc(b, (int)(0x80UL | (c & 0x3FUL)));
    }
}

/* process escapes of a string body into b */
static void unescape(const char *s, long n, Buf *b)
{
    long i = 0;
    while (i < n) {
        int c = s[i++];
        if (c != '\\' || i >= n) { buf_addc(b, c); continue; }
        c = s[i++];
        switch (c) {
        case 'n': buf_addc(b, '\n'); break;
        case 't': buf_addc(b, '\t'); break;
        case 'r': buf_addc(b, '\r'); break;
        case 'a': buf_addc(b, '\a'); break;
        case 'b': buf_addc(b, '\b'); break;
        case 'f': buf_addc(b, '\f'); break;
        case 'v': buf_addc(b, '\v'); break;
        case '\\': buf_addc(b, '\\'); break;
        case '\'': buf_addc(b, '\''); break;
        case '"': buf_addc(b, '"'); break;
        case '\n': break;
        case 'x': case 'u': case 'U': {
            int cnt = (c == 'x') ? 2 : (c == 'u' ? 4 : 8), k;
            unsigned long v = 0;
            for (k = 0; k < cnt && i < n && ch_hex[(unsigned char)s[i]]; k++, i++)
                v = v * 16 + (unsigned long)digit_val((unsigned char)s[i]);
            if (c == 'x') buf_addc(b, (int)v); else put_utf8(b, v);
            break; }
        default:
            if (c >= '0' && c <= '7') {
                unsigned long v = (unsigned long)(c - '0');
                int k;
                for (k = 0; k < 2 && i < n && s[i] >= '0' && s[i] <= '7'; k++, i++)
                    v = v * 8 + (unsigned long)(s[i] - '0');
                buf_addc(b, (int)v);
            } else {
                buf_addc(b, '\\');
                buf_addc(b, c);
            }
        }
    }
}

static int is_str_prefix(const char *s, int n)
{
    int i;
    if (n < 1 || n > 2) return 0;
    for (i = 0; i < n; i++)
        if (!strchr("rRbBuUfF", s[i])) return 0;
    return 1;
}

static void lex_source(const char *src, long len)
{
    long pos = 0;
    int line = 1, depth = 0, bol = 1;
    int stack[100], sp = 0;
    Tok *t;
    ntoks = 0;
    stack[0] = 0;
    while (pos < len) {
        int c;
        if (bol && depth == 0) {
            int col = 0;
            while (pos < len && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\f')) {
                if (src[pos] == ' ') col++;
                else if (src[pos] == '\t') col = (col / 8 + 1) * 8;
                pos++;
            }
            if (pos >= len) break;
            if (src[pos] == '\n' || src[pos] == '\r' || src[pos] == '#') {
                while (pos < len && src[pos] != '\n') pos++;
                if (pos < len) { pos++; line++; }
                continue;
            }
            if (col > stack[sp]) {
                if (sp >= 98) syntax_error(line, "too many indentation levels");
                stack[++sp] = col;
                tok_new(TK_INDENT, line);
            } else {
                while (col < stack[sp]) { sp--; tok_new(TK_DEDENT, line); }
                if (col != stack[sp])
                    syntax_error(line, "unindent does not match any outer indentation level");
            }
            bol = 0;
        }
        c = (unsigned char)src[pos];
        if (c == '\n') {
            pos++;
            if (depth == 0) { tok_new(TK_NEWLINE, line); bol = 1; }
            line++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f') { pos++; continue; }
        if (c == '#') { while (pos < len && src[pos] != '\n') pos++; continue; }
        if (c == '\\' && pos + 1 < len && (src[pos + 1] == '\n' || src[pos + 1] == '\r')) {
            pos++;
            if (src[pos] == '\r') pos++;
            if (pos < len && src[pos] == '\n') pos++;
            line++;
            continue;
        }
        if (ch_alpha[c]) {
            long st = pos;
            while (pos < len && ch_alnum[(unsigned char)src[pos]]) pos++;
            if (pos < len && (src[pos] == '"' || src[pos] == '\'') &&
                is_str_prefix(src + st, (int)(pos - st))) {
                /* string with prefix: fall into string scanning below */
                pos = st;
            } else {
                int k;
                t = tok_new(TK_NAME, line);
                t->s = intern(src + st, pos - st);
                for (k = 1; kw_names[k]; k++)
                    if (strlen(kw_names[k]) == (size_t)(pos - st) &&
                        memcmp(kw_names[k], src + st, (size_t)(pos - st)) == 0) {
                        t->type = TK_KW; t->kw = k; break;
                    }
                continue;
            }
        }
        if (ch_digit[c] || (c == '.' && pos + 1 < len && ch_digit[(unsigned char)src[pos + 1]])) {
            long st = pos;
            int isf = 0;
            char tmp[400];
            long n;
            if (c == '0' && pos + 1 < len && strchr("xXoObB", src[pos + 1])) {
                pos += 2;
                while (pos < len && (ch_hex[(unsigned char)src[pos]] || src[pos] == '_')) pos++;
            } else {
                while (pos < len && (ch_digit[(unsigned char)src[pos]] || src[pos] == '_')) pos++;
                if (pos < len && src[pos] == '.') {
                    isf = 1; pos++;
                    while (pos < len && (ch_digit[(unsigned char)src[pos]] || src[pos] == '_')) pos++;
                }
                if (pos < len && (src[pos] == 'e' || src[pos] == 'E')) {
                    long save = pos;
                    pos++;
                    if (pos < len && (src[pos] == '+' || src[pos] == '-')) pos++;
                    if (pos < len && ch_digit[(unsigned char)src[pos]]) {
                        isf = 1;
                        while (pos < len && ch_digit[(unsigned char)src[pos]]) pos++;
                    } else pos = save;
                }
            }
            n = pos - st;
            if (n > 390) syntax_error(line, "number too long");
            memcpy(tmp, src + st, (size_t)n);
            tmp[n] = 0;
            if (isf) {
                char tmp2[400];
                int k, j = 0;
                for (k = 0; tmp[k]; k++) if (tmp[k] != '_') tmp2[j++] = tmp[k];
                tmp2[j] = 0;
                t = tok_new(TK_FLOAT, line);
                t->fval = strtod(tmp2, NULL);
            } else {
                t = tok_new(TK_NUM, line);
                if (!parse_int(tmp, n, 0, &t->val)) syntax_error(line, "invalid number");
            }
            continue;
        }
        if (c == '"' || c == '\'' || ch_alpha[c]) {
            long st = pos;
            int fl = 0, rw = 0, q, triple = 0, isb = 0;
            long bs, be;
            while (pos < len && src[pos] != '"' && src[pos] != '\'') {
                if (src[pos] == 'f' || src[pos] == 'F') fl = 1;
                if (src[pos] == 'r' || src[pos] == 'R') rw = 1;
                if (src[pos] == 'b' || src[pos] == 'B') isb = 1;
                pos++;
            }
            q = src[pos];
            if (pos + 2 < len && src[pos + 1] == q && src[pos + 2] == q) { triple = 1; pos += 3; }
            else pos++;
            bs = pos;
            for (;;) {
                if (pos >= len) syntax_error(line, "unterminated string literal");
                if (fl && !rw && src[pos] == '{' && pos + 1 < len && src[pos + 1] == '{') { pos += 2; continue; }
                if (fl && src[pos] == '{') {
                    /* replacement field: quotes inside may be nested strings (PEP 701) */
                    int d = 1, pd = 0, inspec = 0;
                    pos++;
                    while (pos < len && d > 0) {
                        int ch = (unsigned char)src[pos];
                        if (!inspec && (ch == '\'' || ch == '"')) {
                            int q2 = ch, tr2 = 0;
                            if (pos + 2 < len && src[pos + 1] == q2 && src[pos + 2] == q2) tr2 = 1;
                            pos += tr2 ? 3 : 1;
                            while (pos < len) {
                                if (src[pos] == '\\') { pos += 2; continue; }
                                if (src[pos] == '\n') line++;
                                if (src[pos] == q2 && (!tr2 || (pos + 2 < len && src[pos + 1] == q2 &&
                                                                 src[pos + 2] == q2))) break;
                                pos++;
                            }
                            pos += tr2 ? 3 : 1;
                            continue;
                        }
                        if (ch == '\n') line++;
                        if (!inspec && (ch == '(' || ch == '[')) pd++;
                        else if (!inspec && (ch == ')' || ch == ']')) pd--;
                        else if (ch == '{') { d++; if (inspec) { /* nested field in a spec */ } }
                        else if (ch == '}') d--;
                        else if (ch == ':' && d == 1 && pd == 0 && !inspec) inspec = 1;
                        pos++;
                    }
                    continue;
                }
                if (src[pos] == '\\' && pos + 1 < len) {
                    if (src[pos + 1] == '\n') line++;
                    pos += 2;
                    continue;
                }
                if (src[pos] == q) {
                    if (!triple) break;
                    if (pos + 2 < len + 0 && src[pos + 1] == q && src[pos + 2] == q) break;
                    if (pos + 2 == len - 0 && 0) break;
                }
                if (src[pos] == '\n') {
                    if (!triple) syntax_error(line, "unterminated string literal");
                    line++;
                }
                pos++;
            }
            be = pos;
            pos += triple ? 3 : 1;
            (void)st;
            t = tok_new(fl ? TK_FSTR : TK_STR, line);
            t->raw = rw;
            t->op = isb;
            if (fl) {
                t->s = str_new(src + bs, be - bs);
            } else if (rw) {
                t->s = str_new(src + bs, be - bs);
            } else {
                Buf b;
                buf_init(&b);
                unescape(src + bs, be - bs, &b);
                t->s = str_new(b.s, b.n);
                buf_free(&b);
            }
            continue;
        }
        /* operators */
        {
            static const char *three[] = { "**=", "//=", ">>=", "<<=", "...", NULL };
            static const int tbop[] = { B_POW, B_FDIV, B_RSH, B_LSH, 0 };
            static const char *two[] = { "==", "!=", "<=", ">=", "**", "//", "<<", ">>", "->",
                "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "@=", ":=", NULL };
            static const int ttype[] = { TK_EQ, TK_NE, TK_LE, TK_GE, TK_POW, TK_FDIV, TK_LSH,
                TK_RSH, TK_ARROW, TK_AUG, TK_AUG, TK_AUG, TK_AUG, TK_AUG, TK_AUG, TK_AUG,
                TK_AUG, TK_AUG, TK_WALRUS };
            static const int tbop2[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, B_ADD, B_SUB, B_MUL, B_DIV,
                B_MOD, B_AND, B_OR, B_XOR, B_MATMUL, 0 };
            int k, found = 0;
            for (k = 0; three[k]; k++)
                if (pos + 3 <= len && memcmp(src + pos, three[k], 3) == 0) {
                    if (k == 4) t = tok_new(TK_ELLIPSIS, line);
                    else { t = tok_new(TK_AUG, line); t->op = tbop[k]; }
                    pos += 3; found = 1; break;
                }
            if (found) continue;
            for (k = 0; two[k]; k++)
                if (pos + 2 <= len && memcmp(src + pos, two[k], 2) == 0) {
                    t = tok_new(ttype[k], line);
                    t->op = tbop2[k];
                    pos += 2; found = 1;
                    break;
                }
            if (found) continue;
        }
        if (strchr("([{", c)) depth++;
        else if (strchr(")]}", c)) { if (depth > 0) depth--; }
        if (!strchr("()[]{}+-*/%&|^~<>=,.:;@!", c)) syntax_error(line, "invalid character");
        tok_new(c, line);
        pos++;
    }
    if (ntoks > 0 && toks[ntoks - 1].type != TK_NEWLINE) tok_new(TK_NEWLINE, line);
    while (sp > 0) { sp--; tok_new(TK_DEDENT, line); }
    tok_new(TK_EOF, line);
}
/* ===== p3_parse.c ===== */
/* =========================================================================
   p3_parse.c   recursive-descent parser: tokens -> AST (with scopes)
   ========================================================================= */
enum {
    N_CONST = 1, N_NAME, N_ATTR, N_SUBSCR, N_SLICE, N_CALL, N_BINOP, N_UNARY, N_AND, N_OR,
    N_NOT, N_CMP, N_IFEXP, N_LIST, N_TUPLE, N_DICT, N_SET, N_LAMBDA, N_COMP, N_FSTR, N_FPART,
    N_STAR, N_DSTAR, N_KW, N_WALRUS,
    S_EXPR, S_ASSIGN, S_AUG, S_IF, S_WHILE, S_FOR, S_BREAK, S_CONTINUE, S_RETURN, S_PASS,
    S_DEF, S_CLASS, S_TRY, S_EXCEPT, S_RAISE, S_ASSERT, S_DEL, S_IMPORT, S_FROM, S_WITH,
    S_BLOCK, S_YIELD, S_YIELDFROM, S_LISTAPP, S_SETADD, S_DICTSET, S_NOP
};
enum { C_LT = 1, C_LE, C_GT, C_GE, C_EQ, C_NE, C_IN, C_NOTIN, C_IS, C_ISNOT };

static int pp;
static Str *cur_gpfx;    /* global name prefix of the module being compiled */
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
    if (at(TK_WALRUS)) {
        Node *w = mk(N_WALRUS);
        Scope *cs = cur_scope;
        if (e->kind != N_NAME) perr("cannot use assignment expressions with expression");
        pp++;
        /* inside a comprehension the target binds in the enclosing function/module */
        while (cs->kind == SC_FUNC && cs->parent && cs->fd &&
               (cs->fd->name == intern_c("<comp>") || cs->fd->name == intern_c("<genexpr>")))
            cs = cs->parent;
        if (cs != cur_scope) {
            Var *ov = scope_var(cs, e->name);
            if (cs->kind == SC_MOD) e->var->isglobal = 1;
            else { ov->defined = 1; e->var->isnonlocal = 1; }
        } else e->var->defined = 1;
        w->a = e;
        w->b = parse_test();
        return w;
    }
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
    while (at('*') || at('/') || at('%') || at(TK_FDIV) || at('@')) {
        int op = at('*') ? B_MUL : (at('/') ? B_DIV : (at('%') ? B_MOD :
                 (at('@') ? B_MATMUL : B_FDIV)));
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
            int isb = TOK.op;
            buf_init(&b);
            while (pp < i) { buf_addn(&b, TOK.s->s, TOK.s->len); pp++; }
            if (isb) {
                int sp = gc_perm;
                Value bv;
                gc_perm = 1;
                bv = mkbytes(b.s, b.n);
                gc_perm = sp;
                n = mk_const(bv, line);
            } else n = mk_const(perm_str(b.s, b.n), line);
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
    if (t == TK_ELLIPSIS) { pp++; return mk_const(Ellipsis_V, line); }
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
        n->pfx = cur_gpfx;
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
        n->pfx = cur_gpfx;
        while (at('.') || at(TK_ELLIPSIS)) { n->level += at('.') ? 1 : 3; pp++; }
        if (at(TK_NAME)) {
            buf_add(&b, TOK.s->s); pp++;
            while (at('.')) {
                pp++;
                buf_addc(&b, '.');
                if (!at(TK_NAME)) perr("expected name");
                buf_add(&b, TOK.s->s); pp++;
            }
            n->name = intern(b.s, b.n);
        } else if (n->level == 0) perr("expected module name");
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

static Node *parse_yield_stmt(int line)
{
    Node *n;
    pp++;
    check_yield_scope();
    if (accept_kw(KW_FROM)) {
        n = node_new(S_YIELDFROM, line);
        n->a = parse_test();
    } else {
        n = node_new(S_YIELD, line);
        if (!at(TK_NEWLINE) && !at(';') && !at(TK_EOF) && !at(TK_DEDENT) && !at(')'))
            n->a = parse_testlist(1);
    }
    n->sidx = cur_scope->nstate++;
    return n;
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
            return parse_yield_stmt(line);
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
                if (atkw(KW_YIELD)) {
                    Node *y;
                    if (as->n != 1) perr("yield in chained assignment is not supported");
                    y = parse_yield_stmt(line);
                    y->b = as->list[0];
                    return y;
                }
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
/* ===== p4_res.c ===== */
/* =========================================================================
   p4_res.c   scope resolution: locals, cells, free variables, globals
   ========================================================================= */
static long *gtab;       /* open addressing hash: global index + 1, 0 = empty */
static long gtabsz;

static void gtab_insert(Str *nm, long idx)
{
    long k = (long)(nm->hash % (unsigned long)gtabsz);
    while (gtab[k]) k = (k + 1) % gtabsz;
    gtab[k] = idx + 1;
}
static long global_find(Str *nm)
{
    long k;
    if (!gtab) return -1;
    k = (long)(nm->hash % (unsigned long)gtabsz);
    while (gtab[k]) {
        if (gnames[gtab[k] - 1] == nm) return gtab[k] - 1;
        k = (k + 1) % gtabsz;
    }
    return -1;
}
static long global_index(Str *nm)
{
    long i;
    if (nm->hash == 0) str_hash(nm);
    i = global_find(nm);
    if (i >= 0) return i;
    if (nglobals >= gcap) {
        gcap = gcap ? gcap * 2 : 256;
        gnames = (Str **)xrealloc(gnames, sizeof(Str *) * (size_t)gcap);
        G = (Value *)xrealloc(G, sizeof(Value) * (size_t)gcap);
    }
    if (!gtab || (nglobals + 1) * 2 > gtabsz) {
        long j;
        free(gtab);
        gtabsz = gtabsz ? gtabsz * 2 : 1024;
        while ((nglobals + 1) * 2 > gtabsz) gtabsz *= 2;
        gtab = (long *)xmalloc(sizeof(long) * (size_t)gtabsz);
        for (j = 0; j < gtabsz; j++) gtab[j] = 0;
        for (j = 0; j < nglobals; j++) gtab_insert(gnames[j], j);
    }
    gnames[nglobals] = nm;
    G[nglobals] = UNSET_V;
    gtab_insert(nm, nglobals);
    return nglobals++;
}

static void set_global(Str *nm, Value v)
{
    long i = global_index(nm);
    G[i] = v;
}

/* the global name `nm` of the module with prefix `pfx` ("a.b."); NULL prefix = builtins */
static Str *prefixed(Str *pfx, Str *nm)
{
    Buf b;
    Str *r;
    if (!pfx) return nm;
    buf_init(&b);
    buf_addn(&b, pfx->s, pfx->len);
    buf_addn(&b, nm->s, nm->len);
    r = intern(b.s, b.n);
    buf_free(&b);
    return r;
}
static void setup_global(Var *v)
{
    v->kind = VK_GLOBAL;
    v->slot = (int)global_index(prefixed(cur_gpfx, v->name));
    v->bslot = cur_gpfx ? (int)global_index(v->name) : -1;
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
        setup_global(v);
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
                setup_global(fb);
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
        setup_global(v);
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
        fd->gpfx = cur_gpfx;
        fd->ncells = nc;
        fd->cellslots = (int *)xmalloc(sizeof(int) * (size_t)(nc + 1));
        for (i = 0; i < s->nvars; i++)
            if (s->vars[i]->kind == VK_CELL) fd->cellslots[k++] = s->vars[i]->slot;
    }
}
/* ===== p6_obj.c ===== */
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
    case T_ND: return cls_ndarray;
    case T_SLICE: return cls_slice;
    case T_DVIEW: return cls_dview[((DView *)v.u.o)->what];
    case T_BYTES: return cls_bytes;
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
    case T_LIST: case T_TUPLE: return AS_LIST(v)->n != 0;
    case T_DICT: case T_SET: return AS_DICT(v)->n != 0;
    case T_RANGE: return range_len((Range *)v.u.o) != 0;
    case T_ND: return nd_truthy(v);
    case T_DVIEW: return AS_DICT(((DView *)v.u.o)->d)->n != 0;
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
    case T_STR: case T_BYTES: return AS_STR(v)->len;
    case T_LIST: case T_TUPLE: return AS_LIST(v)->n;
    case T_DICT: case T_SET: return AS_DICT(v)->n;
    case T_RANGE: return range_len((Range *)v.u.o);
    case T_ND: return nd_len(v);
    case T_DVIEW: return AS_DICT(((DView *)v.u.o)->d)->n;
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
static int num_eq(Value a, Value b)
{
    if (a.t == T_FLOAT || b.t == T_FLOAT) return to_dbl(a) == to_dbl(b);
    return int_cmp(a, b) == 0;
}
static int py_eq(Value a, Value b)
{
    if (is_num(a) && is_num(b)) return num_eq(a, b);
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
    case T_STR: case T_BYTES: return str_hash(AS_STR(v));
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
            double x = to_dbl(a), y = to_dbl(b);
            return x < y ? -1 : (x > y ? 1 : 0);
        }
        return int_cmp(a, b);
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
            double x = to_dbl(a), y = to_dbl(b);
            switch (op) {
            case C_LT: return x < y; case C_LE: return x <= y;
            case C_GT: return x > y; default: return x >= y;
            }
        }
    }
    if ((a.t == T_DVIEW || b.t == T_DVIEW) && (a.t == T_SET || b.t == T_SET || (a.t == T_DVIEW && b.t == T_DVIEW))) {
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
    case T_BYTES: {
        Str *bs = AS_STR(v);
        int q = '\'';
        char tt[8];
        if (memchr(bs->s, '\'', (size_t)bs->len) && !memchr(bs->s, '"', (size_t)bs->len)) q = '"';
        buf_addc(b, 'b');
        buf_addc(b, q);
        for (i = 0; i < bs->len; i++) {
            int c = (unsigned char)bs->s[i];
            if (c == q || c == '\\') { buf_addc(b, '\\'); buf_addc(b, c); }
            else if (c == '\n') buf_add(b, "\\n");
            else if (c == '\t') buf_add(b, "\\t");
            else if (c == '\r') buf_add(b, "\\r");
            else if (c < 32 || c >= 127) { sprintf(tt, "\\x%02x", c); buf_add(b, tt); }
            else buf_addc(b, c);
        }
        buf_addc(b, q);
        break; }
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
        buf_add(b, dv->what == 0 ? "dict_keys([" : (dv->what == 1 ? "dict_values([" : "dict_items(["));
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
            throw_new(cls_TypeError, "Cannot create a consistent method resolution order (MRO) for bases");
        }
        if (nres >= rcap) { rcap *= 2; res = (Class **)xrealloc(res, sizeof(Class *) * (size_t)rcap); }
        res[nres++] = cand;
        for (i = 0; i <= nb; i++)
            if (sq[i].pos < sq[i].n && sq[i].a[sq[i].pos] == cand) sq[i].pos++;
    }
    for (i = 0; i <= nb; i++) free(sq[i].a);
    free(sq);
    c->mro = res;
    c->nmro = nres;
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
        if (nm == S_class) return mkobj(cls_type, T_CLASS);
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
        p = class_find_after(s->obj.t == T_CLASS ? AS_CLASS(s->obj) : class_of(s->obj), s->start, nm);
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
        p = amap_get(&AS_FUNC(o)->attrs, nm);
        if (p) return *p;
        if (nm == S_dict) {
            Dict *d = dict_new();
            int k;
            Value dv = mkobj(d, T_DICT);
            VPUSH(dv);
            for (k = 0; k < AS_FUNC(o)->attrs.n; k++)
                dict_set(d, mkobj(AS_FUNC(o)->attrs.k[k], T_STR), AS_FUNC(o)->attrs.v[k]);
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
        if (nm == intern_c("fget")) return ((Prop *)o.u.o)->get.t == T_UNSET ? NONE_V : ((Prop *)o.u.o)->get;
        if (nm == intern_c("fset")) return ((Prop *)o.u.o)->set.t == T_UNSET ? NONE_V : ((Prop *)o.u.o)->set;
        if (nm == intern_c("fdel")) return ((Prop *)o.u.o)->del.t == T_UNSET ? NONE_V : ((Prop *)o.u.o)->del;
        break; }
    case T_ND: {
        Value r;
        if (nd_getattr(o, nm, &r)) return r;
        break; }
    default: break;
    }
    if (nm == S_class) return mkobj(class_of(o), T_CLASS);
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
        if (p && o.t != T_INST && o.t != T_CLASS && o.t != T_MODULE) return bind_class_attr(o, p, c);
        if (nm == S_name && o.t != T_INST) {
            /* fallthrough to error */
        }
    }
    if (o.t == T_INST && ((nm == intern_c("value") && class_isa(AS_INST(o)->cls, cls_StopIteration)) ||
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
        p = class_find_after(s->obj.t == T_CLASS ? AS_CLASS(s->obj) : class_of(s->obj), s->start, nm);
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
    throw_new(cls_AttributeError, "'%.60s' object has no attribute '%.60s' and no __dict__ for setting new attributes", type_name(o), nm->s);
}
static void del_attr(Value o, Str *nm)
{
    if (o.t == T_INST && (AS_INST(o)->cls->flags & CF_PROPS)) {
        Value *p = class_find(AS_INST(o)->cls, nm);
        if (p && p->t == T_PROP) {
            Prop *pr = (Prop *)p->u.o;
            if (pr->del.t == T_UNSET) throw_new(cls_AttributeError, "can't delete attribute '%.60s'", nm->s);
            call1(pr->del, o);
            return;
        }
    }
    if (o.t == T_INST && amap_del(&AS_INST(o)->a, nm)) return;
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
    case T_ND: return nd_subscr(o, mkslice(lo, hi, st));
    case T_RANGE: {
        Range *r = (Range *)o.u.o;
        long n = range_len(r);
        slice_adjust(lo, hi, st, n, &start, &stop, &step, &cnt);
        return make_range(r->start + start * r->step, r->start + (start + cnt * step) * r->step,
                          r->step * step); }
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
            long i = norm_index(idx.u.i, s->len, "index");
            return mkint((unsigned char)s->s[i]);
        }
        throw_new(cls_TypeError, "byte indices must be integers or slices, not %.40s", type_name(idx));
        break;
    case T_ND: return nd_subscr(o, idx);
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
        throw_new(cls_TypeError, "list indices must be integers or slices, not %.40s", type_name(idx));
        break;
    case T_DICT: dict_set(AS_DICT(o), idx, v); return;
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
    case T_BYTES: it = iter_new(IK_BYTES); it->a = o; return mkobj(it, T_ITER);
    case T_DICT: it = iter_new(IK_DICT); it->a = o; return mkobj(it, T_ITER);
    case T_SET: it = iter_new(IK_SET); it->a = o; return mkobj(it, T_ITER);
    case T_RANGE: {
        Range *r = (Range *)o.u.o;
        it = iter_new(IK_RANGE);
        it->i = r->start; it->n = r->stop; it->step = r->step;
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
    case IK_BYTES: {
        Str *s = AS_STR(t->a);
        if (t->i >= s->len) return 0;
        *out = mkint((unsigned char)s->s[t->i++]);
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
    case T_BYTES: {
        Str *s = AS_STR(c), *u;
        long i;
        if (x.t == T_INT) return memchr(s->s, (int)x.u.i, (size_t)s->len) != NULL;
        if (x.t != T_BYTES) throw_new(cls_TypeError, "a bytes-like object is required, not '%.40s'", type_name(x));
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
    default: break;
    }
    {
        Value it, y;
        if (c.t != T_DVIEW && c.t != T_INST && c.t != T_GEN && c.t != T_ITER && c.t != T_ND)
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
static Value to_set_any(Value v)
{
    Dict *d = dict_new_t(T_SET);
    Value dv = mkobj(d, T_SET), it, y;
    Value *base = vsp;
    VPUSH(dv);
    VPUSH(v);
    it = get_iter(v);
    VPUSH(it);
    while (iter_next(it, &y)) set_add(d, y);
    vsp = base;
    return dv;
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
    if (a.t == T_BYTES && b.t == T_BYTES && op == B_ADD) {
        Str *x = AS_STR(a), *y = AS_STR(b);
        Buf bf;
        Value r;
        buf_init(&bf);
        buf_addn(&bf, x->s, x->len);
        buf_addn(&bf, y->s, y->len);
        r = mkbytes(bf.s, bf.n);
        buf_free(&bf);
        return r;
    }
    if (op == B_MUL && ((a.t == T_BYTES && b.t == T_INT) || (b.t == T_BYTES && a.t == T_INT))) {
        Str *x = AS_STR(a.t == T_BYTES ? a : b);
        long n = a.t == T_BYTES ? b.u.i : a.u.i, i;
        Buf bf;
        Value r;
        buf_init(&bf);
        for (i = 0; i < n; i++) buf_addn(&bf, x->s, x->len);
        r = mkbytes(bf.s, bf.n);
        buf_free(&bf);
        return r;
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
    if (op == B_OR && a.t == T_DICT && b.t == T_DICT) {
        Dict *nd = dict_new();
        Value nv = mkobj(nd, T_DICT);
        VPUSH(nv);
        dict_update_from_hook(nd, a);
        dict_update_from_hook(nd, b);
        vsp--;
        return nv;
    }
    if (a.t == T_SET && b.t == T_SET && (op == B_OR || op == B_AND || op == B_SUB || op == B_XOR))
        return set_op(op, a, b);
    if ((a.t == T_DVIEW || b.t == T_DVIEW) && (op == B_OR || op == B_AND || op == B_SUB || op == B_XOR)) {
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
    if (a.t == T_ND || b.t == T_ND) return nd_binop(op, a, b);
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
static Value inplace_binop(int op, Value a, Value b)
{
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
/* ===== p5_eval.c ===== */
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
/* ===== p7_fmt.c ===== */
/* =========================================================================
   p7_fmt.c   format() mini language, % formatting, str.format
   ========================================================================= */
typedef struct FSpec {
    int fill, align, sign, alt, zero, comma, under;
    long width, prec;
    int type;
} FSpec;

static void parse_spec(const char *s, long n, FSpec *sp)
{
    long i = 0;
    sp->fill = ' '; sp->align = 0; sp->sign = 0; sp->alt = 0; sp->zero = 0;
    sp->comma = 0; sp->under = 0; sp->width = -1; sp->prec = -1; sp->type = 0;
    if (n >= 2 && strchr("<>^=", s[1])) { sp->fill = s[0]; sp->align = s[1]; i = 2; }
    else if (n >= 1 && strchr("<>^=", s[0])) { sp->align = s[0]; i = 1; }
    if (i < n && strchr("+- ", s[i])) { sp->sign = s[i]; i++; }
    if (i < n && s[i] == '#') { sp->alt = 1; i++; }
    if (i < n && s[i] == '0') { sp->zero = 1; i++; }
    if (i < n && ch_digit[(unsigned char)s[i]]) {
        sp->width = 0;
        while (i < n && ch_digit[(unsigned char)s[i]]) sp->width = sp->width * 10 + (s[i++] - '0');
    }
    if (i < n && s[i] == ',') { sp->comma = 1; i++; }
    if (i < n && s[i] == '_') { sp->under = 1; i++; }
    if (i < n && s[i] == '.') {
        i++;
        sp->prec = 0;
        while (i < n && ch_digit[(unsigned char)s[i]]) sp->prec = sp->prec * 10 + (s[i++] - '0');
    }
    if (i < n) sp->type = (unsigned char)s[i++];
    if (i < n) throw_new(cls_ValueError, "Invalid format specifier");
}

/* insert thousands separators in the integer part of digits */
static void add_grouping(Buf *out, const char *d, long n, int sepc)
{
    long i, intlen = 0;
    while (intlen < n && ch_digit[(unsigned char)d[intlen]]) intlen++;
    for (i = 0; i < n; i++) {
        if (i < intlen && i > 0 && (intlen - i) % 3 == 0) buf_addc(out, sepc);
        buf_addc(out, d[i]);
    }
}

static Value pad_result(FSpec *sp, const char *sign, const char *prefix, const char *body,
                        long blen, int defalign)
{
    Buf out;
    long total = (long)strlen(sign) + (long)strlen(prefix) + blen, pad = 0, i;
    int align = sp->align ? sp->align : defalign, fill = sp->fill;
    if (sp->width > total) pad = sp->width - total;
    buf_init(&out);
    if (sp->zero && !sp->align) { align = '='; fill = '0'; }
    if (align == '<') {
        buf_add(&out, sign); buf_add(&out, prefix); buf_addn(&out, body, blen);
        for (i = 0; i < pad; i++) buf_addc(&out, fill);
    } else if (align == '>') {
        for (i = 0; i < pad; i++) buf_addc(&out, fill);
        buf_add(&out, sign); buf_add(&out, prefix); buf_addn(&out, body, blen);
    } else if (align == '^') {
        long l = pad / 2;
        for (i = 0; i < l; i++) buf_addc(&out, fill);
        buf_add(&out, sign); buf_add(&out, prefix); buf_addn(&out, body, blen);
        for (i = 0; i < pad - l; i++) buf_addc(&out, fill);
    } else {
        buf_add(&out, sign); buf_add(&out, prefix);
        for (i = 0; i < pad; i++) buf_addc(&out, fill);
        buf_addn(&out, body, blen);
    }
    return buf_value(&out);
}

static const char *sign_str(FSpec *sp, int neg)
{
    if (neg) return "-";
    if (sp->sign == '+') return "+";
    if (sp->sign == ' ') return " ";
    return "";
}

static Value format_float_spec(double d, FSpec *sp)
{
    char tmp[512], fm[32];
    Buf body;
    int neg = 0, t = sp->type;
    long prec = sp->prec;
    Value r;
    if (d != d) {
        return pad_result(sp, sign_str(sp, 0), "", "nan", 3, '>');
    }
    if (d < 0 || (d == 0 && 1.0 / d < 0)) { neg = 1; d = -d; }
    if (d > DBL_MAX) {
        return pad_result(sp, sign_str(sp, neg), "", t == 'F' || t == 'E' || t == 'G' ? "INF" : "inf", 3, '>');
    }
    if (t == 0) {
        if (prec < 0) { float_repr(d, tmp); }
        else {
            sprintf(fm, "%%.%ldg", prec == 0 ? 1L : prec);
            sprintf(tmp, fm, d);
            if (!strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'n')) strcat(tmp, ".0");
        }
    } else if (t == '%') {
        sprintf(fm, "%%.%ldf", prec < 0 ? 6L : prec);
        sprintf(tmp, fm, d * 100.0);
        strcat(tmp, "%");
    } else {
        int ct = t;
        if (ct == 'F') ct = 'f';
        sprintf(fm, "%%.%ld%c", prec < 0 ? 6L : prec, ct);
        sprintf(tmp, fm, d);
    }
    buf_init(&body);
    if (sp->comma || sp->under) add_grouping(&body, tmp, (long)strlen(tmp), sp->comma ? ',' : '_');
    else buf_add(&body, tmp);
    r = pad_result(sp, sign_str(sp, neg), "", body.s, body.n, '>');
    buf_free(&body);
    return r;
}

static Value format_int_spec(Value v, FSpec *sp)
{
    Buf body, g;
    int neg = int_sign(v) < 0;
    const char *prefix = "";
    Value r, av = neg ? int_neg(v) : v;
    buf_init(&body);
    switch (sp->type) {
    case 'x': case 'X': int_str_base(av, 16, &body); prefix = sp->alt ? "0x" : ""; break;
    case 'o': int_str_base(av, 8, &body); prefix = sp->alt ? "0o" : ""; break;
    case 'b': int_str_base(av, 2, &body); prefix = sp->alt ? "0b" : ""; break;
    default: int_str(av, &body); break;
    }
    if (sp->type == 'X') {
        long i;
        for (i = 0; i < body.n; i++) body.s[i] = (char)to_up[(unsigned char)body.s[i]];
        if (prefix[0]) prefix = "0X";
    }
    if (sp->comma || sp->under) {
        buf_init(&g);
        add_grouping(&g, body.s, body.n, sp->comma ? ',' : '_');
        buf_free(&body);
        body = g;
    }
    r = pad_result(sp, sign_str(sp, neg), prefix, body.s, body.n, '>');
    buf_free(&body);
    return r;
}

static Value format_value(Value v, const char *spec, long speclen)
{
    FSpec sp;
    if (speclen == 0) {
        if (v.t == T_STR) return v;
        return py_str(v);
    }
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_format);
        if (m) return call2(*m, v, mkstr(spec, speclen));
        throw_new(cls_TypeError, "unsupported format string passed to %.60s.__format__", type_name(v));
    }
    parse_spec(spec, speclen, &sp);
    if (v.t == T_STR || (sp.type == 's')) {
        Value s = v.t == T_STR ? v : py_str(v);
        long len = AS_STR(s)->len;
        if (sp.type != 0 && sp.type != 's')
            throw_new(cls_ValueError, "Unknown format code '%c' for object of type 'str'", sp.type);
        if (sp.prec >= 0 && sp.prec < len) len = sp.prec;
        return pad_result(&sp, "", "", AS_STR(s)->s, len, '<');
    }
    if (v.t == T_BOOL && sp.type == 0) return format_value(py_str(v), spec, speclen);
    if (v.t == T_INT || v.t == T_BOOL || v.t == T_BIG) {
        int t = sp.type;
        if (t == 'c') {
            Buf b;
            Value r;
            buf_init(&b);
            put_utf8(&b, (unsigned long)v.u.i);
            r = pad_result(&sp, "", "", b.s, b.n, '<');
            buf_free(&b);
            return r;
        }
        if (t == 0 || t == 'd' || t == 'n' || t == 'x' || t == 'X' || t == 'o' || t == 'b') {
            if (sp.prec >= 0) throw_new(cls_ValueError, "Precision not allowed in integer format specifier");
            return format_int_spec(v, &sp);
        }
        if (strchr("eEfFgG%", t)) return format_float_spec(to_dbl(v), &sp);
        throw_new(cls_ValueError, "Unknown format code '%c' for object of type 'int'", t);
    }
    if (v.t == T_FLOAT) {
        int t = sp.type;
        if (t == 0 || strchr("eEfFgG%", t)) return format_float_spec(v.u.d, &sp);
        throw_new(cls_ValueError, "Unknown format code '%c' for object of type 'float'", t);
    }
    throw_new(cls_TypeError, "unsupported format string passed to %.60s.__format__", type_name(v));
    return NONE_V;
}

/* ---- % formatting ----------------------------------------------------- */
static Value py_format_percent(Value fmt, Value args)
{
    Str *f = AS_STR(fmt);
    Buf out;
    long i = 0, argi = 0;
    List *tup = args.t == T_TUPLE ? AS_LIST(args) : NULL;
    int used_single = 0;
    buf_init(&out);
    while (i < f->len) {
        int c = f->s[i++];
        char spec[64];
        int sl = 0, flags_minus = 0, flags_zero = 0, flags_plus = 0, flags_space = 0, flags_alt = 0;
        long width = -1, prec = -1;
        Value arg, res;
        int t;
        if (c != '%') { buf_addc(&out, c); continue; }
        if (i >= f->len) throw_new(cls_ValueError, "incomplete format");
        arg = NONE_V;
        if (f->s[i] == '(') {
            long st = ++i;
            Value key, d = args;
            while (i < f->len && f->s[i] != ')') i++;
            key = mkstr(f->s + st, i - st);
            i++;
            if (d.t != T_DICT) throw_new(cls_TypeError, "format requires a mapping");
            if (!dict_get(AS_DICT(d), key, &arg)) throw_key(key);
            used_single = 2;
        }
        while (i < f->len && strchr("-+ #0", f->s[i])) {
            switch (f->s[i]) {
            case '-': flags_minus = 1; break;
            case '+': flags_plus = 1; break;
            case ' ': flags_space = 1; break;
            case '#': flags_alt = 1; break;
            default: flags_zero = 1; break;
            }
            i++;
        }
        if (i < f->len && f->s[i] == '*') {
            Value w;
            i++;
            if (!tup || argi >= tup->n) throw_new(cls_TypeError, "not enough arguments for format string");
            w = tup->a[argi++];
            width = need_long(w, "width");
            if (width < 0) { flags_minus = 1; width = -width; }
        } else {
            while (i < f->len && ch_digit[(unsigned char)f->s[i]]) {
                if (width < 0) width = 0;
                width = width * 10 + (f->s[i++] - '0');
            }
        }
        if (i < f->len && f->s[i] == '.') {
            i++;
            prec = 0;
            if (i < f->len && f->s[i] == '*') {
                i++;
                if (!tup || argi >= tup->n) throw_new(cls_TypeError, "not enough arguments for format string");
                prec = need_long(tup->a[argi++], "precision");
            } else {
                while (i < f->len && ch_digit[(unsigned char)f->s[i]]) prec = prec * 10 + (f->s[i++] - '0');
            }
        }
        while (i < f->len && strchr("lhL", f->s[i])) i++;
        if (i >= f->len) throw_new(cls_ValueError, "incomplete format");
        t = (unsigned char)f->s[i++];
        if (t == '%') { buf_addc(&out, '%'); continue; }
        if (used_single != 2) {
            if (tup) {
                if (argi >= tup->n) throw_new(cls_TypeError, "not enough arguments for format string");
                arg = tup->a[argi++];
            } else {
                if (argi >= 1) throw_new(cls_TypeError, "not enough arguments for format string");
                arg = args;
                argi++;
                used_single = 1;
            }
        }
        /* build a format spec for format_value */
        if (flags_minus) spec[sl++] = '<';
        else if (strchr("sr", t) || t == 'a') spec[sl++] = '>';
        if (flags_plus) spec[sl++] = '+';
        else if (flags_space) spec[sl++] = ' ';
        if (flags_alt) spec[sl++] = '#';
        if (flags_zero && !flags_minus && !strchr("sra", t)) spec[sl++] = '0';
        if (width >= 0) sl += sprintf(spec + sl, "%ld", width);
        if (prec >= 0) sl += sprintf(spec + sl, ".%ld", prec);
        if (t == 'r' || t == 'a') { arg = py_repr(arg); t = 's'; }
        else if (t == 's') { arg = py_str(arg); }
        else if (t == 'i' || t == 'u') t = 'd';
        if (t == 'd' || t == 'x' || t == 'X' || t == 'o') {
            if (arg.t == T_FLOAT) arg = int_from_double(arg.u.d < 0 ? ceil(arg.u.d) : floor(arg.u.d));
            else if (!is_intv(arg))
                throw_new(cls_TypeError, "%%%c format: a real number is required, not %.40s", t, type_name(arg));
        } else if (strchr("eEfFgG", t)) {
            if (!is_num(arg))
                throw_new(cls_TypeError, "must be real number, not %.40s", type_name(arg));
        } else if (t == 'c') {
            if (arg.t == T_STR) { buf_addn(&out, AS_STR(arg)->s, AS_STR(arg)->len); continue; }
        } else if (t != 's') {
            throw_new(cls_ValueError, "unsupported format character '%c'", t);
        }
        spec[sl++] = (char)t;
        spec[sl] = 0;
        if (t == 'd' && arg.t == T_BOOL) arg = mkint(arg.u.i);
        res = format_value(arg, spec, sl);
        buf_addn(&out, AS_STR(res)->s, AS_STR(res)->len);
    }
    if (tup && argi < tup->n && used_single != 2)
        throw_new(cls_TypeError, "not all arguments converted during string formatting");
    if (!tup && used_single == 0 && argi == 0 && args.t != T_DICT)
        throw_new(cls_TypeError, "not all arguments converted during string formatting");
    return buf_value(&out);
}

/* ---- str.format ------------------------------------------------------- */
static Value format_template(Str *t, int nargs, Value *args, int nk, Value *kw, int depth)
{
    Buf out;
    long i = 0;
    int auto_idx = 0;
    buf_init(&out);
    if (depth > 3) throw_new(cls_ValueError, "Max string recursion exceeded");
    while (i < t->len) {
        int c = t->s[i];
        if (c == '{' && i + 1 < t->len && t->s[i + 1] == '{') { buf_addc(&out, '{'); i += 2; continue; }
        if (c == '}' && i + 1 < t->len && t->s[i + 1] == '}') { buf_addc(&out, '}'); i += 2; continue; }
        if (c == '}') throw_new(cls_ValueError, "Single '}' encountered in format string");
        if (c == '{') {
            long st = i + 1, j = st, fend, send = -1, sst = -1;
            int depthb = 0, conv = 0, k;
            Value v, sv;
            char name[200];
            long nl;
            for (; j < t->len; j++) {
                if (t->s[j] == '{') depthb++;
                else if (t->s[j] == '}') { if (depthb == 0) break; depthb--; }
            }
            if (j >= t->len) throw_new(cls_ValueError, "expected '}' before end of string");
            send = j;
            fend = st;
            while (fend < send && t->s[fend] != '!' && t->s[fend] != ':') fend++;
            nl = fend - st;
            if (nl > 190) nl = 190;
            memcpy(name, t->s + st, (size_t)nl);
            name[nl] = 0;
            if (fend < send && t->s[fend] == '!') { conv = t->s[fend + 1]; fend += 2; }
            if (fend < send && t->s[fend] == ':') sst = fend + 1;
            /* resolve field */
            {
                char *p = name;
                char first[200];
                int fl = 0;
                while (*p && *p != '.' && *p != '[') first[fl++] = *p++;
                first[fl] = 0;
                if (fl == 0) {
                    if (auto_idx >= nargs) throw_new(cls_IndexError, "Replacement index %d out of range for positional args tuple", auto_idx);
                    v = args[auto_idx++];
                } else if (ch_digit[(unsigned char)first[0]]) {
                    int ix = atoi(first);
                    if (ix >= nargs) throw_new(cls_IndexError, "Replacement index %d out of range for positional args tuple", ix);
                    v = args[ix];
                } else {
                    int found = 0;
                    v = NONE_V;
                    for (k = 0; k < nk; k++)
                        if (strcmp(AS_STR(kw[2 * k])->s, first) == 0) { v = kw[2 * k + 1]; found = 1; }
                    if (!found) throw_key(mkcstr(first));
                }
                while (*p) {
                    if (*p == '.') {
                        char an[100];
                        int al = 0;
                        p++;
                        while (*p && *p != '.' && *p != '[' && al < 99) an[al++] = *p++;
                        an[al] = 0;
                        v = get_attr(v, intern_c(an));
                    } else if (*p == '[') {
                        char in[100];
                        int il = 0;
                        p++;
                        while (*p && *p != ']' && il < 99) in[il++] = *p++;
                        in[il] = 0;
                        if (*p == ']') p++;
                        if (ch_digit[(unsigned char)in[0]]) v = subscr(v, mkint(atol(in)));
                        else v = subscr(v, mkcstr(in));
                    } else break;
                }
            }
            if (conv == 'r' || conv == 'a') v = py_repr(v);
            else if (conv == 's') v = py_str(v);
            if (sst >= 0) {
                Value spec = mkstr(t->s + sst, send - sst);
                if (memchr(t->s + sst, '{', (size_t)(send - sst)))
                    spec = format_template(AS_STR(spec), nargs, args, nk, kw, depth + 1);
                sv = format_value(v, AS_STR(spec)->s, AS_STR(spec)->len);
            } else sv = format_value(v, "", 0);
            buf_addn(&out, AS_STR(sv)->s, AS_STR(sv)->len);
            i = send + 1;
            continue;
        }
        buf_addc(&out, c);
        i++;
    }
    return buf_value(&out);
}
/* ===== p7_bi.c ===== */
/* =========================================================================
   p7_bi.c   builtin functions and methods of the builtin types
   ========================================================================= */
#define BI(name) static Value name(Args *x)
#define A(i) (x->a[i])


static Value kwget(Args *x, const char *nm)
{
    int i;
    for (i = 0; i < x->nk; i++)
        if (strcmp(AS_STR(x->kw[2 * i])->s, nm) == 0) return x->kw[2 * i + 1];
    return UNSET_V;
}
static void need(Args *x, int lo, int hi, const char *fname)
{
    if (x->n < lo || x->n > hi) {
        if (lo == hi)
            throw_new(cls_TypeError, "%s() takes exactly %d argument%s (%d given)", fname, lo,
                      lo == 1 ? "" : "s", x->n);
        if (x->n < lo)
            throw_new(cls_TypeError, "%s() takes at least %d argument%s (%d given)", fname, lo,
                      lo == 1 ? "" : "s", x->n);
        throw_new(cls_TypeError, "%s() takes at most %d argument%s (%d given)", fname, hi,
                  hi == 1 ? "" : "s", x->n);
    }
}
static Str *astr(Value v, const char *what)
{
    if (v.t == T_INST && AS_INST(v)->pay.t == T_STR) v = AS_INST(v)->pay;
    if (v.t != T_STR)
        throw_new(cls_TypeError, "%s must be str, not %.40s", what, type_name(v));
    return AS_STR(v);
}
static Value mkbuiltin(const char *name, BFn fn)
{
    Builtin *b = (Builtin *)gc_alloc(sizeof(Builtin), T_BUILTIN);
    b->fn = fn;
    b->name = name;
    b->sname = intern_c(name);
    b->selfpay = 0;
    return mkobj(b, T_BUILTIN);
}
static void def_bi(Class *c, const char *name, BFn fn)
{
    Value b = mkbuiltin(name, fn);
    if (c && (c == cls_str || c == cls_list || c == cls_tuple || c == cls_dict || c == cls_set))
        ((Builtin *)b.u.o)->selfpay = 1;
    if (c) amap_set(&c->ns, intern_c(name), b);
    else set_global(intern_c(name), b);
}
static Class *new_class(const char *name, Class *base, int flags)
{
    Class *c = (Class *)gc_alloc(sizeof(Class), T_CLASS);
    c->name = intern_c(name);
    c->base = base;
    c->ns.k = NULL; c->ns.v = NULL; c->ns.n = c->ns.cap = 0;
    c->ctor = NULL;
    c->mro = NULL; c->nmro = 0; c->bases = NULL; c->nbases = 0; c->paycls = NULL;
    c->modname = NULL;
    c->flags = flags | (base ? (base->flags & CF_EXC) : 0);
    set_global(c->name, mkobj(c, T_CLASS));
    return c;
}
static long to_index(Value v)
{
    return need_long(v, "argument");
}

/* ---- output ----------------------------------------------------------- */
static int try_attr(Value o, Str *nm, Value *out);
static Module *sys_mod;
static Value print_target(Value filev)
{
    if (filev.t != T_UNSET && filev.t != T_NONE) return filev;
    if (sys_mod) {
        Value *p = amap_get(&sys_mod->ns, intern_c("stdout"));
        if (p) return *p;
    }
    return UNSET_V;
}
static void print_write(Value target, const char *s, long n)
{
    Value m, str;
    if (n == 0) return;
    m = get_attr(target, intern_c("write"));
    VPUSH(m);
    str = mkstr(s, n);
    VPUSH(str);
    call1(m, str);
    vsp -= 2;
}
BI(bi_print)
{
    Value sepv = kwget(x, "sep"), endv = kwget(x, "end"), filev = kwget(x, "file");
    Value flv = kwget(x, "flush");
    FILE *fp = stdout;
    const char *sep = " ", *end = "\n";
    long sepn = 1, endn = 1;
    int i;
    Value target = print_target(filev);
    if (sepv.t == T_STR) { sep = AS_STR(sepv)->s; sepn = AS_STR(sepv)->len; }
    if (endv.t == T_STR) { end = AS_STR(endv)->s; endn = AS_STR(endv)->len; }
    if (target.t == T_FILE) fp = ((File *)target.u.o)->fp;
    else if (target.t != T_UNSET) {
        for (i = 0; i < x->n; i++) {
            Value sv = py_str(A(i));
            VPUSH(sv);
            if (i) print_write(target, sep, sepn);
            print_write(target, AS_STR(sv)->s, AS_STR(sv)->len);
            vsp--;
        }
        print_write(target, end, endn);
        if (flv.t != T_UNSET && py_truthy(flv)) {
            Value fm;
            if (try_attr(target, intern_c("flush"), &fm)) call0(fm);
        }
        return NONE_V;
    }
    if (fp == stderr) fflush(stdout);
    for (i = 0; i < x->n; i++) {
        Buf b;
        if (i) fwrite(sep, 1, (size_t)sepn, fp);
        if (A(i).t == T_STR) {
            fwrite(AS_STR(A(i))->s, 1, (size_t)AS_STR(A(i))->len, fp);
        } else {
            buf_init(&b);
            str_buf(A(i), &b);
            fwrite(b.s, 1, (size_t)b.n, fp);
            buf_free(&b);
        }
    }
    fwrite(end, 1, (size_t)endn, fp);
    if (flv.t != T_UNSET && py_truthy(flv)) fflush(fp);
    return NONE_V;
}
BI(bi_input)
{
    Buf b;
    int c;
    if (x->n > 0) {
        Value s = py_str(A(0));
        fwrite(AS_STR(s)->s, 1, (size_t)AS_STR(s)->len, stdout);
    }
    fflush(stdout);
    buf_init(&b);
    c = getc(stdin);
    if (c == EOF) { buf_free(&b); throw_new(cls_EOFError, "EOF when reading a line"); }
    while (c != EOF && c != '\n') { buf_addc(&b, c); c = getc(stdin); }
    if (b.n > 0 && b.s[b.n - 1] == '\r') b.n--;
    return buf_value(&b);
}

/* ---- numbers and conversions ------------------------------------------ */
BI(bi_len)
{
    need(x, 1, 1, "len");
    return mkint(py_len(A(0)));
}
BI(bi_repr)
{
    need(x, 1, 1, "repr");
    return py_repr(A(0));
}
BI(bi_str)
{
    if (x->n == 0) return mkstr("", 0);
    return py_str(A(0));
}
BI(bi_bool)
{
    if (x->n == 0) return FALSE_V;
    return mkbool(py_truthy(A(0)));
}
BI(bi_int)
{
    Value v, bv = kwget(x, "base");
    int base = 10;
    if (x->n == 0) return mkint(0);
    v = A(0);
    if (x->n > 1) bv = A(1);
    if (bv.t != T_UNSET) base = (int)to_index(bv);
    if (v.t == T_STR) {
        Value r;
        if (!parse_int(AS_STR(v)->s, AS_STR(v)->len, bv.t == T_UNSET ? 10 : base, &r)) {
            Value q = py_repr(v);
            throw_new(cls_ValueError, "invalid literal for int() with base %d: %.200s", base, AS_STR(q)->s);
        }
        return r;
    }
    if (bv.t != T_UNSET) throw_new(cls_TypeError, "int() can't convert non-string with explicit base");
    if (v.t == T_INT || v.t == T_BIG) return v;
    if (v.t == T_BOOL) return mkint(v.u.i);
    if (v.t == T_FLOAT) return int_from_double(v.u.d < 0 ? ceil(v.u.d) : floor(v.u.d));
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_int);
        if (m) return call1(*m, v);
        m = class_find(AS_INST(v)->cls, S_index);
        if (m) return call1(*m, v);
    }
    throw_new(cls_TypeError, "int() argument must be a string, a bytes-like object or a real number, not '%.40s'", type_name(v));
    return NONE_V;
}
BI(bi_float)
{
    Value v;
    if (x->n == 0) return mkfloat(0.0);
    v = A(0);
    if (v.t == T_FLOAT) return v;
    if (is_intv(v)) return mkfloat(int_todouble(v));
    if (v.t == T_STR) {
        double d;
        if (!parse_float(AS_STR(v)->s, AS_STR(v)->len, &d)) {
            Value q = py_repr(v);
            throw_new(cls_ValueError, "could not convert string to float: %.200s", AS_STR(q)->s);
        }
        return mkfloat(d);
    }
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_float);
        if (m) return call1(*m, v);
    }
    throw_new(cls_TypeError, "float() argument must be a string or a real number, not '%.40s'", type_name(v));
    return NONE_V;
}
BI(bi_abs)
{
    Value v;
    need(x, 1, 1, "abs");
    v = A(0);
    if (v.t == T_INT || v.t == T_BOOL) return mkint(v.u.i < 0 ? -v.u.i : v.u.i);
    if (v.t == T_FLOAT) return mkfloat(fabs(v.u.d));
    if (v.t == T_BIG) return int_sign(v) < 0 ? int_neg(v) : v;
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, S_abs);
        if (m) return call1(*m, v);
    }
    throw_new(cls_TypeError, "bad operand type for abs(): '%.40s'", type_name(v));
    return NONE_V;
}
BI(bi_round)
{
    Value v, nd;
    need(x, 1, 2, "round");
    v = A(0);
    nd = x->n > 1 ? A(1) : kwget(x, "ndigits");
    if (is_intv(v)) {
        if (nd.t == T_UNSET || nd.t == T_NONE || to_index(nd) >= 0) return v.t == T_BOOL ? mkint(v.u.i) : v;
        {
            Value p = int_pow(mkint(10), -to_index(nd)), q, r, h;
            int_divmod(v, p, &q, &r);
            h = int_mul(r, mkint(2));
            if (int_cmp(h, p) > 0 || (int_cmp(h, p) == 0 && (q.t != T_BIG ? (q.u.i & 1) : 0)))
                q = int_add(q, mkint(1));
            return int_mul(q, p);
        }
    }
    if (v.t == T_FLOAT) {
        double d = v.u.d;
        if (nd.t == T_UNSET || nd.t == T_NONE) {
            double r;
            if (d != d) throw_new(cls_ValueError, "cannot convert float NaN to integer");
            if (d > DBL_MAX || d < -DBL_MAX) throw_new(cls_OverflowError, "cannot convert float infinity to integer");
            r = floor(d);
            if (d - r > 0.5) r += 1.0;
            else if (d - r == 0.5 && fmod(r, 2.0) != 0) r += 1.0;
            return int_from_double(r);
        } else {
            long k = to_index(nd);
            char tmp[400];
            if (d != d || d > DBL_MAX || d < -DBL_MAX) return v;
            if (k > 300) return v;
            if (k >= 0) {
                sprintf(tmp, "%.*f", (int)k, d);
                return mkfloat(strtod(tmp, NULL));
            } else {
                double p = pow(10.0, (double)-k), q = d / p, r = floor(q);
                if (q - r > 0.5) r += 1.0;
                else if (q - r == 0.5 && fmod(r, 2.0) != 0) r += 1.0;
                return mkfloat(r * p);
            }
        }
    }
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, intern_c("__round__"));
        if (m) return x->n > 1 ? call2(*m, v, A(1)) : call1(*m, v);
    }
    throw_new(cls_TypeError, "type %.40s doesn't define __round__ method", type_name(v));
    return NONE_V;
}
BI(bi_divmod)
{
    Value q, r;
    need(x, 2, 2, "divmod");
    if (is_intv(A(0)) && is_intv(A(1))) {
        int_divmod(A(0), A(1), &q, &r);
        return mktuple2(q, r);
    }
    if (is_num(A(0)) && is_num(A(1))) {
        double a = to_dbl(A(0)), b = to_dbl(A(1));
        if (b == 0) throw_new(cls_ZeroDivisionError, "float divmod()");
        return mktuple2(mkfloat(fl_floordiv(a, b)), mkfloat(fl_mod(a, b)));
    }
    if (A(0).t == T_INST) {
        Value *m = class_find(AS_INST(A(0))->cls, intern_c("__divmod__"));
        if (m) {
            q = call2(*m, A(0), A(1));
            if (!IS_NOTIMPL(q)) return q;
        }
    }
    if (A(1).t == T_INST) {
        Value *m = class_find(AS_INST(A(1))->cls, intern_c("__rdivmod__"));
        if (m) {
            q = call2(*m, A(1), A(0));
            if (!IS_NOTIMPL(q)) return q;
        }
    }
    throw_new(cls_TypeError, "unsupported operand type(s) for divmod(): '%.40s' and '%.40s'",
              type_name(A(0)), type_name(A(1)));
    return NONE_V;
}
BI(bi_pow)
{
    need(x, 2, 3, "pow");
    if (x->n == 3 && A(2).t != T_NONE) {
        Value r = mkint(1), b = int_mod(A(0), A(2)), e = A(1);
        long ee;
        if (!is_intv(A(0)) || !is_intv(e) || !is_intv(A(2)))
            throw_new(cls_TypeError, "pow() 3rd argument not allowed unless all arguments are integers");
        if (int_sign(e) < 0) throw_new(cls_ValueError, "pow() 2nd argument cannot be negative when 3rd argument specified");
        if (e.t == T_BIG) throw_new(cls_OverflowError, "exponent too large");
        ee = e.u.i;
        while (ee > 0) {
            if (ee & 1) r = int_mod(int_mul(r, b), A(2));
            ee >>= 1;
            if (ee) b = int_mod(int_mul(b, b), A(2));
        }
        return int_mod(r, A(2));
    }
    return binop(B_POW, A(0), A(1));
}
BI(bi_chr)
{
    Buf b;
    long c;
    need(x, 1, 1, "chr");
    c = to_index(A(0));
    if (c < 0 || c > 0x10ffffL) throw_new(cls_ValueError, "chr() arg not in range(0x110000)");
    buf_init(&b);
    put_utf8(&b, (unsigned long)c);
    return buf_value(&b);
}
BI(bi_ord)
{
    Str *s;
    unsigned long c;
    need(x, 1, 1, "ord");
    if (A(0).t == T_BYTES && AS_STR(A(0))->len == 1) return mkint((unsigned char)AS_STR(A(0))->s[0]);
    s = astr(A(0), "ord() argument");
    if (s->len == 1) return mkint((unsigned char)s->s[0]);
    c = (unsigned char)s->s[0];
    if (s->len == 2 && (c & 0xE0) == 0xC0)
        return mkint((long)(((c & 0x1F) << 6) | ((unsigned char)s->s[1] & 0x3F)));
    if (s->len == 3 && (c & 0xF0) == 0xE0)
        return mkint((long)(((c & 0x0F) << 12) | (((unsigned char)s->s[1] & 0x3F) << 6) |
                            ((unsigned char)s->s[2] & 0x3F)));
    if (s->len == 4 && (c & 0xF8) == 0xF0)
        return mkint((long)(((c & 0x07) << 18) | (((unsigned char)s->s[1] & 0x3F) << 12) |
                            (((unsigned char)s->s[2] & 0x3F) << 6) | ((unsigned char)s->s[3] & 0x3F)));
    throw_new(cls_TypeError, "ord() expected a character, but string of length %ld found", s->len);
    return NONE_V;
}
static Value radix_str(Value v, int base, const char *prefix)
{
    Buf b;
    if (!is_intv(v)) throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer", type_name(v));
    if (v.t == T_BOOL) v = mkint(v.u.i);
    buf_init(&b);
    if (int_sign(v) < 0) { buf_addc(&b, '-'); v = int_neg(v); }
    buf_add(&b, prefix);
    int_str_base(v, base, &b);
    return buf_value(&b);
}
BI(bi_hex) { need(x, 1, 1, "hex"); return radix_str(A(0), 16, "0x"); }
BI(bi_oct) { need(x, 1, 1, "oct"); return radix_str(A(0), 8, "0o"); }
BI(bi_bin) { need(x, 1, 1, "bin"); return radix_str(A(0), 2, "0b"); }

/* ---- collections ------------------------------------------------------ */
BI(bi_list)
{
    if (x->n == 0) return mklist(list_new(4));
    return iter_to_list(A(0));
}
BI(bi_tuple)
{
    Value l, t;
    if (x->n == 0) return mktuple_from(NULL, 0);
    if (A(0).t == T_TUPLE) return A(0);
    l = iter_to_list(A(0));
    VPUSH(l);
    t = mktuple_from(AS_LIST(l)->a, AS_LIST(l)->n);
    vsp--;
    return t;
}
static void dict_update_from(Dict *d, Value src)
{
    src = unpay(src);
    if (src.t == T_DICT) {
        Dict *s = AS_DICT(src);
        long i;
        for (i = 0; i < s->used; i++)
            if (s->e[i].k.t != T_UNSET) dict_set(d, s->e[i].k, s->e[i].v);
    } else if (src.t == T_INST && class_find(AS_INST(src)->cls, intern_c("keys"))) {
        Value *base = vsp;
        Value ks, it, k;
        VPUSH(src);
        ks = call0(get_attr(src, intern_c("keys")));
        VPUSH(ks);
        it = get_iter(ks);
        VPUSH(it);
        while (iter_next(it, &k)) {
            VPUSH(k);
            dict_set(d, k, subscr(src, k));
            vsp--;
        }
        vsp = base;
    } else {
        Value it, item;
        Value *base = vsp;
        VPUSH(src);
        it = get_iter(src);
        VPUSH(it);
        while (iter_next(it, &item)) {
            Value pair;
            VPUSH(item);
            pair = item;
            if (pair.t != T_LIST && pair.t != T_TUPLE) pair = iter_to_list(item);
            VPUSH(pair);
            if (AS_LIST(pair)->n != 2)
                throw_new(cls_ValueError, "dictionary update sequence element has length %ld; 2 is required", AS_LIST(pair)->n);
            dict_set(d, AS_LIST(pair)->a[0], AS_LIST(pair)->a[1]);
            vsp -= 2;
        }
        vsp = base;
    }
}
BI(bi_dict)
{
    Dict *d = dict_new();
    Value dv = mkobj(d, T_DICT);
    int i;
    VPUSH(dv);
    if (x->n > 0) dict_update_from(d, A(0));
    for (i = 0; i < x->nk; i++) dict_set(d, x->kw[2 * i], x->kw[2 * i + 1]);
    vsp--;
    return dv;
}
BI(bi_set)
{
    Dict *d = dict_new_t(T_SET);
    Value dv = mkobj(d, T_SET), it, y;
    Value *base = vsp;
    VPUSH(dv);
    if (x->n > 0) {
        VPUSH(A(0));
        it = get_iter(A(0));
        VPUSH(it);
        while (iter_next(it, &y)) set_add(d, y);
    }
    vsp = base;
    return dv;
}
BI(bi_range)
{
    long a = 0, b, c = 1;
    need(x, 1, 3, "range");
    if (x->n == 1) b = to_index(A(0));
    else {
        a = to_index(A(0)); b = to_index(A(1));
        if (x->n == 3) c = to_index(A(2));
    }
    if (c == 0) throw_new(cls_ValueError, "range() arg 3 must not be zero");
    return make_range(a, b, c);
}
BI(bi_enumerate)
{
    Iter *it;
    Value iv;
    long start = 0;
    Value sv = kwget(x, "start");
    need(x, 1, 2, "enumerate");
    if (x->n > 1) sv = A(1);
    if (sv.t != T_UNSET) start = to_index(sv);
    iv = get_iter(A(0));
    VPUSH(iv);
    it = iter_new(IK_ENUM);
    it->a = iv;
    it->i = start;
    vsp--;
    return mkobj(it, T_ITER);
}
BI(bi_zip)
{
    List *its = list_new(x->n);
    Value lv = mklist(its);
    Iter *it;
    int i;
    VPUSH(lv);
    for (i = 0; i < x->n; i++) list_push(its, get_iter(A(i)));
    it = iter_new(IK_ZIP);
    it->a = lv;
    vsp--;
    return mkobj(it, T_ITER);
}
BI(bi_map)
{
    List *its = list_new(x->n);
    Value lv = mklist(its);
    Iter *it;
    int i;
    if (x->n < 2) throw_new(cls_TypeError, "map() must have at least two arguments.");
    VPUSH(lv);
    for (i = 1; i < x->n; i++) list_push(its, get_iter(A(i)));
    it = iter_new(IK_MAP);
    it->a = A(0);
    it->b = lv;
    vsp--;
    return mkobj(it, T_ITER);
}
BI(bi_filter)
{
    Iter *it;
    Value iv;
    need(x, 2, 2, "filter");
    iv = get_iter(A(1));
    VPUSH(iv);
    it = iter_new(IK_FILTER);
    it->a = A(0);
    it->b = iv;
    vsp--;
    return mkobj(it, T_ITER);
}
BI(bi_reversed)
{
    Iter *it;
    Value v;
    need(x, 1, 1, "reversed");
    v = A(0);
    if (v.t == T_DICT || v.t == T_DVIEW) v = iter_to_list(v);
    if (v.t == T_LIST || v.t == T_TUPLE || v.t == T_STR || v.t == T_RANGE) {
        it = iter_new(IK_REV);
        it->a = v;
        it->i = py_len(v) - 1;
        return mkobj(it, T_ITER);
    }
    if (v.t == T_INST) {
        Value *m = class_find(AS_INST(v)->cls, intern_c("__reversed__"));
        if (m) return call1(*m, v);
        v = iter_to_list(v);
        it = iter_new(IK_REV);
        it->a = v;
        it->i = AS_LIST(v)->n - 1;
        return mkobj(it, T_ITER);
    }
    throw_new(cls_TypeError, "'%.40s' object is not reversible", type_name(v));
    return NONE_V;
}
BI(bi_iter)
{
    need(x, 1, 2, "iter");
    if (x->n == 2) {
        Iter *it = iter_new(IK_CALLSENT);
        it->a = A(0);
        it->b = A(1);
        return mkobj(it, T_ITER);
    }
    return get_iter(A(0));
}
BI(bi_next)
{
    Value out;
    need(x, 1, 2, "next");
    if (iter_next(A(0), &out)) return out;
    if (x->n > 1) return A(1);
    throw_val(new_exc_args(cls_StopIteration, NULL, 0));
    return NONE_V;
}
BI(bi_any)
{
    Value it, y;
    need(x, 1, 1, "any");
    it = get_iter(A(0));
    VPUSH(it);
    while (iter_next(it, &y)) if (py_truthy(y)) { vsp--; return TRUE_V; }
    vsp--;
    return FALSE_V;
}
BI(bi_all)
{
    Value it, y;
    need(x, 1, 1, "all");
    it = get_iter(A(0));
    VPUSH(it);
    while (iter_next(it, &y)) if (!py_truthy(y)) { vsp--; return FALSE_V; }
    vsp--;
    return TRUE_V;
}
BI(bi_sum)
{
    Value acc = mkint(0), it, y, sv = kwget(x, "start");
    double comp = 0.0;
    need(x, 1, 2, "sum");
    if (x->n > 1) sv = A(1);
    if (sv.t != T_UNSET) acc = sv;
    it = get_iter(A(0));
    VPUSH(it);
    VPUSH(acc);
    while (iter_next(it, &y)) {
        if ((acc.t == T_FLOAT || acc.t == T_INT) &&
            (y.t == T_FLOAT || ((y.t == T_INT || y.t == T_BOOL) && acc.t == T_FLOAT) ||
             (y.t == T_FLOAT && acc.t == T_INT))) {
            double a = acc.t == T_FLOAT ? acc.u.d : (double)acc.u.i;
            double v = y.t == T_FLOAT ? y.u.d : (double)y.u.i;
            double t = a + v;
            if (fabs(a) >= fabs(v)) comp += (a - t) + v;
            else comp += (v - t) + a;
            acc = mkfloat(t);
        } else {
            if (acc.t == T_FLOAT && comp != 0) { acc = mkfloat(acc.u.d + comp); }
            comp = 0.0;
            acc = binop(B_ADD, acc, y);
        }
        vsp[-1] = acc;
    }
    if (acc.t == T_FLOAT && comp != 0 && comp == comp && comp <= DBL_MAX && comp >= -DBL_MAX)
        acc = mkfloat(acc.u.d + comp);
    vsp -= 2;
    return acc;
}
static Value minmax(Args *x, int ismax)
{
    Value key = kwget(x, "key"), dflt = kwget(x, "default"), best = UNSET_V, bestk = UNSET_V;
    Value it, y, k;
    const char *nm = ismax ? "max" : "min";
    Value *base = vsp;
    if (x->n == 0) throw_new(cls_TypeError, "%s expected at least 1 argument, got 0", nm);
    if (x->n == 1) it = get_iter(A(0));
    else it = get_iter(mktuple_from(x->a, x->n));
    VPUSH(it);
    VPUSH(best);
    VPUSH(bestk);
    while (iter_next(it, &y)) {
        VPUSH(y);
        k = (key.t == T_UNSET || key.t == T_NONE) ? y : call1(key, y);
        vsp--;
        if (best.t == T_UNSET || (ismax ? rich_cmp(C_GT, k, bestk) : rich_cmp(C_LT, k, bestk))) {
            best = y; bestk = k;
            base[1] = best; base[2] = bestk;
        }
    }
    vsp = base;
    if (best.t == T_UNSET) {
        if (dflt.t != T_UNSET) return dflt;
        throw_new(cls_ValueError, "%s() iterable argument is empty", nm);
    }
    return best;
}
BI(bi_min) { return minmax(x, 0); }
BI(bi_max) { return minmax(x, 1); }

/* sorting */
static int sort_lt(Value a, Value b)
{
    if (a.t == T_INT && b.t == T_INT) return a.u.i < b.u.i;
    if (a.t == T_STR && b.t == T_STR) {
        Str *p = AS_STR(a), *q = AS_STR(b);
        long n = p->len < q->len ? p->len : q->len;
        int c = memcmp(p->s, q->s, (size_t)n);
        if (c) return c < 0;
        return p->len < q->len;
    }
    if (a.t == T_FLOAT && b.t == T_FLOAT) return a.u.d < b.u.d;
    return rich_cmp(C_LT, a, b);
}
static void msort(Value *k, Value *v, Value *tk, Value *tv, long lo, long hi)
{
    long mid, i, j, p;
    if (hi - lo < 2) return;
    if (hi - lo <= 6) {
        for (i = lo + 1; i < hi; i++) {
            Value kk = k[i], vv = v[i];
            j = i - 1;
            while (j >= lo && sort_lt(kk, k[j])) { k[j + 1] = k[j]; v[j + 1] = v[j]; j--; }
            k[j + 1] = kk; v[j + 1] = vv;
        }
        return;
    }
    mid = (lo + hi) / 2;
    msort(k, v, tk, tv, lo, mid);
    msort(k, v, tk, tv, mid, hi);
    if (!sort_lt(k[mid], k[mid - 1])) return;
    for (i = lo; i < mid; i++) { tk[i] = k[i]; tv[i] = v[i]; }
    i = lo; j = mid; p = lo;
    while (i < mid && j < hi) {
        if (sort_lt(k[j], tk[i])) { k[p] = k[j]; v[p] = v[j]; j++; }
        else { k[p] = tk[i]; v[p] = tv[i]; i++; }
        p++;
    }
    while (i < mid) { k[p] = tk[i]; v[p] = tv[i]; i++; p++; }
}
static void reverse_vals(Value *a, long n)
{
    long i;
    for (i = 0; i < n / 2; i++) { Value t = a[i]; a[i] = a[n - 1 - i]; a[n - 1 - i] = t; }
}
/* sort list l in place with optional key function */
static void sort_list(List *l, Value key, int reverse)
{
    long n = l->n, i;
    List *kl, *t1, *t2;
    Value *base = vsp;
    if (n < 2) return;
    if (reverse) reverse_vals(l->a, n);
    t1 = list_new(n); t1->n = n;
    for (i = 0; i < n; i++) t1->a[i] = NONE_V;
    VPUSH(mklist(t1));
    t2 = list_new(n); t2->n = n;
    for (i = 0; i < n; i++) t2->a[i] = NONE_V;
    VPUSH(mklist(t2));
    if (key.t != T_NONE && key.t != T_UNSET) {
        kl = list_new(n); kl->n = n;
        for (i = 0; i < n; i++) kl->a[i] = NONE_V;
        VPUSH(mklist(kl));
        for (i = 0; i < n; i++) kl->a[i] = call1(key, l->a[i]);
        msort(kl->a, l->a, t1->a, t2->a, 0, n);
    } else {
        msort(l->a, l->a, t1->a, t2->a, 0, n);
    }
    if (reverse) reverse_vals(l->a, n);
    vsp = base;
}
BI(bi_sorted)
{
    Value lv;
    need(x, 1, 1, "sorted");
    lv = iter_to_list(A(0));
    VPUSH(lv);
    sort_list(AS_LIST(lv), kwget(x, "key"), py_truthy(kwget(x, "reverse").t == T_UNSET ? FALSE_V : kwget(x, "reverse")));
    vsp--;
    return lv;
}

/* ---- objects ---------------------------------------------------------- */
static int isinst(Value v, Value c)
{
    if (c.t == T_CLASS) return class_isa(class_of(v), AS_CLASS(c)) || AS_CLASS(c) == cls_object;
    if (c.t == T_TUPLE) {
        long i;
        for (i = 0; i < AS_LIST(c)->n; i++) if (isinst(v, AS_LIST(c)->a[i])) return 1;
        return 0;
    }
    throw_new(cls_TypeError, "isinstance() arg 2 must be a type, a tuple of types, or a union");
    return 0;
}
BI(bi_isinstance)
{
    need(x, 2, 2, "isinstance");
    return mkbool(isinst(A(0), A(1)));
}
BI(bi_issubclass)
{
    need(x, 2, 2, "issubclass");
    if (A(0).t != T_CLASS) throw_new(cls_TypeError, "issubclass() arg 1 must be a class");
    if (A(1).t == T_CLASS) return mkbool(class_isa(AS_CLASS(A(0)), AS_CLASS(A(1))));
    if (A(1).t == T_TUPLE) {
        long i;
        for (i = 0; i < AS_LIST(A(1))->n; i++)
            if (AS_LIST(A(1))->a[i].t == T_CLASS && class_isa(AS_CLASS(A(0)), AS_CLASS(AS_LIST(A(1))->a[i])))
                return TRUE_V;
        return FALSE_V;
    }
    throw_new(cls_TypeError, "issubclass() arg 2 must be a class or tuple of classes");
    return NONE_V;
}
static Str *cur_gp(void);
BI(bi_type)
{
    need(x, 1, 3, "type");
    if (x->n == 2) throw_new(cls_TypeError, "type() takes 1 or 3 arguments");
    if (x->n == 3) {
        Class **bl, *c;
        List *bt;
        Dict *ns;
        long i, nb;
        Value cv;
        Str *nm = intern_str(astr(A(0), "type.__new__() argument 1"));
        if (A(1).t != T_TUPLE) throw_new(cls_TypeError, "type.__new__() argument 2 must be tuple, not %.40s", type_name(A(1)));
        if (A(2).t != T_DICT) throw_new(cls_TypeError, "type.__new__() argument 3 must be dict, not %.40s", type_name(A(2)));
        bt = AS_LIST(A(1));
        ns = AS_DICT(A(2));
        bl = (Class **)xmalloc(sizeof(Class *) * (size_t)(bt->n + 1));
        nb = 0;
        for (i = 0; i < bt->n; i++) {
            if (bt->a[i].t != T_CLASS) { free(bl); throw_new(cls_TypeError, "bases must be types"); }
            bl[nb++] = AS_CLASS(bt->a[i]);
        }
        if (nb == 0) bl[nb++] = cls_object;
        c = make_class(nm, bl, (int)nb, modname_of_gp(cur_gp()));
        cv = mkobj(c, T_CLASS);
        VPUSH(cv);
        for (i = 0; i < ns->used; i++) {
            if (ns->e[i].k.t != T_STR) continue;
            amap_set(&c->ns, intern_str(AS_STR(ns->e[i].k)), ns->e[i].v);
        }
        finish_class(c);
        run_init_subclass(c, c->base, cv);
        vsp--;
        return cv;
    }
    return mkobj(class_of(A(0)), T_CLASS);
}
static int try_attr(Value o, Str *nm, Value *out)
{
    Handler h;
    Value *save = vsp;
    int depth = call_depth;
    Frame *fr = cur_frame;
    h.prev = cur_handler;
    h.sp = save;
    h.depth = depth;
    h.frame = fr;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        *out = get_attr(o, nm);
        cur_handler = h.prev;
        return 1;
    }
    cur_handler = h.prev;
    vsp = save;
    call_depth = depth;
    cur_frame = fr;
    if (class_isa(AS_INST(cur_exc)->cls, cls_AttributeError)) return 0;
    throw_val(cur_exc);
    return 0;
}
BI(bi_hasattr)
{
    Value r;
    need(x, 2, 2, "hasattr");
    return mkbool(try_attr(A(0), intern_str(astr(A(1), "attribute name")), &r));
}
BI(bi_getattr)
{
    Value r;
    need(x, 2, 3, "getattr");
    if (x->n == 3) {
        if (try_attr(A(0), intern_str(astr(A(1), "attribute name")), &r)) return r;
        return A(2);
    }
    return get_attr(A(0), intern_str(astr(A(1), "attribute name")));
}
BI(bi_setattr)
{
    need(x, 3, 3, "setattr");
    set_attr(A(0), intern_str(astr(A(1), "attribute name")), A(2));
    return NONE_V;
}
BI(bi_delattr)
{
    need(x, 2, 2, "delattr");
    del_attr(A(0), intern_str(astr(A(1), "attribute name")));
    return NONE_V;
}
BI(bi_callable)
{
    Value v;
    need(x, 1, 1, "callable");
    v = A(0);
    if (v.t == T_FUNC || v.t == T_BUILTIN || v.t == T_CLASS || v.t == T_BOUND) return TRUE_V;
    if (v.t == T_INST) return mkbool(class_find(AS_INST(v)->cls, S_call) != NULL);
    return FALSE_V;
}
BI(bi_id)
{
    Value v;
    need(x, 1, 1, "id");
    v = A(0);
    if (IS_HEAP(v.t)) return mkint((long)((size_t)v.u.o >> 2));
    if (v.t == T_FLOAT) return mkint((long)v.u.d + 7);
    return mkint(v.u.i * 4 + v.t);
}
BI(bi_hash)
{
    need(x, 1, 1, "hash");
    return mkint((long)(py_hash(A(0)) & 0x3fffffffUL));
}
BI(bi_format)
{
    need(x, 1, 2, "format");
    if (x->n == 1) return py_str(A(0));
    return format_value(A(0), astr(A(1), "format_spec")->s, AS_STR(A(1))->len);
}
BI(bi_object)
{
    Inst *in = new_inst(cls_object);
    (void)x;
    return mkobj(in, T_INST);
}
BI(bi_super)
{
    Super *s;
    need(x, 0, 2, "super");
    if (x->n < 2) throw_new(cls_RuntimeError, "super(): no arguments");
    if (A(0).t != T_CLASS) throw_new(cls_TypeError, "super() argument 1 must be a type");
    s = (Super *)gc_alloc(sizeof(Super), T_SUPER);
    s->start = AS_CLASS(A(0));
    s->obj = A(1);
    return mkobj(s, T_SUPER);
}
BI(bi_property)
{
    Value g = x->n > 0 ? A(0) : kwget(x, "fget"), s = x->n > 1 ? A(1) : kwget(x, "fset");
    return mk_prop(T_PROP, g.t == T_UNSET ? UNSET_V : g, s.t == T_UNSET ? UNSET_V : s);
}
BI(bi_propsetter)
{
    Prop *p;
    need(x, 2, 2, "setter");
    p = (Prop *)A(0).u.o;
    {
        Value r = mk_prop(T_PROP, p->get, A(1));
        ((Prop *)r.u.o)->del = p->del;
        return r;
    }
}
BI(bi_propdeleter)
{
    Prop *p;
    Value r;
    need(x, 2, 2, "deleter");
    p = (Prop *)A(0).u.o;
    r = mk_prop(T_PROP, p->get, p->set);
    ((Prop *)r.u.o)->del = A(1);
    return r;
}
BI(bi_propgetter)
{
    Prop *p;
    Value r;
    need(x, 2, 2, "getter");
    p = (Prop *)A(0).u.o;
    r = mk_prop(T_PROP, A(1), p->set);
    ((Prop *)r.u.o)->del = p->del;
    return r;
}
BI(bi_staticmethod) { need(x, 1, 1, "staticmethod"); return mk_prop(T_SMETH, A(0), UNSET_V); }
BI(bi_classmethod) { need(x, 1, 1, "classmethod"); return mk_prop(T_CMETH, A(0), UNSET_V); }
BI(bi_exit_fn)
{
    Value a0 = x->n > 0 ? A(0) : NONE_V;
    throw_val(new_exc_args(cls_SystemExit, &a0, x->n > 0 ? 1 : 0));
    return NONE_V;
}

/* ---- exceptions ------------------------------------------------------- */
BI(exc_init)
{
    if (x->n < 1 || A(0).t != T_INST) throw_new(cls_TypeError, "descriptor '__init__' requires an exception");
    amap_set(&AS_INST(A(0))->a, S_args, mktuple_from(x->a + 1, x->n - 1));
    return NONE_V;
}
BI(exc_str)
{
    Buf b;
    buf_init(&b);
    exc_str_buf(AS_INST(A(0)), &b, 0);
    return buf_value(&b);
}
BI(exc_repr)
{
    Buf b;
    buf_init(&b);
    exc_str_buf(AS_INST(A(0)), &b, 1);
    return buf_value(&b);
}
BI(obj_init)
{
    (void)x;
    return NONE_V;
}
BI(obj_repr)
{
    Buf b;
    char t[64];
    Value v = A(0);
    buf_init(&b);
    if (v.t == T_INST) {
        buf_add(&b, "<__main__.");
        buf_add(&b, AS_INST(v)->cls->name->s);
        sprintf(t, " object at 0x%lx>", (unsigned long)(size_t)v.u.o);
        buf_add(&b, t);
    } else {
        repr_buf(v, &b, 0);
    }
    return buf_value(&b);
}
BI(obj_str)
{
    return py_repr(A(0));
}
BI(obj_eq)
{
    return mkbool(identical(A(0), A(1)));
}
BI(obj_ne)
{
    return mkbool(!identical(A(0), A(1)));
}
BI(obj_hash)
{
    return mkint((long)(((unsigned long)(size_t)A(0).u.o >> 3) & 0x3fffffffUL));
}

/* ---- string methods --------------------------------------------------- */
#define SELF astr(A(0), "self")
static int ws(int c) { return ch_space[(unsigned char)c]; }

/* two-byte UTF-8 case mapping (Latin-1, Greek, Cyrillic basics); returns 1 if mapped */
static int u8_case(unsigned char *p, int up)
{
    unsigned char c = p[0], d = p[1];
    if (up) {
        if (c == 0xC3 && d >= 0xA0 && d <= 0xBE && d != 0xB7) { p[1] = (unsigned char)(d - 0x20); return 1; }
        if (c == 0xC3 && d == 0xBF) { p[0] = 0xC5; p[1] = 0xB8; return 1; }
        if (c == 0xCE && d >= 0xB1 && d <= 0xBF) { p[1] = (unsigned char)(d - 0x20); return 1; }
        if (c == 0xCF && d >= 0x80 && d <= 0x89 && d != 0x82) {
            p[0] = (unsigned char)(d >= 0x80 && d <= 0x89 ? 0xCE : c);
            p[1] = (unsigned char)(d + 0x20); return 1; }
        if (c == 0xD0 && d >= 0xB0 && d <= 0xBF) { p[1] = (unsigned char)(d - 0x20); return 1; }
        if (c == 0xD1 && d >= 0x80 && d <= 0x8F) { p[0] = 0xD0; p[1] = (unsigned char)(d + 0x20); return 1; }
    } else {
        if (c == 0xC3 && d >= 0x80 && d <= 0x9E && d != 0x97) { p[1] = (unsigned char)(d + 0x20); return 1; }
        if (c == 0xCE && d >= 0x91 && d <= 0x9F) { p[1] = (unsigned char)(d + 0x20); return 1; }
        if (c == 0xCE && d >= 0xA0 && d <= 0xA9 && d != 0xA2) {
            p[0] = 0xCF; p[1] = (unsigned char)(d - 0x20); return 1; }
        if (c == 0xD0 && d >= 0x90 && d <= 0x9F) { p[1] = (unsigned char)(d + 0x20); return 1; }
        if (c == 0xD0 && d >= 0xA0 && d <= 0xAF) { p[0] = 0xD1; p[1] = (unsigned char)(d - 0x20); return 1; }
    }
    return 0;
}
static Value str_case(Str *s, int up)
{
    Value r = mkstr(s->s, s->len);
    unsigned char *p = (unsigned char *)AS_STR(r)->s;
    long i;
    for (i = 0; i < s->len; i++) {
        if (p[i] >= 0xC3 && p[i] <= 0xD1 && i + 1 < s->len && (p[i + 1] & 0xC0) == 0x80 &&
            u8_case(p + i, up)) { i++; continue; }
        p[i] = up ? to_up[p[i]] : to_low[p[i]];
    }
    return r;
}
BI(s_upper) { return str_case(SELF, 1); }
BI(s_lower) { return str_case(SELF, 0); }
BI(s_swapcase)
{
    Str *s = SELF;
    Value r = mkstr(s->s, s->len);
    long i;
    for (i = 0; i < s->len; i++) {
        int c = (unsigned char)s->s[i];
        AS_STR(r)->s[i] = (char)(ch_upper[c] ? to_low[c] : to_up[c]);
    }
    return r;
}
BI(s_capitalize)
{
    Str *s = SELF;
    Value r = mkstr(s->s, s->len);
    long i;
    for (i = 0; i < s->len; i++)
        AS_STR(r)->s[i] = (char)(i == 0 ? to_up[(unsigned char)s->s[i]] : to_low[(unsigned char)s->s[i]]);
    return r;
}
BI(s_title)
{
    Str *s = SELF;
    Value r = mkstr(s->s, s->len);
    long i;
    int prev = 0;
    for (i = 0; i < s->len; i++) {
        int c = (unsigned char)s->s[i];
        AS_STR(r)->s[i] = (char)(prev ? to_low[c] : to_up[c]);
        prev = ch_alpha[c] && c != '_';
    }
    return r;
}
static Value strip_impl(Args *x, int left, int right)
{
    Str *s = SELF;
    const char *chars = NULL;
    long clen = 0, a = 0, b = s->len;
    if (x->n > 1 && A(1).t == T_STR) { chars = AS_STR(A(1))->s; clen = AS_STR(A(1))->len; }
#define STRIPC(c) (chars ? memchr(chars, (c), (size_t)clen) != NULL : ws(c))
    if (left) while (a < b && STRIPC((unsigned char)s->s[a])) a++;
    if (right) while (b > a && STRIPC((unsigned char)s->s[b - 1])) b--;
#undef STRIPC
    return mkstr(s->s + a, b - a);
}
BI(s_strip) { return strip_impl(x, 1, 1); }
BI(s_lstrip) { return strip_impl(x, 1, 0); }
BI(s_rstrip) { return strip_impl(x, 0, 1); }

static long find_sub(Str *s, Str *u, long start, long end, int rev)
{
    long i;
    if (end > s->len) end = s->len;
    if (start < 0) start = 0;
    if (u->len > end - start) return -1;
    if (!rev) {
        for (i = start; i + u->len <= end; i++)
            if (memcmp(s->s + i, u->s, (size_t)u->len) == 0) return i;
    } else {
        for (i = end - u->len; i >= start; i--)
            if (memcmp(s->s + i, u->s, (size_t)u->len) == 0) return i;
    }
    return -1;
}
static void se_range(Args *x, int first, long len, long *st, long *en)
{
    *st = 0; *en = len;
    if (x->n > first && A(first).t != T_NONE) {
        *st = to_index(A(first));
        if (*st < 0) { *st += len; if (*st < 0) *st = 0; }
    }
    if (x->n > first + 1 && A(first + 1).t != T_NONE) {
        *en = to_index(A(first + 1));
        if (*en < 0) { *en += len; if (*en < 0) *en = 0; }
    }
}
static Value find_impl(Args *x, int rev, int raise, const char *nm)
{
    Str *s = SELF, *u;
    long st, en, r;
    need(x, 2, 4, nm);
    u = astr(A(1), "argument");
    se_range(x, 2, s->len, &st, &en);
    r = find_sub(s, u, st, en, rev);
    if (r < 0 && raise) throw_new(cls_ValueError, "substring not found");
    return mkint(r);
}
BI(s_find) { return find_impl(x, 0, 0, "find"); }
BI(s_rfind) { return find_impl(x, 1, 0, "rfind"); }
BI(s_index) { return find_impl(x, 0, 1, "index"); }
BI(s_rindex) { return find_impl(x, 1, 1, "rindex"); }
BI(s_count)
{
    Str *s = SELF, *u;
    long st, en, n = 0, i;
    need(x, 2, 4, "count");
    u = astr(A(1), "argument");
    se_range(x, 2, s->len, &st, &en);
    if (u->len == 0) return mkint(en - st + 1);
    for (i = st; i + u->len <= en; ) {
        if (memcmp(s->s + i, u->s, (size_t)u->len) == 0) { n++; i += u->len; }
        else i++;
    }
    return mkint(n);
}
static Value startend(Args *x, int atend)
{
    Str *s = SELF;
    long st, en;
    Value p;
    need(x, 2, 4, atend ? "endswith" : "startswith");
    p = A(1);
    se_range(x, 2, s->len, &st, &en);
    if (p.t == T_TUPLE) {
        long i;
        for (i = 0; i < AS_LIST(p)->n; i++) {
            Str *u = astr(AS_LIST(p)->a[i], "tuple item");
            if (en - st >= u->len && memcmp(s->s + (atend ? en - u->len : st), u->s, (size_t)u->len) == 0)
                return TRUE_V;
        }
        return FALSE_V;
    }
    {
        Str *u = astr(p, "startswith first arg");
        if (en - st < u->len) return FALSE_V;
        return mkbool(memcmp(s->s + (atend ? en - u->len : st), u->s, (size_t)u->len) == 0);
    }
}
BI(s_startswith) { return startend(x, 0); }
BI(s_endswith) { return startend(x, 1); }

static Value split_impl(Args *x, int rev)
{
    Str *s = SELF;
    Value sepv = x->n > 1 ? A(1) : kwget(x, "sep"), mv = x->n > 2 ? A(2) : kwget(x, "maxsplit");
    long maxs = -1, i, j, n = s->len;
    List *l = list_new(8);
    Value lv = mklist(l);
    VPUSH(lv);
    if (mv.t != T_UNSET) maxs = to_index(mv);
    if (sepv.t == T_UNSET || sepv.t == T_NONE) {
        if (!rev) {
            i = 0;
            while (1) {
                while (i < n && ws((unsigned char)s->s[i])) i++;
                if (i >= n) break;
                if (maxs >= 0 && l->n >= maxs) {
                    long e = n;
                    while (e > i && ws((unsigned char)s->s[e - 1])) e--;
                    list_push(l, mkstr(s->s + i, e - i));
                    break;
                }
                j = i;
                while (j < n && !ws((unsigned char)s->s[j])) j++;
                list_push(l, mkstr(s->s + i, j - i));
                i = j;
            }
        } else {
            long e = n, cnt = 0;
            List *t = list_new(8);
            Value tv = mklist(t);
            VPUSH(tv);
            while (1) {
                while (e > 0 && ws((unsigned char)s->s[e - 1])) e--;
                if (e <= 0) break;
                if (maxs >= 0 && cnt >= maxs) {
                    long b = 0;
                    while (b < e && ws((unsigned char)s->s[b])) b++;
                    list_push(t, mkstr(s->s + b, e - b));
                    break;
                }
                j = e;
                while (j > 0 && !ws((unsigned char)s->s[j - 1])) j--;
                list_push(t, mkstr(s->s + j, e - j));
                cnt++;
                e = j;
            }
            for (i = t->n - 1; i >= 0; i--) list_push(l, t->a[i]);
            vsp--;
        }
    } else {
        Str *sep = astr(sepv, "sep");
        if (sep->len == 0) throw_new(cls_ValueError, "empty separator");
        if (!rev) {
            i = 0;
            while (1) {
                long f = (maxs >= 0 && l->n >= maxs) ? -1 : find_sub(s, sep, i, n, 0);
                if (f < 0) { list_push(l, mkstr(s->s + i, n - i)); break; }
                list_push(l, mkstr(s->s + i, f - i));
                i = f + sep->len;
            }
        } else {
            List *t = list_new(8);
            Value tv = mklist(t);
            long e = n, cnt = 0;
            VPUSH(tv);
            while (1) {
                long f = (maxs >= 0 && cnt >= maxs) ? -1 : find_sub(s, sep, 0, e, 1);
                if (f < 0) { list_push(t, mkstr(s->s, e)); break; }
                list_push(t, mkstr(s->s + f + sep->len, e - f - sep->len));
                e = f;
                cnt++;
            }
            for (i = t->n - 1; i >= 0; i--) list_push(l, t->a[i]);
            vsp--;
        }
    }
    vsp--;
    return lv;
}
BI(s_split) { return split_impl(x, 0); }
BI(s_rsplit) { return split_impl(x, 1); }
BI(s_splitlines)
{
    Str *s = SELF;
    List *l = list_new(8);
    Value lv = mklist(l);
    long i = 0, st = 0;
    int keep = x->n > 1 && py_truthy(A(1));
    VPUSH(lv);
    while (i < s->len) {
        if (s->s[i] == '\n' || s->s[i] == '\r') {
            long e = i;
            if (s->s[i] == '\r' && i + 1 < s->len && s->s[i + 1] == '\n') i++;
            i++;
            list_push(l, mkstr(s->s + st, (keep ? i : e) - st));
            st = i;
        } else i++;
    }
    if (st < s->len) list_push(l, mkstr(s->s + st, s->len - st));
    vsp--;
    return lv;
}
BI(s_join)
{
    Str *sep = SELF;
    Value lv, r;
    List *l;
    Buf b;
    long i;
    need(x, 2, 2, "join");
    lv = iter_to_list(A(1));
    VPUSH(lv);
    l = AS_LIST(lv);
    buf_init(&b);
    for (i = 0; i < l->n; i++) {
        if (l->a[i].t != T_STR) {
            buf_free(&b);
            throw_new(cls_TypeError, "sequence item %ld: expected str instance, %.40s found", i, type_name(l->a[i]));
        }
        if (i) buf_addn(&b, sep->s, sep->len);
        buf_addn(&b, AS_STR(l->a[i])->s, AS_STR(l->a[i])->len);
    }
    r = buf_value(&b);
    vsp--;
    return r;
}
BI(s_replace)
{
    Str *s = SELF, *o, *nw;
    long cnt = -1, i = 0, f;
    Buf b;
    need(x, 3, 4, "replace");
    o = astr(A(1), "old");
    nw = astr(A(2), "new");
    if (x->n > 3) cnt = to_index(A(3));
    buf_init(&b);
    if (o->len == 0) {
        long k = 0;
        for (i = 0; i <= s->len; i++) {
            if (cnt < 0 || k < cnt) { buf_addn(&b, nw->s, nw->len); k++; }
            if (i < s->len) buf_addc(&b, s->s[i]);
        }
        return buf_value(&b);
    }
    while (1) {
        f = cnt == 0 ? -1 : find_sub(s, o, i, s->len, 0);
        if (f < 0) break;
        buf_addn(&b, s->s + i, f - i);
        buf_addn(&b, nw->s, nw->len);
        i = f + o->len;
        if (cnt > 0) cnt--;
    }
    buf_addn(&b, s->s + i, s->len - i);
    return buf_value(&b);
}
static int all_chars(Str *s, const unsigned char *tab)
{
    long i;
    if (s->len == 0) return 0;
    for (i = 0; i < s->len; i++) if (!tab[(unsigned char)s->s[i]]) return 0;
    return 1;
}
BI(s_isalpha) { return mkbool(all_chars(SELF, ch_alpha) && !memchr(SELF->s, '_', (size_t)SELF->len)); }
BI(s_isdigit) { return mkbool(all_chars(SELF, ch_digit)); }
BI(s_isalnum) { return mkbool(all_chars(SELF, ch_alnum) && !memchr(SELF->s, '_', (size_t)SELF->len)); }
BI(s_isspace) { return mkbool(all_chars(SELF, ch_space)); }
BI(s_isupper)
{
    Str *s = SELF;
    long i;
    int any = 0;
    for (i = 0; i < s->len; i++) {
        if (ch_lower[(unsigned char)s->s[i]]) return FALSE_V;
        if (ch_upper[(unsigned char)s->s[i]]) any = 1;
    }
    return mkbool(any);
}
BI(s_islower)
{
    Str *s = SELF;
    long i;
    int any = 0;
    for (i = 0; i < s->len; i++) {
        if (ch_upper[(unsigned char)s->s[i]]) return FALSE_V;
        if (ch_lower[(unsigned char)s->s[i]]) any = 1;
    }
    return mkbool(any);
}
static Value justify(Args *x, int mode)
{
    Str *s = SELF;
    long w, pad, i, l;
    int fill = ' ';
    Buf b;
    need(x, 2, 3, "ljust");
    w = to_index(A(1));
    if (x->n > 2) { Str *f = astr(A(2), "fillchar"); if (f->len != 1) throw_new(cls_TypeError, "The fill character must be exactly one character long"); fill = f->s[0]; }
    pad = w - s->len;
    if (pad <= 0) return A(0);
    buf_init(&b);
    l = mode == 0 ? 0 : (mode == 1 ? pad : pad / 2);
    if (mode == 2) l = pad / 2 + (pad & w & 1);
    for (i = 0; i < l; i++) buf_addc(&b, fill);
    buf_addn(&b, s->s, s->len);
    for (i = 0; i < pad - l; i++) buf_addc(&b, fill);
    return buf_value(&b);
}
BI(s_ljust) { return justify(x, 0); }
BI(s_rjust) { return justify(x, 1); }
BI(s_center) { return justify(x, 2); }
BI(s_zfill)
{
    Str *s = SELF;
    long w = to_index(A(1)), pad = w - s->len, i;
    Buf b;
    if (pad <= 0) return A(0);
    buf_init(&b);
    i = 0;
    if (s->len > 0 && (s->s[0] == '-' || s->s[0] == '+')) { buf_addc(&b, s->s[0]); i = 1; }
    for (; pad > 0; pad--) buf_addc(&b, '0');
    buf_addn(&b, s->s + i, s->len - i);
    return buf_value(&b);
}
static Value partition_impl(Args *x, int rev)
{
    Str *s = SELF, *sep = astr(A(1), "sep");
    long f;
    Value t[3];
    Value *base = vsp;
    if (sep->len == 0) throw_new(cls_ValueError, "empty separator");
    f = find_sub(s, sep, 0, s->len, rev);
    if (f < 0) {
        t[0] = rev ? mkstr("", 0) : A(0);
        VPUSH(t[0]);
        t[1] = mkstr("", 0);
        VPUSH(t[1]);
        t[2] = rev ? A(0) : mkstr("", 0);
    } else {
        t[0] = mkstr(s->s, f);
        VPUSH(t[0]);
        t[1] = A(1);
        t[2] = mkstr(s->s + f + sep->len, s->len - f - sep->len);
    }
    vsp = base;
    return mktuple_from(t, 3);
}
BI(s_partition) { return partition_impl(x, 0); }
BI(s_rpartition) { return partition_impl(x, 1); }
BI(s_format)
{
    return format_template(SELF, x->n - 1, x->a + 1, x->nk, x->kw, 0);
}
BI(s_encode)
{
    Str *s = SELF;
    Value ev = x->n > 1 ? A(1) : kwget(x, "encoding");
    Value errv = x->n > 2 ? A(2) : kwget(x, "errors");
    const char *enc = "utf-8", *err = "strict";
    long i;
    Buf b;
    if (ev.t == T_STR) enc = AS_STR(ev)->s;
    if (errv.t == T_STR) err = AS_STR(errv)->s;
    if (strcmp(enc, "utf-8") == 0 || strcmp(enc, "utf8") == 0 || strcmp(enc, "UTF-8") == 0)
        return mkbytes(s->s, s->len);
    buf_init(&b);
    for (i = 0; i < s->len; ) {
        unsigned long c = (unsigned char)s->s[i];
        int n = 1;
        if (c >= 0xF0) { n = 4; c &= 7; } else if (c >= 0xE0) { n = 3; c &= 15; }
        else if (c >= 0xC0) { n = 2; c &= 31; }
        if (n > 1) { int k; for (k = 1; k < n && i + k < s->len; k++) c = (c << 6) | ((unsigned char)s->s[i + k] & 63); }
        i += n;
        if (c < 128 || (c < 256 && (strcmp(enc, "latin-1") == 0 || strcmp(enc, "latin1") == 0 ||
                                    strcmp(enc, "iso-8859-1") == 0))) buf_addc(&b, (int)c);
        else if (strcmp(err, "ignore") == 0) { }
        else if (strcmp(err, "replace") == 0) buf_addc(&b, '?');
        else {
            buf_free(&b);
            throw_new(cls_ValueError, "'%s' codec can't encode character", enc);
        }
    }
    { Value r = mkbytes(b.s, b.n); buf_free(&b); return r; }
}
BI(s_removeprefix)
{
    Str *s = SELF, *p = astr(A(1), "prefix");
    if (s->len >= p->len && memcmp(s->s, p->s, (size_t)p->len) == 0) return mkstr(s->s + p->len, s->len - p->len);
    return A(0);
}
BI(s_removesuffix)
{
    Str *s = SELF, *p = astr(A(1), "suffix");
    if (p->len > 0 && s->len >= p->len && memcmp(s->s + s->len - p->len, p->s, (size_t)p->len) == 0)
        return mkstr(s->s, s->len - p->len);
    return A(0);
}
BI(s_expandtabs)
{
    Str *s = SELF;
    long tab = x->n > 1 ? to_index(A(1)) : 8, i, col = 0;
    Buf b;
    buf_init(&b);
    for (i = 0; i < s->len; i++) {
        if (s->s[i] == '\t') {
            long k = tab > 0 ? tab - col % tab : 0;
            while (k-- > 0) { buf_addc(&b, ' '); col++; }
        } else {
            buf_addc(&b, s->s[i]);
            col = s->s[i] == '\n' ? 0 : col + 1;
        }
    }
    return buf_value(&b);
}

/* ---- list methods ----------------------------------------------------- */
static List *self_list(Args *x)
{
    if (A(0).t != T_LIST) throw_new(cls_TypeError, "descriptor requires a 'list' object but received '%.40s'", type_name(A(0)));
    return AS_LIST(A(0));
}
BI(l_append)
{
    need(x, 2, 2, "append");
    list_push(self_list(x), A(1));
    return NONE_V;
}
BI(l_extend)
{
    List *l = self_list(x);
    Value it, y;
    need(x, 2, 2, "extend");
    if (A(1).t == T_LIST || A(1).t == T_TUPLE) {
        List *s = AS_LIST(A(1));
        long i, n = s->n;
        for (i = 0; i < n; i++) list_push(l, s->a[i]);
        return NONE_V;
    }
    it = get_iter(A(1));
    VPUSH(it);
    while (iter_next(it, &y)) list_push(l, y);
    vsp--;
    return NONE_V;
}
BI(l_insert)
{
    List *l = self_list(x);
    long i;
    need(x, 3, 3, "insert");
    i = to_index(A(1));
    if (i < 0) { i += l->n; if (i < 0) i = 0; }
    if (i > l->n) i = l->n;
    list_push(l, NONE_V);
    memmove(l->a + i + 1, l->a + i, sizeof(Value) * (size_t)(l->n - 1 - i));
    l->a[i] = A(2);
    return NONE_V;
}
BI(l_pop)
{
    List *l = self_list(x);
    long i;
    Value r;
    need(x, 1, 2, "pop");
    if (l->n == 0) throw_new(cls_IndexError, "pop from empty list");
    i = x->n > 1 ? to_index(A(1)) : l->n - 1;
    if (i < 0) i += l->n;
    if (i < 0 || i >= l->n) throw_new(cls_IndexError, "pop index out of range");
    r = l->a[i];
    list_splice(l, i, i + 1, NULL, 0);
    return r;
}
BI(l_remove)
{
    List *l = self_list(x);
    long i;
    need(x, 2, 2, "remove");
    for (i = 0; i < l->n; i++)
        if (py_eq(l->a[i], A(1))) { list_splice(l, i, i + 1, NULL, 0); return NONE_V; }
    throw_new(cls_ValueError, "list.remove(x): x not in list");
    return NONE_V;
}
static Value seq_index(Args *x, int istuple)
{
    List *l = AS_LIST(A(0));
    long i, st = 0, en = l->n;
    need(x, 2, 4, "index");
    if (x->n > 2) { st = to_index(A(2)); if (st < 0) st += l->n; if (st < 0) st = 0; }
    if (x->n > 3) { en = to_index(A(3)); if (en < 0) en += l->n; }
    for (i = st; i < en && i < l->n; i++)
        if (py_eq(l->a[i], A(1))) return mkint(i);
    if (istuple) throw_new(cls_ValueError, "tuple.index(x): x not in tuple");
    {
        Value r = py_repr(A(1));
        throw_new(cls_ValueError, "%.200s is not in list", AS_STR(r)->s);
    }
    return NONE_V;
}
BI(l_index) { self_list(x); return seq_index(x, 0); }
BI(t_index) { return seq_index(x, 1); }
BI(l_count)
{
    List *l = AS_LIST(A(0));
    long i, n = 0;
    need(x, 2, 2, "count");
    for (i = 0; i < l->n; i++) if (py_eq(l->a[i], A(1))) n++;
    return mkint(n);
}
BI(l_sort)
{
    List *l = self_list(x);
    Value rv = kwget(x, "reverse");
    sort_list(l, kwget(x, "key"), rv.t != T_UNSET && py_truthy(rv));
    return NONE_V;
}
BI(l_reverse)
{
    List *l = self_list(x);
    reverse_vals(l->a, l->n);
    return NONE_V;
}
BI(l_copy)
{
    return iter_to_list(A(0));
}
BI(l_clear)
{
    self_list(x)->n = 0;
    return NONE_V;
}

/* ---- dict methods ----------------------------------------------------- */
static Dict *self_dict(Args *x)
{
    if (A(0).t != T_DICT) throw_new(cls_TypeError, "descriptor requires a 'dict' object but received '%.40s'", type_name(A(0)));
    return AS_DICT(A(0));
}
BI(d_get)
{
    Value r;
    need(x, 2, 3, "get");
    if (dict_get(self_dict(x), A(1), &r)) return r;
    return x->n > 2 ? A(2) : NONE_V;
}
BI(d_keys) { self_dict(x); return mkdview(A(0), 0); }
BI(d_values) { self_dict(x); return mkdview(A(0), 1); }
BI(d_items) { self_dict(x); return mkdview(A(0), 2); }
BI(d_update)
{
    Dict *d = self_dict(x);
    int i;
    if (x->n > 1) dict_update_from(d, A(1));
    for (i = 0; i < x->nk; i++) dict_set(d, x->kw[2 * i], x->kw[2 * i + 1]);
    return NONE_V;
}
BI(d_pop)
{
    Dict *d = self_dict(x);
    Value r;
    need(x, 2, 3, "pop");
    if (dict_get(d, A(1), &r)) { dict_del(d, A(1)); return r; }
    if (x->n > 2) return A(2);
    throw_key(A(1));
    return NONE_V;
}
BI(d_setdefault)
{
    Dict *d = self_dict(x);
    Value r;
    need(x, 2, 3, "setdefault");
    if (dict_get(d, A(1), &r)) return r;
    r = x->n > 2 ? A(2) : NONE_V;
    dict_set(d, A(1), r);
    return r;
}
BI(d_copy)
{
    Dict *n = dict_new();
    Value nv = mkobj(n, T_DICT);
    VPUSH(nv);
    dict_update_from(n, A(0));
    vsp--;
    return nv;
}
BI(d_clear)
{
    dict_clear(self_dict(x));
    return NONE_V;
}
BI(d_popitem)
{
    Dict *d = self_dict(x);
    long i;
    for (i = d->used - 1; i >= 0; i--) {
        if (d->e[i].k.t != T_UNSET) {
            Value r = mktuple2(d->e[i].k, d->e[i].v);
            dict_del(d, d->e[i].k);
            return r;
        }
    }
    throw_new(cls_KeyError, "popitem(): dictionary is empty");
    return NONE_V;
}
BI(d_fromkeys)
{
    Dict *d = dict_new();
    Value dv = mkobj(d, T_DICT), it, y;
    VPUSH(dv);
    it = get_iter(A(0));
    VPUSH(it);
    while (iter_next(it, &y)) dict_set(d, y, x->n > 1 ? A(1) : NONE_V);
    vsp -= 2;
    return dv;
}

/* ---- set methods ------------------------------------------------------ */
static Dict *self_set(Args *x)
{
    if (A(0).t != T_SET) throw_new(cls_TypeError, "descriptor requires a 'set' object but received '%.40s'", type_name(A(0)));
    return AS_DICT(A(0));
}
BI(st_add) { need(x, 2, 2, "add"); set_add(self_set(x), A(1)); return NONE_V; }
BI(st_discard) { need(x, 2, 2, "discard"); dict_del(self_set(x), A(1)); return NONE_V; }
BI(st_remove)
{
    need(x, 2, 2, "remove");
    if (!dict_del(self_set(x), A(1))) throw_key(A(1));
    return NONE_V;
}
BI(st_pop)
{
    Dict *d = self_set(x);
    long i;
    for (i = 0; i < d->used; i++) {
        if (d->e[i].k.t != T_UNSET) {
            Value k = d->e[i].k;
            dict_del(d, k);
            return k;
        }
    }
    throw_new(cls_KeyError, "pop from an empty set");
    return NONE_V;
}
static Value to_set(Value v)
{
    Args a;
    Value arg = v;
    if (v.t == T_SET) return v;
    a.n = 1; a.a = &arg; a.nk = 0; a.kw = NULL;
    return bi_set(&a);
}
static Value set_method_op(Args *x, int op)
{
    Value r = A(0), o;
    int i;
    for (i = 1; i < x->n; i++) {
        VPUSH(r);
        o = to_set(A(i));
        VPUSH(o);
        r = set_op(op, r, o);
        vsp -= 2;
    }
    if (x->n == 1) r = set_op(B_OR, A(0), A(0));
    return r;
}
BI(st_union) { self_set(x); return set_method_op(x, B_OR); }
BI(st_intersection) { self_set(x); return set_method_op(x, B_AND); }
BI(st_difference) { self_set(x); return set_method_op(x, B_SUB); }
BI(st_symdiff) { self_set(x); return set_method_op(x, B_XOR); }
BI(st_update)
{
    Dict *d = self_set(x);
    int i;
    for (i = 1; i < x->n; i++) {
        Value it, y;
        VPUSH(A(i));
        it = get_iter(A(i));
        VPUSH(it);
        while (iter_next(it, &y)) set_add(d, y);
        vsp -= 2;
    }
    return NONE_V;
}
BI(st_issubset)
{
    Value o = to_set(A(1));
    self_set(x);
    return mkbool(rich_cmp(C_LE, A(0), o));
}
BI(st_issuperset)
{
    Value o = to_set(A(1));
    self_set(x);
    return mkbool(rich_cmp(C_GE, A(0), o));
}
BI(st_isdisjoint)
{
    Value o = to_set(A(1)), r;
    self_set(x);
    VPUSH(o);
    r = set_op(B_AND, A(0), o);
    vsp--;
    return mkbool(AS_DICT(r)->n == 0);
}
BI(st_copy)
{
    self_set(x);
    return set_op(B_OR, A(0), A(0));
}
BI(st_clear)
{
    dict_clear(self_set(x));
    return NONE_V;
}

/* ---- numbers ---------------------------------------------------------- */
BI(i_bit_length)
{
    Value v = A(0);
    long n = 0;
    if (v.t == T_BIG) {
        Buf b;
        buf_init(&b);
        int_str_base(int_sign(v) < 0 ? int_neg(v) : v, 2, &b);
        n = b.n;
        buf_free(&b);
        return mkint(n);
    }
    {
        unsigned long u = (unsigned long)(v.u.i < 0 ? -v.u.i : v.u.i);
        while (u) { n++; u >>= 1; }
    }
    return mkint(n);
}
BI(f_is_integer)
{
    return mkbool(A(0).u.d == floor(A(0).u.d));
}

/* ---- generators and iterators ----------------------------------------- */
static void throw_stop(Value g)
{
    Value rv;
    if (g.t == T_GEN && ((Gen *)g.u.o)->ret.t != T_NONE) {
        rv = ((Gen *)g.u.o)->ret;
        ((Gen *)g.u.o)->ret = NONE_V;
        throw_val(new_exc_args(cls_StopIteration, &rv, 1));
    }
    throw_val(new_exc_args(cls_StopIteration, NULL, 0));
}
BI(g_next)
{
    Value out;
    if (iter_next(A(0), &out)) return out;
    throw_stop(A(0));
    return NONE_V;
}
BI(g_iter) { return A(0); }
static int protected_gen(Gen *g, Value *out, int *got, Value *exc)
{
    Handler h;
    Value *save = vsp;
    int depth = call_depth;
    Frame *fr = cur_frame;
    h.prev = cur_handler;
    h.sp = save;
    h.depth = depth;
    h.frame = fr;
    cur_handler = &h;
    if (setjmp(h.jb) == 0) {
        *got = gen_resume(g, out);
        cur_handler = h.prev;
        return 0;
    }
    cur_handler = h.prev;
    vsp = save;
    call_depth = depth;
    cur_frame = fr;
    *exc = cur_exc;
    return 1;
}
BI(g_send)
{
    Value out;
    need(x, 2, 2, "send");
    if (A(0).t != T_GEN) {
        if (A(1).t != T_NONE) throw_new(cls_AttributeError, "'%.40s' object has no attribute 'send'", type_name(A(0)));
        if (iter_next(A(0), &out)) return out;
        throw_stop(A(0));
    }
    if (!((Gen *)A(0).u.o)->started && A(1).t != T_NONE)
        throw_new(cls_TypeError, "can't send non-None value to a just-started generator");
    ((Gen *)A(0).u.o)->sent = A(1);
    if (gen_resume((Gen *)A(0).u.o, &out)) return out;
    throw_stop(A(0));
    return NONE_V;
}
BI(g_throw)
{
    Gen *g;
    Value out, e;
    need(x, 2, 4, "throw");
    if (A(0).t != T_GEN) throw_new(cls_AttributeError, "'%.40s' object has no attribute 'throw'", type_name(A(0)));
    g = (Gen *)A(0).u.o;
    e = A(1);
    if (e.t == T_CLASS) e = call0(e);
    if (e.t != T_INST || !(AS_INST(e)->cls->flags & CF_EXC))
        throw_new(cls_TypeError, "exceptions must derive from BaseException");
    if (!g->started || g->done) {
        g->done = 1;
        throw_val(e);
    }
    g->thrown = e;
    if (gen_resume(g, &out)) return out;
    throw_stop(A(0));
    return NONE_V;
}
BI(g_close)
{
    Gen *g;
    Value out, exc;
    int got = 0;
    if (A(0).t != T_GEN) return NONE_V;
    g = (Gen *)A(0).u.o;
    if (g->started && !g->done && !g->running) {
        g->thrown = new_exc_args(cls_GeneratorExit, NULL, 0);
        if (protected_gen(g, &out, &got, &exc)) {
            g->done = 1;
            if (!class_isa(AS_INST(exc)->cls, cls_GeneratorExit) &&
                !class_isa(AS_INST(exc)->cls, cls_StopIteration))
                throw_val(exc);
            return NONE_V;
        }
        if (got) {
            g->done = 1;
            throw_new(cls_RuntimeError, "generator ignored GeneratorExit");
        }
    }
    g->done = 1;
    return NONE_V;
}

/* ---- files ------------------------------------------------------------ */
static File *self_file(Args *x)
{
    File *f;
    if (A(0).t != T_FILE) throw_new(cls_TypeError, "descriptor requires a file object");
    f = (File *)A(0).u.o;
    if (f->closed) throw_new(cls_ValueError, "I/O operation on closed file");
    return f;
}
static Value file_readline(File *f)
{
    Buf b;
    int c;
    buf_init(&b);
    while ((c = getc(f->fp)) != EOF) {
        buf_addc(&b, c);
        if (c == '\n') break;
    }
    return buf_value(&b);
}
BI(bi_open)
{
    File *f;
    Str *path, *mode;
    FILE *fp;
    char m[8];
    int k = 0, i;
    need(x, 1, 2, "open");
    path = astr(A(0), "file");
    mode = x->n > 1 ? astr(A(1), "mode") : NULL;
    if (!mode && kwget(x, "mode").t == T_STR) mode = AS_STR(kwget(x, "mode"));
    m[k++] = 'r';
    if (mode) {
        k = 0;
        for (i = 0; i < mode->len && k < 3; i++) {
            if (mode->s[i] == 'r' || mode->s[i] == 'w' || mode->s[i] == 'a' || mode->s[i] == '+')
                m[k++] = mode->s[i];
        }
        if (k == 0) m[k++] = 'r';
    }
    m[k] = 0;
    fp = fopen(path->s, m);
    if (!fp) {
        Value v[2];
        Value q = py_repr(A(0));
        char msg[300];
        sprintf(msg, "[Errno 2] No such file or directory: %.200s", AS_STR(q)->s);
        v[0] = mkcstr(msg);
        throw_val(new_exc_args(cls_FileNotFoundError, v, 1));
    }
    f = (File *)gc_alloc(sizeof(File), T_FILE);
    f->fp = fp;
    f->closed = 0;
    f->out = 0;
    return mkobj(f, T_FILE);
}
BI(fl_read)
{
    File *f = self_file(x);
    Buf b;
    int c;
    long n = x->n > 1 && A(1).t != T_NONE ? to_index(A(1)) : -1;
    buf_init(&b);
    while ((n < 0 || b.n < n) && (c = getc(f->fp)) != EOF) buf_addc(&b, c);
    return buf_value(&b);
}
BI(fl_readline) { return file_readline(self_file(x)); }
BI(fl_readlines)
{
    File *f = self_file(x);
    List *l = list_new(8);
    Value lv = mklist(l), ln;
    VPUSH(lv);
    for (;;) {
        ln = file_readline(f);
        if (AS_STR(ln)->len == 0) break;
        list_push(l, ln);
    }
    vsp--;
    return lv;
}
BI(fl_write)
{
    File *f = self_file(x);
    Str *s = astr(A(1), "write() argument");
    if (f->fp == stderr) fflush(stdout);
    fwrite(s->s, 1, (size_t)s->len, f->fp);
    return mkint(s->len);
}
BI(fl_writelines)
{
    File *f = self_file(x);
    Value it, y;
    it = get_iter(A(1));
    VPUSH(it);
    while (iter_next(it, &y)) fwrite(astr(y, "item")->s, 1, (size_t)AS_STR(y)->len, f->fp);
    vsp--;
    return NONE_V;
}
BI(fl_flush)
{
    fflush(self_file(x)->fp);
    return NONE_V;
}
BI(fl_close)
{
    File *f = (File *)A(0).u.o;
    if (!f->closed && !f->out) fclose(f->fp);
    f->closed = 1;
    return NONE_V;
}
BI(fl_enter) { return A(0); }
BI(fl_exit)
{
    File *f = (File *)A(0).u.o;
    if (!f->closed && !f->out) fclose(f->fp);
    f->closed = 1;
    return FALSE_V;
}

/* ---- protocol methods of the native base types (list dict set tuple str) ----------
   On subclass instances the self argument is replaced by the payload (Builtin.selfpay). */
BI(nat_new)
{
    Class *c;
    Inst *in;
    Value iv;
    if (x->n < 1 || A(0).t != T_CLASS) throw_new(cls_TypeError, "__new__(X): X is not a type object");
    c = AS_CLASS(A(0));
    in = new_inst(c);
    iv = mkobj(in, T_INST);
    VPUSH(iv);
    if (c->paycls) {
        if (c->paycls == cls_tuple || c->paycls == cls_str)
            in->pay = pay_ctor(c->paycls, x->n - 1, x->a + 1, x->nk, x->kw);
        else
            in->pay = pay_ctor(c->paycls, 0, NULL, 0, NULL);
    }
    vsp--;
    return iv;
}
BI(nat_getitem) { need(x, 2, 2, "__getitem__"); return subscr(A(0), A(1)); }
BI(nat_setitem) { need(x, 3, 3, "__setitem__"); setitem(A(0), A(1), A(2)); return NONE_V; }
BI(nat_delitem) { need(x, 2, 2, "__delitem__"); delitem(A(0), A(1)); return NONE_V; }
BI(nat_len) { return mkint(py_len(A(0))); }
BI(nat_iter) { return get_iter(A(0)); }
BI(nat_contains) { need(x, 2, 2, "__contains__"); return mkbool(contains(A(0), A(1))); }
BI(nat_repr) { return py_repr(A(0)); }
BI(nat_str) { return py_str(A(0)); }
BI(nat_eq) { need(x, 2, 2, "__eq__"); return mkbool(py_eq(A(0), unpay(A(1)))); }
BI(nat_ne) { need(x, 2, 2, "__ne__"); return mkbool(!py_eq(A(0), unpay(A(1)))); }
BI(nat_lt) { need(x, 2, 2, "__lt__"); return mkbool(rich_cmp(C_LT, A(0), unpay(A(1)))); }
BI(nat_le) { need(x, 2, 2, "__le__"); return mkbool(rich_cmp(C_LE, A(0), unpay(A(1)))); }
BI(nat_gt) { need(x, 2, 2, "__gt__"); return mkbool(rich_cmp(C_GT, A(0), unpay(A(1)))); }
BI(nat_ge) { need(x, 2, 2, "__ge__"); return mkbool(rich_cmp(C_GE, A(0), unpay(A(1)))); }
BI(nat_hash) { return mkint((long)py_hash(A(0))); }
BI(nat_add) { need(x, 2, 2, "__add__"); return binop(B_ADD, A(0), unpay(A(1))); }
BI(nat_mul) { need(x, 2, 2, "__mul__"); return binop(B_MUL, A(0), A(1)); }
BI(nat_mod) { need(x, 2, 2, "__mod__"); return binop(B_MOD, A(0), A(1)); }
BI(nat_or) { need(x, 2, 2, "__or__"); return binop(B_OR, A(0), unpay(A(1))); }
BI(nat_and) { need(x, 2, 2, "__and__"); return binop(B_AND, A(0), unpay(A(1))); }
BI(nat_sub) { need(x, 2, 2, "__sub__"); return binop(B_SUB, A(0), unpay(A(1))); }
BI(nat_xor) { need(x, 2, 2, "__xor__"); return binop(B_XOR, A(0), unpay(A(1))); }
/* arithmetic dunders of int/float (called as methods) */
BI(num_add) { need(x, 2, 2, "__add__"); return binop(B_ADD, unpay(A(0)), unpay(A(1))); }
BI(num_radd) { need(x, 2, 2, "__radd__"); return binop(B_ADD, unpay(A(1)), unpay(A(0))); }
BI(num_sub) { need(x, 2, 2, "__sub__"); return binop(B_SUB, unpay(A(0)), unpay(A(1))); }
BI(num_rsub) { need(x, 2, 2, "__rsub__"); return binop(B_SUB, unpay(A(1)), unpay(A(0))); }
BI(num_mul) { need(x, 2, 2, "__mul__"); return binop(B_MUL, unpay(A(0)), unpay(A(1))); }
BI(num_rmul) { need(x, 2, 2, "__rmul__"); return binop(B_MUL, unpay(A(1)), unpay(A(0))); }
BI(num_truediv) { need(x, 2, 2, "__truediv__"); return binop(B_DIV, unpay(A(0)), unpay(A(1))); }
BI(num_rtruediv) { need(x, 2, 2, "__rtruediv__"); return binop(B_DIV, unpay(A(1)), unpay(A(0))); }
BI(num_floordiv) { need(x, 2, 2, "__floordiv__"); return binop(B_FDIV, unpay(A(0)), unpay(A(1))); }
BI(num_rfloordiv) { need(x, 2, 2, "__rfloordiv__"); return binop(B_FDIV, unpay(A(1)), unpay(A(0))); }
BI(num_mod) { need(x, 2, 2, "__mod__"); return binop(B_MOD, unpay(A(0)), unpay(A(1))); }
BI(num_rmod) { need(x, 2, 2, "__rmod__"); return binop(B_MOD, unpay(A(1)), unpay(A(0))); }
BI(num_pow) { need(x, 2, 2, "__pow__"); return binop(B_POW, unpay(A(0)), unpay(A(1))); }
BI(num_rpow) { need(x, 2, 2, "__rpow__"); return binop(B_POW, unpay(A(1)), unpay(A(0))); }
BI(num_and) { need(x, 2, 2, "__and__"); return binop(B_AND, unpay(A(0)), unpay(A(1))); }
BI(num_rand) { need(x, 2, 2, "__rand__"); return binop(B_AND, unpay(A(1)), unpay(A(0))); }
BI(num_or) { need(x, 2, 2, "__or__"); return binop(B_OR, unpay(A(0)), unpay(A(1))); }
BI(num_ror) { need(x, 2, 2, "__ror__"); return binop(B_OR, unpay(A(1)), unpay(A(0))); }
BI(num_xor) { need(x, 2, 2, "__xor__"); return binop(B_XOR, unpay(A(0)), unpay(A(1))); }
BI(num_rxor) { need(x, 2, 2, "__rxor__"); return binop(B_XOR, unpay(A(1)), unpay(A(0))); }
BI(num_lshift) { need(x, 2, 2, "__lshift__"); return binop(B_LSH, unpay(A(0)), unpay(A(1))); }
BI(num_rlshift) { need(x, 2, 2, "__rlshift__"); return binop(B_LSH, unpay(A(1)), unpay(A(0))); }
BI(num_rshift) { need(x, 2, 2, "__rshift__"); return binop(B_RSH, unpay(A(0)), unpay(A(1))); }
BI(num_rrshift) { need(x, 2, 2, "__rrshift__"); return binop(B_RSH, unpay(A(1)), unpay(A(0))); }
BI(num_neg) { return unop('-', unpay(A(0))); }
BI(num_pos) { return unop('+', unpay(A(0))); }
BI(num_invert) { return unop('~', unpay(A(0))); }
BI(num_abs) { Value v = unpay(A(0)); return rich_cmp(C_LT, v, mkint(0)) ? unop('-', v) : unop('+', v); }
BI(num_conj) { return unpay(A(0)); }
BI(nat_format)
{
    need(x, 2, 2, "__format__");
    return format_value(A(0), astr(A(1), "format_spec")->s, AS_STR(A(1))->len);
}
BI(list_init)
{
    List *l = self_list(x);
    need(x, 1, 2, "__init__");
    l->n = 0;
    if (x->n > 1) {
        Value it, y;
        VPUSH(A(1));
        it = get_iter(A(1));
        VPUSH(it);
        while (iter_next(it, &y)) list_push(l, y);
        vsp -= 2;
    }
    return NONE_V;
}
BI(set_init)
{
    Dict *d = self_set(x);
    need(x, 1, 2, "__init__");
    dict_clear(d);
    if (x->n > 1) {
        Value it, y;
        VPUSH(A(1));
        it = get_iter(A(1));
        VPUSH(it);
        while (iter_next(it, &y)) set_add(d, y);
        vsp -= 2;
    }
    return NONE_V;
}
BI(obj_setattr)
{
    need(x, 3, 3, "__setattr__");
    if (A(0).t != T_INST) throw_new(cls_TypeError, "can't apply this __setattr__ to a non-instance");
    amap_set(&AS_INST(A(0))->a, intern_str(astr(A(1), "attribute name")), A(2));
    return NONE_V;
}
BI(bi_slice)
{
    need(x, 1, 3, "slice");
    if (x->n == 1) return mkslice(NONE_V, A(0), NONE_V);
    return mkslice(A(0), A(1), x->n > 2 ? A(2) : NONE_V);
}
BI(sl_indices)
{
    List *sl;
    long start, stop, step, cnt;
    Value r[3];
    need(x, 2, 2, "indices");
    sl = AS_LIST(A(0));
    slice_adjust(sl->a[0], sl->a[1], sl->a[2], to_index(A(1)), &start, &stop, &step, &cnt);
    r[0] = mkint(start); r[1] = mkint(stop); r[2] = mkint(step);
    return mktuple_from(r, 3);
}

BI(s_isidentifier)
{
    Str *s = SELF;
    long i;
    if (s->len == 0 || ch_digit[(unsigned char)s->s[0]]) return FALSE_V;
    for (i = 0; i < s->len; i++)
        if (!ch_alnum[(unsigned char)s->s[i]] && (unsigned char)s->s[i] < 128) return FALSE_V;
    return TRUE_V;
}
BI(s_istitle)
{
    Str *s = SELF;
    long i;
    int prev = 0, any = 0;
    for (i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->s[i];
        if (ch_upper[c]) { if (prev) return FALSE_V; prev = 1; any = 1; }
        else if (ch_lower[c]) { if (!prev) return FALSE_V; }
        else prev = 0;
    }
    return mkbool(any);
}
BI(s_isprintable)
{
    Str *s = SELF;
    long i;
    for (i = 0; i < s->len; i++)
        if ((unsigned char)s->s[i] < 32 || (unsigned char)s->s[i] == 127) return FALSE_V;
    return TRUE_V;
}
BI(s_isascii)
{
    Str *s = SELF;
    long i;
    for (i = 0; i < s->len; i++) if ((unsigned char)s->s[i] >= 128) return FALSE_V;
    return TRUE_V;
}
BI(notimpl_repr)
{
    (void)x;
    return mkcstr("NotImplemented");
}
BI(ellipsis_repr)
{
    (void)x;
    return mkcstr("Ellipsis");
}

/* ---- bytes ------------------------------------------------------------ */
BI(bi_bytes)
{
    Value v;
    Buf b;
    long i;
    if (x->n == 0) return mkbytes("", 0);
    v = A(0);
    if (v.t == T_BYTES) return v;
    if (v.t == T_STR) {
        Args a2;
        Value av[2];
        if (x->n < 2) throw_new(cls_TypeError, "string argument without an encoding");
        av[0] = v; av[1] = A(1);
        a2.n = 2; a2.a = av; a2.nk = 0; a2.kw = NULL;
        return s_encode(&a2);
    }
    buf_init(&b);
    if (v.t == T_INT) {
        if (v.u.i < 0) { buf_free(&b); throw_new(cls_ValueError, "negative count"); }
        for (i = 0; i < v.u.i; i++) buf_addc(&b, 0);
    } else {
        Value it, y;
        it = get_iter(v);
        VPUSH(it);
        while (iter_next(it, &y)) {
            if (y.t != T_INT && y.t != T_BOOL) { buf_free(&b); throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer", type_name(y)); }
            if (y.u.i < 0 || y.u.i > 255) { buf_free(&b); throw_new(cls_ValueError, "bytes must be in range(0, 256)"); }
            buf_addc(&b, (int)y.u.i);
        }
        vsp--;
    }
    v = mkbytes(b.s, b.n);
    buf_free(&b);
    return v;
}
static Str *abytes(Value v)
{
    if (v.t != T_BYTES) throw_new(cls_TypeError, "descriptor requires a 'bytes' object but received '%.40s'", type_name(v));
    return AS_STR(v);
}
BI(b_decode)
{
    Str *s = abytes(A(0));
    Value ev = x->n > 1 ? A(1) : kwget(x, "encoding");
    Value errv = x->n > 2 ? A(2) : kwget(x, "errors");
    const char *enc = "utf-8", *err = "strict";
    Buf b;
    long i;
    if (ev.t == T_STR) enc = AS_STR(ev)->s;
    if (errv.t == T_STR) err = AS_STR(errv)->s;
    if (strcmp(enc, "latin-1") == 0 || strcmp(enc, "latin1") == 0 || strcmp(enc, "iso-8859-1") == 0) {
        buf_init(&b);
        for (i = 0; i < s->len; i++) put_utf8(&b, (unsigned char)s->s[i]);
        { Value r = buf_value(&b); return r; }
    }
    if (strcmp(enc, "ascii") == 0 || strcmp(enc, "us-ascii") == 0) {
        for (i = 0; i < s->len; i++)
            if ((unsigned char)s->s[i] >= 128 && strcmp(err, "ignore") != 0 && strcmp(err, "replace") != 0)
                throw_new(cls_ValueError, "'ascii' codec can't decode byte 0x%02x in position %ld: ordinal not in range(128)",
                          (unsigned char)s->s[i], i);
    }
    return mkstr(s->s, s->len);
}
BI(b_hex)
{
    Str *s = abytes(A(0));
    Buf b;
    long i;
    char t[4];
    buf_init(&b);
    for (i = 0; i < s->len; i++) { sprintf(t, "%02x", (unsigned char)s->s[i]); buf_add(&b, t); }
    return buf_value(&b);
}
BI(b_fromhex)
{
    Str *s;
    Buf b;
    long i;
    int hi = -1;
    need(x, 1, 2, "fromhex");
    s = astr(A(x->n - 1), "fromhex() argument");
    buf_init(&b);
    for (i = 0; i < s->len; i++) {
        int c = (unsigned char)s->s[i], d;
        if (c == ' ') continue;
        if (!ch_hex[c]) { buf_free(&b); throw_new(cls_ValueError, "non-hexadecimal number found in fromhex() arg at position %ld", i); }
        d = digit_val(c);
        if (hi < 0) hi = d; else { buf_addc(&b, hi * 16 + d); hi = -1; }
    }
    if (hi >= 0) { buf_free(&b); throw_new(cls_ValueError, "non-hexadecimal number found in fromhex() arg at position %ld", s->len); }
    { Value r = mkbytes(b.s, b.n); buf_free(&b); return r; }
}
static Value b2s_back(Value r)
{
    if (r.t == T_STR) return mkbytes(AS_STR(r)->s, AS_STR(r)->len);
    if (r.t == T_LIST || r.t == T_TUPLE) {
        long i;
        for (i = 0; i < AS_LIST(r)->n; i++)
            if (AS_LIST(r)->a[i].t == T_STR)
                AS_LIST(r)->a[i] = mkbytes(AS_STR(AS_LIST(r)->a[i])->s, AS_STR(AS_LIST(r)->a[i])->len);
    }
    return r;
}
/* run a str method on bytes arguments (bytes become temporary str values) */
static Value via_str(Args *x, BFn fn)
{
    Value *av = (Value *)xmalloc(sizeof(Value) * (size_t)(x->n + 1));
    Args a2;
    Value r;
    int i;
    for (i = 0; i < x->n; i++) {
        if (x->a[i].t == T_BYTES) av[i] = mkstr(AS_STR(x->a[i])->s, AS_STR(x->a[i])->len);
        else if (x->a[i].t == T_LIST || x->a[i].t == T_TUPLE) {
            Value l = iter_to_list(x->a[i]);
            long k;
            for (k = 0; k < AS_LIST(l)->n; k++)
                if (AS_LIST(l)->a[k].t == T_BYTES)
                    AS_LIST(l)->a[k] = mkstr(AS_STR(AS_LIST(l)->a[k])->s, AS_STR(AS_LIST(l)->a[k])->len);
            av[i] = l;
        } else av[i] = x->a[i];
    }
    a2.n = x->n; a2.a = av; a2.nk = x->nk; a2.kw = x->kw;
    for (i = 0; i < x->n; i++) VPUSH(av[i]);
    r = fn(&a2);
    vsp -= x->n;
    free(av);
    return b2s_back(r);
}
#define BVIA(name, fn) BI(name) { return via_str(x, fn); }
BVIA(b_upper, s_upper) BVIA(b_lower, s_lower) BVIA(b_strip, s_strip) BVIA(b_lstrip, s_lstrip)
BVIA(b_rstrip, s_rstrip) BVIA(b_split, s_split) BVIA(b_join, s_join) BVIA(b_replace, s_replace)
BVIA(b_find, s_find) BVIA(b_count, s_count) BVIA(b_startswith, s_startswith)
BVIA(b_endswith, s_endswith) BVIA(b_rfind, s_rfind) BVIA(b_index, s_index)
BVIA(b_splitlines, s_splitlines) BVIA(b_isdigit, s_isdigit) BVIA(b_isalpha, s_isalpha)
BI(i_to_bytes)
{
    Value v = A(0), kv;
    long n, i;
    int big = 1, sg = 0;
    Buf b;
    need(x, 1, 3, "to_bytes");
    n = x->n > 1 ? to_index(A(1)) : (kv = kwget(x, "length"), kv.t == T_UNSET ? 1 : to_index(kv));
    kv = x->n > 2 ? A(2) : kwget(x, "byteorder");
    if (kv.t == T_STR && strcmp(AS_STR(kv)->s, "little") == 0) big = 0;
    kv = kwget(x, "signed");
    if (kv.t != T_UNSET && py_truthy(kv)) sg = 1;
    if (int_sign(v) < 0) {
        if (!sg) throw_new(cls_OverflowError, "can't convert negative int to unsigned");
        v = int_add(int_pow(mkint(256), n), v);
    }
    buf_init(&b);
    for (i = 0; i < n; i++) buf_addc(&b, 0);
    for (i = 0; i < n; i++) {
        Value q, r;
        int_divmod(v, mkint(256), &q, &r);
        b.s[big ? n - 1 - i : i] = (char)r.u.i;
        v = q;
    }
    if (int_sign(v) != 0) { buf_free(&b); throw_new(cls_OverflowError, "int too big to convert"); }
    { Value r = mkbytes(b.s, b.n); buf_free(&b); return r; }
}
BI(i_from_bytes)
{
    Value src = A(0), kv, r = mkint(0);
    Str *s;
    long i;
    int big = 1, sg = 0;
    need(x, 1, 2, "from_bytes");
    if (src.t != T_BYTES) src = bi_bytes(x);
    s = AS_STR(src);
    kv = x->n > 1 ? A(1) : kwget(x, "byteorder");
    if (kv.t == T_STR && strcmp(AS_STR(kv)->s, "little") == 0) big = 0;
    kv = kwget(x, "signed");
    if (kv.t != T_UNSET && py_truthy(kv)) sg = 1;
    for (i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->s[big ? i : s->len - 1 - i];
        r = int_add(int_mul(r, mkint(256)), mkint(c));
    }
    if (sg && s->len > 0 && ((unsigned char)s->s[big ? 0 : s->len - 1] & 128))
        r = int_sub(r, int_pow(mkint(256), s->len));
    return r;
}
/* ===== p7_np.c ===== */
/* =========================================================================
   p7_np.c   numpy subset: native ndarray (bool/int64/float64, N-d, row-major,
             contiguous copies), indexing, broadcasting, reductions, printing
   ========================================================================= */
enum {
    OP_MAX = 40, OP_MIN = 41, OP_CMP = 50       /* OP_CMP + C_LT .. OP_CMP + C_NE */
};
enum {
    R_SUM, R_PROD, R_MIN, R_MAX, R_MEAN, R_ARGMIN, R_ARGMAX, R_ANY, R_ALL, R_STD, R_VAR,
    R_CUMSUM, R_CUMPROD
};
enum {
    F_SQRT, F_EXP, F_SIN, F_COS, F_TAN, F_LOG, F_LOG10, F_LOG2, F_FLOOR, F_CEIL, F_TANH,
    F_SINH, F_COSH, F_ASIN, F_ACOS, F_ATAN, F_ABS, F_SIGN, F_SQUARE, F_ISNAN, F_ISINF,
    F_ISFINITE, F_NEG
};

static Module *new_module(const char *name);
static void mod_def(Module *m, const char *name, BFn fn);
static void mod_val(Module *m, const char *name, Value v);
static void np_init2(Module *m);
static void la_init(Module *np);
static void rnd_init(Module *np);

typedef struct NdTmp { Nd nd; long i; double d; } NdTmp;

/* ---- construction ----------------------------------------------------- */
static Nd *nd_new(int dt, int nd, const long *shape)
{
    Nd *a = (Nd *)gc_alloc(sizeof(Nd), T_ND);
    int i;
    long n = 1;
    a->dt = dt; a->nd = nd;
    for (i = 0; i < ND_MAXD; i++) a->shape[i] = 0;
    for (i = 0; i < nd; i++) { a->shape[i] = shape[i]; n *= shape[i]; }
    a->size = n;
    a->iv = NULL; a->dv = NULL;
    if (dt == ND_FLOAT) a->dv = (double *)xmalloc(sizeof(double) * (size_t)n);
    else a->iv = (long *)xmalloc(sizeof(long) * (size_t)n);
    GC_ACCOUNT(8L * n);
    return a;
}
static Nd *nd_new1(int dt, long n)
{
    return nd_new(dt, 1, &n);
}
static Value ndv(Nd *a)
{
    return mkobj(a, T_ND);
}
static Value nd_mkint(long v)
{
    if (v <= SI_MAX && v >= -SI_MAX) return mkint(v);
    return int_from_double((double)v);
}
static Value nd_elem(const Nd *a, long i)
{
    if (a->dt == ND_FLOAT) return mkfloat(a->dv[i]);
    if (a->dt == ND_BOOL) return tobool(a->iv[i]);
    return nd_mkint(a->iv[i]);
}
static double nd_d(const Nd *a, long i)
{
    return a->dt == ND_FLOAT ? a->dv[i] : (double)a->iv[i];
}
static Value nd_ret(Nd *r)
{
    if (r->nd == 0) return nd_elem(r, 0);
    return ndv(r);
}
static void nd_cp(Nd *d, long doff, const Nd *s, long soff, long cnt)
{
    long i;
    if (d->dt == ND_FLOAT) {
        if (s->dt == ND_FLOAT) memmove(d->dv + doff, s->dv + soff, sizeof(double) * (size_t)cnt);
        else for (i = 0; i < cnt; i++) d->dv[doff + i] = (double)s->iv[soff + i];
    } else if (d->dt == ND_BOOL) {
        if (s->dt == ND_FLOAT) for (i = 0; i < cnt; i++) d->iv[doff + i] = s->dv[soff + i] != 0;
        else for (i = 0; i < cnt; i++) d->iv[doff + i] = s->iv[soff + i] != 0;
    } else {
        if (s->dt == ND_FLOAT) for (i = 0; i < cnt; i++) d->iv[doff + i] = (long)s->dv[soff + i];
        else memmove(d->iv + doff, s->iv + soff, sizeof(long) * (size_t)cnt);
    }
}
static Nd *nd_astype(const Nd *a, int dt)
{
    Nd *r = nd_new(dt, a->nd, a->shape);
    nd_cp(r, 0, a, 0, a->size);
    return r;
}
static Nd *nd_to_float(Nd *a)
{
    if (a->dt == ND_FLOAT) return a;
    return nd_astype(a, ND_FLOAT);
}
static Nd *nd_reshaped(const Nd *a, int nd, const long *shape)
{
    Nd *r = nd_new(a->dt, nd, shape);
    nd_cp(r, 0, a, 0, a->size);
    return r;
}
static void shape_str(int nd, const long *shape, char *out)
{
    int k;
    char t[32];
    strcpy(out, "(");
    for (k = 0; k < nd; k++) {
        sprintf(t, k ? ",%ld" : "%ld", shape[k]);
        strcat(out, t);
    }
    if (nd == 1) strcat(out, ",");
    strcat(out, ")");
}

/* ---- python values -> arrays ------------------------------------------ */
static int py_dt(Value v)
{
    switch (v.t) {
    case T_BOOL: return ND_BOOL;
    case T_INT: case T_BIG: return ND_INT;
    case T_FLOAT: return ND_FLOAT;
    default: return -1;
    }
}
static int seq_len(Value v, long *n)
{
    if (v.t == T_LIST || v.t == T_TUPLE) { *n = AS_LIST(v)->n; return 1; }
    if (v.t == T_ND && AS_ND(v)->nd >= 1) { *n = AS_ND(v)->shape[0]; return 1; }
    return 0;
}
static void nd_scan(Value v, int depth, long *shape, int *ndim, int *dt)
{
    long n, i;
    if (seq_len(v, &n)) {
        if (depth >= ND_MAXD || (*ndim >= 0 && depth >= *ndim))
            throw_new(cls_ValueError, "setting an array element with a sequence. The requested "
                      "array has an inhomogeneous shape");
        if (shape[depth] < 0) shape[depth] = n;
        else if (shape[depth] != n)
            throw_new(cls_ValueError, "setting an array element with a sequence. The requested "
                      "array has an inhomogeneous shape");
        if (n == 0 && *ndim < 0) *ndim = depth + 1;
        for (i = 0; i < n; i++) {
            Value it = v.t == T_ND ? nd_item(v, i) : AS_LIST(v)->a[i];
            nd_scan(it, depth + 1, shape, ndim, dt);
        }
    } else {
        int d = py_dt(v);
        if (d < 0)
            throw_new(cls_TypeError, "unsupported array element of type '%.40s'", type_name(v));
        if (*ndim < 0) *ndim = depth;
        else if (*ndim != depth)
            throw_new(cls_ValueError, "setting an array element with a sequence. The requested "
                      "array has an inhomogeneous shape");
        if (d > *dt) *dt = d;
    }
}
static void nd_fill(Nd *r, Value v, long *pos)
{
    long n, i;
    if (seq_len(v, &n)) {
        for (i = 0; i < n; i++)
            nd_fill(r, v.t == T_ND ? nd_item(v, i) : AS_LIST(v)->a[i], pos);
        return;
    }
    if (r->dt == ND_FLOAT) r->dv[*pos] = to_dbl(v);
    else if (r->dt == ND_BOOL) r->iv[*pos] = v.t == T_FLOAT ? v.u.d != 0 : (v.u.i != 0);
    else r->iv[*pos] = v.t == T_FLOAT ? (long)v.u.d : (v.t == T_BIG ? 0 : v.u.i);
    (*pos)++;
}
static int parse_dtype(Value v)
{
    if (v.t == T_CLASS) {
        if (AS_CLASS(v) == cls_float) return ND_FLOAT;
        if (AS_CLASS(v) == cls_int) return ND_INT;
        if (AS_CLASS(v) == cls_bool) return ND_BOOL;
    } else if (v.t == T_STR) {
        const char *s = AS_STR(v)->s;
        if (!strcmp(s, "float64") || !strcmp(s, "float") || !strcmp(s, "f8") ||
            !strcmp(s, "float32") || !strcmp(s, "d") || !strcmp(s, "f4")) return ND_FLOAT;
        if (!strcmp(s, "int64") || !strcmp(s, "int") || !strcmp(s, "i8") ||
            !strcmp(s, "int32") || !strcmp(s, "i4") || !strcmp(s, "int16") ||
            !strcmp(s, "int8") || !strcmp(s, "uint8")) return ND_INT;
        if (!strcmp(s, "bool") || !strcmp(s, "?") || !strcmp(s, "bool_")) return ND_BOOL;
    }
    throw_new(cls_TypeError, "data type not understood");
    return 0;
}
static const char *dt_name(int dt)
{
    return dt == ND_FLOAT ? "float64" : (dt == ND_INT ? "int64" : "bool");
}
/* build an array from a python scalar / nested sequence / array (always a fresh copy) */
static Nd *nd_from_value(Value v, int force)
{
    long shape[ND_MAXD];
    int ndim = -1, dt = ND_BOOL, k;
    long pos = 0;
    Nd *r;
    if (v.t == T_ND) return nd_astype(AS_ND(v), force >= 0 ? force : AS_ND(v)->dt);
    for (k = 0; k < ND_MAXD; k++) shape[k] = -1;
    nd_scan(v, 0, shape, &ndim, &dt);
    if (ndim < 0) ndim = 0;
    {
        long n = 1;
        for (k = 0; k < ndim; k++) n *= shape[k];
        if (n == 0) dt = ND_FLOAT;
    }
    if (force >= 0) dt = force;
    r = nd_new(dt, ndim, shape);
    nd_fill(r, v, &pos);
    return r;
}
static Nd *nd_coerce(Value v, NdTmp *t)
{
    if (v.t == T_ND) return AS_ND(v);
    if (v.t == T_LIST || v.t == T_TUPLE) return nd_from_value(v, -1);
    if (py_dt(v) < 0) throw_new(cls_TypeError, "unsupported operand type '%.40s'", type_name(v));
    memset(&t->nd, 0, sizeof(Nd));
    t->nd.nd = 0; t->nd.size = 1;
    if (v.t == T_FLOAT || v.t == T_BIG) {
        t->nd.dt = ND_FLOAT; t->d = to_dbl(v); t->nd.dv = &t->d;
    } else {
        t->nd.dt = v.t == T_BOOL ? ND_BOOL : ND_INT; t->i = v.u.i; t->nd.iv = &t->i;
    }
    return &t->nd;
}
static Nd *nd_arg0(Args *x, NdTmp *t)
{
    if (x->n < 1) throw_new(cls_TypeError, "missing array argument");
    return nd_coerce(A(0), t);
}

/* ---- broadcasting and elementwise operations -------------------------- */
static int nd_bshape(const Nd *a, const Nd *b, long *rs)
{
    int rnd = a->nd > b->nd ? a->nd : b->nd, k;
    for (k = 0; k < rnd; k++) {
        int ka = k - (rnd - a->nd), kb = k - (rnd - b->nd);
        long da = ka >= 0 ? a->shape[ka] : 1, db = kb >= 0 ? b->shape[kb] : 1;
        if (da == db || db == 1) rs[k] = da;
        else if (da == 1) rs[k] = db;
        else {
            char s1[160], s2[160];
            shape_str(a->nd, a->shape, s1);
            shape_str(b->nd, b->shape, s2);
            throw_new(cls_ValueError, "operands could not be broadcast together with shapes "
                      "%s %s ", s1, s2);
        }
    }
    return rnd;
}
static void nd_bstrides(const Nd *a, int rnd, long *st)
{
    long s = 1;
    int k, off = rnd - a->nd;
    for (k = rnd - 1; k >= 0; k--) {
        int ak = k - off;
        if (ak < 0) st[k] = 0;
        else { st[k] = a->shape[ak] == 1 ? 0 : s; s *= a->shape[ak]; }
    }
}
static void nd_odo(long *idx, int n, const long *rs, const long *sa, const long *sb,
                   long *oa, long *ob)
{
    int k = n - 1;
    while (k >= 0) {
        idx[k]++;
        *oa += sa[k]; *ob += sb[k];
        if (idx[k] < rs[k]) return;
        *oa -= sa[k] * rs[k]; *ob -= sb[k] * rs[k];
        idx[k] = 0;
        k--;
    }
}
static long nd_ifloordiv(long x, long y)
{
    long q;
    if (y == 0) return 0;
    if (y == -1) return (long)(0UL - (unsigned long)x);
    q = x / y;
    if ((x % y != 0) && ((x < 0) != (y < 0))) q--;
    return q;
}
static long nd_imod(long x, long y)
{
    long m;
    if (y == 0 || y == -1) return 0;
    m = x % y;
    if (m != 0 && ((m < 0) != (y < 0))) m += y;
    return m;
}
static long nd_ipow(long x, long y)
{
    unsigned long r = 1, b = (unsigned long)x;
    if (y < 0) throw_new(cls_ValueError, "Integers to negative integer powers are not allowed.");
    while (y > 0) {
        if (y & 1) r *= b;
        b *= b;
        y >>= 1;
    }
    return (long)r;
}
static double nd_fdiv(double x, double y)
{
    if (y == 0) return x / y;
    return fl_floordiv(x, y);
}
static double nd_fmod(double x, double y)
{
    if (y == 0) return x - x;
    return fl_mod(x, y);
}
static double nd_rint(double x)
{
    double r, d;
    if (x != x || x > 1e300 || x < -1e300) return x;
    r = floor(x);
    d = x - r;
    if (d > 0.5) r += 1.0;
    else if (d == 0.5 && fmod(r, 2.0) != 0) r += 1.0;
    if (r == 0 && x < 0) r = -0.0;
    return r;
}

#define NDLOOP(RT, AT, BT, EXPR) \
    do { \
        RT *rp_ = (RT *)rptr; const AT *ap_ = (const AT *)aptr; const BT *bp_ = (const BT *)bptr; \
        long ii_, jj_, oa_ = 0, ob_ = 0; \
        for (k = 0; k < ND_MAXD; k++) idx[k] = 0; \
        for (ii_ = 0; ii_ < nouter; ii_++) { \
            for (jj_ = 0; jj_ < inner; jj_++) { \
                AT x = ap_[oa_ + jj_ * isa]; BT y = bp_[ob_ + jj_ * isb]; \
                *rp_++ = (RT)(EXPR); \
            } \
            nd_odo(idx, rnd - 1, rs, sa, sb, &oa_, &ob_); \
        } \
    } while (0)

static Value nd_binary(int op, Nd *a, Nd *b)
{
    long rs[ND_MAXD], sa[ND_MAXD], sb[ND_MAXD], idx[ND_MAXD];
    long inner, nouter, isa, isb, n;
    int rnd, kd, rdt, k, cmp = op >= OP_CMP, boolres = 0;
    Nd *r;
    void *rptr, *aptr, *bptr;
    rnd = nd_bshape(a, b, rs);
    kd = a->dt > b->dt ? a->dt : b->dt;
    if (cmp) { rdt = ND_BOOL; if (kd == ND_BOOL) kd = ND_INT; }
    else {
        switch (op) {
        case B_DIV: kd = ND_FLOAT; rdt = ND_FLOAT; break;
        case B_AND: case B_OR: case B_XOR: case B_LSH: case B_RSH:
            if (kd == ND_FLOAT)
                throw_new(cls_TypeError, "ufunc bitwise operation not supported for float inputs");
            rdt = (a->dt == ND_BOOL && b->dt == ND_BOOL) ? ND_BOOL : ND_INT;
            kd = ND_INT;
            break;
        default:
            if (kd == ND_BOOL) {
                kd = ND_INT;
                rdt = (op == B_ADD || op == B_MUL || op == OP_MAX || op == OP_MIN) ? ND_BOOL : ND_INT;
                boolres = rdt == ND_BOOL;
            } else rdt = kd;
            break;
        }
    }
    if (kd == ND_FLOAT) { a = nd_to_float(a); b = nd_to_float(b); }
    else {
        if (a->dt == ND_FLOAT) a = nd_astype(a, ND_INT);
        if (b->dt == ND_FLOAT) b = nd_astype(b, ND_INT);
    }
    r = nd_new(rdt, rnd, rs);
    if (r->size == 0) return nd_ret(r);
    nd_bstrides(a, rnd, sa);
    nd_bstrides(b, rnd, sb);
    n = r->size;
    if (rnd == 0) { inner = 1; isa = isb = 0; }
    else { inner = rs[rnd - 1]; isa = sa[rnd - 1]; isb = sb[rnd - 1]; }
    nouter = n / inner;
    rptr = rdt == ND_FLOAT ? (void *)r->dv : (void *)r->iv;
    aptr = kd == ND_FLOAT ? (void *)a->dv : (void *)a->iv;
    bptr = kd == ND_FLOAT ? (void *)b->dv : (void *)b->iv;
    if (cmp) {
        int c = op - OP_CMP;
        if (kd == ND_FLOAT) {
            switch (c) {
            case C_LT: NDLOOP(long, double, double, x < y); break;
            case C_LE: NDLOOP(long, double, double, x <= y); break;
            case C_GT: NDLOOP(long, double, double, x > y); break;
            case C_GE: NDLOOP(long, double, double, x >= y); break;
            case C_EQ: NDLOOP(long, double, double, x == y); break;
            default: NDLOOP(long, double, double, x != y); break;
            }
        } else {
            switch (c) {
            case C_LT: NDLOOP(long, long, long, x < y); break;
            case C_LE: NDLOOP(long, long, long, x <= y); break;
            case C_GT: NDLOOP(long, long, long, x > y); break;
            case C_GE: NDLOOP(long, long, long, x >= y); break;
            case C_EQ: NDLOOP(long, long, long, x == y); break;
            default: NDLOOP(long, long, long, x != y); break;
            }
        }
    } else if (kd == ND_FLOAT) {
        switch (op) {
        case B_ADD: NDLOOP(double, double, double, x + y); break;
        case B_SUB: NDLOOP(double, double, double, x - y); break;
        case B_MUL: NDLOOP(double, double, double, x * y); break;
        case B_DIV: NDLOOP(double, double, double, x / y); break;
        case B_FDIV: NDLOOP(double, double, double, nd_fdiv(x, y)); break;
        case B_MOD: NDLOOP(double, double, double, nd_fmod(x, y)); break;
        case B_POW: NDLOOP(double, double, double, pow(x, y)); break;
        case OP_MAX: NDLOOP(double, double, double, (x != x || x > y) ? x : y); break;
        case OP_MIN: NDLOOP(double, double, double, (x != x || x < y) ? x : y); break;
        default: throw_new(cls_TypeError, "unsupported operand type(s) for %s: 'ndarray' and "
                           "'ndarray'", op_str(op));
        }
    } else {
        switch (op) {
        case B_ADD: NDLOOP(long, long, long, (long)((unsigned long)x + (unsigned long)y)); break;
        case B_SUB: NDLOOP(long, long, long, (long)((unsigned long)x - (unsigned long)y)); break;
        case B_MUL: NDLOOP(long, long, long, (long)((unsigned long)x * (unsigned long)y)); break;
        case B_FDIV: NDLOOP(long, long, long, nd_ifloordiv(x, y)); break;
        case B_MOD: NDLOOP(long, long, long, nd_imod(x, y)); break;
        case B_POW: NDLOOP(long, long, long, nd_ipow(x, y)); break;
        case B_AND: NDLOOP(long, long, long, x & y); break;
        case B_OR: NDLOOP(long, long, long, x | y); break;
        case B_XOR: NDLOOP(long, long, long, x ^ y); break;
        case B_LSH: NDLOOP(long, long, long, (y < 0 || y > 62) ? 0 : (long)((unsigned long)x << y));
            break;
        case B_RSH: NDLOOP(long, long, long, (y < 0 || y > 62) ? (x < 0 ? -1 : 0) : (x >> y));
            break;
        case OP_MAX: NDLOOP(long, long, long, x > y ? x : y); break;
        case OP_MIN: NDLOOP(long, long, long, x < y ? x : y); break;
        default: throw_new(cls_TypeError, "unsupported operand type(s) for %s: 'ndarray' and "
                           "'ndarray'", op_str(op));
        }
    }
    if (boolres) for (n = 0; n < r->size; n++) r->iv[n] = r->iv[n] != 0;
    return nd_ret(r);
}
static Value nd_matmul(Nd *a, Nd *b, int isdot);
static Value nd_binop(int op, Value a, Value b)
{
    NdTmp ta, tb;
    Nd *x, *y;
    if (a.t != T_ND && a.t != T_LIST && a.t != T_TUPLE && py_dt(a) < 0)
        throw_new(cls_TypeError, "unsupported operand type(s) for %s: '%.40s' and '%.40s'",
                  op_str(op), type_name(a), type_name(b));
    x = nd_coerce(a, &ta);
    y = nd_coerce(b, &tb);
    if (op == B_MATMUL) return nd_matmul(x, y, 0);
    return nd_binary(op, x, y);
}
static Value nd_compare(int op, Value a, Value b)
{
    NdTmp ta, tb;
    Nd *x, *y;
    x = nd_coerce(a, &ta);
    y = nd_coerce(b, &tb);
    return nd_binary(OP_CMP + op, x, y);
}

/* ---- unary operations and math functions ------------------------------ */
static double nd_f1(int fn, double x)
{
    switch (fn) {
    case F_SQRT: return sqrt(x);
    case F_EXP: return exp(x);
    case F_SIN: return sin(x);
    case F_COS: return cos(x);
    case F_TAN: return tan(x);
    case F_LOG: return log(x);
    case F_LOG10: return log10(x);
    case F_LOG2: return log(x) / log(2.0);
    case F_FLOOR: return floor(x);
    case F_CEIL: return ceil(x);
    case F_TANH: return tanh(x);
    case F_SINH: return sinh(x);
    case F_COSH: return cosh(x);
    case F_ASIN: return asin(x);
    case F_ACOS: return acos(x);
    case F_ATAN: return atan(x);
    default: return x;
    }
}
static Value nd_ufunc1(int fn, Nd *a)
{
    long i;
    Nd *r;
    if (fn == F_ISNAN || fn == F_ISINF || fn == F_ISFINITE) {
        r = nd_new(ND_BOOL, a->nd, a->shape);
        for (i = 0; i < a->size; i++) {
            double d = a->dt == ND_FLOAT ? a->dv[i] : 0.0;
            if (fn == F_ISNAN) r->iv[i] = d != d;
            else if (fn == F_ISINF) r->iv[i] = (d == d) && (d - d != 0);
            else r->iv[i] = (d == d) && (d - d == 0);
        }
        return nd_ret(r);
    }
    if (fn == F_ABS || fn == F_SIGN || fn == F_SQUARE || fn == F_NEG) {
        int dt = a->dt;
        if (fn == F_NEG && dt == ND_BOOL) dt = ND_INT;
        if (fn == F_SQUARE && dt == ND_BOOL) dt = ND_INT;
        r = nd_new(dt, a->nd, a->shape);
        for (i = 0; i < a->size; i++) {
            if (dt == ND_FLOAT) {
                double d = a->dv[i];
                r->dv[i] = fn == F_ABS ? fabs(d) : (fn == F_NEG ? -d : (fn == F_SQUARE ? d * d :
                           (d > 0 ? 1.0 : (d < 0 ? -1.0 : d))));
            } else {
                long v = a->iv[i];
                r->iv[i] = fn == F_ABS ? (v < 0 ? -v : v) : (fn == F_NEG ? -v :
                           (fn == F_SQUARE ? v * v : (v > 0 ? 1 : (v < 0 ? -1 : 0))));
            }
        }
        return nd_ret(r);
    }
    r = nd_new(ND_FLOAT, a->nd, a->shape);
    for (i = 0; i < a->size; i++) r->dv[i] = nd_f1(fn, a->dt == ND_FLOAT ? a->dv[i] : (double)a->iv[i]);
    return nd_ret(r);
}
static Value nd_unop(int op, Value av)
{
    Nd *a = AS_ND(av);
    long i;
    Nd *r;
    if (op == '-') return nd_ufunc1(F_NEG, a);
    if (op == '+') return av;
    if (a->dt == ND_FLOAT) throw_new(cls_TypeError, "ufunc 'invert' not supported for the input types");
    r = nd_new(a->dt, a->nd, a->shape);
    for (i = 0; i < a->size; i++) r->iv[i] = a->dt == ND_BOOL ? !a->iv[i] : ~a->iv[i];
    return ndv(r);
}

/* ---- reductions ------------------------------------------------------- */
static double pw_sum(const double *a, long n)
{
    long i;
    double res;
    if (n < 8) {
        res = 0.0;
        for (i = 0; i < n; i++) res += a[i];
        return res;
    } else if (n <= 128) {
        double r[8];
        for (i = 0; i < 8; i++) r[i] = a[i];
        for (i = 8; i < n - (n % 8); i += 8) {
            r[0] += a[i + 0]; r[1] += a[i + 1]; r[2] += a[i + 2]; r[3] += a[i + 3];
            r[4] += a[i + 4]; r[5] += a[i + 5]; r[6] += a[i + 6]; r[7] += a[i + 7];
        }
        res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; i++) res += a[i];
        return res;
    } else {
        long n2 = n / 2;
        n2 -= n2 % 8;
        return pw_sum(a, n2) + pw_sum(a + n2, n - n2);
    }
}
static int axis_norm(long ax, int nd)
{
    if (ax < -nd || ax >= nd)
        throw_new(cls_ValueError, "axis %ld is out of bounds for array of dimension %d", ax, nd);
    return (int)(ax < 0 ? ax + nd : ax);
}
/* axis argument at position pos or keyword; returns 1 and the axis if given */
static int get_axis(Args *x, int pos, Nd *a, int *axis)
{
    Value v = x->n > pos ? A(pos) : kwget(x, "axis");
    if (v.t == T_UNSET || v.t == T_NONE) return 0;
    *axis = axis_norm(need_long(v, "axis"), a->nd);
    return 1;
}
static Value nd_reduce(int kind, Nd *a, int has_axis, int axis, long ddof)
{
    long outer = 1, n, inner = 1, o, i, t, rs[ND_MAXD];
    int rnd = 0, k, rdt;
    Nd *r;
    double *tmp = NULL;
    if (kind == R_MEAN || kind == R_STD || kind == R_VAR) a = nd_to_float(a);
    if (!has_axis) n = a->size;
    else {
        for (k = 0; k < axis; k++) outer *= a->shape[k];
        n = a->shape[axis];
        for (k = axis + 1; k < a->nd; k++) inner *= a->shape[k];
        for (k = 0; k < a->nd; k++) if (k != axis) rs[rnd++] = a->shape[k];
    }
    switch (kind) {
    case R_SUM: case R_PROD: rdt = a->dt == ND_FLOAT ? ND_FLOAT : ND_INT; break;
    case R_MIN: case R_MAX: rdt = a->dt; break;
    case R_ARGMIN: case R_ARGMAX: rdt = ND_INT; break;
    case R_ANY: case R_ALL: rdt = ND_BOOL; break;
    default: rdt = ND_FLOAT; break;
    }
    if ((kind == R_MIN || kind == R_MAX) && n == 0)
        throw_new(cls_ValueError, "zero-size array to reduction operation %s which has no identity",
                  kind == R_MIN ? "minimum" : "maximum");
    if ((kind == R_ARGMIN || kind == R_ARGMAX) && n == 0)
        throw_new(cls_ValueError, "attempt to get %s of an empty sequence",
                  kind == R_ARGMIN ? "argmin" : "argmax");
    r = nd_new(rdt, rnd, rs);
    if (kind == R_STD || kind == R_VAR) tmp = (double *)xmalloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    for (o = 0; o < outer; o++) {
        for (i = 0; i < inner; i++) {
            long base = o * n * inner + i, ri = o * inner + i;
            if (a->dt == ND_FLOAT) {
                const double *p = a->dv;
                double acc;
                switch (kind) {
                case R_SUM: case R_MEAN:
                    if (inner == 1) acc = pw_sum(p + base, n);
                    else { acc = 0.0; for (t = 0; t < n; t++) acc += p[base + t * inner]; }
                    r->dv[ri] = kind == R_MEAN ? acc / (double)n : acc;
                    break;
                case R_PROD:
                    acc = 1.0;
                    for (t = 0; t < n; t++) acc *= p[base + t * inner];
                    r->dv[ri] = acc;
                    break;
                case R_MIN: case R_MAX: {
                    double best = p[base];
                    for (t = 1; t < n; t++) {
                        double v = p[base + t * inner];
                        if (best != best) break;
                        if (v != v || (kind == R_MIN ? v < best : v > best)) best = v;
                    }
                    r->dv[ri] = best;
                    break; }
                case R_ARGMIN: case R_ARGMAX: {
                    long bi = 0;
                    double best = p[base];
                    for (t = 1; t < n; t++) {
                        double v = p[base + t * inner];
                        if (best != best) break;
                        if (v != v || (kind == R_ARGMIN ? v < best : v > best)) { best = v; bi = t; }
                    }
                    r->iv[ri] = bi;
                    break; }
                case R_ANY:
                    r->iv[ri] = 0;
                    for (t = 0; t < n; t++) if (p[base + t * inner] != 0) { r->iv[ri] = 1; break; }
                    break;
                case R_ALL:
                    r->iv[ri] = 1;
                    for (t = 0; t < n; t++) if (p[base + t * inner] == 0) { r->iv[ri] = 0; break; }
                    break;
                default: {          /* R_STD, R_VAR */
                    double mean;
                    if (inner == 1) acc = pw_sum(p + base, n);
                    else { acc = 0.0; for (t = 0; t < n; t++) acc += p[base + t * inner]; }
                    mean = acc / (double)n;
                    for (t = 0; t < n; t++) {
                        double dlt = p[base + t * inner] - mean;
                        tmp[t] = dlt * dlt;
                    }
                    if (inner == 1) acc = pw_sum(tmp, n);
                    else { acc = 0.0; for (t = 0; t < n; t++) acc += tmp[t]; }
                    acc = acc / (double)(n - ddof > 0 ? n - ddof : 0);
                    r->dv[ri] = kind == R_STD ? sqrt(acc) : acc;
                    break; }
                }
            } else {
                const long *p = a->iv;
                long acc;
                switch (kind) {
                case R_SUM:
                    acc = 0;
                    for (t = 0; t < n; t++) acc = (long)((unsigned long)acc + (unsigned long)p[base + t * inner]);
                    r->iv[ri] = acc;
                    break;
                case R_PROD:
                    acc = 1;
                    for (t = 0; t < n; t++) acc = (long)((unsigned long)acc * (unsigned long)p[base + t * inner]);
                    r->iv[ri] = acc;
                    break;
                case R_MIN: case R_MAX: {
                    long best = p[base];
                    for (t = 1; t < n; t++) {
                        long v = p[base + t * inner];
                        if (kind == R_MIN ? v < best : v > best) best = v;
                    }
                    r->iv[ri] = best;
                    break; }
                case R_ARGMIN: case R_ARGMAX: {
                    long bi = 0, best = p[base];
                    for (t = 1; t < n; t++) {
                        long v = p[base + t * inner];
                        if (kind == R_ARGMIN ? v < best : v > best) { best = v; bi = t; }
                    }
                    r->iv[ri] = bi;
                    break; }
                case R_ANY:
                    r->iv[ri] = 0;
                    for (t = 0; t < n; t++) if (p[base + t * inner] != 0) { r->iv[ri] = 1; break; }
                    break;
                default:            /* R_ALL */
                    r->iv[ri] = 1;
                    for (t = 0; t < n; t++) if (p[base + t * inner] == 0) { r->iv[ri] = 0; break; }
                    break;
                }
            }
        }
    }
    free(tmp);
    return nd_ret(r);
}
static Value nd_cumulative(int kind, Nd *a, int has_axis, int axis)
{
    long outer = 1, n, inner = 1, o, i, t;
    int k, rdt = a->dt == ND_FLOAT ? ND_FLOAT : ND_INT;
    Nd *r;
    if (!has_axis) { n = a->size; r = nd_new1(rdt, n); }
    else {
        for (k = 0; k < axis; k++) outer *= a->shape[k];
        n = a->shape[axis];
        for (k = axis + 1; k < a->nd; k++) inner *= a->shape[k];
        r = nd_new(rdt, a->nd, a->shape);
    }
    for (o = 0; o < outer; o++)
        for (i = 0; i < inner; i++) {
            long base = o * n * inner + i;
            if (rdt == ND_FLOAT) {
                double acc = kind == R_CUMSUM ? 0.0 : 1.0;
                for (t = 0; t < n; t++) {
                    if (kind == R_CUMSUM) acc += a->dv[base + t * inner];
                    else acc *= a->dv[base + t * inner];
                    r->dv[base + t * inner] = acc;
                }
            } else {
                unsigned long acc = kind == R_CUMSUM ? 0UL : 1UL;
                for (t = 0; t < n; t++) {
                    if (kind == R_CUMSUM) acc += (unsigned long)a->iv[base + t * inner];
                    else acc *= (unsigned long)a->iv[base + t * inner];
                    r->iv[base + t * inner] = (long)acc;
                }
            }
        }
    return ndv(r);
}
static long ddof_arg(Args *x)
{
    Value v = kwget(x, "ddof");
    if (v.t == T_UNSET || v.t == T_NONE) return 0;
    return need_long(v, "ddof");
}
#define NDRED(fname, kind) \
    BI(fname) { \
        NdTmp t_; Nd *a_ = nd_arg0(x, &t_); int ax_ = 0, has_ = get_axis(x, 1, a_, &ax_); \
        return nd_reduce(kind, a_, has_, ax_, ddof_arg(x)); }
NDRED(np_sum, R_SUM)
NDRED(np_prod, R_PROD)
NDRED(np_min, R_MIN)
NDRED(np_max, R_MAX)
NDRED(np_mean, R_MEAN)
NDRED(np_argmin, R_ARGMIN)
NDRED(np_argmax, R_ARGMAX)
NDRED(np_any, R_ANY)
NDRED(np_all, R_ALL)
NDRED(np_std, R_STD)
NDRED(np_var, R_VAR)
#define NDCUM(fname, kind) \
    BI(fname) { \
        NdTmp t_; Nd *a_ = nd_arg0(x, &t_); int ax_ = 0, has_ = get_axis(x, 1, a_, &ax_); \
        if (a_ == &t_.nd) a_ = nd_astype(a_, a_->dt); \
        return nd_cumulative(kind, a_, has_, ax_); }
NDCUM(np_cumsum, R_CUMSUM)
NDCUM(np_cumprod, R_CUMPROD)

/* ---- indexing --------------------------------------------------------- */
static Value mkslice(Value lo, Value hi, Value st)
{
    List *l = tuple_new(3);
    l->h.type = T_SLICE;
    l->a[0] = lo; l->a[1] = hi; l->a[2] = st;
    return mkobj(l, T_SLICE);
}
typedef struct NdSel {
    int rnd;
    long rshape[ND_MAXD];
    long *off[ND_MAXD];
    long base;
    long count;
} NdSel;

static long sel_index(long i, long n, int axis)
{
    long j = i < 0 ? i + n : i;
    if (j < 0 || j >= n)
        throw_new(cls_IndexError, "index %ld is out of bounds for axis %d with size %ld", i, axis, n);
    return j;
}
static void nd_select(Nd *a, Value idx, NdSel *sel)
{
    Value one[1], *comps;
    long ncomp, strides[ND_MAXD], s = 1, i;
    int k;
    sel->rnd = 0; sel->base = 0; sel->count = 1;
    for (k = 0; k < ND_MAXD; k++) sel->off[k] = NULL;
    for (k = a->nd - 1; k >= 0; k--) { strides[k] = s; s *= a->shape[k]; }
    if (idx.t == T_TUPLE) { comps = AS_LIST(idx)->a; ncomp = AS_LIST(idx)->n; }
    else { one[0] = idx; comps = one; ncomp = 1; }
    if (ncomp > a->nd)
        throw_new(cls_IndexError, "too many indices for array: array is %d-dimensional, but %ld "
                  "were indexed", a->nd, ncomp);
    if (ncomp == 1 && comps[0].t == T_ND && AS_ND(comps[0])->dt == ND_BOOL &&
        AS_ND(comps[0])->nd == a->nd && a->nd > 1) {
        Nd *m = AS_ND(comps[0]);
        long cnt = 0, *o;
        for (k = 0; k < a->nd; k++)
            if (m->shape[k] != a->shape[k])
                throw_new(cls_IndexError, "boolean index did not match indexed array");
        for (i = 0; i < m->size; i++) if (m->iv[i]) cnt++;
        o = (long *)xmalloc(sizeof(long) * (size_t)(cnt > 0 ? cnt : 1));
        cnt = 0;
        for (i = 0; i < m->size; i++) if (m->iv[i]) o[cnt++] = i;
        sel->rnd = 1; sel->rshape[0] = cnt; sel->off[0] = o; sel->count = cnt;
        return;
    }
    for (k = 0; k < a->nd; k++) {
        long n = a->shape[k];
        Value c = k < ncomp ? comps[k] : UNSET_V;
        long *o = NULL, cnt = 0;
        if (c.t == T_INT || c.t == T_BOOL) {
            sel->base += sel_index(c.u.i, n, k) * strides[k];
            continue;
        }
        if (c.t == T_UNSET) c = mkslice(NONE_V, NONE_V, NONE_V);
        if (c.t == T_SLICE) {
            long start, stop, step;
            List *sl = AS_LIST(c);
            slice_adjust(sl->a[0], sl->a[1], sl->a[2], n, &start, &stop, &step, &cnt);
            o = (long *)xmalloc(sizeof(long) * (size_t)(cnt > 0 ? cnt : 1));
            for (i = 0; i < cnt; i++) o[i] = (start + i * step) * strides[k];
        } else if (c.t == T_LIST || c.t == T_TUPLE || c.t == T_ND) {
            Nd *ix = c.t == T_ND ? AS_ND(c) : nd_from_value(c, -1);
            if (ix->nd != 1)
                throw_new(cls_IndexError, "only 1-d index arrays are supported");
            if (ix->dt == ND_BOOL) {
                if (ix->size != n)
                    throw_new(cls_IndexError, "boolean index did not match indexed array along "
                              "axis %d; size of axis is %ld but size of corresponding boolean "
                              "axis is %ld", k, n, ix->size);
                for (i = 0; i < n; i++) if (ix->iv[i]) cnt++;
                o = (long *)xmalloc(sizeof(long) * (size_t)(cnt > 0 ? cnt : 1));
                cnt = 0;
                for (i = 0; i < n; i++) if (ix->iv[i]) o[cnt++] = i * strides[k];
            } else if (ix->dt == ND_INT) {
                cnt = ix->size;
                o = (long *)xmalloc(sizeof(long) * (size_t)(cnt > 0 ? cnt : 1));
                for (i = 0; i < cnt; i++) o[i] = sel_index(ix->iv[i], n, k) * strides[k];
            } else
                throw_new(cls_IndexError, "arrays used as indices must be of integer (or boolean) type");
        } else
            throw_new(cls_IndexError, "only integers, slices (`:`), integer or boolean arrays are "
                      "valid indices");
        sel->rshape[sel->rnd] = cnt;
        sel->off[sel->rnd] = o;
        sel->rnd++;
        sel->count *= cnt;
    }
}
static void sel_free(NdSel *sel)
{
    int k;
    for (k = 0; k < sel->rnd; k++) free(sel->off[k]);
}
static Value nd_row(Nd *a, long i)
{
    Nd *r = nd_new(a->dt, a->nd - 1, a->shape + 1);
    nd_cp(r, 0, a, i * r->size, r->size);
    return ndv(r);
}
static Value nd_item(Value o, long i)
{
    Nd *a = AS_ND(o);
    if (a->nd == 1) return nd_elem(a, i);
    return nd_row(a, i);
}
static Value nd_subscr(Value o, Value idx)
{
    Nd *a = AS_ND(o), *r;
    NdSel sel;
    long ix[ND_MAXD], n, pos = 0, off;
    int k;
    if (idx.t == T_INT && a->nd >= 1) {
        long i = sel_index(idx.u.i, a->shape[0], 0);
        return nd_item(o, i);
    }
    if (a->nd == 0) throw_new(cls_IndexError, "too many indices for array: array is 0-dimensional");
    nd_select(a, idx, &sel);
    r = nd_new(a->dt, sel.rnd, sel.rshape);
    n = sel.count;
    for (k = 0; k < ND_MAXD; k++) ix[k] = 0;
    while (pos < n) {
        off = sel.base;
        for (k = 0; k < sel.rnd; k++) off += sel.off[k][ix[k]];
        if (a->dt == ND_FLOAT) r->dv[pos] = a->dv[off]; else r->iv[pos] = a->iv[off];
        pos++;
        for (k = sel.rnd - 1; k >= 0; k--) {
            if (++ix[k] < sel.rshape[k]) break;
            ix[k] = 0;
        }
    }
    sel_free(&sel);
    return nd_ret(r);
}
static void nd_setitem(Value o, Value idx, Value v)
{
    Nd *a = AS_ND(o), *s;
    NdTmp tv;
    NdSel sel;
    long ix[ND_MAXD], sst[ND_MAXD], n, pos = 0, off, so;
    int k, extra;
    if (a->nd == 0) throw_new(cls_IndexError, "too many indices for array: array is 0-dimensional");
    s = nd_coerce(v, &tv);
    if (idx.t == T_INT && a->nd == 1 && s->nd == 0) {
        long i = sel_index(idx.u.i, a->shape[0], 0);
        nd_cp(a, i, s, 0, 1);
        return;
    }
    nd_select(a, idx, &sel);
    n = sel.count;
    if (s->nd > sel.rnd) {
        char s1[160], s2[160];
        shape_str(s->nd, s->shape, s1);
        shape_str(sel.rnd, sel.rshape, s2);
        throw_new(cls_ValueError, "could not broadcast input array from shape %s into shape %s", s1, s2);
    }
    extra = sel.rnd - s->nd;
    {
        long st = 1;
        for (k = sel.rnd - 1; k >= 0; k--) {
            int sk = k - extra;
            if (sk < 0) sst[k] = 0;
            else {
                if (s->shape[sk] != 1 && s->shape[sk] != sel.rshape[k]) {
                    char s1[160], s2[160];
                    shape_str(s->nd, s->shape, s1);
                    shape_str(sel.rnd, sel.rshape, s2);
                    sel_free(&sel);
                    throw_new(cls_ValueError, "could not broadcast input array from shape %s into "
                              "shape %s", s1, s2);
                }
                sst[k] = s->shape[sk] == 1 ? 0 : st;
                st *= s->shape[sk];
            }
        }
    }
    for (k = 0; k < ND_MAXD; k++) ix[k] = 0;
    while (pos < n) {
        off = sel.base; so = 0;
        for (k = 0; k < sel.rnd; k++) { off += sel.off[k][ix[k]]; so += ix[k] * sst[k]; }
        nd_cp(a, off, s, so, 1);
        pos++;
        for (k = sel.rnd - 1; k >= 0; k--) {
            if (++ix[k] < sel.rshape[k]) break;
            ix[k] = 0;
        }
    }
    sel_free(&sel);
}
static void nd_assign_all(Value dst, Value src)
{
    if (AS_ND(dst)->nd == 0) { nd_cp(AS_ND(dst), 0, AS_ND(src), 0, 1); return; }
    nd_setitem(dst, mkslice(NONE_V, NONE_V, NONE_V), src);
}
static long nd_len(Value o)
{
    Nd *a = AS_ND(o);
    if (a->nd == 0) throw_new(cls_TypeError, "len() of unsized object");
    return a->shape[0];
}
static int nd_truthy(Value o)
{
    Nd *a = AS_ND(o);
    if (a->size == 1) return a->dt == ND_FLOAT ? a->dv[0] != 0 : a->iv[0] != 0;
    if (a->size == 0) return 0;
    throw_new(cls_ValueError, "The truth value of an array with more than one element is "
              "ambiguous. Use a.any() or a.all()");
    return 0;
}

/* ---- printing (numpy's array2string layout) --------------------------- */
#define NDW 64
typedef struct NdR {
    Nd *a;
    char *strs;          /* formatted elements, NDW bytes each, in display order */
    long next;
    int summ;
    int comma;           /* separator ", " (repr) instead of " " */
    long *ids;           /* flat indices of displayed elements, in display order */
    long nids;
} NdR;

static void nd_collect(NdR *r, int axis, long off)
{
    Nd *a = r->a;
    long n = a->shape[axis], i, stride = 1;
    int k;
    for (k = axis + 1; k < a->nd; k++) stride *= a->shape[k];
    for (i = 0; i < n; i++) {
        if (r->summ && n > 6 && i >= 3 && i < n - 3) { i = n - 4; continue; }
        if (axis == a->nd - 1) r->ids[r->nids++] = off + i;
        else nd_collect(r, axis + 1, off + i * stride);
    }
}
/* shortest repr with at most 8 fractional digits: digits after the point in *fr */
static void fl_fixed(double d, char *ip, char *fr)
{
    char t[400];
    int p;
    char *dot;
    for (p = 0; p <= 8; p++) {
        sprintf(t, "%.*f", p, d);
        if (strtod(t, NULL) == d) break;
    }
    if (p > 8) {
        int n;
        sprintf(t, "%.8f", d);
        n = (int)strlen(t);
        while (t[n - 1] == '0') t[--n] = 0;
    }
    dot = strchr(t, '.');
    if (dot) { *dot = 0; strcpy(ip, t); strcpy(fr, dot + 1); }
    else { strcpy(ip, t); fr[0] = 0; }
}
/* scientific: mantissa digits after the point, exponent value */
static int fl_sci_digits(double d)
{
    char t[64];
    int p;
    for (p = 0; p <= 8; p++) {
        sprintf(t, "%.*e", p, d);
        if (strtod(t, NULL) == d) return p;
    }
    return 8;
}
static void nd_format_elems(NdR *r)
{
    Nd *a = r->a;
    long i;
    char *sp = r->strs;
    if (a->dt == ND_BOOL) {
        int w = a->nd == 0 ? 4 : 5;
        for (i = 0; i < r->nids; i++) if (!a->iv[r->ids[i]]) w = 5;
        for (i = 0; i < r->nids; i++, sp += NDW)
            sprintf(sp, "%*s", w, a->iv[r->ids[i]] ? "True" : "False");
    } else if (a->dt == ND_INT) {
        int w = 1, l;
        for (i = 0; i < r->nids; i++) {
            sprintf(sp, "%ld", a->iv[r->ids[i]]);
            l = (int)strlen(sp);
            if (l > w) w = l;
        }
        for (i = 0; i < r->nids; i++, sp += NDW) {
            char t[NDW];
            sprintf(t, "%ld", a->iv[r->ids[i]]);
            sprintf(sp, "%*s", w, t);
        }
    } else {
        double mx = 0, mn = 0;
        int have = 0, expf = 0, padl = 0, padr = 0, prec = 0, expw = 2, nonfin = 0, neginf = 0;
        for (i = 0; i < r->nids; i++) {
            double d = a->dv[r->ids[i]], ad = fabs(d);
            if (d != d || ad - ad != 0 || d == 0) continue;
            if (!have) { mx = mn = ad; have = 1; }
            else { if (ad > mx) mx = ad; if (ad < mn) mn = ad; }
        }
        if (have && (mx >= 1e8 || mn < 1e-4 || mx / mn > 1000.0)) expf = 1;
        for (i = 0; i < r->nids; i++) {
            double d = a->dv[r->ids[i]];
            if (d != d || d - d != 0) { nonfin = 1; if (d < 0) neginf = 1; }
        }
        if (!expf) {
            for (i = 0; i < r->nids; i++) {
                double d = a->dv[r->ids[i]];
                char ip[400], fr[400];
                if (d != d || d - d != 0) continue;
                fl_fixed(d, ip, fr);
                if ((int)strlen(ip) > padl) padl = (int)strlen(ip);
                if ((int)strlen(fr) > padr) padr = (int)strlen(fr);
            }
            if (nonfin) {
                int offs = padr + 1;
                if (3 - offs > padl) padl = 3 - offs;
                if (3 + neginf - offs > padl) padl = 3 + neginf - offs;
            }
            for (i = 0; i < r->nids; i++, sp += NDW) {
                double d = a->dv[r->ids[i]];
                char ip[400], fr[400];
                if (d != d) sprintf(sp, "%*s", padl + padr + 1, "nan");
                else if (d - d != 0) sprintf(sp, "%*s", padl + padr + 1, d > 0 ? "inf" : "-inf");
                else {
                    fl_fixed(d, ip, fr);
                    sprintf(sp, "%*s.%s%*s", padl, ip, fr, padr - (int)strlen(fr), "");
                }
            }
        } else {
            for (i = 0; i < r->nids; i++) {
                double d = a->dv[r->ids[i]];
                char t[64];
                int p, e;
                if (d != d || d - d != 0) continue;
                p = fl_sci_digits(d);
                if (p > prec) prec = p;
                sprintf(t, "%.0e", d);
                e = atoi(strchr(t, 'e') + 1);
                if (e < 0) e = -e;
                if (e >= 100 && expw < 3) expw = 3;
                if (t[0] == '-') { if (padl < 2) padl = 2; }
                else if (padl < 1) padl = 1;
            }
            if (nonfin) {
                int offs = expw + 2 + prec + 1;
                if (3 - offs > padl) padl = 3 - offs;
                if (3 + neginf - offs > padl) padl = 3 + neginf - offs;
            }
            for (i = 0; i < r->nids; i++, sp += NDW) {
                double d = a->dv[r->ids[i]];
                char t[64], m[64];
                char *ep;
                int e, w;
                if (d != d) { sprintf(sp, "%*s", padl + prec + 2 + expw + 2 - 1, "nan"); continue; }
                if (d - d != 0) {
                    sprintf(sp, "%*s", padl + prec + 2 + expw + 2 - 1, d > 0 ? "inf" : "-inf");
                    continue;
                }
                sprintf(t, "%.*e", prec, d);
                ep = strchr(t, 'e');
                e = atoi(ep + 1);
                *ep = 0;
                strcpy(m, t);
                if (prec == 0) strcat(m, ".");
                w = padl - (int)(strchr(m, '.') - m);
                sprintf(sp, "%*s%se%c%0*d", w > 0 ? w : 0, "", m, e < 0 ? '-' : '+', expw,
                        e < 0 ? -e : e);
            }
        }
    }
}
static void nd_extend(Buf *s, Buf *line, const char *word, int elemw, int hang)
{
    int wl = (int)strlen(word);
    int wrap = line->n + wl > elemw;
    if (line->n <= hang) wrap = 0;
    if (wrap) {
        while (line->n > 0 && line->s[line->n - 1] == ' ') line->n--;
        line->s[line->n] = 0;
        buf_addn(s, line->s, line->n);
        buf_addc(s, '\n');
        line->n = 0; line->s[0] = 0;
        {
            int i;
            for (i = 0; i < hang; i++) buf_addc(line, ' ');
        }
    }
    buf_add(line, word);
}
static void nd_linesep(NdR *r, Buf *s, int axes_left)
{
    int i;
    if (r->comma) buf_addc(s, ',');
    for (i = 0; i < axes_left - 1; i++) buf_addc(s, '\n');
}
static void nd_rec(NdR *r, int axis, int hang, int curw, Buf *out)
{
    Nd *a = r->a;
    int axes_left = a->nd - axis, i;
    long n = a->shape[axis], j, lead, trail;
    int summ = r->summ && n > 6;
    Buf s;
    buf_init(&s);
    if (summ) { lead = 3; trail = 3; } else { lead = 0; trail = n; }
    if (axes_left == 1) {
        int elemw = curw - 1;
        Buf line;
        buf_init(&line);
        for (i = 0; i < hang; i++) buf_addc(&line, ' ');
        for (j = 0; j < lead; j++) {
            nd_extend(&s, &line, r->strs + NDW * (r->next++), elemw, hang);
            buf_add(&line, r->comma ? ", " : " ");
        }
        if (summ) { nd_extend(&s, &line, "...", elemw, hang); buf_add(&line, r->comma ? ", " : " "); }
        for (j = trail; j > 1; j--) {
            nd_extend(&s, &line, r->strs + NDW * (r->next++), elemw, hang);
            buf_add(&line, r->comma ? ", " : " ");
        }
        nd_extend(&s, &line, r->strs + NDW * (r->next++), elemw, hang);
        buf_addn(&s, line.s, line.n);
        buf_free(&line);
    } else {
        for (j = 0; j < lead; j++) {
            for (i = 0; i < hang; i++) buf_addc(&s, ' ');
            nd_rec(r, axis + 1, hang + 1, curw - 1, &s);
            nd_linesep(r, &s, axes_left);
        }
        if (summ) {
            for (i = 0; i < hang; i++) buf_addc(&s, ' ');
            buf_add(&s, "...");
            nd_linesep(r, &s, axes_left);
        }
        for (j = trail; j > 1; j--) {
            for (i = 0; i < hang; i++) buf_addc(&s, ' ');
            nd_rec(r, axis + 1, hang + 1, curw - 1, &s);
            nd_linesep(r, &s, axes_left);
        }
        for (i = 0; i < hang; i++) buf_addc(&s, ' ');
        nd_rec(r, axis + 1, hang + 1, curw - 1, &s);
    }
    buf_addc(out, '[');
    buf_addn(out, s.s + hang, s.n - hang);
    buf_addc(out, ']');
    buf_free(&s);
}
static void nd_format(Value o, Buf *b, int repr)
{
    Nd *a = AS_ND(o);
    NdR r;
    if (repr) buf_add(b, "array(");
    if (a->nd == 0) {
        r.a = a; r.nids = 1;
        r.ids = (long *)xmalloc(sizeof(long));
        r.ids[0] = 0;
        r.strs = (char *)xmalloc(NDW);
        nd_format_elems(&r);
        {
            char *p = r.strs;
            while (*p == ' ') p++;
            buf_add(b, p);
        }
        free(r.ids); free(r.strs);
    } else if (a->size == 0) {
        buf_add(b, "[]");
        if (repr) {
            buf_add(b, ", dtype=");
            buf_add(b, dt_name(a->dt));
        }
    } else {
        Buf body;
        r.a = a; r.summ = a->size > 1000; r.next = 0; r.nids = 0; r.comma = repr;
        {
            long cap = 1;
            int k;
            for (k = 0; k < a->nd; k++)
                cap *= (r.summ && a->shape[k] > 6) ? 6 : a->shape[k];
            r.ids = (long *)xmalloc(sizeof(long) * (size_t)cap + 8);
        }
        nd_collect(&r, 0, 0);
        r.strs = (char *)xmalloc((size_t)NDW * (size_t)(r.nids > 0 ? r.nids : 1));
        nd_format_elems(&r);
        r.next = 0;
        buf_init(&body);
        nd_rec(&r, 0, repr ? 7 : 1, repr ? 74 : 75, &body);
        buf_addn(b, body.s, body.n);
        buf_free(&body);
        free(r.ids); free(r.strs);
    }
    if (repr) buf_addc(b, ')');
}

/* ---- creation, shape manipulation ------------------------------------- */
static int shape_arg(Value v, long *shape)
{
    int nd = 0, k;
    if (v.t == T_INT || v.t == T_BOOL) { shape[0] = v.u.i; nd = 1; }
    else if (v.t == T_TUPLE || v.t == T_LIST) {
        List *l = AS_LIST(v);
        long i;
        if (l->n > ND_MAXD) throw_new(cls_ValueError, "maximum supported dimension for an ndarray is %d", ND_MAXD);
        for (i = 0; i < l->n; i++) shape[i] = need_long(l->a[i], "shape");
        nd = (int)l->n;
    } else
        throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer", type_name(v));
    for (k = 0; k < nd; k++)
        if (shape[k] < 0) throw_new(cls_ValueError, "negative dimensions are not allowed");
    return nd;
}
static int dtype_kw(Args *x, int dflt)
{
    Value d = kwget(x, "dtype");
    if (d.t == T_UNSET || d.t == T_NONE) return dflt;
    return parse_dtype(d);
}
static void nd_fillv(Nd *r, Value v)
{
    long i;
    if (r->dt == ND_FLOAT) { double d = to_dbl(v); for (i = 0; i < r->size; i++) r->dv[i] = d; }
    else {
        long n = v.t == T_FLOAT ? (long)v.u.d : v.u.i;
        if (r->dt == ND_BOOL) n = v.t == T_FLOAT ? v.u.d != 0 : n != 0;
        for (i = 0; i < r->size; i++) r->iv[i] = n;
    }
}
static Value nd_filled(Args *x, Value fill, int dflt)
{
    long shape[ND_MAXD];
    int nd, dt;
    Nd *r;
    if (x->n < 1) throw_new(cls_TypeError, "missing shape argument");
    nd = shape_arg(A(0), shape);
    dt = dtype_kw(x, dflt);
    r = nd_new(dt, nd, shape);
    nd_fillv(r, fill);
    return ndv(r);
}
BI(np_zeros) { return nd_filled(x, mkint(0), ND_FLOAT); }
BI(np_ones) { return nd_filled(x, mkint(1), ND_FLOAT); }
BI(np_full)
{
    long shape[ND_MAXD];
    int nd, dflt;
    Nd *r;
    Value fv;
    if (x->n < 2) throw_new(cls_TypeError, "full() missing required arguments");
    fv = A(1);
    dflt = py_dt(fv) < 0 ? ND_FLOAT : py_dt(fv);
    nd = shape_arg(A(0), shape);
    r = nd_new(dtype_kw(x, dflt), nd, shape);
    nd_fillv(r, fv);
    return ndv(r);
}
static Value like_fill(Args *x, Value fill)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    r = nd_new(dtype_kw(x, a->dt), a->nd, a->shape);
    nd_fillv(r, fill);
    return ndv(r);
}
BI(np_zeros_like) { return like_fill(x, mkint(0)); }
BI(np_ones_like) { return like_fill(x, mkint(1)); }
BI(np_full_like)
{
    if (x->n < 2) throw_new(cls_TypeError, "full_like() missing required arguments");
    return like_fill(x, A(1));
}
BI(np_eye)
{
    long n, m, kk = 0, i, sh[2];
    Value mv = x->n > 1 ? A(1) : kwget(x, "M"), kv = x->n > 2 ? A(2) : kwget(x, "k");
    Nd *r;
    if (x->n < 1) throw_new(cls_TypeError, "eye() missing required argument 'N'");
    n = need_long(A(0), "N");
    m = (mv.t == T_UNSET || mv.t == T_NONE) ? n : need_long(mv, "M");
    if (kv.t != T_UNSET && kv.t != T_NONE) kk = need_long(kv, "k");
    sh[0] = n; sh[1] = m;
    r = nd_new(dtype_kw(x, ND_FLOAT), 2, sh);
    nd_fillv(r, mkint(0));
    for (i = 0; i < n; i++)
        if (i + kk >= 0 && i + kk < m) {
            if (r->dt == ND_FLOAT) r->dv[i * m + i + kk] = 1.0; else r->iv[i * m + i + kk] = 1;
        }
    return ndv(r);
}
BI(np_arange)
{
    Value sv, ev, tv;
    int allint = 1, i, dt;
    double start = 0, stop, step = 1;
    long n, k;
    Nd *r;
    if (x->n < 1 || x->n > 3) throw_new(cls_TypeError, "arange() takes 1 to 3 positional arguments");
    for (i = 0; i < x->n; i++) {
        if (py_dt(A(i)) < 0) throw_new(cls_TypeError, "arange() arguments must be numbers");
        if (A(i).t == T_FLOAT) allint = 0;
    }
    if (x->n == 1) { sv = mkint(0); ev = A(0); tv = mkint(1); }
    else { sv = A(0); ev = A(1); tv = x->n > 2 ? A(2) : mkint(1); }
    start = to_dbl(sv); stop = to_dbl(ev); step = to_dbl(tv);
    if (step == 0) throw_new(cls_ZeroDivisionError, "Maximum allowed size exceeded");
    n = (long)ceil((stop - start) / step);
    if (n < 0) n = 0;
    dt = dtype_kw(x, allint ? ND_INT : ND_FLOAT);
    r = nd_new(dt, 1, &n);
    if (allint) {
        long s0 = sv.u.i, st = tv.u.i;
        for (k = 0; k < n; k++) {
            long v = s0 + k * st;
            if (dt == ND_FLOAT) r->dv[k] = (double)v; else r->iv[k] = dt == ND_BOOL ? v != 0 : v;
        }
    } else {
        double delta = (start + step) - start;
        for (k = 0; k < n; k++) {
            double v = start + (double)k * delta;
            if (dt == ND_FLOAT) r->dv[k] = v; else r->iv[k] = dt == ND_BOOL ? v != 0 : (long)v;
        }
    }
    return ndv(r);
}
BI(np_linspace)
{
    Value nv = x->n > 2 ? A(2) : kwget(x, "num"), ev = x->n > 3 ? A(3) : kwget(x, "endpoint");
    double start, stop, step, delta;
    long num = 50, i, div;
    int endpoint = 1;
    Nd *r;
    if (x->n < 2) throw_new(cls_TypeError, "linspace() missing required arguments");
    start = to_dbl(A(0)); stop = to_dbl(A(1));
    if (nv.t != T_UNSET) num = need_long(nv, "num");
    if (ev.t != T_UNSET) endpoint = py_truthy(ev);
    if (num < 0) throw_new(cls_ValueError, "Number of samples, %ld, must be non-negative.", num);
    r = nd_new1(ND_FLOAT, num);
    div = endpoint ? num - 1 : num;
    delta = stop - start;
    if (div > 0) {
        step = delta / (double)div;
        for (i = 0; i < num; i++) r->dv[i] = (double)i * step + start;
    } else {
        for (i = 0; i < num; i++) r->dv[i] = start;
    }
    if (endpoint && num > 1) r->dv[num - 1] = stop;
    if (dtype_kw(x, ND_FLOAT) != ND_FLOAT) return ndv(nd_astype(r, dtype_kw(x, ND_FLOAT)));
    return ndv(r);
}
BI(np_array)
{
    Value d = x->n > 1 ? A(1) : kwget(x, "dtype");
    int dt = -1;
    if (x->n < 1) throw_new(cls_TypeError, "array() missing required argument 'object'");
    if (d.t != T_UNSET && d.t != T_NONE) dt = parse_dtype(d);
    return ndv(nd_from_value(A(0), dt));
}
BI(np_asarray)
{
    Value d = x->n > 1 ? A(1) : kwget(x, "dtype");
    if (x->n < 1) throw_new(cls_TypeError, "asarray() missing required argument 'a'");
    if (A(0).t == T_ND && (d.t == T_UNSET || d.t == T_NONE)) return A(0);
    return np_array(x);
}
BI(nd_copy)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    return ndv(nd_astype(a, a->dt));
}
BI(nd_astype_m)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    Value d = x->n > 1 ? A(1) : kwget(x, "dtype");
    if (d.t == T_UNSET) throw_new(cls_TypeError, "astype() missing required argument 'dtype'");
    return ndv(nd_astype(a, parse_dtype(d)));
}
static Value nd_tolist_rec(Nd *a, int axis, long off)
{
    long n, i, stride = 1;
    int k;
    List *l;
    Value lv;
    if (a->nd == 0) return nd_elem(a, 0);
    n = a->shape[axis];
    for (k = axis + 1; k < a->nd; k++) stride *= a->shape[k];
    l = list_new(n);
    lv = mklist(l);
    for (i = 0; i < n; i++) {
        if (axis == a->nd - 1) list_push(l, nd_elem(a, off + i));
        else list_push(l, nd_tolist_rec(a, axis + 1, off + i * stride));
    }
    return lv;
}
BI(nd_tolist)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    return nd_tolist_rec(a, 0, 0);
}
BI(nd_item_m)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    if (a->size != 1) throw_new(cls_ValueError, "can only convert an array of size 1 to a Python scalar");
    return nd_elem(a, 0);
}
BI(nd_reshape)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long shape[ND_MAXD], known = 1;
    int nd, k, unk = -1;
    if (x->n == 2 && (A(1).t == T_TUPLE || A(1).t == T_LIST)) {
        List *l = AS_LIST(A(1));
        if (l->n > ND_MAXD) throw_new(cls_ValueError, "too many dimensions");
        for (k = 0; k < l->n; k++) shape[k] = need_long(l->a[k], "shape");
        nd = (int)l->n;
    } else {
        if (x->n - 1 > ND_MAXD) throw_new(cls_ValueError, "too many dimensions");
        for (k = 1; k < x->n; k++) shape[k - 1] = need_long(A(k), "shape");
        nd = x->n - 1;
    }
    for (k = 0; k < nd; k++) {
        if (shape[k] == -1) {
            if (unk >= 0) throw_new(cls_ValueError, "can only specify one unknown dimension");
            unk = k;
        } else known *= shape[k];
    }
    if (unk >= 0) {
        if (known == 0 || a->size % known != 0)
            throw_new(cls_ValueError, "cannot reshape array of size %ld into shape %ld", a->size, known);
        shape[unk] = a->size / known;
    } else if (known != a->size) {
        char s1[160];
        shape_str(nd, shape, s1);
        throw_new(cls_ValueError, "cannot reshape array of size %ld into shape %s", a->size, s1);
    }
    return ndv(nd_reshaped(a, nd, shape));
}
BI(nd_flatten)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long n = a->size;
    return ndv(nd_reshaped(a, 1, &n));
}
static Nd *nd_transposed(Nd *a)
{
    long rs[ND_MAXD], sst[ND_MAXD], idx[ND_MAXD], st = 1, i, off = 0;
    int k;
    Nd *r;
    for (k = 0; k < a->nd; k++) rs[k] = a->shape[a->nd - 1 - k];
    for (k = a->nd - 1; k >= 0; k--) { sst[a->nd - 1 - k] = st; st *= a->shape[k]; }
    r = nd_new(a->dt, a->nd, rs);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        nd_cp(r, i, a, off, 1);
        for (k = a->nd - 1; k >= 0; k--) {
            idx[k]++; off += sst[k];
            if (idx[k] < rs[k]) break;
            off -= sst[k] * rs[k]; idx[k] = 0;
        }
    }
    return r;
}
BI(nd_transpose)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    return ndv(nd_transposed(a));
}
static Nd *nd_expand(const Nd *a, int rnd, const long *rs, int dt)
{
    Nd *r = nd_new(dt, rnd, rs);
    long st[ND_MAXD], idx[ND_MAXD], i, off = 0;
    int k;
    nd_bstrides(a, rnd, st);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        nd_cp(r, i, a, off, 1);
        for (k = rnd - 1; k >= 0; k--) {
            idx[k]++; off += st[k];
            if (idx[k] < rs[k]) break;
            off -= st[k] * rs[k]; idx[k] = 0;
        }
    }
    return r;
}

/* ---- joining, selection, sorting -------------------------------------- */
static Nd *nd_concat(Nd **arr, int cnt, int axis)
{
    long rs[ND_MAXD], outer = 1, inner = 1, o, off = 0, tot = 0;
    int i, k, dt = ND_BOOL, nd = arr[0]->nd;
    Nd *r;
    for (i = 0; i < cnt; i++) {
        if (arr[i]->nd != nd || nd == 0)
            throw_new(cls_ValueError, "all the input array dimensions except for the concatenation "
                      "axis must match exactly");
        for (k = 0; k < nd; k++)
            if (k != axis && arr[i]->shape[k] != arr[0]->shape[k])
                throw_new(cls_ValueError, "all the input array dimensions except for the "
                          "concatenation axis must match exactly, but along dimension %d, the "
                          "array at index 0 has size %ld and the array at index %d has size %ld",
                          k, arr[0]->shape[k], i, arr[i]->shape[k]);
        if (arr[i]->dt > dt) dt = arr[i]->dt;
        tot += arr[i]->shape[axis];
    }
    for (k = 0; k < nd; k++) rs[k] = arr[0]->shape[k];
    rs[axis] = tot;
    for (k = 0; k < axis; k++) outer *= rs[k];
    for (k = axis + 1; k < nd; k++) inner *= rs[k];
    r = nd_new(dt, nd, rs);
    for (o = 0; o < outer; o++)
        for (i = 0; i < cnt; i++) {
            long blk = arr[i]->shape[axis] * inner;
            nd_cp(r, off, arr[i], o * blk, blk);
            off += blk;
        }
    return r;
}
static int seq_arrays(Value v, Nd ***out)
{
    long n, i;
    Nd **arr;
    if (v.t != T_LIST && v.t != T_TUPLE && v.t != T_ND)
        throw_new(cls_TypeError, "expected a sequence of arrays");
    if (v.t == T_ND) n = nd_len(v); else n = AS_LIST(v)->n;
    if (n == 0) throw_new(cls_ValueError, "need at least one array to concatenate");
    arr = (Nd **)xmalloc(sizeof(Nd *) * (size_t)n);
    for (i = 0; i < n; i++) {
        Value it = v.t == T_ND ? nd_item(v, i) : AS_LIST(v)->a[i];
        arr[i] = it.t == T_ND ? AS_ND(it) : nd_from_value(it, -1);
    }
    *out = arr;
    return (int)n;
}
BI(np_concatenate)
{
    Nd **arr;
    int n, axis = 0;
    Value av = x->n > 1 ? A(1) : kwget(x, "axis"), r;
    if (x->n < 1) throw_new(cls_TypeError, "concatenate() missing required argument");
    n = seq_arrays(A(0), &arr);
    if (av.t != T_UNSET && av.t != T_NONE) axis = axis_norm(need_long(av, "axis"), arr[0]->nd);
    r = ndv(nd_concat(arr, n, axis));
    free(arr);
    return r;
}
static void nd_atleast2(Nd **arr, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        long sh[2];
        if (arr[i]->nd == 1) { sh[0] = 1; sh[1] = arr[i]->shape[0]; arr[i] = nd_reshaped(arr[i], 2, sh); }
        else if (arr[i]->nd == 0) { sh[0] = 1; sh[1] = 1; arr[i] = nd_reshaped(arr[i], 2, sh); }
    }
}
BI(np_vstack)
{
    Nd **arr;
    int n;
    Value r;
    if (x->n < 1) throw_new(cls_TypeError, "vstack() missing required argument");
    n = seq_arrays(A(0), &arr);
    nd_atleast2(arr, n);
    r = ndv(nd_concat(arr, n, 0));
    free(arr);
    return r;
}
BI(np_hstack)
{
    Nd **arr;
    int n, i, one = 1;
    Value r;
    if (x->n < 1) throw_new(cls_TypeError, "hstack() missing required argument");
    n = seq_arrays(A(0), &arr);
    for (i = 0; i < n; i++) if (arr[i]->nd != 1) one = 0;
    if (!one) {
        for (i = 0; i < n; i++)
            if (arr[i]->nd == 0) { long sh = 1; arr[i] = nd_reshaped(arr[i], 1, &sh); }
    }
    r = ndv(nd_concat(arr, n, one || arr[0]->nd == 1 ? 0 : 1));
    free(arr);
    return r;
}
BI(np_stack)
{
    Nd **arr;
    int n, i, axis = 0;
    Value av = x->n > 1 ? A(1) : kwget(x, "axis"), r;
    if (x->n < 1) throw_new(cls_TypeError, "stack() missing required argument");
    n = seq_arrays(A(0), &arr);
    if (av.t != T_UNSET && av.t != T_NONE) axis = axis_norm(need_long(av, "axis"), arr[0]->nd + 1);
    for (i = 1; i < n; i++)
        if (arr[i]->nd != arr[0]->nd || memcmp(arr[i]->shape, arr[0]->shape, sizeof(long) * ND_MAXD))
            throw_new(cls_ValueError, "all input arrays must have the same shape");
    for (i = 0; i < n; i++) {
        long sh[ND_MAXD];
        int k, m = 0;
        for (k = 0; k <= arr[i]->nd; k++) sh[k] = k == axis ? 1 : arr[i]->shape[m++];
        arr[i] = nd_reshaped(arr[i], arr[i]->nd + 1, sh);
    }
    r = ndv(nd_concat(arr, n, axis));
    free(arr);
    return r;
}
static Value np_nonzero(Args *x);
BI(np_where)
{
    NdTmp tc, tx, ty, tp;
    Nd *c, *xx, *yy, *r;
    long rs1[ND_MAXD], rs[ND_MAXD], i;
    int r1, rnd, dt;
    if (x->n == 1) return np_nonzero(x);
    if (x->n != 3) throw_new(cls_TypeError, "where() requires 3 arguments");
    c = nd_coerce(A(0), &tc); xx = nd_coerce(A(1), &tx); yy = nd_coerce(A(2), &ty);
    r1 = nd_bshape(xx, yy, rs1);
    memset(&tp, 0, sizeof(tp));
    tp.nd.nd = r1;
    for (i = 0; i < r1; i++) tp.nd.shape[i] = rs1[i];
    rnd = nd_bshape(c, &tp.nd, rs);
    dt = xx->dt > yy->dt ? xx->dt : yy->dt;
    c = nd_expand(c, rnd, rs, c->dt);
    xx = nd_expand(xx, rnd, rs, dt);
    yy = nd_expand(yy, rnd, rs, dt);
    r = nd_new(dt, rnd, rs);
    for (i = 0; i < r->size; i++) {
        int t = c->dt == ND_FLOAT ? c->dv[i] != 0 : c->iv[i] != 0;
        nd_cp(r, i, t ? xx : yy, i, 1);
    }
    return nd_ret(r);
}
BI(np_nonzero)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long cnt = 0, i, pos = 0, idx[ND_MAXD];
    int k;
    Nd *outs[ND_MAXD];
    Value tv[ND_MAXD];
    if (a->nd == 0) throw_new(cls_ValueError, "Calling nonzero on 0d arrays is not allowed");
    for (i = 0; i < a->size; i++) if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) cnt++;
    for (k = 0; k < a->nd; k++) outs[k] = nd_new1(ND_INT, cnt);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < a->size; i++) {
        if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) {
            for (k = 0; k < a->nd; k++) outs[k]->iv[pos] = idx[k];
            pos++;
        }
        for (k = a->nd - 1; k >= 0; k--) {
            if (++idx[k] < a->shape[k]) break;
            idx[k] = 0;
        }
    }
    for (k = 0; k < a->nd; k++) tv[k] = ndv(outs[k]);
    return mktuple_from(tv, a->nd);
}
static int cmp_long(const void *p, const void *q)
{
    long a = *(const long *)p, b = *(const long *)q;
    return a < b ? -1 : (a > b ? 1 : 0);
}
static int cmp_dbl(const void *p, const void *q)
{
    double a = *(const double *)p, b = *(const double *)q;
    if (a != a) return b != b ? 0 : 1;
    if (b != b) return -1;
    return a < b ? -1 : (a > b ? 1 : 0);
}
static void nd_sort_rows(Nd *r, long n)
{
    long o;
    if (n <= 0) return;
    for (o = 0; o + n <= r->size; o += n) {
        if (r->dt == ND_FLOAT) qsort(r->dv + o, (size_t)n, sizeof(double), cmp_dbl);
        else qsort(r->iv + o, (size_t)n, sizeof(long), cmp_long);
    }
}
BI(np_sort)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value av = x->n > 1 ? A(1) : kwget(x, "axis");
    if (av.t == T_NONE) {
        long n = a->size;
        r = nd_reshaped(a, 1, &n);
    } else {
        if (av.t != T_UNSET && a->nd > 0 && axis_norm(need_long(av, "axis"), a->nd) != a->nd - 1)
            throw_new(cls_NotImplementedError, "sort: only the last axis is supported");
        r = nd_astype(a, a->dt);
    }
    if (r->nd == 0) return nd_ret(r);
    nd_sort_rows(r, r->shape[r->nd - 1]);
    return ndv(r);
}
BI(nd_sort_m)
{
    Nd *a;
    if (x->n < 1 || A(0).t != T_ND) throw_new(cls_TypeError, "sort() requires an array");
    a = AS_ND(A(0));
    if (a->nd > 0) nd_sort_rows(a, a->shape[a->nd - 1]);
    return NONE_V;
}
static const double *srt_d;
static const long *srt_i;
static int cmp_arg(const void *p, const void *q)
{
    long a = *(const long *)p, b = *(const long *)q;
    int c;
    if (srt_d) c = cmp_dbl(srt_d + a, srt_d + b); else c = cmp_long(srt_i + a, srt_i + b);
    if (c) return c;
    return a < b ? -1 : (a > b ? 1 : 0);
}
BI(np_argsort)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long n, o, i, *tmp;
    if (a->nd == 0) throw_new(cls_ValueError, "argsort of a 0-d array");
    n = a->shape[a->nd - 1];
    r = nd_new(ND_INT, a->nd, a->shape);
    tmp = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    for (o = 0; n > 0 && o + n <= a->size; o += n) {
        for (i = 0; i < n; i++) tmp[i] = i;
        srt_d = a->dt == ND_FLOAT ? a->dv + o : NULL;
        srt_i = a->dt == ND_FLOAT ? NULL : a->iv + o;
        qsort(tmp, (size_t)n, sizeof(long), cmp_arg);
        for (i = 0; i < n; i++) r->iv[o + i] = tmp[i];
    }
    free(tmp);
    return ndv(r);
}
BI(np_unique)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *s, *r;
    long n = a->size, i, m = 0;
    s = nd_reshaped(a, 1, &n);
    nd_sort_rows(s, n);
    for (i = 0; i < n; i++)
        if (i == 0 || (s->dt == ND_FLOAT ? s->dv[i] != s->dv[i - 1] : s->iv[i] != s->iv[i - 1])) m++;
    r = nd_new1(s->dt, m);
    m = 0;
    for (i = 0; i < n; i++)
        if (i == 0 || (s->dt == ND_FLOAT ? s->dv[i] != s->dv[i - 1] : s->iv[i] != s->iv[i - 1])) {
            nd_cp(r, m, s, i, 1);
            m++;
        }
    return ndv(r);
}
BI(np_median)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *s;
    long n = a->size;
    if (n == 0) return mkfloat(HUGE_VAL - HUGE_VAL);
    s = nd_astype(a, ND_FLOAT);
    s = nd_reshaped(s, 1, &n);
    nd_sort_rows(s, n);
    if (n % 2) return mkfloat(s->dv[n / 2]);
    return mkfloat((s->dv[n / 2 - 1] + s->dv[n / 2]) / 2.0);
}

/* ---- linear algebra ---------------------------------------------------- */
static Value nd_matmul(Nd *a, Nd *b, int isdot)
{
    long m, kk, n, i, k, j, rs[2];
    int rnd = 0, fl;
    Nd *r;
    if (a->nd == 0 || b->nd == 0) {
        if (!isdot) throw_new(cls_ValueError, "matmul: Input operand does not have enough dimensions");
        return nd_binary(B_MUL, a, b);
    }
    if (a->nd > 2 || b->nd > 2) throw_new(cls_NotImplementedError, "matmul: only 1-d and 2-d arrays are supported");
    if (a->nd == 1) { m = 1; kk = a->shape[0]; } else { m = a->shape[0]; kk = a->shape[1]; }
    if (b->nd == 1) { n = 1; if (b->shape[0] != kk) goto mismatch; }
    else { n = b->shape[1]; if (b->shape[0] != kk) goto mismatch; }
    if (a->nd == 2) rs[rnd++] = m;
    if (b->nd == 2) rs[rnd++] = n;
    fl = a->dt == ND_FLOAT || b->dt == ND_FLOAT;
    if (fl) { a = nd_to_float(a); b = nd_to_float(b); }
    r = nd_new(fl ? ND_FLOAT : ND_INT, rnd, rs);
    if (fl) {
        for (i = 0; i < m * n; i++) r->dv[i] = 0.0;
        for (i = 0; i < m; i++)
            for (k = 0; k < kk; k++) {
                double aik = a->dv[i * kk + k];
                const double *bp = b->dv + k * n;
                double *cp = r->dv + i * n;
                for (j = 0; j < n; j++) cp[j] += aik * bp[j];
            }
    } else {
        for (i = 0; i < m * n; i++) r->iv[i] = 0;
        for (i = 0; i < m; i++)
            for (k = 0; k < kk; k++) {
                unsigned long aik = (unsigned long)a->iv[i * kk + k];
                const long *bp = b->iv + k * n;
                long *cp = r->iv + i * n;
                for (j = 0; j < n; j++) cp[j] = (long)((unsigned long)cp[j] + aik * (unsigned long)bp[j]);
            }
    }
    return nd_ret(r);
mismatch:
    {
        char s1[160], s2[160];
        shape_str(a->nd, a->shape, s1);
        shape_str(b->nd, b->shape, s2);
        throw_new(cls_ValueError, "shapes %s and %s not aligned: %ld (dim %d) != %ld (dim 0)", s1, s2,
                  kk, a->nd - 1, b->shape[0]);
    }
    return NONE_V;
}
BI(np_dot)
{
    NdTmp ta, tb;
    if (x->n != 2) throw_new(cls_TypeError, "dot() takes exactly 2 arguments");
    return nd_matmul(nd_coerce(A(0), &ta), nd_coerce(A(1), &tb), 1);
}
BI(np_matmul)
{
    NdTmp ta, tb;
    if (x->n != 2) throw_new(cls_TypeError, "matmul() takes exactly 2 arguments");
    return nd_matmul(nd_coerce(A(0), &ta), nd_coerce(A(1), &tb), 0);
}
BI(np_diag)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long i, n, sh[2];
    if (a->nd == 1) {
        n = a->shape[0]; sh[0] = n; sh[1] = n;
        r = nd_new(a->dt, 2, sh);
        for (i = 0; i < n * n; i++) { if (r->dt == ND_FLOAT) r->dv[i] = 0; else r->iv[i] = 0; }
        for (i = 0; i < n; i++) nd_cp(r, i * n + i, a, i, 1);
        return ndv(r);
    }
    if (a->nd != 2) throw_new(cls_ValueError, "Input must be 1- or 2-d.");
    n = a->shape[0] < a->shape[1] ? a->shape[0] : a->shape[1];
    r = nd_new1(a->dt, n);
    for (i = 0; i < n; i++) nd_cp(r, i, a, i * a->shape[1] + i, 1);
    return ndv(r);
}
BI(np_trace)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *d;
    Args y;
    Value dv;
    if (a->nd != 2) throw_new(cls_ValueError, "diag requires an array of at least two dimensions");
    y.n = 1; dv = ndv(a); y.a = &dv; y.nk = 0; y.kw = NULL;
    d = AS_ND(np_diag(&y));
    return nd_reduce(R_SUM, d, 0, 0, 0);
}

/* ---- rounding, comparison helpers, ufunc wrappers ---------------------- */
static double round_dec(double v, long dec)
{
    double f;
    if (dec >= 0) {
        f = pow(10.0, (double)dec);
        return nd_rint(v * f) / f;
    }
    f = pow(10.0, (double)-dec);
    return nd_rint(v / f) * f;
}
BI(np_round)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value dv = x->n > 1 ? A(1) : kwget(x, "decimals");
    long dec = 0, i;
    if (dv.t != T_UNSET && dv.t != T_NONE) dec = need_long(dv, "decimals");
    if (a->dt != ND_FLOAT) {
        if (dec >= 0) return nd_ret(nd_astype(a, a->dt));
        r = nd_astype(a, ND_FLOAT);
        for (i = 0; i < r->size; i++) r->dv[i] = round_dec(r->dv[i], dec);
        return ndv(nd_astype(r, ND_INT));
    }
    r = nd_new(ND_FLOAT, a->nd, a->shape);
    for (i = 0; i < a->size; i++) r->dv[i] = round_dec(a->dv[i], dec);
    return nd_ret(r);
}
/* float.round(ndigits) - numpy float64 scalars have this method */
BI(f_np_round)
{
    long dec = 0;
    if (x->n > 1 && A(1).t != T_NONE) dec = need_long(A(1), "decimals");
    return mkfloat(round_dec(A(0).u.d, dec));
}
BI(np_clip)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    Value lo = x->n > 1 ? A(1) : kwget(x, "a_min"), hi = x->n > 2 ? A(2) : kwget(x, "a_max");
    Value r = ndv(a);
    NdTmp tl, th;
    if (lo.t == T_UNSET) lo = kwget(x, "min");
    if (hi.t == T_UNSET) hi = kwget(x, "max");
    if (lo.t != T_UNSET && lo.t != T_NONE) r = nd_binary(OP_MAX, nd_coerce(r, &t), nd_coerce(lo, &tl));
    if (hi.t != T_UNSET && hi.t != T_NONE) r = nd_binary(OP_MIN, nd_coerce(r, &t), nd_coerce(hi, &th));
    if (r.t == T_ND && r.u.o == (Hdr *)a) return ndv(nd_astype(a, a->dt));
    return r;
}
BI(np_allclose)
{
    NdTmp ta, tb;
    Nd *a, *b;
    double rtol = 1e-5, atol = 1e-8;
    Value rv = x->n > 2 ? A(2) : kwget(x, "rtol"), av = x->n > 3 ? A(3) : kwget(x, "atol");
    long rs[ND_MAXD], i;
    int rnd;
    if (x->n < 2) throw_new(cls_TypeError, "allclose() missing required arguments");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (rv.t != T_UNSET) rtol = to_dbl(rv);
    if (av.t != T_UNSET) atol = to_dbl(av);
    rnd = nd_bshape(a, b, rs);
    a = nd_expand(a, rnd, rs, ND_FLOAT);
    b = nd_expand(b, rnd, rs, ND_FLOAT);
    for (i = 0; i < a->size; i++) {
        double p = a->dv[i], q = b->dv[i];
        if (p != p || q != q) return FALSE_V;
        if (p == q) continue;
        if (p - p != 0 || q - q != 0) return FALSE_V;
        if (!(fabs(p - q) <= atol + rtol * fabs(q))) return FALSE_V;
    }
    return TRUE_V;
}
BI(np_array_equal)
{
    NdTmp ta, tb;
    Nd *a, *b;
    long i;
    int k;
    if (x->n != 2) throw_new(cls_TypeError, "array_equal() takes exactly 2 arguments");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->nd != b->nd) return FALSE_V;
    for (k = 0; k < a->nd; k++) if (a->shape[k] != b->shape[k]) return FALSE_V;
    for (i = 0; i < a->size; i++) {
        if (a->dt == ND_FLOAT || b->dt == ND_FLOAT) {
            if (nd_d(a, i) != nd_d(b, i)) return FALSE_V;
        } else if (a->iv[i] != b->iv[i]) return FALSE_V;
    }
    return TRUE_V;
}
BI(np_count_nonzero)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long i, c = 0;
    for (i = 0; i < a->size; i++) if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) c++;
    return mkint(c);
}
#define NDF1(fname, fn) \
    BI(fname) { NdTmp t_; Nd *a_ = nd_arg0(x, &t_); return nd_ufunc1(fn, a_); }
NDF1(np_sqrt, F_SQRT)
NDF1(np_exp, F_EXP)
NDF1(np_sin, F_SIN)
NDF1(np_cos, F_COS)
NDF1(np_tan, F_TAN)
NDF1(np_log, F_LOG)
NDF1(np_log10, F_LOG10)
NDF1(np_log2, F_LOG2)
NDF1(np_floor, F_FLOOR)
NDF1(np_ceil, F_CEIL)
NDF1(np_tanh, F_TANH)
NDF1(np_sinh, F_SINH)
NDF1(np_cosh, F_COSH)
NDF1(np_arcsin, F_ASIN)
NDF1(np_arccos, F_ACOS)
NDF1(np_arctan, F_ATAN)
NDF1(np_abs, F_ABS)
NDF1(np_sign, F_SIGN)
NDF1(np_square, F_SQUARE)
NDF1(np_isnan, F_ISNAN)
NDF1(np_isinf, F_ISINF)
NDF1(np_isfinite, F_ISFINITE)
NDF1(np_negative, F_NEG)
#define NDB2(fname, op) \
    BI(fname) { \
        NdTmp ta_, tb_; \
        if (x->n != 2) throw_new(cls_TypeError, "function takes exactly 2 arguments"); \
        return nd_binary(op, nd_coerce(A(0), &ta_), nd_coerce(A(1), &tb_)); }
NDB2(np_add, B_ADD)
NDB2(np_subtract, B_SUB)
NDB2(np_multiply, B_MUL)
NDB2(np_divide, B_DIV)
NDB2(np_floor_divide, B_FDIV)
NDB2(np_mod, B_MOD)
NDB2(np_power, B_POW)
NDB2(np_maximum, OP_MAX)
NDB2(np_minimum, OP_MIN)

/* ---- attributes -------------------------------------------------------- */
static int nd_getattr(Value o, Str *nm, Value *out)
{
    Nd *a = AS_ND(o);
    const char *s = nm->s;
    if (!strcmp(s, "shape")) {
        Value tv[ND_MAXD];
        int k;
        for (k = 0; k < a->nd; k++) tv[k] = mkint(a->shape[k]);
        *out = mktuple_from(tv, a->nd);
        return 1;
    }
    if (!strcmp(s, "ndim")) { *out = mkint(a->nd); return 1; }
    if (!strcmp(s, "size")) { *out = mkint(a->size); return 1; }
    if (!strcmp(s, "dtype")) { *out = mkcstr(dt_name(a->dt)); return 1; }
    if (!strcmp(s, "itemsize")) { *out = mkint(a->dt == ND_BOOL ? 1 : 8); return 1; }
    if (!strcmp(s, "nbytes")) { *out = mkint(a->size * (a->dt == ND_BOOL ? 1 : 8)); return 1; }
    if (!strcmp(s, "T")) { *out = ndv(nd_transposed(a)); return 1; }
    if (!strcmp(s, "real")) { *out = o; return 1; }
    if (!strcmp(s, "flat")) { long n = a->size; *out = ndv(nd_reshaped(a, 1, &n)); return 1; }
    return 0;
}

/* ---- module ------------------------------------------------------------ */
static int np_float_done;
static void np_float_methods(void)
{
    if (np_float_done) return;
    np_float_done = 1;
    def_bi(cls_float, "round", f_np_round);
    class_epoch++;
}
static void np_init(void)
{
    Module *m;
    Class *c;
    c = (Class *)gc_alloc(sizeof(Class), T_CLASS);
    c->name = intern_c("ndarray"); c->base = cls_object;
    c->ns.k = NULL; c->ns.v = NULL; c->ns.n = c->ns.cap = 0;
    c->ctor = np_array; c->flags = CF_BUILTIN;
    c->mro = NULL; c->nmro = 0; c->bases = NULL; c->nbases = 0; c->paycls = NULL; c->modname = NULL;
    cls_ndarray = c;
    def_bi(cls_ndarray, "tolist", nd_tolist); def_bi(cls_ndarray, "copy", nd_copy);
    def_bi(cls_ndarray, "reshape", nd_reshape); def_bi(cls_ndarray, "flatten", nd_flatten);
    def_bi(cls_ndarray, "ravel", nd_flatten); def_bi(cls_ndarray, "astype", nd_astype_m);
    def_bi(cls_ndarray, "item", nd_item_m); def_bi(cls_ndarray, "transpose", nd_transpose);
    def_bi(cls_ndarray, "sort", nd_sort_m); def_bi(cls_ndarray, "argsort", np_argsort);
    def_bi(cls_ndarray, "sum", np_sum); def_bi(cls_ndarray, "mean", np_mean);
    def_bi(cls_ndarray, "min", np_min); def_bi(cls_ndarray, "max", np_max);
    def_bi(cls_ndarray, "prod", np_prod); def_bi(cls_ndarray, "argmax", np_argmax);
    def_bi(cls_ndarray, "argmin", np_argmin); def_bi(cls_ndarray, "cumsum", np_cumsum);
    def_bi(cls_ndarray, "cumprod", np_cumprod); def_bi(cls_ndarray, "std", np_std);
    def_bi(cls_ndarray, "var", np_var); def_bi(cls_ndarray, "round", np_round);
    def_bi(cls_ndarray, "dot", np_dot); def_bi(cls_ndarray, "any", np_any);
    def_bi(cls_ndarray, "all", np_all); def_bi(cls_ndarray, "clip", np_clip);
    def_bi(cls_ndarray, "nonzero", np_nonzero); def_bi(cls_ndarray, "trace", np_trace);
    m = new_module("numpy");
    mod_def(m, "array", np_array); mod_def(m, "asarray", np_asarray);
    mod_def(m, "zeros", np_zeros); mod_def(m, "ones", np_ones); mod_def(m, "empty", np_zeros);
    mod_def(m, "full", np_full); mod_def(m, "zeros_like", np_zeros_like);
    mod_def(m, "ones_like", np_ones_like); mod_def(m, "empty_like", np_zeros_like);
    mod_def(m, "full_like", np_full_like); mod_def(m, "eye", np_eye);
    mod_def(m, "identity", np_eye);
    mod_def(m, "arange", np_arange); mod_def(m, "linspace", np_linspace);
    mod_def(m, "sqrt", np_sqrt); mod_def(m, "abs", np_abs); mod_def(m, "absolute", np_abs);
    mod_def(m, "exp", np_exp); mod_def(m, "sin", np_sin); mod_def(m, "cos", np_cos);
    mod_def(m, "tan", np_tan); mod_def(m, "log", np_log); mod_def(m, "log10", np_log10);
    mod_def(m, "log2", np_log2); mod_def(m, "floor", np_floor); mod_def(m, "ceil", np_ceil);
    mod_def(m, "tanh", np_tanh); mod_def(m, "sinh", np_sinh); mod_def(m, "cosh", np_cosh);
    mod_def(m, "arcsin", np_arcsin); mod_def(m, "arccos", np_arccos);
    mod_def(m, "arctan", np_arctan); mod_def(m, "sign", np_sign);
    mod_def(m, "square", np_square); mod_def(m, "negative", np_negative);
    mod_def(m, "isnan", np_isnan); mod_def(m, "isinf", np_isinf);
    mod_def(m, "isfinite", np_isfinite);
    mod_def(m, "add", np_add); mod_def(m, "subtract", np_subtract);
    mod_def(m, "multiply", np_multiply); mod_def(m, "divide", np_divide);
    mod_def(m, "true_divide", np_divide); mod_def(m, "floor_divide", np_floor_divide);
    mod_def(m, "mod", np_mod); mod_def(m, "remainder", np_mod); mod_def(m, "power", np_power);
    mod_def(m, "maximum", np_maximum); mod_def(m, "minimum", np_minimum);
    mod_def(m, "where", np_where); mod_def(m, "nonzero", np_nonzero);
    mod_def(m, "concatenate", np_concatenate); mod_def(m, "vstack", np_vstack);
    mod_def(m, "hstack", np_hstack); mod_def(m, "stack", np_stack);
    mod_def(m, "sort", np_sort); mod_def(m, "argsort", np_argsort);
    mod_def(m, "unique", np_unique); mod_def(m, "median", np_median);
    mod_def(m, "dot", np_dot); mod_def(m, "matmul", np_matmul);
    mod_def(m, "transpose", nd_transpose); mod_def(m, "diag", np_diag);
    mod_def(m, "trace", np_trace); mod_def(m, "cumsum", np_cumsum);
    mod_def(m, "cumprod", np_cumprod);
    mod_def(m, "clip", np_clip); mod_def(m, "any", np_any); mod_def(m, "all", np_all);
    mod_def(m, "allclose", np_allclose); mod_def(m, "array_equal", np_array_equal);
    mod_def(m, "std", np_std); mod_def(m, "var", np_var);
    mod_def(m, "round", np_round); mod_def(m, "around", np_round);
    mod_def(m, "sum", np_sum); mod_def(m, "mean", np_mean); mod_def(m, "prod", np_prod);
    mod_def(m, "min", np_min); mod_def(m, "max", np_max); mod_def(m, "amin", np_min);
    mod_def(m, "amax", np_max); mod_def(m, "argmin", np_argmin); mod_def(m, "argmax", np_argmax);
    mod_def(m, "reshape", nd_reshape); mod_def(m, "ravel", nd_flatten);
    mod_def(m, "copy", nd_copy); mod_def(m, "count_nonzero", np_count_nonzero);
    mod_val(m, "pi", mkfloat(3.14159265358979323846));
    mod_val(m, "e", mkfloat(2.71828182845904523536));
    mod_val(m, "inf", mkfloat(HUGE_VAL));
    mod_val(m, "nan", mkfloat(HUGE_VAL - HUGE_VAL));
    mod_val(m, "newaxis", NONE_V);
    mod_val(m, "ndarray", mkobj(cls_ndarray, T_CLASS));
    mod_val(m, "float64", mkobj(cls_float, T_CLASS));
    mod_val(m, "int64", mkobj(cls_int, T_CLASS));
    mod_val(m, "bool_", mkobj(cls_bool, T_CLASS));
    np_init2(m);
    la_init(m);
    rnd_init(m);
}
/* ===== p7_la.c ===== */
/* =========================================================================
   p7_la.c   numpy.linalg (dense real matrices, double precision)
   ========================================================================= */
static Class *cls_LinAlgError;

/* portable C89 replacements for hypot / log1p / expm1 / asinh / ... */
static double n_hypot(double a, double b)
{
    double t;
    a = fabs(a); b = fabs(b);
    if (a < b) { t = a; a = b; b = t; }
    if (a == 0.0) return 0.0;
    if (a - a != 0.0) return a;
    t = b / a;
    return a * sqrt(1.0 + t * t);
}
static double n_log1p(double x)
{
    double u = 1.0 + x;
    if (u == 1.0) return x;
    return log(u) * x / (u - 1.0);
}
static double n_expm1(double x)
{
    double u = exp(x), um1;
    if (u == 1.0) return x;
    um1 = u - 1.0;
    if (um1 == -1.0) return -1.0;
    if (u - u != 0) return u;
    return um1 * x / log(u);
}
static double n_asinh(double v)
{
    double a = fabs(v), r;
    if (a < 1e-8 || a - a != 0.0) return v;
    r = a > 1e8 ? log(a) + 0.69314718055994530942 : n_log1p(a + a * a / (1.0 + sqrt(1.0 + a * a)));
    return v < 0 ? -r : r;
}
static double n_acosh(double v)
{
    if (v < 1.0) return HUGE_VAL - HUGE_VAL;
    if (v > 1e8) return log(v) + 0.69314718055994530942;
    return n_log1p((v - 1.0) + sqrt((v - 1.0) * (v + 1.0)));
}
static double n_atanh(double v)
{
    if (v > 1.0 || v < -1.0) return HUGE_VAL - HUGE_VAL;
    if (v == 1.0) return HUGE_VAL;
    if (v == -1.0) return -HUGE_VAL;
    return 0.5 * n_log1p(2.0 * v / (1.0 - v));
}
static double n_cbrt(double v)
{
    double a, r;
    if (v == 0 || v - v != 0) return v;
    a = fabs(v);
    r = pow(a, 1.0 / 3.0);
    r = r - (r * r * r - a) / (3.0 * r * r);
    return v < 0 ? -r : r;
}
static double n_trunc(double v)
{
    return v < 0 ? ceil(v) : floor(v);
}

static Value np_arg(Args *x, int pos, const char *nm)
{
    if (pos < x->n) return A(pos);
    return kwget(x, nm);
}
static int np_has(Value v)
{
    return v.t != T_UNSET && v.t != T_NONE;
}
/* fresh float copy of any array-like */
static Nd *np_f(Value v)
{
    NdTmp t;
    Nd *a = nd_coerce(v, &t);
    return nd_astype(a, ND_FLOAT);
}
static Nd *la_mat(Value v, long *m, long *n)
{
    Nd *a = np_f(v);
    if (a->nd < 2)
        throw_new(cls_LinAlgError, "%d-dimensional array given. Array must be at least two-dimensional", a->nd);
    if (a->nd > 2) throw_new(cls_NotImplementedError, "linalg: stacked matrices are not supported");
    *m = a->shape[0]; *n = a->shape[1];
    return a;
}
static Nd *la_sq(Value v, long *n)
{
    long m;
    Nd *a = la_mat(v, &m, n);
    if (m != *n) throw_new(cls_LinAlgError, "Last 2 dimensions of the array must be square");
    return a;
}
static Nd *mat_new(long m, long n)
{
    long sh[2];
    sh[0] = m; sh[1] = n;
    return nd_new(ND_FLOAT, 2, sh);
}
static Nd *vec_new(long n)
{
    return nd_new1(ND_FLOAT, n);
}

/* LU with partial pivoting, in place; returns sign (+1/-1) or 0 when singular */
static int lu_dec(double *a, long n, long *piv)
{
    long i, j, k, p;
    int sign = 1, sing = 0;
    for (k = 0; k < n; k++) {
        double mx = fabs(a[k * n + k]);
        p = k;
        for (i = k + 1; i < n; i++)
            if (fabs(a[i * n + k]) > mx) { mx = fabs(a[i * n + k]); p = i; }
        piv[k] = p;
        if (mx == 0.0) { sing = 1; continue; }
        if (p != k) {
            for (j = 0; j < n; j++) {
                double t = a[k * n + j]; a[k * n + j] = a[p * n + j]; a[p * n + j] = t;
            }
            sign = -sign;
        }
        for (i = k + 1; i < n; i++) {
            double f = a[i * n + k] / a[k * n + k];
            a[i * n + k] = f;
            if (f != 0.0) for (j = k + 1; j < n; j++) a[i * n + j] -= f * a[k * n + j];
        }
    }
    return sing ? 0 : sign;
}
/* solve with an LU factorisation: b is n x nrhs (row-major), overwritten */
static void lu_solve(const double *lu, long n, const long *piv, double *b, long nrhs)
{
    long i, j, k;
    for (k = 0; k < n; k++)
        if (piv[k] != k)
            for (j = 0; j < nrhs; j++) {
                double t = b[k * nrhs + j]; b[k * nrhs + j] = b[piv[k] * nrhs + j]; b[piv[k] * nrhs + j] = t;
            }
    for (i = 0; i < n; i++)
        for (k = 0; k < i; k++) {
            double f = lu[i * n + k];
            if (f != 0.0) for (j = 0; j < nrhs; j++) b[i * nrhs + j] -= f * b[k * nrhs + j];
        }
    for (i = n - 1; i >= 0; i--) {
        for (k = i + 1; k < n; k++) {
            double f = lu[i * n + k];
            if (f != 0.0) for (j = 0; j < nrhs; j++) b[i * nrhs + j] -= f * b[k * nrhs + j];
        }
        for (j = 0; j < nrhs; j++) b[i * nrhs + j] /= lu[i * n + i];
    }
}
BI(la_det)
{
    long n, i, *piv;
    int sg;
    Nd *a;
    double d;
    need(x, 1, 1, "det");
    a = la_sq(A(0), &n);
    if (n == 0) return mkfloat(1.0);
    piv = (long *)xmalloc(sizeof(long) * (size_t)n);
    sg = lu_dec(a->dv, n, piv);
    free(piv);
    if (sg == 0) return mkfloat(0.0);
    d = (double)sg;
    for (i = 0; i < n; i++) d *= a->dv[i * n + i];
    return mkfloat(d);
}
BI(la_slogdet)
{
    long n, i, *piv;
    int sg;
    Nd *a;
    double ld = 0.0, sgn;
    Value tv[2];
    need(x, 1, 1, "slogdet");
    a = la_sq(A(0), &n);
    piv = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    sg = lu_dec(a->dv, n, piv);
    free(piv);
    if (sg == 0) { tv[0] = mkfloat(0.0); tv[1] = mkfloat(-HUGE_VAL); return mktuple_from(tv, 2); }
    sgn = (double)sg;
    for (i = 0; i < n; i++) {
        double d = a->dv[i * n + i];
        if (d < 0) { sgn = -sgn; d = -d; }
        ld += log(d);
    }
    tv[0] = mkfloat(sgn); tv[1] = mkfloat(ld);
    return mktuple_from(tv, 2);
}
BI(la_inv)
{
    long n, i, *piv;
    Nd *a, *r;
    need(x, 1, 1, "inv");
    a = la_sq(A(0), &n);
    r = mat_new(n, n);
    piv = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    if (n > 0 && lu_dec(a->dv, n, piv) == 0) {
        free(piv);
        throw_new(cls_LinAlgError, "Singular matrix");
    }
    for (i = 0; i < n * n; i++) r->dv[i] = 0.0;
    for (i = 0; i < n; i++) r->dv[i * n + i] = 1.0;
    if (n > 0) lu_solve(a->dv, n, piv, r->dv, n);
    free(piv);
    return ndv(r);
}
BI(la_solve)
{
    long n, i, *piv, nrhs;
    Nd *a, *b, *r;
    need(x, 2, 2, "solve");
    a = la_sq(A(0), &n);
    b = np_f(A(1));
    if (b->nd < 1 || b->nd > 2 || b->shape[0] != n)
        throw_new(cls_ValueError, "solve: Input operand 1 has a mismatch in its core dimension 0, with gufunc signature (m,m),(m,n)->(m,n) (size %ld is different from %ld)",
                  b->nd > 0 ? b->shape[0] : 0L, n);
    nrhs = b->nd == 1 ? 1 : b->shape[1];
    r = nd_astype(b, ND_FLOAT);
    piv = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    if (n > 0 && lu_dec(a->dv, n, piv) == 0) {
        free(piv);
        throw_new(cls_LinAlgError, "Singular matrix");
    }
    if (n > 0) lu_solve(a->dv, n, piv, r->dv, nrhs);
    free(piv);
    (void)i;
    return ndv(r);
}
BI(la_cholesky)
{
    long n, i, j, k;
    Nd *a, *r;
    need(x, 1, 1, "cholesky");
    a = la_sq(A(0), &n);
    r = mat_new(n, n);
    for (i = 0; i < n * n; i++) r->dv[i] = 0.0;
    for (i = 0; i < n; i++)
        for (j = 0; j <= i; j++) {
            double s = a->dv[i * n + j];
            for (k = 0; k < j; k++) s -= r->dv[i * n + k] * r->dv[j * n + k];
            if (i == j) {
                if (s <= 0.0) throw_new(cls_LinAlgError, "Matrix is not positive definite");
                r->dv[i * n + i] = sqrt(s);
            } else r->dv[i * n + j] = s / r->dv[j * n + j];
        }
    return ndv(r);
}

/* symmetric eigenproblem: cyclic Jacobi.  a (n x n, destroyed), w ascending,
   v receives eigenvectors as columns */
static void jacobi_eig(double *a, long n, double *w, double *v)
{
    long i, j, p, q, sweep, k;
    for (i = 0; i < n * n; i++) v[i] = 0.0;
    for (i = 0; i < n; i++) v[i * n + i] = 1.0;
    for (sweep = 0; sweep < 100; sweep++) {
        double off = 0.0, dsum = 0.0;
        for (p = 0; p < n; p++) {
            dsum += a[p * n + p] * a[p * n + p];
            for (q = p + 1; q < n; q++) off += a[p * n + q] * a[p * n + q];
        }
        if (off == 0.0 || off < 1e-32 * dsum) break;
        for (p = 0; p < n - 1; p++)
            for (q = p + 1; q < n; q++) {
                double apq = a[p * n + q], theta, t, c, s;
                if (apq == 0.0) continue;
                theta = (a[q * n + q] - a[p * n + p]) / (2.0 * apq);
                t = (theta >= 0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
                c = 1.0 / sqrt(t * t + 1.0);
                s = t * c;
                for (k = 0; k < n; k++) {
                    double akp = a[k * n + p], akq = a[k * n + q];
                    a[k * n + p] = c * akp - s * akq;
                    a[k * n + q] = s * akp + c * akq;
                }
                for (k = 0; k < n; k++) {
                    double apk = a[p * n + k], aqk = a[q * n + k];
                    a[p * n + k] = c * apk - s * aqk;
                    a[q * n + k] = s * apk + c * aqk;
                }
                for (k = 0; k < n; k++) {
                    double vkp = v[k * n + p], vkq = v[k * n + q];
                    v[k * n + p] = c * vkp - s * vkq;
                    v[k * n + q] = s * vkp + c * vkq;
                }
            }
    }
    for (i = 0; i < n; i++) w[i] = a[i * n + i];
    /* selection sort ascending, swapping columns of v */
    for (i = 0; i < n - 1; i++) {
        long mi = i;
        for (j = i + 1; j < n; j++) if (w[j] < w[mi]) mi = j;
        if (mi != i) {
            double t = w[i]; w[i] = w[mi]; w[mi] = t;
            for (k = 0; k < n; k++) {
                t = v[k * n + i]; v[k * n + i] = v[k * n + mi]; v[k * n + mi] = t;
            }
        }
    }
}
/* copy one triangle over the other so the matrix is exactly symmetric */
static void symmetrize(double *a, long n, int upper)
{
    long i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < i; j++) {
            if (upper) a[i * n + j] = a[j * n + i];
            else a[j * n + i] = a[i * n + j];
        }
}
static int uplo_arg(Args *x, int pos)
{
    Value u = np_arg(x, pos, "UPLO");
    if (u.t == T_STR) {
        const char *s = AS_STR(u)->s;
        if (s[0] == 'U' || s[0] == 'u') return 1;
    }
    return 0;
}
BI(la_eigvalsh)
{
    long n;
    Nd *a, *w;
    double *v;
    need(x, 1, 2, "eigvalsh");
    a = la_sq(A(0), &n);
    symmetrize(a->dv, n, uplo_arg(x, 1));
    w = vec_new(n);
    v = (double *)xmalloc(sizeof(double) * (size_t)(n * n > 0 ? n * n : 1));
    jacobi_eig(a->dv, n, w->dv, v);
    free(v);
    return ndv(w);
}
/* make each eigenvector's largest component positive-free of ambiguity:
   LAPACK gives no guarantee, so normalise the sign deterministically */
static void fix_vec_signs(double *v, long n)
{
    long i, j;
    for (j = 0; j < n; j++) {
        double mx = 0.0, sgn = 1.0;
        for (i = 0; i < n; i++)
            if (fabs(v[i * n + j]) > fabs(mx) * (1.0 + 1e-12)) mx = v[i * n + j];
        if (mx < 0) sgn = -1.0;
        if (sgn < 0) for (i = 0; i < n; i++) v[i * n + j] = -v[i * n + j];
    }
}
BI(la_eigh)
{
    long n;
    Nd *a, *w, *v;
    Value tv[2];
    need(x, 1, 2, "eigh");
    a = la_sq(A(0), &n);
    symmetrize(a->dv, n, uplo_arg(x, 1));
    w = vec_new(n);
    v = mat_new(n, n);
    jacobi_eig(a->dv, n, w->dv, v->dv);
    fix_vec_signs(v->dv, n);
    tv[0] = ndv(w); tv[1] = ndv(v);
    return mktuple_from(tv, 2);
}

/* general real matrix: Hessenberg reduction + shifted QR; returns 0 on
   success, sets wr/wi */
static void hessenberg(double *a, long n)
{
    long k, i, j;
    double *v = (double *)xmalloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    for (k = 0; k + 2 < n; k++) {
        double alpha = 0.0, nrm = 0.0, vn = 0.0;
        for (i = k + 1; i < n; i++) nrm += a[i * n + k] * a[i * n + k];
        nrm = sqrt(nrm);
        if (nrm == 0.0) continue;
        alpha = a[(k + 1) * n + k] > 0 ? -nrm : nrm;
        for (i = k + 1; i < n; i++) v[i] = a[i * n + k];
        v[k + 1] -= alpha;
        for (i = k + 1; i < n; i++) vn += v[i] * v[i];
        if (vn == 0.0) continue;
        for (j = 0; j < n; j++) {
            double s = 0.0;
            for (i = k + 1; i < n; i++) s += v[i] * a[i * n + j];
            s = 2.0 * s / vn;
            for (i = k + 1; i < n; i++) a[i * n + j] -= s * v[i];
        }
        for (i = 0; i < n; i++) {
            double s = 0.0;
            for (j = k + 1; j < n; j++) s += a[i * n + j] * v[j];
            s = 2.0 * s / vn;
            for (j = k + 1; j < n; j++) a[i * n + j] -= s * v[j];
        }
    }
    free(v);
}
static void qr_eigs(double *a, long n, double *wr, double *wi)
{
    long hi = n - 1, i, k, iter = 0, total = 0;
    double *cs = (double *)xmalloc(sizeof(double) * (size_t)(2 * n + 2));
    double *sn = cs + n + 1;
    hessenberg(a, n);
    while (hi >= 0) {
        long l;
        double mu;
        if (hi == 0) { wr[0] = a[0]; wi[0] = 0.0; break; }
        /* look for a small subdiagonal element */
        for (l = hi; l > 0; l--) {
            double s = fabs(a[(l - 1) * n + l - 1]) + fabs(a[l * n + l]);
            if (s == 0.0) s = 1.0;
            if (fabs(a[l * n + l - 1]) <= DBL_EPSILON * s) { a[l * n + l - 1] = 0.0; break; }
        }
        if (l == hi) {
            wr[hi] = a[hi * n + hi]; wi[hi] = 0.0;
            hi--; iter = 0;
            continue;
        }
        if (l == hi - 1) {
            double p = a[(hi - 1) * n + hi - 1], q = a[(hi - 1) * n + hi];
            double r = a[hi * n + hi - 1], s = a[hi * n + hi];
            double m = (p + s) / 2.0, disc = (p - s) * (p - s) / 4.0 + q * r;
            if (disc >= 0) {
                double sq = sqrt(disc);
                wr[hi - 1] = m + sq; wr[hi] = m - sq;
                if (wr[hi - 1] != 0.0 && wr[hi] != 0.0) {
                    /* refine the smaller root through the determinant */
                    double det = p * s - q * r;
                    if (fabs(wr[hi - 1]) > fabs(wr[hi])) wr[hi] = det / wr[hi - 1];
                    else wr[hi - 1] = det / wr[hi];
                }
                wi[hi - 1] = wi[hi] = 0.0;
            } else {
                wr[hi - 1] = wr[hi] = m;
                wi[hi - 1] = sqrt(-disc); wi[hi] = -wi[hi - 1];
            }
            hi -= 2; iter = 0;
            continue;
        }
        if (++total > 500 * n + 1000) {
            free(cs);
            throw_new(cls_LinAlgError, "Eigenvalues did not converge");
        }
        /* Wilkinson shift from the trailing 2x2 block (real part if complex) */
        {
            double p = a[(hi - 1) * n + hi - 1], q = a[(hi - 1) * n + hi];
            double r = a[hi * n + hi - 1], s = a[hi * n + hi];
            double m = (p + s) / 2.0, disc = (p - s) * (p - s) / 4.0 + q * r;
            if (disc >= 0) {
                double sq = sqrt(disc), e1 = m + sq, e2 = m - sq;
                mu = fabs(e1 - s) < fabs(e2 - s) ? e1 : e2;
            } else mu = m;
            if (++iter % 11 == 10) mu += fabs(a[hi * n + hi - 1]) * 0.75;
        }
        for (i = l; i <= hi; i++) a[i * n + i] -= mu;
        for (k = l; k < hi; k++) {
            double f = a[k * n + k], g = a[(k + 1) * n + k], r = n_hypot(f, g), c, s;
            long j;
            if (r == 0.0) { c = 1.0; s = 0.0; } else { c = f / r; s = g / r; }
            cs[k] = c; sn[k] = s;
            for (j = k; j <= hi; j++) {
                double t1 = a[k * n + j], t2 = a[(k + 1) * n + j];
                a[k * n + j] = c * t1 + s * t2;
                a[(k + 1) * n + j] = -s * t1 + c * t2;
            }
        }
        for (k = l; k < hi; k++) {
            double c = cs[k], s = sn[k];
            long top = k + 2 < hi ? k + 2 : hi, r2;
            for (r2 = l; r2 <= top; r2++) {
                double t1 = a[r2 * n + k], t2 = a[r2 * n + k + 1];
                a[r2 * n + k] = c * t1 + s * t2;
                a[r2 * n + k + 1] = -s * t1 + c * t2;
            }
        }
        for (i = l; i <= hi; i++) a[i * n + i] += mu;
    }
    free(cs);
}
BI(la_eigvals)
{
    long n, i;
    Nd *a, *w;
    double *wi;
    need(x, 1, 1, "eigvals");
    a = la_sq(A(0), &n);
    w = vec_new(n);
    wi = (double *)xmalloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    if (n > 0) qr_eigs(a->dv, n, w->dv, wi);
    for (i = 0; i < n; i++)
        if (wi[i] != 0.0) {
            free(wi);
            throw_new(cls_NotImplementedError, "eigvals: complex eigenvalues are not supported");
        }
    free(wi);
    /* ascending order is not guaranteed by LAPACK; keep QR deflation order reversed
       so that a triangular matrix returns its diagonal in order */
    for (i = 0; i < n / 2; i++) { double t = w->dv[i]; w->dv[i] = w->dv[n - 1 - i]; w->dv[n - 1 - i] = t; }
    return ndv(w);
}
/* eig: eigenvalues by QR, eigenvectors by solving (A - w I) v = 0 with inverse iteration */
BI(la_eig)
{
    long n, i, j, it, *piv;
    Nd *a, *w, *v, *a0;
    double *wi, *b, *lu;
    Value tv[2];
    need(x, 1, 1, "eig");
    a = la_sq(A(0), &n);
    a0 = nd_astype(a, ND_FLOAT);
    w = vec_new(n);
    v = mat_new(n, n);
    wi = (double *)xmalloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    if (n > 0) qr_eigs(a->dv, n, w->dv, wi);
    for (i = 0; i < n; i++)
        if (wi[i] != 0.0) {
            free(wi);
            throw_new(cls_NotImplementedError, "eig: complex eigenvalues are not supported");
        }
    free(wi);
    for (i = 0; i < n / 2; i++) { double t = w->dv[i]; w->dv[i] = w->dv[n - 1 - i]; w->dv[n - 1 - i] = t; }
    lu = (double *)xmalloc(sizeof(double) * (size_t)(n * n + n + 1));
    b = lu + n * n;
    piv = (long *)xmalloc(sizeof(long) * (size_t)(n > 0 ? n : 1));
    for (j = 0; j < n; j++) {
        double nrm = 0.0, shift = w->dv[j];
        double scale = 0.0;
        for (i = 0; i < n * n; i++) if (fabs(a0->dv[i]) > scale) scale = fabs(a0->dv[i]);
        if (scale == 0.0) scale = 1.0;
        for (i = 0; i < n * n; i++) lu[i] = a0->dv[i];
        for (i = 0; i < n; i++) lu[i * n + i] -= shift + scale * 1e-14;
        lu_dec(lu, n, piv);
        for (i = 0; i < n; i++) if (lu[i * n + i] == 0.0) lu[i * n + i] = scale * 1e-300;
        for (i = 0; i < n; i++) b[i] = 1.0 / sqrt((double)n) + 0.01 * (double)i;
        for (it = 0; it < 4; it++) {
            nrm = 0.0;
            lu_solve(lu, n, piv, b, 1);
            for (i = 0; i < n; i++) nrm += b[i] * b[i];
            nrm = sqrt(nrm);
            if (nrm == 0.0 || nrm != nrm) break;
            for (i = 0; i < n; i++) b[i] /= nrm;
        }
        for (i = 0; i < n; i++) v->dv[i * n + j] = b[i];
    }
    fix_vec_signs(v->dv, n);
    free(lu); free(piv);
    tv[0] = ndv(w); tv[1] = ndv(v);
    return mktuple_from(tv, 2);
}

/* one-sided Jacobi SVD of an m x n matrix with m >= n.
   u: m x n (overwritten with left vectors scaled), v: n x n, s: n singular values */
static void svd_jacobi(double *u, long m, long n, double *s, double *v)
{
    long i, p, q, sweep;
    for (i = 0; i < n * n; i++) v[i] = 0.0;
    for (i = 0; i < n; i++) v[i * n + i] = 1.0;
    for (sweep = 0; sweep < 60; sweep++) {
        int rotated = 0;
        for (p = 0; p < n - 1; p++)
            for (q = p + 1; q < n; q++) {
                double al = 0.0, be = 0.0, ga = 0.0, zeta, t, c, sn;
                for (i = 0; i < m; i++) {
                    al += u[i * n + p] * u[i * n + p];
                    be += u[i * n + q] * u[i * n + q];
                    ga += u[i * n + p] * u[i * n + q];
                }
                if (ga == 0.0 || fabs(ga) <= 1e-15 * sqrt(al * be)) continue;
                rotated = 1;
                zeta = (be - al) / (2.0 * ga);
                t = (zeta >= 0 ? 1.0 : -1.0) / (fabs(zeta) + sqrt(1.0 + zeta * zeta));
                c = 1.0 / sqrt(1.0 + t * t);
                sn = c * t;
                for (i = 0; i < m; i++) {
                    double up = u[i * n + p], uq = u[i * n + q];
                    u[i * n + p] = c * up - sn * uq;
                    u[i * n + q] = sn * up + c * uq;
                }
                for (i = 0; i < n; i++) {
                    double vp = v[i * n + p], vq = v[i * n + q];
                    v[i * n + p] = c * vp - sn * vq;
                    v[i * n + q] = sn * vp + c * vq;
                }
            }
        if (!rotated) break;
    }
    for (q = 0; q < n; q++) {
        double nr = 0.0;
        for (i = 0; i < m; i++) nr += u[i * n + q] * u[i * n + q];
        s[q] = sqrt(nr);
    }
    /* sort descending */
    for (p = 0; p < n - 1; p++) {
        long mi = p;
        for (q = p + 1; q < n; q++) if (s[q] > s[mi]) mi = q;
        if (mi != p) {
            double t = s[p]; s[p] = s[mi]; s[mi] = t;
            for (i = 0; i < m; i++) { t = u[i * n + p]; u[i * n + p] = u[i * n + mi]; u[i * n + mi] = t; }
            for (i = 0; i < n; i++) { t = v[i * n + p]; v[i * n + p] = v[i * n + mi]; v[i * n + mi] = t; }
        }
    }
    for (q = 0; q < n; q++)
        if (s[q] > 0.0) for (i = 0; i < m; i++) u[i * n + q] /= s[q];
}
/* fill the columns of q (m x nc, row-major) flagged invalid so that all columns are orthonormal */
static void orth_complete(double *q, long m, long nc, const char *valid)
{
    long j, c, i, k;
    double *w = (double *)xmalloc(sizeof(double) * (size_t)(m > 0 ? m : 1));
    char *ok = (char *)xmalloc((size_t)(nc > 0 ? nc : 1));
    for (j = 0; j < nc; j++) ok[j] = valid[j];
    for (j = 0; j < nc; j++) {
        double best = -1.0;
        long bc = 0;
        if (ok[j]) continue;
        for (c = 0; c < m; c++) {
            double nr;
            for (i = 0; i < m; i++) w[i] = i == c ? 1.0 : 0.0;
            for (k = 0; k < nc; k++) {
                double d = 0.0;
                if (!ok[k]) continue;
                for (i = 0; i < m; i++) d += w[i] * q[i * nc + k];
                for (i = 0; i < m; i++) w[i] -= d * q[i * nc + k];
            }
            nr = 0.0;
            for (i = 0; i < m; i++) nr += w[i] * w[i];
            if (nr > best) { best = nr; bc = c; }
        }
        for (i = 0; i < m; i++) w[i] = i == bc ? 1.0 : 0.0;
        for (c = 0; c < 2; c++)
            for (k = 0; k < nc; k++) {
                double d = 0.0;
                if (!ok[k]) continue;
                for (i = 0; i < m; i++) d += w[i] * q[i * nc + k];
                for (i = 0; i < m; i++) w[i] -= d * q[i * nc + k];
            }
        best = 0.0;
        for (i = 0; i < m; i++) best += w[i] * w[i];
        best = sqrt(best);
        for (i = 0; i < m; i++) q[i * nc + j] = best > 0 ? w[i] / best : 0.0;
        ok[j] = 1;
    }
    free(w); free(ok);
}
/* full SVD driver: returns u (m x r), s (r), vt (r x n), r = min(m, n) */
static void svd_thin(Nd *a, long m, long n, double **pu, double **ps, double **pvt)
{
    long r = m < n ? m : n, i, j;
    double *u, *s, *vt, *work, *v;
    char *valid;
    u = (double *)xmalloc(sizeof(double) * (size_t)(m * r + 1));
    s = (double *)xmalloc(sizeof(double) * (size_t)(r + 1));
    vt = (double *)xmalloc(sizeof(double) * (size_t)(r * n + 1));
    valid = (char *)xmalloc((size_t)(r + 1));
    memset(valid, 0, (size_t)(r + 1));
    if (m >= n) {
        v = (double *)xmalloc(sizeof(double) * (size_t)(n * n + 1));
        memcpy(u, a->dv, sizeof(double) * (size_t)(m * n));
        svd_jacobi(u, m, n, s, v);
        for (i = 0; i < n; i++) for (j = 0; j < n; j++) vt[i * n + j] = v[j * n + i];
        free(v);
        for (j = 0; j < r; j++) valid[j] = s[j] > 0.0;
        orth_complete(u, m, r, valid);
    } else {
        /* A^T = U' S V'^T  =>  A = V' S U'^T */
        work = (double *)xmalloc(sizeof(double) * (size_t)(n * m + 1));
        v = (double *)xmalloc(sizeof(double) * (size_t)(m * m + 1));
        for (i = 0; i < m; i++) for (j = 0; j < n; j++) work[j * m + i] = a->dv[i * n + j];
        svd_jacobi(work, n, m, s, v);
        for (i = 0; i < m; i++) for (j = 0; j < m; j++) u[i * m + j] = v[i * m + j];
        for (i = 0; i < m; i++) for (j = 0; j < n; j++) vt[i * n + j] = work[j * m + i];
        /* vt is m x n; complete invalid rows through the transposed view */
        for (j = 0; j < r; j++) valid[j] = s[j] > 0.0;
        {
            double *tq = (double *)xmalloc(sizeof(double) * (size_t)(n * m + 1));
            for (i = 0; i < m; i++) for (j = 0; j < n; j++) tq[j * m + i] = vt[i * n + j];
            orth_complete(tq, n, m, valid);
            for (i = 0; i < m; i++) for (j = 0; j < n; j++) vt[i * n + j] = tq[j * m + i];
            free(tq);
        }
        free(work); free(v);
    }
    free(valid);
    *pu = u; *ps = s; *pvt = vt;
}
BI(la_svd)
{
    long m, n, r, i, j;
    Nd *a, *S;
    double *u, *s, *vt;
    int full = 1, cuv = 1;
    Value fv = np_arg(x, 1, "full_matrices"), cv = np_arg(x, 2, "compute_uv");
    Value tv[3];
    need(x, 1, 4, "svd");
    if (np_has(fv)) full = py_truthy(fv);
    if (np_has(cv)) cuv = py_truthy(cv);
    a = la_mat(A(0), &m, &n);
    r = m < n ? m : n;
    svd_thin(a, m, n, &u, &s, &vt);
    S = vec_new(r);
    for (i = 0; i < r; i++) S->dv[i] = s[i];
    if (!cuv) { free(u); free(s); free(vt); return ndv(S); }
    {
        long uc = full ? m : r, vr = full ? n : r;
        Nd *U = mat_new(m, uc), *V = mat_new(vr, n);
        char *valid;
        double *tmp;
        if (uc > r) {
            tmp = (double *)xmalloc(sizeof(double) * (size_t)(m * uc + 1));
            valid = (char *)xmalloc((size_t)uc + 1);
            for (j = 0; j < uc; j++) valid[j] = j < r;
            for (i = 0; i < m; i++) for (j = 0; j < uc; j++) tmp[i * uc + j] = j < r ? u[i * r + j] : 0.0;
            orth_complete(tmp, m, uc, valid);
            memcpy(U->dv, tmp, sizeof(double) * (size_t)(m * uc));
            free(tmp); free(valid);
        } else memcpy(U->dv, u, sizeof(double) * (size_t)(m * r));
        if (vr > r) {
            tmp = (double *)xmalloc(sizeof(double) * (size_t)(n * vr + 1));
            valid = (char *)xmalloc((size_t)vr + 1);
            for (j = 0; j < vr; j++) valid[j] = j < r;
            for (i = 0; i < n; i++) for (j = 0; j < vr; j++) tmp[i * vr + j] = j < r ? vt[j * n + i] : 0.0;
            orth_complete(tmp, n, vr, valid);
            for (i = 0; i < n; i++) for (j = 0; j < vr; j++) V->dv[j * n + i] = tmp[i * vr + j];
            free(tmp); free(valid);
        } else memcpy(V->dv, vt, sizeof(double) * (size_t)(r * n));
        tv[0] = ndv(U); tv[1] = ndv(S); tv[2] = ndv(V);
    }
    free(u); free(s); free(vt);
    return mktuple_from(tv, 3);
}
static double la_eps(void)
{
    return DBL_EPSILON;
}
BI(la_matrix_rank)
{
    long m, n, r, i, rank = 0;
    Nd *a;
    double *u, *s, *vt, tol, smax = 0.0;
    Value tv = np_arg(x, 1, "tol");
    need(x, 1, 3, "matrix_rank");
    a = np_f(A(0));
    if (a->nd < 2) {
        for (i = 0; i < a->size; i++) if (a->dv[i] != 0.0) return mkint(1);
        return mkint(0);
    }
    if (a->nd > 2) throw_new(cls_NotImplementedError, "linalg: stacked matrices are not supported");
    m = a->shape[0]; n = a->shape[1];
    r = m < n ? m : n;
    if (r == 0) return mkint(0);
    svd_thin(a, m, n, &u, &s, &vt);
    for (i = 0; i < r; i++) if (s[i] > smax) smax = s[i];
    tol = np_has(tv) ? to_dbl(tv) : smax * (double)(m > n ? m : n) * la_eps();
    for (i = 0; i < r; i++) if (s[i] > tol) rank++;
    free(u); free(s); free(vt);
    return mkint(rank);
}
/* least squares x = pinv(a) b with SVD cutoff; returns rank, fills singular values */
static void lstsq_core(Nd *a, long m, long n, const double *b, long k, double rcond,
                       double *xo, long *rankp, double *sv)
{
    long r = m < n ? m : n, i, j, c, rank = 0;
    double *u, *s, *vt, cut, smax = 0.0;
    svd_thin(a, m, n, &u, &s, &vt);
    for (i = 0; i < r; i++) if (s[i] > smax) smax = s[i];
    cut = rcond * smax;
    for (i = 0; i < n * k; i++) xo[i] = 0.0;
    for (i = 0; i < r; i++) {
        if (sv) sv[i] = s[i];
        if (s[i] > cut) {
            rank++;
            for (c = 0; c < k; c++) {
                double d = 0.0;
                for (j = 0; j < m; j++) d += u[j * r + i] * b[j * k + c];
                d /= s[i];
                for (j = 0; j < n; j++) xo[j * k + c] += d * vt[i * n + j];
            }
        }
    }
    *rankp = rank;
    free(u); free(s); free(vt);
}
BI(la_lstsq)
{
    long m, n, k, rank, i, j, c, r;
    Nd *a, *b, *xr, *res, *sv;
    double rc, *bb;
    Value rcv = np_arg(x, 2, "rcond"), tv[4];
    need(x, 2, 3, "lstsq");
    a = la_mat(A(0), &m, &n);
    b = np_f(A(1));
    if (b->nd < 1 || b->nd > 2 || b->shape[0] != m)
        throw_new(cls_LinAlgError, "Incompatible dimensions");
    k = b->nd == 1 ? 1 : b->shape[1];
    r = m < n ? m : n;
    rc = np_has(rcv) && to_dbl(rcv) >= 0 ? to_dbl(rcv) : la_eps() * (double)(m > n ? m : n);
    xr = b->nd == 1 ? vec_new(n) : mat_new(n, k);
    sv = vec_new(r);
    bb = b->dv;
    lstsq_core(a, m, n, bb, k, rc, xr->dv, &rank, sv->dv);
    if (rank == n && m > n) {
        res = vec_new(k);
        for (c = 0; c < k; c++) {
            double ss = 0.0;
            for (i = 0; i < m; i++) {
                double d = bb[i * k + c];
                for (j = 0; j < n; j++) d -= a->dv[i * n + j] * xr->dv[j * k + c];
                ss += d * d;
            }
            res->dv[c] = ss;
        }
    } else res = vec_new(0);
    tv[0] = ndv(xr); tv[1] = ndv(res); tv[2] = mkint(rank); tv[3] = ndv(sv);
    return mktuple_from(tv, 4);
}
BI(la_pinv)
{
    long m, n, i, j, k, r;
    Nd *a, *res;
    double *u, *s, *vt, cut, smax = 0.0, rc;
    Value rcv = np_arg(x, 1, "rcond");
    need(x, 1, 2, "pinv");
    a = la_mat(A(0), &m, &n);
    r = m < n ? m : n;
    res = mat_new(n, m);
    for (i = 0; i < n * m; i++) res->dv[i] = 0.0;
    if (r == 0) return ndv(res);
    svd_thin(a, m, n, &u, &s, &vt);
    for (i = 0; i < r; i++) if (s[i] > smax) smax = s[i];
    rc = np_has(rcv) ? to_dbl(rcv) : la_eps() * (double)(m > n ? m : n);
    cut = rc * smax;
    for (k = 0; k < r; k++)
        if (s[k] > cut)
            for (i = 0; i < n; i++)
                for (j = 0; j < m; j++) res->dv[i * m + j] += vt[k * n + i] * u[j * r + k] / s[k];
    free(u); free(s); free(vt);
    return ndv(res);
}
/* Householder QR (LAPACK sign conventions) */
BI(la_qr)
{
    long m, n, K, i, j, k, qc;
    Nd *a, *Q, *R;
    double *vs, *tau;
    const char *mode = "reduced";
    Value mv = np_arg(x, 1, "mode"), tv[2];
    need(x, 1, 2, "qr");
    if (mv.t == T_STR) mode = AS_STR(mv)->s;
    a = la_mat(A(0), &m, &n);
    K = m < n ? m : n;
    vs = (double *)xmalloc(sizeof(double) * (size_t)(m * (K > 0 ? K : 1) + 1));
    tau = (double *)xmalloc(sizeof(double) * (size_t)(K + 1));
    for (k = 0; k < K; k++) {
        double alpha = a->dv[k * n + k], xn = 0.0, beta;
        for (i = k + 1; i < m; i++) xn += a->dv[i * n + k] * a->dv[i * n + k];
        xn = sqrt(xn);
        for (i = 0; i < m; i++) vs[i * K + k] = 0.0;
        if (xn == 0.0) { tau[k] = 0.0; vs[k * K + k] = 1.0; continue; }
        beta = -(alpha >= 0 ? 1.0 : -1.0) * n_hypot(alpha, xn);
        tau[k] = (beta - alpha) / beta;
        vs[k * K + k] = 1.0;
        for (i = k + 1; i < m; i++) vs[i * K + k] = a->dv[i * n + k] / (alpha - beta);
        a->dv[k * n + k] = beta;
        for (i = k + 1; i < m; i++) a->dv[i * n + k] = 0.0;
        for (j = k + 1; j < n; j++) {
            double d = 0.0;
            for (i = k; i < m; i++) d += vs[i * K + k] * a->dv[i * n + j];
            d *= tau[k];
            for (i = k; i < m; i++) a->dv[i * n + j] -= d * vs[i * K + k];
        }
    }
    if (strcmp(mode, "r") == 0) {
        R = mat_new(K, n);
        for (i = 0; i < K; i++) for (j = 0; j < n; j++) R->dv[i * n + j] = j >= i ? a->dv[i * n + j] : 0.0;
        free(vs); free(tau);
        return ndv(R);
    }
    qc = strcmp(mode, "complete") == 0 ? m : K;
    Q = mat_new(m, m);
    for (i = 0; i < m * m; i++) Q->dv[i] = 0.0;
    for (i = 0; i < m; i++) Q->dv[i * m + i] = 1.0;
    for (k = K - 1; k >= 0; k--) {
        if (tau[k] == 0.0) continue;
        for (j = 0; j < m; j++) {
            double d = 0.0;
            for (i = k; i < m; i++) d += vs[i * K + k] * Q->dv[i * m + j];
            d *= tau[k];
            for (i = k; i < m; i++) Q->dv[i * m + j] -= d * vs[i * K + k];
        }
    }
    {
        Nd *Qo = mat_new(m, qc);
        long rr = qc;
        for (i = 0; i < m; i++) for (j = 0; j < qc; j++) Qo->dv[i * qc + j] = Q->dv[i * m + j];
        R = mat_new(rr, n);
        for (i = 0; i < rr; i++) for (j = 0; j < n; j++) R->dv[i * n + j] = (j >= i && i < m) ? a->dv[i * n + j] : 0.0;
        tv[0] = ndv(Qo); tv[1] = ndv(R);
    }
    free(vs); free(tau);
    return mktuple_from(tv, 2);
}
/* ---- norms ------------------------------------------------------------- */
static double vec_norm(const double *p, long n, long stride, double ord, int ordnone)
{
    long i;
    double s = 0.0;
    if (ordnone || ord == 2.0) {
        double scale = 0.0;
        for (i = 0; i < n; i++) if (fabs(p[i * stride]) > scale) scale = fabs(p[i * stride]);
        if (scale == 0.0 || scale != scale || scale - scale != 0.0) {
            for (i = 0; i < n; i++) s += p[i * stride] * p[i * stride];
            return sqrt(s);
        }
        for (i = 0; i < n; i++) s += p[i * stride] * p[i * stride];
        return sqrt(s);
    }
    if (ord == HUGE_VAL) {
        for (i = 0; i < n; i++) if (fabs(p[i * stride]) > s || p[i * stride] != p[i * stride]) s = fabs(p[i * stride]);
        return s;
    }
    if (ord == -HUGE_VAL) {
        s = HUGE_VAL;
        for (i = 0; i < n; i++) if (fabs(p[i * stride]) < s) s = fabs(p[i * stride]);
        return s;
    }
    if (ord == 0.0) {
        for (i = 0; i < n; i++) if (p[i * stride] != 0.0) s += 1.0;
        return s;
    }
    if (ord == 1.0) {
        for (i = 0; i < n; i++) s += fabs(p[i * stride]);
        return s;
    }
    for (i = 0; i < n; i++) s += pow(fabs(p[i * stride]), ord);
    return pow(s, 1.0 / ord);
}
BI(la_norm)
{
    Nd *a;
    Value ov = np_arg(x, 1, "ord"), av = np_arg(x, 2, "axis"), kv = np_arg(x, 3, "keepdims");
    double ord = 2.0;
    int ordnone = !np_has(ov), fro = 0, nuc = 0, keep = 0;
    need(x, 1, 4, "norm");
    a = np_f(A(0));
    if (np_has(kv)) keep = py_truthy(kv);
    if (np_has(ov)) {
        if (ov.t == T_STR) {
            if (strcmp(AS_STR(ov)->s, "fro") == 0) fro = 1;
            else if (strcmp(AS_STR(ov)->s, "nuc") == 0) nuc = 1;
            else throw_new(cls_ValueError, "Invalid norm order for matrices.");
        } else ord = to_dbl(ov);
    }
    if (np_has(av)) {
        long ax;
        if (av.t == T_TUPLE) throw_new(cls_NotImplementedError, "norm: matrix norms over an axis pair are not supported");
        ax = axis_norm(need_long(av, "axis"), a->nd);
        {
            long outer = 1, len = a->shape[ax], inner = 1, o, i, sh[ND_MAXD];
            int k, rn = 0;
            Nd *r;
            for (k = 0; k < ax; k++) outer *= a->shape[k];
            for (k = ax + 1; k < a->nd; k++) inner *= a->shape[k];
            for (k = 0; k < a->nd; k++) {
                if (k == ax) { if (keep) sh[rn++] = 1; }
                else sh[rn++] = a->shape[k];
            }
            r = nd_new(ND_FLOAT, rn, sh);
            for (o = 0; o < outer; o++)
                for (i = 0; i < inner; i++)
                    r->dv[o * inner + i] = vec_norm(a->dv + o * len * inner + i, len, inner, ord, ordnone || fro);
            return nd_ret(r);
        }
    }
    if (a->nd <= 1 || (ordnone && !nuc) || fro) {
        double v;
        if (a->nd > 2 && !ordnone && !fro) throw_new(cls_ValueError, "Improper number of dimensions to norm.");
        v = vec_norm(a->dv, a->size, 1, ord, ordnone || fro);
        if (keep && a->nd > 0) {
            Nd *r;
            long sh[ND_MAXD];
            int k;
            for (k = 0; k < a->nd; k++) sh[k] = 1;
            r = nd_new(ND_FLOAT, a->nd, sh);
            r->dv[0] = v;
            return ndv(r);
        }
        return mkfloat(v);
    }
    if (a->nd != 2) throw_new(cls_ValueError, "Improper number of dimensions to norm.");
    {
        long m = a->shape[0], n = a->shape[1], i, j;
        double v = 0.0;
        if (nuc || ord == 2.0 || ord == -2.0) {
            double *u, *s, *vt;
            long r = m < n ? m : n;
            if (r == 0) return mkfloat(0.0);
            svd_thin(a, m, n, &u, &s, &vt);
            if (nuc) for (i = 0; i < r; i++) v += s[i];
            else v = ord > 0 ? s[0] : s[r - 1];
            free(u); free(s); free(vt);
        } else if (ord == 1.0 || ord == -1.0) {
            v = ord > 0 ? 0.0 : HUGE_VAL;
            for (j = 0; j < n; j++) {
                double cs = 0.0;
                for (i = 0; i < m; i++) cs += fabs(a->dv[i * n + j]);
                if (ord > 0 ? cs > v : cs < v) v = cs;
            }
        } else if (ord == HUGE_VAL || ord == -HUGE_VAL) {
            v = ord > 0 ? 0.0 : HUGE_VAL;
            for (i = 0; i < m; i++) {
                double rs = 0.0;
                for (j = 0; j < n; j++) rs += fabs(a->dv[i * n + j]);
                if (ord > 0 ? rs > v : rs < v) v = rs;
            }
        } else throw_new(cls_ValueError, "Invalid norm order for matrices.");
        if (keep) {
            Nd *r = mat_new(1, 1);
            r->dv[0] = v;
            return ndv(r);
        }
        return mkfloat(v);
    }
}
BI(la_cond)
{
    long m, n, r, i;
    Nd *a;
    double *u, *s, *vt, v;
    need(x, 1, 2, "cond");
    if (x->n > 1 && np_has(A(1))) {
        Args y;
        Value av[2], c1, c2;
        Value inv;
        y.n = 1; y.a = av; y.nk = 0; y.kw = NULL;
        av[0] = A(0);
        inv = la_inv(&y);
        y.n = 2; av[0] = A(0); av[1] = A(1);
        c1 = la_norm(&y);
        av[0] = inv;
        c2 = la_norm(&y);
        return mkfloat(to_dbl(c1) * to_dbl(c2));
    }
    a = la_mat(A(0), &m, &n);
    r = m < n ? m : n;
    svd_thin(a, m, n, &u, &s, &vt);
    v = s[r - 1] == 0.0 ? HUGE_VAL : s[0] / s[r - 1];
    (void)i;
    free(u); free(s); free(vt);
    return mkfloat(v);
}
BI(la_matrix_power)
{
    long n, p, i;
    Nd *a, *r, *base;
    Value pv, res;
    need(x, 2, 2, "matrix_power");
    a = la_sq(A(0), &n);
    p = need_long(A(1), "n");
    if (p < 0) {
        Args y;
        Value av[1];
        y.n = 1; y.a = av; y.nk = 0; y.kw = NULL;
        av[0] = A(0);
        pv = la_inv(&y);
        a = AS_ND(pv);
        p = -p;
    }
    r = mat_new(n, n);
    for (i = 0; i < n * n; i++) r->dv[i] = 0.0;
    for (i = 0; i < n; i++) r->dv[i * n + i] = 1.0;
    base = a;
    while (p > 0) {
        if (p & 1) { res = nd_matmul(r, base, 0); r = AS_ND(res); }
        p >>= 1;
        if (p) { res = nd_matmul(base, base, 0); base = AS_ND(res); }
    }
    return ndv(r);
}
static void la_init(Module *np)
{
    Module *m = new_module("numpy.linalg");
    {
        Class **b = (Class **)xmalloc(sizeof(Class *));
        b[0] = cls_ValueError;
        cls_LinAlgError = make_class(intern_c("LinAlgError"), b, 1, intern_c("numpy.linalg"));
    }
    mod_def(m, "det", la_det); mod_def(m, "slogdet", la_slogdet); mod_def(m, "inv", la_inv);
    mod_def(m, "solve", la_solve); mod_def(m, "cholesky", la_cholesky);
    mod_def(m, "eigvalsh", la_eigvalsh); mod_def(m, "eigh", la_eigh);
    mod_def(m, "eigvals", la_eigvals); mod_def(m, "eig", la_eig);
    mod_def(m, "svd", la_svd); mod_def(m, "matrix_rank", la_matrix_rank);
    mod_def(m, "lstsq", la_lstsq); mod_def(m, "pinv", la_pinv); mod_def(m, "qr", la_qr);
    mod_def(m, "norm", la_norm); mod_def(m, "cond", la_cond);
    mod_def(m, "matrix_power", la_matrix_power);
    mod_val(m, "LinAlgError", mkobj(cls_LinAlgError, T_CLASS));
    mod_val(np, "linalg", mkobj(m, T_MODULE));
    mod_val(np, "LinAlgError", mkobj(cls_LinAlgError, T_CLASS));
}
/* ===== p7_np2.c ===== */
/* =========================================================================
   p7_np2.c   more numpy: elementwise helpers, shape functions, statistics,
              polynomials, histogram, ... (all on contiguous copies)
   ========================================================================= */
static double g_rtol, g_atol;
static int g_eqnan;
typedef double (*DF2)(double, double);

/* elementwise binary op with broadcasting; result dtype outdt */
static Value nd_map2(Nd *a, Nd *b, DF2 f, int outdt)
{
    long rs[ND_MAXD], sa[ND_MAXD], sb[ND_MAXD], idx[ND_MAXD], i, oa = 0, ob = 0;
    int rnd, k;
    Nd *r;
    rnd = nd_bshape(a, b, rs);
    nd_bstrides(a, rnd, sa);
    nd_bstrides(b, rnd, sb);
    r = nd_new(outdt, rnd, rs);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        double v = f(nd_d(a, oa), nd_d(b, ob));
        if (outdt == ND_FLOAT) r->dv[i] = v; else r->iv[i] = v != 0.0;
        nd_odo(idx, rnd, rs, sa, sb, &oa, &ob);
    }
    return nd_ret(r);
}
static double f_atan2(double y, double x) { return atan2(y, x); }
static double f_hypot(double a, double b) { return n_hypot(a, b); }
static double f_fmax(double a, double b) { return a != a ? b : (b != b ? a : (a > b ? a : b)); }
static double f_fmin(double a, double b) { return a != a ? b : (b != b ? a : (a < b ? a : b)); }
static double f_copysign(double a, double b) { return b < 0 || (b == 0 && 1.0 / b < 0) ? -fabs(a) : fabs(a); }
static double f_isclose(double a, double b)
{
    if (a != a || b != b) return g_eqnan && a != a && b != b;
    if (a == b) return 1.0;
    if (a - a != 0.0 || b - b != 0.0) return 0.0;
    return fabs(a - b) <= g_atol + g_rtol * fabs(b);
}
static double f_land(double a, double b) { return a != 0.0 && b != 0.0; }
static double f_lor(double a, double b) { return a != 0.0 || b != 0.0; }
static double f_lxor(double a, double b) { return (a != 0.0) != (b != 0.0); }
#define NDM2(fname, f, dt) \
    BI(fname) { \
        NdTmp ta_, tb_; \
        if (x->n != 2) throw_new(cls_TypeError, "function takes exactly 2 arguments"); \
        return nd_map2(nd_coerce(A(0), &ta_), nd_coerce(A(1), &tb_), f, dt); }
NDM2(np_arctan2, f_atan2, ND_FLOAT)
NDM2(np_hypot, f_hypot, ND_FLOAT)
NDM2(np_copysign, f_copysign, ND_FLOAT)
NDM2(np_logical_and, f_land, ND_BOOL)
NDM2(np_logical_or, f_lor, ND_BOOL)
NDM2(np_logical_xor, f_lxor, ND_BOOL)
BI(np_fmax)
{
    NdTmp ta, tb;
    Nd *a, *b;
    need(x, 2, 2, "fmax");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->dt != ND_FLOAT && b->dt != ND_FLOAT) return nd_binary(OP_MAX, a, b);
    return nd_map2(a, b, f_fmax, ND_FLOAT);
}
BI(np_fmin)
{
    NdTmp ta, tb;
    Nd *a, *b;
    need(x, 2, 2, "fmin");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->dt != ND_FLOAT && b->dt != ND_FLOAT) return nd_binary(OP_MIN, a, b);
    return nd_map2(a, b, f_fmin, ND_FLOAT);
}
BI(np_isclose)
{
    NdTmp ta, tb;
    Value rt = np_arg(x, 2, "rtol"), at = np_arg(x, 3, "atol"), en = np_arg(x, 4, "equal_nan");
    need(x, 2, 5, "isclose");
    g_rtol = np_has(rt) ? to_dbl(rt) : 1e-5;
    g_atol = np_has(at) ? to_dbl(at) : 1e-8;
    g_eqnan = np_has(en) && py_truthy(en);
    return nd_map2(nd_coerce(A(0), &ta), nd_coerce(A(1), &tb), f_isclose, ND_BOOL);
}
BI(np_logical_not)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long i;
    r = nd_new(ND_BOOL, a->nd, a->shape);
    for (i = 0; i < a->size; i++) r->iv[i] = a->dt == ND_FLOAT ? a->dv[i] == 0.0 : a->iv[i] == 0;
    return nd_ret(r);
}

/* more unary functions via a double->double table */
typedef double (*DF1)(double);
static double d_exp2(double v) { return pow(2.0, v); }
static double nd_em1(double v) { return n_expm1(v); }
static double nd_l1p(double v) { return n_log1p(v); }
static double d_cbrt(double v) { return n_cbrt(v); }
static double d_asinh(double v) { return n_asinh(v); }
static double d_acosh(double v) { return n_acosh(v); }
static double d_atanh(double v) { return n_atanh(v); }
static double d_deg2rad(double v) { return v * (3.14159265358979323846 / 180.0); }
static double d_rad2deg(double v) { return v * (180.0 / 3.14159265358979323846); }
static double d_recip(double v) { return 1.0 / v; }
static double d_trunc(double v) { return n_trunc(v); }
static double d_rint(double v) { return nd_rint(v); }
static double d_sinc(double v)
{
    double t = 3.14159265358979323846 * v;
    return v == 0.0 ? 1.0 : sin(t) / t;
}
static Value nd_map1(Nd *a, DF1 f)
{
    Nd *r = nd_new(ND_FLOAT, a->nd, a->shape);
    long i;
    for (i = 0; i < a->size; i++) r->dv[i] = f(nd_d(a, i));
    return nd_ret(r);
}
#define NDD1(fname, f) BI(fname) { NdTmp t_; Nd *a_ = nd_arg0(x, &t_); return nd_map1(a_, f); }
NDD1(np_exp2, d_exp2)
NDD1(np_expm1, nd_em1)
NDD1(np_log1p, nd_l1p)
NDD1(np_cbrt, d_cbrt)
NDD1(np_arcsinh, d_asinh)
NDD1(np_arccosh, d_acosh)
NDD1(np_arctanh, d_atanh)
NDD1(np_deg2rad, d_deg2rad)
NDD1(np_rad2deg, d_rad2deg)
NDD1(np_reciprocal, d_recip)
NDD1(np_trunc, d_trunc)
NDD1(np_rint, d_rint)
NDD1(np_sinc, d_sinc)

BI(np_nan_to_num)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value nv = np_arg(x, 1, "nan"), pv = np_arg(x, 2, "posinf"), mv = np_arg(x, 3, "neginf");
    double nanv = np_has(nv) ? to_dbl(nv) : 0.0;
    double pinf = np_has(pv) ? to_dbl(pv) : DBL_MAX, minf = np_has(mv) ? to_dbl(mv) : -DBL_MAX;
    long i;
    r = nd_astype(a, a->dt);
    if (r->dt == ND_FLOAT)
        for (i = 0; i < r->size; i++) {
            double d = r->dv[i];
            if (d != d) r->dv[i] = nanv;
            else if (d == HUGE_VAL) r->dv[i] = pinf;
            else if (d == -HUGE_VAL) r->dv[i] = minf;
        }
    return nd_ret(r);
}
/* nan-aware reductions: flat or along one axis */
static Nd *nan_fill(Nd *a, double fillv)
{
    Nd *r = nd_astype(a, ND_FLOAT);
    long i;
    for (i = 0; i < r->size; i++) if (r->dv[i] != r->dv[i]) r->dv[i] = fillv;
    return r;
}
static Nd *nan_count(Nd *a)
{
    Nd *r = nd_new(ND_INT, a->nd, a->shape);
    long i;
    for (i = 0; i < a->size; i++) r->iv[i] = a->dt == ND_FLOAT ? a->dv[i] == a->dv[i] : 1;
    return r;
}
BI(np_nansum)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (a->dt != ND_FLOAT) return nd_reduce(R_SUM, a, has, axis, 0);
    return nd_reduce(R_SUM, nan_fill(a, 0.0), has, axis, 0);
}
BI(np_nanprod)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (a->dt != ND_FLOAT) return nd_reduce(R_PROD, a, has, axis, 0);
    return nd_reduce(R_PROD, nan_fill(a, 1.0), has, axis, 0);
}
BI(np_nanmean)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *cnt;
    int axis = 0, has = get_axis(x, 1, a, &axis);
    Value s, c;
    NdTmp t1, t2;
    if (a->dt != ND_FLOAT) return nd_reduce(R_MEAN, a, has, axis, 0);
    s = nd_reduce(R_SUM, nan_fill(a, 0.0), has, axis, 0);
    cnt = nan_count(a);
    c = nd_reduce(R_SUM, cnt, has, axis, 0);
    return nd_binary(B_DIV, nd_coerce(s, &t1), nd_coerce(c, &t2));
}
static Value nan_var(Args *x, int issd)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *z, *cnt, *dev;
    long ddof = ddof_arg(x), i, n = 0;
    double mean = 0.0, ss = 0.0, v;
    int axis = 0;
    if (get_axis(x, 1, a, &axis))
        throw_new(cls_NotImplementedError, "nanstd/nanvar: axis is not supported");
    z = nan_fill(a, 0.0);
    cnt = nan_count(a);
    for (i = 0; i < z->size; i++) { mean += nd_d(z, i); n += cnt->iv[i]; }
    if (n == 0) return mkfloat(HUGE_VAL - HUGE_VAL);
    mean /= (double)n;
    dev = nd_astype(a, ND_FLOAT);
    for (i = 0; i < dev->size; i++) if (cnt->iv[i]) { double d = dev->dv[i] - mean; ss += d * d; }
    v = ss / (double)(n - ddof > 0 ? n - ddof : 0);
    return mkfloat(issd ? sqrt(v) : v);
}
BI(np_nanvar) { return nan_var(x, 0); }
BI(np_nanstd) { return nan_var(x, 1); }
BI(np_nanmax)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (a->dt != ND_FLOAT) return nd_reduce(R_MAX, a, has, axis, 0);
    return nd_reduce(R_MAX, nan_fill(a, -HUGE_VAL), has, axis, 0);
}
BI(np_nanmin)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (a->dt != ND_FLOAT) return nd_reduce(R_MIN, a, has, axis, 0);
    return nd_reduce(R_MIN, nan_fill(a, HUGE_VAL), has, axis, 0);
}
BI(np_nanargmax)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    return nd_reduce(R_ARGMAX, a->dt == ND_FLOAT ? nan_fill(a, -HUGE_VAL) : a, has, axis, 0);
}
BI(np_nanargmin)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    return nd_reduce(R_ARGMIN, a->dt == ND_FLOAT ? nan_fill(a, HUGE_VAL) : a, has, axis, 0);
}
BI(np_ptp)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    int axis = 0, has = get_axis(x, 1, a, &axis);
    Value mx = nd_reduce(R_MAX, a, has, axis, 0), mn = nd_reduce(R_MIN, a, has, axis, 0);
    NdTmp t1, t2;
    return nd_binary(B_SUB, nd_coerce(mx, &t1), nd_coerce(mn, &t2));
}
BI(np_average)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    Value wv = np_arg(x, 2, "weights");
    int axis = 0, has = get_axis(x, 1, a, &axis);
    if (!np_has(wv)) return nd_reduce(R_MEAN, a, has, axis, 0);
    {
        NdTmp tw, t1, t2, t3;
        Nd *w = nd_coerce(wv, &tw);
        Value prod = nd_binary(B_MUL, a, w), num, den;
        if (w->nd != a->nd && has) throw_new(cls_NotImplementedError, "average: weights must match the array shape");
        num = nd_reduce(R_SUM, nd_coerce(prod, &t1), has, axis, 0);
        if (w->nd == a->nd || !has) den = nd_reduce(R_SUM, w, has, axis, 0);
        else den = nd_reduce(R_SUM, w, 0, 0, 0);
        return nd_binary(B_DIV, nd_coerce(num, &t2), nd_coerce(den, &t3));
    }
}

/* ---- axis helpers -------------------------------------------------------- */
static void axis_split(const Nd *a, int ax, long *outer, long *len, long *inner)
{
    int k;
    *outer = 1; *inner = 1; *len = a->shape[ax];
    for (k = 0; k < ax; k++) *outer *= a->shape[k];
    for (k = ax + 1; k < a->nd; k++) *inner *= a->shape[k];
}
BI(np_diff)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *cur;
    Value nv = np_arg(x, 1, "n"), av = np_arg(x, 2, "axis");
    long n = np_has(nv) ? need_long(nv, "n") : 1, step;
    int ax;
    if (a->nd == 0) throw_new(cls_ValueError, "diff requires input that is at least one dimensional");
    ax = np_has(av) ? axis_norm(need_long(av, "axis"), a->nd) : a->nd - 1;
    if (n < 0) throw_new(cls_ValueError, "order must be non-negative but got %ld", n);
    cur = nd_astype(a, a->dt);
    for (step = 0; step < n; step++) {
        long outer, len, inner, o, j, i, sh[ND_MAXD];
        Nd *r;
        int k;
        axis_split(cur, ax, &outer, &len, &inner);
        if (len == 0) break;
        for (k = 0; k < cur->nd; k++) sh[k] = cur->shape[k];
        sh[ax] = len - 1;
        r = nd_new(cur->dt, cur->nd, sh);
        for (o = 0; o < outer; o++)
            for (j = 0; j + 1 < len; j++)
                for (i = 0; i < inner; i++) {
                    long p = (o * len + j) * inner + i, q = p + inner, d = (o * (len - 1) + j) * inner + i;
                    if (cur->dt == ND_FLOAT) r->dv[d] = cur->dv[q] - cur->dv[p];
                    else if (cur->dt == ND_BOOL) r->iv[d] = cur->iv[q] != cur->iv[p];
                    else r->iv[d] = cur->iv[q] - cur->iv[p];
                }
        cur = r;
    }
    return ndv(cur);
}
BI(np_ediff1d)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *f;
    long n = a->size;
    Args y;
    Value av[1];
    f = nd_reshaped(a, 1, &n);
    y.n = 1; y.a = av; y.nk = 0; y.kw = NULL;
    av[0] = ndv(f);
    return np_diff(&y);
}
BI(np_gradient)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *f;
    int naxes, ax, k, single = 0;
    Value axv = kwget(x, "axis"), tv[ND_MAXD];
    int axes[ND_MAXD];
    int nvar = x->n - 1;
    if (a->nd == 0) throw_new(cls_ValueError, "gradient: need at least 1-d input");
    f = nd_astype(a, ND_FLOAT);
    if (np_has(axv)) {
        if (axv.t == T_TUPLE || axv.t == T_LIST) {
            List *l = AS_LIST(axv);
            naxes = (int)l->n;
            for (k = 0; k < naxes; k++) axes[k] = axis_norm(need_long(l->a[k], "axis"), a->nd);
        } else { naxes = 1; axes[0] = axis_norm(need_long(axv, "axis"), a->nd); single = 1; }
    } else {
        naxes = a->nd;
        for (k = 0; k < naxes; k++) axes[k] = k;
    }
    if (nvar != 0 && nvar != 1 && nvar != naxes)
        throw_new(cls_TypeError, "invalid number of arguments");
    for (k = 0; k < naxes; k++) {
        long outer, len, inner, o, j, i;
        Nd *r, *xs = NULL;
        double h = 1.0;
        Value sp = nvar == 0 ? UNSET_V : (nvar == 1 ? A(1) : A(1 + k));
        ax = axes[k];
        axis_split(f, ax, &outer, &len, &inner);
        if (len < 2) throw_new(cls_ValueError, "Shape of array too small to calculate a numerical gradient, at least (edge_order + 1) elements are required.");
        if (np_has(sp)) {
            if (sp.t == T_INT || sp.t == T_FLOAT || sp.t == T_BOOL) h = to_dbl(sp);
            else {
                xs = np_f(sp);
                if (xs->size != len)
                    throw_new(cls_ValueError, "when 1d, distances must match the length of the corresponding dimension");
            }
        }
        r = nd_new(ND_FLOAT, f->nd, f->shape);
        for (o = 0; o < outer; o++)
            for (i = 0; i < inner; i++) {
                double *fp = f->dv + o * len * inner + i, *rp = r->dv + o * len * inner + i;
                rp[0] = (fp[inner] - fp[0]) / (xs ? xs->dv[1] - xs->dv[0] : h);
                rp[(len - 1) * inner] = (fp[(len - 1) * inner] - fp[(len - 2) * inner]) /
                                        (xs ? xs->dv[len - 1] - xs->dv[len - 2] : h);
                for (j = 1; j < len - 1; j++) {
                    if (xs) {
                        double hd = xs->dv[j] - xs->dv[j - 1], hs = xs->dv[j + 1] - xs->dv[j];
                        double fm = fp[(j - 1) * inner], f0 = fp[j * inner], fpl = fp[(j + 1) * inner];
                        rp[j * inner] = (hd * hd * fpl - hs * hs * fm + (hs * hs - hd * hd) * f0) /
                                        (hs * hd * (hd + hs));
                    } else
                        rp[j * inner] = (fp[(j + 1) * inner] - fp[(j - 1) * inner]) / (2.0 * h);
                }
            }
        tv[k] = ndv(r);
    }
    if (single || naxes == 1) return tv[0];
    return mktuple_from(tv, naxes);
}

/* ---- sorting related ------------------------------------------------------ */
BI(np_searchsorted)
{
    NdTmp t, tv_;
    Nd *a = nd_arg0(x, &t), *v, *r;
    Value sd = np_arg(x, 2, "side");
    int right = 0;
    long i;
    need(x, 2, 4, "searchsorted");
    if (sd.t == T_STR && AS_STR(sd)->s[0] == 'r') right = 1;
    if (a->nd != 1) throw_new(cls_ValueError, "object too deep for desired array");
    v = nd_coerce(A(1), &tv_);
    r = nd_new(ND_INT, v->nd, v->shape);
    for (i = 0; i < v->size; i++) {
        double key = nd_d(v, i);
        long lo = 0, hi = a->size;
        while (lo < hi) {
            long mid = (lo + hi) / 2;
            double e = nd_d(a, mid);
            int go = right ? (e <= key) : (e < key);
            if (e != e) go = 0;
            if (key != key) go = e == e;
            if (go) lo = mid + 1; else hi = mid;
        }
        r->iv[i] = lo;
    }
    return nd_ret(r);
}
/* percentile / quantile / median along an axis (linear interpolation) */
static double lerp_q(double a, double b, double t)
{
    double d = b - a, r = a + d * t;
    if (t >= 0.5) r = b - d * (1.0 - t);
    return r;
}
static int g_median_mode;
static double quant_sorted(const double *s, long n, double q)
{
    double vi, t;
    long lo, hi;
    if (n == 0) return HUGE_VAL - HUGE_VAL;
    if (g_median_mode) {
        if (s[n - 1] != s[n - 1]) return s[n - 1];
        return n % 2 ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2.0;
    }
    vi = q * (double)(n - 1);
    lo = (long)floor(vi); hi = lo + 1;
    if (lo < 0) lo = 0;
    if (hi > n - 1) hi = n - 1;
    t = vi - floor(vi);
    if (s[hi] != s[hi]) return s[hi];
    return lerp_q(s[lo], s[hi], t);
}
static Value quantile_impl(Args *x, double scale, const char *fname)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *qs, *r, *f;
    Value av = np_arg(x, 2, "axis"), mv = kwget(x, "method");
    long nq, qi, outer, len, inner, o, i, j;
    int ax = -1, scalar_q;
    double *buf;
    need(x, 2, 6, fname);
    if (mv.t == T_STR && strcmp(AS_STR(mv)->s, "linear") != 0)
        throw_new(cls_NotImplementedError, "%s: only method='linear' is supported", fname);
    {
        NdTmp tq;
        qs = nd_astype(nd_coerce(A(1), &tq), ND_FLOAT);
        scalar_q = qs->nd == 0;
        nq = qs->size;
        for (qi = 0; qi < nq; qi++) {
            qs->dv[qi] /= scale;
            if (!(qs->dv[qi] >= 0.0 && qs->dv[qi] <= 1.0))
                throw_new(cls_ValueError, scale == 1.0 ? "Quantiles must be in the range [0, 1]" : "Percentiles must be in the range [0, 100]");
        }
    }
    f = nd_astype(a, ND_FLOAT);
    if (np_has(av)) ax = axis_norm(need_long(av, "axis"), a->nd);
    if (ax < 0) {
        long n = f->size;
        buf = f->dv;
        qsort(buf, (size_t)n, sizeof(double), cmp_dbl);
        if (scalar_q) return mkfloat(quant_sorted(buf, n, qs->dv[0]));
        r = nd_new1(ND_FLOAT, nq);
        for (qi = 0; qi < nq; qi++) r->dv[qi] = quant_sorted(buf, n, qs->dv[qi]);
        return ndv(r);
    }
    axis_split(f, ax, &outer, &len, &inner);
    {
        long sh[ND_MAXD];
        int k, rn = 0;
        if (!scalar_q) sh[rn++] = nq;
        for (k = 0; k < a->nd; k++) if (k != ax) sh[rn++] = a->shape[k];
        r = nd_new(ND_FLOAT, rn, sh);
    }
    buf = (double *)xmalloc(sizeof(double) * (size_t)(len > 0 ? len : 1));
    for (o = 0; o < outer; o++)
        for (i = 0; i < inner; i++) {
            for (j = 0; j < len; j++) buf[j] = f->dv[(o * len + j) * inner + i];
            qsort(buf, (size_t)len, sizeof(double), cmp_dbl);
            for (qi = 0; qi < nq; qi++)
                r->dv[qi * outer * inner + o * inner + i] = quant_sorted(buf, len, qs->dv[qi]);
        }
    free(buf);
    return nd_ret(r);
}
BI(np_percentile) { return quantile_impl(x, 100.0, "percentile"); }
BI(np_quantile) { return quantile_impl(x, 1.0, "quantile"); }
BI(np_median2)
{
    Value av = np_arg(x, 1, "axis"), r;
    Args y;
    Value v[3];
    if (!np_has(av)) return np_median(x);
    y.n = 3; y.a = v; y.nk = 0; y.kw = NULL;
    v[0] = A(0); v[1] = mkfloat(0.5); v[2] = av;
    g_median_mode = 1;
    r = quantile_impl(&y, 1.0, "median");
    g_median_mode = 0;
    return r;
}
BI(np_histogram)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *f, *edges, *hist, *w = NULL;
    Value bv = np_arg(x, 1, "bins"), rv = np_arg(x, 2, "range"), dv = kwget(x, "density"), wv = kwget(x, "weights");
    long nb = 10, i, n;
    double lo, hi;
    int density = np_has(dv) && py_truthy(dv);
    Value tv[2];
    Nd *edges_out = NULL;
    f = nd_astype(a, ND_FLOAT);
    n = f->size;
    if (np_has(wv)) {
        w = np_f(wv);
        if (w->size != n) throw_new(cls_ValueError, "weights should have the same shape as a.");
    }
    if (np_has(bv) && (bv.t == T_LIST || bv.t == T_TUPLE || bv.t == T_ND)) {
        edges = np_f(bv);
        edges_out = nd_from_value(bv, -1);
        nb = edges->size - 1;
        if (nb < 1) throw_new(cls_ValueError, "`bins` must have at least 2 edges");
        for (i = 0; i < nb; i++)
            if (edges->dv[i + 1] < edges->dv[i]) throw_new(cls_ValueError, "`bins` must increase monotonically, when an array");
    } else {
        if (np_has(bv)) nb = need_long(bv, "bins");
        if (nb < 1) throw_new(cls_ValueError, "`bins` must be positive, when an integer");
        if (np_has(rv)) {
            Nd *rg = np_f(rv);
            if (rg->size != 2) throw_new(cls_ValueError, "range must be a pair");
            lo = rg->dv[0]; hi = rg->dv[1];
            if (lo > hi) throw_new(cls_ValueError, "max must be larger than min in range parameter.");
        } else if (n == 0) { lo = 0.0; hi = 1.0; }
        else {
            lo = HUGE_VAL; hi = -HUGE_VAL;
            for (i = 0; i < n; i++) { if (f->dv[i] < lo) lo = f->dv[i]; if (f->dv[i] > hi) hi = f->dv[i]; }
        }
        if (lo == hi) { lo -= 0.5; hi += 0.5; }
        edges = vec_new(nb + 1);
        {
            double step = (hi - lo) / (double)nb;
            for (i = 0; i <= nb; i++) edges->dv[i] = (double)i * step + lo;
            edges->dv[nb] = hi;
        }
    }
    hist = nd_new1(w || density ? ND_FLOAT : ND_INT, nb);
    for (i = 0; i < nb; i++) { if (hist->dt == ND_FLOAT) hist->dv[i] = 0.0; else hist->iv[i] = 0; }
    for (i = 0; i < n; i++) {
        double v = f->dv[i];
        long l = 0, h = nb;
        if (!(v >= edges->dv[0] && v <= edges->dv[nb])) continue;
        while (l < h) {
            long mid = (l + h) / 2;
            if (edges->dv[mid + 1] <= v) l = mid + 1; else h = mid;
        }
        if (l >= nb) l = nb - 1;
        if (hist->dt == ND_FLOAT) hist->dv[l] += w ? w->dv[i] : 1.0; else hist->iv[l]++;
    }
    if (density) {
        double tot = 0.0;
        for (i = 0; i < nb; i++) tot += hist->dv[i];
        for (i = 0; i < nb; i++) hist->dv[i] = hist->dv[i] / tot / (edges->dv[i + 1] - edges->dv[i]);
    }
    tv[0] = ndv(hist); tv[1] = ndv(edges_out ? edges_out : edges);
    return mktuple_from(tv, 2);
}
BI(np_bincount)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r, *w = NULL;
    Value wv = np_arg(x, 1, "weights"), mv = np_arg(x, 2, "minlength");
    long i, mx = -1, ml = np_has(mv) ? need_long(mv, "minlength") : 0;
    if (a->dt == ND_FLOAT) throw_new(cls_TypeError, "Cannot cast array data from dtype('float64') to dtype('int64') according to the rule 'safe'");
    if (a->nd != 1) throw_new(cls_ValueError, "object too deep for desired array");
    for (i = 0; i < a->size; i++) {
        if (a->iv[i] < 0) throw_new(cls_ValueError, "'list' argument must have no negative elements");
        if (a->iv[i] > mx) mx = a->iv[i];
    }
    if (mx + 1 < ml) mx = ml - 1;
    if (np_has(wv)) w = np_f(wv);
    r = nd_new1(w ? ND_FLOAT : ND_INT, mx + 1);
    for (i = 0; i <= mx; i++) { if (w) r->dv[i] = 0.0; else r->iv[i] = 0; }
    for (i = 0; i < a->size; i++) { if (w) r->dv[a->iv[i]] += w->dv[i]; else r->iv[a->iv[i]]++; }
    return ndv(r);
}
/* ---- covariance ---------------------------------------------------------- */
static Nd *cov_matrix(Args *x, int *scalar)
{
    NdTmp t;
    Nd *m = nd_astype(nd_arg0(x, &t), ND_FLOAT), *X, *c;
    Value yv = np_arg(x, 1, "y"), rv = np_arg(x, 2, "rowvar"), bv = np_arg(x, 3, "bias"), dv = np_arg(x, 4, "ddof");
    int rowvar = !np_has(rv) || py_truthy(rv);
    long ddof = np_has(bv) && py_truthy(bv) ? 0 : 1, nv, N, i, j, k;
    double fact;
    double *means;
    if (np_has(dv)) ddof = need_long(dv, "ddof");
    if (m->nd > 2) throw_new(cls_ValueError, "m has more than 2 dimensions");
    if (m->nd < 2) { long sh[2]; sh[0] = 1; sh[1] = m->size; m = nd_reshaped(m, 2, sh); rowvar = 1; }
    *scalar = 0;
    if (!rowvar && m->shape[0] != 1) {
        long sh[2];
        Nd *tr = mat_new(m->shape[1], m->shape[0]);
        for (i = 0; i < m->shape[0]; i++) for (j = 0; j < m->shape[1]; j++) tr->dv[j * m->shape[0] + i] = m->dv[i * m->shape[1] + j];
        m = tr; (void)sh;
    }
    if (np_has(yv)) {
        Nd *y = np_f(yv);
        long sh[2];
        if (y->nd < 2) { sh[0] = 1; sh[1] = y->size; y = nd_reshaped(y, 2, sh); }
        else if (!rowvar && y->shape[0] != 1) {
            Nd *tr = mat_new(y->shape[1], y->shape[0]);
            for (i = 0; i < y->shape[0]; i++) for (j = 0; j < y->shape[1]; j++) tr->dv[j * y->shape[0] + i] = y->dv[i * y->shape[1] + j];
            y = tr;
        }
        if (y->shape[1] != m->shape[1]) throw_new(cls_ValueError, "all the input array dimensions except for the concatenation axis must match exactly");
        X = mat_new(m->shape[0] + y->shape[0], m->shape[1]);
        memcpy(X->dv, m->dv, sizeof(double) * (size_t)m->size);
        memcpy(X->dv + m->size, y->dv, sizeof(double) * (size_t)y->size);
    } else X = m;
    nv = X->shape[0]; N = X->shape[1];
    fact = (double)(N - ddof);
    if (fact <= 0) fact = 0.0;
    means = (double *)xmalloc(sizeof(double) * (size_t)(nv + 1));
    for (i = 0; i < nv; i++) {
        means[i] = pw_sum(X->dv + i * N, N) / (double)N;
        for (j = 0; j < N; j++) X->dv[i * N + j] -= means[i];
    }
    free(means);
    c = mat_new(nv, nv);
    for (i = 0; i < nv; i++)
        for (j = 0; j < nv; j++) {
            double s = 0.0;
            for (k = 0; k < N; k++) s += X->dv[i * N + k] * X->dv[j * N + k];
            c->dv[i * nv + j] = s * (1.0 / fact);
        }
    if (nv == 1) *scalar = 1;
    return c;
}
BI(np_cov)
{
    int sc;
    Nd *c = cov_matrix(x, &sc);
    if (sc) return mkfloat(c->dv[0]);
    return ndv(c);
}
BI(np_corrcoef)
{
    int sc;
    Nd *c = cov_matrix(x, &sc);
    long n = c->shape[0], i, j;
    double *sd;
    if (sc) return mkfloat(c->dv[0] / c->dv[0]);
    sd = (double *)xmalloc(sizeof(double) * (size_t)n);
    for (i = 0; i < n; i++) sd[i] = sqrt(c->dv[i * n + i]);
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            double v = c->dv[i * n + j] / sd[i] / sd[j];
            if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
            c->dv[i * n + j] = v;
        }
    free(sd);
    return ndv(c);
}
/* ---- triangular, kron, outer, cross ------------------------------------- */
static Value tri_impl(Args *x, int upper)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value kv = np_arg(x, 1, "k");
    long k = np_has(kv) ? need_long(kv, "k") : 0, m, n, o, nmat, i, j;
    if (a->nd < 2) {
        /* numpy broadcasts a 1-d input against its own length */
        long sh[2];
        Nd *tmp;
        sh[0] = a->size; sh[1] = a->size;
        tmp = nd_new(a->dt, 2, sh);
        for (i = 0; i < a->size; i++) nd_cp(tmp, i * a->size, a, 0, a->size);
        a = tmp;
    }
    m = a->shape[a->nd - 2]; n = a->shape[a->nd - 1];
    nmat = m * n > 0 ? a->size / (m * n) : 0;
    r = nd_astype(a, a->dt);
    for (o = 0; o < nmat; o++)
        for (i = 0; i < m; i++)
            for (j = 0; j < n; j++) {
                int keep = upper ? (j - i >= k) : (j - i <= k);
                if (!keep) { if (r->dt == ND_FLOAT) r->dv[o * m * n + i * n + j] = 0.0; else r->iv[o * m * n + i * n + j] = 0; }
            }
    return ndv(r);
}
BI(np_tril) { return tri_impl(x, 0); }
BI(np_triu) { return tri_impl(x, 1); }
BI(np_kron)
{
    NdTmp ta, tb;
    Nd *a, *b, *ra, *rb, *r;
    int nd, k;
    long sh[ND_MAXD], sa[ND_MAXD], sb[ND_MAXD], i, j, idx[ND_MAXD], dt;
    need(x, 2, 2, "kron");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    nd = a->nd > b->nd ? a->nd : b->nd;
    if (nd == 0) return nd_binary(B_MUL, a, b);
    for (k = 0; k < nd; k++) {
        int ka = k - (nd - a->nd), kb = k - (nd - b->nd);
        sa[k] = ka >= 0 ? a->shape[ka] : 1;
        sb[k] = kb >= 0 ? b->shape[kb] : 1;
        sh[k] = sa[k] * sb[k];
    }
    dt = (a->dt == ND_FLOAT || b->dt == ND_FLOAT) ? ND_FLOAT : ND_INT;
    ra = nd_astype(a, (int)dt); rb = nd_astype(b, (int)dt);
    r = nd_new(dt, nd, sh);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        long oa = 0, ob = 0, ma = 1, mb = 1;
        for (k = nd - 1; k >= 0; k--) {
            oa += (idx[k] / sb[k]) * ma; ma *= sa[k];
            ob += (idx[k] % sb[k]) * mb; mb *= sb[k];
        }
        if (dt == ND_FLOAT) r->dv[i] = ra->dv[oa] * rb->dv[ob];
        else r->iv[i] = (long)((unsigned long)ra->iv[oa] * (unsigned long)rb->iv[ob]);
        for (k = nd - 1; k >= 0; k--) { if (++idx[k] < sh[k]) break; idx[k] = 0; }
    }
    (void)j;
    return ndv(r);
}
BI(np_outer)
{
    NdTmp ta, tb;
    Nd *a, *b, *ra, *rb;
    long sh[2];
    need(x, 2, 2, "outer");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    sh[0] = a->size; sh[1] = 1;
    ra = nd_reshaped(a, 2, sh);
    sh[0] = 1; sh[1] = b->size;
    rb = nd_reshaped(b, 2, sh);
    return nd_binary(B_MUL, ra, rb);
}
BI(np_inner)
{
    NdTmp ta, tb;
    Nd *a, *b;
    need(x, 2, 2, "inner");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->nd <= 1 && b->nd <= 1) return nd_matmul(a, b, 1);
    if (a->nd == 2 && b->nd == 1) return nd_matmul(a, b, 1);
    if (a->nd <= 1 && b->nd == 2) return nd_matmul(a, nd_transposed(b), 1);
    if (a->nd == 2 && b->nd == 2) return nd_matmul(a, nd_transposed(b), 1);
    throw_new(cls_NotImplementedError, "inner: only up to 2-d arrays are supported");
    return NONE_V;
}
BI(np_vdot)
{
    NdTmp ta, tb;
    Nd *a, *b;
    long n;
    need(x, 2, 2, "vdot");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->size != b->size) throw_new(cls_ValueError, "cannot reshape array of size %ld into shape (%ld)", b->size, a->size);
    n = a->size;
    return nd_matmul(nd_reshaped(a, 1, &n), nd_reshaped(b, 1, &n), 1);
}
BI(np_cross)
{
    NdTmp ta, tb;
    Nd *a, *b, *r;
    long na, nb, n, i;
    int dt;
    need(x, 2, 2, "cross");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (a->nd < 1 || b->nd < 1 || a->nd > 2 || b->nd > 2 ||
        a->shape[a->nd - 1] != 3 || b->shape[b->nd - 1] != 3)
        throw_new(cls_ValueError, "incompatible dimensions for cross product\n(dimension must be 3)");
    na = a->nd == 2 ? a->shape[0] : 1;
    nb = b->nd == 2 ? b->shape[0] : 1;
    if (na != nb && na != 1 && nb != 1)
        throw_new(cls_ValueError, "operands could not be broadcast together with shapes (%ld,3) (%ld,3) ", na, nb);
    n = na > nb ? na : nb;
    dt = (a->dt == ND_FLOAT || b->dt == ND_FLOAT) ? ND_FLOAT : ND_INT;
    if (a->nd == 1 && b->nd == 1) r = nd_new1(dt, 3);
    else { long sh[2]; sh[0] = n; sh[1] = 3; r = nd_new(dt, 2, sh); }
    for (i = 0; i < n; i++) {
        long pa = (na == 1 ? 0 : i) * 3, pb = (nb == 1 ? 0 : i) * 3;
        if (dt == ND_FLOAT) {
            double a0 = nd_d(a, pa), a1 = nd_d(a, pa + 1), a2 = nd_d(a, pa + 2);
            double b0 = nd_d(b, pb), b1 = nd_d(b, pb + 1), b2 = nd_d(b, pb + 2);
            r->dv[i * 3] = a1 * b2 - a2 * b1;
            r->dv[i * 3 + 1] = a2 * b0 - a0 * b2;
            r->dv[i * 3 + 2] = a0 * b1 - a1 * b0;
        } else {
            long a0 = a->iv[pa], a1 = a->iv[pa + 1], a2 = a->iv[pa + 2];
            long b0 = b->iv[pb], b1 = b->iv[pb + 1], b2 = b->iv[pb + 2];
            r->iv[i * 3] = a1 * b2 - a2 * b1;
            r->iv[i * 3 + 1] = a2 * b0 - a0 * b2;
            r->iv[i * 3 + 2] = a0 * b1 - a1 * b0;
        }
    }
    return ndv(r);
}
BI(np_meshgrid)
{
    int n = x->n, k, xy = 1;
    Value iv = kwget(x, "indexing");
    Nd *g[ND_MAXD];
    Value tv[ND_MAXD];
    long sh[ND_MAXD], idx[ND_MAXD], i;
    if (iv.t == T_STR) {
        if (strcmp(AS_STR(iv)->s, "ij") == 0) xy = 0;
        else if (strcmp(AS_STR(iv)->s, "xy") != 0) throw_new(cls_ValueError, "Valid values for `indexing` are 'xy' and 'ij'.");
    }
    if (n > ND_MAXD) throw_new(cls_ValueError, "too many arrays");
    for (k = 0; k < n; k++) {
        NdTmp t;
        Nd *a = nd_coerce(A(k), &t);
        long sz = a->size;
        g[k] = nd_reshaped(a, 1, &sz);
        sh[k] = sz;
    }
    if (xy && n >= 2) { long t = sh[0]; sh[0] = sh[1]; sh[1] = t; }
    for (k = 0; k < n; k++) {
        int axis = k;
        Nd *r = nd_new(g[k]->dt, n, sh);
        if (xy && n >= 2) { if (k == 0) axis = 1; else if (k == 1) axis = 0; }
        for (i = 0; i < n; i++) idx[i] = 0;
        for (i = 0; i < r->size; i++) {
            int d;
            nd_cp(r, i, g[k], idx[axis], 1);
            for (d = n - 1; d >= 0; d--) { if (++idx[d] < sh[d]) break; idx[d] = 0; }
        }
        tv[k] = ndv(r);
    }
    return mktuple_from(tv, n);
}
/* ---- polynomials, convolution ------------------------------------------- */
BI(np_polyfit)
{
    NdTmp tx, ty;
    Nd *xs, *ys, *V, *res, *w = NULL;
    Value dv = np_arg(x, 2, "deg"), wv = kwget(x, "w");
    long deg, m, cols, k, i, j, rank;
    double *scale, *b, *sol, rc;
    need(x, 3, 8, "polyfit");
    deg = need_long(dv, "deg");
    if (deg < 0) throw_new(cls_ValueError, "expected deg >= 0");
    xs = nd_astype(nd_coerce(A(0), &tx), ND_FLOAT);
    ys = nd_astype(nd_coerce(A(1), &ty), ND_FLOAT);
    if (xs->nd != 1) throw_new(cls_TypeError, "expected 1D vector for x");
    if (xs->size == 0) throw_new(cls_TypeError, "expected non-empty vector for x");
    if (ys->nd < 1 || ys->nd > 2) throw_new(cls_TypeError, "expected 1D or 2D array for y");
    if (ys->shape[0] != xs->size) throw_new(cls_TypeError, "expected x and y to have same length");
    if (np_has(wv)) {
        w = np_f(wv);
        if (w->size != xs->size) throw_new(cls_TypeError, "expected w and x to have same length");
    }
    m = xs->size; cols = deg + 1;
    k = ys->nd == 1 ? 1 : ys->shape[1];
    V = mat_new(m, cols);
    for (i = 0; i < m; i++) {
        double p = 1.0;
        for (j = cols - 1; j >= 0; j--) { V->dv[i * cols + j] = p; p *= xs->dv[i]; }
    }
    b = (double *)xmalloc(sizeof(double) * (size_t)(m * k + 1));
    memcpy(b, ys->dv, sizeof(double) * (size_t)(m * k));
    if (w) {
        for (i = 0; i < m; i++) {
            for (j = 0; j < cols; j++) V->dv[i * cols + j] *= w->dv[i];
            for (j = 0; j < k; j++) b[i * k + j] *= w->dv[i];
        }
    }
    scale = (double *)xmalloc(sizeof(double) * (size_t)cols);
    for (j = 0; j < cols; j++) {
        double s = 0.0;
        for (i = 0; i < m; i++) s += V->dv[i * cols + j] * V->dv[i * cols + j];
        scale[j] = sqrt(s);
        if (scale[j] == 0.0) scale[j] = 1.0;
        for (i = 0; i < m; i++) V->dv[i * cols + j] /= scale[j];
    }
    sol = (double *)xmalloc(sizeof(double) * (size_t)(cols * k + 1));
    rc = (double)m * DBL_EPSILON;
    lstsq_core(V, m, cols, b, k, rc, sol, &rank, NULL);
    for (j = 0; j < cols; j++) for (i = 0; i < k; i++) sol[j * k + i] /= scale[j];
    if (ys->nd == 1) { res = vec_new(cols); memcpy(res->dv, sol, sizeof(double) * (size_t)cols); }
    else { res = mat_new(cols, k); memcpy(res->dv, sol, sizeof(double) * (size_t)(cols * k)); }
    free(b); free(scale); free(sol);
    return ndv(res);
}
BI(np_polyval)
{
    NdTmp tp, tx;
    Nd *p, *xs, *r;
    long i, j;
    need(x, 2, 2, "polyval");
    p = nd_coerce(A(0), &tp); xs = nd_coerce(A(1), &tx);
    if (p->nd > 1) throw_new(cls_ValueError, "object too deep for desired array");
    if (p->dt == ND_FLOAT || xs->dt == ND_FLOAT) {
        r = nd_new(ND_FLOAT, xs->nd, xs->shape);
        for (i = 0; i < xs->size; i++) {
            double v = 0.0, xv = nd_d(xs, i);
            for (j = 0; j < p->size; j++) v = v * xv + nd_d(p, j);
            r->dv[i] = v;
        }
    } else {
        r = nd_new(ND_INT, xs->nd, xs->shape);
        for (i = 0; i < xs->size; i++) {
            long v = 0;
            for (j = 0; j < p->size; j++) v = v * xs->iv[i] + p->iv[j];
            r->iv[i] = v;
        }
    }
    return nd_ret(r);
}
BI(np_convolve)
{
    NdTmp ta, tv_;
    Nd *a, *v, *full, *r, *t;
    Value mv = np_arg(x, 2, "mode");
    const char *mode = mv.t == T_STR ? AS_STR(mv)->s : "full";
    long na, nv, nf, i, j, start = 0, len = 0;
    int fl;
    need(x, 2, 3, "convolve");
    a = nd_coerce(A(0), &ta); v = nd_coerce(A(1), &tv_);
    if (a->nd > 1 || v->nd > 1) throw_new(cls_ValueError, "object too deep for desired array");
    if (a->size == 0) throw_new(cls_ValueError, "a cannot be empty");
    if (v->size == 0) throw_new(cls_ValueError, "v cannot be empty");
    if (v->size > a->size) { t = a; a = v; v = t; }
    na = a->size; nv = v->size; nf = na + nv - 1;
    fl = a->dt == ND_FLOAT || v->dt == ND_FLOAT;
    full = nd_new1(fl ? ND_FLOAT : ND_INT, nf);
    for (i = 0; i < nf; i++) { if (fl) full->dv[i] = 0.0; else full->iv[i] = 0; }
    for (i = 0; i < na; i++)
        for (j = 0; j < nv; j++) {
            if (fl) full->dv[i + j] += nd_d(a, i) * nd_d(v, j);
            else full->iv[i + j] += a->iv[i] * v->iv[j];
        }
    if (strcmp(mode, "full") == 0 || mode[0] == 'f') return ndv(full);
    if (strcmp(mode, "same") == 0 || mode[0] == 's') { start = (nv - 1) / 2; len = na; }
    else if (strcmp(mode, "valid") == 0 || mode[0] == 'v') { start = nv - 1; len = na - nv + 1; }
    else throw_new(cls_ValueError, "mode must be one of 'full', 'same', or 'valid'");
    r = nd_new1(full->dt, len);
    nd_cp(r, 0, full, start, len);
    return ndv(r);
}
BI(np_trapz)
{
    NdTmp t;
    Nd *y = nd_astype(nd_arg0(x, &t), ND_FLOAT), *xs = NULL;
    Value xv = np_arg(x, 1, "x"), dxv = np_arg(x, 2, "dx");
    double dx = np_has(dxv) ? to_dbl(dxv) : 1.0, s = 0.0;
    long i;
    if (y->nd != 1) throw_new(cls_NotImplementedError, "trapz: only 1-d input is supported");
    if (np_has(xv)) xs = np_f(xv);
    for (i = 1; i < y->size; i++) {
        double h = xs ? xs->dv[i] - xs->dv[i - 1] : dx;
        s += h * (y->dv[i] + y->dv[i - 1]) / 2.0;
    }
    return mkfloat(s);
}
/* ---- shape manipulation --------------------------------------------------- */
BI(np_flip)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value av = np_arg(x, 1, "axis");
    long outer, len, inner, o, j, i;
    int ax;
    if (a->nd == 0) return nd_ret(nd_astype(a, a->dt));
    r = nd_astype(a, a->dt);
    if (!np_has(av)) {
        for (ax = 0; ax < a->nd; ax++) {
            Nd *s = nd_astype(r, r->dt);
            axis_split(s, ax, &outer, &len, &inner);
            for (o = 0; o < outer; o++) for (j = 0; j < len; j++)
                nd_cp(r, (o * len + j) * inner, s, (o * len + (len - 1 - j)) * inner, inner);
        }
        return ndv(r);
    }
    ax = axis_norm(need_long(av, "axis"), a->nd);
    axis_split(a, ax, &outer, &len, &inner);
    for (o = 0; o < outer; o++) for (j = 0; j < len; j++)
        for (i = 0; i < 1; i++) nd_cp(r, (o * len + j) * inner, a, (o * len + (len - 1 - j)) * inner, inner);
    return ndv(r);
}
BI(np_fliplr)
{
    Args y;
    Value v[2];
    y.n = 2; y.a = v; y.nk = 0; y.kw = NULL;
    v[0] = A(0); v[1] = mkint(1);
    return np_flip(&y);
}
BI(np_flipud)
{
    Args y;
    Value v[2];
    y.n = 2; y.a = v; y.nk = 0; y.kw = NULL;
    v[0] = A(0); v[1] = mkint(0);
    return np_flip(&y);
}
BI(np_roll)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value sv = np_arg(x, 1, "shift"), av = np_arg(x, 2, "axis");
    long shift, outer, len, inner, o, j;
    int ax;
    shift = need_long(sv, "shift");
    r = nd_astype(a, a->dt);
    if (a->size == 0) return ndv(r);
    if (!np_has(av)) {
        long n = a->size;
        shift = ((shift % n) + n) % n;
        for (j = 0; j < n; j++) nd_cp(r, (j + shift) % n, a, j, 1);
        return ndv(r);
    }
    ax = axis_norm(need_long(av, "axis"), a->nd);
    axis_split(a, ax, &outer, &len, &inner);
    shift = ((shift % len) + len) % len;
    for (o = 0; o < outer; o++) for (j = 0; j < len; j++)
        nd_cp(r, (o * len + (j + shift) % len) * inner, a, (o * len + j) * inner, inner);
    return ndv(r);
}
BI(np_tile)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long reps[ND_MAXD], sh[ND_MAXD], idx[ND_MAXD], i;
    int rn, nd, k;
    need(x, 2, 2, "tile");
    memset(sh, 0, sizeof(sh));
    rn = A(1).t == T_INT ? (reps[0] = A(1).u.i, 1) : shape_arg(A(1), reps);
    nd = a->nd > rn ? a->nd : rn;
    {
        long ash[ND_MAXD], rr[ND_MAXD];
        for (k = 0; k < nd; k++) {
            int ka = k - (nd - a->nd), kr = k - (nd - rn);
            ash[k] = ka >= 0 ? a->shape[ka] : 1;
            rr[k] = kr >= 0 ? reps[kr] : 1;
            sh[k] = ash[k] * rr[k];
        }
        r = nd_new(a->dt, nd, sh);
        for (k = 0; k < nd; k++) idx[k] = 0;
        for (i = 0; i < r->size; i++) {
            long off = 0, mul = 1;
            for (k = nd - 1; k >= 0; k--) { off += (idx[k] % ash[k]) * mul; mul *= ash[k]; }
            nd_cp(r, i, a, off, 1);
            for (k = nd - 1; k >= 0; k--) { if (++idx[k] < sh[k]) break; idx[k] = 0; }
        }
    }
    return ndv(r);
}
BI(np_repeat)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r, *src;
    Value rv = np_arg(x, 1, "repeats"), av = np_arg(x, 2, "axis");
    long outer, len, inner, o, j, i, c, total = 0, pos = 0, sh[ND_MAXD];
    Nd *reps = NULL;
    int ax, k;
    need(x, 2, 3, "repeat");
    if (rv.t == T_INT) c = rv.u.i;
    else { reps = nd_astype(nd_coerce(rv, &t), ND_INT); c = -1; }
    if (!np_has(av)) {
        long n = a->size;
        src = nd_reshaped(a, 1, &n);
        ax = 0;
    } else { src = a; ax = axis_norm(need_long(av, "axis"), a->nd); }
    axis_split(src, ax, &outer, &len, &inner);
    if (reps && reps->size != len && reps->size != 1)
        throw_new(cls_ValueError, "operands could not be broadcast together with shape (%ld,) (%ld,)", len, reps->size);
    for (j = 0; j < len; j++) total += c >= 0 ? c : reps->iv[reps->size == 1 ? 0 : j];
    for (k = 0; k < src->nd; k++) sh[k] = src->shape[k];
    sh[ax] = total;
    r = nd_new(src->dt, src->nd, sh);
    for (o = 0; o < outer; o++) {
        for (j = 0; j < len; j++) {
            long cnt = c >= 0 ? c : reps->iv[reps->size == 1 ? 0 : j];
            for (i = 0; i < cnt; i++) {
                nd_cp(r, (o * total + pos + i) * inner, src, (o * len + j) * inner, inner);
            }
            pos += cnt;
        }
        pos = 0;
    }
    return ndv(r);
}
BI(np_squeeze)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long sh[ND_MAXD];
    int k, rn = 0;
    Value av = np_arg(x, 1, "axis");
    for (k = 0; k < a->nd; k++) {
        if (a->shape[k] == 1 && (!np_has(av) || axis_norm(need_long(av, "axis"), a->nd) == k)) continue;
        if (np_has(av) && axis_norm(need_long(av, "axis"), a->nd) == k)
            throw_new(cls_ValueError, "cannot select an axis to squeeze out which has size not equal to one");
        sh[rn++] = a->shape[k];
    }
    return nd_ret(nd_reshaped(a, rn, sh));
}
BI(np_expand_dims)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t);
    long sh[ND_MAXD], ax;
    int k, j = 0;
    need(x, 2, 2, "expand_dims");
    ax = need_long(A(1), "axis");
    if (a->nd + 1 > ND_MAXD) throw_new(cls_ValueError, "maximum supported dimension for an ndarray is %d", ND_MAXD);
    if (ax < 0) ax += a->nd + 1;
    if (ax < 0 || ax > a->nd) throw_new(cls_ValueError, "axis %ld is out of bounds for array of dimension %d", ax, a->nd + 1);
    for (k = 0; k <= a->nd; k++) {
        if (k == ax) sh[k] = 1; else sh[k] = a->shape[j++];
    }
    return ndv(nd_reshaped(a, a->nd + 1, sh));
}
BI(np_swapaxes)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long i, idx[ND_MAXD], sh[ND_MAXD], st[ND_MAXD], mul = 1;
    int a1, a2, k;
    need(x, 3, 3, "swapaxes");
    a1 = axis_norm(need_long(A(1), "axis1"), a->nd);
    a2 = axis_norm(need_long(A(2), "axis2"), a->nd);
    for (k = a->nd - 1; k >= 0; k--) { st[k] = mul; mul *= a->shape[k]; }
    for (k = 0; k < a->nd; k++) sh[k] = a->shape[k];
    sh[a1] = a->shape[a2]; sh[a2] = a->shape[a1];
    { long t1 = st[a1]; st[a1] = st[a2]; st[a2] = t1; }
    r = nd_new(a->dt, a->nd, sh);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < r->size; i++) {
        long off = 0;
        for (k = 0; k < a->nd; k++) off += idx[k] * st[k];
        nd_cp(r, i, a, off, 1);
        for (k = a->nd - 1; k >= 0; k--) { if (++idx[k] < sh[k]) break; idx[k] = 0; }
    }
    return nd_ret(r);
}
BI(np_argwhere)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    long cnt = 0, i, pos = 0, idx[ND_MAXD], sh[2];
    int k;
    for (i = 0; i < a->size; i++) if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) cnt++;
    sh[0] = cnt; sh[1] = a->nd;
    r = nd_new(ND_INT, 2, sh);
    for (k = 0; k < ND_MAXD; k++) idx[k] = 0;
    for (i = 0; i < a->size; i++) {
        if (a->dt == ND_FLOAT ? a->dv[i] != 0 : a->iv[i] != 0) {
            for (k = 0; k < a->nd; k++) r->iv[pos * a->nd + k] = idx[k];
            pos++;
        }
        for (k = a->nd - 1; k >= 0; k--) { if (++idx[k] < a->shape[k]) break; idx[k] = 0; }
    }
    return ndv(r);
}
BI(np_isin)
{
    NdTmp ta, tb;
    Nd *a, *b, *r;
    long i, j;
    need(x, 2, 2, "isin");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    r = nd_new(ND_BOOL, a->nd, a->shape);
    for (i = 0; i < a->size; i++) {
        double v = nd_d(a, i);
        r->iv[i] = 0;
        for (j = 0; j < b->size; j++) if (nd_d(b, j) == v) { r->iv[i] = 1; break; }
    }
    return nd_ret(r);
}
BI(np_append)
{
    NdTmp ta, tb;
    Nd *a, *b, *arr[2];
    Value av = np_arg(x, 2, "axis");
    need(x, 2, 3, "append");
    a = nd_coerce(A(0), &ta); b = nd_coerce(A(1), &tb);
    if (!np_has(av)) {
        long na = a->size, nb = b->size;
        arr[0] = nd_reshaped(a, 1, &na); arr[1] = nd_reshaped(b, 1, &nb);
        return ndv(nd_concat(arr, 2, 0));
    }
    arr[0] = nd_astype(a, a->dt); arr[1] = nd_astype(b, b->dt);
    return ndv(nd_concat(arr, 2, axis_norm(need_long(av, "axis"), a->nd)));
}
BI(np_take)
{
    NdTmp ta, ti;
    Nd *a, *ix, *r, *src;
    Value av = np_arg(x, 2, "axis");
    long outer, len, inner, o, j, i, sh[ND_MAXD];
    int ax, k, rn = 0;
    need(x, 2, 3, "take");
    a = nd_coerce(A(0), &ta); ix = nd_coerce(A(1), &ti);
    if (ix->dt == ND_FLOAT) throw_new(cls_TypeError, "Cannot cast array data from dtype('float64') to dtype('int64') according to the rule 'same_kind'");
    if (!np_has(av)) { long n = a->size; src = nd_reshaped(a, 1, &n); ax = 0; }
    else { src = a; ax = axis_norm(need_long(av, "axis"), a->nd); }
    axis_split(src, ax, &outer, &len, &inner);
    for (k = 0; k < ax; k++) sh[rn++] = src->shape[k];
    for (k = 0; k < ix->nd; k++) sh[rn++] = ix->shape[k];
    for (k = ax + 1; k < src->nd; k++) sh[rn++] = src->shape[k];
    r = nd_new(src->dt, rn, sh);
    for (o = 0; o < outer; o++)
        for (j = 0; j < ix->size; j++) {
            long p = ix->iv[j];
            if (p < -len || p >= len)
                throw_new(cls_IndexError, "index %ld is out of bounds for axis %d with size %ld", p, ax, len);
            if (p < 0) p += len;
            for (i = 0; i < 1; i++) nd_cp(r, (o * ix->size + j) * inner, src, (o * len + p) * inner, inner);
        }
    return nd_ret(r);
}
BI(np_shape) { NdTmp t; Nd *a = nd_arg0(x, &t); Value tv[ND_MAXD]; int k; for (k = 0; k < a->nd; k++) tv[k] = mkint(a->shape[k]); return mktuple_from(tv, a->nd); }
BI(np_ndim) { NdTmp t; Nd *a = nd_arg0(x, &t); return mkint(a->nd); }
BI(np_size) { NdTmp t; Nd *a = nd_arg0(x, &t); return mkint(a->size); }
BI(np_isscalar)
{
    need(x, 1, 1, "isscalar");
    return tobool(A(0).t == T_INT || A(0).t == T_FLOAT || A(0).t == T_BOOL || A(0).t == T_BIG || A(0).t == T_STR);
}
BI(np_linspace2)
{
    Value rs = np_arg(x, 4, "retstep");
    Value r = np_linspace(x), tv[2];
    if (np_has(rs) && py_truthy(rs)) {
        Nd *a = AS_ND(r);
        Value nv = x->n > 2 ? A(2) : kwget(x, "num"), ev = x->n > 3 ? A(3) : kwget(x, "endpoint");
        long num = np_has(nv) ? need_long(nv, "num") : 50;
        int endpoint = !np_has(ev) || py_truthy(ev);
        long div = endpoint ? num - 1 : num;
        double step = div > 0 ? (to_dbl(A(1)) - to_dbl(A(0))) / (double)div : HUGE_VAL - HUGE_VAL;
        (void)a;
        tv[0] = r; tv[1] = mkfloat(step);
        return mktuple_from(tv, 2);
    }
    return r;
}
BI(np_tri)
{
    long n, m, k = 0, i, j, sh[2];
    Value mv = np_arg(x, 1, "M"), kv = np_arg(x, 2, "k");
    Nd *r;
    n = need_long(A(0), "N");
    m = np_has(mv) ? need_long(mv, "M") : n;
    if (np_has(kv)) k = need_long(kv, "k");
    sh[0] = n; sh[1] = m;
    r = nd_new(dtype_kw(x, ND_FLOAT), 2, sh);
    for (i = 0; i < n; i++) for (j = 0; j < m; j++) {
        int v = (j - i <= k);
        if (r->dt == ND_FLOAT) r->dv[i * m + j] = v; else r->iv[i * m + j] = v;
    }
    return ndv(r);
}
BI(np_vander)
{
    NdTmp t;
    Nd *a = nd_arg0(x, &t), *r;
    Value nv = np_arg(x, 1, "N"), iv = np_arg(x, 2, "increasing");
    long n = np_has(nv) ? need_long(nv, "N") : a->size, i, j, sh[2];
    int inc = np_has(iv) && py_truthy(iv);
    sh[0] = a->size; sh[1] = n;
    r = nd_new(a->dt == ND_FLOAT ? ND_FLOAT : ND_INT, 2, sh);
    for (i = 0; i < a->size; i++)
        for (j = 0; j < n; j++) {
            long e = inc ? j : n - 1 - j;
            if (r->dt == ND_FLOAT) r->dv[i * n + j] = pow(a->dv[i], (double)e);
            else { long p = 1, q; for (q = 0; q < e; q++) p *= a->iv[i]; r->iv[i * n + j] = p; }
        }
    return ndv(r);
}

static void np_init2(Module *m)
{
    mod_def(m, "arctan2", np_arctan2); mod_def(m, "hypot", np_hypot);
    mod_def(m, "copysign", np_copysign); mod_def(m, "fmax", np_fmax); mod_def(m, "fmin", np_fmin);
    mod_def(m, "isclose", np_isclose);
    mod_def(m, "logical_and", np_logical_and); mod_def(m, "logical_or", np_logical_or);
    mod_def(m, "logical_xor", np_logical_xor); mod_def(m, "logical_not", np_logical_not);
    mod_def(m, "exp2", np_exp2); mod_def(m, "expm1", np_expm1); mod_def(m, "log1p", np_log1p);
    mod_def(m, "cbrt", np_cbrt); mod_def(m, "arcsinh", np_arcsinh);
    mod_def(m, "arccosh", np_arccosh); mod_def(m, "arctanh", np_arctanh);
    mod_def(m, "deg2rad", np_deg2rad); mod_def(m, "radians", np_deg2rad);
    mod_def(m, "rad2deg", np_rad2deg); mod_def(m, "degrees", np_rad2deg);
    mod_def(m, "reciprocal", np_reciprocal); mod_def(m, "trunc", np_trunc);
    mod_def(m, "fix", np_trunc); mod_def(m, "rint", np_rint); mod_def(m, "sinc", np_sinc);
    mod_def(m, "nan_to_num", np_nan_to_num);
    mod_def(m, "nansum", np_nansum); mod_def(m, "nanprod", np_nanprod);
    mod_def(m, "nanmean", np_nanmean); mod_def(m, "nanvar", np_nanvar);
    mod_def(m, "nanstd", np_nanstd); mod_def(m, "nanmax", np_nanmax);
    mod_def(m, "nanmin", np_nanmin); mod_def(m, "nanargmax", np_nanargmax);
    mod_def(m, "nanargmin", np_nanargmin);
    mod_def(m, "ptp", np_ptp); mod_def(m, "average", np_average);
    mod_def(m, "diff", np_diff); mod_def(m, "ediff1d", np_ediff1d);
    mod_def(m, "gradient", np_gradient); mod_def(m, "searchsorted", np_searchsorted);
    mod_def(m, "percentile", np_percentile); mod_def(m, "quantile", np_quantile);
    mod_def(m, "median", np_median2); mod_def(m, "histogram", np_histogram);
    mod_def(m, "bincount", np_bincount); mod_def(m, "cov", np_cov);
    mod_def(m, "corrcoef", np_corrcoef); mod_def(m, "tril", np_tril);
    mod_def(m, "triu", np_triu); mod_def(m, "kron", np_kron); mod_def(m, "outer", np_outer);
    mod_def(m, "inner", np_inner); mod_def(m, "vdot", np_vdot); mod_def(m, "cross", np_cross);
    mod_def(m, "meshgrid", np_meshgrid); mod_def(m, "polyfit", np_polyfit);
    mod_def(m, "polyval", np_polyval); mod_def(m, "convolve", np_convolve);
    mod_def(m, "trapz", np_trapz); mod_def(m, "trapezoid", np_trapz);
    mod_def(m, "flip", np_flip); mod_def(m, "fliplr", np_fliplr); mod_def(m, "flipud", np_flipud);
    mod_def(m, "roll", np_roll); mod_def(m, "tile", np_tile); mod_def(m, "repeat", np_repeat);
    mod_def(m, "squeeze", np_squeeze); mod_def(m, "expand_dims", np_expand_dims);
    mod_def(m, "swapaxes", np_swapaxes); mod_def(m, "argwhere", np_argwhere);
    mod_def(m, "isin", np_isin); mod_def(m, "append", np_append); mod_def(m, "take", np_take);
    mod_def(m, "shape", np_shape); mod_def(m, "ndim", np_ndim); mod_def(m, "size", np_size);
    mod_def(m, "isscalar", np_isscalar); mod_def(m, "tri", np_tri); mod_def(m, "vander", np_vander);
    mod_def(m, "linspace", np_linspace2);
    mod_val(m, "float32", mkobj(cls_float, T_CLASS));
    mod_val(m, "int32", mkobj(cls_int, T_CLASS));
    mod_val(m, "int_", mkobj(cls_int, T_CLASS));
    mod_val(m, "float_", mkobj(cls_float, T_CLASS));
    mod_val(m, "bool", mkobj(cls_bool, T_CLASS));
    mod_val(m, "euler_gamma", mkfloat(0.57721566490153286061));
    mod_val(m, "Inf", mkfloat(HUGE_VAL));
    mod_val(m, "NaN", mkfloat(HUGE_VAL - HUGE_VAL));
    mod_val(m, "PINF", mkfloat(HUGE_VAL));
    mod_val(m, "NINF", mkfloat(-HUGE_VAL));
}
/* ===== p7_rnd.c ===== */
/* =========================================================================
   p7_rnd.c   numpy.random: xoshiro256** generator, legacy functions,
              default_rng / Generator / RandomState objects
   ========================================================================= */
typedef unsigned long u64;
static u64 g_rs[4];
static Class *cls_rng[2];

static u64 splitmix(u64 *z)
{
    u64 r;
    *z += 0x9E3779B97F4A7C15UL;
    r = *z;
    r = (r ^ (r >> 30)) * 0xBF58476D1CE4E5B9UL;
    r = (r ^ (r >> 27)) * 0x94D049BB133111EBUL;
    return r ^ (r >> 31);
}
static void nrng_seed(u64 *s, u64 seed)
{
    int i;
    for (i = 0; i < 4; i++) s[i] = splitmix(&seed);
}
static u64 rotl64(u64 v, int k)
{
    return (v << k) | (v >> (64 - k));
}
static u64 nrng_next(u64 *s)
{
    u64 r = rotl64(s[1] * 5UL, 7) * 9UL, t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl64(s[3], 45);
    return r;
}
static double nrng_dbl(u64 *s)
{
    return (double)(nrng_next(s) >> 11) * (1.0 / 9007199254740992.0);
}
static double nrng_norm(u64 *s)
{
    double u1, u2;
    do u1 = nrng_dbl(s); while (u1 <= 0.0);
    u2 = nrng_dbl(s);
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586476925 * u2);
}
/* uniform integer in [0, n) */
static u64 nrng_below(u64 *s, u64 n)
{
    u64 lim, r;
    if (n <= 1) return 0;
    lim = ULONG_MAX - (ULONG_MAX % n);
    do r = nrng_next(s); while (r >= lim);
    return r % n;
}
static u64 seed_value(Value v)
{
    if (v.t == T_INT || v.t == T_BOOL) return (u64)v.u.i;
    if (v.t == T_BIG) return (u64)py_hash(v);
    if (v.t == T_FLOAT) throw_new(cls_TypeError, "Cannot cast scalar from dtype('float64') to dtype('int64') according to the rule 'safe'");
    if (v.t == T_ND || v.t == T_LIST || v.t == T_TUPLE) {
        Nd *a = nd_from_value(v, ND_INT);
        u64 h = 0x1234567UL;
        long i;
        for (i = 0; i < a->size; i++) { u64 t = (u64)a->iv[i] + h; h = splitmix(&t); }
        return h;
    }
    throw_new(cls_TypeError, "Seed must be an integer");
    return 0;
}
static void seed_from(u64 *s, Value v)
{
    if (v.t == T_UNSET || v.t == T_NONE) {
        u64 t = (u64)time(NULL) * 1000003UL + (u64)clock() + (u64)(size_t)s;
        nrng_seed(s, t);
    } else nrng_seed(s, seed_value(v));
}

/* ---- argument helpers: off = 0 for module functions, 1 for methods ---- */
#define RN (x->n - off)
#define RA(i) (x->a[off + (i)])
static Value rarg(Args *x, int off, int pos, const char *nm)
{
    if (pos < RN) return RA(pos);
    return kwget(x, nm);
}
static double rdbl(Args *x, int off, int pos, const char *nm, double dflt)
{
    Value v = rarg(x, off, pos, nm);
    if (!np_has(v)) return dflt;
    return to_dbl(v);
}
/* size argument -> shape; returns -1 for a scalar result */
static int rsize(Value sz, long *shape)
{
    if (!np_has(sz)) return -1;
    return shape_arg(sz, shape);
}
typedef double (*RGen)(u64 *s, double p1, double p2);
static double g_unif(u64 *s, double a, double b) { return a + (b - a) * nrng_dbl(s); }
static double g_norm(u64 *s, double a, double b) { return a + b * nrng_norm(s); }
static double g_exp(u64 *s, double a, double b)
{
    double u;
    (void)b;
    do u = nrng_dbl(s); while (u <= 0.0);
    return -log(u) * a;
}
static Value rfill(u64 *s, Value sz, RGen g, double p1, double p2)
{
    long shape[ND_MAXD], i;
    int nd = rsize(sz, shape);
    Nd *r;
    if (nd < 0) return mkfloat(g(s, p1, p2));
    r = nd_new(ND_FLOAT, nd, shape);
    for (i = 0; i < r->size; i++) r->dv[i] = g(s, p1, p2);
    return nd_ret(r);
}
/* positional dims (rand / randn) */
static Value rdims(Args *x, int off, u64 *s, RGen g)
{
    long shape[ND_MAXD], i;
    int nd = RN, k;
    Nd *r;
    if (nd == 0) return mkfloat(g(s, 0.0, 1.0));
    if (nd > ND_MAXD) throw_new(cls_ValueError, "maximum supported dimension for an ndarray is %d", ND_MAXD);
    for (k = 0; k < nd; k++) {
        shape[k] = need_long(RA(k), "dimension");
        if (shape[k] < 0) throw_new(cls_ValueError, "negative dimensions are not allowed");
    }
    r = nd_new(ND_FLOAT, nd, shape);
    for (i = 0; i < r->size; i++) r->dv[i] = g(s, 0.0, 1.0);
    return ndv(r);
}
static Value nr_random(Args *x, int off, u64 *s)
{
    return rfill(s, rarg(x, off, 0, "size"), g_unif, 0.0, 1.0);
}
static Value nr_stdnorm(Args *x, int off, u64 *s)
{
    return rfill(s, rarg(x, off, 0, "size"), g_norm, 0.0, 1.0);
}
static Value nr_normal(Args *x, int off, u64 *s)
{
    double loc = rdbl(x, off, 0, "loc", 0.0), sc = rdbl(x, off, 1, "scale", 1.0);
    if (sc < 0) throw_new(cls_ValueError, "scale < 0");
    return rfill(s, rarg(x, off, 2, "size"), g_norm, loc, sc);
}
static Value nr_uniform(Args *x, int off, u64 *s)
{
    double lo = rdbl(x, off, 0, "low", 0.0), hi = rdbl(x, off, 1, "high", 1.0);
    return rfill(s, rarg(x, off, 2, "size"), g_unif, lo, hi);
}
static Value nr_exponential(Args *x, int off, u64 *s)
{
    double sc = rdbl(x, off, 0, "scale", 1.0);
    if (sc < 0) throw_new(cls_ValueError, "scale < 0");
    return rfill(s, rarg(x, off, 1, "size"), g_exp, sc, 0.0);
}
static Value nr_stdexp(Args *x, int off, u64 *s)
{
    return rfill(s, rarg(x, off, 0, "size"), g_exp, 1.0, 0.0);
}
/* integers: legacy randint has high exclusive; Generator.integers adds endpoint */
static Value nr_ints(Args *x, int off, u64 *s, int gen)
{
    Value lov = rarg(x, off, 0, "low"), hiv = rarg(x, off, 1, "high"), szv = rarg(x, off, 2, "size");
    Value epv = gen ? kwget(x, "endpoint") : UNSET_V;
    long lo, hi, shape[ND_MAXD], i;
    int nd;
    Nd *r;
    if (!np_has(lov)) throw_new(cls_TypeError, "integers() missing required argument 'low'");
    if (!np_has(hiv)) { hi = need_long(lov, "low"); lo = 0; }
    else { lo = need_long(lov, "low"); hi = need_long(hiv, "high"); }
    if (np_has(epv) && py_truthy(epv)) hi++;
    if (lo >= hi) throw_new(cls_ValueError, np_has(hiv) ? "low >= high" : "high <= 0");
    nd = rsize(szv, shape);
    if (nd < 0) return mkint(lo + (long)nrng_below(s, (u64)hi - (u64)lo));
    r = nd_new(ND_INT, nd, shape);
    for (i = 0; i < r->size; i++) r->iv[i] = lo + (long)nrng_below(s, (u64)hi - (u64)lo);
    return nd_ret(r);
}
static Value nr_randint(Args *x, int off, u64 *s) { return nr_ints(x, off, s, 0); }
static Value nr_integers(Args *x, int off, u64 *s) { return nr_ints(x, off, s, 1); }

/* in-place Fisher-Yates of the first axis of an array / list */
static void shuffle_nd(Nd *a, u64 *s)
{
    long n, row, i, j;
    Nd *tmp;
    if (a->nd == 0) throw_new(cls_TypeError, "shuffle: 0-d array");
    n = a->shape[0];
    row = n > 0 ? a->size / n : 0;
    tmp = nd_new(a->dt, 1, &row);
    for (i = n - 1; i > 0; i--) {
        j = (long)nrng_below(s, (u64)i + 1);
        if (j != i && row > 0) {
            nd_cp(tmp, 0, a, i * row, row);
            nd_cp(a, i * row, a, j * row, row);
            nd_cp(a, j * row, tmp, 0, row);
        }
    }
}
static Value nr_shuffle(Args *x, int off, u64 *s)
{
    Value v;
    if (RN < 1) throw_new(cls_TypeError, "shuffle() missing 1 required positional argument: 'x'");
    v = RA(0);
    if (v.t == T_ND) shuffle_nd(AS_ND(v), s);
    else if (v.t == T_LIST) {
        List *l = AS_LIST(v);
        long i, j;
        for (i = l->n - 1; i > 0; i--) {
            Value t;
            j = (long)nrng_below(s, (u64)i + 1);
            t = l->a[i]; l->a[i] = l->a[j]; l->a[j] = t;
        }
    } else throw_new(cls_TypeError, "shuffle: unsupported type '%.40s'", type_name(v));
    return NONE_V;
}
static Value nr_permutation(Args *x, int off, u64 *s)
{
    Value v;
    Nd *r;
    if (RN < 1) throw_new(cls_TypeError, "permutation() missing 1 required positional argument: 'x'");
    v = RA(0);
    if (v.t == T_INT) {
        long n = v.u.i, i;
        if (n < 0) throw_new(cls_ValueError, "negative dimensions are not allowed");
        r = nd_new1(ND_INT, n);
        for (i = 0; i < n; i++) r->iv[i] = i;
    } else {
        r = nd_from_value(v, -1);
        if (r->nd == 0) throw_new(cls_ValueError, "x must be an integer or at least 1-dimensional");
    }
    shuffle_nd(r, s);
    return ndv(r);
}
static Value nr_choice(Args *x, int off, u64 *s)
{
    Value av = rarg(x, off, 0, "a"), szv = rarg(x, off, 1, "size"), rpv = rarg(x, off, 2, "replace");
    Value pv = rarg(x, off, 3, "p");
    Nd *src, *idx, *res;
    long n, cnt, i, j, shape[ND_MAXD], row;
    int nd, repl = 1, k;
    double *cum = NULL;
    if (!np_has(av)) throw_new(cls_TypeError, "choice() missing required argument 'a'");
    if (np_has(rpv)) repl = py_truthy(rpv);
    if (av.t == T_INT) {
        n = av.u.i;
        if (n <= 0) throw_new(cls_ValueError, "a must be a positive integer unless no samples are taken");
        src = NULL;
    } else {
        src = nd_from_value(av, -1);
        if (src->nd < 1) throw_new(cls_ValueError, "a must be 1-dimensional or an integer");
        n = src->shape[0];
        if (n == 0) throw_new(cls_ValueError, "a cannot be empty unless no samples are taken");
    }
    nd = rsize(szv, shape);
    cnt = 1;
    if (nd >= 0) for (k = 0; k < nd; k++) cnt *= shape[k];
    if (np_has(pv)) {
        Nd *p = np_f(pv);
        double tot = 0.0;
        if (p->size != n) throw_new(cls_ValueError, "a and p must have same size");
        cum = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
        for (i = 0; i < n; i++) {
            if (p->dv[i] < 0) { free(cum); throw_new(cls_ValueError, "probabilities are not non-negative"); }
            tot += p->dv[i];
            cum[i] = tot;
        }
        if (fabs(tot - 1.0) > 1e-8) { free(cum); throw_new(cls_ValueError, "probabilities do not sum to 1"); }
        for (i = 0; i < n; i++) cum[i] /= tot;
    }
    if (!repl && cnt > n) {
        free(cum);
        throw_new(cls_ValueError, "Cannot take a larger sample than population when 'replace=False'");
    }
    idx = nd_new1(ND_INT, cnt);
    if (cum) {
        if (repl) {
            for (i = 0; i < cnt; i++) {
                double u = nrng_dbl(s);
                long lo = 0, hi = n - 1;
                while (lo < hi) {
                    long mid = (lo + hi) / 2;
                    if (cum[mid] > u) hi = mid; else lo = mid + 1;
                }
                idx->iv[i] = lo;
            }
        } else {
            /* sequential removal, renormalising */
            double *w = (double *)xmalloc(sizeof(double) * (size_t)(n + 1));
            double prev = 0.0;
            for (i = 0; i < n; i++) { w[i] = cum[i] - prev; prev = cum[i]; }
            for (i = 0; i < cnt; i++) {
                double tot = 0.0, u, acc = 0.0;
                long pick = n - 1;
                for (j = 0; j < n; j++) tot += w[j];
                u = nrng_dbl(s) * tot;
                for (j = 0; j < n; j++) {
                    acc += w[j];
                    if (w[j] > 0 && acc > u) { pick = j; break; }
                }
                idx->iv[i] = pick;
                w[pick] = 0.0;
            }
            free(w);
        }
        free(cum);
    } else if (repl) {
        for (i = 0; i < cnt; i++) idx->iv[i] = (long)nrng_below(s, (u64)n);
    } else {
        long *pool = (long *)xmalloc(sizeof(long) * (size_t)n);
        for (i = 0; i < n; i++) pool[i] = i;
        for (i = 0; i < cnt; i++) {
            long t;
            j = i + (long)nrng_below(s, (u64)(n - i));
            t = pool[i]; pool[i] = pool[j]; pool[j] = t;
            idx->iv[i] = pool[i];
        }
        free(pool);
    }
    if (nd < 0) {
        if (!src) return mkint(idx->iv[0]);
        if (src->nd == 1) return nd_elem(src, idx->iv[0]);
        row = src->size / n;
        res = nd_new(src->dt, src->nd - 1, src->shape + 1);
        nd_cp(res, 0, src, idx->iv[0] * row, row);
        return ndv(res);
    }
    if (!src) {
        res = nd_new(ND_INT, nd, shape);
        for (i = 0; i < cnt; i++) res->iv[i] = idx->iv[i];
        return nd_ret(res);
    }
    row = src->size / n;
    {
        long rs[ND_MAXD];
        int rn = nd;
        for (k = 0; k < nd; k++) rs[k] = shape[k];
        for (k = 1; k < src->nd && rn < ND_MAXD; k++) rs[rn++] = src->shape[k];
        res = nd_new(src->dt, rn, rs);
    }
    for (i = 0; i < cnt; i++) nd_cp(res, i * row, src, idx->iv[i] * row, row);
    return nd_ret(res);
}
static long nr_poisson1(u64 *s, double lam)
{
    long k = 0;
    while (lam > 500.0) {
        /* split: Poisson(lam) = Poisson(500) + Poisson(lam - 500) */
        k += nr_poisson1(s, 500.0);
        lam -= 500.0;
    }
    {
        double L = exp(-lam), p = 1.0;
        do { k++; p *= nrng_dbl(s); } while (p > L);
    }
    return k - 1;
}
static Value nr_poisson(Args *x, int off, u64 *s)
{
    double lam = rdbl(x, off, 0, "lam", 1.0);
    long shape[ND_MAXD], i;
    int nd = rsize(rarg(x, off, 1, "size"), shape);
    Nd *r;
    if (lam < 0) throw_new(cls_ValueError, "lam < 0 or lam is NaN");
    if (nd < 0) return mkint(nr_poisson1(s, lam));
    r = nd_new(ND_INT, nd, shape);
    for (i = 0; i < r->size; i++) r->iv[i] = nr_poisson1(s, lam);
    return nd_ret(r);
}
static long nr_binom1(u64 *s, long n, double p)
{
    long k = 0, i;
    for (i = 0; i < n; i++) if (nrng_dbl(s) < p) k++;
    return k;
}
static Value nr_binomial(Args *x, int off, u64 *s)
{
    long n = need_long(rarg(x, off, 0, "n"), "n"), shape[ND_MAXD], i;
    double p = rdbl(x, off, 1, "p", 0.5);
    int nd = rsize(rarg(x, off, 2, "size"), shape);
    Nd *r;
    if (n < 0) throw_new(cls_ValueError, "n < 0");
    if (!(p >= 0.0 && p <= 1.0)) throw_new(cls_ValueError, "p < 0, p > 1 or p is NaN");
    if (nd < 0) return mkint(nr_binom1(s, n, p));
    r = nd_new(ND_INT, nd, shape);
    for (i = 0; i < r->size; i++) r->iv[i] = nr_binom1(s, n, p);
    return nd_ret(r);
}

/* module-level (global state) and method (instance state) wrappers */
static u64 *self_state(Args *x)
{
    Value *sv;
    if (x->n < 1 || x->a[0].t != T_INST) throw_new(cls_TypeError, "descriptor requires a Generator object");
    sv = amap_get(&AS_INST(x->a[0])->a, intern_c("_s"));
    if (!sv || sv->t != T_ND) throw_new(cls_TypeError, "uninitialised Generator");
    return (u64 *)AS_ND(*sv)->iv;
}
#define RFN(nm, impl) \
    BI(rm_##nm) { return impl(x, 0, g_rs); } \
    BI(rg_##nm) { return impl(x, 1, self_state(x)); }
RFN(random, nr_random)
RFN(stdnorm, nr_stdnorm)
RFN(normal, nr_normal)
RFN(uniform, nr_uniform)
RFN(exponential, nr_exponential)
RFN(stdexp, nr_stdexp)
RFN(randint, nr_randint)
RFN(integers, nr_integers)
RFN(choice, nr_choice)
RFN(shuffle, nr_shuffle)
RFN(permutation, nr_permutation)
RFN(poisson, nr_poisson)
RFN(binomial, nr_binomial)
BI(rm_rand) { return rdims(x, 0, g_rs, g_unif); }
BI(rg_rand) { return rdims(x, 1, self_state(x), g_unif); }
BI(rm_randn) { return rdims(x, 0, g_rs, g_norm); }
BI(rg_randn) { return rdims(x, 1, self_state(x), g_norm); }
BI(rm_seed)
{
    Value sv = x->n > 0 ? A(0) : kwget(x, "seed");
    seed_from(g_rs, sv);
    return NONE_V;
}
static Value make_rng(Class *c, Value seed)
{
    Inst *in = new_inst(c);
    Nd *st = nd_new1(ND_INT, 4);
    Value iv = mkobj(in, T_INST);
    VPUSH(iv);
    seed_from((u64 *)st->iv, seed);
    amap_set(&in->a, intern_c("_s"), ndv(st));
    vsp--;
    return iv;
}
BI(rm_default_rng)
{
    Value sv = x->n > 0 ? A(0) : kwget(x, "seed");
    if (sv.t == T_INST && AS_INST(sv)->cls == cls_rng[0]) return sv;
    return make_rng(cls_rng[0], sv);
}
BI(rm_RandomState)
{
    Value sv = x->n > 0 ? A(0) : kwget(x, "seed");
    return make_rng(cls_rng[1], sv);
}
BI(rg_seed)
{
    Value sv = x->n > 1 ? A(1) : kwget(x, "seed");
    seed_from(self_state(x), sv);
    return NONE_V;
}
static void rnd_methods(Class *c)
{
    def_bi(c, "random", rg_random); def_bi(c, "random_sample", rg_random);
    def_bi(c, "standard_normal", rg_stdnorm); def_bi(c, "normal", rg_normal);
    def_bi(c, "uniform", rg_uniform); def_bi(c, "exponential", rg_exponential);
    def_bi(c, "standard_exponential", rg_stdexp);
    def_bi(c, "integers", rg_integers); def_bi(c, "randint", rg_randint);
    def_bi(c, "choice", rg_choice); def_bi(c, "shuffle", rg_shuffle);
    def_bi(c, "permutation", rg_permutation); def_bi(c, "poisson", rg_poisson);
    def_bi(c, "binomial", rg_binomial); def_bi(c, "rand", rg_rand);
    def_bi(c, "randn", rg_randn); def_bi(c, "seed", rg_seed);
}
static void rnd_init(Module *np)
{
    Module *m = new_module("numpy.random");
    int i;
    static const char *nm[2] = { "Generator", "RandomState" };
    (void)rm_integers;
    for (i = 0; i < 2; i++) {
        Class **b = (Class **)xmalloc(sizeof(Class *));
        b[0] = cls_object;
        cls_rng[i] = make_class(intern_c(nm[i]), b, 1, intern_c("numpy.random"));
        rnd_methods(cls_rng[i]);
        mod_val(m, nm[i], mkobj(cls_rng[i], T_CLASS));
    }
    cls_rng[1]->ctor = rm_RandomState;
    nrng_seed(g_rs, (u64)time(NULL) * 2654435761UL + (u64)clock());
    mod_def(m, "seed", rm_seed); mod_def(m, "default_rng", rm_default_rng);
    mod_def(m, "random", rm_random); mod_def(m, "random_sample", rm_random);
    mod_def(m, "ranf", rm_random); mod_def(m, "sample", rm_random);
    mod_def(m, "rand", rm_rand); mod_def(m, "randn", rm_randn);
    mod_def(m, "standard_normal", rm_stdnorm); mod_def(m, "normal", rm_normal);
    mod_def(m, "uniform", rm_uniform); mod_def(m, "exponential", rm_exponential);
    mod_def(m, "standard_exponential", rm_stdexp);
    mod_def(m, "randint", rm_randint); mod_def(m, "choice", rm_choice);
    mod_def(m, "shuffle", rm_shuffle); mod_def(m, "permutation", rm_permutation);
    mod_def(m, "poisson", rm_poisson); mod_def(m, "binomial", rm_binomial);
    mod_val(np, "random", mkobj(m, T_MODULE));
}
/* ===== p7_imp.c ===== */
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
/* ===== p7_mod.c ===== */
/* =========================================================================
   p7_mod.c   builtin modules (math random sys time) and initialisation
   ========================================================================= */
static Module *new_module(const char *name)
{
    Module *m = (Module *)gc_alloc(sizeof(Module), T_MODULE);
    m->name = intern_c(name);
    m->ns.k = NULL; m->ns.v = NULL; m->ns.n = m->ns.cap = 0;
    m->gp = NULL;
    m->gmap = NULL;
    if (sys_modules_v.t == T_DICT) dict_set(AS_DICT(sys_modules_v), mkobj(m->name, T_STR), mkobj(m, T_MODULE));
    return m;
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

/* ---- more math: isclose fsum log1p expm1 erf gamma ... (portable C89 versions) ---- */
BI(m_isclose)
{
    double a, b, rel = 1e-9, ab = 0.0, diff;
    Value v;
    need(x, 2, 4, "isclose");
    a = dbl_arg(x, 0); b = dbl_arg(x, 1);
    v = x->n > 2 ? A(2) : kwget(x, "rel_tol");
    if (v.t != T_UNSET) rel = to_dbl(v);
    v = x->n > 3 ? A(3) : kwget(x, "abs_tol");
    if (v.t != T_UNSET) ab = to_dbl(v);
    if (a == b) return TRUE_V;
    if (a - a != 0 || b - b != 0) return FALSE_V;
    diff = fabs(b - a);
    return mkbool(diff <= fabs(rel * b) || diff <= fabs(rel * a) || diff <= ab);
}
BI(m_fsum)
{
    double *p = (double *)xmalloc(sizeof(double) * 64), xv, y, hi, lo = 0.0, yr;
    long np = 0, cap = 64, i, j, n;
    Value it, v;
    need(x, 1, 1, "fsum");
    it = get_iter(A(0));
    VPUSH(it);
    while (iter_next(it, &v)) {
        xv = to_dbl(v);
        for (i = j = 0; j < np; j++) {
            y = p[j];
            if (fabs(xv) < fabs(y)) { double t = xv; xv = y; y = t; }
            hi = xv + y;
            yr = hi - xv;
            lo = y - yr;
            if (lo != 0.0) p[i++] = lo;
            xv = hi;
        }
        np = i;
        if (np >= cap) { cap *= 2; p = (double *)xrealloc(p, sizeof(double) * (size_t)cap); }
        p[np++] = xv;
    }
    vsp--;
    n = np;
    hi = 0.0;
    if (n > 0) {
        hi = p[--n];
        lo = 0.0;
        while (n > 0) {
            xv = hi;
            y = p[--n];
            hi = xv + y;
            yr = hi - xv;
            lo = y - yr;
            if (lo != 0.0) break;
        }
        if (n > 0 && ((lo < 0.0 && p[n - 1] < 0.0) || (lo > 0.0 && p[n - 1] > 0.0))) {
            y = lo * 2.0;
            xv = hi + y;
            yr = xv - hi;
            if (y == yr) hi = xv;
        }
    }
    free(p);
    return mkfloat(hi);
}
static double d_log1p(double x)
{
    double u = 1.0 + x;
    if (u == 1.0) return x;
    return log(u) * x / (u - 1.0);
}
static double d_expm1(double x)
{
    double u = exp(x), um1;
    if (u == 1.0) return x;
    um1 = u - 1.0;
    if (um1 == -1.0) return -1.0;
    if (u - u != 0) return u;
    return um1 * x / log(u);
}
static double d_erf_series(double x)
{
    double term = x, sum = x, x2 = x * x;
    int n;
    for (n = 0; n < 400; n++) {
        term *= 2.0 * x2 / (2.0 * n + 3.0);
        sum += term;
        if (fabs(term) < 1e-17 * fabs(sum)) break;
    }
    return 1.1283791670955126 * exp(-x2) * sum;
}
static double d_erfc_cf(double x)
{
    /* continued fraction for x > 0, modified Lentz */
    double tiny = 1e-300, f, c, d, delta;
    int n;
    f = x; if (f == 0) f = tiny;
    c = f; d = 0.0;
    for (n = 1; n < 2000; n++) {
        double an = n * 0.5;
        d = x + an * d; if (d == 0) d = tiny;
        c = x + an / c; if (c == 0) c = tiny;
        d = 1.0 / d;
        delta = c * d;
        f *= delta;
        if (fabs(delta - 1.0) < 1e-16) break;
    }
    return exp(-x * x) / f * 0.5641895835477563;
}
static double d_erf(double x)
{
    double a = fabs(x), r;
    if (x != x) return x;
    if (a < 2.0) r = d_erf_series(a);
    else if (a > 6.0) r = 1.0;
    else r = 1.0 - d_erfc_cf(a);
    return x < 0 ? -r : r;
}
static double d_erfc(double x)
{
    if (x != x) return x;
    if (x < 0.5) return 1.0 - d_erf(x);
    if (x > 27.0) return 0.0;
    return d_erfc_cf(x);
}
static double d_lanczos_sum(double x)
{
    static const double c[9] = { 0.99999999999980993, 676.5203681218851, -1259.1392167224028,
        771.32342877765313, -176.61502916214059, 12.507343278686905, -0.13857109526572012,
        9.9843695780195716e-6, 1.5056327351493116e-7 };
    double a = c[0];
    int i;
    for (i = 1; i < 9; i++) a += c[i] / (x + i);
    return a;
}
static double d_gamma(double x)
{
    double PI = 3.14159265358979323846;
    if (x == floor(x) && x > 0 && x < 171.0) {
        double r = 1.0;
        int i, n = (int)x;
        for (i = 2; i < n; i++) r *= (double)i;
        return r;
    }
    if (x < 0.5) return PI / (sin(PI * x) * d_gamma(1.0 - x));
    x -= 1.0;
    {
        double t = x + 7.5;
        return sqrt(2.0 * PI) * pow(t, x + 0.5) * exp(-t) * d_lanczos_sum(x);
    }
}
static double d_lgamma(double x)
{
    double PI = 3.14159265358979323846;
    if (x < 0.5) return log(PI / fabs(sin(PI * x))) - d_lgamma(1.0 - x);
    x -= 1.0;
    {
        double t = x + 7.5;
        return 0.5 * log(2.0 * PI) + (x + 0.5) * log(t) - t + log(d_lanczos_sum(x));
    }
}
#define MATH1B(name, expr) \
    BI(name) { double v; need(x, 1, 1, "math"); v = dbl_arg(x, 0); return mkfloat(expr); }
MATH1B(m_log1p, d_log1p(v))
MATH1B(m_expm1, d_expm1(v))
MATH1B(m_erf, d_erf(v))
MATH1B(m_erfc, d_erfc(v))
MATH1B(m_exp2, pow(2.0, v))
BI(m_gamma)
{
    double v;
    need(x, 1, 1, "gamma");
    v = dbl_arg(x, 0);
    if (v == floor(v) && v <= 0) throw_new(cls_ValueError, "math domain error");
    if (v > 171.7) throw_new(cls_OverflowError, "math range error");
    return mkfloat(d_gamma(v));
}
BI(m_lgamma)
{
    double v;
    need(x, 1, 1, "lgamma");
    v = dbl_arg(x, 0);
    if (v == floor(v) && v <= 0) throw_new(cls_ValueError, "math domain error");
    return mkfloat(d_lgamma(v));
}
BI(m_asinh)
{
    double v, a, r;
    need(x, 1, 1, "asinh");
    v = dbl_arg(x, 0);
    a = fabs(v);
    if (a < 1e-8) return mkfloat(v);
    r = a > 1e8 ? log(a) + 0.69314718055994530942 : d_log1p(a + a * a / (1.0 + sqrt(1.0 + a * a)));
    return mkfloat(v < 0 ? -r : r);
}
BI(m_acosh)
{
    double v;
    need(x, 1, 1, "acosh");
    v = dbl_arg(x, 0);
    domain_check(v >= 1.0);
    if (v > 1e8) return mkfloat(log(v) + 0.69314718055994530942);
    return mkfloat(d_log1p((v - 1.0) + sqrt((v - 1.0) * (v + 1.0))));
}
BI(m_atanh)
{
    double v;
    need(x, 1, 1, "atanh");
    v = dbl_arg(x, 0);
    domain_check(v > -1.0 && v < 1.0);
    return mkfloat(0.5 * d_log1p(2.0 * v / (1.0 - v)));
}
BI(m_cbrt)
{
    double v, a, r;
    need(x, 1, 1, "cbrt");
    v = dbl_arg(x, 0);
    if (v == 0 || v - v != 0) return mkfloat(v);
    a = fabs(v);
    r = pow(a, 1.0 / 3.0);
    r = r - (r * r * r - a) / (3.0 * r * r);
    return mkfloat(v < 0 ? -r : r);
}
BI(m_remainder)
{
    double a, b, r, q;
    need(x, 2, 2, "remainder");
    a = dbl_arg(x, 0); b = dbl_arg(x, 1);
    if (b == 0) throw_new(cls_ValueError, "math domain error");
    r = fmod(a, b);
    q = (a - r) / b;
    if (fabs(r) * 2.0 > fabs(b) || (fabs(r) * 2.0 == fabs(b) && fmod(q, 2.0) != 0.0))
        r -= (r < 0 ? -fabs(b) : fabs(b));
    return mkfloat(r);
}
BI(m_modf)
{
    double ip, fp;
    need(x, 1, 1, "modf");
    fp = modf(dbl_arg(x, 0), &ip);
    return mktuple2(mkfloat(fp), mkfloat(ip));
}
BI(m_frexp)
{
    int e;
    double m;
    need(x, 1, 1, "frexp");
    m = frexp(dbl_arg(x, 0), &e);
    return mktuple2(mkfloat(m), mkint(e));
}
BI(m_ldexp)
{
    need(x, 2, 2, "ldexp");
    return mkfloat(ldexp(dbl_arg(x, 0), (int)to_index(A(1))));
}
BI(m_perm)
{
    Value r = mkint(1);
    long n, k, i;
    need(x, 1, 2, "perm");
    n = to_index(A(0));
    k = x->n > 1 && A(1).t != T_NONE ? to_index(A(1)) : n;
    if (n < 0 || k < 0) throw_new(cls_ValueError, "n and k must be non-negative integers");
    if (k > n) return mkint(0);
    for (i = 0; i < k; i++) r = int_mul(r, mkint(n - i));
    return r;
}
BI(m_dist)
{
    Value p, q;
    double s = 0.0;
    long i;
    need(x, 2, 2, "dist");
    p = iter_to_list(A(0));
    VPUSH(p);
    q = iter_to_list(A(1));
    VPUSH(q);
    if (AS_LIST(p)->n != AS_LIST(q)->n)
        throw_new(cls_ValueError, "both points must have the same number of dimensions");
    for (i = 0; i < AS_LIST(p)->n; i++) {
        double d = to_dbl(AS_LIST(p)->a[i]) - to_dbl(AS_LIST(q)->a[i]);
        s += d * d;
    }
    vsp -= 2;
    return mkfloat(sqrt(s));
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
static Class *cls_dummy_exc[40];
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
    { "ModuleNotFoundError", &cls_ModuleNotFoundError, &cls_ImportError },
    { "FloatingPointError", &cls_dummy_exc[0], &cls_ArithmeticError },
    { "UnicodeError", &cls_dummy_exc[1], &cls_ValueError },
    { "UnicodeDecodeError", &cls_dummy_exc[2], &cls_dummy_exc[1] },
    { "UnicodeEncodeError", &cls_dummy_exc[3], &cls_dummy_exc[1] },
    { "MemoryError", &cls_dummy_exc[4], &cls_Exception },
    { "ReferenceError", &cls_dummy_exc[5], &cls_Exception },
    { "BufferError", &cls_dummy_exc[6], &cls_Exception },
    { "SystemError", &cls_dummy_exc[7], &cls_Exception },
    { "StopAsyncIteration", &cls_dummy_exc[8], &cls_Exception },
    { "PermissionError", &cls_dummy_exc[9], &cls_OSError },
    { "FileExistsError", &cls_dummy_exc[10], &cls_OSError },
    { "IsADirectoryError", &cls_dummy_exc[11], &cls_OSError },
    { "NotADirectoryError", &cls_dummy_exc[12], &cls_OSError },
    { "TimeoutError", &cls_dummy_exc[13], &cls_OSError },
    { "ConnectionError", &cls_dummy_exc[14], &cls_OSError },
    { "IndentationError", &cls_dummy_exc[15], &cls_SyntaxError },
    { "TabError", &cls_dummy_exc[16], &cls_dummy_exc[15] },
    { "Warning", &cls_dummy_exc[17], &cls_Exception },
    { "UserWarning", &cls_dummy_exc[18], &cls_dummy_exc[17] },
    { "DeprecationWarning", &cls_dummy_exc[19], &cls_dummy_exc[17] },
    { "PendingDeprecationWarning", &cls_dummy_exc[20], &cls_dummy_exc[17] },
    { "RuntimeWarning", &cls_dummy_exc[21], &cls_dummy_exc[17] },
    { "FutureWarning", &cls_dummy_exc[22], &cls_dummy_exc[17] },
    { "SyntaxWarning", &cls_dummy_exc[23], &cls_dummy_exc[17] },
    { "ImportWarning", &cls_dummy_exc[24], &cls_dummy_exc[17] },
    { "UnicodeWarning", &cls_dummy_exc[25], &cls_dummy_exc[17] },
    { "BytesWarning", &cls_dummy_exc[26], &cls_dummy_exc[17] },
    { "ResourceWarning", &cls_dummy_exc[27], &cls_dummy_exc[17] },
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
    const char *sargv0 = argc > 0 ? argv[0] : "";
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
    SN(S_super, "super"); SN(S_setattr, "__setattr__"); SN(S_new, "__new__");
    SN(S_module, "__module__"); SN(S_reversed, "__reversed__");
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
    def_bi(NULL, "format", bi_format);
    def_bi(NULL, "exit", bi_exit_fn); def_bi(NULL, "quit", bi_exit_fn);
    propsetter_fn = mkbuiltin("setter", bi_propsetter);
    propdeleter_fn = mkbuiltin("deleter", bi_propdeleter);
    propgetter_fn = mkbuiltin("getter", bi_propgetter);
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
    def_bi(cls_object, "__new__", nat_new);
    def_bi(cls_object, "__setattr__", obj_setattr);
    obj_str_v = *amap_get(&cls_object->ns, S_str);

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
    def_bi(cls_str, "isidentifier", s_isidentifier); def_bi(cls_str, "istitle", s_istitle);
    def_bi(cls_str, "isprintable", s_isprintable); def_bi(cls_str, "isascii", s_isascii);
    /* protocol methods of the native base types (also used by subclasses) */
    {
        Class *nc[5];
        int k;
        nc[0] = cls_str; nc[1] = cls_list; nc[2] = cls_tuple; nc[3] = cls_dict; nc[4] = cls_set;
        for (k = 0; k < 5; k++) {
            def_bi(nc[k], "__len__", nat_len); def_bi(nc[k], "__iter__", nat_iter);
            def_bi(nc[k], "__contains__", nat_contains); def_bi(nc[k], "__repr__", nat_repr);
            def_bi(nc[k], "__eq__", nat_eq); def_bi(nc[k], "__ne__", nat_ne);
            if (nc[k] != cls_set && nc[k] != cls_dict) {
                def_bi(nc[k], "__getitem__", nat_getitem);
                def_bi(nc[k], "__add__", nat_add); def_bi(nc[k], "__mul__", nat_mul);
                def_bi(nc[k], "__rmul__", nat_mul);
            }
            if (nc[k] != cls_dict) {
                def_bi(nc[k], "__lt__", nat_lt); def_bi(nc[k], "__le__", nat_le);
                def_bi(nc[k], "__gt__", nat_gt); def_bi(nc[k], "__ge__", nat_ge);
            }
            if (nc[k] == cls_str || nc[k] == cls_tuple) def_bi(nc[k], "__hash__", nat_hash);
        }
        def_bi(cls_str, "__str__", nat_str); def_bi(cls_str, "__mod__", nat_mod);
        def_bi(cls_str, "__format__", nat_format);
        def_bi(cls_list, "__setitem__", nat_setitem); def_bi(cls_list, "__delitem__", nat_delitem);
        def_bi(cls_dict, "__getitem__", nat_getitem);
        def_bi(cls_dict, "__setitem__", nat_setitem); def_bi(cls_dict, "__delitem__", nat_delitem);
        def_bi(cls_dict, "__or__", nat_or);
        def_bi(cls_set, "__or__", nat_or); def_bi(cls_set, "__and__", nat_and);
        def_bi(cls_set, "__sub__", nat_sub); def_bi(cls_set, "__xor__", nat_xor);
        def_bi(cls_dict, "__init__", d_update);
        def_bi(cls_list, "__init__", list_init);
        def_bi(cls_set, "__init__", set_init);
    }
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
    {
        Class *nk[2];
        int k;
        nk[0] = cls_int; nk[1] = cls_float;
        for (k = 0; k < 2; k++) {
            def_bi(nk[k], "__add__", num_add); def_bi(nk[k], "__radd__", num_radd);
            def_bi(nk[k], "__sub__", num_sub); def_bi(nk[k], "__rsub__", num_rsub);
            def_bi(nk[k], "__mul__", num_mul); def_bi(nk[k], "__rmul__", num_rmul);
            def_bi(nk[k], "__truediv__", num_truediv); def_bi(nk[k], "__rtruediv__", num_rtruediv);
            def_bi(nk[k], "__floordiv__", num_floordiv); def_bi(nk[k], "__rfloordiv__", num_rfloordiv);
            def_bi(nk[k], "__mod__", num_mod); def_bi(nk[k], "__rmod__", num_rmod);
            def_bi(nk[k], "__pow__", num_pow); def_bi(nk[k], "__rpow__", num_rpow);
            def_bi(nk[k], "__neg__", num_neg); def_bi(nk[k], "__pos__", num_pos);
            def_bi(nk[k], "__abs__", num_abs); def_bi(nk[k], "conjugate", num_conj);
            def_bi(nk[k], "__eq__", nat_eq); def_bi(nk[k], "__ne__", nat_ne);
            def_bi(nk[k], "__lt__", nat_lt); def_bi(nk[k], "__le__", nat_le);
            def_bi(nk[k], "__gt__", nat_gt); def_bi(nk[k], "__ge__", nat_ge);
        }
        def_bi(cls_int, "__and__", num_and); def_bi(cls_int, "__rand__", num_rand);
        def_bi(cls_int, "__or__", num_or); def_bi(cls_int, "__ror__", num_ror);
        def_bi(cls_int, "__xor__", num_xor); def_bi(cls_int, "__rxor__", num_rxor);
        def_bi(cls_int, "__lshift__", num_lshift); def_bi(cls_int, "__rlshift__", num_rlshift);
        def_bi(cls_int, "__rshift__", num_rshift); def_bi(cls_int, "__rrshift__", num_rrshift);
        def_bi(cls_int, "__invert__", num_invert);
    }
    /* numbers */
    def_bi(cls_int, "bit_length", i_bit_length);
    def_bi(cls_float, "is_integer", f_is_integer);
    /* generators / iterators */
    def_bi(cls_gen, "__next__", g_next); def_bi(cls_gen, "__iter__", g_iter);
    def_bi(cls_gen, "close", g_close); def_bi(cls_gen, "send", g_send);
    def_bi(cls_gen, "throw", g_throw);
    def_bi(cls_iter, "__next__", g_next); def_bi(cls_iter, "__iter__", g_iter);
    /* files */
    def_bi(cls_file, "read", fl_read); def_bi(cls_file, "readline", fl_readline);
    def_bi(cls_file, "readlines", fl_readlines); def_bi(cls_file, "write", fl_write);
    def_bi(cls_file, "writelines", fl_writelines); def_bi(cls_file, "flush", fl_flush);
    def_bi(cls_file, "close", fl_close); def_bi(cls_file, "__enter__", fl_enter);
    def_bi(cls_file, "__exit__", fl_exit);

    /* bytes */
    cls_bytes = mk_type("bytes", cls_object, bi_bytes);
    def_bi(cls_bytes, "decode", b_decode); def_bi(cls_bytes, "hex", b_hex);
    def_bi(cls_bytes, "fromhex", b_fromhex);
    def_bi(cls_bytes, "upper", b_upper); def_bi(cls_bytes, "lower", b_lower);
    def_bi(cls_bytes, "strip", b_strip); def_bi(cls_bytes, "lstrip", b_lstrip);
    def_bi(cls_bytes, "rstrip", b_rstrip); def_bi(cls_bytes, "split", b_split);
    def_bi(cls_bytes, "join", b_join); def_bi(cls_bytes, "replace", b_replace);
    def_bi(cls_bytes, "find", b_find); def_bi(cls_bytes, "count", b_count);
    def_bi(cls_bytes, "startswith", b_startswith); def_bi(cls_bytes, "endswith", b_endswith);
    def_bi(cls_bytes, "rfind", b_rfind); def_bi(cls_bytes, "index", b_index);
    def_bi(cls_bytes, "splitlines", b_splitlines); def_bi(cls_bytes, "isdigit", b_isdigit);
    def_bi(cls_bytes, "isalpha", b_isalpha);
    def_bi(cls_int, "to_bytes", i_to_bytes); def_bi(cls_int, "from_bytes", i_from_bytes);
    set_global(intern_c("bytearray"), mkobj(cls_bytes, T_CLASS));
    /* misc types */
    cls_slice = mk_type("slice", cls_object, bi_slice);
    def_bi(cls_slice, "indices", sl_indices);
    cls_notimpl = mk_type("NotImplementedType", cls_object, NULL);
    NotImpl_V = mkobj(new_inst(cls_notimpl), T_INST);
    set_global(intern_c("NotImplemented"), NotImpl_V);
    {
        Class *ce = mk_type("ellipsis", cls_object, NULL);
        Ellipsis_V = mkobj(new_inst(ce), T_INST);
        set_global(intern_c("Ellipsis"), Ellipsis_V);
        def_bi(ce, "__repr__", ellipsis_repr);
    }
    cls_dview[0] = mk_type("dict_keys", cls_object, NULL);
    cls_dview[1] = mk_type("dict_values", cls_object, NULL);
    cls_dview[2] = mk_type("dict_items", cls_object, NULL);
    def_bi(cls_notimpl, "__repr__", notimpl_repr);
    def_bi(NULL, "globals", bi_globals); def_bi(NULL, "locals", bi_locals);
    def_bi(NULL, "vars", bi_vars2); def_bi(NULL, "dir", bi_dir);
    def_bi(NULL, "exec", bi_exec); def_bi(NULL, "eval", bi_eval);
    def_bi(NULL, "__import__", bi_import);

    /* modules */
    sys_modules_v = mkobj(dict_new(), T_DICT);
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
    mod_def(m, "isclose", m_isclose); mod_def(m, "fsum", m_fsum); mod_def(m, "log1p", m_log1p);
    mod_def(m, "expm1", m_expm1); mod_def(m, "erf", m_erf); mod_def(m, "erfc", m_erfc);
    mod_def(m, "exp2", m_exp2); mod_def(m, "gamma", m_gamma); mod_def(m, "lgamma", m_lgamma);
    mod_def(m, "asinh", m_asinh); mod_def(m, "acosh", m_acosh); mod_def(m, "atanh", m_atanh);
    mod_def(m, "cbrt", m_cbrt); mod_def(m, "remainder", m_remainder); mod_def(m, "modf", m_modf);
    mod_def(m, "frexp", m_frexp); mod_def(m, "ldexp", m_ldexp); mod_def(m, "perm", m_perm);
    mod_def(m, "dist", m_dist);
    mod_val(m, "pi", mkfloat(3.14159265358979323846));
    mod_val(m, "e", mkfloat(2.71828182845904523536));
    mod_val(m, "tau", mkfloat(6.28318530717958647692));
    mod_val(m, "inf", mkfloat(HUGE_VAL));
    mod_val(m, "nan", mkfloat(HUGE_VAL - HUGE_VAL));
    np_init();
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
    sys_mod = m;
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
    mod_def(m, "exc_info", bi_exc_info);
    mod_val(m, "modules", sys_modules_v);
    init_sys_path(sargv0);
    mod_val(m, "path", sys_path_v);
    {
        Value vi[5];
        vi[0] = mkint(3); vi[1] = mkint(12); vi[2] = mkint(0); vi[3] = mkcstr("final"); vi[4] = mkint(0);
        mod_val(m, "version_info", mktuple_from(vi, 5));
    }
    mod_val(m, "byteorder", mkcstr("little"));
    mod_val(m, "executable", mkcstr(exe_path));
    mod_val(m, "builtin_module_names", mktuple_from(NULL, 0));
    m = new_module("os");
    {
        Module *pm = new_module("os.path");
        mod_def(m, "getenv", os_getenv); mod_def(m, "getcwd", os_getcwd);
        mod_def(m, "remove", os_remove); mod_def(m, "unlink", os_remove);
        mod_def(m, "rename", os_rename);
        mod_val(m, "sep", mkcstr("/")); mod_val(m, "pathsep", mkcstr(":"));
        mod_val(m, "linesep", mkcstr("\n")); mod_val(m, "name", mkcstr("posix"));
        mod_val(m, "curdir", mkcstr(".")); mod_val(m, "pardir", mkcstr(".."));
        mod_val(m, "devnull", mkcstr("/dev/null"));
        mod_val(m, "environ", init_environ());
        mod_val(m, "path", mkobj(pm, T_MODULE));
        mod_def(pm, "join", osp_join); mod_def(pm, "exists", osp_exists);
        mod_def(pm, "basename", osp_basename); mod_def(pm, "dirname", osp_dirname);
        mod_def(pm, "split", osp_split); mod_def(pm, "splitext", osp_splitext);
        mod_def(pm, "isabs", osp_isabs); mod_def(pm, "normpath", osp_normpath);
        mod_def(pm, "abspath", osp_abspath); mod_def(pm, "expanduser", osp_expanduser);
        mod_def(pm, "isdir", osp_isdir); mod_def(pm, "isfile", osp_isfile);
        mod_def(pm, "getsize", osp_getsize);
        mod_val(pm, "sep", mkcstr("/")); mod_val(pm, "curdir", mkcstr("."));
        mod_val(pm, "pardir", mkcstr("..")); mod_val(pm, "pathsep", mkcstr(":"));
    }
    {
        Module *mm = module_new_py(S_main);
        Value mv = mkobj(mm, T_MODULE);
        main_gp = mm->gp;
        dict_set(AS_DICT(sys_modules_v), mkobj(S_main, T_STR), mv);
        mod_gset(mm, "__name__", mkobj(S_main, T_STR));
        mod_gset(mm, "__doc__", NONE_V);
        mod_gset(mm, "__package__", NONE_V);
    }
    gc_perm = 0;
}
/* ===== p8_main.c ===== */
/* =========================================================================
   p8_main.c   command line, REPL, main
   ========================================================================= */
static int line_opens_block(const char *s, long n)
{
    long i = 0, e = n;
    int q = 0;
    long last = -1;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i < n && s[i] == '@') return 1;
    for (e = i; e < n; e++) {
        if (q) { if (s[e] == q) q = 0; continue; }
        if (s[e] == '"' || s[e] == '\'') { q = s[e]; continue; }
        if (s[e] == '#') break;
        if (s[e] != ' ' && s[e] != '\t' && s[e] != '\r' && s[e] != '\n') last = e;
    }
    return last >= 0 && s[last] == ':';
}
static int bracket_depth(const char *s, long n)
{
    long i;
    int d = 0, q = 0;
    for (i = 0; i < n; i++) {
        if (q) { if (s[i] == '\\') i++; else if (s[i] == q) q = 0; continue; }
        if (s[i] == '"' || s[i] == '\'') q = s[i];
        else if (s[i] == '#') { while (i < n && s[i] != '\n') i++; }
        else if (s[i] == '(' || s[i] == '[' || s[i] == '{') d++;
        else if (s[i] == ')' || s[i] == ']' || s[i] == '}') d--;
    }
    return d;
}

static void repl(int prompt)
{
    Buf acc;
    int c, block = 0;
    buf_init(&acc);
    script_name = "<stdin>";
    repl_echo = 1;
    for (;;) {
        Buf line;
        int blank;
        if (prompt) {
            fputs(acc.n == 0 ? ">>> " : "... ", stdout);
            fflush(stdout);
        }
        buf_init(&line);
        while ((c = getc(stdin)) != EOF && c != '\n') buf_addc(&line, c);
        if (c == EOF && line.n == 0) { buf_free(&line); break; }
        buf_addc(&line, '\n');
        blank = 1;
        {
            long i;
            for (i = 0; i < line.n; i++)
                if (line.s[i] != ' ' && line.s[i] != '\t' && line.s[i] != '\n' && line.s[i] != '\r')
                    blank = 0;
        }
        if (acc.n == 0 && blank) { buf_free(&line); continue; }
        if (acc.n == 0 && line_opens_block(line.s, line.n)) block = 1;
        buf_addn(&acc, line.s, line.n);
        buf_free(&line);
        if (block) {
            if (!blank) continue;
        } else if (bracket_depth(acc.s, acc.n) > 0) continue;
        run_source(acc.s, acc.n);
        acc.n = 0;
        acc.s[0] = 0;
        block = 0;
        fflush(stdout);
    }
    if (acc.n > 0) run_source(acc.s, acc.n);
    buf_free(&acc);
    if (prompt) fputc('\n', stdout);
}

int main(int argc, char **argv)
{
    const char *code = NULL, *file = NULL;
    int force_i = 0, i = 1, sargc = 0, rc = 0;
    char **sargv = NULL;
    static char dash_c[] = "-c";
    static char empty[] = "";
    char *dummy[2];
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            code = argv[i + 1];
            dummy[0] = dash_c;
            sargv = dummy;
            sargc = 1;
            i += 2;
            break;
        } else if (strcmp(argv[i], "-i") == 0) {
            force_i = 1;
            i++;
        } else break;
    }
    if (!code && i < argc) {
        file = argv[i];
        sargv = argv + i;
        sargc = argc - i;
    } else if (code) {
        /* sys.argv = ['-c', rest...] */
        sargv = (char **)xmalloc(sizeof(char *) * (size_t)(argc + 2));
        sargv[0] = dash_c;
        {
            int k;
            for (k = i; k < argc; k++) sargv[1 + k - i] = argv[k];
            sargc = 1 + argc - i;
        }
    } else {
        dummy[0] = empty;
        sargv = dummy;
        sargc = 1;
    }
    exe_path = argv[0];
    init_runtime(sargc, sargv);
    if (code) {
        script_name = "<string>";
        rc = run_source(code, (long)strlen(code));
    } else if (file) {
        long len;
        char *src = read_whole(file, &len);
        if (!src) {
            fprintf(stderr, "python: can't open file '%s'\n", file);
            return 2;
        }
        script_name = file;
        rc = run_source(src, len);
    } else {
        int prompt = force_i;
#ifdef PY_POSIX
        if (isatty(0)) prompt = 1;
#endif
        repl(prompt);
    }
    fflush(stdout);
    return rc ? 1 : 0;
}
