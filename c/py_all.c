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
    T_SMETH, T_CMETH, T_SUPER, T_FILE, T_NTYPES
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
} Class;
#define CF_EXC 1
#define CF_PROPS 2
#define CF_BUILTIN 4

typedef struct Inst { Hdr h; Class *cls; AMap a; } Inst;

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
} Func;

typedef struct Builtin { Hdr h; BFn fn; const char *name; Str *sname; } Builtin;
typedef struct Bound { Hdr h; Value self; Value fn; } Bound;
typedef struct Module { Hdr h; Str *name; AMap ns; } Module;
typedef struct Range { Hdr h; long start, stop, step; } Range;
typedef struct Prop { Hdr h; Value get, set; } Prop;
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
    int started, done, running;
} Gen;

enum { IK_LIST, IK_TUPLE, IK_STR, IK_RANGE, IK_DICT, IK_DVALS, IK_DITEMS, IK_SET,
       IK_ENUM, IK_ZIP, IK_MAP, IK_FILTER, IK_REV, IK_FILE, IK_CALL, IK_LISTREV };
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
    struct Frame *up;
} Frame;

typedef struct Handler {
    jmp_buf jb;
    struct Handler *prev;
    Value *sp;
    int depth;
    Frame *frame;
} Handler;

static Value NONE_V, TRUE_V, FALSE_V, UNSET_V;
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
    *cls_smeth, *cls_cmeth, *cls_super;
static Class *cls_BaseException, *cls_Exception, *cls_ArithmeticError, *cls_ZeroDivisionError,
    *cls_OverflowError, *cls_LookupError, *cls_IndexError, *cls_KeyError, *cls_ValueError,
    *cls_TypeError, *cls_NameError, *cls_UnboundLocalError, *cls_AttributeError,
    *cls_RuntimeError, *cls_RecursionError, *cls_NotImplementedError, *cls_StopIteration,
    *cls_AssertionError, *cls_SystemExit, *cls_ImportError, *cls_OSError, *cls_EOFError,
    *cls_SyntaxError, *cls_KeyboardInterrupt, *cls_GeneratorExit;

