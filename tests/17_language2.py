import sys
def t(label, f):
    try:
        r = f()
    except Exception as e:
        r = "EXC " + type(e).__name__ + ": " + str(e)
    print(label, "->", r)

class Slot:
    __slots__ = ("a", "b")
    def __init__(self): self.a = 1; self.b = 2
t("slots", lambda: (Slot().a, Slot().b))
class A:
    x = 1
    def __init__(self, *args, **kw): self.args = args; self.kw = kw
    def __repr__(self): return "%s(%r, %r)" % (type(self).__name__, self.args, self.kw)
class B(A):
    def __init__(self, *a, **k): super().__init__(*a, **k)
t("super fwd", lambda: repr(B(1, 2, z=3)))
t("class attr", lambda: (B.x, B().x, B.__name__, B().__class__.__name__))
t("getattr default", lambda: (getattr(B(), "nope", 5), hasattr(B(), "x")))
t("setdefault", lambda: {}.setdefault("a", []))
def g(): 
    yield from [1, 2]
    return 3
t("gen", lambda: list(g()))
t("star lit", lambda: ([*[1, 2], *(3, 4)], {**{"a": 1}, "b": 2}, {*[1, 2], 3}))
def unp():
    a, *b = [1, 2, 3]; *c, d = [1, 2, 3]
    return a, b, c, d
