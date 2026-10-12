import numpy as np
from scipy import fft as sf, signal


def P(a):
    a = np.asarray(a)
    if str(a.dtype)[:7] == "complex":
        print(np.round(a.real, 6) + 0.0, np.round(a.imag, 6) + 0.0)
    else:
        print(np.round(a, 6) + 0.0)


x = np.array([0.5, -1.0, 2.0, 0.25, 3.0, -0.75, 1.5])
for n in (None, 4, 7, 12):
    P(np.fft.fft(x, n))
for norm in ("backward", "ortho", "forward"):
    P(np.fft.fft(x, norm=norm))
    P(np.fft.ifft(x, norm=norm))
    P(np.fft.rfft(x, norm=norm))
    P(np.fft.irfft(np.fft.rfft(x), 7, norm=norm))
P(np.fft.irfft(np.fft.rfft(x), 8))
P(np.fft.hfft(np.array([1.0, 2.0, 3.0, 4.0])))
P(np.fft.ihfft(x))
for n in (1, 2, 3, 6, 11, 13, 16, 50, 53, 97):
    v = np.array([np.sin(0.7 * i) + 0.1 * i for i in range(n)])
    X = np.fft.fft(v)
    print(n, np.round(np.abs(X).sum(), 6), np.round(np.abs(np.fft.ifft(X).real - v).max(), 9) < 1e-9, np.round(X[0].real, 6) + 0.0)
a = np.arange(12.0).reshape(3, 4)
P(np.fft.fft(a, axis=0))
P(np.fft.fft(a, axis=1))
P(np.fft.fft2(a))
P(np.fft.ifft2(np.fft.fft2(a)))
P(np.fft.rfft2(a))
P(np.fft.irfft2(np.fft.rfft2(a), s=(3, 4)))
P(np.fft.fftn(a, s=(4, 5)))
P(np.fft.rfft(a, n=6, axis=1))
P(np.fft.fftshift(np.arange(7)))
P(np.fft.ifftshift(np.arange(7)))
P(np.fft.fftshift(a))
P(np.fft.fftshift(a, axes=1))
P(np.fft.fftfreq(7, 0.1))
P(np.fft.rfftfreq(10, 0.5))
for t in (1, 2, 3, 4):
    P(sf.dct(x, t))
    P(sf.dct(x, t, norm="ortho"))
    P(sf.idct(x, t, norm="ortho"))
    P(sf.dst(x, t))
    P(sf.dst(x, t, norm="ortho"))
    P(sf.idst(x, t))
P(sf.dctn(a, norm="ortho"))
P(sf.idctn(sf.dctn(a, norm="ortho"), norm="ortho"))
P(sf.dct(a, axis=0))
print([sf.next_fast_len(i) for i in (1, 13, 127, 1021, 1031)], sf.next_fast_len(127, True), sf.prev_fast_len(127))
b, aa = [0.2, 0.4, 0.2], [1.0, -0.5, 0.25]
w, h = signal.freqz(b, aa, 6)
P(w)
P(np.abs(h))
P(np.angle(h))
w, h = signal.freqz(b, worN=4, whole=True, fs=100.0)
P(w)
P(h)
h = signal.hilbert(np.array([1.0, 2.0, 0.0, -1.0, 0.5, 0.25]))
P(h)
P(np.abs(h))
sig = np.array([np.sin(0.4 * i) + 0.5 * np.cos(1.3 * i) for i in range(64)])
f, t, S = signal.spectrogram(sig, fs=8.0, nperseg=16, noverlap=8)
print(S.shape)
P(f)
P(t)
P(S[:, 0])
f, p = signal.welch(sig, nperseg=32)
P(p)
f, p = signal.periodogram(sig)
P(p[:8])
