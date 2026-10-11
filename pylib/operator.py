"""operator: functional forms of the operators."""


def lt(a, b): return a < b
def le(a, b): return a <= b
def eq(a, b): return a == b
def ne(a, b): return a != b
def ge(a, b): return a >= b
def gt(a, b): return a > b
def not_(a): return not a
def truth(a): return True if a else False
def is_(a, b): return a is b
def is_not(a, b): return a is not b
def abs(a): return a if a >= 0 else -a
def add(a, b): return a + b
def and_(a, b): return a & b
def floordiv(a, b): return a // b
def index(a): return a.__index__() if hasattr(a, "__index__") else int(a)
def inv(a): return ~a
invert = inv
def lshift(a, b): return a << b
def mod(a, b): return a % b
def mul(a, b): return a * b
def matmul(a, b): return a @ b
def neg(a): return -a
def or_(a, b): return a | b
def pos(a): return +a
def pow(a, b): return a ** b
def rshift(a, b): return a >> b
def sub(a, b): return a - b
def truediv(a, b): return a / b
def xor(a, b): return a ^ b
def concat(a, b): return a + b
def contains(a, b): return b in a
def countOf(a, b):
    n = 0
    for x in a:
        if x == b:
            n += 1
    return n
def delitem(a, b): del a[b]
def getitem(a, b): return a[b]
def indexOf(a, b):
    i = 0
    for x in a:
        if x == b:
            return i
        i += 1
    raise ValueError("sequence.index(x): x not in sequence")
def setitem(a, b, c): a[b] = c
def length_hint(a, default=0): return len(a)
def iadd(a, b):
    a += b
    return a
def isub(a, b):
    a -= b
    return a
def imul(a, b):
    a *= b
    return a
def call(f, *a, **k): return f(*a, **k)


class itemgetter:
    def __init__(self, *items):
        self._items = items

    def __call__(self, obj):
        it = self._items
        if len(it) == 1:
            return obj[it[0]]
        return tuple([obj[i] for i in it])


def _resolve(obj, name):
    for part in name.split("."):
        obj = getattr(obj, part)
    return obj


class attrgetter:
    def __init__(self, *attrs):
        self._attrs = attrs

    def __call__(self, obj):
        at = self._attrs
        if len(at) == 1:
            return _resolve(obj, at[0])
        return tuple([_resolve(obj, a) for a in at])


class methodcaller:
    def __init__(self, name, *args, **kwargs):
        self._name = name
        self._args = args
        self._kwargs = kwargs

    def __call__(self, obj):
        return getattr(obj, self._name)(*self._args, **self._kwargs)
