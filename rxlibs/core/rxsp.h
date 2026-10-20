/* rxsp - C89 scipy-style numerics (special functions, distributions, integration, interpolation, fits) */
#ifndef RXSP_H
#define RXSP_H
#include "rxnp.h"
double rxsp_erf(double x);
double rxsp_erfc(double x);
double rxsp_lgamma(double x);
double rxsp_gamma(double x);
double rxsp_betainc(double a, double b, double x);             /* regularised incomplete beta */
double rxsp_norm_pdf(double x);
double rxsp_norm_cdf(double x);
double rxsp_norm_ppf(double p);
double rxsp_t_cdf(double t, double df);
rxnp  *rxsp_trapz(const rxnp *y, const rxnp *x);                 /* x may be NULL: unit spacing; 0-d result */
rxnp  *rxsp_simpson(const rxnp *y, const rxnp *x);               /* odd number of samples */
rxnp  *rxsp_interp(const rxnp *x, const rxnp *xp, const rxnp *fp);
rxnp  *rxsp_polyfit(const rxnp *x, const rxnp *y, int deg);      /* highest power first */
rxnp  *rxsp_polyval(const rxnp *c, const rxnp *x);
int    rxsp_pearsonr(const rxnp *x, const rxnp *y, double *r, double *p);
int    rxsp_ttest_ind(const rxnp *a, const rxnp *b, double *t, double *p);   /* equal variance */
const char *rxsp_error(void);
/* cubic spline (scipy CubicSpline): bc 0 = not-a-knot, 1 = natural; deriv 0..2; extrapolates with the end polynomials */
rxnp *rxsp_cubic_spline(const rxnp *x, const rxnp *y, int natural, const rxnp *xnew, int deriv);
/* ---- rxsp_dist.c: special functions (accurate to ~1e-13), distributions, descriptive statistics and tests ---- */
int rxsp_fail(const char *msg);                                /* sets rxsp_error(), returns 0 */
double rxsp_beta(double a, double b);
double rxsp_lbeta(double a, double b);
double rxsp_gammainc(double a, double x);                      /* regularised lower P(a,x) */
double rxsp_gammaincc(double a, double x);                     /* regularised upper Q(a,x) */
double rxsp_erfinv(double y);
double rxsp_t_sf(double t, double df);
/* distributions: name is one of norm t chi2 f gamma beta expon uniform lognorm binom poisson geom.
 * Parameters (scipy order): a, b = shape parameters (t:df; chi2:df; f:dfn,dfd; gamma:a; beta:a,b; lognorm:s;
 * binom:n,p; poisson:mu; geom:p), then loc, scale (scale ignored for discrete).  Returns 0 and sets rxsp_error()
 * for an unknown distribution; invalid parameters give NaN like scipy. */
int    rxsp_dist_known(const char *name);
int    rxsp_dist_discrete(const char *name);
double rxsp_dist_pdf(const char *name, double x, double a, double b, double loc, double scale);   /* pmf for discrete */
double rxsp_dist_cdf(const char *name, double x, double a, double b, double loc, double scale);
double rxsp_dist_sf(const char *name, double x, double a, double b, double loc, double scale);
double rxsp_dist_ppf(const char *name, double q, double a, double b, double loc, double scale);
double rxsp_dist_mean(const char *name, double a, double b, double loc, double scale);
double rxsp_dist_var(const char *name, double a, double b, double loc, double scale);
/* random variates from the legacy MT19937 stream: reproduces np.random.seed(s); dist.rvs(size=n) of scipy */
rxnp  *rxsp_dist_rvs(const char *name, rxnp_rng *g, long n, double a, double b, double loc, double scale);
/* descriptive statistics and tests (1-d float64/int64 input) */
rxnp  *rxsp_describe(const rxnp *x);                           /* [nobs, min, max, mean, variance(ddof 1), skewness, kurtosis] */
double rxsp_skew(const rxnp *x);
double rxsp_kurtosis(const rxnp *x);                           /* Fisher, biased */
rxnp  *rxsp_zscore(const rxnp *x, int ddof);
double rxsp_percentile(const rxnp *x, double q);               /* linear interpolation, q in [0,100] */
rxnp  *rxsp_correlate(const rxnp *a, const rxnp *v, const char *mode);     /* numpy.correlate */
int    rxsp_ttest_1samp(const rxnp *x, double popmean, double *t, double *p);
int    rxsp_ttest_welch(const rxnp *a, const rxnp *b, double *t, double *p, double *df);
int    rxsp_spearmanr(const rxnp *x, const rxnp *y, double *rho, double *p);
rxnp  *rxsp_linregress(const rxnp *x, const rxnp *y);          /* [slope, intercept, r, p, stderr, intercept_stderr] */
int    rxsp_chi2_contingency(const rxnp *table, int correction, double *chi2, double *p, long *dof, rxnp **expected);
#endif
