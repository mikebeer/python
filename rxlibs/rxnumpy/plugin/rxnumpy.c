/*
 * rxnumpy - CREXX native plugin exposing the rxnp C89 numeric core
 * (dense float64/int64 n-d arrays, linalg, fft, legacy MT19937 random).
 *
 * Arrays are opaque `.binary` handles: a native payload {magic, rxnp*}
 * with copy (retain) and finalize (release) callbacks.  Copies of a handle
 * share the array (reference semantics, like numpy); use np_copy for a deep
 * copy.  Errors are raised as CREXX signals (INVALID_ARGUMENTS / OUT_OF_RANGE).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "crexxpa.h"
#include "rxnp.h"

RXPA_PLUGIN_PROCESS_REENTRANT

#include "rxlibs_handle.h"
#define NP_TYPE "crexx.rxnumpy.array.v1"
static void np_retain_cb(void *o) { rxnp_retain((rxnp *)o); }
static void np_release_cb(void *o) { rxnp_release((rxnp *)o); }
static void np_copy_cb(void *dest, void *src);
static void np_fin_cb(void *v);
static const rxl_kind np_kind = { { NP_TYPE, np_copy_cb, np_fin_cb }, np_retain_cb, np_release_cb };

static rxnp_rng *g_rng = NULL;
static rxnp_rng *rng(void) { if (!g_rng) g_rng = rxnp_rng_new(); return g_rng; }

static rxl_payload *payload_of(rxpa_attribute_value v) { return rxl_get(v, NP_TYPE); }
static void np_copy_cb(void *dest, void *src)
{
    rxl_payload *p = payload_of(src), q;
    if (!p) return;
    q = *p; rxnp_retain((rxnp *)q.obj);
    if (SETNATIVEPAYLOAD(dest, &q, sizeof q, &np_kind.ops, 0u) != 0) rxnp_release((rxnp *)q.obj);
}
static void np_fin_cb(void *v)
{
    rxl_payload *p = payload_of(v);
    if (p) rxnp_release((rxnp *)p->obj);
}
/* publish array a (reference owned by caller transfers to the handle) */
static int publish(rxpa_attribute_value dest, rxnp *a) { return rxl_publish(dest, &np_kind, a); }

#define ARGCHK(n) if (NUM_ARGS != (n)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "wrong number of arguments")
#define NEED(i, var) { rxl_payload *_p = payload_of(ARG(i)); if (!_p) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "argument is not an rxnumpy array handle"); var = (rxnp *)_p->obj; }
#define RETARR(expr) { rxnp *_r = (expr); if (!_r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_error()); \
    if (!publish(RETURN, _r)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish array handle"); RESETSIGNAL }
#define RETARRLA(expr) { rxnp *_r = (expr); if (!_r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_la_error()); \
    if (!publish(RETURN, _r)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish array handle"); RESETSIGNAL }

/* ---- creation ---- */
PROCEDURE(f_array) { ARGCHK(1); RETARR(rxnp_from_text(GETSTRING(ARG0))) }
PROCEDURE(f_zeros) { ARGCHK(1); RETARR(rxnp_full(GETSTRING(ARG0), 0.0, RXNP_F64)) }
PROCEDURE(f_ones)  { ARGCHK(1); RETARR(rxnp_full(GETSTRING(ARG0), 1.0, RXNP_F64)) }
PROCEDURE(f_full)  { ARGCHK(2); RETARR(rxnp_full(GETSTRING(ARG0), GETFLOAT(ARG1), RXNP_F64)) }
PROCEDURE(f_zerosi){ ARGCHK(1); RETARR(rxnp_full(GETSTRING(ARG0), 0.0, RXNP_I64)) }
PROCEDURE(f_arange){ ARGCHK(3); RETARR(rxnp_arange((double)GETINT(ARG0), (double)GETINT(ARG1), (double)GETINT(ARG2), 1)) }
PROCEDURE(f_arangef){ ARGCHK(3); RETARR(rxnp_arange(GETFLOAT(ARG0), GETFLOAT(ARG1), GETFLOAT(ARG2), 0)) }
PROCEDURE(f_linspace){ ARGCHK(3); RETARR(rxnp_linspace(GETFLOAT(ARG0), GETFLOAT(ARG1), (long)GETINT(ARG2))) }
PROCEDURE(f_eye)   { ARGCHK(1); RETARR(rxnp_eye((long)GETINT(ARG0))) }
PROCEDURE(f_scalar){ ARGCHK(1); RETARR(rxnp_scalar(GETFLOAT(ARG0))) }
PROCEDURE(f_copy)  { rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_copy(a)) }
PROCEDURE(f_asint) { rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_astype(a, RXNP_I64)) }
PROCEDURE(f_asfloat){ rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_astype(a, RXNP_F64)) }

