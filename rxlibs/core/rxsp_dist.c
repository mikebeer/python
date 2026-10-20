/* rxsp_dist.c - special functions, distributions (pdf/pmf, cdf, sf, ppf, rvs, mean, var), descriptive statistics and tests.
 * C89.  Special functions follow Cephes/NR algorithms, accurate to about 1e-13 relative in the usual ranges. */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "rxsp.h"

#define PI 3.14159265358979323846
#define LN_SQRT_2PI 0.91893853320467274178
static double nanv(void) { volatile double z = 0.0; return z / z; }
static double infv(void) { return HUGE_VAL; }
static int isnanv(double x) { return x != x; }
static double rx_expm1(double x) { double u = exp(x); if (u == 1.0) return x; if (u - 1.0 == -1.0) return -1.0; return (u - 1.0) * x / log(u); }
static double rx_log1p(double x) { double u = 1.0 + x; if (u == 1.0) return x; return log(u) * x / (u - 1.0); }

/* ---------------------------------------------------------------- gamma family */
static double stirling(double x)       /* lgamma(x) for x >= 10 */
{
    double x2 = 1.0 / (x * x);
    double s = (1.0/12 - x2*(1.0/360 - x2*(1.0/1260 - x2*(1.0/1680 - x2*(1.0/1188 - x2*(691.0/360360 - x2/156.0)))))) / x;
    return (x - 0.5) * log(x) - x + LN_SQRT_2PI + s;
}
double rxsp_lgamma(double x)
{
    double prod = 1.0;
    if (isnanv(x)) return x;
    if (x == 1.0 || x == 2.0) return 0.0;
    if (x <= 0.0) {
        if (x == floor(x)) return infv();
        return log(PI / fabs(sin(PI * x))) - rxsp_lgamma(1.0 - x);
    }
    if (x >= 10.0) return x > 1e300 ? infv() : stirling(x);
    while (x < 10.0) { prod *= x; x += 1.0; }
    return stirling(x) - log(prod);
}
double rxsp_gamma(double x)
{
    double prod = 1.0;
    if (isnanv(x)) return x;
    if (x == 0.0) return infv();
    if (x == floor(x)) {
        if (x < 0.0) return nanv();
        if (x < 171.5) { double r = 1.0; int i; for (i = 2; i < (int)x; i++) r *= i; return r; }
        return infv();
    }
    if (x < 0.0) return PI / (sin(PI * x) * rxsp_gamma(1.0 - x));
    if (x > 171.7) return infv();
    while (x < 10.0) { prod *= x; x += 1.0; }
    return exp(stirling(x)) / prod;
}
double rxsp_lbeta(double a, double b) { return rxsp_lgamma(a) + rxsp_lgamma(b) - rxsp_lgamma(a + b); }
double rxsp_beta(double a, double b)
{
    if (a > 0.0 && b > 0.0 && a + b < 100.0) return rxsp_gamma(a) * rxsp_gamma(b) / rxsp_gamma(a + b);
    return exp(rxsp_lbeta(a, b));
}
/* regularised incomplete gamma */
static double gser(double a, double x)          /* P(a,x), x < a+1 */
{
    double sum = 1.0 / a, del = sum, ap = a; int n;
    for (n = 0; n < 200000; n++) { ap += 1.0; del *= x / ap; sum += del; if (fabs(del) < fabs(sum) * 1e-17) break; }
    return sum * exp(-x + a * log(x) - rxsp_lgamma(a));
}
static double gcf(double a, double x)           /* Q(a,x), x >= a+1 : modified Lentz */
{
    const double tiny = 1e-300;
    double b = x + 1.0 - a, c = 1.0 / tiny, d = 1.0 / b, h = d, an, del; int i;
    for (i = 1; i < 200000; i++) {
        an = -(double)i * ((double)i - a); b += 2.0;
        d = an * d + b; if (fabs(d) < tiny) d = tiny;
        c = b + an / c; if (fabs(c) < tiny) c = tiny;
        d = 1.0 / d; del = d * c; h *= del;
        if (fabs(del - 1.0) < 1e-16) break;
    }
    return exp(-x + a * log(x) - rxsp_lgamma(a)) * h;
}
double rxsp_gammainc(double a, double x)
{
    if (isnanv(a) || isnanv(x) || a < 0.0 || x < 0.0) return nanv();
    if (a == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    if (x == infv()) return 1.0;
    return x < a + 1.0 ? gser(a, x) : 1.0 - gcf(a, x);
}
double rxsp_gammaincc(double a, double x)
{
    if (isnanv(a) || isnanv(x) || a < 0.0 || x < 0.0) return nanv();
    if (a == 0.0) return 0.0;
    if (x == 0.0) return 1.0;
    if (x == infv()) return 0.0;
    return x < a + 1.0 ? 1.0 - gser(a, x) : gcf(a, x);
}
/* ---------------------------------------------------------------- error function */
double rxsp_erf(double x)
{
    double ax = fabs(x), term, sum, r; int n;
    if (isnanv(x)) return x;
    if (ax >= 3.0) { r = 1.0 - rxsp_erfc(ax); return x < 0 ? -r : r; }
    /* erf x = 2/sqrt(pi) exp(-x^2) sum 2^n x^(2n+1) / (1*3*...*(2n+1)) : all terms positive */
    term = ax; sum = ax;
    for (n = 1; n < 300; n++) { term *= 2.0 * ax * ax / (2.0 * n + 1.0); sum += term; if (term < sum * 1e-17) break; }
    r = 2.0 / sqrt(PI) * exp(-ax * ax) * sum;
    return x < 0 ? -r : r;
}
double rxsp_erfc(double x)
{
    double ax = fabs(x), r;
    if (isnanv(x)) return x;
    if (ax < 1.2) r = 1.0 - rxsp_erf(ax);
    else if (ax > 27.3) r = 0.0;
    else r = gcf(0.5, ax * ax);
    return x >= 0 ? r : 2.0 - r;
}
double rxsp_erfinv(double y)
{
    double ay = fabs(y), x, f, fp, l, a = 0.147, t1, t2; int i, usec;
    if (isnanv(y) || ay > 1.0) return nanv();
    if (ay == 1.0) return y > 0 ? infv() : -infv();
    if (ay == 0.0) return y;
    l = log((1.0 - ay) * (1.0 + ay));
    t1 = 2.0 / (PI * a) + l / 2.0; t2 = l / a;
    x = sqrt(sqrt(t1 * t1 - t2) - t1);
    usec = ay > 0.9;
    for (i = 0; i < 8; i++) {
        fp = 2.0 / sqrt(PI) * exp(-x * x);
        if (usec) { f = rxsp_erfc(x) - (1.0 - ay); fp = -fp; } else f = rxsp_erf(x) - ay;
        if (fp == 0.0) break;
        x = x - f / (fp + x * f);
    }
    return y < 0 ? -x : x;
}
/* ---------------------------------------------------------------- distribution cores */
enum { D_NORM, D_T, D_CHI2, D_F, D_GAMMA, D_BETA, D_EXPON, D_UNIFORM, D_LOGNORM, D_BINOM, D_POISSON, D_GEOM, D_NONE };
static const char *dnames[] = { "norm", "t", "chi2", "f", "gamma", "beta", "expon", "uniform", "lognorm", "binom", "poisson", "geom" };
static int did(const char *nm) { int i; for (i = 0; i < D_NONE; i++) if (strcmp(nm, dnames[i]) == 0) return i; return D_NONE; }
int rxsp_dist_known(const char *nm) { return did(nm) != D_NONE; }
int rxsp_dist_discrete(const char *nm) { int d = did(nm); return d == D_BINOM || d == D_POISSON || d == D_GEOM; }

static double t_tail(double z, double df)       /* P(T > |z|) */
{
    if (df > 1e10) return 0.5 * rxsp_erfc(fabs(z) / sqrt(2.0));
    return 0.5 * rxsp_betainc(df / 2.0, 0.5, df / (df + z * z));
}
double rxsp_t_sf(double t, double df)
{
    double p = t_tail(t, df);
    if (t == 0.0) return 0.5;
    return t > 0 ? p : 1.0 - p;
}
/* standardised distribution functions */
static double std_pdf(int d, double z, double a, double b)
{
    switch (d) {
    case D_NORM: return exp(-0.5 * z * z) / sqrt(2.0 * PI);
    case D_T: if (a > 1e10) return exp(-0.5 * z * z) / sqrt(2.0 * PI);
        return exp(rxsp_lgamma((a + 1.0) / 2.0) - rxsp_lgamma(a / 2.0) - (a + 1.0) / 2.0 * rx_log1p(z * z / a)) / sqrt(a * PI);
    case D_CHI2: if (z < 0) return 0.0;
        if (z == 0) return a < 2 ? infv() : (a == 2 ? 0.5 : 0.0);
        return exp((a / 2.0 - 1.0) * log(z) - z / 2.0 - a / 2.0 * log(2.0) - rxsp_lgamma(a / 2.0));
    case D_F: if (z < 0) return 0.0;
        if (z == 0) return a < 2 ? infv() : (a == 2 ? 1.0 : 0.0);
        return exp(a / 2.0 * log(a) + b / 2.0 * log(b) + (a / 2.0 - 1.0) * log(z) - (a + b) / 2.0 * log(b + a * z) - rxsp_lbeta(a / 2.0, b / 2.0));
    case D_GAMMA: if (z < 0) return 0.0;
        if (z == 0) return a < 1 ? infv() : (a == 1 ? 1.0 : 0.0);
        return exp((a - 1.0) * log(z) - z - rxsp_lgamma(a));
    case D_BETA: if (z < 0 || z > 1) return 0.0;
        if (z == 0) return a < 1 ? infv() : (a == 1 ? b : 0.0);
        if (z == 1) return b < 1 ? infv() : (b == 1 ? a : 0.0);
        return exp((a - 1.0) * log(z) + (b - 1.0) * rx_log1p(-z) - rxsp_lbeta(a, b));
    case D_EXPON: return z < 0 ? 0.0 : exp(-z);
    case D_UNIFORM: return (z < 0 || z > 1) ? 0.0 : 1.0;
    case D_LOGNORM: if (z <= 0) return 0.0; { double l = log(z) / a; return exp(-0.5 * l * l) / (a * z * sqrt(2.0 * PI)); }
    }
    return nanv();
}
static double std_cdf(int d, double z, double a, double b)
{
    switch (d) {
    case D_NORM: return 0.5 * rxsp_erfc(-z / sqrt(2.0));
    case D_T: if (z == 0) return 0.5; { double p = t_tail(z, a); return z > 0 ? 1.0 - p : p; }
    case D_CHI2: return z <= 0 ? 0.0 : rxsp_gammainc(a / 2.0, z / 2.0);
    case D_F: return z <= 0 ? 0.0 : rxsp_betainc(a / 2.0, b / 2.0, a * z / (a * z + b));
    case D_GAMMA: return z <= 0 ? 0.0 : rxsp_gammainc(a, z);
    case D_BETA: return rxsp_betainc(a, b, z);
    case D_EXPON: return z <= 0 ? 0.0 : -rx_expm1(-z);
    case D_UNIFORM: return z <= 0 ? 0.0 : (z >= 1 ? 1.0 : z);
    case D_LOGNORM: return z <= 0 ? 0.0 : 0.5 * rxsp_erfc(-log(z) / a / sqrt(2.0));
    }
    return nanv();
}
static double std_sf(int d, double z, double a, double b)
{
    switch (d) {
    case D_NORM: return 0.5 * rxsp_erfc(z / sqrt(2.0));
    case D_T: if (z == 0) return 0.5; { double p = t_tail(z, a); return z > 0 ? p : 1.0 - p; }
    case D_CHI2: return z <= 0 ? 1.0 : rxsp_gammaincc(a / 2.0, z / 2.0);
    case D_F: return z <= 0 ? 1.0 : rxsp_betainc(b / 2.0, a / 2.0, b / (a * z + b));
    case D_GAMMA: return z <= 0 ? 1.0 : rxsp_gammaincc(a, z);
    case D_BETA: return z <= 0 ? 1.0 : (z >= 1 ? 0.0 : rxsp_betainc(b, a, 1.0 - z));
    case D_EXPON: return z <= 0 ? 1.0 : exp(-z);
    case D_UNIFORM: return z <= 0 ? 1.0 : (z >= 1 ? 0.0 : 1.0 - z);
    case D_LOGNORM: return z <= 0 ? 1.0 : 0.5 * rxsp_erfc(log(z) / a / sqrt(2.0));
    }
    return nanv();
}
/* safeguarded Newton on the smaller tail for the continuous distributions without closed form */
static double std_ppf_num(int d, double q, double a, double b)
{
    int upper = q > 0.5, it; double tgt = upper ? 1.0 - q : q, lo, hi, x, fx, dfx, xn;
    int tdist = (d == D_T);
    if (tdist) {                                    /* symmetric: solve for x >= 0 on the upper tail */
        double r;
        if (q == 0.5) return 0.0;
        tgt = q > 0.5 ? 1.0 - q : q; r = 0.0;
        lo = 0.0; hi = 1.0;
        while (std_sf(d, hi, a, b) > tgt && hi < 1e300) hi *= 2.0;
        x = 0.5 * (lo + hi);
        for (it = 0; it < 300; it++) {
            fx = std_sf(d, x, a, b) - tgt;
            if (fx > 0) lo = x; else hi = x;
            dfx = -std_pdf(d, x, a, b);
            xn = (dfx != 0.0) ? x - fx / dfx : lo - 1.0;
            if (!(xn > lo && xn < hi)) xn = 0.5 * (lo + hi);
            if (fabs(xn - x) <= 1e-16 * fabs(xn) || hi - lo <= 1e-16 * hi) { x = xn; break; }
            x = xn;
        }
        r = x;
        return q > 0.5 ? r : -r;
    }
    lo = 0.0; hi = 1.0;
    if (d != D_BETA)
        while ((upper ? std_sf(d, hi, a, b) > tgt : std_cdf(d, hi, a, b) < tgt) && hi < 1e300) hi *= 2.0;
    x = 0.5 * (lo + hi);
    for (it = 0; it < 400; it++) {
        double fv = upper ? std_sf(d, x, a, b) : std_cdf(d, x, a, b);
        fx = fv - tgt;
        if (upper) { if (fx > 0) lo = x; else hi = x; } else { if (fx < 0) lo = x; else hi = x; }
        dfx = std_pdf(d, x, a, b); if (upper) dfx = -dfx;
        xn = (dfx != 0.0 && dfx == dfx && fabs(dfx) < 1e300) ? x - fx / dfx : -1.0;
        if (!(xn > lo && xn < hi)) xn = 0.5 * (lo + hi);
        if (fabs(xn - x) <= 1e-16 * fabs(xn) || hi - lo <= 1e-17 * hi) { x = xn; break; }
        x = xn;
    }
    return x;
}
static double std_ppf(int d, double q, double a, double b)
{
    if (isnanv(q) || q < 0.0 || q > 1.0) return nanv();
    switch (d) {
    case D_NORM: return rxsp_norm_ppf(q);
    case D_EXPON: return q == 1.0 ? infv() : -rx_log1p(-q);
    case D_UNIFORM: return q;
    case D_LOGNORM: return exp(a * rxsp_norm_ppf(q));
    }
    if (q == 0.0) return d == D_T ? -infv() : 0.0;
    if (q == 1.0) return d == D_BETA ? 1.0 : infv();
    return std_ppf_num(d, q, a, b);
}
/* discrete */
static double dpmf(int d, double k, double a, double b)
{
    if (k != floor(k)) return 0.0;
    switch (d) {
    case D_BINOM: if (k < 0 || k > a) return 0.0;
        if (b == 0.0) return k == 0 ? 1.0 : 0.0;
        if (b == 1.0) return k == a ? 1.0 : 0.0;
        return exp(rxsp_lgamma(a + 1.0) - rxsp_lgamma(k + 1.0) - rxsp_lgamma(a - k + 1.0) + k * log(b) + (a - k) * rx_log1p(-b));
    case D_POISSON: if (k < 0) return 0.0;
        if (a == 0.0) return k == 0 ? 1.0 : 0.0;
        return exp(k * log(a) - a - rxsp_lgamma(k + 1.0));
    case D_GEOM: if (k < 1) return 0.0;
        if (a == 1.0) return k == 1 ? 1.0 : 0.0;
        return exp((k - 1.0) * rx_log1p(-a)) * a;
    }
    return nanv();
}
static double dcdf(int d, double k, double a, double b)
{
    k = floor(k);
    switch (d) {
    case D_BINOM: if (k < 0) return 0.0; if (k >= a) return 1.0; return rxsp_betainc(a - k, k + 1.0, 1.0 - b);
    case D_POISSON: if (k < 0) return 0.0; return rxsp_gammaincc(k + 1.0, a);
    case D_GEOM: if (k < 1) return 0.0; if (a == 1.0) return 1.0; return -rx_expm1(k * rx_log1p(-a));
    }
    return nanv();
}
static double dsf(int d, double k, double a, double b)
{
    k = floor(k);
    switch (d) {
    case D_BINOM: if (k < 0) return 1.0; if (k >= a) return 0.0; return rxsp_betainc(k + 1.0, a - k, b);
    case D_POISSON: if (k < 0) return 1.0; return rxsp_gammainc(k + 1.0, a);
    case D_GEOM: if (k < 1) return 1.0; if (a == 1.0) return 0.0; return exp(k * rx_log1p(-a));
    }
    return nanv();
}
static double dppf(int d, double q, double a, double b)
{
    double lo, hi, mid;
    if (isnanv(q) || q < 0.0 || q > 1.0) return nanv();
    if (d == D_GEOM) {
        double v;
        if (q == 1.0) return infv();
        if (q == 0.0) return 0.0;
        v = ceil(rx_log1p(-q) / rx_log1p(-a));
        if (v > 0 && dcdf(d, v - 1.0, a, b) >= q) v -= 1.0;
        return v;
    }
    if (q == 0.0) return -1.0;
    if (d == D_BINOM) { lo = -1.0; hi = a; if (q == 1.0) return a; }
    else { if (q == 1.0) return infv(); lo = -1.0; hi = 1.0; while (dcdf(d, hi, a, b) < q) hi *= 2.0; }
    while (hi - lo > 1.0) { mid = floor(0.5 * (lo + hi)); if (dcdf(d, mid, a, b) >= q) hi = mid; else lo = mid; }
    return hi;
}
/* parameter validation: NaN when invalid */
static int okpar(int d, double a, double b, double scale)
{
    switch (d) {
    case D_NORM: case D_EXPON: case D_UNIFORM: return scale > 0;
    case D_T: case D_CHI2: case D_GAMMA: case D_LOGNORM: return a > 0 && scale > 0;
    case D_F: case D_BETA: return a > 0 && b > 0 && scale > 0;
    case D_BINOM: return a >= 0 && a == floor(a) && b >= 0 && b <= 1;
    case D_POISSON: return a >= 0;
    case D_GEOM: return a > 0 && a <= 1;
    }
    return 0;
}
#define CHECKD  int d = did(nm); if (d == D_NONE) return nanv(); if (!okpar(d, a, b, scale)) return nanv(); if (isnanv(x)) return x;
double rxsp_dist_pdf(const char *nm, double x, double a, double b, double loc, double scale)
{
    CHECKD
    if (d >= D_BINOM) return dpmf(d, x - loc, a, b);
    return std_pdf(d, (x - loc) / scale, a, b) / scale;
}
double rxsp_dist_cdf(const char *nm, double x, double a, double b, double loc, double scale)
{
    CHECKD
    if (d >= D_BINOM) return dcdf(d, x - loc, a, b);
    return std_cdf(d, (x - loc) / scale, a, b);
}
double rxsp_dist_sf(const char *nm, double x, double a, double b, double loc, double scale)
{
    CHECKD
    if (d >= D_BINOM) return dsf(d, x - loc, a, b);
    return std_sf(d, (x - loc) / scale, a, b);
}
double rxsp_dist_ppf(const char *nm, double x, double a, double b, double loc, double scale)
{
    double v;
    CHECKD
    if (d >= D_BINOM) return dppf(d, x, a, b) + loc;
    v = std_ppf(d, x, a, b);
    return v * scale + loc;
}
double rxsp_dist_mean(const char *nm, double a, double b, double loc, double scale)
{
    double x = 0.0, m; int d = did(nm);
    if (d == D_NONE || !okpar(d, a, b, scale)) return nanv();
    switch (d) {
    case D_NORM: m = 0.0; break;
    case D_T: m = a > 1 ? 0.0 : nanv(); break;
    case D_CHI2: m = a; break;
    case D_F: m = b > 2 ? b / (b - 2.0) : nanv(); break;
    case D_GAMMA: m = a; break;
    case D_BETA: m = a / (a + b); break;
    case D_EXPON: m = 1.0; break;
    case D_UNIFORM: m = 0.5; break;
    case D_LOGNORM: m = exp(a * a / 2.0); break;
    case D_BINOM: return a * b + loc;
    case D_POISSON: return a + loc;
    default: return 1.0 / a + loc;
    }
    (void)x;
    return m * scale + loc;
}
double rxsp_dist_var(const char *nm, double a, double b, double loc, double scale)
{
    double v; int d = did(nm); (void)loc;
    if (d == D_NONE || !okpar(d, a, b, scale)) return nanv();
    switch (d) {
    case D_NORM: v = 1.0; break;
    case D_T: v = a > 2 ? a / (a - 2.0) : (a > 1 ? infv() : nanv()); break;
    case D_CHI2: v = 2.0 * a; break;
    case D_F: v = b > 4 ? 2.0 * b * b * (a + b - 2.0) / (a * (b - 2.0) * (b - 2.0) * (b - 4.0)) : (b > 2 ? infv() : nanv()); break;
    case D_GAMMA: v = a; break;
    case D_BETA: v = a * b / ((a + b) * (a + b) * (a + b + 1.0)); break;
    case D_EXPON: v = 1.0; break;
    case D_UNIFORM: v = 1.0 / 12.0; break;
    case D_LOGNORM: v = rx_expm1(a * a) * exp(a * a); break;
    case D_BINOM: return a * b * (1.0 - b);
    case D_POISSON: return a;
    default: return (1.0 - a) / (a * a);
    }
    return v * scale * scale;
}
/* ---------------------------------------------------------------- random variates (legacy MT19937 stream) */
static double r_exp(rxnp_rng *g) { return -log(1.0 - rxnp_rng_random(g)); }
static double r_gamma(rxnp_rng *g, double shape)
{
    double u, v, x, y, b, c;
    if (shape == 1.0) return r_exp(g);
    if (shape == 0.0) return 0.0;
    if (shape < 1.0) {
        for (;;) {
            u = rxnp_rng_random(g); v = r_exp(g);
            if (u <= 1.0 - shape) { x = pow(u, 1.0 / shape); if (x <= v) return x; }
            else { y = -log((1.0 - u) / shape); x = pow(1.0 - shape + shape * y, 1.0 / shape); if (x <= v + y) return x; }
        }
    }
    b = shape - 1.0 / 3.0; c = 1.0 / sqrt(9.0 * b);
    for (;;) {
        do { x = rxnp_rng_gauss(g); v = 1.0 + c * x; } while (v <= 0.0);
        v = v * v * v; u = rxnp_rng_random(g);
        if (u < 1.0 - 0.0331 * (x * x) * (x * x)) return b * v;
        if (log(u) < 0.5 * x * x + b * (1.0 - v + log(v))) return b * v;
    }
}
static double r_beta(rxnp_rng *g, double a, double b)
{
    if (a <= 1.0 && b <= 1.0) {
        for (;;) {
            double u = rxnp_rng_random(g), v = rxnp_rng_random(g), x = pow(u, 1.0 / a), y = pow(v, 1.0 / b), xpy = x + y;
            if (xpy <= 1.0 && u + v > 0.0) {
                if (xpy > 0.0) return x / xpy;
                { double lx = log(u) / a, ly = log(v) / b, lm = lx > ly ? lx : ly; lx -= lm; ly -= lm; return exp(lx - log(exp(lx) + exp(ly))); }
            }
        }
    }
    { double ga = r_gamma(g, a), gb = r_gamma(g, b); return ga / (ga + gb); }
}
static long r_binom_inv(rxnp_rng *g, long n, double p)
{
    double q = 1.0 - p, qn = exp((double)n * log(q)), np = (double)n * p, bound, px, u; long x = 0;
    bound = np + 10.0 * sqrt(np * q + 1.0); if ((double)n < bound) bound = (double)n;
    px = qn; u = rxnp_rng_random(g);
    while (u > px) {
        x++;
        if ((double)x > bound) { x = 0; px = qn; u = rxnp_rng_random(g); }
        else { u -= px; px = ((double)(n - x + 1) * p * px) / ((double)x * q); }
    }
    return x;
}
/* inversion by the cdf for parameters where numpy uses BTPE / PTRS: valid sample, not bit-identical */
static double r_cdf_inv(rxnp_rng *g, int d, double a, double b) { return dppf(d, rxnp_rng_random(g), a, b); }
static long r_binom(rxnp_rng *g, long n, double p)
{
    if (p <= 0.5) { if (p * (double)n <= 30.0) return r_binom_inv(g, n, p); return (long)r_cdf_inv(g, D_BINOM, (double)n, p); }
    if ((1.0 - p) * (double)n <= 30.0) return n - r_binom_inv(g, n, 1.0 - p);
    return (long)r_cdf_inv(g, D_BINOM, (double)n, p);
}
static long r_poisson(rxnp_rng *g, double lam)
{
    if (lam >= 10.0) return (long)r_cdf_inv(g, D_POISSON, lam, 0.0);
    if (lam == 0.0) return 0;
    { double en = exp(-lam), prod = 1.0; long x = 0; for (;;) { prod *= rxnp_rng_random(g); if (prod > en) x++; else return x; } }
}
static long r_geom(rxnp_rng *g, double p)
{
    if (p >= 0.333333333333333333333333) {
        double sum, prod, q, u; long x = 1;
        sum = prod = p; q = 1.0 - p; u = rxnp_rng_random(g);
        while (u > sum) { prod *= q; sum += prod; x++; }
        return x;
    }
    return (long)ceil(log(1.0 - rxnp_rng_random(g)) / log(1.0 - p));
}
rxnp *rxsp_dist_rvs(const char *nm, rxnp_rng *g, long n, double a, double b, double loc, double scale)
{
    int d = did(nm); long k, shp[1]; rxnp *r; double x;
    if (d == D_NONE) { rxsp_fail("unknown distribution"); return NULL; }
    if (!okpar(d, a, b, scale)) { rxsp_fail("invalid distribution parameters"); return NULL; }
    if (n < 0) { rxsp_fail("rvs: negative size"); return NULL; }
    shp[0] = n;
    if (d >= D_BINOM) {
        r = rxnp_new(RXNP_I64, 1, shp); if (!r) return NULL;
        for (k = 0; k < n; k++) {
            long v = d == D_BINOM ? r_binom(g, (long)a, b) : (d == D_POISSON ? r_poisson(g, a) : r_geom(g, a));
            r->i[k] = v + (long)loc;
        }
        return r;
    }
    r = rxnp_new(RXNP_F64, 1, shp); if (!r) return NULL;
    for (k = 0; k < n; k++) {
        switch (d) {
        case D_NORM: x = rxnp_rng_gauss(g); break;
        case D_T: { double num = rxnp_rng_gauss(g), den = r_gamma(g, a / 2.0); x = sqrt(a / 2.0) * num / sqrt(den); } break;
        case D_CHI2: x = 2.0 * r_gamma(g, a / 2.0); break;
        case D_F: { double c1 = 2.0 * r_gamma(g, a / 2.0), c2 = 2.0 * r_gamma(g, b / 2.0); x = (c1 * b) / (c2 * a); } break;
        case D_GAMMA: x = r_gamma(g, a); break;
        case D_BETA: x = r_beta(g, a, b); break;
        case D_EXPON: x = r_exp(g); break;
        case D_UNIFORM: x = rxnp_rng_random(g); break;
        default: x = exp(a * rxnp_rng_gauss(g)); break;
        }
        r->f[k] = x * scale + loc;
    }
    return r;
}
/* ---------------------------------------------------------------- descriptive statistics and tests */
static double ev(const rxnp *a, long k) { return a->dt == RXNP_F64 ? a->f[k] : (double)a->i[k]; }
static double mean_of(const rxnp *x) { double s = 0.0; long k; for (k = 0; k < x->size; k++) s += ev(x, k); return s / (double)x->size; }
static void moments(const rxnp *x, double *m, double *m2, double *m3, double *m4)
{
    long k, n = x->size; double mu = mean_of(x), s2 = 0, s3 = 0, s4 = 0;
    for (k = 0; k < n; k++) { double d = ev(x, k) - mu, d2 = d * d; s2 += d2; s3 += d2 * d; s4 += d2 * d2; }
    *m = mu; *m2 = s2 / n; *m3 = s3 / n; *m4 = s4 / n;
}
double rxsp_skew(const rxnp *x)
{
    double m, m2, m3, m4;
    if (x->size < 1) return nanv();
    moments(x, &m, &m2, &m3, &m4);
    return m2 == 0.0 ? nanv() : m3 / pow(m2, 1.5);
}
double rxsp_kurtosis(const rxnp *x)
{
    double m, m2, m3, m4;
    if (x->size < 1) return nanv();
    moments(x, &m, &m2, &m3, &m4);
    return m2 == 0.0 ? nanv() : m4 / (m2 * m2) - 3.0;
}
rxnp *rxsp_describe(const rxnp *x)
{
    long n = x->size, k, shp[1]; rxnp *r; double mn, mx, m, m2, m3, m4;
    if (n < 1) { rxsp_fail("describe: empty input"); return NULL; }
    mn = mx = ev(x, 0);
    for (k = 1; k < n; k++) { double v = ev(x, k); if (v < mn) mn = v; if (v > mx) mx = v; }
    moments(x, &m, &m2, &m3, &m4);
    shp[0] = 7; r = rxnp_new(RXNP_F64, 1, shp); if (!r) return NULL;
    r->f[0] = (double)n; r->f[1] = mn; r->f[2] = mx; r->f[3] = m;
    r->f[4] = n > 1 ? m2 * n / (n - 1.0) : nanv();
    r->f[5] = m2 == 0.0 ? nanv() : m3 / pow(m2, 1.5);
    r->f[6] = m2 == 0.0 ? nanv() : m4 / (m2 * m2) - 3.0;
    return r;
}
rxnp *rxsp_zscore(const rxnp *x, int ddof)
{
    long n = x->size, k; double m = mean_of(x), s = 0.0, sd; rxnp *r;
    if (n < 1 || n - ddof <= 0) { rxsp_fail("zscore: not enough samples for ddof"); return NULL; }
    for (k = 0; k < n; k++) { double d = ev(x, k) - m; s += d * d; }
    sd = sqrt(s / (double)(n - ddof));
    r = rxnp_new(RXNP_F64, x->ndim, x->shape); if (!r) return NULL;
    for (k = 0; k < n; k++) r->f[k] = (ev(x, k) - m) / sd;
    return r;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : (x > y ? 1 : 0); }
double rxsp_percentile(const rxnp *x, double q)
{
    long n = x->size, k, i; double *v, vi, t, a, b, r;
    if (n < 1 || !(q >= 0.0 && q <= 100.0)) { rxsp_fail("percentile: empty input or q outside [0, 100]"); return nanv(); }
    v = (double *)malloc((size_t)n * sizeof(double)); if (!v) { rxsp_fail("out of memory"); return nanv(); }
    for (k = 0; k < n; k++) v[k] = ev(x, k);
    qsort(v, (size_t)n, sizeof(double), cmpd);
    vi = q / 100.0 * (double)(n - 1); i = (long)floor(vi); t = vi - (double)i;
    if (i >= n - 1) { r = v[n - 1]; free(v); return r; }
    a = v[i]; b = v[i + 1];
    r = t >= 0.5 ? b - (b - a) * (1.0 - t) : a + (b - a) * t;
    free(v); return r;
}
rxnp *rxsp_correlate(const rxnp *a, const rxnp *v, const char *mode)
{
    const rxnp *A = a, *V = v; long n1, n, k0, cnt, k, m, shp[1], j; int inverted = 0; rxnp *r;
    if (a->size < 1 || v->size < 1) { rxsp_fail("correlate: inputs must not be empty"); return NULL; }
    if (v->size > a->size) { A = v; V = a; inverted = 1; }
    n1 = A->size; n = V->size;
    if (strcmp(mode, "valid") == 0) { k0 = 0; cnt = n1 - n + 1; }
    else if (strcmp(mode, "same") == 0) { k0 = -(n / 2); cnt = n1; }
    else if (strcmp(mode, "full") == 0) { k0 = -(n - 1); cnt = n1 + n - 1; }
    else { rxsp_fail("correlate: mode must be valid, same or full"); return NULL; }
    shp[0] = cnt; r = rxnp_new(RXNP_F64, 1, shp); if (!r) return NULL;
    for (j = 0; j < cnt; j++) {
        double s = 0.0; k = k0 + j;
        for (m = 0; m < n; m++) if (m + k >= 0 && m + k < n1) s += ev(A, m + k) * ev(V, m);
        r->f[inverted ? cnt - 1 - j : j] = s;
    }
    return r;
}
int rxsp_ttest_1samp(const rxnp *x, double popmean, double *t, double *p)
{
    long n = x->size, k; double m, v = 0.0;
    if (n < 2) return rxsp_fail("ttest_1samp: need at least 2 values");
    m = mean_of(x);
    for (k = 0; k < n; k++) { double d = ev(x, k) - m; v += d * d; }
    v /= (double)(n - 1);
    *t = (m - popmean) / sqrt(v / (double)n);
    *p = 2.0 * rxsp_t_sf(fabs(*t), (double)(n - 1));
    return 1;
}
int rxsp_ttest_welch(const rxnp *a, const rxnp *b, double *t, double *p, double *dfo)
{
    long n1 = a->size, n2 = b->size, k; double m1, m2, v1 = 0, v2 = 0, s1, s2, df;
    if (n1 < 2 || n2 < 2) return rxsp_fail("ttest_ind: each sample needs at least 2 values");
    m1 = mean_of(a); m2 = mean_of(b);
    for (k = 0; k < n1; k++) { double d = ev(a, k) - m1; v1 += d * d; }
    for (k = 0; k < n2; k++) { double d = ev(b, k) - m2; v2 += d * d; }
    v1 /= (double)(n1 - 1); v2 /= (double)(n2 - 1);
    s1 = v1 / n1; s2 = v2 / n2;
    df = (s1 + s2) * (s1 + s2) / (s1 * s1 / (n1 - 1.0) + s2 * s2 / (n2 - 1.0));
    *t = (m1 - m2) / sqrt(s1 + s2); *p = 2.0 * rxsp_t_sf(fabs(*t), df); *dfo = df;
    return 1;
}
static double *ranks(const rxnp *x)
{
    long n = x->size, i, j, k; double *r = (double *)malloc((size_t)n * sizeof(double)); long *ord = (long *)malloc((size_t)n * sizeof(long));
    if (!r || !ord) { free(r); free(ord); return NULL; }
    for (i = 0; i < n; i++) ord[i] = i;
    /* insertion-merge: simple stable sort by value (n is small in practice; use shell-free merge via qsort on pairs) */
    {   double *key = (double *)malloc((size_t)n * 2 * sizeof(double)); long q;
        if (!key) { free(r); free(ord); return NULL; }
        for (i = 0; i < n; i++) { key[2 * i] = ev(x, i); key[2 * i + 1] = (double)i; }
        qsort(key, (size_t)n, 2 * sizeof(double), cmpd);
        for (q = 0; q < n; q++) ord[q] = (long)key[2 * q + 1];
        free(key);
    }
    for (i = 0; i < n; i = j) {
        j = i + 1; while (j < n && ev(x, ord[j]) == ev(x, ord[i])) j++;
        for (k = i; k < j; k++) r[ord[k]] = (double)(i + j + 1) / 2.0;      /* average of ranks i+1 .. j */
    }
    free(ord); return r;
}
int rxsp_spearmanr(const rxnp *x, const rxnp *y, double *rho, double *p)
{
    long n = x->size, k; double *rx, *ry, mx = 0, my = 0, sxy = 0, sxx = 0, syy = 0, r, t;
    if (n != y->size || n < 3) return rxsp_fail("spearmanr: need two equal-length samples of at least 3");
    rx = ranks(x); ry = ranks(y); if (!rx || !ry) { free(rx); free(ry); return rxsp_fail("out of memory"); }
    for (k = 0; k < n; k++) { mx += rx[k]; my += ry[k]; } mx /= n; my /= n;
    for (k = 0; k < n; k++) { double dx = rx[k] - mx, dy = ry[k] - my; sxy += dx * dy; sxx += dx * dx; syy += dy * dy; }
    free(rx); free(ry);
    r = sxy / sqrt(sxx * syy); if (r > 1.0) r = 1.0; if (r < -1.0) r = -1.0;
    *rho = r;
    if (fabs(r) >= 1.0) *p = 0.0;
    else { t = r * sqrt((n - 2.0) / ((r + 1.0) * (1.0 - r))); *p = 2.0 * rxsp_t_sf(fabs(t), n - 2.0); }
    return 1;
}
rxnp *rxsp_linregress(const rxnp *x, const rxnp *y)
{
    long n = x->size, k, shp[1]; double mx, my, sxx = 0, sxy = 0, syy = 0, r, slope, df, t, p, se, ise; rxnp *o;
    if (n != y->size || n < 3) { rxsp_fail("linregress: need two equal-length samples of at least 3"); return NULL; }
    mx = mean_of(x); my = mean_of(y);
    for (k = 0; k < n; k++) { double dx = ev(x, k) - mx, dy = ev(y, k) - my; sxx += dx * dx; sxy += dx * dy; syy += dy * dy; }
    sxx /= n; sxy /= n; syy /= n;
    if (sxx == 0.0) { rxsp_fail("linregress: x values are identical"); return NULL; }
    r = sxy / sqrt(sxx * syy); if (r > 1.0) r = 1.0; if (r < -1.0) r = -1.0;
    slope = sxy / sxx; df = n - 2.0;
    t = r * sqrt(df / ((1.0 - r + 1e-20) * (1.0 + r + 1e-20)));
    p = 2.0 * rxsp_t_sf(fabs(t), df);
    se = sqrt((1.0 - r * r) * syy / sxx / df);
    ise = se * sqrt(sxx + mx * mx);
    shp[0] = 6; o = rxnp_new(RXNP_F64, 1, shp); if (!o) return NULL;
    o->f[0] = slope; o->f[1] = my - slope * mx; o->f[2] = r; o->f[3] = p; o->f[4] = se; o->f[5] = ise;
    return o;
}
int rxsp_chi2_contingency(const rxnp *tab, int correction, double *chi2, double *p, long *dof, rxnp **expected)
{
    long R, C, i, j; double tot = 0.0, *rs, *cs, st = 0.0; rxnp *e;
    if (tab->ndim != 2) return rxsp_fail("chi2_contingency: table must be 2-d");
    R = tab->shape[0]; C = tab->shape[1];
    rs = (double *)calloc((size_t)R, sizeof(double)); cs = (double *)calloc((size_t)C, sizeof(double));
    e = rxnp_new(RXNP_F64, 2, tab->shape);
    if (!rs || !cs || !e) { free(rs); free(cs); if (e) rxnp_release(e); return rxsp_fail("out of memory"); }
    for (i = 0; i < R; i++) for (j = 0; j < C; j++) { double v = ev(tab, i * C + j); rs[i] += v; cs[j] += v; tot += v; }
    for (i = 0; i < R; i++) for (j = 0; j < C; j++) e->f[i * C + j] = rs[i] * cs[j] / tot;
    *dof = (R - 1) * (C - 1);
    for (i = 0; i < R; i++) for (j = 0; j < C; j++) if (e->f[i * C + j] == 0.0) { free(rs); free(cs); rxnp_release(e); return rxsp_fail("chi2_contingency: zero expected frequency"); }
    if (*dof == 0) { *chi2 = 0.0; *p = 1.0; }
    else {
        for (i = 0; i < R * C; i++) {
            double o = ev(tab, i), ex = e->f[i], dd;
            if (correction && *dof == 1) { dd = ex - o; { double m = fabs(dd) < 0.5 ? fabs(dd) : 0.5; o += (dd > 0 ? 1.0 : (dd < 0 ? -1.0 : 0.0)) * m; } }
            st += (o - ex) * (o - ex) / ex;
        }
        *chi2 = st; *p = rxsp_gammaincc((double)*dof / 2.0, st / 2.0);
    }
    free(rs); free(cs);
    if (expected) *expected = e; else rxnp_release(e);
    return 1;
}
