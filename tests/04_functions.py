def add(a, b=10, *rest, scale=1, **kw):
    return (a + b + sum(rest)) * scale, sorted(kw.items())
print(add(1))
print(add(1, 2, 3, 4, scale=2, z=1, y=2))
print(add(b=5, a=1))
def fact(n):
    return 1 if n <= 1 else n * fact(n - 1)
print(fact(20), fact(25))
def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a
print([fib(i) for i in range(15)])
def counter():
    c = 0
    def inc():
        nonlocal c
        c += 1
        return c
    return inc
k = counter()
print(k(), k(), k())
g = 100
def setg():
    global g
    g = 5
setg()
print(g)
sq = lambda v: v * v
print(sq(7), (lambda a, b=2: a * b)(4))
print(list(map(sq, [1, 2, 3])), list(filter(lambda v: v % 2, range(8))))
def apply(fn, *args):
    return fn(*args)
print(apply(max, 3, 9, 4), apply(add, 1, 2))
args = (1, 2)
kws = {"scale": 3}
print(add(*args, **kws))
def gen_default(x, acc=None):
    if acc is None:
        acc = []
    acc.append(x)
    return acc
print(gen_default(1), gen_default(2))
def outer():
    fs = []
    for i in range(3):
        fs.append(lambda i=i: i * 10)
    return [f() for f in fs]
print(outer())
def hanoi(n, a, b, c):
    if n == 0:
        return 0
    return hanoi(n - 1, a, c, b) + 1 + hanoi(n - 1, c, b, a)
print(hanoi(10, "A", "B", "C"))
def doc():
    "docstring"
    return 1
print(doc.__name__, doc())
print(sorted([3, 1, 2], key=lambda v: -v), sorted(["bb", "a", "ccc"], key=len, reverse=True))
