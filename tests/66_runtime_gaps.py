# language and runtime gaps: __dict__ updates, splitlines(keepends), int(bytes, base), Class.__new__,
# super().__init_subclass__, GenericAlias, iterable enums, binary files, exception chaining
import enum, os, tempfile, types


class P:
    def __init__(self):
        self.a = 1


p = P()
p.__dict__.update({'b': 2, 'c': 3})
print(p.b, p.c, sorted(vars(p)))
p.__dict__['d'] = 4
print(p.d)
del p.__dict__['a']
print(hasattr(p, 'a'), sorted(p.__dict__))
p.e = 5
print('e' in p.__dict__, p.__dict__['e'])
p.__dict__ = {'z': 26}
print(p.z, sorted(vars(p)))

print('a\nb\r\nc'.splitlines(keepends=True), 'a\nb\n'.splitlines(True), 'x\ny'.splitlines())
print(b'a\nb\n'.splitlines(keepends=True))
print(int(b'ff', 16), int(b'101', 2), int(bytearray(b'42')), int(b'-7'), int(b' 12 ', 10))


class Q:
    def __init__(self):
        self.made = 'init'


q = Q.__new__(Q)
print(type(q).__name__, hasattr(q, 'made'))
print(object.__new__(Q).__class__.__name__)


class Base:
    registry = []

    def __init_subclass__(cls, tag=None, **kw):
        super().__init_subclass__(**kw)
        Base.registry.append((cls.__name__, tag))


class A(Base, tag='a'):
    pass


class B(A, tag='b'):
    pass


class C(Base):
    pass


print(Base.registry)

print(list[int], dict[str, int], tuple[int, ...], list[int].__origin__, list[int].__args__)
print(isinstance(list[int], types.GenericAlias), types.GenericAlias(dict, (str, int)))


class Box:
    def __class_getitem__(cls, item):
        return ('box', item)


print(Box[int])


class Color(enum.Enum):
    RED = 1
    GREEN = 2
    BLUE = 3


print(list(Color), len(Color), Color.RED in Color, Color['GREEN'], Color(3))
print([c.name for c in Color], [m for m in Color.__members__])
for c in Color:
    print(c.value, end=' ')
print()

d = tempfile.mkdtemp()
fn = os.path.join(d, 'bin.dat')
with open(fn, 'wb') as fh:
    print(fh.write(bytes(range(10))), fh.write(b'\x00\xff'))
with open(fn, 'ab') as fh:
    fh.write(b'tail')
with open(fn, 'rb') as fh:
    data = fh.read()
    print(type(data).__name__, len(data), data[:3], data[-6:])
    fh.seek(10)
    print(fh.read(4), fh.tell())
with open(fn, 'rb') as fh:
    print(fh.read(3), fh.read(2))
with open(fn, 'r+b') as fh:
    fh.seek(2)
    fh.write(b'XY')
    fh.seek(0)
    print(fh.read(5))
with open(fn, 'rb') as fh:
    print(fh.mode, fh.name == fn, fh.closed)
print(fh.closed)
os.remove(fn)
os.rmdir(d)

try:
    try:
        1 / 0
    except ZeroDivisionError as e:
        raise KeyError('k') from e
except KeyError as k:
    print(repr(k.__cause__), repr(k.__context__), k.__suppress_context__)
try:
    try:
        1 / 0
    except ZeroDivisionError:
        {}['a']
except KeyError as k:
    print(repr(k.__cause__), repr(k.__context__), k.__suppress_context__)
try:
    try:
        1 / 0
    except ZeroDivisionError:
        raise ValueError('v') from None
except ValueError as k:
    print(k.__cause__, repr(k.__context__), k.__suppress_context__)
try:
    raise ValueError('plain')
except ValueError as k:
    print(k.__cause__, k.__context__, k.__suppress_context__)
