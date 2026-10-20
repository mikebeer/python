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
#endif
