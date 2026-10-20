/* rxsp.c - see rxsp.h. C89. */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "rxsp.h"

static char sperr[160];
const char *rxsp_error(void) { return sperr; }
static int sfail(const char *m) { strncpy(sperr, m, sizeof sperr - 1); return 0; }
static double nanq(void) { volatile double z = 0.0; return z / z; }
#define PI 3.14159265358979323846

double rxsp_lgamma(double x)
{
    static const double c[9] = { 0.99999999999980993, 676.5203681218851, -1259.1392167224028,
        771.32342877765313, -176.61502916214059, 12.507343278686905, -0.13857109526572012,
        9.9843695780195716e-6, 1.5056327351493116e-7 };
    double a, t; int i;
    if (x < 0.5) return log(PI / fabs(sin(PI * x))) - rxsp_lgamma(1.0 - x);
    x -= 1.0; a = c[0]; t = x + 7.5;
    for (i = 1; i < 9; i++) a += c[i] / (x + (double)i);
    return 0.91893853320467274178 + (x + 0.5) * log(t) - t + log(a);
}
double rxsp_gamma(double x)
{
    if (x == floor(x) && x > 0 && x < 171) { double r = 1.0; int i; for (i = 2; i < (int)x; i++) r *= i; return r; }
    if (x < 0.5) return PI / (sin(PI * x) * rxsp_gamma(1.0 - x));
    return exp(rxsp_lgamma(x));
}
/* erf/erfc: series for small |x|, continued fraction for large */
double rxsp_erfc(double x)
{
    double ax = fabs(x), r;
    if (ax < 2.5) r = 1.0 - rxsp_erf(ax);
    else {
        /* Lentz continued fraction for erfc */
        double f = 0.0; int k;
        for (k = 60; k >= 1; k--) f = (double)k / 2.0 / (ax + f);
        r = exp(-ax * ax) / sqrt(PI) / (ax + f);
    }
    return x >= 0 ? r : 2.0 - r;
}
double rxsp_erf(double x)
{
    double ax = fabs(x), sum, term; int n;
    if (ax >= 2.5) { double r = 1.0 - rxsp_erfc(ax); return x < 0 ? -r : r; }
    sum = ax; term = ax;
    for (n = 1; n < 200; n++) {
        term *= -ax * ax / (double)n;
        sum += term / (2.0 * n + 1.0);
        if (fabs(term) < 1e-17 * fabs(sum)) break;
    }
    sum *= 2.0 / sqrt(PI);
    return x < 0 ? -sum : sum;
}
double rxsp_norm_pdf(double x) { return exp(-0.5 * x * x) / sqrt(2.0 * PI); }
double rxsp_norm_cdf(double x) { return 0.5 * rxsp_erfc(-x / sqrt(2.0)); }
double rxsp_norm_ppf(double p)
{
    static const double a[6] = { -3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
        1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00 };
    static const double b[5] = { -5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
        6.680131188771972e+01, -1.328068155288572e+01 };
    static const double c[6] = { -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
        -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00 };
    static const double d[4] = { 7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
        3.754408661907416e+00 };
    double x, q, r, e, u; int i;
    if (p <= 0.0) return p == 0.0 ? -HUGE_VAL : nanq();
    if (p >= 1.0) return p == 1.0 ? HUGE_VAL : nanq();
    if (p < 0.02425) { q = sqrt(-2.0 * log(p)); x = (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0); }
    else if (p > 1.0 - 0.02425) { q = sqrt(-2.0 * log(1.0 - p)); x = -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0); }
    else { q = p - 0.5; r = q * q; x = (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q / (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0); }
    for (i = 0; i < 3; i++) {                       /* Halley refinement */
        e = rxsp_norm_cdf(x) - p; u = e * sqrt(2.0 * PI) * exp(x * x / 2.0);
        x = x - u / (1.0 + x * u / 2.0);
    }
    return x;
}
static double betacf(double a, double b, double x)
{
    double qab = a + b, qap = a + 1.0, qam = a - 1.0, c = 1.0, d = 1.0 - qab * x / qap, h, aa, del; int m;
    if (fabs(d) < 1e-300) d = 1e-300;
    d = 1.0 / d; h = d;
    for (m = 1; m <= 500; m++) {
        int m2 = 2 * m;
        aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d; if (fabs(d) < 1e-300) d = 1e-300;
        c = 1.0 + aa / c; if (fabs(c) < 1e-300) c = 1e-300;
        d = 1.0 / d; h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d; if (fabs(d) < 1e-300) d = 1e-300;
        c = 1.0 + aa / c; if (fabs(c) < 1e-300) c = 1e-300;
        d = 1.0 / d; del = d * c; h *= del;
        if (fabs(del - 1.0) < 1e-16) break;
    }
    return h;
}
double rxsp_betainc(double a, double b, double x)
{
    double bt;
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    bt = exp(rxsp_lgamma(a + b) - rxsp_lgamma(a) - rxsp_lgamma(b) + a * log(x) + b * log(1.0 - x));
    if (x < (a + 1.0) / (a + b + 2.0)) return bt * betacf(a, b, x) / a;
    return 1.0 - bt * betacf(b, a, 1.0 - x) / b;
}
double rxsp_t_cdf(double t, double df)
{
    double x = df / (df + t * t), p = 0.5 * rxsp_betainc(df / 2.0, 0.5, x);
    return t > 0 ? 1.0 - p : p;
}
static double ev(const rxnp *a, long k) { return a->dt == RXNP_F64 ? a->f[k] : (double)a->i[k]; }
rxnp *rxsp_trapz(const rxnp *y, const rxnp *x)
{
    long n = y->size, k; double s = 0.0; rxnp *r;
    if (x && x->size != n) { sfail("trapz: x and y must have the same length"); return NULL; }
    for (k = 1; k < n; k++) s += (x ? ev(x, k) - ev(x, k - 1) : 1.0) * (ev(y, k) + ev(y, k - 1)) / 2.0;
    r = rxnp_scalar(s);
    return r;
}
rxnp *rxsp_simpson(const rxnp *y, const rxnp *x)
{
    long n = y->size, k; double s = 0.0;
    if (n < 3 || n % 2 == 0) { sfail("simpson: needs an odd number of samples >= 3"); return NULL; }
    if (x && x->size != n) { sfail("simpson: x and y must have the same length"); return NULL; }
    for (k = 0; k + 2 < n; k += 2) {
        double h0 = x ? ev(x, k + 1) - ev(x, k) : 1.0, h1 = x ? ev(x, k + 2) - ev(x, k + 1) : 1.0;
        double hs = h0 + h1, hp = h0 * h1 == 0 ? 0 : hs / (h0 * h1);
        s += hs / 6.0 * (ev(y, k) * (2.0 - h1 / h0) + ev(y, k + 1) * hs * hp + ev(y, k + 2) * (2.0 - h0 / h1));
    }
    return rxnp_scalar(s);
}
rxnp *rxsp_interp(const rxnp *x, const rxnp *xp, const rxnp *fp)
{
    long n = xp->size, k, lo, hi, mid; rxnp *r;
    if (n < 1 || fp->size != n) { sfail("interp: xp and fp must be non-empty and equal in length"); return NULL; }
    r = rxnp_new(RXNP_F64, x->ndim, x->shape);
    if (!r) return NULL;
    for (k = 0; k < x->size; k++) {
        double v = ev(x, k), res;
        if (v <= ev(xp, 0)) res = ev(fp, 0);
        else if (v >= ev(xp, n - 1)) res = ev(fp, n - 1);
        else {
            lo = 0; hi = n - 1;
            while (hi - lo > 1) { mid = (lo + hi) / 2; if (ev(xp, mid) <= v) lo = mid; else hi = mid; }
            res = ev(fp, lo) + (ev(fp, hi) - ev(fp, lo)) * (v - ev(xp, lo)) / (ev(xp, hi) - ev(xp, lo));
        }
        r->f[k] = res;
    }
    return r;
}
rxnp *rxsp_polyfit(const rxnp *x, const rxnp *y, int deg)
{
    long m = x->size, n = deg + 1L, i, j; long sh[2]; rxnp *A, *Q, *R, *c, *b; rxnp *qq = NULL, *rr = NULL;
    if (deg < 0 || y->size != m || m < n) { sfail("polyfit: bad degree or lengths"); return NULL; }
    sh[0] = m; sh[1] = n; A = rxnp_new(RXNP_F64, 2, sh);
    if (!A) return NULL;
    for (i = 0; i < m; i++) for (j = 0; j < n; j++) A->f[i * n + j] = pow(ev(x, i), (double)(deg - j));
    if (!rxnp_qr(A, &qq, &rr)) { rxnp_release(A); sfail(rxnp_la_error()); return NULL; }
    Q = qq; R = rr; rxnp_release(A);
    sh[0] = n; c = rxnp_new(RXNP_F64, 1, sh); b = c;
    if (!c) { rxnp_release(Q); rxnp_release(R); return NULL; }
    for (j = 0; j < n; j++) { double s = 0.0; for (i = 0; i < m; i++) s += Q->f[i * n + j] * ev(y, i); b->f[j] = s; }
    for (i = n - 1; i >= 0; i--) {
        double s = b->f[i];
        for (j = i + 1; j < n; j++) s -= R->f[i * n + j] * c->f[j];
        c->f[i] = s / R->f[i * n + i];
    }
    rxnp_release(Q); rxnp_release(R);
    return c;
}
rxnp *rxsp_polyval(const rxnp *c, const rxnp *x)
{
    long k, j; rxnp *r = rxnp_new(RXNP_F64, x->ndim, x->shape);
    if (!r) return NULL;
    for (k = 0; k < x->size; k++) { double s = 0.0; for (j = 0; j < c->size; j++) s = s * ev(x, k) + ev(c, j); r->f[k] = s; }
    return r;
}
int rxsp_pearsonr(const rxnp *x, const rxnp *y, double *r, double *p)
{
    long n = x->size, k; double mx = 0, my = 0, sxy = 0, sxx = 0, syy = 0, t;
    if (n != y->size || n < 2) return sfail("pearsonr: need two equal-length samples of at least 2");
    for (k = 0; k < n; k++) { mx += ev(x, k); my += ev(y, k); }
    mx /= n; my /= n;
    for (k = 0; k < n; k++) { double dx = ev(x, k) - mx, dy = ev(y, k) - my; sxy += dx * dy; sxx += dx * dx; syy += dy * dy; }
    *r = sxy / sqrt(sxx * syy);
    if (*r > 1.0) *r = 1.0;
    if (*r < -1.0) *r = -1.0;
    if (fabs(*r) >= 1.0) *p = 0.0;
    else { t = *r * sqrt((n - 2.0) / (1.0 - *r * *r)); *p = 2.0 * (1.0 - rxsp_t_cdf(fabs(t), n - 2.0)); }
    return 1;
}
int rxsp_ttest_ind(const rxnp *a, const rxnp *b, double *t, double *p)
{
    long n1 = a->size, n2 = b->size, k; double m1 = 0, m2 = 0, v1 = 0, v2 = 0, sp, df;
    if (n1 < 2 || n2 < 2) return sfail("ttest_ind: each sample needs at least 2 values");
    for (k = 0; k < n1; k++) m1 += ev(a, k); m1 /= n1;
    for (k = 0; k < n2; k++) m2 += ev(b, k); m2 /= n2;
    for (k = 0; k < n1; k++) v1 += (ev(a, k) - m1) * (ev(a, k) - m1);
    for (k = 0; k < n2; k++) v2 += (ev(b, k) - m2) * (ev(b, k) - m2);
    df = n1 + n2 - 2.0;
    sp = (v1 + v2) / df;
    *t = (m1 - m2) / sqrt(sp * (1.0 / n1 + 1.0 / n2));
    *p = 2.0 * (1.0 - rxsp_t_cdf(fabs(*t), df));
    return 1;
}