/* ---- introspection / access ---- */
PROCEDURE(f_ndim) { rxnp *a; ARGCHK(1); NEED(0, a); RETURNINT(a->ndim); RESETSIGNAL }
PROCEDURE(f_size) { rxnp *a; ARGCHK(1); NEED(0, a); RETURNINT(a->size); RESETSIGNAL }
PROCEDURE(f_dim)  { rxnp *a; long k; ARGCHK(2); NEED(0, a); k = (long)GETINT(ARG1);
    if (k < 0 || k >= a->ndim) RETURNSIGNAL(SIGNAL_OUT_OF_RANGE, "dimension index out of range")
    RETURNINT(a->shape[k]); RESETSIGNAL }
PROCEDURE(f_isint){ rxnp *a; ARGCHK(1); NEED(0, a); RETURNINT(a->dt == RXNP_I64); RESETSIGNAL }
PROCEDURE(f_shape){ rxnp *a; char buf[160]; int k, n = 0; ARGCHK(1); NEED(0, a); buf[0] = 0;
    for (k = 0; k < a->ndim; k++) n += sprintf(buf + n, "%s%ld", k ? "," : "", a->shape[k]);
    RETURNSTR(buf); RESETSIGNAL }
PROCEDURE(f_get) { rxnp *a; double v; ARGCHK(2); NEED(0, a);
    if (!rxnp_getf(a, (long)GETINT(ARG1), &v)) RETURNSIGNAL(SIGNAL_OUT_OF_RANGE, rxnp_error())
    RETURNFLOAT(v); RESETSIGNAL }
PROCEDURE(f_geti) { rxnp *a; double v; ARGCHK(2); NEED(0, a);
    if (!rxnp_getf(a, (long)GETINT(ARG1), &v)) RETURNSIGNAL(SIGNAL_OUT_OF_RANGE, rxnp_error())
    RETURNINT((rxinteger)v); RESETSIGNAL }
PROCEDURE(f_get2) { rxnp *a; double v; long i, j; ARGCHK(3); NEED(0, a);
    if (a->ndim != 2) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "np_get2 needs a 2-d array")
    i = (long)GETINT(ARG1); j = (long)GETINT(ARG2);
    if (i < 0) i += a->shape[0]; if (j < 0) j += a->shape[1];
    if (i < 0 || i >= a->shape[0] || j < 0 || j >= a->shape[1]) RETURNSIGNAL(SIGNAL_OUT_OF_RANGE, "index out of range")
    rxnp_getf(a, i * a->shape[1] + j, &v); RETURNFLOAT(v); RESETSIGNAL }
PROCEDURE(f_set) { rxnp *a; ARGCHK(3); NEED(0, a);
    if (!rxnp_setf(a, (long)GETINT(ARG1), GETFLOAT(ARG2))) RETURNSIGNAL(SIGNAL_OUT_OF_RANGE, rxnp_error())
    RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_set2) { rxnp *a; long i, j; ARGCHK(4); NEED(0, a);
    if (a->ndim != 2) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "np_set2 needs a 2-d array")
    i = (long)GETINT(ARG1); j = (long)GETINT(ARG2);
    if (i < 0) i += a->shape[0]; if (j < 0) j += a->shape[1];
    if (i < 0 || i >= a->shape[0] || j < 0 || j >= a->shape[1]) RETURNSIGNAL(SIGNAL_OUT_OF_RANGE, "index out of range")
    rxnp_setf(a, i * a->shape[1] + j, GETFLOAT(ARG3)); RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_tolist) { rxnp *a; char *t; ARGCHK(1); NEED(0, a); t = rxnp_to_text(a);
    if (!t) RETURNSIGNAL(SIGNAL_FAILURE, "out of memory")
    RETURNSTR(t); free(t); RESETSIGNAL }

/* ---- shape ---- */
PROCEDURE(f_reshape){ rxnp *a; ARGCHK(2); NEED(0, a); RETARR(rxnp_reshape(a, GETSTRING(ARG1))) }
PROCEDURE(f_transpose){ rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_transpose(a)) }
PROCEDURE(f_flatten){ rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_flatten(a)) }
PROCEDURE(f_slice){ rxnp *a; ARGCHK(5); NEED(0, a);
    RETARR(rxnp_slice(a, (int)GETINT(ARG1), (long)GETINT(ARG2), (long)GETINT(ARG3), (long)GETINT(ARG4))) }
PROCEDURE(f_concat){ rxnp *a, *b; ARGCHK(3); NEED(0, a); NEED(1, b); RETARR(rxnp_concat(a, b, (int)GETINT(ARG2))) }

