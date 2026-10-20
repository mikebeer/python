#ifndef RXNP_LAPACK_H
#define RXNP_LAPACK_H
/* all matrices row-major */
int  rxla_lu_dec(double *a, long n, long *piv);                   /* returns sign or 0 */
void rxla_lu_solve(const double *lu, long n, const long *piv, double *b, long nrhs);
int  rxla_geev(double *a, long n, double *wr, double *wi, double *vr);
int  rxla_syev(double *a, long n, int upper, double *w, double *v);
int  rxla_gesdd(double *a, long m, long n, int full, int vec, double *s, double *u, double *vt);
void rxla_geqr2(double *a, long m, long n, double *tau);
void rxla_org2r(double *q, long m, long ncol, long kk, const double *tau);
#endif
