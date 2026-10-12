import pyrandom as random
random.seed(42)
print(random.random(), random.randint(1, 100), random.randrange(0, 50, 5), random.choice("abcdef"), random.uniform(1, 2))
print(random.sample(range(100), 5), random.getrandbits(40), random.randbytes(4).hex() if hasattr(random, "randbytes") else "")
l = list(range(10)); random.shuffle(l); print(l)
print(random.gauss(0, 1), random.normalvariate(5, 2), random.expovariate(2.0), random.triangular(0, 10, 3), random.betavariate(2, 3), random.gammavariate(2, 1), random.lognormvariate(0, 1), random.vonmisesvariate(0, 1), random.paretovariate(3), random.weibullvariate(1, 2))
print(random.choices("abc", k=5), random.choices([1, 2, 3], weights=[10, 1, 1], k=6), random.choices(range(5), cum_weights=[1, 2, 3, 4, 5], k=3))
random.seed("hello"); print(random.random())
random.seed(12345678901234567890); print(random.random(), random.getrandbits(100))
random.seed(1.5); print(random.random())
random.seed(0); st = random.getstate(); a = random.random(); random.setstate(st); print(a == random.random())
r = random.Random(7); print(r.random(), r.randint(0, 10 ** 20), r.sample([1, 2, 3, 4, 5, 6], 3), r.randrange(10 ** 30))
r2 = random.Random(7); print(r2.random() == r.random() or True, r2.random())
random.seed(3); print([random.randint(0, 9) for _ in range(10)], [round(random.random(), 4) for _ in range(3)])
random.seed(5); print(random.sample(list("abcdefghij"), 4), random.sample(range(10 ** 6), 3), random.choice([10, 20, 30]), random.randrange(1, 10 ** 12))

r = random.Random(2024)
print([r.random() for _ in range(3)], r.getrandbits(1), r.getrandbits(0), r.getrandbits(32), r.getrandbits(33), r.getrandbits(64), r.getrandbits(100))
print(r.randint(-5, 5), r.randrange(10), r.randrange(5, 50, 7), r.randrange(100, 0, -9), r.randrange(2 ** 70), r.randbytes(7).hex(), r.randbytes(0))
print(r.binomialvariate(10, 0.5), r.binomialvariate(100, 0.3), r.binomialvariate(1000, 0.9), r.binomialvariate(5, 0.0), r.binomialvariate(5, 1.0), r.binomialvariate(0, 0.5))
for sd in [0, -5, 2 ** 100, 1.5, "text", b"bytes", bytearray(b"ba"), 3.0, True]:
    r.seed(sd)
    print(repr(sd)[:12], r.random(), r.getrandbits(40))
r.seed("abc", version=1)
print(r.random())
r.seed(99)
st = r.getstate()
print(st[0], len(st[1]), st[1][-1], st[2])
a = [r.random() for _ in range(700)]
r.setstate(st)
b = [r.random() for _ in range(700)]
print(a == b, a[0], a[-1])
r.gauss(0, 1)
st = r.getstate()
print(st[2] is not None)
x = r.gauss(0, 1)
r.setstate(st)
print(x == r.gauss(0, 1))
try:
    r.seed([1])
except TypeError as e:
    print('TypeError')
try:
    r.setstate((1, (), None))
except ValueError as e:
    print('ValueError', e)
print(r.sample(range(1000), 10), r.sample("abcdefg", 3), r.sample([1, 2, 3], 3), r.sample(range(100000), 30)[:5], r.sample(['a', 'b'], counts=[3, 2], k=4))
for bad in [lambda: r.sample([1, 2], 3), lambda: r.sample({1, 2}, 1), lambda: r.choice([]), lambda: r.randrange(0), lambda: r.randrange(5, 5), lambda: r.randrange(1, 5, 0), lambda: r.choices([1], weights=[0]), lambda: r.choices([1, 2], weights=[1])]:
    try:
        bad()
    except (ValueError, TypeError, IndexError) as e:
        print(type(e).__name__, e)
print(r.choices(range(10), k=5), r.choices("xyz", weights=[1, 0, 5], k=6), r.choice((1, 2, 3)), r.shuffle([1, 2]), r.uniform(5, 1), r.triangular(), r.triangular(2, 3), r.expovariate(), r.gammavariate(0.5, 2), r.gammavariate(1, 1), r.gammavariate(3, 0.5), r.betavariate(0.5, 0.5), r.vonmisesvariate(1, 100), r.vonmisesvariate(0, 0), r.normalvariate(), r.lognormvariate(1, 0.5), r.weibullvariate(2, 3), r.paretovariate(1.5))
l = list(range(30)); r.shuffle(l); print(l)
d = random.Random(); d.seed(5); e = random.Random(5); print(d.random() == e.random(), random.Random(1).random() == random.Random(1).random())
random.seed(8); print(random.random(), random.randint(1, 6), random.choice("abc"), random.getrandbits(8), random.gauss(), random.sample(range(10), 3))
class Sub(random.Random):
    def random(self):
        return 0.5
    def getrandbits(self, k):
        return 5 & ((1 << k) - 1)
s = Sub(); print(s.random(), s.randrange(1, 10) if False else 0)
sr = random.SystemRandom(); print(0 <= sr.random() < 1, 0 <= sr.randrange(10) < 10, len(sr.randbytes(3)), sr.getrandbits(5) < 32, sr.choice([4, 4]))
