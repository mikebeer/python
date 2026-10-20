#!/usr/bin/env python3
"""Generate *.expected for the Level B tests from CPython + numpy (the oracle)."""
import numpy as np, os
here = os.path.dirname(os.path.abspath(__file__))

def rr(x):
    return str((np.round(np.asarray(x, dtype=float) * 1e9) / 1e9).tolist())
def sc(v):                       # how CREXX `say` prints a scalar
    v = float(v)
    s = '%.15g' % v
    if v == int(v) and abs(v) < 1e15: return str(int(v))
    return s
def L(x): return str(np.asarray(x).tolist())
def fl(v): return float('%.15g' % v)   # scalar -> string -> parse round trip

out = []
def say(*a): out.append(' '.join(a))

# ---------------- t_basic
a = np.array([[1, 2, 3], [4, 5, 6]]); f = np.array([[0.5, 1.5, 2.5], [3.5, 4.5, 5.5]])
say("list", L(a))
say("shape", "%s" % ','.join(map(str, a.shape)), str(a.ndim), str(a.size), "1")
say("add", L(a + a)); say("mulf", rr(a * f)); say("div", rr(a / f))
say("bcast", L(a + np.array([10, 20, 30]))); say("bcastcol", L(a + np.array([[100], [200]])))
say("pow", L(a ** np.array(2)))
say("floordiv", L(np.array([-7, 7, 8]) // np.array(2))); say("mod", L(np.array([-7, 7, 8]) % np.array(3)))
say("sum", sc(a.sum()), sc(f.sum()), sc(f.mean()), sc(a.min()), sc(f.max()))
say("std", rr([fl(f.std())]), rr([fl(f.var(ddof=1))]))
say("sum0", L(a.sum(0))); say("sum1", L(a.sum(1))); say("mean1", rr(f.mean(1)))
say("argmax", sc(a.argmax()), L(a.argmax(1)))
say("cumsum", L(a.cumsum()), L(a.cumsum(0)))
say("median", sc(np.median([5, 1, 4, 2])))
say("T", L(a.T)); say("reshape", L(a.reshape(3, -1))); say("flatten", L(a.flatten()))
say("arange", L(np.arange(0, 10, 3)), L(np.arange(0.0, 1.0, 0.25)))
say("linspace", L(np.linspace(0, 1, 5)))
say("zeros", L(np.zeros((2, 2))), L(np.ones(3)))
say("slice", L(a[:, 0:3:2]), L(a[:, -1:-4:-1]))
say("concat", L(np.concatenate([a, a], 0)))
say("sort", L(np.sort(np.array([[3, 1, 2], [9, 7, 8]]))), L(np.argsort(np.array([3.5, 1.5, 2.5]), kind='stable')))
say("dot", L(a.dot(a.T)), L(f.dot(np.array([1.0, 2.0, 3.0]))))
say("unary", rr(np.exp([0.0, 1.0, -1.0])), rr(np.sqrt([4.0, 2.0])), L(np.abs([-3, 4])))
say("outer", L(np.outer([1, 2], [3, 4, 5])))
g = np.zeros((2, 2)); g[1, 0] = 7.5; g.flat[1] = 2.5
say("setget", L(g), sc(g[1, 0]), sc(g.flat[-1]))
say("shared", "9"); say("copy", "9", "1"); say("signal", "INVALID_ARGUMENTS")
open(os.path.join(here, 't_basic.expected'), 'w').write('\n'.join(out) + '\n'); out[:] = []

# ---------------- t_linalg
A = np.array([[4.0, 1.0, 2.0], [1.0, 3.0, 0.5], [2.0, 0.5, 5.0]]); b = np.array([1.0, 2.0, 3.0])
say("solve", rr(np.linalg.solve(A, b))); say("inv", rr(np.linalg.inv(A)))
say("det", rr([fl(np.linalg.det(A))])); say("chol", rr(np.linalg.cholesky(A)))
q, r = np.linalg.qr(A); say("qr", rr(q), rr(r))
w, v = np.linalg.eigh(A); say("eigh", rr(w), rr(v))
C = np.array([[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]]); u, s, vt = np.linalg.svd(C, full_matrices=False)
say("svd", rr(u), rr(s), rr(vt))
def ev(m):
    e = np.linalg.eigvals(np.array(m)); return rr(np.stack([e.real, e.imag], 1))
say("eigvals", ev([[0.0, 1.0], [-2.0, -3.0]])); say("eigvalsc", ev([[0.0, -1.0], [1.0, 0.0]]))
say("norm", rr([fl(np.linalg.norm(A)), fl(np.trace(A))]))
say("signal", "INVALID_ARGUMENTS", "Singular matrix")
open(os.path.join(here, 't_linalg.expected'), 'w').write('\n'.join(out) + '\n'); out[:] = []

# ---------------- t_fft_random
def cx(z): return rr(np.stack([z.real, z.imag], 1))
say("fft8", cx(np.fft.fft([1.0, 2.0, 0.0, -1.0, 3.0, 0.5, 2.0, 1.0])))
say("fft6", cx(np.fft.fft([1.0, 2.0, 0.0, -1.0, 3.0, 0.5])))
say("ifft", cx(np.fft.ifft(np.fft.fft([1.0, 2.0, 0.5, -1.0, 3.0]))))
np.random.seed(42)
say("rand", L(np.random.rand(2, 3))); say("randn", L(np.random.randn(5)))
say("randint", L(np.random.randint(0, 10, 8)), L(np.random.randint(-5, 1000000, 3)))
say("scalar", rr([fl(np.random.random()), fl(np.random.randn())]))
np.random.seed(0); say("rand0", L(np.random.rand(3)))
open(os.path.join(here, 't_fft_random.expected'), 'w').write('\n'.join(out) + '\n')
