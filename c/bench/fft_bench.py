import numpy as np
n = 4096
t = np.arange(n) / n
x = np.sin(2 * np.pi * 50 * t) + 0.5 * np.sin(2 * np.pi * 120 * t)
tot = 0.0
for i in range(5):
    X = np.fft.fft(x)
    y = np.fft.ifft(X).real
    tot += float(np.abs(X).sum()) + float(y.sum())
print(round(tot, 3), int(np.argmax(np.abs(np.fft.rfft(x)))))
