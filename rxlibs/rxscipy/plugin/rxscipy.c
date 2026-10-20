/*
 * rxscipy - CREXX native plugin: scipy-style numerics on rxnumpy array handles.
 * Accepts handles created by the rxnumpy plugin (matched by payload type_name) and returns
 * rxnumpy-compatible handles.  Scalars in/out use .float.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
SCALAR1(f_sqrt, sqrt(x)) SCALAR1(f_exp, exp(x)) SCALAR1(f_log, log(x)) SCALAR1(f_fabs, fabs(x))
SCALAR1(f_sin, sin(x)) SCALAR1(f_cos, cos(x)) SCALAR1(f_floor, floor(x))
PROCEDURE(f_pow) { ARGCHK(2); RETURNFLOAT(pow(GETFLOAT(ARG0), GETFLOAT(ARG1))); RESETSIGNAL }
PROCEDURE(f_isfinite) { double x; ARGCHK(1); x = GETFLOAT(ARG0); RETURNINT((x == x && x - x == 0.0) ? 1 : 0); RESETSIGNAL }
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
PROCEDURE(f_cspline) { rxnp *x, *y, *xn; ARGCHK(5); NEED(0, x); NEED(1, y); NEED(2, xn);
    RETARR(rxsp_cubic_spline(x, y, (int)GETINT(ARG3), xn, (int)GETINT(ARG4))) }
PROCEDURE(f_polyfit) { rxnp *x, *y; ARGCHK(3); NEED(0, x); NEED(1, y); RETARR(rxsp_polyfit(x, y, (int)GETINT(ARG2))) }
PROCEDURE(f_polyval) { rxnp *c, *x; ARGCHK(2); NEED(0, c); NEED(1, x); RETARR(rxsp_polyval(c, x)) }
PROCEDURE(f_pearsonr) { rxnp *x, *y; double r, p; ARGCHK(4); NEED(0, x); NEED(1, y);
    if (!rxsp_pearsonr(x, y, &r, &p)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    SETFLOAT(ARG2, r); SETFLOAT(ARG3, p); RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_ttest) { rxnp *x, *y; double t, p; ARGCHK(4); NEED(0, x); NEED(1, y);
    if (!rxsp_ttest_ind(x, y, &t, &p)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    SETFLOAT(ARG2, t); SETFLOAT(ARG3, p); RETURNINT(0); RESETSIGNAL }

/* ---- distributions, special functions, statistics (rxsp_dist.c) ---- */
static rxnp_rng *g_rng = NULL;
static rxnp_rng *rng(void) { if (!g_rng) g_rng = rxnp_rng_new(); return g_rng; }
#define DISTCHK(nm) if (!rxsp_dist_known(nm)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, "unknown distribution (norm t chi2 f gamma beta expon uniform lognorm binom poisson geom)")
#define DISTFN(fn, core) PROCEDURE(fn) { const char *nm; ARGCHK(6); nm = GETSTRING(ARG0); DISTCHK(nm); \
    RETURNFLOAT(core(nm, GETFLOAT(ARG1), GETFLOAT(ARG2), GETFLOAT(ARG3), GETFLOAT(ARG4), GETFLOAT(ARG5))); RESETSIGNAL }
DISTFN(f_dpdf, rxsp_dist_pdf) DISTFN(f_dcdf, rxsp_dist_cdf) DISTFN(f_dsf, rxsp_dist_sf) DISTFN(f_dppf, rxsp_dist_ppf)
PROCEDURE(f_dmean) { const char *nm; ARGCHK(5); nm = GETSTRING(ARG0); DISTCHK(nm);
    RETURNFLOAT(rxsp_dist_mean(nm, GETFLOAT(ARG1), GETFLOAT(ARG2), GETFLOAT(ARG3), GETFLOAT(ARG4))); RESETSIGNAL }
PROCEDURE(f_dvar) { const char *nm; ARGCHK(5); nm = GETSTRING(ARG0); DISTCHK(nm);
    RETURNFLOAT(rxsp_dist_var(nm, GETFLOAT(ARG1), GETFLOAT(ARG2), GETFLOAT(ARG3), GETFLOAT(ARG4))); RESETSIGNAL }