/* ---- elementwise ---- */
#define BINOP(fn, op) PROCEDURE(fn) { rxnp *a, *b; ARGCHK(2); NEED(0, a); NEED(1, b); RETARR(rxnp_binary(op, a, b)) }
BINOP(f_add, RXNP_ADD) BINOP(f_sub, RXNP_SUB) BINOP(f_mul, RXNP_MUL) BINOP(f_div, RXNP_DIV)
BINOP(f_pow, RXNP_POW) BINOP(f_mod, RXNP_MOD) BINOP(f_floordiv, RXNP_FLOORDIV)
BINOP(f_maximum, RXNP_MAXIMUM) BINOP(f_minimum, RXNP_MINIMUM) BINOP(f_atan2, RXNP_ATAN2)
#define SBINOP(fn, op) PROCEDURE(fn) { rxnp *a, *s, *r; ARGCHK(2); NEED(0, a); s = rxnp_scalar(GETFLOAT(ARG1)); \
    if (!s) RETURNSIGNAL(SIGNAL_FAILURE, "out of memory") r = rxnp_binary(op, a, s); rxnp_release(s); RETARR(r) }
SBINOP(f_adds, RXNP_ADD) SBINOP(f_subs, RXNP_SUB) SBINOP(f_muls, RXNP_MUL) SBINOP(f_divs, RXNP_DIV)
SBINOP(f_pows, RXNP_POW) SBINOP(f_mods, RXNP_MOD)
#define UNOP(fn, op) PROCEDURE(fn) { rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_unary(op, a)) }
UNOP(f_neg, RXNP_NEG) UNOP(f_abs, RXNP_ABS) UNOP(f_sqrt, RXNP_SQRT) UNOP(f_exp, RXNP_EXP)
UNOP(f_log, RXNP_LOG) UNOP(f_sin, RXNP_SIN) UNOP(f_cos, RXNP_COS) UNOP(f_tan, RXNP_TAN)
UNOP(f_tanh, RXNP_TANH) UNOP(f_floor, RXNP_FLOOR) UNOP(f_ceil, RXNP_CEIL) UNOP(f_square, RXNP_SQUARE)
UNOP(f_round, RXNP_ROUND) UNOP(f_log10, RXNP_LOG10) UNOP(f_log2, RXNP_LOG2) UNOP(f_arctan, RXNP_ARCTAN)
UNOP(f_sign, RXNP_SIGN)
PROCEDURE(f_clip){ rxnp *a; ARGCHK(3); NEED(0, a); RETARR(rxnp_clip(a, GETFLOAT(ARG1), GETFLOAT(ARG2))) }

/* ---- reductions ---- */
#define REDALL(fn, op, ddof, isint) PROCEDURE(fn) { rxnp *a, *r; double v = 0; ARGCHK(1); NEED(0, a); \
    r = rxnp_reduce(op, a, 0, 1, ddof); if (!r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_error()) \
    rxnp_getf(r, 0, &v); rxnp_release(r); if (isint) { RETURNINT((rxinteger)v); } else { RETURNFLOAT(v); } RESETSIGNAL }
REDALL(f_sum, RXNP_SUM, 0, 0) REDALL(f_mean, RXNP_MEAN, 0, 0) REDALL(f_min, RXNP_MIN, 0, 0)
REDALL(f_max, RXNP_MAX, 0, 0) REDALL(f_prod, RXNP_PROD, 0, 0) REDALL(f_std, RXNP_STD, 0, 0)
REDALL(f_var, RXNP_VAR, 0, 0) REDALL(f_median, RXNP_MEDIAN, 0, 0)
REDALL(f_argmin, RXNP_ARGMIN, 0, 1) REDALL(f_argmax, RXNP_ARGMAX, 0, 1)
PROCEDURE(f_sumi) { rxnp *a, *r; double v = 0; ARGCHK(1); NEED(0, a);
    r = rxnp_reduce(RXNP_SUM, a, 0, 1, 0); if (!r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_error())
    if (r->dt == RXNP_I64) { RETURNINT(r->i[0]); } else { v = r->f[0]; RETURNINT((rxinteger)v); }
    rxnp_release(r); RESETSIGNAL }
PROCEDURE(f_stdd) { rxnp *a, *r; double v; ARGCHK(2); NEED(0, a);
    r = rxnp_reduce(RXNP_STD, a, 0, 1, (long)GETINT(ARG1)); if (!r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_error())
    rxnp_getf(r, 0, &v); rxnp_release(r); RETURNFLOAT(v); RESETSIGNAL }
PROCEDURE(f_vard) { rxnp *a, *r; double v; ARGCHK(2); NEED(0, a);
    r = rxnp_reduce(RXNP_VAR, a, 0, 1, (long)GETINT(ARG1)); if (!r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_error())
    rxnp_getf(r, 0, &v); rxnp_release(r); RETURNFLOAT(v); RESETSIGNAL }
