#!/usr/bin/env python3
"""Benchmark the rxlibs plugin calls against CPython numpy / scipy / pandas.

Every case runs strictly one after the other (CPython first, then the CREXX program), 5 timed repetitions of INNER
calls each; the median repetition divided by INNER is reported.  INNER is chosen per case so that the slower side
needs about 30 ms per repetition.  Setup (array creation, data generation) is outside the timed region.
usage: run_bench.py [case-name ...]      environment: CREXX_VM (driver), as for scripts/run.sh
Writes bench/results.csv (and prints it)."""
import os
for _v in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS"): os.environ[_v] = "1"     # single-threaded BLAS: the plugins are single threaded
import subprocess, sys, time, statistics, shutil, re
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REPS = 5
WORK = os.path.join(ROOT, 'build', 'benchwork')

def Q(s): return s.replace('\n', '\n  ')

CASES = []   # (op, size, crexx_imports, crexx_classes, crexx_setup, crexx_body, py_setup, py_body)
def case(op, size, imports, classes, csetup, cbody, psetup, pbody, ctail=''):
    CASES.append(dict(op=op, size=size, imports=imports, classes=classes, csetup=csetup, cbody=cbody, psetup=psetup, pbody=pbody))

for n in (50, 100, 200):
    case('matmul', '%dx%d' % (n, n), ['rxnumpy'], '',
         'call rxnumpy..np_rand_seed(1)\na = rxnumpy..np_rand("%d,%d")\nb = rxnumpy..np_rand("%d,%d")\nc = a' % (n, n, n, n),
         'c = rxnumpy..np_dot(a, b)',
         'import numpy as np\nnp.random.seed(1)\na = np.random.rand(%d,%d)\nb = np.random.rand(%d,%d)' % (n, n, n, n), 'c = a @ b')
for n in (1000, 100000, 1000000):
    case('sum', '%d' % n, ['rxnumpy'], '', 'call rxnumpy..np_rand_seed(1)\nv = rxnumpy..np_rand("%d")\ntot = 0.0' % n,
         'tot = rxnumpy..np_sum(v)', 'import numpy as np\nnp.random.seed(1)\nv = np.random.rand(%d)' % n, 't = v.sum()')
for n in (1024, 16384, 262144):
    case('fft', '%d' % n, ['rxnumpy'], '', 'call rxnumpy..np_rand_seed(1)\nx = rxnumpy..np_rand("%d")\ny = x' % n,
         'y = rxnumpy..np_fft(x)', 'import numpy as np\nnp.random.seed(1)\nx = np.random.rand(%d)' % n, 'y = np.fft.fft(x)')
for n in (1000, 100000, 1000000):
    case('sort', '%d' % n, ['rxnumpy'], '', 'call rxnumpy..np_rand_seed(1)\nv = rxnumpy..np_rand("%d")\ns = v' % n,
         's = rxnumpy..np_sort(v)', 'import numpy as np\nnp.random.seed(1)\nv = np.random.rand(%d)' % n, 's = np.sort(v)')
for n in (50, 200, 500):
    case('solve', '%dx%d' % (n, n), ['rxnumpy'], '',
         'call rxnumpy..np_rand_seed(1)\na = rxnumpy..np_add(rxnumpy..np_rand("%d,%d"), rxnumpy..np_muls(rxnumpy..np_eye(%d), %d.0))\nb = rxnumpy..np_rand("%d")\nx = b' % (n, n, n, n, n),
         'x = rxnumpy..np_solve(a, b)',
         'import numpy as np\nnp.random.seed(1)\na = np.random.rand(%d,%d) + %d.0*np.eye(%d)\nb = np.random.rand(%d)' % (n, n, n, n, n), 'x = np.linalg.solve(a, b)')
for n in (1000, 100000, 1000000):
    case('groupby', '%d rows/100 keys' % n, ['rxnumpy', 'rxpandas'], '',
         'call rxnumpy..np_rand_seed(1)\ndf = rxpandas..pd_new()\ncall rxpandas..pd_add_col(df, "k", rxnumpy..np_asfloat(rxnumpy..np_randint(0, 100, "%d")))\n'
         'call rxpandas..pd_add_col(df, "v", rxnumpy..np_rand("%d"))\ng = df' % (n, n),
         'g = rxpandas..pd_groupby(df, "k", "v", "sum")',
         'import numpy as np, pandas as pd\nnp.random.seed(1)\ndf = pd.DataFrame({"k": np.random.randint(0, 100, %d).astype(float), "v": np.random.rand(%d)})' % (n, n),
         'g = df.groupby("k")["v"].sum()')
for w in (1, 10, 100):
    cls = ('osc: class implements .sp_f1\n  *: factory\n    return\n  f: method = .float\n  arg x = .float\n'
           '  return rxscipy..sp_cos(%d.0 * x)\n' % w)
    case('quad', 'cos(%dx) on [0,10]' % w, ['rxnumpy', 'rxscipy', 'rxspalg'], cls,
         'o = .osc()\nabserr = 0.0\nq = 0.0', 'q = rxspalg..sp_quad(o, 0.0, 10.0, 0.0000000149, 0.0000000149, 500, abserr)',
         'import numpy as np\nfrom scipy.integrate import quad\nimport math\nf = lambda x: math.cos(%d.0 * x)' % w, 'q = quad(f, 0.0, 10.0, epsabs=1.49e-8, epsrel=1.49e-8, limit=500)')
