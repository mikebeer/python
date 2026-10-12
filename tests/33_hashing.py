# hashing: unhashable types, tuple hashes and frozenset hashes (CPython 3.13 values)
def attempt(label, f):
    try:
        r = f()
        print(label, "ok", r)
    except TypeError as e:
        print(label, "TypeError", e)

# --- unhashable elements and keys
attempt("set in set", lambda: {{1}})
attempt("list in set", lambda: {[1]})
attempt("dict in set", lambda: {{1: 2}})
attempt("bytearray in set", lambda: {bytearray(b"a")})
attempt("tuple with set", lambda: {(1, {2})})
attempt("tuple with list", lambda: {(1, (2, [3]))})
def setkey():
    d = {}
    d[{1}] = 1
attempt("dict key set", setkey)
def listkey():
    d = {}
    d[[1]] = 1
attempt("dict key list", listkey)
def baKey():
    d = {}
    d[bytearray(b"x")] = 1
attempt("dict key bytearray", baKey)
def dictkey():
    d = {}
    d[{}] = 1
attempt("dict key dict", dictkey)
def add():
    s = set()
    s.add({2})
attempt("add set", add)
attempt("frozenset([set])", lambda: frozenset([{1}]))
attempt("set([list])", lambda: set([[1]]))
attempt("dict get", lambda: {1: 2}.get({1}))
attempt("dict get default", lambda: {1: 2}.get([1], 0))
attempt("dict setdefault", lambda: {}.setdefault({1}, 0))
attempt("dict pop", lambda: {1: 2}.pop({1}, 0))
attempt("dict contains set", lambda: {1} in {1: 2})
attempt("dict getitem", lambda: {1: 2}[{1}])
attempt("dict del", lambda: {1: 2}.__delitem__({1}))
attempt("dict fromkeys", lambda: dict.fromkeys([1, {2}]))
attempt("hash set", lambda: hash({1}))
attempt("hash list", lambda: hash([1]))
attempt("hash dict", lambda: hash({}))
attempt("hash bytearray", lambda: hash(bytearray(b"a")))
attempt("hash tuple set", lambda: hash((1, {2})))
attempt("hash tuple list", lambda: hash(((1,), [2])))

# a failed insertion leaves the container unchanged
d = {1: 2}
try:
    d[[1]] = 3
except TypeError:
    pass
s = {1}
try:
    s.add([1])
except TypeError:
    pass
print(d, s)

# --- lookups with a set probe convert it to a frozenset
print({1} in {frozenset({1})})
print({1, 2} in {frozenset({1}), frozenset({1, 2})})
print({3} in {frozenset({1})})
fs = {frozenset({1, 2}), 5}
fs.discard({1, 2})
print(fs)
fs = {frozenset({1, 2}), 5}
fs.remove({1, 2})
print(fs)
try:
    {frozenset({7})}.remove({8})
except KeyError as e:
    print("KeyError", e)
attempt("list probe", lambda: [1] in {1, 2})
print({frozenset({1}): "a"}[frozenset({1})])
print(frozenset({1}) in {frozenset({1})}, frozenset() in {frozenset()})

# a subclass that defines __hash__ is hashable
class H(list):
    def __hash__(self):
        return 7
print(len({H([1])}), hash(H([2])))
class S(set):
    pass
attempt("set subclass", lambda: {S([1])})
class F(frozenset):
    pass
print(len({F([1])}))

# --- tuple hashes
print(hash(()))
print(hash((1,)), hash((1, 2)), hash((2, 1)), hash((1, 2, 3)))
print(hash((0,)), hash((-1,)), hash((-1, -1)), hash((True, False)))
print(hash((None,)), hash((None, None)), hash(None))
print(hash((1.5,)), hash((0.0, -0.0)), hash((float("inf"), float("-inf"))))
print(hash((2, 2.0)), hash((1, 1.0)) == hash((1, 1)))
print(hash(((1, 2), (3, 4))), hash((((),),)), hash((1, (2, (3, (4,))))))
print(hash((2**61 - 1, 2**61)), hash((2**64, -2**64)), hash((10**30, 10**30 + 1)))
print(hash((12345678901234567890,)), hash((-5, 5, -50, 50, 500)))
print(hash(tuple(range(10))), hash(tuple(range(100))))
print(hash((frozenset(), frozenset({1}), frozenset({1, 2, 3}))))
print(hash((1, 2, 3)) == hash(tuple([1, 2, 3])))
print(hash((1.25, 2.5, 3.75)), hash((1e300, -1e-300)))
print(hash((3 + 4j,)), hash((1j, 2)))
print(hash((b"ab",)) == hash((b"ab",)))
class P:
    def __init__(self, a, b):
        self.a, self.b = a, b
    def __eq__(self, o):
        return (self.a, self.b) == (o.a, o.b)
    def __hash__(self):
        return hash((self.a, self.b))
print(hash(P(1, 2)) == hash(P(1, 2)), len({P(1, 2), P(1, 2), P(2, 1)}))
print({(1, 2): "a", (1, 2.0): "b", (True, 2): "c"})
class T(tuple):
    pass
print(hash(T((1, 2))) == hash((1, 2)))

# --- frozenset hashes
print(hash(frozenset()))
print(hash(frozenset({1})), hash(frozenset({1, 2})), hash(frozenset({1, 2, 3})))
print(hash(frozenset({-1})), hash(frozenset({0})), hash(frozenset({None})))
print(hash(frozenset({(1, 2)})), hash(frozenset({1.5, 2.5})))
print(hash(frozenset({frozenset({1}), frozenset()})))
print(hash(frozenset(range(100))))
print(hash(frozenset({2**70, -3})))
print(hash(frozenset({1, 2})) == hash(frozenset({2, 1})))
print(hash(frozenset({(1, frozenset({2}))})))
