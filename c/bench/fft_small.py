import numpy as np
n = 1024
t = np.arange(n) / n
x = np.sin(2 * np.pi * 50 * t) + 0.5 * np.sin(2 * np.pi * 120 * t)
X = np.fft.fft(x)
y = np.fft.ifft(X).real
print(round(float(np.abs(X).sum()) + float(y.sum()), 3), int(np.argmax(np.abs(np.fft.rfft(x)))))
