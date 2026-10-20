/* rxnp - standalone C89 numerical core for the rxnumpy CREXX plugin.
 * Dense float64 / int64 n-d arrays (C order), no Python dependencies.
 * int64 is `long` (LP64: Linux/macOS 64-bit; see README for LLP64 note). */
#ifndef RXNP_H
#define RXNP_H
#include <stddef.h>

#define RXNP_MAXDIM 8
enum { RXNP_F64 = 0, RXNP_I64 = 1 };

typedef struct rxnp {
    int dt;                    /* RXNP_F64 / RXNP_I64 */
    int ndim;
    long shape[RXNP_MAXDIM];
    long size;
    double *f;                 /* data when dt == F64 */
    long *i;                   /* data when dt == I64 */
    long refs;                 /* reference count (owned by the plugin) */
} rxnp;

/* error handling: functions returning NULL / nonzero set a message */
const char *rxnp_error(void);

/* lifetime */
rxnp *rxnp_new(int dt, int ndim, const long *shape);   /* zero-filled */
rxnp *rxnp_retain(rxnp *a);
void  rxnp_release(rxnp *a);
rxnp *rxnp_copy(const rxnp *a);
rxnp *rxnp_astype(const rxnp *a, int dt);

/* creation */
int   rxnp_parse_shape(const char *s, long *shape, int *ndim);    /* "3,4" "(3 4)" */
rxnp *rxnp_from_text(const char *text);                           /* "[[1,2],[3,4]]" */
char *rxnp_to_text(const rxnp *a);                                /* malloc'd, tolist repr */
rxnp *rxnp_full(const char *shape, double v, int dt);
rxnp *rxnp_arange(double start, double stop, double step, int as_int);
rxnp *rxnp_linspace(double start, double stop, long num);
rxnp *rxnp_eye(long n);
rxnp *rxnp_scalar(double v);                                      /* 0-d f64 */

/* shape */
rxnp *rxnp_reshape(const rxnp *a, const char *shape);             /* -1 allowed */
rxnp *rxnp_transpose(const rxnp *a);                              /* reverse axes */
rxnp *rxnp_slice(const rxnp *a, int axis, long start, long stop, long step);
rxnp *rxnp_concat(const rxnp *a, const rxnp *b, int axis);
rxnp *rxnp_flatten(const rxnp *a);

/* element access (flat index, negative allowed) */
int   rxnp_getf(const rxnp *a, long idx, double *out);
int   rxnp_setf(rxnp *a, long idx, double v);

/* elementwise */
enum { RXNP_ADD, RXNP_SUB, RXNP_MUL, RXNP_DIV, RXNP_POW, RXNP_MOD,
       RXNP_FLOORDIV, RXNP_MAXIMUM, RXNP_MINIMUM, RXNP_ATAN2 };
rxnp *rxnp_binary(int op, const rxnp *a, const rxnp *b);          /* broadcasting */
enum { RXNP_NEG, RXNP_ABS, RXNP_SQRT, RXNP_EXP, RXNP_LOG, RXNP_SIN, RXNP_COS,
       RXNP_TAN, RXNP_TANH, RXNP_FLOOR, RXNP_CEIL, RXNP_SQUARE, RXNP_ROUND,
       RXNP_LOG10, RXNP_LOG2, RXNP_ARCTAN, RXNP_SIGN };
rxnp *rxnp_unary(int op, const rxnp *a);
rxnp *rxnp_clip(const rxnp *a, double lo, double hi);

/* reductions; axis < 0 with all=1 reduces everything */
enum { RXNP_SUM, RXNP_MEAN, RXNP_MIN, RXNP_MAX, RXNP_PROD, RXNP_STD, RXNP_VAR,
       RXNP_ARGMIN, RXNP_ARGMAX, RXNP_MEDIAN, RXNP_CUMSUM, RXNP_CUMPROD };
rxnp *rxnp_reduce(int op, const rxnp *a, int axis, int all, long ddof);
double rxnp_pairwise_sum(const double *x, long n, long stride);

/* sort */
rxnp *rxnp_sort(const rxnp *a);                                   /* last axis, stable */
rxnp *rxnp_argsort(const rxnp *a);

/* products */
rxnp *rxnp_matmul(const rxnp *a, const rxnp *b);                  /* np.dot semantics 1-d/2-d */
rxnp *rxnp_outer(const rxnp *a, const rxnp *b);
double rxnp_trace(const rxnp *a);