t("unpack", unp)
t("fmt attr", lambda: "{0.x} {1[0]} {a}".format(B(), [7], a="z"))
t("fmt nested", lambda: "{:{w}.{p}f}|{:>{w}}".format(3.14159, "ab", w=8, p=2))
t("pct tuple", lambda: "%s-%s %5.1f%%" % ("a", 2, 99.5))
t("int base", lambda: (int("ff", 16), int("0b101", 0), int("  12 "), float("1e3"), float("inf"), int(3.9), int(-3.9)))
t("divmod", lambda: (divmod(-7, 2), -7 // 2, -7 % 2, 7 % -3, 2 ** -1, 10 ** 20 // 3, (-2) ** 3, 7 // 2.0))
t("float fmt", lambda: (str(1e16), str(1.5e-7), repr(0.1 + 0.2), 1 / 3, 2.0 ** 0.5, float("nan") != float("nan"), round(1234.5678, -2), round(-0.5), round(1.5), round(0.5)))
t("str methods", lambda: ("a,b".partition(","), "a,b,c".rpartition(","), "abc".center(7, "*"), "abc".zfill(6), "Hello World".swapcase(), "x".join("abc"), "abc".startswith(("x", "a")), "a b".title(), "  x ".strip(), "abcabc".rfind("b"), "abc".index("c"), "a\tb".split("\t"), "ab".ljust(4) + "|", "%-5s|" % "ab"))
t("str more", lambda: ("abc".isalpha(), "123".isdigit(), "ab1".isalnum(), " ".isspace(), "Abc".istitle(), "ABC".isupper(), "abc".islower(), "aXbXc".split("X", 1), "aXbXc".rsplit("X", 1), "a\nb".splitlines(), "abc"[::-1], "abc"[-2:], "é".upper()))
t("list methods", lambda: (lambda l: (l.sort(key=lambda x: -x), l, l.index(2), l.count(2), l.copy(), l.reverse(), l, l.pop(0), l.insert(1, 9), l, l.extend([7]), l, l.remove(9), l))([3, 2, 1, 2]))
t("sorted multi", lambda: (sorted([("b", 2), ("a", 2), ("c", 1)], key=lambda p: (p[1], p[0])), sorted("bca", reverse=True), sorted({3: 1, 1: 2}), max("abc", key=ord), min([3, 1, 2], key=lambda v: -v)))
t("dict ops", lambda: (lambda d: (d.pop("a"), d.popitem(), d.update(z=1), d, dict.fromkeys("ab", 0), {k: v for k, v in zip("ab", range(2))}, d.get("q", 9), list(d.items()), "z" in d, len(d)))({"a": 1, "b": 2}))
t("set ops", lambda: (lambda a, b: (a | b, a & b, a - b, a ^ b, a <= b, a.union(b), a.intersection([2]), a.issubset(b), sorted(a.symmetric_difference(b))))({1, 2}, {2, 3}))
t("zip/map/filter", lambda: (list(zip([1, 2], "ab", [True, False])), list(map(lambda x, y: x + y, [1, 2], [3, 4])), list(filter(None, [0, 1, "", "a"])), list(reversed([1, 2, 3])), list(zip(*[[1, 2], [3, 4]]))))
t("any/all/sum", lambda: (any([]), all([]), sum([0.1] * 10), sum([[1], [2]], []), sum(range(101)), min(1, 2.5), max([1, 2], [1, 3])))
t("chain cmp", lambda: (1 < 2 < 3, 1 < 3 < 2, "a" < "b", [1, 2] < [1, 3], (1, 2) == (1, 2), None is None, 1 == 1.0, True + True, 3 in range(5), 5 in range(0, 10, 5)))
def outer():
    n = 0
    def inc():
        nonlocal n
        n += 1
        return n
    inc(); inc()
    return n
t("nonlocal", outer)
def fin():
    try:
        return "try"
    finally:
        print("finally ran")
t("finally", fin)
def wl():
    i = 0
    while i < 3:
        i += 1
    else:
        return i
t("while else", wl)
t("lambda default", lambda: [(lambda x, k=k: x + k)(1) for k in range(3)])
t("exc chain", lambda: (lambda: [e for e in [1]] )())
def exc():
    try:
        raise ValueError("bad", 3)
    except (KeyError, ValueError) as e:
        return (type(e).__name__, e.args, str(e), repr(e))
t("exc", exc)
class MyErr(Exception):
    def __init__(self, msg, code):
        super().__init__(msg)
        self.code = code
def exc2():
    try:
        raise MyErr("boom", 7)
    except Exception as e:
        return (str(e), e.code, isinstance(e, MyErr))
t("custom exc", exc2)
t("raise from", lambda: (lambda: [1/0])())
t("hash", lambda: (hash(1) == hash(1.0), hash("a") == hash("a"), hash((1, 2)) == hash((1, 2)), {(1, 2): 3}[(1, 2)]))
t("chr/ord", lambda: (chr(65), ord("a"), hex(255), oct(8), bin(5), abs(-3), pow(2, 10), pow(2, -1), 7.5 // 2, -7.5 % 2, int(True)))
t("bigint", lambda: (2 ** 100, 10 ** 30 % 7, (10 ** 30) // (10 ** 10), -(2 ** 70) // 3, 12345678901234567890 * 98765432109876543210))
t("fstr", lambda: (f"{1234567.891:,.2f}", f"{255:#x} {255:08b} {-5:+d} {0.5:%}", f"{'x'!r:>5}", f"{3.0:g} {1e-5:g} {123456789.0:g}", f"{12:>5}|{12:<5}|{12:^5}|", f"{1e10:e}"))
t("type names", lambda: (type(1).__name__, type(1.0).__name__, type("").__name__, type([]).__name__, type(None).__name__, type(len).__name__, type(lambda: 0).__name__, type(B).__name__, str(type(1))))
t("isinstance", lambda: (isinstance(True, int), isinstance(1, float), isinstance([], (list, tuple)), issubclass(B, A), isinstance(B(), A), isinstance(None, type(None))))
t("callable", lambda: (callable(len), callable(1), callable(A), callable(lambda: 0)))
t("id/is", lambda: (lambda a: (a is a, [] is [], id(a) == id(a)))([]))
t("str*int", lambda: ("ab" * 3, [0] * 3, (1,) * 2, "a" + "b", [1] + [2], 3 * "x"))
t("enumerate", lambda: (list(enumerate("ab", 1)), dict(enumerate("ab")), list(range(10, 0, -3)), list(range(0)), len(range(1, 10, 2))))
t("print sep", lambda: print(1, 2, sep="-", end="!\n"))
t("assert", lambda: (lambda: [assert_ for assert_ in []])())
t("global dict iteration mutation", lambda: (lambda d: [d.update({k + "x": 1}) for k in list(d)] and sorted(d))({"a": 1}))
t("slice assign", lambda: (lambda l: (l.__setitem__(slice(1, 3), [9]), l, l.__delitem__(0), l))([1, 2, 3, 4]))
t("dunder via ops", lambda: (3 .__add__(4), "a".__len__(), [1].__contains__(1), (5).__str__(), (1).__eq__(1)))
t("bool ops", lambda: (0 or "x", 1 and 2, not 0, None or [], "" and 1, 1 if [] else 2))
t("int methods", lambda: ((10).bit_length(), (3.5).is_integer(), (5).real, 3 .conjugate()))

# ---- stage 2 language features ----
class Base1:
    def who(self): return "Base1"
class L1(Base1):
    def who(self): return "L1>" + super().who()
class R1(Base1):
    def who(self): return "R1>" + super().who()
class D1(L1, R1):
    def who(self): return "D1>" + super().who()
t("mro", lambda: (D1().who(), [k.__name__ for k in D1.__mro__], D1.__bases__ == (L1, R1)))
class Dyn:
    def __init__(self): object.__setattr__(self, "v", 1)
    def __setattr__(self, k, val): object.__setattr__(self, k, val * 2)
    def __getattr__(self, n): return "missing:" + n
d_ = Dyn(); d_.w = 4
t("setattr/getattr", lambda: (d_.v, d_.w, d_.zzz))
class Seq:
    def __init__(self): self.d = [1, 2, 3, 4]
    def __getitem__(self, i): return self.d[i]
    def __setitem__(self, i, v): self.d[i] = v
    def __delitem__(self, i): del self.d[i]
    def __len__(self): return len(self.d)
    def __iter__(self): return iter(self.d)
    def __contains__(self, x): return x in self.d
s_ = Seq(); s_[0] = 9; del s_[1]
t("seq proto", lambda: (s_[0], s_[0:2], s_[::2], len(s_), list(s_), 3 in s_, 7 in s_))
class Num:
    def __init__(self, v): self.v = v
    def __add__(self, o): return Num(self.v + (o.v if isinstance(o, Num) else o))
    __radd__ = __add__
    def __iadd__(self, o): self.v += 100; return self
    def __mul__(self, o): return Num(self.v * o)
    def __neg__(self): return Num(-self.v)
    def __pos__(self): return Num(abs(self.v))
    def __abs__(self): return Num(abs(self.v))
    def __invert__(self): return Num(~self.v)
    def __lt__(self, o): return self.v < o.v
    def __eq__(self, o): return isinstance(o, Num) and self.v == o.v
    def __hash__(self): return hash(self.v)
    def __bool__(self): return self.v != 0
    def __repr__(self): return "Num(%r)" % self.v
    def __format__(self, spec): return "N<" + spec + ">"
    def __round__(self, n=0): return Num(round(self.v, n))
    def __call__(self, *a, **k): return (a, sorted(k.items()))
n_ = Num(3)
t("num ops", lambda: (n_ + 1, 1 + n_, sum([Num(1), Num(2)], Num(0)), -n_, +Num(-2), abs(Num(-5)), ~n_, sorted([Num(3), Num(1)]), Num(1) == Num(1), Num(1) != Num(2), {Num(1): "x"}[Num(1)], bool(Num(0)), f"{n_:ab}", round(Num(2.567), 1), n_(1, k=2), max(Num(1), Num(4))))
n_ += 1
t("iadd", lambda: n_)
class Ctx:
    def __enter__(self): print("enter"); return self
    def __exit__(self, et, ev, tb): print("exit", et.__name__ if et else None); return True
def ctx_():
    with Ctx() as c:
        raise ValueError("x")
    return "after"
t("ctx", ctx_)
def gsend():
    got = []
    x = yield 1
    got.append(x)
    y = yield 2
    got.append(y)
    return got
def gdrive():
    g = gsend(); a = next(g); b = g.send("A")
    try:
        g.send("B")
    except StopIteration as e:
        return (a, b, e.value)
t("send", gdrive)
def gwith():
    with Ctx():
        yield 1
        yield 2
t("yield in with", lambda: list(gwith()))
def gthrow():
    try:
        yield 1
    except ValueError as e:
        yield "caught " + str(e)
    finally:
        print("gen finally")
def gt():
    g = gthrow(); next(g)
    r = g.throw(ValueError("boom"))
    g.close()
    return r
t("throw/close", gt)
def yf():
    r = yield from gsend()
    yield ("got", r)
def yfd():
    g = yf(); next(g); g.send(1); return g.send(2)
t("yield from value", yfd)
dv = {"a": 1, "b": 2}
ks, vs, its = dv.keys(), dv.values(), dv.items()
dv["c"] = 3
t("live views", lambda: (list(ks), list(vs), list(its), len(ks), "c" in ks, ("a", 1) in its, ks & {"a", "z"}, sorted(ks | {"q"}), ks == {"a", "b", "c"}, repr(ks)))
sl_ = slice(1, 5, 2)
t("slice obj", lambda: (sl_.start, sl_.stop, sl_.step, list(range(10))[sl_], "abcdef"[slice(None, None, -1)], [1, 2, 3][slice(1, None)], sl_ == slice(1, 5, 2), repr(sl_)))
t("globals/eval", lambda: ("sl_" in globals(), eval("1 + 2 * 3"), eval("sl_.start + 1"), eval("x + 1", {"x": 41}), eval("a * b", {"a": 6}, {"b": 7})))
ns_ = {}
exec("y = 5\ndef f(a): return a + y\nz = f(1)", ns_)
t("exec ns", lambda: (ns_["y"], ns_["z"], ns_["f"](10)))
def loc_():
    a_ = 1; b_ = "two"
    return sorted(locals().items()), eval("a_ + 1")
t("locals", loc_)
t("vars/dir", lambda: (vars(Dyn()) == {"v": 2} or vars(Dyn()), "who" in dir(D1()), sorted(vars(Num(1)))))
t("type()", lambda: (lambda K: (K.__name__, K().m(), K.attr))(type("K", (object,), {"m": lambda self: 42, "attr": 7})))
t("pow/round", lambda: (pow(3, 100, 7), pow(2, 5), round(2.5), round(3.5), round(-2.5), round(2.675, 2), round(1e15 + 0.5), 10 ** -2))
t("float sum", lambda: (sum([0.1] * 10), sum([0.1, 0.2, 0.3]), sum([1, 2.5]), sum([0.1] * 3, 1), -0.0, 0.0 * -1))
t("str ext", lambda: ("a\tb".expandtabs(4), "abc".removeprefix("a"), "abc".removesuffix("c"), "a\nb\r\nc".splitlines(True), "AbC".casefold(), "x1".isidentifier(), "1x".isidentifier()))
t("min/max/enum", lambda: (min([], default=7), max([3, 1], default=0), list(enumerate("ab", 5)), (255).bit_length(), (2.0).is_integer()))
t("fstr nested", lambda: (f"{dv["a"]}", f"{'x' + "y"}", f"{dv['a']=}", f"{1+1=}"))
t("print file", lambda: (lambda o: (print("p", 1, file=o), o.getvalue()))(__import__("io").StringIO()))
t("dunder calls", lambda: ((3).__add__(4), [1, 2].__len__(), "a".__eq__("a"), [3, 1].__contains__(1), (5).__neg__(), (2).__pow__(8)))
