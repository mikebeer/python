# allocation heavy code: results must not depend on when objects are reclaimed
class Node:
    def __init__(self, v, nxt=None):
        self.v = v
        self.nxt = nxt


class Acc:
    def __init__(self):
        self.items = []
        self.d = {}

    def add(self, k, v):
        self.items.append((k, v))
        self.d[k] = v
        return self


def build(n):
    head = None
    for i in range(n):
        head = Node(i * 1.5, head)
    s = 0.0
    while head is not None:
        s += head.v
        head = head.nxt
    return s


def tree(depth):
    if depth == 0:
        return [1.5]
    return [tree(depth - 1), tree(depth - 1), "x" + str(depth)]


def count(t):
    n = 0
    for e in t:
        if isinstance(e, list):
            n += count(e)
        else:
            n += 1
    return n


def gen(n):
    for i in range(n):
        yield (i, i * 0.5, str(i))


def closure_maker(k):
    def f(x):
        return x * k + 0.5
    return f


def keep_some(n):
    keep = []
    for i in range(n):
        t = [i, i * 2.5, {"a": i, "b": [i]}]
        if i % 1000 == 0:
            keep.append(t)
    return keep


total = 0.0
acc = Acc()
saved = []
for i in range(6000):
    a = [i * 0.5, i * 0.25, str(i)]
    b = {"k": a, "n": i}
    c = (a, b, i * 1.5)
    if i % 500 == 0:
        saved.append(c)
        acc.add(i, c)
    total += c[2] + len(c[1]) + len(a[2])
print(round(total, 3), len(saved), len(acc.items), len(acc.d))
print([round(s[2], 1) for s in saved])
print([s[0][2] for s in saved])
print(round(build(3000), 3))
t = tree(8)
print(count(t), t[0][0][0][0][0][0][0][0], t[2], t[0][2], t[1][1][2])
s = 0.0
for a, b, c in gen(5000):
    s += a + b + len(c)
print(s)
fs = [closure_maker(k) for k in range(5)]
r = 0.0
for i in range(2000):
    r += fs[i % 5](i)
print(r)
ks = keep_some(8000)
print(len(ks), [k[0] for k in ks][:5], ks[3][2]["b"], ks[7][1])
words = []
for i in range(3000):
    words.append("w" + str(i % 97))
cnt = {}
for w in words:
    cnt[w] = cnt.get(w, 0) + 1
print(len(cnt), cnt["w0"], cnt["w96"], sorted(cnt)[:3])
sets = set()
for i in range(4000):
    sets.add((i % 50, str(i % 7)))
print(len(sets))
big = []
for i in range(3000):
    big.append([i, [i, i * 2], {"x": i * 1.0}])
    if len(big) > 100:
        big.pop(0)
print(len(big), big[0][0], big[-1][2], sum(b[1][1] for b in big))
ex = 0
for i in range(1500):
    try:
        if i % 3 == 0:
            raise ValueError("bad" + str(i))
        x = [i] * 3
    except ValueError as e:
        ex += len(str(e))
    finally:
        y = {"i": i}
print(ex, y)
ss = sorted([(i * 7919) % 1009 for i in range(2000)], key=lambda v: (v % 10, v))
print(ss[:6], ss[-3:])
zz = list(zip(range(5), map(lambda v: v * 2.5, range(5)), filter(lambda v: v % 2, range(10))))
print(zz)
m = [[0.0] * 30 for _ in range(30)]
for i in range(30):
    for j in range(30):
        m[i][j] = (i * j) * 0.5 + 1.25
print(round(sum(sum(r) for r in m), 3))
lst = []
for i in range(5000):
    lst.append(i * 0.1)
    if i % 2:
        lst.pop()
print(len(lst), round(sum(lst), 3))
sg = 0
g = (v * 2 for v in range(4000))
for v in g:
    sg += v
print(sg, sum(x * x for x in range(1000)))
D = {}
for i in range(3000):
    D[str(i)] = [i]
    if i >= 50:
        del D[str(i - 50)]
print(len(D), sorted(D, key=int)[:3])
def mk(n):
    xs = [n, n+1]
    def get(i):
        return xs[i] + n
    def mut(v):
        xs.append(v)
        return len(xs)
    return get, mut
keep = []
tot = 0
for i in range(3000):
    g, m = mk(i)
    m(i * 2)
    if i % 100 == 0:
        keep.append((g, m))
    tot += g(1) + m(5)
print(tot)
print([g(2) + m(1) for g, m in keep][:5], len(keep))
