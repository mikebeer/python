import numpy as np
from scipy import stats
def r6(v): return str(round(float(v) * 1e6) / 1e6)
a = np.array([5.1, 4.9, 6.2, 5.8, 6.0, 5.5, 5.3, 6.1]); b = np.array([6.3, 6.8, 5.9, 7.1, 6.6, 6.9, 7.4, 6.2])
r = stats.ttest_1samp(a, 5.0); print("one sample t test vs 5.0:  t =", r6(r.statistic), " p =", r6(r.pvalue))
r = stats.ttest_ind(a, b); print("two sample t test:         t =", r6(r.statistic), " p =", r6(r.pvalue))
r = stats.ttest_ind(a, b, equal_var=False); print("Welch t test:              t =", r6(r.statistic), " p =", r6(r.pvalue), " df =", r6(r.df))
r = stats.chi2_contingency(np.array([[20, 15, 30], [10, 25, 20]])); print("chi2 =", r6(r.statistic), " p =", r6(r.pvalue), " dof =", r.dof)
print("t quantile (df 7, 0.975):", r6(stats.t.ppf(0.975, 7)))
print("P(chi2(4) > 9.49):       ", r6(stats.chi2.sf(9.49, 4)))
print("binomial(20, 0.3) P(X <= 8):", r6(stats.binom.cdf(8, 20, 0.3)))
