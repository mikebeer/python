/*
 * rxpandas - CREXX native plugin: a small DataFrame-like table (rxdf) behind opaque handles.
 * Numeric columns are exchanged with rxnumpy through rxnumpy array handles.
 */
#include <stdlib.h>
#include <string.h>
#include "crexxpa.h"
#include "rxdf.h"

RXPA_PLUGIN_PROCESS_REENTRANT
#include "rxlibs_handle.h"
#define NP_TYPE "crexx.rxnumpy.array.v1"
#define DF_TYPE "crexx.rxpandas.frame.v1"
static void np_retain_cb(void *o) { rxnp_retain((rxnp *)o); }
static void np_release_cb(void *o) { rxnp_release((rxnp *)o); }
static void np_copy_cb(void *dest, void *src);
static void np_fin_cb(void *v);
static const rxl_kind np_kind = { { NP_TYPE, np_copy_cb, np_fin_cb }, np_retain_cb, np_release_cb };
static void np_copy_cb(void *dest, void *src)
{
    rxl_payload *p = rxl_get(src, NP_TYPE), q;
    if (!p) return;
    q = *p; rxnp_retain((rxnp *)q.obj);
    if (SETNATIVEPAYLOAD(dest, &q, sizeof q, &np_kind.ops, 0u) != 0) rxnp_release((rxnp *)q.obj);
}
static void np_fin_cb(void *v) { rxl_payload *p = rxl_get(v, NP_TYPE); if (p) rxnp_release((rxnp *)p->obj); }

static void df_retain_cb(void *o) { rxdf_retain((rxdf *)o); }
static void df_release_cb(void *o) { rxdf_release((rxdf *)o); }
static void df_copy_cb(void *dest, void *src);
static void df_fin_cb(void *v);
static const rxl_kind df_kind = { { DF_TYPE, df_copy_cb, df_fin_cb }, df_retain_cb, df_release_cb };
static void df_copy_cb(void *dest, void *src)
{
    rxl_payload *p = rxl_get(src, DF_TYPE), q;
    if (!p) return;
    q = *p; rxdf_retain((rxdf *)q.obj);
    if (SETNATIVEPAYLOAD(dest, &q, sizeof q, &df_kind.ops, 0u) != 0) rxdf_release((rxdf *)q.obj);
}
static void df_fin_cb(void *v) { rxl_payload *p = rxl_get(v, DF_TYPE); if (p) rxdf_release((rxdf *)p->obj); }

#define ARGCHK(n) if (NUM_ARGS != (n)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "wrong number of arguments")
#define NEEDDF(i, var) { rxl_payload *_p = rxl_get(ARG(i), DF_TYPE); if (!_p) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "argument is not an rxpandas table handle"); var = (rxdf *)_p->obj; }
#define NEEDNP(i, var) { rxl_payload *_p = rxl_get(ARG(i), NP_TYPE); if (!_p) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "argument is not an rxnumpy array handle"); var = (rxnp *)_p->obj; }
#define RETDF(expr) { rxdf *_r = (expr); if (!_r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxdf_error()); \
    if (!rxl_publish(RETURN, &df_kind, _r)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish table handle"); RESETSIGNAL }

PROCEDURE(f_new) { rxdf *d = rxdf_new(); ARGCHK(0); if (!d) RETURNSIGNAL(SIGNAL_FAILURE, "out of memory") if (!rxl_publish(RETURN, &df_kind, d)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish table handle") RESETSIGNAL }
PROCEDURE(f_read_csv) { ARGCHK(1); RETDF(rxdf_read_csv(GETSTRING(ARG0))) }
PROCEDURE(f_parse_csv) { ARGCHK(1); RETDF(rxdf_parse_csv(GETSTRING(ARG0))) }
PROCEDURE(f_to_csv) { rxdf *d; ARGCHK(2); NEEDDF(0, d); if (!rxdf_to_csv(d, GETSTRING(ARG1))) RETURNSIGNAL(SIGNAL_FAILURE, rxdf_error()) RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_add_col) { rxdf *d; rxnp *a; ARGCHK(3); NEEDDF(0, d); NEEDNP(2, a);
    if (!rxdf_add_num(d, GETSTRING(ARG1), a)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxdf_error()) RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_add_str) { rxdf *d; ARGCHK(3); NEEDDF(0, d);
    if (!rxdf_add_str(d, GETSTRING(ARG1), GETSTRING(ARG2))) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxdf_error()) RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_to_string) { rxdf *d; char *t; ARGCHK(2); NEEDDF(0, d); t = rxdf_to_text(d, (long)GETINT(ARG1));
    if (!t) RETURNSIGNAL(SIGNAL_FAILURE, "out of memory") RETURNSTR(t); free(t); RESETSIGNAL }
PROCEDURE(f_nrows) { rxdf *d; ARGCHK(1); NEEDDF(0, d); RETURNINT(d->nrows); RESETSIGNAL }
PROCEDURE(f_ncols) { rxdf *d; ARGCHK(1); NEEDDF(0, d); RETURNINT(d->ncols); RESETSIGNAL }
PROCEDURE(f_colname) { rxdf *d; long i; ARGCHK(2); NEEDDF(0, d); i = (long)GETINT(ARG1);
    if (i < 0 || i >= d->ncols) RETURNSIGNAL(SIGNAL_OUT_OF_RANGE, "column index out of range") RETURNSTR(d->names[i]); RESETSIGNAL }
PROCEDURE(f_isstr) { rxdf *d; long c; ARGCHK(2); NEEDDF(0, d); c = rxdf_find(d, GETSTRING(ARG1));
    if (c < 0) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "no such column") RETURNINT(d->isstr[c]); RESETSIGNAL }
PROCEDURE(f_col) { rxdf *d; rxnp *a; ARGCHK(2); NEEDDF(0, d); a = rxdf_col(d, GETSTRING(ARG1));
    if (!a) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxdf_error())
    if (!rxl_publish(RETURN, &np_kind, a)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish array handle") RESETSIGNAL }
PROCEDURE(f_get) { rxdf *d; long c, r; ARGCHK(3); NEEDDF(0, d); c = rxdf_find(d, GETSTRING(ARG1)); r = (long)GETINT(ARG2);
    if (c < 0 || d->isstr[c]) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "no such numeric column")
    if (r < 0 || r >= d->nrows) RETURNSIGNAL(SIGNAL_OUT_OF_RANGE, "row out of range")
    RETURNFLOAT(d->num[c][r]); RESETSIGNAL }
PROCEDURE(f_gets) { rxdf *d; long c, r; ARGCHK(3); NEEDDF(0, d); c = rxdf_find(d, GETSTRING(ARG1)); r = (long)GETINT(ARG2);
    if (c < 0 || !d->isstr[c]) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "no such string column")
    if (r < 0 || r >= d->nrows) RETURNSIGNAL(SIGNAL_OUT_OF_RANGE, "row out of range")
    RETURNSTR(d->str[c][r]); RESETSIGNAL }