PROCEDURE(f_seed) { ARGCHK(1); rxnp_rng_seed(rng(), (unsigned long)GETINT(ARG0)); RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_drvs) { const char *nm; ARGCHK(6); nm = GETSTRING(ARG0); DISTCHK(nm);
    RETARR(rxsp_dist_rvs(nm, rng(), (long)GETINT(ARG1), GETFLOAT(ARG2), GETFLOAT(ARG3), GETFLOAT(ARG4), GETFLOAT(ARG5))) }
SCALAR1(f_erfinv, rxsp_erfinv(x))
PROCEDURE(f_beta) { ARGCHK(2); RETURNFLOAT(rxsp_beta(GETFLOAT(ARG0), GETFLOAT(ARG1))); RESETSIGNAL }
PROCEDURE(f_gammainc) { ARGCHK(2); RETURNFLOAT(rxsp_gammainc(GETFLOAT(ARG0), GETFLOAT(ARG1))); RESETSIGNAL }
PROCEDURE(f_gammaincc) { ARGCHK(2); RETURNFLOAT(rxsp_gammaincc(GETFLOAT(ARG0), GETFLOAT(ARG1))); RESETSIGNAL }
PROCEDURE(f_skew) { rxnp *x; ARGCHK(1); NEED(0, x); RETURNFLOAT(rxsp_skew(x)); RESETSIGNAL }
PROCEDURE(f_kurt) { rxnp *x; ARGCHK(1); NEED(0, x); RETURNFLOAT(rxsp_kurtosis(x)); RESETSIGNAL }
PROCEDURE(f_describe) { rxnp *x; ARGCHK(1); NEED(0, x); RETARR(rxsp_describe(x)) }
PROCEDURE(f_zscore) { rxnp *x; ARGCHK(2); NEED(0, x); RETARR(rxsp_zscore(x, (int)GETINT(ARG1))) }
PROCEDURE(f_percentile) { rxnp *x; double v; ARGCHK(2); NEED(0, x); rxsp_fail("");
    v = rxsp_percentile(x, GETFLOAT(ARG1)); if (v != v) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error()); RETURNFLOAT(v); RESETSIGNAL }
