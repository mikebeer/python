# more stdlib: heapq bisect dataclasses enum contextlib fractions decimal csv pprint collections itertools functools
import heapq, bisect, csv, io, copy, operator, textwrap, string, statistics, pprint, abc, typing
import itertools as it
import functools as ft
from collections import Counter, defaultdict, deque, OrderedDict, namedtuple, ChainMap
from dataclasses import dataclass, field, fields, asdict, replace
from enum import Enum, IntEnum, auto
from contextlib import contextmanager, suppress, closing
from fractions import Fraction
from decimal import Decimal, getcontext, ROUND_HALF_UP
from typing import List, Optional, Dict

h = []
for x in [5, 3, 8, 1, 9, 2]:
    heapq.heappush(h, x)
print(h, heapq.heappop(h), h, heapq.heappushpop(h, 4), heapq.heapreplace(h, 7), h)
a = [9, 4, 7, 1, 8, 2]
heapq.heapify(a)
print(a, heapq.nlargest(2, a), heapq.nsmallest(3, a), list(heapq.merge([1, 4, 7], [2, 5], [3, 6, 9])), heapq.nsmallest(2, ["bb", "a", "ccc"], key=len))
s = [1, 3, 3, 5, 7]
print(bisect.bisect_left(s, 3), bisect.bisect_right(s, 3), bisect.bisect(s, 6), bisect.bisect_left(s, 0), bisect.bisect_right(s, 9))
bisect.insort(s, 4); bisect.insort_left(s, 3)
print(s, bisect.bisect_left(["a", "c"], "b"), bisect.bisect_right(s, 5, 1, 4))

@dataclass
class Pt:
    x: int
    y: int = 0
    tags: List[str] = field(default_factory=list)

    def norm1(self):
        return abs(self.x) + abs(self.y)

@dataclass(order=True)
class Ver:
    major: int
    minor: int = 0

p = Pt(3, -4)
print(p, p.norm1(), p == Pt(3, -4), Pt(1, tags=["a"]), asdict(Pt(2)), replace(p, y=9), [f.name for f in fields(p)])
print(Ver(1, 2) < Ver(1, 10), sorted([Ver(2), Ver(1, 5), Ver(1)]), max(Ver(0, 1), Ver(0, 2)))
try:
    Pt()
except TypeError as e:
    print("TypeError", e)

class Color(Enum):
    RED = 1
    GREEN = 2
    BLUE = 3

    def label(self):
        return self.name.title()

class Level(IntEnum):
    LOW = 1
    HIGH = 10

class Auto(Enum):
    A = auto()
    B = auto()

print(Color.RED, repr(Color.GREEN), Color.BLUE.value, Color.BLUE.name, Color.RED.label(), Color.RED is Color.RED, Color.RED == Color.GREEN)
print(Level.HIGH > Level.LOW, Level.HIGH + 1, Level.LOW == 1, int(Level.HIGH), Auto.A.value, Auto.B.value, isinstance(Color.RED, Color))

@contextmanager
def tag(name, log):
    log.append("<" + name + ">")
    try:
        yield name.upper()
    finally:
        log.append("</" + name + ">")

log = []
with tag("b", log) as t:
    log.append(t)
try:
    with tag("i", log):
        raise KeyError("boom")
except KeyError:
    log.append("caught")
with suppress(ZeroDivisionError):
    1 / 0
    log.append("not reached")
print(log)

a, b = Fraction(1, 3), Fraction(1, 6)
print(a + b, a * b, a / b, a - b, a < b, Fraction("2/6"), Fraction(0.25), float(a), a ** 2, -a, Fraction(7, 2).limit_denominator(2), round(Fraction(7, 2)))
print(sum([Fraction(1, n) for n in range(1, 5)]), Fraction(3, 1) == 3, hash(Fraction(2, 1)) == hash(2))

