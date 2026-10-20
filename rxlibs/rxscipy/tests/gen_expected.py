#!/usr/bin/env python3
import numpy as np, os
from scipy import special, stats, integrate
here = os.path.dirname(os.path.abspath(__file__))
def rr(x): return str((np.round(np.asarray(x, dtype=float) * 1e9) / 1e9).tolist())
def fl(v): return float('%.15g' % float(v))
def sc(v): return rr([fl(v)])
o = []
say = lambda *a: o.append(' '.join(a))
say("erf", sc(special.erf(0.5)), sc(special.erf(-2.0)), sc(special.erfc(3.0)), sc(special.erf(4.0)))
say("gamma", sc(special.gamma(5.0)), sc(special.gamma(0.5)), sc(special.gammaln(10.5)))
say("norm", sc(stats.norm.pdf(0.3)), sc(stats.norm.cdf(1.96)), sc(stats.norm.ppf(0.975)), sc(stats.norm.ppf(0.01)))
say("t", sc(stats.t.cdf(2.0, 5.0)), sc(stats.t.cdf(-1.0, 12.0)))
say("betainc", sc(special.betainc(2.0, 3.0, 0.4)))
x = np.linspace(0, 3, 7); y = x ** 2
say("trapz", sc(np.trapezoid(y, x)), sc(np.trapezoid(y)))
say("simpson", sc(integrate.simpson(y, x=x)))
say("interp", rr(np.interp([0.5, 1.25, 2.9, -1.0, 9.0], x, y)))
c = np.polyfit([0., 1, 2, 3, 4], [1.0, 2.9, 5.2, 6.9, 9.1], 1)
say("polyfit", rr(c), rr(np.polyval(c, [5.0, 6.0])))
a = np.array([1, 2, 3, 4, 5, 6.5]); b = np.array([2, 1.5, 4.1, 3.9, 7.0, 6.0])
r, p = stats.pearsonr(a, b); say("pearson", sc(r), sc(p))
t = stats.ttest_ind(a, b + 1.5); say("ttest", sc(t.statistic), sc(t.pvalue))
open(os.path.join(here, 't_scipy.expected'), 'w').write('\n'.join(o) + '\n')
