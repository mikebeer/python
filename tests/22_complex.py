z = 3 + 4j
w = complex(1, -2)
print(z, w, z + w, z - w, z * w, z / w, -z, abs(z), z.real, z.imag, z.conjugate(), z == 3 + 4j, z != w)
print(repr(1j), 1j * 1j, (1 + 2j) ** 2, 2 ** (1 + 1j) if False else 0, complex("1+2j"), complex(3), complex(0, 1), 0j, 1e3j, 1.5 + 2.5j)
print(type(z).__name__, isinstance(z, complex), bool(0j), bool(1j), z * 2, 2 * z, z + 1, 1 - z, z / 2, 1 / z, z ** 0, z ** 3)
import cmath, math
print(cmath.sqrt(-1), cmath.exp(1j * math.pi).real, round(cmath.exp(1j * math.pi).imag, 10), cmath.phase(1j), cmath.polar(1 + 1j), cmath.rect(1, 0))
print(round(abs(cmath.sqrt(-4) - 2j), 12), cmath.isclose(1 + 1j, 1 + 1j), round(cmath.log(1j).imag, 6), round(cmath.sin(1j).imag, 6), cmath.pi == math.pi)
print(sum([1j, 2, 3 + 1j]), [z, w], {z: 1}[3 + 4j], hash(3 + 0j) == hash(3), 3 + 0j == 3, str(1 - 0j), str(-1j), format(z, ".2f"), "{:.1e}".format(1 + 2j), f"{z:g}", f"{w!r}")
import numpy as np
a = np.array([1 + 2j, 3 - 1j])
print(a, a.dtype, a.real, a.imag, np.abs(a).round(4), a * 2, a.conj(), a.sum(), np.sqrt(np.array([-4.0 + 0j])))
print(np.roots([1, 0, 1]) if hasattr(np, "roots") else "x")
try:
    z < w
except TypeError as e:
    print(e)
try:
    1 / 0j
except ZeroDivisionError as e:
    print(e)
print(np.linalg.eigvals(np.array([[0.0, -1.0], [1.0, 0.0]])))
