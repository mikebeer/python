# metaclass __call__, enum with custom __new__ / tuple values, Flag and IntFlag
from enum import Enum, IntEnum, Flag, IntFlag, auto

calls = []


class Meta(type):
    def __call__(cls, *args, **kw):
        calls.append(cls.__name__)
        obj = type.__call__(cls, *args, **kw)
        obj.stamp = len(calls)
        return obj


class A(metaclass=Meta):
    def __init__(self, x):
        self.x = x


class B(A):
    pass


a = A(5)
b = B(7)
print(a.x, a.stamp, b.x, b.stamp, calls)
print(type(A) is Meta, type(B) is Meta, isinstance(a, A))


class Singleton(type):
    _inst = {}

    def __call__(cls, *args):
        if cls not in Singleton._inst:
            Singleton._inst[cls] = type.__call__(cls, *args)
        return Singleton._inst[cls]


class Conf(metaclass=Singleton):
    def __init__(self, v=1):
        self.v = v


c1 = Conf(3)
c2 = Conf(9)
print(c1 is c2, c1.v)


class Status(IntEnum):
    def __new__(cls, value, phrase, description=''):
        obj = int.__new__(cls, value)
        obj._value_ = value
        obj.phrase = phrase
        obj.description = description
        return obj

    OK = 200, 'OK', 'Request fulfilled'
    NOT_FOUND = 404, 'Not Found', 'Nothing matches'


print(Status.OK, int(Status.NOT_FOUND), Status.NOT_FOUND.phrase, Status.OK.description)
print(Status(404) is Status.NOT_FOUND, Status.OK == 200, repr(Status.OK))


class Planet(Enum):
    MERCURY = (3.303e+23, 2.4397e6)
    EARTH = (5.976e+24, 6.37814e6)

    def __init__(self, mass, radius):
        self.mass = mass
        self.radius = radius

    @property
    def gravity(self):
        return 6.67300E-11 * self.mass / (self.radius * self.radius)


print(Planet.EARTH.value, round(Planet.EARTH.gravity, 2), Planet((3.303e+23, 2.4397e6)))


class Coord(Enum):
    def __new__(cls, a, b):
        obj = object.__new__(cls)
        obj._value_ = (a, b)
        obj.a = a
        obj.b = b
        return obj

    P = 1, 2
    Q = 3, 4


print(Coord.Q.value, Coord.Q.a, Coord.P.b, Coord((1, 2)) is Coord.P, [m.name for m in Coord])


class Perm(Flag):
    R = 4
    W = 2
    X = 1


rw = Perm.R | Perm.W
print(rw.value, Perm.R in rw, Perm.X in rw, bool(Perm.R & Perm.X), (rw & Perm.W).value)
print(Perm(3).value, (rw ^ Perm.R).value, (~Perm.R).value)


class Bits(IntFlag):
    A = 1
    B = 2
    C = 4


x = Bits.A | Bits.C
print(int(x), x == 5, x & Bits.B == 0, (x | 2) == 7, Bits.B in x, Bits.C in x)
print(Bits(5) == x, int(Bits.A) + 10, Bits.A < Bits.B)
