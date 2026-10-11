# arithmetic, strings, truthiness
print(1 + 2 * 3, (1 + 2) * 3, 7 // 2, -7 // 2, 7 % 3, -7 % 3, 2 ** 10, 2 ** -1)
print(7 / 2, 6 / 3, 10 // 3.0, 5 % 2.5)
print(2 ** 100)
print(12345678901234567890 * 98765432109876543210)
print(-(2 ** 70) // 3, (2 ** 70) % 1000)
print(abs(-5), abs(-2.5), min(3, 1, 2), max([4, 9, 2]), sum([1, 2, 3]), divmod(17, 5), divmod(-17, 5))
print(round(2.5), round(3.5), round(3.14159, 2), int(3.9), int("42"), float("2.5"), int(-3.9))
print(1 < 2 < 3, 1 < 3 < 2, 1 == 1.0, 3 != 4, not 0, not [])
print(True + True, True and 5, 0 or "x", None is None, 1 is not None)
print(bool(0), bool(""), bool("a"), bool([0]), bool(None))
print(5 & 3, 5 | 3, 5 ^ 3, ~5, 1 << 10, 1024 >> 3)
print(hex(255), oct(8), bin(5), int("ff", 16), int("101", 2), 0x1F, 0o17, 0b11, 1_000)
a = b = 3
a += 2
b *= 4
print(a, b)
x, y = 1, 2
x, y = y, x
print(x, y)
print(type(1).__name__, type(1.5).__name__, type("s").__name__, type(None).__name__, type([]).__name__)
print(isinstance(3, int), isinstance(3, float), isinstance("a", str), isinstance(True, int))
print(10 ** 20 == 100000000000000000000, 3 == 3.0, "a" < "b", [1, 2] < [1, 3])