PROCEDURE(f_correlate) { rxnp *a, *v; ARGCHK(3); NEED(0, a); NEED(1, v); RETARR(rxsp_correlate(a, v, GETSTRING(ARG2))) }
PROCEDURE(f_ttest1) { rxnp *x; double t, p; ARGCHK(4); NEED(0, x);
    if (!rxsp_ttest_1samp(x, GETFLOAT(ARG1), &t, &p)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    SETFLOAT(ARG2, t); SETFLOAT(ARG3, p); RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_ttestw) { rxnp *x, *y; double t, p, df; ARGCHK(5); NEED(0, x); NEED(1, y);
    if (!rxsp_ttest_welch(x, y, &t, &p, &df)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    SETFLOAT(ARG2, t); SETFLOAT(ARG3, p); SETFLOAT(ARG4, df); RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_spearman) { rxnp *x, *y; double r, p; ARGCHK(4); NEED(0, x); NEED(1, y);
    if (!rxsp_spearmanr(x, y, &r, &p)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    SETFLOAT(ARG2, r); SETFLOAT(ARG3, p); RETURNINT(0); RESETSIGNAL }
PROCEDURE(f_linreg) { rxnp *x, *y; ARGCHK(2); NEED(0, x); NEED(1, y); RETARR(rxsp_linregress(x, y)) }
PROCEDURE(f_chi2c) { rxnp *t, *e = NULL; double c, p; long dof; ARGCHK(6); NEED(0, t);
    if (!rxsp_chi2_contingency(t, (int)GETINT(ARG1), &c, &p, &dof, &e)) RETURNSIGNAL(SIGNAL_INVALID_ARGUMENTS, rxsp_error())
    SETFLOAT(ARG2, c); SETFLOAT(ARG3, p); SETINT(ARG4, dof);
    if (!publish(ARG5, e)) RETURNSIGNAL(SIGNAL_FAILURE, "cannot publish array handle")
    RETURNINT(0); RESETSIGNAL }

#define P(f, name, ret, args) ADDPROC(f, "rxscipy." name, "b", ret, args);
LOADFUNCS
P(f_sqrt,"sp_sqrt",".float","x = .float") P(f_exp,"sp_exp",".float","x = .float") P(f_log,"sp_log",".float","x = .float")
P(f_fabs,"sp_fabs",".float","x = .float") P(f_sin,"sp_sin",".float","x = .float") P(f_cos,"sp_cos",".float","x = .float")
P(f_floor,"sp_floor",".float","x = .float") P(f_pow,"sp_pow",".float","x = .float, y = .float")
P(f_isfinite,"sp_isfinite",".int","x = .float")
P(f_erf,"sp_erf",".float","x = .float") P(f_erfc,"sp_erfc",".float","x = .float")
P(f_gamma,"sp_gamma",".float","x = .float") P(f_lgamma,"sp_lgamma",".float","x = .float")
P(f_normpdf,"sp_norm_pdf",".float","x = .float") P(f_normcdf,"sp_norm_cdf",".float","x = .float")
P(f_normppf,"sp_norm_ppf",".float","p = .float") P(f_tcdf,"sp_t_cdf",".float","t = .float, df = .float")
P(f_betainc,"sp_betainc",".float","a = .float, b = .float, x = .float")
P(f_trapz,"sp_trapz",".float","y = .binary, x = .binary") P(f_trapz1,"sp_trapz_unit",".float","y = .binary")
P(f_simpson,"sp_simpson",".float","y = .binary, x = .binary")
P(f_interp,"sp_interp",".binary","x = .binary, xp = .binary, fp = .binary")
P(f_cspline,"sp_cubic_spline",".binary","x = .binary, y = .binary, xnew = .binary, natural = .int, deriv = .int")
P(f_polyfit,"sp_polyfit",".binary","x = .binary, y = .binary, deg = .int")
P(f_polyval,"sp_polyval",".binary","c = .binary, x = .binary")
P(f_pearsonr,"sp_pearsonr",".int","x = .binary, y = .binary, expose r = .float, expose p = .float")
P(f_ttest,"sp_ttest_ind",".int","a = .binary, b = .binary, expose t = .float, expose p = .float")
P(f_dpdf,"sp_pdf",".float","dist = .string, x = .float, a = .float, b = .float, loc = .float, scale = .float")
P(f_dpdf,"sp_pmf",".float","dist = .string, x = .float, a = .float, b = .float, loc = .float, scale = .float")
P(f_dcdf,"sp_cdf",".float","dist = .string, x = .float, a = .float, b = .float, loc = .float, scale = .float")
P(f_dsf,"sp_sf",".float","dist = .string, x = .float, a = .float, b = .float, loc = .float, scale = .float")
P(f_dppf,"sp_ppf",".float","dist = .string, q = .float, a = .float, b = .float, loc = .float, scale = .float")
P(f_dmean,"sp_mean",".float","dist = .string, a = .float, b = .float, loc = .float, scale = .float")
P(f_dvar,"sp_var",".float","dist = .string, a = .float, b = .float, loc = .float, scale = .float")
P(f_seed,"sp_seed",".int","seed = .int")
P(f_drvs,"sp_rvs",".binary","dist = .string, size = .int, a = .float, b = .float, loc = .float, scale = .float")
P(f_erfinv,"sp_erfinv",".float","y = .float") P(f_beta,"sp_beta",".float","a = .float, b = .float")
P(f_gammainc,"sp_gammainc",".float","a = .float, x = .float") P(f_gammaincc,"sp_gammaincc",".float","a = .float, x = .float")
P(f_skew,"sp_skew",".float","x = .binary") P(f_kurt,"sp_kurtosis",".float","x = .binary")
P(f_describe,"sp_describe",".binary","x = .binary") P(f_zscore,"sp_zscore",".binary","x = .binary, ddof = .int")
P(f_percentile,"sp_percentile",".float","x = .binary, q = .float")
P(f_correlate,"sp_correlate",".binary","a = .binary, v = .binary, mode = .string")
P(f_ttest1,"sp_ttest_1samp",".int","x = .binary, popmean = .float, expose t = .float, expose p = .float")
P(f_ttestw,"sp_ttest_welch",".int","a = .binary, b = .binary, expose t = .float, expose p = .float, expose df = .float")
P(f_spearman,"sp_spearmanr",".int","x = .binary, y = .binary, expose rho = .float, expose p = .float")
P(f_linreg,"sp_linregress",".binary","x = .binary, y = .binary")
P(f_chi2c,"sp_chi2_contingency",".int","table = .binary, correction = .int, expose chi2 = .float, expose p = .float, expose dof = .int, expose expected = .binary")
ENDLOADFUNCS
