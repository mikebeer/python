import numpy as np
from scipy import fft as sfft, signal
x = np.array([1.0, 2.0, 3.0, 4.0, 0.0, -1.0, -2.0, 1.0])
X = np.fft.fft(x)
print(np.round(X, 4))
print(np.round(np.fft.ifft(X).real, 6), np.round(np.abs(X), 4), np.round(np.angle(X), 4))
print(np.round(np.fft.rfft(x), 4), np.round(np.fft.irfft(np.fft.rfft(x)), 6), np.fft.fftfreq(8, 0.5), np.fft.rfftfreq(8))
y = np.arange(6.0)
print(np.round(np.fft.fft(y), 4), np.round(sfft.fft(y), 4)[:2], np.round(np.fft.fftshift(np.fft.fftfreq(6)), 4))
t = np.linspace(0, 1, 64, endpoint=False)
s = np.sin(2 * np.pi * 5 * t)
print(int(np.argmax(np.abs(np.fft.rfft(s)))), np.round(np.abs(np.fft.rfft(s))[5], 4))
print(np.round(np.fft.fft2(np.array([[1.0, 2.0], [3.0, 4.0]])), 4), np.round(signal.convolve([1, 2, 3], [0, 1, 0.5]), 4), np.round(sfft.dct(np.array([1.0, 2.0, 3.0, 4.0]), norm="ortho"), 4) if hasattr(sfft, "dct") else "")
print(np.round(np.fft.ifft(np.fft.fft(np.array([1.0, 0.0, 0.0, 0.0, 0.0]))).real, 6))