#define REDAX(fn, op) PROCEDURE(fn) { rxnp *a; ARGCHK(2); NEED(0, a); RETARR(rxnp_reduce(op, a, (int)GETINT(ARG1), 0, 0)) }
REDAX(f_sumax, RXNP_SUM) REDAX(f_meanax, RXNP_MEAN) REDAX(f_minax, RXNP_MIN) REDAX(f_maxax, RXNP_MAX)
REDAX(f_prodax, RXNP_PROD) REDAX(f_stdax, RXNP_STD) REDAX(f_varax, RXNP_VAR) REDAX(f_medianax, RXNP_MEDIAN)
REDAX(f_argminax, RXNP_ARGMIN) REDAX(f_argmaxax, RXNP_ARGMAX) REDAX(f_cumsumax, RXNP_CUMSUM) REDAX(f_cumprodax, RXNP_CUMPROD)
PROCEDURE(f_cumsum){ rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_reduce(RXNP_CUMSUM, a, 0, 1, 0)) }
PROCEDURE(f_cumprod){ rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_reduce(RXNP_CUMPROD, a, 0, 1, 0)) }
PROCEDURE(f_sort){ rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_sort(a)) }
PROCEDURE(f_argsort){ rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_argsort(a)) }

/* ---- products / linalg ---- */
PROCEDURE(f_dot){ rxnp *a, *b; ARGCHK(2); NEED(0, a); NEED(1, b);
    if (a->ndim > 2 || b->ndim > 2) { RETARR(rxnp_dot_nd(a, b)) } else { RETARR(rxnp_matmul(a, b)) } }
PROCEDURE(f_outer){ rxnp *a, *b; ARGCHK(2); NEED(0, a); NEED(1, b); RETARR(rxnp_outer(a, b)) }
PROCEDURE(f_trace){ rxnp *a; ARGCHK(1); NEED(0, a); RETURNFLOAT(rxnp_trace(a)); RESETSIGNAL }
PROCEDURE(f_norm){ rxnp *a; ARGCHK(1); NEED(0, a); RETURNFLOAT(rxnp_norm(a)); RESETSIGNAL }
PROCEDURE(f_solve){ rxnp *a, *b; ARGCHK(2); NEED(0, a); NEED(1, b); RETARRLA(rxnp_solve(a, b)) }
PROCEDURE(f_inv){ rxnp *a; ARGCHK(1); NEED(0, a); RETARRLA(rxnp_inv(a)) }
PROCEDURE(f_det){ rxnp *a; double d; ARGCHK(1); NEED(0, a);
    if (!rxnp_det(a, &d)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_la_error())
    RETURNFLOAT(d); RESETSIGNAL }
