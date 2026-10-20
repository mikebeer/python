#!/usr/bin/env python3
"""Writes t_stats.crexx and t_stats.expected: the crexx program and the CPython scipy/numpy reference output from one spec list."""
import numpy as np, os
from scipy import special as sp, stats as st
here = os.path.dirname(os.path.abspath(__file__))
def rr(x): return str((np.round(np.asarray(x, dtype=float) * 1e9) / 1e9).tolist())
def fl(v): return float('%.15g' % float(v))
def sc(v): return rr([fl(v)])
def F(v):
    s = '%.12g' % v
    return s if ('.' in s or 'e' in s) else s + '.0'
def A(v): return 'rxnumpy..np_array("%s")' % str([float(t) for t in v]).replace(' ', '')
prog = []; exp = []
def line(label, cx_parts, py_parts):
    prog.append('  say "%s" %s' % (label, ' '.join(cx_parts))); exp.append(' '.join([label] + py_parts))
def scal(label, cx, val): line(label, ['sc(%s)' % cx], [sc(val)])
SPF = lambda f, *a: 'rxscipy..%s(%s)' % (f, ', '.join(a))
# --- special functions
for x in [0.3, 2.0, 5.5, 7.25]: scal('gamma', SPF('sp_gamma', F(x)), sp.gamma(x)); scal('lgamma', SPF('sp_lgamma', F(x)), sp.gammaln(x))
for x in [0.2, 1.1, 2.7, 4.0]: scal('erf', SPF('sp_erf', F(x)), sp.erf(x)); scal('erfc', SPF('sp_erfc', F(x)), sp.erfc(x))
for y in [0.1, 0.75, 0.999, -0.3]: scal('erfinv', SPF('sp_erfinv', F(y)), sp.erfinv(y))
scal('beta', SPF('sp_beta', '2.5', '3.5'), sp.beta(2.5, 3.5))
scal('betainc', SPF('sp_betainc', '2.0', '5.0', '0.3'), sp.betainc(2, 5, .3)); scal('betainc', SPF('sp_betainc', '10.0', '3.5', '0.8'), sp.betainc(10, 3.5, .8))
for a, x in [(0.5, 0.4), (3.0, 2.0), (12.0, 15.5)]:
    scal('gammainc', SPF('sp_gammainc', F(a), F(x)), sp.gammainc(a, x)); scal('gammaincc', SPF('sp_gammaincc', F(a), F(x)), sp.gammaincc(a, x))
# --- distributions
D = [('norm', 0, 0, 1.0, 2.0, [-1.0, 0.5, 2.8]), ('t', 7, 0, 0, 1, [-2.1, 0.0, 1.7]), ('chi2', 4, 0, 0, 1, [0.5, 3.0, 9.0]),
     ('f', 5, 8, 0, 1, [0.4, 1.5, 4.0]), ('gamma', 2.5, 0, 0, 1.5, [0.7, 3.0, 8.0]), ('beta', 2.0, 3.0, 0, 1, [0.2, 0.5, 0.9]),
     ('expon', 0, 0, 0, 2.0, [0.3, 1.0, 5.0]), ('uniform', 0, 0, 1.0, 4.0, [0.5, 2.0, 4.2]), ('lognorm', 0.6, 0, 0, 1.0, [0.4, 1.0, 3.0]),
     ('binom', 12, 0.35, 0, 1, [0.0, 4.0, 9.0]), ('poisson', 3.5, 0, 0, 1, [0.0, 3.0, 8.0]), ('geom', 0.3, 0, 0, 1, [1.0, 3.0, 9.0])]
