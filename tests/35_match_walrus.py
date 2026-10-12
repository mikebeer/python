def kind(x):
    match x:
        case 0:
            return "zero"
        case 1 | 2 | 3:
            return "small"
        case int() if x < 0:
            return "negative"
        case int(n):
            return "int %d" % n
        case float():
            return "float"
        case "a" | "b":
            return "ab"
        case str() as s:
            return "str " + s
        case [] | ():
            return "empty seq"
        case [a]:
            return "one %r" % (a,)
        case [a, b]:
            return "two %r %r" % (a, b)
        case [1, *rest]:
            return "starts1 %r" % (rest,)
        case [_, _, *rest] if len(rest) > 2:
            return "long"
        case {"k": v, **others}:
            return "dict k=%r others=%r" % (v, others)
        case {"x": 1}:
            return "dict x1"
        case None:
            return "none"
        case _:
            return "other"
for v in [0, 2, -5, 99, 1.5, "a", "zz", [], [7], [1, 2], [1, 2, 3], [5, 6, 7, 8, 9], {"k": 1, "z": 2}, {"x": 1}, None, object, (4, 5)]:
    print(repr(v)[:20], kind(v))
class Pt:
    __match_args__ = ("x", "y")
    def __init__(self, x, y): self.x, self.y = x, y
def where(p):
    match p:
        case Pt(0, 0): return "origin"
        case Pt(x, 0): return "x-axis %d" % x
        case Pt(x=0, y=y): return "y-axis %d" % y
        case Pt(x, y) if x == y: return "diag"
        case Pt(): return "pt"
        case (x, y): return "tuple %d,%d" % (x, y)
for p in [Pt(0, 0), Pt(3, 0), Pt(0, 4), Pt(2, 2), Pt(1, 2), (8, 9)]:
    print(where(p))
cmd = "go north"
match cmd.split():
    case ["go", ("north" | "south") as d]:
        print("moving", d)
    case _:
        print("?")
COLOR = "red"
match "red":
    case "blue": print("b")
    case x if x == COLOR: print("matched guard", x)
import enum
class Color(enum.Enum):
    RED = 1
    BLUE = 2
match Color.BLUE:
    case Color.RED: print("r")
    case Color.BLUE: print("b")
data = [1, 2, 3, 4]
if (n := len(data)) > 3:
    print("long", n)
print([y for x in range(5) if (y := x * x) > 3], (z := 10) + z)
while (chunk := data[:2]):
    print(chunk)
    data = data[2:]
import re
if m := re.match(r"(\d+)", "42abc"):
    print(m.group(1))
print(any((q := v) > 2 for v in [1, 2, 3]), q)
def f(a, /, b, *, c): return a + b + c
print(f(1, 2, c=3), f(1, b=2, c=3))
print(f"{(w := 5)} {w}", [(i, j) for i in range(2) for j in range(i)])
x: int = 5
y: "list[int]" = [1]
def typed(a: int, b: str = "s") -> str: return b * a
print(x, y, typed(2), typed.__name__)
try:
    raise ExceptionGroup("g", [ValueError(1), TypeError(2)])
except* ValueError as eg:
    print("value", len(eg.exceptions))
except* TypeError as eg:
    print("type", len(eg.exceptions))
print(*[1, 2], *"ab", **{}) if False else print({**{"a": 1}, "b": 2}, [*range(2), *"x"], (*[1], 2))