ROSEN = ('ross: class implements .sp_fn\n  *: factory\n    return\n  f: method = .float\n  arg x = .binary\n'
         '  n = rxnumpy..np_size(x)\n  xs = rxnumpy..np_slice(x, 0, 0, n - 1, 1)\n  xt = rxnumpy..np_slice(x, 0, 1, n, 1)\n'
         '  t = rxnumpy..np_sub(xt, rxnumpy..np_square(xs))\n  u = rxnumpy..np_subs(rxnumpy..np_muls(xs, -1.0), -1.0)\n'
         '  return 100.0 * rxnumpy..np_sum(rxnumpy..np_square(t)) + rxnumpy..np_sum(rxnumpy..np_square(u))\n')
def x0text(n): return ','.join('%.1f' % (1.3 if i % 2 == 0 else 0.7) for i in range(n))
for n in (2, 5, 10):
    for meth, fn, extra in (('minimize NM', 'sp_minimize_nm', '0.0000000001, 0.0000000001, 20000'), ('minimize BFGS', 'sp_minimize_bfgs', '0.00001, %d' % (200 * n))):
        pm = ("minimize(f, x0, method='Nelder-Mead', options=dict(xatol=1e-10, fatol=1e-10, maxiter=20000, maxfev=20000))" if 'NM' in meth else \
             ("minimize(f, x0, method='BFGS', options=dict(gtol=1e-5, maxiter=%d))" % (200 * n)))
        case(meth, 'rosenbrock n=%d' % n, ['rxnumpy', 'rxscipy', 'rxspalg'], ROSEN,
             'o = .ross()\nx0 = rxnumpy..np_array("[%s]")\nfval = 0.0\nnfev = 0\nr = x0' % ','.join(x0text(n).split(',')[:n]).replace(',', ', ').replace(' ', ' '),
             'r = rxspalg..%s(o, x0, %s, fval, nfev)' % (fn, extra),
             'import numpy as np\nfrom scipy.optimize import minimize\nx0 = np.array([%s])\n'
             'def f(x):\n    return 100.0*np.sum((x[1:]-x[:-1]**2)**2) + np.sum((1-x[:-1])**2)' % x0text(n), 'r = ' + pm)

def run(cmd, **kw): return subprocess.run(cmd, capture_output=True, text=True, **kw)

def py_time(c, inner, reps):
    g = {}
    exec(c['psetup'], g)
    code = compile('for _ in range(%d):\n    %s\n' % (inner, Q(c['pbody'])), 'b', 'exec')
    ts = []
    for _ in range(reps):
        t = time.perf_counter(); exec(code, g); ts.append((time.perf_counter() - t) / inner)
    return ts

def crexx_prog(c, inner, reps):
    imp = ''.join('import %s\n' % m for m in ['rxfnsb'] + c['imports'])
    return ('options levelb floats_binary\n' + imp + c['classes'] + '\nmain: procedure = .int\n  ' + Q(c['csetup']) + '\n'
            '  do rep = 1 to %d\n    ignore = time(\'r\')\n    do i = 1 to %d\n      %s\n    end\n    e = time(\'e\')\n    say "REP" e\n  end\n  return 0\n' % (reps, inner, Q(Q(c['cbody']))))

def crexx_time(c, inner, reps):
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, 'b.crexx'); open(src, 'w').write(crexx_prog(c, inner, reps))
    r = run(['sh', os.path.join(ROOT, 'scripts', 'run.sh'), src])
    ts = [float(m) / inner for m in re.findall(r'^REP\s+([0-9.eE+-]+)', r.stdout, re.M)]
    if len(ts) != reps: raise RuntimeError('crexx run failed for %s %s:\n%s\n%s' % (c['op'], c['size'], r.stdout[-600:], r.stderr[-600:]))
    return ts

def main():
    sel = sys.argv[1:]
    old = {}
    if os.path.exists(os.path.join(HERE, 'results.csv')):
        import csv
        for r in csv.DictReader(open(os.path.join(HERE, 'results.csv'))):
            old[(r['operation'], r['size'])] = (r['operation'], r['size'], int(r['inner_calls']), float(r['plugin_us']), float(r['cpython_us']))
    rows = []
    for c in CASES:
        if sel and c['op'] not in sel:
            if (c['op'], c['size']) in old: rows.append(old[(c['op'], c['size'])])
            continue
        p1 = py_time(c, 1, 3); x1 = crexx_time(c, 1, 3)
        inner = max(1, min(5000, int(0.03 / max(statistics.median(p1), statistics.median(x1), 1e-9))))
        p = py_time(c, inner, REPS)
        x = crexx_time(c, inner, REPS)
        pm, xm = statistics.median(p), statistics.median(x)
        rows.append((c['op'], c['size'], inner, xm * 1e6, pm * 1e6))
        print('%-14s %-20s inner %-5d crexx %12.2f us   cpython %12.2f us   crexx/cpython %.2f' % (c['op'], c['size'], inner, xm * 1e6, pm * 1e6, xm / pm), flush=True)
    out = os.path.join(HERE, 'results.csv')
    with open(out, 'w') as f:
        f.write('operation,size,inner_calls,plugin_us,cpython_us,ratio_plugin_over_cpython\n')
        for r in rows: f.write('%s,"%s",%d,%.3f,%.3f,%.3f\n' % (r[0], r[1], r[2], r[3], r[4], r[3] / r[4]))
if __name__ == '__main__': main()
