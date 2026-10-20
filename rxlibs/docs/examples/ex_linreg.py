import numpy as np
from scipy import stats
rng = np.random.default_rng(1)
x = np.arange(0.0, 10.0, 1.0)
y = x * 0.8 + 2.5 + rng.normal(0.0, 0.3, 10)
def r6(a): return str((np.round(np.asarray(a, dtype=float) * 1e6) / 1e6).tolist())
D = np.column_stack([np.ones(10), x])
print("normal equations [intercept, slope]:", r6(np.linalg.solve(D.T @ D, D.T @ y)))
print("polyfit          [slope, intercept]:", r6(np.polyfit(x, y, 1)))
r = stats.linregress(x, y)
print("linregress:", r6([r.slope, r.intercept, r.rvalue, r.pvalue, r.stderr, r.intercept_stderr]))
beta = np.linalg.solve(D.T @ D, D.T @ y)
res = y - (x * beta[1] + beta[0])
print("R squared:", r6([1.0 - (res ** 2).sum() / ((y - y.mean()) ** 2).sum()])[1:-1])