PROCEDURE(f_filter) { rxdf *d; ARGCHK(4); NEEDDF(0, d); RETDF(rxdf_filter(d, GETSTRING(ARG1), GETSTRING(ARG2), GETSTRING(ARG3))) }
PROCEDURE(f_sort) { rxdf *d; ARGCHK(3); NEEDDF(0, d); RETDF(rxdf_sort(d, GETSTRING(ARG1), (int)GETINT(ARG2))) }
PROCEDURE(f_head) { rxdf *d; ARGCHK(2); NEEDDF(0, d); RETDF(rxdf_head(d, (long)GETINT(ARG1))) }
PROCEDURE(f_select) { rxdf *d; ARGCHK(2); NEEDDF(0, d); RETDF(rxdf_select(d, GETSTRING(ARG1))) }
PROCEDURE(f_groupby) { rxdf *d; ARGCHK(4); NEEDDF(0, d); RETDF(rxdf_groupby(d, GETSTRING(ARG1), GETSTRING(ARG2), GETSTRING(ARG3))) }
PROCEDURE(f_merge) { rxdf *a, *b; ARGCHK(4); NEEDDF(0, a); NEEDDF(1, b); RETDF(rxdf_merge(a, b, GETSTRING(ARG2), GETSTRING(ARG3))) }
PROCEDURE(f_describe) { rxdf *d; ARGCHK(1); NEEDDF(0, d); RETDF(rxdf_describe(d)) }

#define P(f, name, ret, args) ADDPROC(f, "rxpandas." name, "b", ret, args);
LOADFUNCS
P(f_new,"pd_new",".binary","") P(f_read_csv,"pd_read_csv",".binary","path = .string")
P(f_parse_csv,"pd_parse_csv",".binary","text = .string") P(f_to_csv,"pd_to_csv",".int","df = .binary, path = .string")
P(f_add_col,"pd_add_col",".int","df = .binary, name = .string, values = .binary")
P(f_add_str,"pd_add_str_col",".int","df = .binary, name = .string, csv = .string")
P(f_to_string,"pd_to_string",".string","df = .binary, maxrows = .int")
P(f_nrows,"pd_nrows",".int","df = .binary") P(f_ncols,"pd_ncols",".int","df = .binary")
P(f_colname,"pd_colname",".string","df = .binary, i = .int") P(f_isstr,"pd_isstr",".int","df = .binary, name = .string")
P(f_col,"pd_col",".binary","df = .binary, name = .string")
P(f_get,"pd_get",".float","df = .binary, name = .string, row = .int")
P(f_gets,"pd_gets",".string","df = .binary, name = .string, row = .int")
P(f_filter,"pd_filter",".binary","df = .binary, col = .string, op = .string, value = .string")
P(f_sort,"pd_sort",".binary","df = .binary, col = .string, ascending = .int")
P(f_head,"pd_head",".binary","df = .binary, n = .int") P(f_select,"pd_select",".binary","df = .binary, names = .string")
P(f_groupby,"pd_groupby",".binary","df = .binary, key = .string, val = .string, agg = .string")
P(f_merge,"pd_merge",".binary","a = .binary, b = .binary, key = .string, how = .string")
P(f_describe,"pd_describe",".binary","df = .binary")
ENDLOADFUNCS
