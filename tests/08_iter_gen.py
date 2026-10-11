def count(n):
    i = 0
    while i < n:
        yield i
        i += 1
print(list(count(4)), sum(count(5)))
def evens():
    n = 0
    while True:
        yield n
        n += 2
it = evens()
print(next(it), next(it), next(it))
def fibs():
    a, b = 0, 1
    while True:
        yield a
        a, b = b, a + b
out = []
for v in fibs():
    if v > 50: break
    out.append(v)
print(out)
def chain(*its):
    for i in its:
        for x in i:
            yield x
print(list(chain([1, 2], "ab", range(2))))
def delegate():
    yield 1
    yield from [2, 3]
    yield 4
print(list(delegate()))
class Countdown:
    def __init__(self, n): self.n = n
    def __iter__(self): return self
    def __next__(self):
        if self.n <= 0: raise StopIteration
        self.n -= 1
        return self.n + 1
print(list(Countdown(3)))
sq = (x * x for x in range(5))
print(list(sq), list(sq))
print(list(zip(count(3), "xyz")), dict(enumerate("ab")))
def tri():
    for i in range(1, 4):
        for j in range(i):
            yield (i, j)
print(list(tri()))
e = iter([10, 20]); print(next(e), next(e), next(e, "done"))
