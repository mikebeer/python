import sys
import time
import numpy as np


def lap(name, t0):
    t1 = time.perf_counter()
    sys.stderr.write("  %-22s %8.2f ms\n" % (name, (t1 - t0) * 1000.0))
    return t1


n = 1000000
t = time.perf_counter()
a = np.arange(n)
t = lap("arange(1M)", t)
b = a * a + 1
t = lap("a*a+1", t)
print(b.sum())
t = lap("sum (int)", t)
f = np.arange(n) * 0.5
g = (f * f + 1.0).sum()
print(g)
t = lap("float a*a+1 and sum", t)
print(np.sqrt(f).mean())
t = lap("sqrt + mean", t)
m = np.arange(40000.0).reshape(200, 200) / 40000.0
p = m @ m
print(p.sum())
t = lap("200x200 matmul", t)
q = np.eye(200) + m
print((q @ q.T).trace())
t = lap("matmul with transpose", t)
x = f[f > 400000.0]
print(x.size, x[::1000].sum(), np.sort(f[::-1])[:3])
t = lap("mask, slice, sort", t)
