import numpy as np
from scipy import stats, optimize, integrate, interpolate, linalg, special, spatial, signal, constants, sparse
import scipy.sparse.linalg


def fmt(x):
    if isinstance(x, bool):
        return str(x)
    if isinstance(x, int):
        return str(x)
    if isinstance(x, float):
        v = float(x)
        if v != v:
            return "nan"
        if v == float("inf"):
            return "inf"
        if v == float("-inf"):
            return "-inf"
        s = "%.5f" % v
        if s == "-0.00000":
            s = "0.00000"
        return s
    if isinstance(x, np.ndarray):
        return fmt(x.tolist())
    if isinstance(x, (list, tuple)):
        return "[" + ", ".join([fmt(v) for v in x]) + "]"
    return str(x)


def P(*a):
    print(" ".join([fmt(v) for v in a]))


# --- distributions
P(stats.norm.cdf(1.3), stats.norm.pdf(0.4), stats.norm.ppf(0.9), stats.norm.sf(2.0), stats.norm.logpdf(1.0))
P(stats.norm.cdf(3, loc=1, scale=2), stats.norm.interval(0.9), stats.norm.stats(moments="mv"))
P(stats.t.cdf(1.5, 7), stats.t.ppf(0.975, 12), stats.t.pdf(0.3, 4), stats.t.sf(2.2, 30))
P(stats.chi2.cdf(4.0, 3), stats.chi2.ppf(0.95, 5), stats.chi2.pdf(2.0, 4), stats.chi2.sf(7.0, 2))
P(stats.f.cdf(2.5, 3, 10), stats.f.ppf(0.95, 4, 12), stats.f.sf(3.0, 5, 8))
P(stats.expon.cdf(1.2, scale=2.0), stats.expon.ppf(0.5), stats.uniform.cdf(0.3, 0, 2), stats.uniform.ppf(0.25, 1, 3))
P(stats.gamma.cdf(2.0, 3.0), stats.gamma.ppf(0.5, 2.5), stats.beta.cdf(0.4, 2, 5), stats.beta.ppf(0.7, 3, 2))
P(stats.lognorm.cdf(2.0, 0.5), stats.laplace.cdf(0.7), stats.cauchy.cdf(1.0), stats.logistic.ppf(0.8), stats.weibull_min.cdf(1.5, 2.0))
P(stats.binom.pmf(3, 10, 0.4), stats.binom.cdf(4, 10, 0.4), stats.binom.ppf(0.5, 10, 0.4))
P(stats.poisson.pmf(2, 3.0), stats.poisson.cdf(4, 3.0), stats.poisson.ppf(0.9, 3.0), stats.geom.pmf(3, 0.3), stats.randint.cdf(3, 1, 7))
fz = stats.norm(loc=10, scale=3)
P(fz.mean(), fz.std(), fz.cdf(12), fz.ppf(0.1), fz.var())

# --- descriptive
x = [2.1, 3.4, 1.9, 5.6, 4.4, 3.8, 2.7, 6.1, 4.0, 3.3]
y = [1.8, 3.9, 2.2, 5.1, 4.9, 3.2, 2.5, 6.6, 3.7, 3.9]
P(stats.skew(x), stats.kurtosis(x), stats.moment(x, 3), stats.variation(x), stats.sem(x))
P(stats.gmean(x), stats.hmean(x), stats.trim_mean(x, 0.1), stats.iqr(x), stats.median_abs_deviation(x))
P(stats.zscore(x[:4]), stats.rankdata([3, 1, 4, 1, 5]), stats.scoreatpercentile(x, 30), stats.percentileofscore(x, 3.8))
d = stats.describe(x)
P(d.nobs, d.minmax, d.mean, d.variance, d.skewness, d.kurtosis)
P(stats.entropy([0.2, 0.3, 0.5]), stats.entropy([0.2, 0.3, 0.5], [0.4, 0.4, 0.2]))
P(stats.mode([1, 2, 2, 3, 3, 3, 4]).mode, stats.tmean(x, (2, 5)), stats.tstd(x), stats.tvar(x))

# --- tests
r = stats.pearsonr(x, y)
P(r.statistic, r.pvalue)
r = stats.spearmanr(x, y)
P(r.statistic, r.pvalue)
r = stats.kendalltau(x, y)
P(r.statistic, r.pvalue)
r = stats.linregress(x, y)
P(r.slope, r.intercept, r.rvalue, r.pvalue, r.stderr)
r = stats.ttest_1samp(x, 3.0)
P(r.statistic, r.pvalue)
r = stats.ttest_ind(x, y)
P(r.statistic, r.pvalue)
r = stats.ttest_ind(x, y, equal_var=False)
P(r.statistic, r.pvalue)
r = stats.ttest_rel(x, y)
P(r.statistic, r.pvalue)
r = stats.chisquare([16, 18, 16, 14, 12, 12])
P(r.statistic, r.pvalue)
r = stats.chi2_contingency([[10, 20], [30, 25]])
P(r[0], r[1], r[2])
r = stats.f_oneway(x, y, [3.0, 4.0, 5.0, 2.0, 6.0])
P(r.statistic, r.pvalue)
r = stats.fisher_exact([[8, 2], [1, 5]])
P(r[0], r[1])
r = stats.mannwhitneyu(x, y)
P(r.statistic, r.pvalue)
r = stats.ks_2samp(x, [1.0, 2.0, 3.0, 7.0, 8.0])
P(r.statistic, r.pvalue)
r = stats.jarque_bera(x)
P(r.statistic, r.pvalue)
P(stats.binomtest(7, 10, 0.5).pvalue)

