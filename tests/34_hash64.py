# 64-bit hash values of user __hash__ (C interpreter only: CREXX integers are not 64-bit hashes)
class H:
    def __init__(self, v): self.v = v
    def __hash__(self): return self.v
    def __eq__(self, o): return isinstance(o, H) and self.v == o.v
    def __repr__(self): return "H(%r)" % (self.v,)

for v in [0, 1, -1, -2, 5, 2**61 - 1, 2**61, 2**62 - 1, 2**62, 2**62 + 7, 2**63 - 1, -2**63,
          -2**63 + 1, 2**63, -2**63 - 1, 2**64 + 5, -(2**64) - 5, 2**100 + 3, -(2**100) - 3,
          True, False, 10**18, 10**19, 123456789012345678901234567890]:
    print(repr(v), hash(H(v)))

class P:
    def __hash__(self): return hash((1, 2))
print(hash(P()) == hash((1, 2)), hash(P()))
print(hash((1, 2)), hash(("a", 1)) == hash(("a", 1)))

class Bad:
    def __hash__(self): return "x"
class Bad2:
    def __hash__(self): return 1.5
for c in (Bad, Bad2):
    try: hash(c())
    except TypeError as e: print("TypeError:", e)
    try: {c()}
    except TypeError as e: print("TypeError:", e)
    try: {c(): 1}
    except TypeError as e: print("TypeError:", e)

# dict lookup with big hashes
d = {H(2**63 - 1): "a", H(-2**63): "b", H(2**64 + 5): "c", H(-1): "d", H(-2): "e"}
print(d[H(2**63 - 1)], d[H(-2**63)], d[H(2**64 + 5)], len(d))
print(H(-1) in d, H(-2) in d, H(7) in d)

# set iteration order follows the hashes (CPython open addressing)
def order(vals):
    return [x.v for x in {H(v) for v in vals}]
print(order([5, 3, 8, 1, 2]))
print(order([2**63 - 1, -2**63, 2**64 + 5, 2**62, 2**61 - 1, 100]))
print(order(range(40, 0, -3)))
print(order([i * 8 for i in range(20)]))
print(order([i * 2**40 for i in range(12)]))
print(order([-5, -1, -2, 0, 3]))
s = set()
for i in range(30): s.add(H(i * 7 % 31))
print([x.v for x in s])
s.discard(H(14)); s.add(H(100)); s.add(H(200))
print([x.v for x in s])
print([x.v for x in frozenset(H(i) for i in (9, 4, 17, 1))])
print([x.v for x in {H(1), H(2)} | {H(3)}])
print([x.v for x in {H(3), H(1), H(2)} & {H(2), H(3)}])

# tuples containing such objects
print([t[1] for t in {(H(2), 1), (H(1), 2), (H(3), 3)}])

# identity-hash objects
class Plain: pass
ps = [Plain() for _ in range(5)]
print(len(set(ps)), len({Plain(): 1, Plain(): 2}))
o = Plain()
print(hash(o) == hash(o), isinstance(hash(o), int))
