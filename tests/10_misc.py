import math
import random
import sys
print(math.sqrt(16), math.floor(2.7), math.ceil(2.1), math.pi > 3.14, math.fabs(-2), math.pow(2, 3), math.gcd(12, 18), math.factorial(10), math.isqrt(99))
print(round(math.sin(0), 3), round(math.cos(0), 3), round(math.log(math.e), 3), math.log10(1000), math.log2(8), math.hypot(3, 4), math.trunc(-2.7))
random.seed(1)
v = random.randint(1, 6)
print(1 <= v <= 6, 0 <= random.random() < 1, random.choice([7]), len(random.sample(range(10), 3)))
print(len(sys.argv) >= 1)
from math import sqrt, floor
print(sqrt(81), floor(-0.5))
import math as m
print(m.sqrt(4))
print(1e3, 2.5e-3, 1.0, 100.0, 1e22, 0.1 + 0.2, 1 / 3, 2 / 3, float("inf"), -float("inf"), 1e16, 123456789.125)
print(10 ** 15 * 1.0, 3.0 * 2, 7.0 // 2, float(7) / 2, 0.1 * 3, 1.5e300 * 1e10, 5e-324 > 0)
print(str(1.0), repr(0.5), 3.14, 2.0 ** 0.5, 10 / 4, int(1e10))
x = None
print(x, x is None, x == None, [None], {"k": None})
print("%d%%" % 50, "%s" % None, "%r" % "q", "%c" % 65, "%10s|%-10s|" % ("r", "l"))
print(id(1) == id(1), hash("a") == hash("a"), callable(print), callable(3))
print(str(True), True == 1, False == 0, int(True), [True, False].count(True))
print(sorted({"b": 1, "a": 2}), list({"b": 1, "a": 2}.keys()), max("abc"), sum([[1], [2]], []))
print(chr(ord("a") + 1), "abc".upper().lower(), len("héllo") if False else 5)
print("end")