# --- special
P(special.gamma(4.5), special.gammaln(10.0), special.digamma(2.5), special.beta(2.5, 3.5), special.betaln(2, 3))
P(special.erf(0.7), special.erfc(0.7), special.erfinv(0.5), special.ndtr(1.0), special.ndtri(0.3))
P(special.comb(10, 3), special.comb(5, 2, exact=True), special.perm(6, 2), special.factorial(6), special.factorial2(7))
P(special.gammainc(2.0, 1.5), special.gammaincc(2.0, 1.5), special.betainc(2.0, 3.0, 0.4), special.expit(0.8), special.logit(0.3))
P(special.logsumexp([1.0, 2.0, 3.0]), special.softmax([1.0, 2.0, 3.0]), special.xlogy(2.0, 3.0), special.entr(0.3))
P(special.ellipk(0.5), special.ellipe(0.5), special.boxcox(2.0, 0.5), special.cbrt(27.0))

# --- optimize
res = optimize.minimize(lambda p: (p[0] - 1) ** 2 + (p[1] + 2) ** 2 + 3, [0.0, 0.0], method="Nelder-Mead")
P(res.x, res.fun, res.success)
res = optimize.minimize(lambda p: (p[0] - 1) ** 2 + (p[1] + 2) ** 2 + 3, [0.0, 0.0], method="BFGS")
P(res.x, res.fun, res.success)
res = optimize.minimize(lambda p: (p[0] - 1) ** 2 + (p[1] + 2) ** 2, [3.0, 3.0], bounds=[(2, 5), (0, 4)], method="L-BFGS-B")
P(res.x, res.fun)
res = optimize.minimize_scalar(lambda t: (t - 2.5) ** 2 + 1)
P(res.x, res.fun)
res = optimize.minimize_scalar(lambda t: np.cos(t), bounds=(2, 5), method="bounded")
P(res.x, res.fun)
P(optimize.brentq(lambda t: t ** 3 - 2, 0, 3), optimize.bisect(lambda t: t ** 2 - 3, 0, 3), optimize.newton(lambda t: t ** 2 - 5, 2.0))
P(optimize.ridder(lambda t: np.exp(t) - 4, 0, 3), optimize.fsolve(lambda v: [v[0] ** 2 + v[1] ** 2 - 4, v[0] - v[1]], [1.0, 1.0]))
popt, pcov = optimize.curve_fit(lambda t, a, b: a * np.exp(-b * t), [0, 1, 2, 3, 4], [5.0, 3.0, 1.9, 1.1, 0.7], p0=[4.0, 0.5])
P(popt, np.sqrt(np.diag(pcov)))
res = optimize.least_squares(lambda p: [p[0] - 1, 10 * (p[1] - p[0] ** 2)], [-1.0, 1.0])
P(res.x, res.cost)
res = optimize.linprog([-1, -2], A_ub=[[1, 1], [1, 3]], b_ub=[4, 6], bounds=[(0, None), (0, None)])
P(res.x, res.fun, res.success)

# --- integrate
P(integrate.quad(lambda t: np.sin(t), 0, np.pi)[0], integrate.quad(lambda t: np.exp(-t * t), -np.inf, np.inf)[0])
P(integrate.quad(lambda t: 1.0 / np.sqrt(t), 0, 1)[0], integrate.dblquad(lambda b, a: a * b, 0, 1, 0, 2)[0])
xs = [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0]
ys = [v * v for v in xs]
P(integrate.trapezoid(ys, xs), integrate.simpson(ys, x=xs), integrate.cumulative_trapezoid(ys, x=xs), integrate.romb([1.0, 2.0, 5.0, 10.0, 17.0], dx=1.0))
P(integrate.fixed_quad(lambda t: t ** 3, 0, 2)[0])
sol = integrate.solve_ivp(lambda t, y: [-0.5 * y[0], y[0] - y[1]], (0, 4), [2.0, 0.0], rtol=1e-8, atol=1e-10)
P(sol.y[0][-1], sol.y[1][-1], sol.success)
sol = integrate.solve_ivp(lambda t, y: -y, (0, 2), [1.0], t_eval=[0.5, 1.0, 2.0], rtol=1e-8, atol=1e-10)
P(sol.t, sol.y[0])