/* interned names used by the runtime */
static Str *S_init, *S_str, *S_repr, *S_eq, *S_ne, *S_lt, *S_le, *S_gt, *S_ge, *S_add, *S_sub,
    *S_mul, *S_neg, *S_len, *S_getitem, *S_setitem, *S_delitem, *S_contains, *S_iter, *S_next,
    *S_call, *S_bool, *S_hash, *S_enter, *S_exit, *S_name, *S_class, *S_args, *S_truediv,
    *S_floordiv, *S_mod, *S_pow, *S_radd, *S_rsub, *S_rmul, *S_abs, *S_int, *S_float,
    *S_index, *S_pos, *S_invert, *S_and, *S_or, *S_xor, *S_lshift, *S_rshift, *S_dict,
    *S_mro, *S_doc, *S_setter, *S_main, *S_getattr, *S_format, *S_self, *S_super;

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
static unsigned long py_hash(Value v);
static Value get_attr(Value o, Str *nm);
static Value get_iter(Value o);
static int iter_next(Value it, Value *out);
static int py_truthy(Value v);
static Class *class_of(Value v);
static Value *class_find(Class *c, Str *nm);
static int class_isa(Class *c, Class *base);
static Value new_exc(Class *c, const char *msg);

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
    case T_STR: return (long)sizeof(Str) + ((Str *)h)->len;
    case T_BIG: return (long)sizeof(Big) + 2L * ((Big *)h)->n;
    case T_LIST: case T_TUPLE:
        return (long)sizeof(List) + (long)sizeof(Value) * ((List *)h)->cap;
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
    case T_LIST: case T_TUPLE: {
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
        gc_pushp(c->name, T_STR); gc_pushp(c->base, T_CLASS); gc_mark_amap(&c->ns);
        break; }
    case T_INST: {
        Inst *n = (Inst *)h;
        gc_pushp(n->cls, T_CLASS); gc_mark_amap(&n->a);
        break; }
    case T_FUNC: {
        Func *f = (Func *)h;
        int k;
        for (k = 0; f->cells && k < f->def->fd->nfree; k++) gc_pushp(f->cells[k], T_CELL);
        for (k = 0; k < f->ndefs; k++) gc_push(f->defs[k]);
        for (k = 0; f->kwdefs && k < f->def->fd->nkwonly; k++) gc_push(f->kwdefs[k]);
        gc_pushp(f->defclass, T_CLASS); gc_pushp(f->name, T_STR);
        break; }
    case T_BOUND: {
        Bound *b = (Bound *)h;
        gc_push(b->self); gc_push(b->fn);
        break; }
    case T_CELL: gc_push(((Cell *)h)->v); break;
    case T_MODULE: gc_pushp(((Module *)h)->name, T_STR); gc_mark_amap(&((Module *)h)->ns); break;
    case T_PROP: case T_SMETH: case T_CMETH:
        gc_push(((Prop *)h)->get); gc_push(((Prop *)h)->set);
        break;
    case T_SUPER: gc_pushp(((Super *)h)->start, T_CLASS); gc_push(((Super *)h)->obj); break;
    case T_GEN: {
        Gen *g = (Gen *)h;
        int k;
        for (k = 0; k < g->nloc; k++) gc_push(g->loc[k]);
        for (k = 0; k < g->nstate; k++) gc_push(g->state[k]);
        for (k = 0; g->cells && k < g->def->fd->nfree; k++) gc_pushp(g->cells[k], T_CELL);
        gc_push(g->yielded);
        gc_pushp(g->fn, T_FUNC);
        break; }
    case T_ITER: {
        Iter *t = (Iter *)h;
        gc_push(t->a); gc_push(t->b); gc_push(t->c);
        break; }
    case T_BUILTIN: gc_pushp(((Builtin *)h)->sname, T_STR); break;
    default: break;
    }
}
static void gc_free_obj(Hdr *h)
{
    switch (h->type) {
    case T_LIST: case T_TUPLE: free(((List *)h)->a); break;
    case T_DICT: case T_SET: free(((Dict *)h)->e); free(((Dict *)h)->idx); break;
    case T_CLASS: free(((Class *)h)->ns.k); free(((Class *)h)->ns.v); break;
    case T_INST: free(((Inst *)h)->a.k); free(((Inst *)h)->a.v); break;
    case T_FUNC: free(((Func *)h)->cells); free(((Func *)h)->defs); free(((Func *)h)->kwdefs);
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
    TK_ARROW, TK_ELLIPSIS, TK_AUG
};
enum {
    B_NONE = 0, B_ADD, B_SUB, B_MUL, B_DIV, B_FDIV, B_MOD, B_POW, B_AND, B_OR, B_XOR,
    B_LSH, B_RSH
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
            int fl = 0, rw = 0, q, triple = 0;
            long bs, be;
            while (pos < len && src[pos] != '"' && src[pos] != '\'') {
                if (src[pos] == 'f' || src[pos] == 'F') fl = 1;
                if (src[pos] == 'r' || src[pos] == 'R') rw = 1;
                pos++;
            }
            q = src[pos];
            if (pos + 2 < len && src[pos + 1] == q && src[pos + 2] == q) { triple = 1; pos += 3; }
            else pos++;
            bs = pos;
            for (;;) {
                if (pos >= len) syntax_error(line, "unterminated string literal");
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
                TK_AUG, TK_AUG, ':' };
            static const int tbop2[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, B_ADD, B_SUB, B_MUL, B_DIV,
                B_MOD, B_AND, B_OR, B_XOR, B_MUL, 0 };
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
/* ===== p4_res.c ===== */
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
/* ===== p5_eval.c ===== */
/* =========================================================================
   p5_eval.c   evaluator: variables, calls, expressions, statements,
               generators, exceptions
   ========================================================================= */
enum { ST_NORMAL = 0, ST_BREAK, ST_CONTINUE, ST_RETURN, ST_YIELD };

static int repl_echo;
static Value find_module(Str *nm);
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
    case VK_GLOBAL: x = G[v->slot]; break;
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
    return 0;
}

static Value call_class(Class *c, int n, Value *a, int nk, Value *kw)
{
    Inst *in;
    Value iv, *init;
    Value *base = vsp;
    int i;
    if (c->ctor) {
        Args x;
        x.n = n; x.a = a; x.nk = nk; x.kw = kw;
        return c->ctor(&x);
    }
    in = new_inst(c);
    iv = mkobj(in, T_INST);
    VPUSH(iv);
    init = class_find(c, S_init);
    if (init) {
        VPUSH(*init);
        VPUSH(iv);
        for (i = 0; i < n; i++) VPUSH(a[i]);
        call_value(*init, n + 1, base + 2, nk, kw);
    } else if (n > 0 || nk > 0) {
        throw_new(cls_TypeError, "%.80s() takes no arguments", c->name->s);
    }
    vsp = base;
    return iv;
}

static Value call_value(Value fn, int n, Value *a, int nk, Value *kw)
{
    switch (fn.t) {
    case T_FUNC: return call_func(AS_FUNC(fn), n, a, nk, kw);
    case T_BUILTIN: {
        Args x;
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
    if (cn->kind == N_NAME && cn->var->kind == VK_GLOBAL && cn->name == intern_c("super") &&
        n->n == 0 && G[cn->var->slot].t == T_BUILTIN) {
        Super *s;
        Value self;
        if (!f->fn || !f->fn->defclass || !f->loc || f->fn->def->fd->npos < 1)
            throw_new(cls_RuntimeError, "super(): no arguments");
        self = f->loc[0];
        if (self.t == T_CELL) self = ((Cell *)self.u.o)->v;
        s = (Super *)gc_alloc(sizeof(Super), T_SUPER);
        s->start = f->fn->defclass->base ? f->fn->defclass->base : cls_object;
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

static void exec_class(Frame *f, Node *n)
{
    FuncDef *fd = n->fd;
    Class *c, *base = cls_object;
    Value cv, *basev = vsp;
    int i, nbases = n->n - n->op;
    Frame cf;
    Cell **cells = NULL;
    for (i = 0; i < nbases; i++) {
        Value b = eval(f, n->list[i]);
        VPUSH(b);
        if (b.t != T_CLASS) throw_new(cls_TypeError, "bases must be types");
        if (i == 0) base = AS_CLASS(b);
    }
    c = (Class *)gc_alloc(sizeof(Class), T_CLASS);
    c->name = n->name;
    c->base = base;
    c->ns.k = NULL; c->ns.v = NULL; c->ns.n = c->ns.cap = 0;
    c->ctor = NULL;
    c->flags = base->flags & (CF_EXC | CF_PROPS);
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
    cf.up = cur_frame;
    cur_frame = &cf;
    exec_block(&cf, fd->body);
    cur_frame = cf.up;
    free(cells);
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
    class_epoch++;
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

static void exec_import(Frame *f, Node *n)
{
    int i;
    for (i = 0; i < n->n; i++) {
        Node *it = n->list[i];
        Value m = find_module(it->name);
        if (m.t == T_UNSET) throw_new(cls_ImportError, "No module named '%.80s'", it->name->s);
        store_var(f, it->b->var, m);
    }
}
static void exec_from(Frame *f, Node *n)
{
    int i;
    Value m = find_module(n->name);
    if (m.t == T_UNSET) throw_new(cls_ImportError, "No module named '%.80s'", n->name->s);
    if (n->op == 1) {
        Module *md = (Module *)m.u.o;
        for (i = 0; i < md->ns.n; i++)
            if (md->ns.k[i]->s[0] != '_')
                set_global(md->ns.k[i], md->ns.v[i]);
        return;
    }
    for (i = 0; i < n->n; i++) {
        Node *it = n->list[i];
        Value *p = amap_get(&((Module *)m.u.o)->ns, it->name);
        if (!p) throw_new(cls_ImportError, "cannot import name '%.80s' from '%.80s'", it->name->s, n->name->s);
        store_var(f, it->b->var, *p);
    }
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
        } else r = binop(n->op, cur, rhs);
        store_var(f, t->var, r);
        break; }
    case N_ATTR: {
        Value o = eval(f, t->a);
        VPUSH(o);
        cur = get_attr(o, t->name);
        VPUSH(cur);
        rhs = eval(f, n->b);
        VPUSH(rhs);
        r = binop(n->op, cur, rhs);
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
        r = binop(n->op, cur, rhs);
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
    if (f->resuming) {
        it = STATE(f, n, 0);
        f->resuming = 0;
    } else {
        it = get_iter(eval(f, n->a));
        STATE(f, n, 0) = it;
    }
    if (iter_next(it, &x)) {
        f->gen->yielded = x;
        return ST_YIELD;
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
        if (f->resuming) { f->resuming = 0; return 0; }
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
    Node *blk;
    Frame fr;
    gc_perm = 1;
    lex_source(src, len);
    mod_scope = scope_new(SC_MOD, NULL);
    cur_scope = mod_scope;
    blk = parse_module();
    resolve_scope(mod_scope);
    gc_perm = 0;
    fr.loc = NULL; fr.cells = NULL; fr.fn = NULL; fr.ret = NONE_V; fr.gen = NULL;
    fr.ns = NULL; fr.resuming = 0; fr.up = NULL;
    cur_frame = &fr;
    exec_block(&fr, blk);
    cur_frame = NULL;
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

static Class *cls_FileNotFoundError;
static Value object_init_v;

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
    return mkobj(b, T_BUILTIN);
}
static void def_bi(Class *c, const char *name, BFn fn)
{
    Value b = mkbuiltin(name, fn);
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
    c->flags = flags | (base ? (base->flags & CF_EXC) : 0);
    set_global(c->name, mkobj(c, T_CLASS));
    return c;
}
static long to_index(Value v)
{
    return need_long(v, "argument");
}

/* ---- output ----------------------------------------------------------- */
BI(bi_print)
{
    Value sepv = kwget(x, "sep"), endv = kwget(x, "end"), filev = kwget(x, "file");
    Value flv = kwget(x, "flush");
    FILE *fp = stdout;
    const char *sep = " ", *end = "\n";
    long sepn = 1, endn = 1;
    int i;
    if (sepv.t == T_STR) { sep = AS_STR(sepv)->s; sepn = AS_STR(sepv)->len; }
    if (endv.t == T_STR) { end = AS_STR(endv)->s; endn = AS_STR(endv)->len; }
    if (filev.t == T_FILE) fp = ((File *)filev.u.o)->fp;
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
    if (src.t == T_DICT) {
        Dict *s = AS_DICT(src);
        long i;
        for (i = 0; i < s->used; i++)
            if (s->e[i].k.t != T_UNSET) dict_set(d, s->e[i].k, s->e[i].v);
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
    if (v.t == T_DICT) v = iter_to_list(v);
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
    need(x, 1, 1, "iter");
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
BI(bi_type)
{
    need(x, 1, 1, "type");
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
    s->start = AS_CLASS(A(0))->base ? AS_CLASS(A(0))->base : cls_object;
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
    return mk_prop(T_PROP, p->get, A(1));
}
BI(bi_staticmethod) { need(x, 1, 1, "staticmethod"); return mk_prop(T_SMETH, A(0), UNSET_V); }
BI(bi_classmethod) { need(x, 1, 1, "classmethod"); return mk_prop(T_CMETH, A(0), UNSET_V); }
BI(bi_exit_fn)
{
    Value a0 = x->n > 0 ? A(0) : NONE_V;
    throw_val(new_exc_args(cls_SystemExit, &a0, x->n > 0 ? 1 : 0));
    return NONE_V;
}
BI(bi_vars)
{
    need(x, 1, 1, "vars");
    return get_attr(A(0), S_dict);
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

BI(s_upper)
{
    Str *s = SELF;
    Value r = mkstr(s->s, s->len);
    long i;
    for (i = 0; i < s->len; i++) AS_STR(r)->s[i] = (char)to_up[(unsigned char)s->s[i]];
    return r;
}
BI(s_lower)
{
    Str *s = SELF;
    Value r = mkstr(s->s, s->len);
    long i;
    for (i = 0; i < s->len; i++) AS_STR(r)->s[i] = (char)to_low[(unsigned char)s->s[i]];
    return r;
}
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
BI(s_encode) { return A(0); }
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
static Value dict_view(Dict *d, int what)
{
    List *l = list_new(d->n);
    long i;
    for (i = 0; i < d->used; i++) {
        if (d->e[i].k.t == T_UNSET) continue;
        if (what == 0) l->a[l->n++] = d->e[i].k;
        else if (what == 1) l->a[l->n++] = d->e[i].v;
        else {
            Value *base = vsp;
            VPUSH(mklist(l));
            l->a[l->n] = mktuple2(d->e[i].k, d->e[i].v);
            l->n++;
            vsp = base;
        }
    }
    return mklist(l);
}
BI(d_keys) { return dict_view(self_dict(x), 0); }
BI(d_values) { return dict_view(self_dict(x), 1); }
BI(d_items) { return dict_view(self_dict(x), 2); }
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
BI(g_next)
{
    Value out;
    if (iter_next(A(0), &out)) return out;
    throw_val(new_exc_args(cls_StopIteration, NULL, 0));
    return NONE_V;
}
BI(g_iter) { return A(0); }
BI(g_close)
{
    if (A(0).t == T_GEN) ((Gen *)A(0).u.o)->done = 1;
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
/* ===== p7_mod.c ===== */
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
/* ===== p8_main.c ===== */
/* =========================================================================
   p8_main.c   command line, REPL, main
   ========================================================================= */
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
    buf[n] = 0;
    fclose(fp);
    *len = n;
    return buf;
}

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
