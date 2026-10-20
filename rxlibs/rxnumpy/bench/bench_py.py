import numpy as np, time
a = np.arange(10000.0).reshape(100, 100) * 0.0001
t = time.time(); c = a.dot(a); print("matmul100", time.time() - t, flush=True)
v = np.arange(100000.0)
t = time.time(); s = v.sum(); print("sum1e5", time.time() - t, s, flush=True)
t = time.time(); y = np.fft.fft(np.arange(256.0)); print("fft256", time.time() - t, flush=True)
v = np.arange(1000000.0)
t = time.time(); s = v.sum(); print("sum1e6", time.time() - t, s, flush=True)
x = np.arange(1024.0)