# --- interpolate
cs = interpolate.CubicSpline([0, 1, 2, 3, 4], [0.0, 1.0, 0.0, 1.0, 0.0])
P(float(cs(0.5)), float(cs(2.7)), float(cs(1.5, 1)), float(cs.integrate(0, 4)))
f = interpolate.interp1d([0, 1, 2, 3], [0.0, 2.0, 1.0, 5.0], kind="quadratic")
P(float(f(1.5)))
f = interpolate.interp1d([0, 1, 2, 3], [0.0, 2.0, 1.0, 5.0], kind="cubic")
P(float(f(2.2)))
P(float(interpolate.PchipInterpolator([0, 1, 2, 3], [0.0, 2.0, 1.0, 5.0])(1.7)))
P(float(interpolate.InterpolatedUnivariateSpline([0, 1, 2, 3, 4], [0.0, 1.0, 4.0, 9.0, 16.0])(2.5)))
P(float(interpolate.lagrange([0, 1, 2], [1.0, 3.0, 11.0])(1.5)), float(interpolate.BarycentricInterpolator([0, 1, 2], [1.0, 3.0, 11.0])(0.5)))

# --- linalg
A = np.array([[4.0, 3.0, 2.0], [3.0, 5.0, 1.0], [2.0, 1.0, 6.0]])
b = np.array([1.0, 2.0, 3.0])
P(linalg.solve(A, b), linalg.det(A), linalg.inv(A), linalg.norm(A), linalg.norm(b, 1))
w = linalg.eigh(A)
P(w[0])
P(linalg.cholesky(A), linalg.lu_solve(linalg.lu_factor(A), b))
q, rr = linalg.qr(A)
P(np.abs(rr), linalg.svdvals(A))
P(linalg.expm(np.array([[0.0, 1.0], [-1.0, 0.0]])), linalg.sqrtm(np.array([[4.0, 0.0], [0.0, 9.0]])))
P(linalg.pinv(np.array([[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]])), linalg.null_space(np.array([[1.0, 1.0], [1.0, 1.0]])).shape)
P(linalg.solve_triangular(np.array([[2.0, 0.0], [1.0, 3.0]]), [2.0, 5.0], lower=True), linalg.toeplitz([1, 2, 3]), linalg.hilbert(2))
P(linalg.lstsq(np.array([[1.0, 1.0], [1.0, 2.0], [1.0, 3.0]]), np.array([1.0, 2.0, 2.0]))[0])

# --- spatial
pa = [1.0, 2.0, 3.0]
pb = [4.0, 0.0, 3.0]
dd = spatial.distance
P(dd.euclidean(pa, pb), dd.cityblock(pa, pb), dd.chebyshev(pa, pb), dd.cosine(pa, pb), dd.minkowski(pa, pb, 3), dd.sqeuclidean(pa, pb))
P(dd.correlation(pa, [2.0, 1.0, 5.0]), dd.braycurtis(pa, pb), dd.canberra(pa, pb), dd.hamming([1, 0, 1], [1, 1, 0]), dd.jaccard([1, 0, 1], [1, 1, 0]))
P(dd.cdist([[0.0, 0.0], [1.0, 1.0]], [[1.0, 0.0], [2.0, 2.0]]), dd.pdist([[0.0, 0.0], [3.0, 4.0], [6.0, 8.0]]), dd.squareform(dd.pdist([[0.0, 0.0], [3.0, 4.0], [6.0, 8.0]])))
P(dd.mahalanobis([1.0, 0.0], [0.0, 1.0], [[2.0, 0.0], [0.0, 2.0]]))

# --- signal
P(signal.convolve([1.0, 2.0, 3.0], [0.0, 1.0, 0.5]), signal.convolve([1.0, 2.0, 3.0, 4.0], [1.0, 1.0], mode="same"), signal.correlate([1.0, 2.0, 3.0], [1.0, 1.0], mode="valid"))
P(signal.windows.hann(5), signal.windows.hamming(4), signal.get_window("blackman", 5, fftbins=False))
P(signal.lfilter([0.5, 0.5], [1.0], [1.0, 2.0, 3.0, 4.0]), signal.lfilter([1.0], [1.0, -0.5], [1.0, 0.0, 0.0, 0.0]))
P(signal.find_peaks([0.0, 1.0, 0.0, 2.0, 0.0, 3.0, 1.0])[0], signal.detrend([1.0, 2.0, 4.0, 7.0]))
P(signal.savgol_filter([1.0, 4.0, 9.0, 16.0, 25.0, 36.0, 49.0], 5, 2))

# --- constants
P(constants.pi, constants.c, constants.golden, constants.kilo, constants.hour, constants.g, constants.convert_temperature(100.0, "Celsius", "Kelvin"))

# --- sparse
m = sparse.csr_matrix(np.array([[1.0, 0.0, 2.0], [0.0, 0.0, 3.0], [4.0, 5.0, 0.0]]))
P(m.shape, m.nnz, m.toarray(), m.dot(np.array([1.0, 2.0, 3.0])), m.T.toarray(), m.sum(), m.diagonal())
P(sparse.eye(3).toarray(), (m + m).toarray(), (m * 2.0).toarray(), m.dot(m).toarray(), sparse.diags([1.0, 2.0, 3.0]).toarray())
P(sparse.linalg.spsolve(sparse.csr_matrix(np.array([[4.0, 1.0], [1.0, 3.0]])), np.array([1.0, 2.0])))