/* linear algebra (float64, square where required); NULL on failure */
rxnp *rxnp_solve(const rxnp *a, const rxnp *b);
rxnp *rxnp_inv(const rxnp *a);
int   rxnp_det(const rxnp *a, double *out);
rxnp *rxnp_cholesky(const rxnp *a);                               /* lower */
int   rxnp_qr(const rxnp *a, rxnp **q, rxnp **r);                 /* reduced */
int   rxnp_eigh(const rxnp *a, rxnp **w, rxnp **v);              /* symmetric, ascending */
int   rxnp_svd(const rxnp *a, rxnp **u, rxnp **s, rxnp **vt);    /* reduced */
rxnp *rxnp_eigvals(const rxnp *a);                                /* shape (n,2): re,im */
const char *rxnp_la_error(void);
double rxnp_norm(const rxnp *a);                                  /* Frobenius / 2-norm */

/* fft: result is shape (n,2) = [re, im] per row; input real (n) or (n,2) */
rxnp *rxnp_fft(const rxnp *a, int inverse);

/* random: legacy MT19937 (np.random.seed/rand/randn/randint) */
typedef struct rxnp_rng rxnp_rng;
rxnp_rng *rxnp_rng_new(void);
void  rxnp_rng_free(rxnp_rng *g);
void  rxnp_rng_seed(rxnp_rng *g, unsigned long seed);            /* np.random.seed(int) */
double rxnp_rng_random(rxnp_rng *g);                              /* random_sample */
double rxnp_rng_gauss(rxnp_rng *g);                               /* legacy gauss (randn) */
long  rxnp_rng_randint(rxnp_rng *g, long low, long high);        /* [low, high) */
rxnp *rxnp_rand(rxnp_rng *g, const char *shape);
rxnp *rxnp_randn(rxnp_rng *g, const char *shape);
rxnp *rxnp_randint(rxnp_rng *g, long low, long high, const char *shape);

/* random: numpy.random.default_rng() (SeedSequence + PCG64 + ziggurat), bit-exact with numpy; rxnp_pcg.c */
typedef struct rxnp_gen rxnp_gen;
rxnp_gen *rxnp_gen_new(unsigned long seed);                     /* refs = 1 */
rxnp_gen *rxnp_gen_new_array(const rxnp *seed_words);           /* int64 array entropy */
rxnp_gen *rxnp_gen_retain(rxnp_gen *g);
void  rxnp_gen_release(rxnp_gen *g);
double rxnp_gen_random(rxnp_gen *g);
double rxnp_gen_normal(rxnp_gen *g);
double rxnp_gen_exponential(rxnp_gen *g);
long  rxnp_gen_integer1(rxnp_gen *g, long low, long high);       /* [low, high) */
rxnp *rxnp_gen_random_arr(rxnp_gen *g, const char *shape);
rxnp *rxnp_gen_normal_arr(rxnp_gen *g, double loc, double scale, const char *shape);
rxnp *rxnp_gen_exponential_arr(rxnp_gen *g, double scale, const char *shape);
rxnp *rxnp_gen_integers(rxnp_gen *g, long low, long high, const char *shape);
int   rxnp_gen_shuffle(rxnp_gen *g, rxnp *a);                    /* in place, first axis */
rxnp *rxnp_gen_permutation(rxnp_gen *g, long n);
rxnp *rxnp_gen_choice(rxnp_gen *g, const rxnp *a, long size, int replace);

/* indexing / joining / comparison / n-d products (rxnp_idx.c).  Masks are int64 arrays (0 = false). */
void  rxnp_seterror(const char *msg);
enum { RXNP_GT, RXNP_LT, RXNP_GE, RXNP_LE, RXNP_EQ, RXNP_NE };
rxnp *rxnp_cmp(int op, const rxnp *a, const rxnp *b);            /* broadcasting; result int64 0/1 */
rxnp *rxnp_take(const rxnp *a, const rxnp *idx, int axis);       /* axis < -1: flattened (np.take default) */
rxnp *rxnp_where(const rxnp *c, const rxnp *a, const rxnp *b);
rxnp *rxnp_compress(const rxnp *mask, const rxnp *a, int axis);  /* np.compress */
rxnp *rxnp_boolean_get(const rxnp *a, const rxnp *mask);         /* a[mask] */
int   rxnp_boolean_set(rxnp *a, const rxnp *mask, const rxnp *values);   /* a[mask] = values (size 1 or count) */
rxnp *rxnp_unique(const rxnp *a, rxnp **counts);
rxnp *rxnp_stack(const rxnp *a, const rxnp *b, int axis);
rxnp *rxnp_vstack(const rxnp *a, const rxnp *b);
rxnp *rxnp_hstack(const rxnp *a, const rxnp *b);
rxnp *rxnp_matmul_nd(const rxnp *a, const rxnp *b);              /* np.matmul with batch broadcasting */
rxnp *rxnp_dot_nd(const rxnp *a, const rxnp *b);                 /* np.dot for any ndim */
rxnp *rxnp_tensordot(const rxnp *a, const rxnp *b, int naxes);   /* contract last naxes of a with first naxes of b */
#endif
