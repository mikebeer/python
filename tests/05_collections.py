l = [3, 1, 2]
l.append(5); l.extend([7, 8]); l.insert(0, 0)
print(l, len(l), l[0], l[-1], l[1:3], l[::-1], l.index(2), l.count(1), 5 in l)
l.sort(); print(l); l.reverse(); print(l)
print(l.pop(), l.pop(0), l)
l.remove(5); print(l)
l[1] = 99; del l[0]; print(l)
l += [1, 2]; print(l, l * 2, [0] * 3, [1, 2] + [3])
a = [1, 2, 3]; b = a; b.append(4); c = a[:]; c.append(5)
print(a, b, c, a is b, a == b, a is c)
m = [[1, 2], [3, 4]]
print(m, m[1][0], [row[0] for row in m], sum(len(r) for r in m))
t = (1, 2, 3)
print(t, t[1], t[:2], len(t), t + (4,), (1,), (), t.index(2), tuple([1, 2]), 2 in t)
p, q, *r = [1, 2, 3, 4]
print(p, q, r)
d = {"a": 1, "b": 2}
d["c"] = 3
print(d, d["a"], d.get("z"), d.get("z", 0), len(d), "a" in d, list(d), list(d.values()), list(d.items()))
d.update({"a": 10, "d": 4}); print(d)
print(d.pop("a"), d.setdefault("q", 7), d.setdefault("b", 0), d)
del d["b"]
print(sorted(d), {k: v for k, v in d.items() if v > 3})
for k, v in d.items():
    print(k, v)
dd = dict(x=1, y=2); print(dd, dict([(1, 2)]), dict(zip("ab", [1, 2])))
s = {1, 2, 3}; s.add(4); s.discard(1)
print(sorted(s), 2 in s, len(s), sorted(s | {9}), sorted(s & {2, 3}), sorted(s - {2}), sorted(s ^ {2, 100}), set([1, 1, 2]) == {1, 2})
print([x * x for x in range(6) if x % 2 == 0], {x % 3 for x in range(10)} == {0, 1, 2}, [(i, j) for i in range(2) for j in range(2)])
print(list(zip([1, 2, 3], "ab")), list(enumerate("xy", 1)), list(reversed([1, 2, 3])), sorted([3, 1, 2], reverse=True))
print(any([0, 1]), all([1, 1]), all([]), any([]), min("bca"), max([1, 5, 3], key=lambda v: -v), sum([0.5, 0.25]))
words = "the cat the dog the end".split()
cnt = {}
for w in words:
    cnt[w] = cnt.get(w, 0) + 1
print(sorted(cnt.items()), sorted(cnt.items(), key=lambda kv: (-kv[1], kv[0])))
print(list(map(str, [1, 2])), ",".join(map(str, range(4))), list(range(3)) == [0, 1, 2], [1, [2, [3]]])
g = (x for x in range(3))
print(sum(x for x in range(5)), list(g))
print(len({}), len([]), [] == [], {} == {}, (1, 2) == (1, 2), (1, 2) < (1, 3), {1: "a"} == {1: "a"})
print(l.copy(), [1, 2, 3].__len__() if False else 3, list("hello")[1:3], "x" * 0)
