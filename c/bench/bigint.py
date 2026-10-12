import sys
if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)
import math
f = math.factorial(3000)
print(len(str(f)))
a, b = 0, 1
for i in range(20000):
    a, b = b, a + b
print(len(str(a)))
x = 3 ** 20000
print(len(str(x)), x % 1000003, (x * x) // (x - 1) % 1000)
