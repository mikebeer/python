import numpy as np
a = np.arange(100000)
b = a * a + 1
print(b.sum())
f = np.arange(100000) * 0.5
print((f * f + 1.0).sum(), np.sqrt(f).mean())
m = np.arange(2500.0).reshape(50, 50) / 2500.0
print((m @ m).sum())