for nm, a, b, loc, scale, xs in D:
    sh = {'norm': (), 't': (a,), 'chi2': (a,), 'f': (a, b), 'gamma': (a,), 'beta': (a, b), 'expon': (), 'uniform': (), 'lognorm': (a,),
          'binom': (a, b), 'poisson': (a,), 'geom': (a,)}[nm]
    disc = nm in ('binom', 'poisson', 'geom')
    d = getattr(st, nm)(*sh, loc=loc) if disc else getattr(st, nm)(*sh, loc=loc, scale=scale)
    args = lambda x: '"%s", %s, %s, %s, %s, %s' % (nm, F(x), F(a), F(b), F(loc), F(scale))
    f = lambda x: 'rxscipy..%s(' + args(x) + ')'
    for x in xs:
        xx = x + loc if disc else x
        pdf = d.pmf(xx) if disc else d.pdf(xx)
        line(nm, ['sc(rxscipy..sp_pdf(%s))' % args(xx), 'sc(rxscipy..sp_cdf(%s))' % args(xx), 'sc(rxscipy..sp_sf(%s))' % args(xx)], [sc(pdf), sc(d.cdf(xx)), sc(d.sf(xx))])
    qa = lambda q: '"%s", %s, %s, %s, %s, %s' % (nm, F(q), F(a), F(b), F(loc), F(scale))
    line(nm + '_ppf', ['sc(rxscipy..sp_ppf(%s))' % qa(q) for q in (0.05, 0.5, 0.975)], [sc(d.ppf(q)) for q in (0.05, 0.5, 0.975)])
    ma = '"%s", %s, %s, %s, %s' % (nm, F(a), F(b), F(loc), F(scale))
    line(nm + '_mv', ['sc(rxscipy..sp_mean(%s))' % ma, 'sc(rxscipy..sp_var(%s))' % ma], [sc(d.mean()), sc(d.var())])
# --- rvs reproduction via the legacy stream: np.random.seed(s); dist.rvs(size=5)
R = [('norm', 0, 0, 1.0, 2.0), ('t', 5.5, 0, 0, 1), ('chi2', 3, 0, 0, 1), ('f', 4, 9, 0, 1), ('gamma', 2.5, 0, 0, 1.5), ('gamma', 0.6, 0, 0, 1),
     ('beta', 2.0, 3.0, 0, 1), ('beta', 0.5, 0.7, 0, 1), ('expon', 0, 0, 0, 2.0), ('uniform', 0, 0, 1.0, 4.0), ('lognorm', 0.6, 0, 0, 1.0),
     ('binom', 12, 0.35, 0, 1), ('binom', 40, 0.9, 0, 1), ('poisson', 3.5, 0, 0, 1), ('geom', 0.5, 0, 0, 1), ('geom', 0.1, 0, 0, 1)]
for k, (nm, a, b, loc, scale) in enumerate(R):
    sh = {'norm': (), 't': (a,), 'chi2': (a,), 'f': (a, b), 'gamma': (a,), 'beta': (a, b), 'expon': (), 'uniform': (), 'lognorm': (a,),
          'binom': (a, b), 'poisson': (a,), 'geom': (a,)}[nm]
    disc = nm in ('binom', 'poisson', 'geom')
    np.random.seed(100 + k)
    d = getattr(st, nm)(*sh, loc=loc) if disc else getattr(st, nm)(*sh, loc=loc, scale=scale)
    v = d.rvs(size=5)
    prog.append('  call rxscipy..sp_seed(%d)' % (100 + k))
    line('rvs_' + nm, ['rr(rxnumpy..np_asfloat(rxscipy..sp_rvs("%s", 5, %s, %s, %s, %s)))' % (nm, F(a), F(b), F(loc), F(scale))], [rr(v)])
# --- descriptive statistics and tests
x = [2.1, 3.4, 1.9, 5.6, 4.4, 3.8, 7.2, 2.9, 4.1, 6.0]
y = [1.2, 2.9, 2.0, 4.8, 5.1, 3.1, 6.5, 2.2, 4.9, 5.2]
ym = [3.0, 1.0, 4.0, 1.0, 5.0, 9.0, 2.0, 6.0]
pa = 'rxnumpy..np_array("%s")'
line('describe', ['rr(rxscipy..sp_describe(%s))' % A(x)], [rr(list(st.describe(x)[:1]) + list(st.describe(x).minmax) + list(st.describe(x)[2:]))])
scal('skew', 'rxscipy..sp_skew(%s)' % A(x), st.skew(x)); scal('kurtosis', 'rxscipy..sp_kurtosis(%s)' % A(x), st.kurtosis(x))
line('zscore', ['rr(rxscipy..sp_zscore(%s, 0))' % A(x)], [rr(st.zscore(x))]); line('zscore1', ['rr(rxscipy..sp_zscore(%s, 1))' % A(x)], [rr(st.zscore(x, ddof=1))])
for q in (0.0, 12.5, 50.0, 90.0, 100.0): scal('percentile', 'rxscipy..sp_percentile(%s, %s)' % (A(x), F(q)), np.percentile(x, q))
for mode in ('valid', 'same', 'full'):
    for (p, q) in ((x[:6], [1.0, 0.5, 0.25]), ([1.0, 2.0, 3.0], x[:7]), (x[:5], x[5:])):
        line('correlate_' + mode, ['rr(rxscipy..sp_correlate(%s, %s, "%s"))' % (A(p), A(q), mode)], [rr(np.correlate(p, q, mode))])