print(Decimal("0.1") + Decimal("0.2"), Decimal("1.10") * 3, Decimal(1) / Decimal(7), Decimal("2.675").quantize(Decimal("0.01")), Decimal("2.675").quantize(Decimal("0.01"), ROUND_HALF_UP))
print(Decimal("10") / Decimal("4"), Decimal("-7") // Decimal("2"), Decimal("-7") % Decimal("3"), Decimal("1.50") == Decimal("1.5"), Decimal("2").sqrt(), repr(Decimal("3.140")), int(Decimal("9.99")), Decimal(5) ** 3)
getcontext().prec = 5
print(Decimal(1) / Decimal(3), Decimal("123456789") + 1)
getcontext().prec = 28

c = Counter("abracadabra")
c.update("aaa")
print(c.most_common(3), c["a"], c["zz"], sorted(c), Counter("ab") + Counter("bc"), Counter("aab") - Counter("ab"), list(Counter({"x": 2, "y": 1}).elements()), c == Counter("abracadabra" + "aaa"))
dd = defaultdict(int)
for ch in "hello":
    dd[ch] += 1
print(dict(dd), dd, "z" in dd, len(dd), defaultdict(list, {"a": [1]}))
dq = deque(range(5))
dq.rotate(2); dq.appendleft(-1); dq.extend([9, 9])
print(dq, dq.count(9), dq.index(3), dq[0], dq[-1], deque([1, 2, 3], maxlen=2), list(reversed(dq)))
od = OrderedDict([("b", 1), ("a", 2)])
od.move_to_end("b"); od["c"] = 3
print(od, od.popitem(), list(od.items()), OrderedDict(a=1) == OrderedDict(a=1))
Emp = namedtuple("Emp", ["name", "age"], defaults=[30])
e = Emp("Ann")
print(e, e._asdict(), e._replace(age=31), Emp._fields, Emp("Bob", 5) < Emp("Bob", 6), tuple(e), e == ("Ann", 30), Emp._make(["Z", 1]))
cm = ChainMap({"a": 1}, {"a": 2, "b": 3})
print(cm["a"], cm["b"], sorted(cm), len(cm), dict(cm.items()), cm.get("zz", 0))

print(list(it.islice(it.cycle("ab"), 5)), list(it.takewhile(lambda v: v < 3, it.count())), list(it.dropwhile(lambda v: v < 3, range(6))), list(it.starmap(pow, [(2, 3), (3, 2)])))
print(list(it.compress("abcd", [1, 0, 1, 0])), list(it.pairwise([1, 2, 3, 4])), list(it.batched(range(7), 3)), list(it.chain.from_iterable([[1], [2, 3]])), list(it.filterfalse(None, [0, 1, 0, 2])))
print(list(it.combinations_with_replacement("ab", 2)), len(list(it.permutations(range(4)))), list(it.product(range(2), repeat=2)), list(it.accumulate([1, 2, 3], lambda p, q: p * q, initial=10)))
t1, t2 = it.tee(iter([1, 2, 3]))
print(list(t1), list(t2), [(k, len(list(g))) for k, g in it.groupby(sorted("mississippi"))], list(it.zip_longest("ab", "c", fillvalue="-")))

calls = []
@ft.lru_cache(maxsize=None)
def f(n):
    calls.append(n)
    return n * n
@ft.lru_cache(maxsize=2)
def g(n):
    calls.append(-n)
    return n
f(2); f(2); f(3); g(1); g(2); g(1); g(3); g(2)
print(calls, f.cache_info(), g.cache_info().hits)
f.cache_clear()
print(f.cache_info().currsize)
@ft.cache
def fib(n): return n if n < 2 else fib(n - 1) + fib(n - 2)
def deco(fn):
    @ft.wraps(fn)
    def w(*a, **k):
        return fn(*a, **k)
    return w
@deco
def documented(): "doc"
print(fib(80), documented.__name__, ft.reduce(operator.add, [1, 2, 3], 10), ft.partial(int, base=2)("101"), sorted([3, 1, 2], key=ft.cmp_to_key(lambda p, q: q - p)))
@ft.total_ordering
class W:
    def __init__(self, v): self.v = v
    def __eq__(self, o): return self.v == o.v
    def __lt__(self, o): return self.v < o.v
print(W(1) < W(2), W(2) >= W(2), W(3) > W(1), W(1) <= W(0))

print(operator.itemgetter(1, 0)([5, 6]), operator.attrgetter("real", "imag")(3), operator.methodcaller("upper")("a"), operator.mul(3, 4), operator.neg(2), operator.contains([1], 1))
d1 = {"a": [1, {"b": 2}]}
d2 = copy.deepcopy(d1); d3 = copy.copy(d1)
d2["a"][1]["b"] = 99
print(d1, d2, d3["a"] is d1["a"], copy.deepcopy(p) == p, copy.copy(p) is not p)
print(textwrap.wrap("The quick brown fox jumps over the lazy dog", 12), textwrap.indent("a\nb", "> "), textwrap.shorten("hello big wide world", 14), textwrap.fill("x" * 25, 10))
print(string.Template("$a-${b}-$$").substitute(a=1, b="x"), string.capwords("hello big  world"), statistics.median([1, 3, 2, 4]), statistics.mean([1.5, 2.5]), statistics.variance([2, 4, 4, 4, 5, 5, 7, 9]), statistics.multimode("aabbc"))

rows = list(csv.reader(io.StringIO('a,"b,c",d\n1,"x ""y""",3\n')))
buf = io.StringIO()
csv.writer(buf, lineterminator="\n").writerows([["p", "q r", "s,t"], [1, 2.5, None]])
print(rows, repr(buf.getvalue()), list(csv.DictReader(io.StringIO("k,v\na,1\nb,2\n"))))
pprint.pprint({"z": list(range(30)), "a": {"k": "v" * 40, "l": (1, 2)}})

class Shape(abc.ABC):
    @abc.abstractmethod
    def area(self): ...
class Sq(Shape):
    def area(self): return 4
def typed(a: List[int], b: Optional[Dict[str, int]] = None) -> int:
    return len(a)
print(Sq().area(), typed([1, 2]), isinstance(Sq(), Shape))