PROCEDURE(f_cholesky){ rxnp *a; ARGCHK(1); NEED(0, a); RETARRLA(rxnp_cholesky(a)) }
PROCEDURE(f_eigvals){ rxnp *a; ARGCHK(1); NEED(0, a); RETARRLA(rxnp_eigvals(a)) }
PROCEDURE(f_qr){ rxnp *a, *q, *r; ARGCHK(3); NEED(0, a);
    if (!rxnp_qr(a, &q, &r)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_la_error())
    if (!publish(ARG1, q) || !publish(ARG2, r)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish array handle")
    RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_eigh){ rxnp *a, *w, *v; ARGCHK(3); NEED(0, a);
    if (!rxnp_eigh(a, &w, &v)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_la_error())
    if (!publish(ARG1, w) || !publish(ARG2, v)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish array handle")
    RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_svd){ rxnp *a, *u, *s, *vt; ARGCHK(4); NEED(0, a);
    if (!rxnp_svd(a, &u, &s, &vt)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_la_error())
    if (!publish(ARG1, u) || !publish(ARG2, s) || !publish(ARG3, vt)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish array handle")
    RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_fft){ rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_fft(a, 0)) }
PROCEDURE(f_ifft){ rxnp *a; ARGCHK(1); NEED(0, a); RETARR(rxnp_fft(a, 1)) }

/* ---- random ---- */
PROCEDURE(f_seed){ ARGCHK(1); rxnp_rng_seed(rng(), (unsigned long)GETINT(ARG0)); RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_rand){ ARGCHK(1); RETARR(rxnp_rand(rng(), GETSTRING(ARG0))) }
PROCEDURE(f_randn){ ARGCHK(1); RETARR(rxnp_randn(rng(), GETSTRING(ARG0))) }
PROCEDURE(f_randint){ ARGCHK(3); RETARR(rxnp_randint(rng(), (long)GETINT(ARG0), (long)GETINT(ARG1), GETSTRING(ARG2))) }
PROCEDURE(f_random1){ ARGCHK(0); RETURNFLOAT(rxnp_rng_random(rng())); RESETSIGNAL }
PROCEDURE(f_randn1){ ARGCHK(0); RETURNFLOAT(rxnp_rng_gauss(rng())); RESETSIGNAL }


/* ---- Generator handle (default_rng) ---- */
#define GEN_TYPE "crexx.rxnumpy.rng.v1"
static void gen_retain_cb(void *o) { rxnp_gen_retain((rxnp_gen *)o); }
static void gen_release_cb(void *o) { rxnp_gen_release((rxnp_gen *)o); }
static void gen_copy_cb(void *dest, void *src);
static void gen_fin_cb(void *v);
static const rxl_kind gen_kind = { { GEN_TYPE, gen_copy_cb, gen_fin_cb }, gen_retain_cb, gen_release_cb };
static void gen_copy_cb(void *dest, void *src)
{
    rxl_payload *p = rxl_get(src, GEN_TYPE), q;
    if (!p) return;
    q = *p; rxnp_gen_retain((rxnp_gen *)q.obj);
    if (SETNATIVEPAYLOAD(dest, &q, sizeof q, &gen_kind.ops, 0u) != 0) rxnp_gen_release((rxnp_gen *)q.obj);
}
static void gen_fin_cb(void *v) { rxl_payload *p = rxl_get(v, GEN_TYPE); if (p) rxnp_gen_release((rxnp_gen *)p->obj); }
#define NEEDG(i, var) { rxl_payload *_p = rxl_get(ARG(i), GEN_TYPE); if (!_p) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "argument is not an rxnumpy Generator handle"); var = (rxnp_gen *)_p->obj; }
PROCEDURE(f_default_rng) { rxnp_gen *g; ARGCHK(1); if (GETINT(ARG0) < 0) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "expected non-negative integer")
    g = rxnp_gen_new((unsigned long)GETINT(ARG0)); if (!g) RETURNSIGNAL(SIGNAL_FAILURE, "out of memory")
    if (!rxl_publish(RETURN, &gen_kind, g)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish generator handle") RESETSIGNAL }
PROCEDURE(f_default_rng_arr) { rxnp *a; rxnp_gen *g; ARGCHK(1); NEED(0, a); g = rxnp_gen_new_array(a);
    if (!g) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "seed must be a non-empty int array")
    if (!rxl_publish(RETURN, &gen_kind, g)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish generator handle") RESETSIGNAL }
PROCEDURE(f_rng_random) { rxnp_gen *g; ARGCHK(2); NEEDG(0, g); RETARR(rxnp_gen_random_arr(g, GETSTRING(ARG1))) }
PROCEDURE(f_rng_random1) { rxnp_gen *g; ARGCHK(1); NEEDG(0, g); RETURNFLOAT(rxnp_gen_random(g)); RESETSIGNAL }
PROCEDURE(f_rng_normal) { rxnp_gen *g; ARGCHK(4); NEEDG(0, g); RETARR(rxnp_gen_normal_arr(g, GETFLOAT(ARG1), GETFLOAT(ARG2), GETSTRING(ARG3))) }
PROCEDURE(f_rng_normal1) { rxnp_gen *g; ARGCHK(1); NEEDG(0, g); RETURNFLOAT(rxnp_gen_normal(g)); RESETSIGNAL }
PROCEDURE(f_rng_exponential) { rxnp_gen *g; ARGCHK(3); NEEDG(0, g); RETARR(rxnp_gen_exponential_arr(g, GETFLOAT(ARG1), GETSTRING(ARG2))) }
PROCEDURE(f_rng_integers) { rxnp_gen *g; ARGCHK(4); NEEDG(0, g);
    RETARR(rxnp_gen_integers(g, (long)GETINT(ARG1), (long)GETINT(ARG2), GETSTRING(ARG3))) }
PROCEDURE(f_rng_integer1) { rxnp_gen *g; long lo, hi; ARGCHK(3); NEEDG(0, g); lo = (long)GETINT(ARG1); hi = (long)GETINT(ARG2);
    if (lo >= hi) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "low >= high")
    RETURNINT(rxnp_gen_integer1(g, lo, hi)); RESETSIGNAL }
PROCEDURE(f_rng_choice) { rxnp_gen *g; rxnp *a; ARGCHK(4); NEEDG(0, g); NEED(1, a);
    RETARR(rxnp_gen_choice(g, a, (long)GETINT(ARG2), (int)GETINT(ARG3) != 0)) }
PROCEDURE(f_rng_shuffle) { rxnp_gen *g; rxnp *a; ARGCHK(2); NEEDG(0, g); NEED(1, a);
    if (!rxnp_gen_shuffle(g, a)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "shuffle: 0-d array") RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_rng_permutation) { rxnp_gen *g; ARGCHK(2); NEEDG(0, g); RETARR(rxnp_gen_permutation(g, (long)GETINT(ARG1))) }

/* ---- comparison / indexing / joining / n-d products ---- */
static int cmp_op(const char *s)
{
    if (!strcmp(s, ">")) return RXNP_GT; if (!strcmp(s, "<")) return RXNP_LT; if (!strcmp(s, ">=")) return RXNP_GE;
    if (!strcmp(s, "<=")) return RXNP_LE; if (!strcmp(s, "==")) return RXNP_EQ; if (!strcmp(s, "!=")) return RXNP_NE; return -1;
}
PROCEDURE(f_cmp) { rxnp *a, *b; int op; ARGCHK(3); NEED(0, a); NEED(1, b); op = cmp_op(GETSTRING(ARG2));
    if (op < 0) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "comparison operator must be one of > < >= <= == !=")
    RETARR(rxnp_cmp(op, a, b)) }
PROCEDURE(f_cmps) { rxnp *a, *s, *r; int op; ARGCHK(3); NEED(0, a); op = cmp_op(GETSTRING(ARG2));
    if (op < 0) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "comparison operator must be one of > < >= <= == !=")
    s = rxnp_scalar(GETFLOAT(ARG1)); if (!s) RETURNSIGNAL(SIGNAL_FAILURE, "out of memory") r = rxnp_cmp(op, a, s); rxnp_release(s); RETARR(r) }
PROCEDURE(f_take) { rxnp *a, *i; ARGCHK(3); NEED(0, a); NEED(1, i); RETARR(rxnp_take(a, i, (int)GETINT(ARG2))) }
PROCEDURE(f_takeflat) { rxnp *a, *i; ARGCHK(2); NEED(0, a); NEED(1, i); RETARR(rxnp_take(a, i, -2000)) }
PROCEDURE(f_where) { rxnp *c, *a, *b; ARGCHK(3); NEED(0, c); NEED(1, a); NEED(2, b); RETARR(rxnp_where(c, a, b)) }
PROCEDURE(f_compress) { rxnp *m, *a; ARGCHK(3); NEED(0, m); NEED(1, a); RETARR(rxnp_compress(m, a, (int)GETINT(ARG2))) }
PROCEDURE(f_bget) { rxnp *a, *m; ARGCHK(2); NEED(0, a); NEED(1, m); RETARR(rxnp_boolean_get(a, m)) }
PROCEDURE(f_bset) { rxnp *a, *m, *v; ARGCHK(3); NEED(0, a); NEED(1, m); v = rxnp_scalar(GETFLOAT(ARG2));
    if (!v) RETURNSIGNAL(SIGNAL_FAILURE, "out of memory")
    if (!rxnp_boolean_set(a, m, v)) { rxnp_release(v); RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_error()) }
    rxnp_release(v); RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_bsetarr) { rxnp *a, *m, *v; ARGCHK(3); NEED(0, a); NEED(1, m); NEED(2, v);
    if (!rxnp_boolean_set(a, m, v)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxnp_error()) RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_unique) { rxnp *a, *c = NULL, *r; ARGCHK(1); NEED(0, a); r = rxnp_unique(a, &c); rxnp_release(c); RETARR(r) }
