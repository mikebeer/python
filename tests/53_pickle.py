import pickle, io, copyreg
from collections import namedtuple, deque, defaultdict

samples = [None, True, False, 0, 1, -1, 255, 256, 65535, 65536, 2**31, -2**31 - 1, 2**63, 2**100, -2**100, 0.0, -0.5, 1e300, 3.141592653589793,
           float('inf'), '', 'abc', 'x' * 300, b'', b'bytes\x00\xff', bytearray(b'ba'), (), (1,), (1, 2), (1, 2, 3), (1, 2, 3, 4), [], [1, 2, 3], {}, {'a': 1, 2: [3]},
           set(), {1, 2, 3}, frozenset(), frozenset([1, 2]), 1 + 2j, range(3), range(1, 10, 2), [[1], [2, [3, [4]]]], list(range(100)), {i: str(i) for i in range(300)},
           ('nested', (1, (2, (3,))), [None, True]), Ellipsis, NotImplemented, slice(1, 5, 2)]
for proto in range(6):
    n = 0
    for o in samples:
        d = pickle.dumps(o, proto)
        r = pickle.loads(d)
        if isinstance(o, float) and o != o:
            continue
        assert r == o and type(r) is type(o), (proto, o, r)
        n += 1
    print('proto', proto, n)
for proto in range(6):
    print(proto, pickle.dumps([1, 'a', (2.5, None), {'k': b'v'}, {3}, True], proto).hex())
    print(proto, pickle.dumps(('abc', 70000, 2**40, -3, 1.5, b'x' * 3), proto).hex())
print(pickle.dumps('d', protocol=pickle.HIGHEST_PROTOCOL).hex(), pickle.DEFAULT_PROTOCOL, pickle.HIGHEST_PROTOCOL, pickle.format_version)

# shared references and cycles
a = [1, 2]
s = [a, a, (a,)]
r = pickle.loads(pickle.dumps(s))
print(r, r[0] is r[1], r[2][0] is r[0])
cyc = [1]
cyc.append(cyc)
r = pickle.loads(pickle.dumps(cyc))
print(r[1] is r, r[0])
dd = {}
dd['self'] = dd
r = pickle.loads(pickle.dumps(dd, 2))
print(r['self'] is r)

class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    def __eq__(self, o):
        return isinstance(o, Point) and (self.x, self.y) == (o.x, o.y)
    def __repr__(self):
        return 'Point(%r, %r)' % (self.x, self.y)

class Slotted:
    __slots__ = ('a', 'b')
    def __init__(self, a, b):
        self.a = a
        self.b = b

class WithState:
    def __init__(self):
        self.v = 1
        self.cache = 'drop'
    def __getstate__(self):
        return {'v': self.v}
    def __setstate__(self, st):
        self.v = st['v'] * 10
        self.cache = None

class Reducer:
    def __init__(self, n):
        self.n = n
    def __reduce__(self):
        return (Reducer, (self.n + 1,))

class MyList(list):
    pass

class MyDict(dict):
    def extra(self):
        return len(self)

class MyTuple(tuple):
    pass

class MyStr(str):
    pass

class Child(Point):
    def __init__(self):
        Point.__init__(self, 'c', 'd')
        self.z = [1]

class MyErr(Exception):
    pass

for proto in range(6):
    p = pickle.loads(pickle.dumps(Point(1, [2, 3]), proto))
    sl = pickle.loads(pickle.dumps(Slotted(1, 'two'), proto)) if proto >= 2 else None
    ws = pickle.loads(pickle.dumps(WithState(), proto))
    rd = pickle.loads(pickle.dumps(Reducer(1), proto))
    ml = pickle.loads(pickle.dumps(MyList([1, 2]), proto))
    md = pickle.loads(pickle.dumps(MyDict(a=1), proto))
    mt = pickle.loads(pickle.dumps(MyTuple((1, 2)), proto))
    ms = pickle.loads(pickle.dumps(MyStr('hey'), proto))
    ch = pickle.loads(pickle.dumps(Child(), proto))
    er = pickle.loads(pickle.dumps(MyErr('boom', 2), proto))
    ve = pickle.loads(pickle.dumps(ValueError('bad'), proto))
    print(proto, p, (sl.a, sl.b) if sl else None, ws.v, ws.cache, rd.n, ml, type(ml).__name__, md, md.extra(), mt, type(mt).__name__, ms, type(ms).__name__, ch, ch.z, er.args, type(er).__name__, repr(ve))

