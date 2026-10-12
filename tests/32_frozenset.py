f = frozenset([3, 1, 2, 2])
g = frozenset({2, 3, 4})
print(sorted(f), len(f), 1 in f, 9 in f, type(f).__name__, isinstance(f, frozenset), isinstance(f, set), isinstance(set(), frozenset))
print(sorted(f | g), sorted(f & g), sorted(f - g), sorted(f ^ g), type(f | g).__name__, type(f & {1}).__name__, type({1} | f).__name__)
print(f == frozenset([1, 2, 3]), f == {1, 2, 3}, f != g, f <= frozenset([1, 2, 3, 4]), f < f, f >= frozenset([1]), f.issubset({1, 2, 3}), f.issuperset([1]), f.isdisjoint(frozenset([7])))
print(hash(f) == hash(frozenset([1, 2, 3])), hash(frozenset()) == hash(frozenset()), len({f, frozenset([1, 2, 3])}), {f: "v"}[frozenset([3, 2, 1])])
print(repr(frozenset()), repr(frozenset([1])), str(frozenset(["a"])), frozenset("a"), sorted(frozenset("hello")))
print(sorted(f.union([9])), sorted(f.intersection([1, 9])), sorted(f.difference([1])), sorted(f.symmetric_difference([1, 9])), sorted(f.copy()), f.copy() is f)
s = {frozenset([1, 2]), frozenset([2, 1]), frozenset([3])}
print(len(s), sorted(map(sorted, s)), frozenset([frozenset([1])]), sorted(x for x in f), min(f), max(f), sum(f), any(f), list(sorted(f))[::-1])
try:
    f.add(1)
except AttributeError as e:
    print("AttributeError")
try:
    frozenset([[1]])
except TypeError as e:
    print(e)
try:
    {[1]: 1}
except TypeError as e:
    print(e)
class MyFS(frozenset):
    def total(self): return sum(self)
m = MyFS([1, 2, 3])
print(m.total(), m == frozenset([1, 2, 3]), isinstance(m, frozenset), len(m), sorted(m | {9}))
t = set(f); t.add(10)
print(sorted(t), sorted(f), set(frozenset()), bool(frozenset()), bool(f))
print({1, 2} == frozenset({1, 2}), frozenset({1, 2}) == {1, 2}, hash(frozenset({1, 2})) == hash(frozenset({2, 1})))
