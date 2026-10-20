/*
 * rxscipy - CREXX native plugin: scipy-style numerics on rxnumpy array handles.
 * Accepts handles created by the rxnumpy plugin (matched by payload type_name) and returns
 * rxnumpy-compatible handles.  Scalars in/out use .float.
 */
#include <stdlib.h>
#include <string.h>
#include "crexxpa.h"
#include "rxsp.h"

RXPA_PLUGIN_PROCESS_REENTRANT
#include "rxlibs_handle.h"
#define NP_TYPE "crexx.rxnumpy.array.v1"
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
static int publish(rxpa_attribute_value dest, rxnp *a) { return rxl_publish(dest, &np_kind, a); }

#define ARGCHK(n) if (NUM_ARGS != (n)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "wrong number of arguments")
#define NEED(i, var) { rxl_payload *_p = rxl_get(ARG(i), NP_TYPE); if (!_p) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "argument is not an rxnumpy array handle"); var = (rxnp *)_p->obj; }
#define RETARR(expr) { rxnp *_r = (expr); if (!_r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error()); \
    if (!publish(RETURN, _r)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish array handle"); RESETSIGNAL }

#define SCALAR1(fn, expr) PROCEDURE(fn) { double x; ARGCHK(1); x = GETFLOAT(ARG0); RETURNFLOAT(expr); RESETSIGNAL }
SCALAR1(f_erf, rxsp_erf(x)) SCALAR1(f_erfc, rxsp_erfc(x)) SCALAR1(f_gamma, rxsp_gamma(x))
SCALAR1(f_lgamma, rxsp_lgamma(x)) SCALAR1(f_normpdf, rxsp_norm_pdf(x)) SCALAR1(f_normcdf, rxsp_norm_cdf(x))
SCALAR1(f_normppf, rxsp_norm_ppf(x))
PROCEDURE(f_tcdf) { ARGCHK(2); RETURNFLOAT(rxsp_t_cdf(GETFLOAT(ARG0), GETFLOAT(ARG1))); RESETSIGNAL }
PROCEDURE(f_betainc) { ARGCHK(3); RETURNFLOAT(rxsp_betainc(GETFLOAT(ARG0), GETFLOAT(ARG1), GETFLOAT(ARG2))); RESETSIGNAL }
PROCEDURE(f_trapz) { rxnp *y, *x, *r; double v; ARGCHK(2); NEED(0, y); NEED(1, x);
    r = rxsp_trapz(y, x); if (!r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    v = r->f[0]; rxnp_release(r); RETURNFLOAT(v); RESETSIGNAL }
PROCEDURE(f_trapz1) { rxnp *y, *r; double v; ARGCHK(1); NEED(0, y);
    r = rxsp_trapz(y, NULL); if (!r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    v = r->f[0]; rxnp_release(r); RETURNFLOAT(v); RESETSIGNAL }
PROCEDURE(f_simpson) { rxnp *y, *x, *r; double v; ARGCHK(2); NEED(0, y); NEED(1, x);
    r = rxsp_simpson(y, x); if (!r) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    v = r->f[0]; rxnp_release(r); RETURNFLOAT(v); RESETSIGNAL }
PROCEDURE(f_interp) { rxnp *x, *xp, *fp; ARGCHK(3); NEED(0, x); NEED(1, xp); NEED(2, fp); RETARR(rxsp_interp(x, xp, fp)) }
PROCEDURE(f_polyfit) { rxnp *x, *y; ARGCHK(3); NEED(0, x); NEED(1, y); RETARR(rxsp_polyfit(x, y, (int)GETINT(ARG2))) }
PROCEDURE(f_polyval) { rxnp *c, *x; ARGCHK(2); NEED(0, c); NEED(1, x); RETARR(rxsp_polyval(c, x)) }
PROCEDURE(f_pearsonr) { rxnp *x, *y; double r, p; ARGCHK(4); NEED(0, x); NEED(1, y);
    if (!rxsp_pearsonr(x, y, &r, &p)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    SETFLOAT(ARG2, r); SETFLOAT(ARG3, p); RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_ttest) { rxnp *x, *y; double t, p; ARGCHK(4); NEED(0, x); NEED(1, y);
    if (!rxsp_ttest_ind(x, y, &t, &p)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    SETFLOAT(ARG2, t); SETFLOAT(ARG3, p); RETURNINT(0); RESETSIGNAL }

#define P(f, name, ret, args) ADDPROC(f, "rxscipy." name, "b", ret, args);
LOADFUNCS
P(f_erf,"sp_erf",".float","x = .float") P(f_erfc,"sp_erfc",".float","x = .float")
P(f_gamma,"sp_gamma",".float","x = .float") P(f_lgamma,"sp_lgamma",".float","x = .float")
P(f_normpdf,"sp_norm_pdf",".float","x = .float") P(f_normcdf,"sp_norm_cdf",".float","x = .float")
P(f_normppf,"sp_norm_ppf",".float","p = .float") P(f_tcdf,"sp_t_cdf",".float","t = .float, df = .float")
P(f_betainc,"sp_betainc",".float","a = .float, b = .float, x = .float")
P(f_trapz,"sp_trapz",".float","y = .binary, x = .binary") P(f_trapz1,"sp_trapz_unit",".float","y = .binary")
P(f_simpson,"sp_simpson",".float","y = .binary, x = .binary")
P(f_interp,"sp_interp",".binary","x = .binary, xp = .binary, fp = .binary")
P(f_polyfit,"sp_polyfit",".binary","x = .binary, y = .binary, deg = .int")
P(f_polyval,"sp_polyval",".binary","c = .binary, x = .binary")
P(f_pearsonr,"sp_pearsonr",".int","x = .binary, y = .binary, expose r = .float, expose p = .float")
P(f_ttest,"sp_ttest_ind",".int","a = .binary, b = .binary, expose t = .float, expose p = .float")
ENDLOADFUNCS