NT = namedtuple('NT', 'a b')
for proto in range(2, 6):
    r = pickle.loads(pickle.dumps([NT(1, 2), deque([1, 2, 3]), defaultdict(list, {'k': [1]})], proto))
    print(proto, r[0], r[1], dict(r[2]), type(r[2]).__name__, r[2].default_factory)

import datetime, fractions, decimal
for proto in range(2, 6):
    objs = [datetime.date(2024, 2, 29), datetime.datetime(2020, 1, 2, 3, 4, 5, 6), datetime.timedelta(days=1, seconds=5), fractions.Fraction(3, 7), decimal.Decimal('1.50')]
    r = pickle.loads(pickle.dumps(objs, proto))
    print(proto, r == objs, r)

# functions and classes by reference
def top_level(x):
    return x + 1
r = pickle.loads(pickle.dumps([top_level, Point, len, int, dict, str], 2))
print(r[0](1), r[1] is Point, r[2]([1]), r[3]('5'), r[4] is dict, r[5] is str)
# file API
f = io.BytesIO()
pickle.dump({'x': [1, 2]}, f)
pickle.dump('second', f, protocol=2)
f.seek(0)
print(pickle.load(f), pickle.load(f))
try:
    pickle.load(f)
except EOFError as e:
    print('EOFError', e)
pk = pickle.Pickler(io.BytesIO(), 3)
pk.dump([1])
print(pk.fast if hasattr(pk, 'fast') else 0)
u = pickle.Unpickler(io.BytesIO(pickle.dumps((1, 2))))
print(u.load())
# persistent ids
class PP(pickle.Pickler):
    def persistent_id(self, obj):
        if isinstance(obj, Point):
            return ('pt', obj.x)
        return None
class UU(pickle.Unpickler):
    def persistent_load(self, pid):
        return Point(pid[1], 'loaded')
buf = io.BytesIO()
PP(buf).dump([Point(7, 8), 'k'])
print(UU(io.BytesIO(buf.getvalue())).load())
# errors
LAM = lambda x: x
for name, bad in [('lambda', lambda: pickle.dumps(LAM)), ('garbage', lambda: pickle.loads(b'garbage')), ('empty', lambda: pickle.loads(b'')), ('trunc', lambda: pickle.loads(pickle.dumps([1, 2, 3])[:-3])), ('proto', lambda: pickle.dumps(1, 99)), ('str', lambda: pickle.loads('str')), ('badkey', lambda: pickle.loads(b'\xfe'))]:
    try:
        bad()
        print(name, 'no error')
    except (pickle.PickleError, EOFError, TypeError, ValueError, AttributeError) as e:
        print(name, 'error', isinstance(e, pickle.PickleError), isinstance(e, EOFError), isinstance(e, ValueError))
# protocol 0/1 odd values
print(pickle.loads(b'I12\n.'), pickle.loads(b'L5L\n.'), pickle.loads(b"S'abc'\n."), pickle.loads(b'F1.5\n.'), pickle.loads(b'(lp0\nI1\naI2\na.'), pickle.loads(b'(dp0\nS\'k\'\np1\nI2\ns.'), pickle.loads(b'I01\n.'), pickle.loads(b'N.'), pickle.loads(b'Vtext\\u0107x\n.') == 'text' + chr(263) + 'x')
