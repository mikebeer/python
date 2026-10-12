class Point(tuple):
    def __new__(cls, x, y):
        return super().__new__(cls, (x, y))
    @property
    def x(self): return self[0]
    @property
    def y(self): return self[1]
    def norm(self): return (self.x ** 2 + self.y ** 2) ** 0.5
p = Point(3, 4)
print(p, p.x, p.norm(), isinstance(p, tuple), len(p), p == (3, 4), p[1], tuple(p), hash(p) == hash((3, 4)))
class MyList(list):
    def total(self): return sum(self)
    def append(self, v):
        super().append(v * 2)
m = MyList([1, 2])
m.append(5)
print(m, m.total(), len(m), isinstance(m, list), m[-1], m + [1], sorted(m), type(m).__name__)
class Counter2(dict):
    def __missing__(self, k):
        return 0
    def inc(self, k): self[k] = self[k] + 1
c = Counter2()
c.inc("a"); c.inc("a"); c.inc("b")
print(c, c["zz"], len(c), sorted(c.items()), dict(c), isinstance(c, dict), "a" in c)
class Meters(float):
    def __new__(cls, v): return float.__new__(cls, v)
    def __repr__(self): return "Meters(%s)" % float.__repr__(self)
print(Meters(2.5), Meters(2.5) + 1, repr(Meters(1)), isinstance(Meters(1), float))
class Name(str):
    def shout(self): return self.upper() + "!"
n = Name("bob")
print(n, n.shout(), n + "x", len(n), n == "bob", n[0], isinstance(n, str), {n: 1}["bob"])
class Singleton:
    _inst = None
    def __new__(cls, *a):
        if cls._inst is None:
            cls._inst = super().__new__(cls)
        return cls._inst
    def __init__(self, v=0): self.v = v
print(Singleton(1) is Singleton(2), Singleton(5).v)
class Imm:
    def __new__(cls, v):
        o = object.__new__(cls)
        o.v = v
        return o
print(Imm(3).v)
class Ordered(dict):
    def __setitem__(self, k, v):
        print("set", k)
        dict.__setitem__(self, k, v)
o = Ordered(); o["q"] = 1; o.update(z=2)
print(o, list(o))
import collections
Pt = collections.namedtuple("Pt", "a b")
print(Pt(1, 2), isinstance(Pt(1, 2), tuple))
class E(Exception): pass
class Err2(ValueError):
    def __init__(self, msg, code):
        super().__init__(msg)
        self.code = code
try:
    raise Err2("bad", 7)
except ValueError as e:
    print(e, e.code, isinstance(e, Exception))
class S(set):
    def add(self, x): super().add(x * 10)
s = S(); s.add(1); print(s, isinstance(s, set))
