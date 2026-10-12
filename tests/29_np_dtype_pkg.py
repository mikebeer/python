import numpy as np
import numpy.fft
from numpy.fft import fft
from numpy import fft as f2
import numpy.linalg as la
import scipy.fft
a = np.array([1j])
print(a.dtype == complex, a.dtype == float, a.dtype == 'complex128', a.dtype == np.complex128)
b = np.array([1.5, 2.5])
print(b.dtype == float, b.dtype == np.float64, b.dtype == int, b.dtype != float, float == b.dtype)
print(np.array([1, 2]).dtype == int, np.array([True]).dtype == bool)
print(repr(b.dtype), str(b.dtype), b.dtype.name, b.dtype.kind, b.dtype.itemsize)
print(np.dtype('float64') == b.dtype, np.dtype(int))
fs = np.array([1.5]).sum()
is_ = np.array([1]).sum()
print(isinstance(fs, np.floating), isinstance(is_, np.integer), isinstance(is_, np.floating),
      isinstance(is_, np.number), isinstance(np.array([1j]).sum(), np.complexfloating),
      np.float64(3.5), np.int64(7))
print(fft([1, 0, 0, 0]).tolist(), f2.fft is fft, numpy.fft.ifft([1, 1]).tolist())
print(la.det(np.eye(2)))