for mu in (3.0, 5.0):
    r = st.ttest_1samp(x, mu)
    prog.append('  call rxscipy..sp_ttest_1samp(%s, %s, r, p)' % (A(x), F(mu))); line('ttest_1samp', ['sc(r)', 'sc(p)'], [sc(r.statistic), sc(r.pvalue)])
r = st.ttest_ind(x, y); prog.append('  call rxscipy..sp_ttest_ind(%s, %s, r, p)' % (A(x), A(y))); line('ttest_ind', ['sc(r)', 'sc(p)'], [sc(r.statistic), sc(r.pvalue)])
r = st.ttest_ind(x, ym, equal_var=False); prog.append('  call rxscipy..sp_ttest_welch(%s, %s, r, p, df)' % (A(x), A(ym)))
line('ttest_welch', ['sc(r)', 'sc(p)', 'sc(df)'], [sc(r.statistic), sc(r.pvalue), sc(r.df)])
r = st.pearsonr(x, y); prog.append('  call rxscipy..sp_pearsonr(%s, %s, r, p)' % (A(x), A(y))); line('pearsonr', ['sc(r)', 'sc(p)'], [sc(r.statistic), sc(r.pvalue)])
xt = [1.0, 2.0, 2.0, 3.0, 5.0, 5.0, 6.0, 9.0]
r = st.spearmanr(xt, ym); prog.append('  call rxscipy..sp_spearmanr(%s, %s, r, p)' % (A(xt), A(ym))); line('spearmanr', ['sc(r)', 'sc(p)'], [sc(r.statistic), sc(r.pvalue)])
r = st.spearmanr(x, y); prog.append('  call rxscipy..sp_spearmanr(%s, %s, r, p)' % (A(x), A(y))); line('spearmanr', ['sc(r)', 'sc(p)'], [sc(r.statistic), sc(r.pvalue)])
r = st.linregress(x, y)
line('linregress', ['rr(rxscipy..sp_linregress(%s, %s))' % (A(x), A(y))], [rr([r.slope, r.intercept, r.rvalue, r.pvalue, r.stderr, r.intercept_stderr])])
for tab, corr in (([[10, 20, 30], [6, 9, 17]], 1), ([[12, 5], [29, 3]], 1), ([[12, 5], [29, 3]], 0), ([[8, 3, 9, 1], [4, 7, 2, 6], [5, 5, 5, 5]], 1)):
    r = st.chi2_contingency(np.array(tab), correction=bool(corr))
    prog.append('  call rxscipy..sp_chi2_contingency(rxnumpy..np_array("%s"), %d, r, p, dof, ex)' % (str([[float(v) for v in row] for row in tab]).replace(' ', ''), corr))
    line('chi2_contingency', ['sc(r)', 'sc(p)', 'dof', 'rr(ex)'], [sc(r.statistic), sc(r.pvalue), str(r.dof), rr(r.expected_freq)])
head = '''options levelb floats_binary
import rxfnsb
import rxnumpy
import rxscipy

rr: procedure = .string
  arg x = .binary
  return rxnumpy..np_tolist(rxnumpy..np_divs(rxnumpy..np_round(rxnumpy..np_muls(x, 1000000000.0)), 1000000000.0))

sc: procedure = .string
  arg x = .float
  return rr(rxnumpy..np_array("[" || x || "]"))

main: procedure = .int
  r = 0.0
  p = 0.0
  df = 0.0
  dof = 0
  ex = rxnumpy..np_zeros("1")
'''
open(os.path.join(here, 't_stats.crexx'), 'w').write(head + '\n'.join(prog) + '\n  return 0\n')
open(os.path.join(here, 't_stats.expected'), 'w').write('\n'.join(exp) + '\n')