PROCEDURE(f_uniquec) { rxnp *a, *c = NULL, *r; ARGCHK(1); NEED(0, a); r = rxnp_unique(a, &c); rxnp_release(r); RETARR(c) }
PROCEDURE(f_stack) { rxnp *a, *b; ARGCHK(3); NEED(0, a); NEED(1, b); RETARR(rxnp_stack(a, b, (int)GETINT(ARG2))) }
PROCEDURE(f_vstack) { rxnp *a, *b; ARGCHK(2); NEED(0, a); NEED(1, b); RETARR(rxnp_vstack(a, b)) }
PROCEDURE(f_hstack) { rxnp *a, *b; ARGCHK(2); NEED(0, a); NEED(1, b); RETARR(rxnp_hstack(a, b)) }
PROCEDURE(f_matmul) { rxnp *a, *b; ARGCHK(2); NEED(0, a); NEED(1, b); RETARR(rxnp_matmul_nd(a, b)) }
PROCEDURE(f_tensordot) { rxnp *a, *b; ARGCHK(3); NEED(0, a); NEED(1, b); RETARR(rxnp_tensordot(a, b, (int)GETINT(ARG2))) }

#define A1 "a = .binary"
#define A2 "a = .binary, b = .binary"
#define P(f, name, ret, args) ADDPROC(f, "rxnumpy." name, "b", ret, args);
LOADFUNCS
P(f_array,"np_array",".binary","text = .string")
P(f_zeros,"np_zeros",".binary","shape = .string") P(f_ones,"np_ones",".binary","shape = .string")
P(f_full,"np_full",".binary","shape = .string, value = .float") P(f_zerosi,"np_zeros_int",".binary","shape = .string")
P(f_arange,"np_arange",".binary","start = .int, stop = .int, step = .int")
P(f_arangef,"np_arangef",".binary","start = .float, stop = .float, step = .float")
P(f_linspace,"np_linspace",".binary","start = .float, stop = .float, num = .int")
P(f_eye,"np_eye",".binary","n = .int") P(f_scalar,"np_scalar",".binary","value = .float")
P(f_copy,"np_copy",".binary",A1) P(f_asint,"np_asint",".binary",A1) P(f_asfloat,"np_asfloat",".binary",A1)
P(f_ndim,"np_ndim",".int",A1) P(f_size,"np_size",".int",A1) P(f_dim,"np_dim",".int","a = .binary, axis = .int")
P(f_isint,"np_isint",".int",A1) P(f_shape,"np_shape",".string",A1)
P(f_get,"np_get",".float","a = .binary, index = .int") P(f_geti,"np_geti",".int","a = .binary, index = .int")
P(f_get2,"np_get2",".float","a = .binary, i = .int, j = .int")
P(f_set,"np_set",".int","a = .binary, index = .int, value = .float")
P(f_set2,"np_set2",".int","a = .binary, i = .int, j = .int, value = .float")
P(f_tolist,"np_tolist",".string",A1)
P(f_reshape,"np_reshape",".binary","a = .binary, shape = .string")
P(f_transpose,"np_transpose",".binary",A1) P(f_flatten,"np_flatten",".binary",A1)
P(f_slice,"np_slice",".binary","a = .binary, axis = .int, start = .int, stop = .int, step = .int")
P(f_concat,"np_concat",".binary","a = .binary, b = .binary, axis = .int")
P(f_add,"np_add",".binary",A2) P(f_sub,"np_sub",".binary",A2) P(f_mul,"np_mul",".binary",A2) P(f_div,"np_div",".binary",A2)
P(f_pow,"np_pow",".binary",A2) P(f_mod,"np_mod",".binary",A2) P(f_floordiv,"np_floordiv",".binary",A2)
P(f_maximum,"np_maximum",".binary",A2) P(f_minimum,"np_minimum",".binary",A2) P(f_atan2,"np_arctan2",".binary",A2)
P(f_adds,"np_adds",".binary","a = .binary, s = .float") P(f_subs,"np_subs",".binary","a = .binary, s = .float")
P(f_muls,"np_muls",".binary","a = .binary, s = .float") P(f_divs,"np_divs",".binary","a = .binary, s = .float")
P(f_pows,"np_pows",".binary","a = .binary, s = .float") P(f_mods,"np_mods",".binary","a = .binary, s = .float")
P(f_neg,"np_neg",".binary",A1) P(f_abs,"np_abs",".binary",A1) P(f_sqrt,"np_sqrt",".binary",A1) P(f_exp,"np_exp",".binary",A1)
P(f_log,"np_log",".binary",A1) P(f_sin,"np_sin",".binary",A1) P(f_cos,"np_cos",".binary",A1) P(f_tan,"np_tan",".binary",A1)
P(f_tanh,"np_tanh",".binary",A1) P(f_floor,"np_floor",".binary",A1) P(f_ceil,"np_ceil",".binary",A1)
P(f_square,"np_square",".binary",A1) P(f_round,"np_round",".binary",A1) P(f_log10,"np_log10",".binary",A1)
P(f_log2,"np_log2",".binary",A1) P(f_arctan,"np_arctan",".binary",A1) P(f_sign,"np_sign",".binary",A1)
P(f_clip,"np_clip",".binary","a = .binary, lo = .float, hi = .float")
P(f_sum,"np_sum",".float",A1) P(f_sumi,"np_sumi",".int",A1) P(f_mean,"np_mean",".float",A1)
P(f_min,"np_min",".float",A1) P(f_max,"np_max",".float",A1) P(f_prod,"np_prod",".float",A1)
P(f_std,"np_std",".float",A1) P(f_var,"np_var",".float",A1) P(f_median,"np_median",".float",A1)
P(f_stdd,"np_std_ddof",".float","a = .binary, ddof = .int") P(f_vard,"np_var_ddof",".float","a = .binary, ddof = .int")
P(f_argmin,"np_argmin",".int",A1) P(f_argmax,"np_argmax",".int",A1)
P(f_sumax,"np_sum_axis",".binary","a = .binary, axis = .int") P(f_meanax,"np_mean_axis",".binary","a = .binary, axis = .int")
P(f_minax,"np_min_axis",".binary","a = .binary, axis = .int") P(f_maxax,"np_max_axis",".binary","a = .binary, axis = .int")
P(f_prodax,"np_prod_axis",".binary","a = .binary, axis = .int") P(f_stdax,"np_std_axis",".binary","a = .binary, axis = .int")
P(f_varax,"np_var_axis",".binary","a = .binary, axis = .int") P(f_medianax,"np_median_axis",".binary","a = .binary, axis = .int")
P(f_argminax,"np_argmin_axis",".binary","a = .binary, axis = .int") P(f_argmaxax,"np_argmax_axis",".binary","a = .binary, axis = .int")
P(f_cumsumax,"np_cumsum_axis",".binary","a = .binary, axis = .int") P(f_cumprodax,"np_cumprod_axis",".binary","a = .binary, axis = .int")
P(f_cumsum,"np_cumsum",".binary",A1) P(f_cumprod,"np_cumprod",".binary",A1)
P(f_sort,"np_sort",".binary",A1) P(f_argsort,"np_argsort",".binary",A1)
P(f_dot,"np_dot",".binary",A2) P(f_outer,"np_outer",".binary",A2) P(f_trace,"np_trace",".float",A1) P(f_norm,"np_norm",".float",A1)
P(f_solve,"np_solve",".binary",A2) P(f_inv,"np_inv",".binary",A1) P(f_det,"np_det",".float",A1)
P(f_cholesky,"np_cholesky",".binary",A1) P(f_eigvals,"np_eigvals",".binary",A1)
P(f_qr,"np_qr",".int","a = .binary, expose q = .binary, expose r = .binary")
P(f_eigh,"np_eigh",".int","a = .binary, expose w = .binary, expose v = .binary")
P(f_svd,"np_svd",".int","a = .binary, expose u = .binary, expose s = .binary, expose vt = .binary")
P(f_fft,"np_fft",".binary",A1) P(f_ifft,"np_ifft",".binary",A1)
P(f_seed,"np_rand_seed",".int","seed = .int") P(f_rand,"np_rand",".binary","shape = .string")
P(f_randn,"np_randn",".binary","shape = .string")
P(f_randint,"np_randint",".binary","low = .int, high = .int, shape = .string")
P(f_random1,"np_random",".float","") P(f_randn1,"np_gauss",".float","")
P(f_default_rng,"np_default_rng",".binary","seed = .int") P(f_default_rng_arr,"np_default_rng_arr",".binary","seed = .binary")
P(f_rng_random,"np_rng_random",".binary","rng = .binary, shape = .string") P(f_rng_random1,"np_rng_random1",".float","rng = .binary")
P(f_rng_normal,"np_rng_normal",".binary","rng = .binary, loc = .float, scale = .float, shape = .string")
P(f_rng_normal1,"np_rng_normal1",".float","rng = .binary")
P(f_rng_exponential,"np_rng_exponential",".binary","rng = .binary, scale = .float, shape = .string")
P(f_rng_integers,"np_rng_integers",".binary","rng = .binary, low = .int, high = .int, shape = .string")
P(f_rng_integer1,"np_rng_integer1",".int","rng = .binary, low = .int, high = .int")
P(f_rng_choice,"np_rng_choice",".binary","rng = .binary, a = .binary, size = .int, replace = .int")
P(f_rng_shuffle,"np_rng_shuffle",".int","rng = .binary, a = .binary") P(f_rng_permutation,"np_rng_permutation",".binary","rng = .binary, n = .int")
P(f_cmp,"np_cmp",".binary","a = .binary, b = .binary, op = .string") P(f_cmps,"np_cmps",".binary","a = .binary, s = .float, op = .string")
P(f_take,"np_take",".binary","a = .binary, idx = .binary, axis = .int") P(f_takeflat,"np_take_flat",".binary","a = .binary, idx = .binary")
P(f_where,"np_where",".binary","cond = .binary, a = .binary, b = .binary")
P(f_compress,"np_compress",".binary","mask = .binary, a = .binary, axis = .int")
P(f_bget,"np_boolean_get",".binary","a = .binary, mask = .binary")
P(f_bset,"np_boolean_set",".int","a = .binary, mask = .binary, value = .float")
P(f_bsetarr,"np_boolean_set_arr",".int","a = .binary, mask = .binary, values = .binary")
P(f_unique,"np_unique",".binary",A1) P(f_uniquec,"np_unique_counts",".binary",A1)
P(f_stack,"np_stack",".binary","a = .binary, b = .binary, axis = .int") P(f_vstack,"np_vstack",".binary",A2) P(f_hstack,"np_hstack",".binary",A2)
P(f_concat,"np_concatenate",".binary","a = .binary, b = .binary, axis = .int")
P(f_matmul,"np_matmul",".binary",A2) P(f_tensordot,"np_tensordot",".binary","a = .binary, b = .binary, naxes = .int")
ENDLOADFUNCS
