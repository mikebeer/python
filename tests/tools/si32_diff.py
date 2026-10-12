# Differential test of the C interpreter against CPython, aimed at 32-bit `long` problems.
#
#   python3 tests/tools/si32_diff.py [-n N] [-s SEED] [-c CATS] [--keep] [--show K] PY [PY ...]
#   python3 tests/tools/si32_diff.py --build [-n N] ...        build the three modes, then compare
#   --self: use the first interpreter as the reference instead of CPython (m32 against si32)
#
# Each PY is an interpreter command (a path, or "path:label").  A generated program with N cases
# per category is run by CPython and by every interpreter; each case is one line
#     case(17, lambda: expr)
# printing "17 <repr of the result>" or "17 !ExceptionName".  Differences are listed with the
# expression, so a failing case can be pasted into a file.  With --build the modes are
#     normal   cc -std=c89 -O2 py_all.c                     (64-bit long)
#     si32     -DPY_SI32 -DPY_U64_EMUL                      (64-bit long, small ints +-999999999)
#     m32      gcc -m32 -msse2 -mfpmath=sse                 (real 32-bit long; skipped without multilib)
# CATS is a comma separated subset of the category names (default: all).
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# ---------------------------------------------------------------------------------------------
# value generators

EDGES = [0, 1, 2, 3, 7, 10, 255, 256, 65535, 65536, 9999, 10000, 99999999, 100000000,
         999999999, 1000000000, 1000000001, 2147483647, 2147483648, 4294967295, 4294967296,
         10 ** 10, 10 ** 12, 2 ** 53, 2 ** 53 + 1, 2 ** 60, 2 ** 61 - 1, 2 ** 61, 2 ** 62,
         2 ** 63 - 1, 2 ** 63, 2 ** 64 - 1, 2 ** 64, 2 ** 64 + 1, 10 ** 18 - 1, 10 ** 18,
         10 ** 19, 10 ** 30, 2 ** 100, 2 ** 127, 2 ** 128 - 1]


def rint(r, maxbits=None):
    k = r.random()
    if k < 0.25:
        v = r.choice(EDGES) + r.choice((-1, 0, 0, 0, 1))
    elif k < 0.55:
        v = r.randint(0, 1000)
    else:
        v = r.getrandbits(r.choice((8, 16, 24, 31, 32, 33, 40, 53, 62, 63, 64, 65, 80, 128, 200)))
    if maxbits is not None:
        v &= (1 << maxbits) - 1
    return -v if r.random() < 0.4 else v


def rfloat(r):
    k = r.random()
    if k < 0.15:
        return float(rint(r))
    if k < 0.3:
        return float(r.choice(EDGES)) + r.choice((0.0, 0.5, -0.5, 0.25))
    if k < 0.4:
        return r.choice([0.0, -0.0, 0.5, 1.5, 2.5, -2.5, 0.1, 0.2, 0.3, 1e15, 1e16, 1e17, 1e22,
                         1e23, 5e-324, 1.7976931348623157e308, 2.2250738585072014e-308,
                         1e100, 123456789012345678.0, 4503599627370496.5, 9007199254740993.0])
    if k < 0.7:
        return r.uniform(-1000, 1000)
    if k < 0.85:
        return r.uniform(-1, 1) * 10.0 ** r.randint(-30, 30)
    return r.uniform(-1, 1) * 10.0 ** r.randint(-300, 300)


def rstr(r, n=12):
    alpha = "abcxyz ABC019_-.,;:'\"\\\t\n"
    return "".join(r.choice(alpha) for _ in range(r.randint(0, n)))


def lit(v):
    return repr(v)


# ---------------------------------------------------------------------------------------------
# categories: each returns an expression string

def c_intarith(r):
    a, b = rint(r), rint(r)
    op = r.choice(["+", "-", "*", "//", "%", "==", "<", "<=", ">", "!=", "divmod", "min", "max",
                   "gcd", "abs", "neg", "bool", "sum3", "cmpf", "hash", "mixed"])
    A, B = lit(a), "(%s)" % lit(b)
    if op in ("+", "-", "*", "//", "%", "==", "<", "<=", ">", "!="):
        return "(%s) %s %s" % (A, op, B)
    if op == "divmod":
        return "divmod(%s, %s)" % (A, B)
    if op in ("min", "max"):
        return "%s(%s, %s)" % (op, A, B)
    if op == "gcd":
        return "__import__('math').gcd(%s, %s)" % (A, B)
    if op == "abs":
        return "abs(%s)" % A
    if op == "neg":
        return "-(%s)" % A
    if op == "bool":
        return "bool(%s)" % A
    if op == "sum3":
        return "sum([%s, %s, %s])" % (A, B, lit(rint(r)))
    if op == "cmpf":
        return "(%s) == float(%s), (%s) < float(%s), float(%s) <= %s" % (A, B, A, B, A, B)
    if op == "hash":
        return "hash(%s) == hash(%s), hash(%s) == hash(float(%s)) if abs(%s) < 2**53 else 1" % (
            A, B, A, A, A)
    return "(%s) + %s * 1.5" % (A, B)


def c_intpow(r):
    op = r.choice(["pow", "pow3", "shl", "shr", "bitlen", "isqrt", "fact", "comb", "perm",
                   "ipow", "negpow"])
    a = rint(r, 70)
    if op == "pow":
        return "(%s) ** %d" % (lit(a), r.randint(0, 12))
    if op == "ipow":
        return "%d ** %d" % (r.randint(-12, 12), r.randint(0, 70))
    if op == "negpow":
        return "(%d) ** (-%d)" % (r.randint(-9, 9), r.randint(0, 6))
    if op == "pow3":
        return "pow(%s, %d, %s)" % (lit(a), r.randint(0, 5000), lit(abs(rint(r, 70)) + 1))
    if op == "shl":
        return "(%s) << %d" % (lit(a), r.randint(0, 130))
    if op == "shr":
        return "(%s) >> %d" % (lit(a), r.randint(0, 150))
    if op == "bitlen":
        return "(%s).bit_length()" % lit(a)
    if op == "isqrt":
        return "__import__('math').isqrt(%s)" % lit(abs(a))
    if op == "fact":
        return "__import__('math').factorial(%d)" % r.randint(0, 40)
    if op == "comb":
        n = r.randint(0, 80)
        return "__import__('math').comb(%d, %d)" % (n, r.randint(0, n))
    return "__import__('math').perm(%d, %d)" % (r.randint(0, 25), r.randint(0, 25))


def c_bitops(r):
    a, b = rint(r), rint(r)
    op = r.choice(["&", "|", "^", "&", "|", "^", "~", "mix", "aug", "bool"])
    if op == "~":
        return "~(%s)" % lit(a)
    if op == "mix":
        return "((%s) & (%s)) | ((%s) ^ ~(%s))" % (lit(a), lit(b), lit(b), lit(a))
    if op == "aug":
        return "(lambda x: [x.__iand__(1) if 0 else x, x])(%s)" % lit(a)
    if op == "bool":
        return "True %s %s, %s %s True" % (r.choice("&|^"), lit(a), lit(a), r.choice("&|^"))
    return "(%s) %s (%s)" % (lit(a), op, lit(b))


def c_intconv(r):
    a = rint(r)
    op = r.choice(["str", "repr", "hex", "oct", "bin", "fmt_d", "fmt_x", "fmt_o", "fmt_b",
                   "fmt_e", "pct", "pctx", "int_s", "int_b", "tobytes", "frombytes", "fstr",
                   "chr", "digits", "sep", "round", "index", "float", "strint"])
    A = lit(a)
    if op in ("str", "repr", "hex", "oct", "bin"):
        return "%s(%s)" % (op, A)
    if op.startswith("fmt_"):
        spec = r.choice(["", "<20", ">25", "^30", "+", " ", "010", "#", "020", ",", "_", "0=+25"])
        t = op[4:]
        if t == "d":
            t = r.choice(["d", "n"])
        if t == "e":
            t = r.choice(["e", "f", "g", "E", ".3f", ".0f", "%"])
            if t == "%":
                return "format(%s, '%s')" % (A, ".1%")
        if "#" in spec and t in "dn" and r.random() < 0.5:
            spec = spec.replace("#", "")
        return "format(%s, '%s%s')" % (A, spec, t)
    if op == "pct":
        return "'%%%s%s' %% (%s,)" % (r.choice(["", "10", "-10", "+", "010", " ", "20.5", ".3"]),
                                      r.choice("dif"), A)
    if op == "pctx":
        return "'%%%s%s' %% (%s,)" % (r.choice(["", "#", "012", "-12", "#x"]), r.choice("xXo"), A)
    if op == "int_s":
        base = r.choice([2, 8, 10, 16, 36, 7, 0])
        s = r.choice(["", "-", "+", " "]) + _tostr(abs(a), base if base else 10)
        if base == 0:
            s = r.choice(["0x", "0o", "0b", ""]) + (s if False else _tostr(abs(a), 16 if False else 10))
            s = s.replace("0x0x", "0x")
        if r.random() < 0.5:
            s = s + r.choice(["", "_", " ", "x", "\n"])
        return "int(%r, %d)" % (s, base)
    if op == "int_b":
        return "int(%r)" % (r.choice(["", "-", "+", " "]) + str(abs(a)) + r.choice(["", " ", "_1", "e1", "."]))
    if op == "tobytes":
        n = r.choice([0, 1, 2, 4, 8, 9, 16, 33])
        return "(%s).to_bytes(%d, %r, signed=%s)" % (A, n, r.choice(["big", "little"]), r.choice(["True", "False"]))
    if op == "frombytes":
        bs = bytes(r.getrandbits(8) for _ in range(r.randint(0, 20)))
        return "int.from_bytes(%r, %r, signed=%s)" % (bs, r.choice(["big", "little"]), r.choice(["True", "False"]))
    if op == "fstr":
        return "f'{%s:>{%d}}|{%s!r}|{%s:x}'" % (A, r.randint(0, 40), A, A)
    if op == "chr":
        return "chr(%d)" % r.choice([65, 97, 126, 127, 0, 31, 32, 9, 10, 0x110000, -1])
    if op == "digits":
        return "sum(int(c) for c in str(abs(%s)))" % A
    if op == "sep":
        return "format(%s, '%s')" % (A, r.choice([",", "_", ",d", "_x", "_b", "_o"]))
    if op == "round":
        return "round(%s, %d)" % (A, -r.randint(0, 25))
    if op == "index":
        return "[10, 20, 30, 40][%d:%d]" % (r.choice([-5, -1, 0, 1, 2, 10, 2 ** 31, 2 ** 40, -2 ** 40]), r.choice([-5, 1, 3, 10, 2 ** 31, 2 ** 70]))
    if op == "float":
        return "float(%s)" % A
    return "int(str(%s)) == %s" % (A, A)


def _tostr(n, base):
    digs = "0123456789abcdefghijklmnopqrstuvwxyz"
    if n == 0:
        return "0"
    s = ""
    while n:
        n, d = divmod(n, base)
        s = digs[d] + s
    return s


def c_float(r):
    x, y = rfloat(r), rfloat(r)
    op = r.choice(["repr", "str", "int", "intfl", "round", "round1", "fmt", "fmt2", "pct", "pctg",
                   "arith", "floordiv", "mod", "divmod", "pow", "cmpint", "isint", "ratio",
                   "floor", "ceil", "trunc", "fromstr", "hash", "hashint", "sum", "fsum",
                   "complexh", "abs", "bigfloat", "min", "intdiv", "truediv"])
    X, Y = lit(x), "(%s)" % lit(y)
    if op in ("repr", "str"):
        return "%s(%s)" % (op, X)
    if op == "int":
        return "int(%s)" % X
    if op == "intfl":
        return "int(%s * %s) if abs(%s * %s) < 1e300 else 0" % (X, Y, X, Y)
    if op == "round":
        return "round(%s)" % X
    if op == "round1":
        return "round(%s, %d)" % (X, r.randint(-5, 12))
    if op == "fmt":
        return "format(%s, '%s%s%s')" % (X, r.choice(["", "+", " ", "<20", ">25", "010", ",", "_"]),
                                       r.choice(["", ".3", ".0", ".10", ".17", ".20"]), r.choice(["f", "e", "g", "E", "G", "F", "%", ""]))
    if op == "fmt2":
        return "'{:.%d}'.format(%s), '{:#.3g}'.format(%s)" % (r.randint(0, 20), X, X)
    if op == "pct":
        return "'%%%s%s' %% (%s,)" % (r.choice(["", "10", "-12", "+", "012", " ", ".3", "20.5", "#.0"]), r.choice("feEgG"), X)
    if op == "pctg":
        return "'%%r %%s %%.17g %%.15g' %% (%s, %s, %s, %s)" % (X, X, X, X)
    if op == "arith":
        return "(%s) %s %s" % (X, r.choice("+-*/"), Y)
    if op == "floordiv":
        return "(%s) // %s" % (X, Y)
    if op == "mod":
        return "(%s) %% %s" % (X, Y)
    if op == "divmod":
        return "divmod(%s, %s)" % (X, Y)
    if op == "pow":
        return "(%s) ** %s" % (lit(abs(x) if r.random() < 0.5 else x), lit(r.choice([2, 3, 0.5, -1, 10, -2.5, 0, 1.5])))
    if op == "cmpint":
        return "(%s) == %s, (%s) < %s, (%s) >= %s" % (X, lit(rint(r)), X, lit(rint(r)), X, lit(rint(r)))
    if op == "isint":
        return "(%s).is_integer()" % X
    if op == "ratio":
        return "(%s).as_integer_ratio()" % X
    if op in ("floor", "ceil", "trunc"):
        return "__import__('math').%s(%s)" % (op, X)
    if op == "fromstr":
        return "float(%r)" % r.choice([repr(x), "%.17g" % x, "%e" % x, " " + repr(x) + " ", "1e400", "-1e400", "1_0.5", "nan", "-inf", "1e-400", ".5", "5.", "0x1p3"])
    if op == "hash":
        return "hash(%s) == hash(%s), hash(%s) == hash(%s)" % (X, X, X, lit(x))
    if op == "hashint":
        n = r.choice([rint(r), int(x) if abs(x) < 1e300 else 0])
        return "hash(float(%s)) == hash(%s) if float(%s) == %s else 1" % (lit(n), lit(n), lit(n), lit(n)) \
            if abs(n) < 10 ** 300 else "1"
    if op == "sum":
        return "sum([%s, %s, %s])" % (X, Y, lit(rfloat(r)))
    if op == "fsum":
        return "__import__('math').fsum([%s, %s, %s])" % (X, Y, lit(rfloat(r)))
    if op == "complexh":
        return "hash(complex(%s, 0)) == hash(%s)" % (X, X)
    if op == "abs":
        return "abs(%s), -(%s), +(%s)" % (X, X, X)
    if op == "bigfloat":
        return "float(%s ** %d)" % (r.randint(2, 99), r.randint(100, 500))
    if op == "min":
        return "min(%s, %s), max(%s, %s)" % (X, Y, X, Y)
    if op == "intdiv":
        a, b = rint(r), rint(r)
        return "(%s) / (%s)" % (lit(a), lit(b))
    a, b = rint(r), rint(r)
    return "(%s) / (%s), (%s) * 0.5, (%s) - 0.5" % (lit(a), lit(b), lit(a), lit(a))


def c_float2(r):
    # int <-> float conversions around the small-int and 2**53 boundaries
    e = r.choice([9, 10, 15, 16, 17, 18, 19, 20, 21, 22, 23, 30, 60, 100, 300])
    m = r.choice([1, 1, 2, 3, 5, 9, 1.5, 2.5, 9.99])
    x = float(m) * 10.0 ** e
    if r.random() < 0.3:
        x = float(2 ** r.randint(25, 100)) + r.choice((0, 0, 1, -1, 0.5))
    if r.random() < 0.5:
        x = -x
    X = lit(x)
    op = r.choice(["int", "round", "floor", "ceil", "fmt", "mixcmp", "intdiv", "divmod", "ratio", "hashes",
                   "range", "pow", "mul", "floordiv", "mod", "str", "repr", "fromint"])
    n = int(x)
    if op == "int":
        return "int(%s)" % X
    if op == "round":
        return "round(%s), round(%s, -3), round(%s, 2)" % (X, X, X)
    if op in ("floor", "ceil"):
        return "__import__('math').%s(%s)" % (op, X)
    if op == "fmt":
        return "'%%d %%i' %% (%s, %s), format(%s, '.0f'), '{:d}'.format(int(%s))" % (X, X, X, X)
    if op == "mixcmp":
        k = n + r.choice((-1, 0, 1, 2))
        return "(%s == %s, %s < %s, %s > %s, %s <= %s)" % (X, lit(k), X, lit(k), lit(k), X, lit(k), X)
    if op == "intdiv":
        return "%s / %s, %s // %s, %s %% %s" % (lit(n), lit(r.randint(1, 10 ** 12)), lit(n), lit(r.randint(1, 10 ** 12)), lit(n), lit(r.randint(1, 10 ** 12)))
    if op == "divmod":
        return "divmod(%s, %s)" % (X, lit(r.choice((7, 1e9, 0.3, -2.5, 3))))
    if op == "ratio":
        return "(%s).as_integer_ratio()" % X
    if op == "hashes":
        return "hash(%s) == hash(%s)" % (X, lit(n))
    if op == "range":
        return "list(range(%s, %s + 3))[:3]" % (lit(n), lit(n))
    if op == "pow":
        return "%s ** 2, 2 ** %s" % (X, lit(r.choice((1.5, 0.5, 10.0, 100.0, 1000.0, -3.0))))
    if op == "mul":
        return "%s * %s, %s + %s, %s - %s" % (lit(n), lit(r.choice((0.5, 2.0, 1e10, 3))), lit(n), X, lit(n), X)
    if op == "floordiv":
        return "%s // %s" % (X, lit(r.choice((7, 1e9, 0.3, -2.5, 3, 10 ** 15))))
    if op == "mod":
        return "%s %% %s" % (X, lit(r.choice((7, 1e9, 0.3, -2.5, 3, 10 ** 15))))
    if op == "fromint":
        return "float(%s), float(%s) == %s, int(float(%s))" % (lit(n), lit(n), lit(n), lit(n))
    return "%s(%s)" % (op, X)


def c_special(r):
    op = r.choice(["inf", "nan", "ninf", "negz", "tiny", "hugeint", "tofloat_over"])
    if op == "inf":
        return r.choice(["float('inf') > 10**400", "int(float('inf'))", "float('inf') == float('inf')",
                         "hash(float('inf'))", "hash(-float('inf'))", "float('inf') % 3", "round(float('inf'))",
                         "divmod(float('inf'), 2)", "float('inf') // 1", "int('9'*30) < float('inf')"])
    if op == "nan":
        return r.choice(["int(float('nan'))", "float('nan') == float('nan')", "round(float('nan'))",
                         "max(1, float('nan')), max(float('nan'), 1)", "sorted([3.0, 1.0, 2.0])",
                         "float('nan') < 10**30", "str(float('nan')), repr(-float('nan'))"])
    if op == "ninf":
        return r.choice(["-float('inf') < -10**400", "float('-inf') < 0", "str(float('-inf'))", "format(float('inf'), '10.2f')",
                         "format(float('nan'), '>10')", "'%f %e %g' % (float('inf'), float('-inf'), float('nan'))"])
    if op == "negz":
        return r.choice(["-0.0", "str(-0.0)", "0.0 == -0.0", "hash(-0.0) == hash(0)", "round(-0.4)", "round(-0.5)",
                         "int(-0.5)", "format(-0.0, '.1f')", "'%d' % -0.9", "round(-0.0, 2)", "-0.0 * 5",
                         "__import__('math').copysign(1, -0.0)", "__import__('math').ceil(-0.5)", "1 / -0.0 if 0 else 2"])
    if op == "tiny":
        return r.choice(["5e-324", "5e-324 / 2", "2.2250738585072014e-308 / 2", "1e-320", "float('1e-400')", "1e308 * 10",
                         "-1e308 * 10", "1.7976931348623157e308 + 1e292", "2 ** 1023 * 2.0", "float(2 ** 1023)", "float(2 ** 1024 - 1) if 0 else 1"])
    if op == "hugeint":
        return r.choice(["float(10 ** 308)", "float(10 ** 309)", "10 ** 400 / 10 ** 399", "10 ** 400 // 10 ** 398",
                         "(10 ** 400) / (3 * 10 ** 399)", "float(2 ** 1023 + 2 ** 970)", "float(2 ** 1024 - 2 ** 970)", "10 ** 308 * 1.0",
                         "10 ** 309 * 1.0", "2 ** 1024 > 1e308", "1e308 < 10 ** 309", "int(1e308) == 10 ** 308"])
    return r.choice(["2 ** 1024 + 0.0", "float(2 ** 1024)", "1e19 == 10 ** 19", "int(1e19)", "int(1.5e19)"])


def c_str(r):
    s = rstr(r)
    t = rstr(r, 4)
    op = r.choice(["slice", "upper", "lower", "title", "cap", "strip", "split", "rsplit", "splitlines", "find",
                   "rfind", "count", "replace", "join", "center", "ljust", "zfill", "isx", "startswith", "len",
                   "index", "mul", "cmp", "sorted", "partition", "format", "pct", "repr", "ascii", "encode",
                   "expandtabs", "swapcase", "casefold", "ord", "bigidx", "hash", "translate", "in", "f"])
    S = lit(s)
    T = lit(t)
    if op == "slice":
        a = r.choice([None, 0, 1, -1, 2, 5, -3, 100, -100, 2 ** 31, -2 ** 31, 2 ** 40, 2 ** 70, -2 ** 70])
        b = r.choice([None, 0, 1, -1, 3, 7, -2, 100, -100, 2 ** 31 - 1, 2 ** 33, 2 ** 70])
        c = r.choice([None, None, 1, 2, -1, -2, 3, 2 ** 31, 2 ** 40, -2 ** 40, 2 ** 70])
        return "(%s)[%s:%s:%s]" % (S, lit(a), lit(b), lit(c))
    if op in ("upper", "lower", "title", "swapcase", "casefold", "strip"):
        return "(%s).%s()" % (S, op)
    if op == "cap":
        return "(%s).capitalize()" % S
    if op == "split":
        return "(%s).split(%s, %d)" % (S, r.choice(["None", T, "' '", "','"]), r.choice([-1, 0, 1, 2, 2 ** 40, 2 ** 70]))
    if op == "rsplit":
        return "(%s).rsplit(%s, %d)" % (S, r.choice(["None", T, "' '"]), r.choice([-1, 0, 1, 2, 2 ** 40]))
    if op == "splitlines":
        return "(%s).splitlines(%s)" % (S, r.choice(["", "True"]))
    if op in ("find", "rfind", "count", "index"):
        return "(%s).%s(%s, %s, %s)" % (S, op if op != "index" else "find", T, r.choice([0, 1, -3, 2 ** 40, -2 ** 40]), r.choice([100, 2, -1, 2 ** 40, 2 ** 70]))
    if op == "replace":
        return "(%s).replace(%s, %s, %d)" % (S, T, lit(rstr(r, 3)), r.choice([-1, 0, 1, 2, 2 ** 40, 2 ** 70]))
    if op == "join":
        return "(%s).join([%s, %s, %s])" % (T, S, T, lit(rstr(r, 3)))
    if op in ("center", "ljust"):
        return "(%s).%s(%d, %r)" % (S, op, r.randint(0, 30), r.choice("*-_ 0"))
    if op == "zfill":
        return "(%s).zfill(%d)" % (S, r.randint(0, 20))
    if op == "isx":
        return "(%s).%s()" % (S, r.choice(["isalpha", "isdigit", "isalnum", "isspace", "isupper", "islower", "isidentifier", "isprintable", "isascii", "isnumeric", "isdecimal", "istitle"]))
    if op == "startswith":
        return "(%s).%s(%s), (%s).endswith(%s)" % (S, "startswith", T, S, T)
    if op == "len":
        return "len(%s)" % S
    if op == "mul":
        return "(%s) * %d, %d * (%s)" % (T, r.choice([0, 1, 3, -1, 20]), r.choice([0, 2, -5]), T)
    if op == "cmp":
        return "(%s) < (%s), (%s) == (%s), (%s) >= (%s)" % (S, T, S, S, T, S)
    if op == "sorted":
        return "sorted([%s, %s, %s])" % (S, T, lit(rstr(r, 5)))
    if op == "partition":
        return "(%s).partition(%s), (%s).rpartition(%s)" % (S, T, S, T)
    if op == "format":
        return "'{:%s%s%s}'.format(%s)" % (r.choice(["", "<", ">", "^", "*^", "-<"]), r.choice(["", "5", "20"]), r.choice(["", ".3", ".0"]) + r.choice(["", "s"]), S)
    if op == "pct":
        return "'%%%s%s|%%r|%%a' %% (%s, %s, %s)" % (r.choice(["", "10", "-10", ".2", "8.3"]), "s", S, S, S)
    if op == "repr":
        return "repr(%s), str(%s)" % (S, S)
    if op == "ascii":
        return "ascii(%s)" % S
    if op == "encode":
        return "(%s).encode(), (%s).encode('utf-8').decode('utf-8') == %s" % (S, S, S)
    if op == "expandtabs":
        return "(%s).expandtabs(%d)" % (S, r.randint(0, 9))
    if op == "ord":
        return "[ord(c) for c in %s]" % S
    if op == "bigidx":
        return "(%s)[%s]" % (S, lit(r.choice([0, -1, 2 ** 31, 2 ** 40, -2 ** 40, 2 ** 70, 5])))
    if op == "hash":
        return "hash(%s) == hash(%s)" % (S, lit(s))
    if op == "translate":
        return "(%s).translate({ord('a'): 'b', ord('b'): None, 955: 'L'})" % S
    if op == "in":
        return "(%s) in (%s), (%s) not in (%s)" % (T, S, S, T)
    return "'{!r:>20}{!s:^7}'.format(%s, %s)" % (S, T)


def c_range(r):
    def b():
        return r.choice([0, 1, -1, 5, 10, 100, 2 ** 31 - 1, 2 ** 31, 2 ** 32, 2 ** 40, 2 ** 62, 2 ** 63, 2 ** 64, 2 ** 70, 10 ** 9, 10 ** 18, 10 ** 19]) * r.choice((1, -1))

    def st():
        return r.choice([1, 1, 2, 3, -1, -2, 7, 2 ** 31, 2 ** 40, 2 ** 62, 2 ** 64, -2 ** 33, 10 ** 9])
    a, bb, s = b(), b(), st()
    R = "range(%s, %s, %s)" % (lit(a), lit(bb), lit(s))
    op = r.choice(["len", "list3", "idx", "contains", "slice", "rev", "repr", "eq", "sum", "min", "bool", "count",
                   "index", "iterlast", "lenvar", "enum", "zip", "sliceslice"])
    if op == "len":
        return "len(%s)" % R if abs(a - bb) // abs(s) < 2 ** 62 else "1"
    if op == "list3":
        return "[x for _, x in zip(range(5), %s)]" % R
    if op == "idx":
        return "(%s)[%s]" % (R, lit(r.choice([0, 1, -1, 2, 3, 2 ** 31, 2 ** 40, 2 ** 62, -2 ** 62])))
    if op == "contains":
        return "%s in %s" % (lit(r.choice([a, a + s, bb, 0, a + 2 * s, a + s + 1, 2 ** 40])), R)
    if op == "slice":
        return "%s[%s:%s:%s]" % (R, lit(r.choice([None, 0, 1, 2 ** 31, -2 ** 40, 2 ** 70])), lit(r.choice([None, 3, -1, 2 ** 40, 2 ** 70])), lit(r.choice([None, 1, 2, -1, 2 ** 33])))
    if op == "rev":
        return "[x for _, x in zip(range(3), reversed(%s))]" % R if abs(a - bb) // abs(s) < 2 ** 62 else "1"
    if op == "repr":
        return "repr(%s), str(range(%s)), repr(range(%s, %s))" % (R, lit(a), lit(a), lit(bb))
    if op == "eq":
        return "%s == range(%s, %s, %s), %s == range(0)" % (R, lit(a), lit(bb), lit(s), R)
    if op == "sum":
        return "sum(%s)" % R if abs(a - bb) // abs(s) < 300 else "0"
    if op == "min":
        return "(min(%s), max(%s)) if len(%s) < 300 and len(%s) else 0" % (R, R, R, R)
    if op == "bool":
        return "bool(%s)" % R
    if op == "count":
        return "%s.count(%s)" % (R, lit(r.choice([a, a + s, 0, 7])))
    if op == "index":
        return "%s.index(%s)" % (R, lit(r.choice([a, a + s, a + 3 * s, 0])))
    if op == "iterlast":
        return "next(iter(%s))" % R
    if op == "lenvar":
        return "len(range(%d)), len(range(%s, 0, -%d))" % (r.choice([0, 10, 2 ** 31, 2 ** 40]), lit(r.choice([2 ** 31, 2 ** 40])), r.choice([1, 3, 2 ** 20]))
    if op == "enum":
        return "list(enumerate(range(3), %s))" % lit(r.choice([0, 2 ** 31 - 1, 2 ** 40, -2 ** 40, 10 ** 18, 2 ** 70]))
    if op == "zip":
        return "list(zip(range(2), 'ab', [%s, %s]))" % (lit(a), lit(bb))
    return "%s[1:3][::-1] if len(%s) > 3 else 1" % (R, R)


def c_seq(r):
    ls = [rint(r, 40) for _ in range(r.randint(0, 8))]
    fl = [rfloat(r) for _ in range(r.randint(0, 6))]
    mix = ls + [float(x) for x in ls[:2]] + [x for x in fl if x == x]
    r.shuffle(mix)
    op = r.choice(["sorted", "sortrev", "sortkey", "listops", "dict", "set", "setops", "dictkeys", "counter",
                   "minmax", "sum", "index", "slice", "tuple", "sortmix", "dictint", "setmix", "bigmul", "reversed",
                   "ops", "dictcmp", "bisect", "heapq", "any", "listcmp", "insert", "bigidx", "repeat", "sortedstr", "unpack"])
    L = lit(ls)
    if op == "sorted":
        return "sorted(%s)" % L
    if op == "sortrev":
        return "sorted(%s, reverse=True)" % L
    if op == "sortkey":
        return "sorted(%s, key=lambda v: (v %% 7, -v))" % L
    if op == "listops":
        return "(lambda l: (l.sort(), l.reverse(), l.append(%s), l.extend([1, 2]), l.insert(2, %s), l.pop(), l.count(1), l)[-1])(list(%s))" % (lit(rint(r)), lit(rint(r)), L)
    if op == "dict":
        return "sorted({v: v * 2 for v in %s}.items())" % L
    if op == "set":
        return "sorted(set(%s))" % L
    if op == "setops":
        m = [rint(r, 40) for _ in range(5)]
        return "sorted(set(%s) | set(%s)), sorted(set(%s) & set(%s)), sorted(set(%s) - set(%s)), sorted(set(%s) ^ set(%s))" % (L, lit(m), L, lit(m), L, lit(m), L, lit(m))
    if op == "dictkeys":
        return "list({v: 1 for v in %s}.keys()) == list(dict.fromkeys(%s))" % (L, L)
    if op == "counter":
        return "sorted(__import__('collections').Counter(%s).items())" % L
    if op == "minmax":
        return "(min(%s), max(%s)) if %s else 0" % (L, L, L)
    if op == "sum":
        return "sum(%s), sum(%s, 5), sum(%s, 0.5)" % (L, L, L)
    if op == "index":
        return "(%s + [5]).index(5), (%s + [5, 5]).count(5)" % (L, L)
    if op == "slice":
        return "(%s)[%s:%s:%s]" % (L, lit(r.choice([None, 1, -2, 2 ** 40, -2 ** 40, 2 ** 70])), lit(r.choice([None, 3, -1, 2 ** 40, 2 ** 70])), lit(r.choice([None, 1, 2, -1, 2 ** 33, -2 ** 70])))
    if op == "tuple":
        return "tuple(%s), tuple(%s) < tuple(%s) + (1,)" % (L, L, L)
    if op == "sortmix":
        return "sorted(%s)" % lit(mix)
    if op == "dictint":
        a = rint(r, 70)
        return "{%s: 1, %s: 2, %s: 3}[%s]" % (lit(a), lit(float(a)) if abs(a) < 2 ** 1000 else "0.5", lit(a + 1), lit(a))
    if op == "setmix":
        a = rint(r, 70)
        return "len({%s, %s, %s, %s})" % (lit(a), lit(float(a)), "True" if a == 1 else lit(a), lit(a + 0))
    if op == "bigmul":
        return "[1, 2] * %s, (3,) * %d, 'ab' * %d" % (lit(r.choice([0, 1, -1, 3])), r.choice([0, 2, -2]), r.choice([0, 3]))
    if op == "reversed":
        return "list(reversed(%s))" % L
    if op == "ops":
        return "%s + %s, %s * 2, len(%s), %s == %s" % (L, L, L, L, L, L)
    if op == "dictcmp":
        return "{%s: 1} == {%s: 1}, {1: 2} | {%s: 4}" % (lit(rint(r)), lit(rint(r)), lit(rint(r)))
    if op == "bisect":
        return "__import__('bisect').bisect_left(sorted(%s), %s)" % (L, lit(rint(r, 40)))
    if op == "heapq":
        return "(lambda h: (__import__('heapq').heapify(h), [__import__('heapq').heappop(h) for _ in range(len(h))])[1])(list(%s))" % L
    if op == "any":
        return "any(%s), all(%s)" % (L, L)
    if op == "listcmp":
        return "%s < %s, %s == %s, [%s] <= [%s]" % (L, lit(ls[::-1]), L, L, lit(rint(r)), lit(float(rint(r, 50))))
    if op == "insert":
        return "(lambda l: (l.insert(%s, 9), l)[1])(list(%s))" % (lit(r.choice([0, -1, 2 ** 40, -2 ** 40, 2 ** 70, 3])), L)
    if op == "bigidx":
        return "(%s + [0])[%s]" % (L, lit(r.choice([0, -1, 2 ** 31, 2 ** 40, 2 ** 70, -2 ** 70, 2 ** 63, 2 ** 64])))
    if op == "repeat":
        return "[0] * %s" % lit(r.choice([3, 2 ** 40, 2 ** 62, 2 ** 70, -2 ** 70]))
    if op == "sortedstr":
        return "sorted(map(str, %s))" % L
    return "(lambda a, *b: (a, b))(*(%s + [1]))" % L


def c_struct(r):
    code = r.choice("bBhHiIlLqQnNxc?fd")
    byo = r.choice(["", "@", "=", "<", ">", "!"])
    if byo in ("", "@") and code in "lLnN":
        byo = "<"
    sizes = {"b": 8, "B": 8, "h": 16, "H": 16, "i": 32, "I": 32, "l": 32, "L": 32, "q": 64, "Q": 64,
             "n": 64, "N": 64}
    if code in "fd":
        v = rfloat(r)
        if code == "f" and abs(v) > 3e38:
            v = 1.5
        fmt = byo + code
        return "(lambda b: (b, __import__('struct').unpack(%r, b)))(__import__('struct').pack(%r, %s))" % (fmt, fmt, lit(v))
    if code == "x":
        return "__import__('struct').pack(%r), __import__('struct').calcsize(%r)" % (byo + "x", byo + "xi")
    if code == "c":
        return "__import__('struct').pack(%r, %r)" % (byo + "c", bytes([r.getrandbits(8)]))
    if code == "?":
        return "__import__('struct').pack(%r, %s)" % (byo + "?", lit(rint(r, 4)))
    bits = sizes[code]
    if byo in ("", "@") and code in "lL":
        bits = 32
    signed = code.islower()
    if r.random() < 0.8:
        v = rint(r, bits)
        v = v & ((1 << bits) - 1)
        if signed and v >> (bits - 1):
            v -= 1 << bits
    else:
        v = rint(r)
    fmt = byo + code
    if r.random() < 0.3:
        return "__import__('struct').calcsize(%r), __import__('struct').calcsize(%r)" % (fmt, byo + "bi" + code + "q")
    return "(lambda b: (b, __import__('struct').unpack(%r, b)))(__import__('struct').pack(%r, %s))" % (fmt, fmt, lit(v))


def c_json(r):
    def val(d=0):
        k = r.random()
        if k < 0.3:
            return rint(r)
        if k < 0.5:
            return rfloat(r) if r.random() < 0.8 else r.choice([1e22, 1e16, 12345678912345.0])
        if k < 0.65:
            return rstr(r, 8)
        if k < 0.7:
            return r.choice([True, False, None])
        if d > 2:
            return 0
        if k < 0.85:
            return [val(d + 1) for _ in range(r.randint(0, 4))]
        return {rstr(r, 4): val(d + 1) for _ in range(r.randint(0, 3))}
    v = val()
    op = r.choice(["dumps", "dumpsind", "roundtrip", "loadstr", "sort", "ascii", "loadbig"])
    if op == "dumps":
        return "__import__('json').dumps(%s)" % lit(v)
    if op == "dumpsind":
        return "__import__('json').dumps(%s, indent=%d, sort_keys=True, ensure_ascii=%s)" % (lit(v), r.randint(0, 3), r.choice(["True", "False"]))
    if op == "roundtrip":
        return "__import__('json').loads(__import__('json').dumps(%s)) == %s" % (lit(v), lit(v))
    if op == "sort":
        return "__import__('json').dumps(%s, sort_keys=True, separators=(',', ':'))" % lit({str(rint(r, 20)): rint(r, 20) for _ in range(4)})
    if op == "ascii":
        return "__import__('json').dumps(%s), __import__('json').dumps(%s, ensure_ascii=False)" % (lit(rstr(r, 10)), lit(rstr(r, 10)))
    if op == "loadbig":
        s = r.choice(["123456789012345678901234567890", "-9223372036854775809", "4294967296", "-2147483649", "1e5", "1E400",
                      "-0", "-0.0", "1.5e300", "12345678901234567890.5", "[1, 2.5, 3e2]", '{"a": 18446744073709551616}',
                      "NaN", "Infinity", "9007199254740993", "0.1e-5", "1.0e+2", "100000000000000000000.0"])
        return "__import__('json').loads(%r)" % s
    s = r.choice([str(rint(r)), repr(rfloat(r)), '"%s"' % r.choice(["a", "\\u00e9", "\\ud83d\\ude00", "\\n\\t", "\\u0041\\/"]), "[1,2,{\"a\":[]}]", " { \"k\" : [ 1 , 2 ] } ", "tru", "[1,]", "{'a':1}", "01", "1.", ".5", "-", "\"\\x\"", "[1 2]", "nul"])
    return "__import__('json').loads(%r)" % s


def c_re(r):
    pats = [r"\d+", r"[a-z]+", r"(\w)(\d)", r"a|b", r"x*", r"\s+", r"(?:ab)+", r"[^a-c]", r"\bfoo\b", r"(a)(b)?", r"[A-Z][a-z]*", r"\w+@\w+", r"a{2,3}", r"(?P<n>\d+)-(?P=n)", r"^\s*$", r".", r"(?i)abc", r"[\d_]+", r"\D+", r"\W", r"[a-fA-F0-9]+", r"\x41", r"\u00e9", r"[\w-]+", r"[.-]", r"a.*?b", r"(?<=a)b", r"b(?!c)", r"\Aab", r"a\Z", "\\$", r"[\]a]", r"[a\-z]", r"\\", r"\.", r"(?s).+", r"(?m)^a"]
    texts = ["abc 123 foo", "AbC_9 x@y", "aaab aab", "foo foobar foo", "a-b_c.d", "12-12 7-8", "", "  \t\n", "x" * 5, "ABCabc", "caf\u00e9 \u03b1\u03b2", "ab\nab\n", "deadBEEF 0xAF", "a\\b.c$d]e"]
    p = r.choice(pats)
    t = r.choice(texts) + r.choice(["", " ", "ab", "7"])
    op = r.choice(["findall", "sub", "search", "split", "match", "fullmatch", "finditer", "subn", "span", "groups", "escape", "compile", "subfn", "maxsplit"])
    P, T = lit(p), lit(t)
    if op == "findall":
        return "__import__('re').findall(%s, %s)" % (P, T)
    if op == "sub":
        return "__import__('re').sub(%s, %s, %s)" % (P, lit(r.choice(["-", "<\\g<0>>", "", "\\\\", "x\\n"])), T)
    if op == "search":
        return "(lambda m: m and (m.group(), m.span(), m.groups(), m.start(), m.end()))(__import__('re').search(%s, %s))" % (P, T)
    if op == "split":
        return "__import__('re').split(%s, %s)" % (P, T)
    if op == "match":
        return "(lambda m: m and (m.group(0), m.span()))(__import__('re').match(%s, %s))" % (P, T)
    if op == "fullmatch":
        return "bool(__import__('re').fullmatch(%s, %s))" % (P, T)
    if op == "finditer":
        return "[(m.start(), m.group()) for m in __import__('re').finditer(%s, %s)]" % (P, T)
    if op == "subn":
        return "__import__('re').subn(%s, 'X', %s, %s)" % (P, T, r.choice([0, 1, 2, 2 ** 40, 2 ** 70]))
    if op == "span":
        return "[m.span() for m in __import__('re').finditer(%s, %s)]" % (P, T)
    if op == "groups":
        return "(lambda m: m and (m.groups(), m.lastindex, m.groupdict()))(__import__('re').search(%s, %s))" % (P, T)
    if op == "escape":
        return "__import__('re').escape(%s)" % lit(rstr(r, 10))
    if op == "compile":
        return "__import__('re').compile(%s, %d).pattern, __import__('re').compile(%s).flags" % (P, r.choice([0, 2, 8, 16]), P)
    if op == "subfn":
        return "__import__('re').sub(%s, lambda m: m.group(0).upper(), %s)" % (P, T)
    return "__import__('re').split(%s, %s, %d)" % (P, T, r.choice([0, 1, 2, 2 ** 40]))


def c_misc(r):
    op = r.choice(["bytes", "bytearr", "hexb", "sorted_bytes", "complex", "fraction", "decimal", "divmod_neg", "int_methods",
                   "isinstance", "bool_ops", "enumerate", "zip", "map", "slice_obj", "len_big", "ascii", "chr_ord", "round_half",
                   "isqrt", "mathfn", "str_int_limit", "ternary", "format_misc", "hash_tuple", "hash_str", "hash_none", "ndigits",
                   "bytes_big", "dictorder", "time", "random"])
    if op == "bytes":
        return "bytes([%s]), bytes(%d), bytes(%r, 'utf-8')" % (", ".join(str(r.randint(0, 255)) for _ in range(r.randint(0, 6))), r.randint(0, 5), rstr(r, 5))
    if op == "bytearr":
        return "bytearray(b'abc')[%s:%s]" % (lit(r.choice([0, 1, 2 ** 40, -2 ** 40])), lit(r.choice([2, 2 ** 40, 2 ** 70])))
    if op == "hexb":
        b = bytes(r.getrandbits(8) for _ in range(r.randint(0, 8)))
        return "%r.hex(), bytes.fromhex(%r), int.from_bytes(%r, 'big')" % (b, b.hex(), b)
    if op == "sorted_bytes":
        return "sorted(b'hello'), b'abc' < b'abd', b'a' * 3"
    if op == "complex":
        a, b = rfloat(r), rfloat(r)
        return r.choice(["complex(%s, %s)" % (lit(a), lit(b)), "complex(%s, %s) * 1j" % (lit(a), lit(b)), "abs(complex(%s, %s))" % (lit(a % 1000), lit(b % 1000)),
                         "hash(complex(%s, 0)) == hash(%s)" % (lit(float(rint(r, 60))), lit(float(rint(r, 60)))), "str(complex(%s, %s))" % (lit(a), lit(b)),
                         "complex(%s, %s) == complex(%s, %s)" % (lit(a), lit(b), lit(a), lit(b)), "complex(%s, 0) == %s" % (lit(float(rint(r, 60))), lit(rint(r, 60)))])
    if op == "fraction":
        a, b = rint(r, 40), abs(rint(r, 40)) + 1
        return r.choice(["str(__import__('fractions').Fraction(%s, %s))" % (lit(a), lit(b)),
                         "__import__('fractions').Fraction(%s) + __import__('fractions').Fraction(%s, %s)" % (lit(rfloat(r)) if abs(rfloat(r)) < 1e300 else "0.5", lit(a), lit(b)),
                         "__import__('fractions').Fraction(%s, %s) * %s" % (lit(a), lit(b), lit(rint(r, 40))),
                         "float(__import__('fractions').Fraction(%s, %s))" % (lit(a), lit(b)),
                         "__import__('fractions').Fraction(%s).limit_denominator(1000)" % lit(abs(r.uniform(-10, 10))),
                         "hash(__import__('fractions').Fraction(%s, %s)) == hash(%s / %s) if %s == 1 else 1" % (lit(a), lit(b), lit(a), lit(b), lit(b))])
    if op == "decimal":
        return "str(__import__('decimal').Decimal(%s) + __import__('decimal').Decimal(%s))" % (repr(str(rint(r, 80))), repr(str(r.randint(0, 100))))
    if op == "divmod_neg":
        a, b = rint(r, 66), rint(r, 66)
        return "divmod(%s, %s) if %s else 0, (%s) // (%s) if %s else 0" % (lit(a), lit(b), lit(b), lit(a), lit(b), lit(b))
    if op == "int_methods":
        a = rint(r)
        return r.choice(["(%s).bit_length()" % lit(a), "(%s).conjugate()" % lit(a), "(%s).real, (%s).imag" % (lit(a), lit(a)), "(%s).numerator, (%s).denominator" % (lit(a), lit(a)),
                         "(%s).__abs__()" % lit(a), "(%s).bit_count()" % lit(a), "(%s).as_integer_ratio()" % lit(a), "(%s).__index__()" % lit(a),
                         "int.__add__(%s, %s)" % (lit(a), lit(rint(r)))])
    if op == "isinstance":
        return "isinstance(%s, int), isinstance(%s, float), type(%s).__name__, isinstance(True, int)" % tuple(lit(rint(r)) for _ in range(3))
    if op == "bool_ops":
        return "True + True, True * %s, False - %s, -True, ~True, True << %d, True & %s" % (lit(rint(r)), lit(rint(r)), r.randint(0, 70), lit(rint(r)))
    if op == "enumerate":
        return "list(enumerate('ab', %s))" % lit(r.choice((0, 2 ** 31 - 1, 2 ** 31, 2 ** 40, 10 ** 18, 10 ** 19)))
    if op == "zip":
        return "list(zip([%s, %s], [%s, %s]))" % tuple(lit(rint(r)) for _ in range(4))
    if op == "map":
        return "list(map(lambda v: v * v, [%s, %s, %s]))" % tuple(lit(rint(r)) for _ in range(3))
    if op == "slice_obj":
        return "slice(%s, %s, %s).indices(%s)" % (lit(r.choice([None, 1, -1, 2 ** 40, 2 ** 70, -2 ** 70])), lit(r.choice([None, 5, -3, 2 ** 40, 2 ** 70])), lit(r.choice([None, 1, 2, -1, -2 ** 40, 2 ** 70])), lit(r.choice([0, 5, 100, 2 ** 31, 2 ** 40])))
    if op == "len_big":
        return "len(range(%s, %s))" % (lit(r.choice([0, 10, 2 ** 40])), lit(r.choice([5, 2 ** 40, 2 ** 50])))
    if op == "ascii":
        return "ascii(%s)" % lit(rstr(r, 10))
    if op == "chr_ord":
        return "ord(chr(%d)), len(chr(%d))" % tuple([r.choice([65, 127, 0, 10, 32])] * 2)
    if op == "round_half":
        return "[round(x / 2) for x in range(-7, 8)], round(%s), round(%s, 1), round(%s, -1)" % (lit(r.choice([0.5, 1.5, 2.5, -0.5, 2.675, 1e15 + 0.5, 4503599627370497.0, 2.5e15])), lit(r.choice([0.25, 0.35, 2.675, 1.05])), lit(r.choice([15, 25, 35, 14.5, 1e17, 1.5e17])))
    if op == "isqrt":
        return "__import__('math').isqrt(%s), __import__('math').gcd(%s, %s), __import__('math').lcm(%s, %s)" % (lit(abs(rint(r, 120))), lit(rint(r, 90)), lit(rint(r, 90)), lit(rint(r, 40)), lit(rint(r, 40)))
    if op == "mathfn":
        x = abs(rfloat(r)) + 0.001
        return r.choice(["__import__('math').floor(%s)" % lit(x), "__import__('math').sqrt(%s) ** 2 > 0" % lit(x), "__import__('math').ldexp(%s, %d)" % (lit(r.random()), r.randint(-1100, 1100)),
                         "__import__('math').frexp(%s)" % lit(x), "__import__('math').modf(%s)" % lit(x), "__import__('math').fmod(%s, %s)" % (lit(x), lit(r.choice((3.0, 0.7)))),
                         "__import__('math').copysign(%s, -1)" % lit(x), "__import__('math').isclose(%s, %s)" % (lit(x), lit(x)), "__import__('math').prod([%s, %s, 3])" % (lit(rint(r, 40)), lit(rint(r, 40))),
                         "__import__('math').trunc(%s)" % lit(x * 1e12), "__import__('math').dist([0, 0], [3, 4])", "__import__('math').hypot(3, 4)", "__import__('math').log2(%d)" % 2 ** r.randint(0, 200),
                         "__import__('math').log(%s)" % lit(10 ** r.randint(1, 400)), "__import__('math').log10(%s)" % lit(10 ** r.randint(1, 400)), "__import__('math').sqrt(%s)" % lit(10 ** r.randint(1, 400)),
                         "__import__('math').exp(0), __import__('math').pow(2, 10), __import__('math').degrees(0)", "__import__('math').ulp(%s)" % lit(x), "__import__('math').nextafter(%s, 0) < %s" % (lit(x), lit(x))])
    if op == "str_int_limit":
        return "len(str(10 ** %d)), len(str(7 ** %d)), str(10 ** %d)[-3:]" % (r.randint(1, 600), r.randint(1, 400), r.randint(1, 4000))
    if op == "ternary":
        return "[i for i in range(%d, %d, %d)]" % (r.randint(-5, 5), r.randint(-5, 15), r.choice((1, 2, 3, -1)))
    if op == "format_misc":
        return "'{:,}'.format(%s), '{:_}'.format(%s), '{:08.3f}'.format(%s), '{:+.2e}'.format(%s), '{!r:>8}'.format(%s), '{0:{1}}'.format(%s, 10)" % (
            lit(rint(r)), lit(rint(r)), lit(rfloat(r) % 1000), lit(rfloat(r)), lit(rstr(r, 3)), lit(rint(r)))
    if op == "hash_tuple":
        return "hash((%s, %s)) == hash((%s, %s)), hash(()) == hash(())" % ((lit(rint(r)), lit(rint(r))) * 2)
    if op == "hash_str":
        return "hash('abc') == hash('abc'), hash(b'abc') == hash(b'abc'), hash('') == 0"
    if op == "hash_none":
        return "hash(None) == hash(None), hash(True) == 1, hash(False) == 0, hash(-1) == -2, hash(-2) == -2, hash(2**61 - 1), hash(2**61), hash(-2**61), hash(2**64), hash(-(2**64)), hash(10**20), hash(1.5), hash(-1.5), hash(0.1), hash(1e300), hash(float('inf')), hash(3 + 4j), hash(2**61 - 2), hash(10**40)"
    if op == "ndigits":
        return "len(str(%s)), len(repr(%s)), len(hex(%s)), len(bin(%s))" % tuple([lit(rint(r))] * 4)
    if op == "bytes_big":
        return "b'ab' * %s" % lit(r.choice([0, 3, -1]))
    if op == "dictorder":
        ks = [rint(r, 40) for _ in range(6)]
        return "list({k: 0 for k in %s}), list({k: 0 for k in %s}.values()), [k for k in %s if k > 5]" % (lit(ks), lit(ks), lit(ks))
    if op == "time":
        return "type(__import__('time').time()).__name__, __import__('time').time() > 1e9, __import__('time').perf_counter() >= 0, type(__import__('time').monotonic()).__name__, int(__import__('time').time()) > 1700000000, __import__('time').time_ns() > 10 ** 18 if hasattr(__import__('time'), 'time_ns') else 1"
    return "(lambda m: [m.seed(%s), 0 <= m.random() < 1, 1 <= m.randint(1, 6) <= 6, m.randrange(%s) >= 0, len(m.sample(range(100), 5)), m.getrandbits(%d) < 2 ** %d][1:])(__import__('random'))" % (
        lit(rint(r)), lit(r.choice([10, 2 ** 31, 2 ** 40, 10 ** 20])), 40, 40)


def c_lex(r):
    op = r.choice(["hex", "oct", "bin", "us", "fl", "big", "cplx", "cmp", "neg", "pow", "chain", "esc", "bytes_lit", "ops"])
    v = rint(r)
    a = abs(v)
    if op == "hex":
        return "0x%x, 0X%X, -0x%x" % (a, a, a)
    if op == "oct":
        return "0o%o, 0O%o" % (a, a)
    if op == "bin":
        return "0b%s, 0B%s" % (bin(a)[2:], bin(a)[2:])
    if op == "us":
        return "%s, 0x%s" % (format(a, "_d"), format(a, "_x"))
    if op == "fl":
        f = rfloat(r)
        return "%r, %s, %s" % (f, repr(f).replace("e", "E"), "%.17g" % f if abs(f) < 1e300 else "1e300")
    if op == "big":
        return "%d, %d.0, %de0, %d/1" % (a, a % 10 ** 300, a % 10 ** 30, a)
    if op == "cplx":
        return "%dj, %rj, %r + %rj" % (a % 1000, abs(rfloat(r)), rfloat(r), abs(rfloat(r)))
    if op == "cmp":
        return "%d < %d <= %d, %d == %d.0" % (a, a + 1, a + 2, a % 2 ** 53, a % 2 ** 53)
    if op == "neg":
        return "-%d, - -%d, ~%d, +%d, -(-%d)" % (a, a, a, a, a)
    if op == "pow":
        return "%d ** 2, -%d ** 2, (-%d) ** 3, 2 ** %d" % (a % 10 ** 12, a % 10 ** 12, a % 10 ** 6, r.randint(0, 200))
    if op == "chain":
        return "1 if %d else 2, %d if %d > %d else %d" % (a, a, a, a - 1, a + 1)
    if op == "esc":
        return repr("\\x%02x\\u%04x\\N{DIGIT ONE}" % (r.randint(1, 127), r.randint(1, 0xD7FF))).replace("\\\\", "\\") if False else repr("a\\x41b\\u0042c\\101d\\n\\t\\\\")
    if op == "bytes_lit":
        return "b'\\x00\\xff\\x7f' + bytes([%d, %d]), len(b'%s')" % (r.randint(0, 255), r.randint(0, 255), "ab" * r.randint(0, 5))
    return "%d // 7 * 7 + %d %% 7 == %d" % (a, a, a)


def c_numstr(r):
    op = r.choice(["int", "float", "int_base", "float_ws", "float_exp", "intbig", "fmtround"])
    if op == "int":
        v = rint(r)
        s = r.choice(["%d", " %d ", "+%d", "%d\n", "%s", "00%d", "%d_0", "%d__0", "_%d", "%d_"]) 
        if "%s" in s:
            s = s % str(v)
        else:
            s = s % v
        return "int(%r)" % s
    if op == "float":
        f = rfloat(r)
        return "float(%r), float(%r)" % (repr(f), "%.17g" % f if abs(f) < 1e300 else "1e300")
    if op == "int_base":
        base = r.choice([2, 8, 16, 36, 3, 0, 10, 7])
        v = abs(rint(r))
        digs = _tostr(v, base if base else 16)
        pre = ""
        if base == 0:
            pre = r.choice(["0x", "0o", "0b"]) if False else ""
            digs = _tostr(v, 10)
        if base == 16 and r.random() < 0.5:
            pre = "0x"
        if base == 8 and r.random() < 0.5:
            pre = "0o"
        if base == 2 and r.random() < 0.5:
            pre = "0b"
        return "int(%r, %d)" % (r.choice(["", "-", "+"]) + pre + digs, base)
    if op == "float_ws":
        return "float(%r)" % r.choice([" 1.5 ", "1_000.5", "1__0", "1e5", "1E-5", ".5", "5.", "+.5e1", "-inf", "+Infinity", "nan", "-nan", "1e", "e5", "0x10", "1 2", "", " ", "1_0e1_0", "١٢٣", "1e999", "-1e999", "4.9e-324", "2e-324", "1.7976931348623158e308", "1.7976931348623159e308"])
    if op == "float_exp":
        return "float('%de%d')" % (r.randint(1, 10 ** 17), r.randint(-340, 320))
    if op == "intbig":
        return "int('%s'), int('-%s')" % ("9" * r.randint(1, 80), "1" + "0" * r.randint(1, 80))
    return "round(%s, %d), '%%.%df' %% %s" % (lit(rfloat(r)), r.randint(0, 6), r.randint(0, 8), lit(rfloat(r)))


def c_hashmix(r):
    n = r.choice([0, 1, 2, 3, 7, -1, 255, 2 ** 31, 2 ** 32 + 1, 2 ** 53, 2 ** 53 + 1, 2 ** 62, 2 ** 63, 2 ** 64, 10 ** 18, 10 ** 19, 10 ** 30, -2 ** 61, 2 ** 61 - 1, 2 ** 100]) + r.choice((0, 0, 1))
    x = repr(n)
    op = r.choice(["set", "dict", "eqhash", "fraction", "decimal", "complex", "bool", "in", "tuple", "frozen", "counter", "intersect", "float"])
    f = "float(%s)" % x if abs(n) < 10 ** 300 else "0.5"
    if op == "set":
        return "len({%s, %s, %s, complex(%s, 0)}), sorted({%s, %s}) == sorted({%s})" % (x, f, f, x, x, f, x)
    if op == "dict":
        return "{%s: 1, %s: 2}, {%s: 1}[%s]" % (x, f, f, x)
    if op == "eqhash":
        return "(%s == %s) == (hash(%s) == hash(%s)), hash(%s) == hash(%s + 0)" % (x, f, x, f, x, x)
    if op == "fraction":
        return "hash(__import__('fractions').Fraction(%s)) == hash(%s), __import__('fractions').Fraction(%s) == %s" % (x, x, x, f)
    if op == "decimal":
        return "hash(__import__('decimal').Decimal(%s)) == hash(%s), __import__('decimal').Decimal(%s) == %s" % (x, x, x, x)
    if op == "complex":
        return "hash(complex(%s, 0)) == hash(%s), complex(%s, 0) == %s" % (f, f, f, x)
    if op == "bool":
        return "{True: 1}.get(1), {1: 2}.get(True), {0: 1}.get(False), {1.0: 3}.get(True), hash(True) == hash(1.0)"
    if op == "in":
        return "%s in [%s], %s in (%s,), %s in {%s}, %s in {%s: 1}" % (x, f, f, x, x, f, f, x)
    if op == "tuple":
        return "hash((%s, 1)) == hash((%s, 1)), {(%s, 1): 5}[(%s, 1)]" % (x, f, x, f)
    if op == "frozen":
        return "frozenset([%s]) == frozenset([%s]), hash(frozenset([%s])) == hash(frozenset([%s]))" % (x, f, x, f)
    if op == "counter":
        return "__import__('collections').Counter([%s, %s, %s])" % (x, f, x)
    if op == "intersect":
        return "{%s} & {%s}, {%s} | {%s}" % (x, f, x, f)
    return "hash(%s) , hash(%s), hash(-%s), hash(%s * 0.5), hash(%s / 3)" % (f, x, x, x, x)


def c_iter(r):
    op = r.choice(["islice", "count", "repeat", "accumulate", "product", "comb", "perm", "reduce", "deque", "chain", "zip_longest", "starmap", "takewhile", "groupby", "cycle", "pairwise", "compress", "tee"])
    big = r.choice([2 ** 31, 2 ** 40, 2 ** 62, 10 ** 10, 3, 10 ** 20])
    I = "__import__('itertools')"
    if op == "islice":
        return "list(%s.islice(range(100), %s, %s, %s))" % (I, r.choice([0, 2, 2 ** 31, 2 ** 40]), r.choice([5, 10, 2 ** 40, None]), r.choice([1, 3, 2 ** 40]))
    if op == "count":
        return "list(%s.islice(%s.count(%s, %s), 3))" % (I, I, r.choice([0, 2 ** 31 - 2, 2 ** 40, 10 ** 18, 10 ** 19, -5, 0.5, 2 ** 63 - 1]), r.choice([1, 2, 2 ** 31, 0.25, -3]))
    if op == "repeat":
        return "list(%s.repeat('a', %d)), list(%s.islice(%s.repeat(1), %d))" % (I, r.randint(0, 5), I, I, r.randint(0, 5))
    if op == "accumulate":
        return "list(%s.accumulate([%s, %s, %s]))" % (I, lit(rint(r)), lit(rint(r)), lit(rint(r)))
    if op == "product":
        return "len(list(%s.product(range(%d), repeat=2)))" % (I, r.randint(0, 10))
    if op == "comb":
        return "list(%s.combinations(range(5), %d))[:5]" % (I, r.randint(0, 5))
    if op == "perm":
        return "list(%s.permutations(range(4), %d))[-2:]" % (I, r.randint(0, 4))
    if op == "reduce":
        return "__import__('functools').reduce(lambda a, b: a * b, [%s, %s, %s, %s], 1)" % tuple(lit(rint(r, 40)) for _ in range(4))
    if op == "deque":
        return "list(__import__('collections').deque(range(10), maxlen=%d))" % r.randint(0, 12)
    if op == "chain":
        return "list(%s.chain(range(2), [%s], 'a'))" % (I, lit(rint(r)))
    if op == "zip_longest":
        return "list(%s.zip_longest(range(2), 'abc', fillvalue=%s))" % (I, lit(rint(r)))
    if op == "starmap":
        return "list(%s.starmap(pow, [(2, %d), (3, %d)]))" % (I, r.randint(0, 80), r.randint(0, 50))
    if op == "takewhile":
        return "list(%s.takewhile(lambda v: v < %s, [1, 5, 9, 2]))" % (I, lit(r.choice([3, 6, 10, 2 ** 40])))
    if op == "groupby":
        return "[(k, len(list(g))) for k, g in %s.groupby(sorted([%s, %s, %s, %s]))]" % (I, lit(rint(r, 3)), lit(rint(r, 3)), lit(rint(r, 3)), lit(rint(r, 3)))
    if op == "cycle":
        return "list(%s.islice(%s.cycle([%s, %s]), 5))" % (I, I, lit(rint(r)), lit(rint(r)))
    if op == "pairwise":
        return "list(%s.pairwise([%s, %s, %s]))" % (I, lit(rint(r)), lit(rint(r)), lit(rint(r)))
    if op == "compress":
        return "list(%s.compress('abc', [1, 0, %s]))" % (I, lit(rint(r, 3)))
    return "[list(t) for t in %s.tee(range(3), 2)]" % I


def c_dt(r):
    D = "__import__('datetime')"
    ts = r.choice([0, 1, 86399, 2 ** 31 - 1, 2 ** 31, 2 ** 32, 1700000000, 4102444800, 253402300799, -1, -86400, 951782400, 1e9, 1.5, 2 ** 32 + 0.5])
    op = r.choice(["utc", "td", "ordinal", "iso", "arith", "weekday", "strftime", "tdsec", "replace", "date"])
    if op == "utc":
        return "%s.datetime.fromtimestamp(%s, %s.timezone.utc).isoformat() if %s >= 0 else 0" % (D, lit(ts), D, lit(ts))
    if op == "td":
        return "%s.timedelta(days=%s, seconds=%s, microseconds=%s), %s.timedelta(seconds=%s).total_seconds()" % (D, r.choice([0, 1, 2 ** 31, 10 ** 6, -3, 999999999]), r.choice([0, 86399, 10 ** 9, -5]), r.choice([0, 999999, 10 ** 7]), D, lit(r.choice([0, 1.5, 2 ** 31, 10 ** 12])))
    if op == "ordinal":
        return "%s.date.fromordinal(%d).isoformat(), %s.date(%d, %d, %d).toordinal()" % (D, r.randint(1, 3652059), D, r.randint(1, 9999), r.randint(1, 12), r.randint(1, 28))
    if op == "iso":
        return "%s.date.fromisoformat('%04d-%02d-%02d'), %s.datetime(%d, %d, %d, %d, %d, %d, %d)" % (D, r.randint(1, 9999), r.randint(1, 12), r.randint(1, 28), D, r.randint(1, 9999), r.randint(1, 12), r.randint(1, 28), r.randint(0, 23), r.randint(0, 59), r.randint(0, 59), r.randint(0, 999999))
    if op == "arith":
        return "%s.datetime(2000, 1, 1) + %s.timedelta(days=%d, seconds=%d), %s.date(2000, 1, 1) - %s.date(%d, 6, 1)" % (D, D, r.randint(-10 ** 5, 10 ** 5), r.randint(-10 ** 9, 10 ** 9), D, D, r.randint(1, 9999))
    if op == "weekday":
        return "%s.date(%d, %d, %d).weekday(), %s.date(%d, %d, %d).isocalendar()" % (D, r.randint(1, 9999), r.randint(1, 12), r.randint(1, 28), D, r.randint(1, 9999), r.randint(1, 12), r.randint(1, 28))
    if op == "strftime":
        return "%s.datetime(%d, %d, %d, %d, %d, %d).strftime('%%Y-%%m-%%d %%H:%%M:%%S %%j %%a %%b')" % (D, r.randint(1000, 9999), r.randint(1, 12), r.randint(1, 28), r.randint(0, 23), r.randint(0, 59), r.randint(0, 59))
    if op == "tdsec":
        return "%s.timedelta(hours=%d, minutes=%d) // %s.timedelta(minutes=7), %s.timedelta(days=%d) * %s" % (D, r.randint(-1000, 10 ** 6), r.randint(0, 10 ** 6), D, D, r.randint(-100, 10 ** 6), lit(r.choice([2, 0.5, 3, 10 ** 6])))
    if op == "replace":
        return "%s.datetime(2020, 2, 29).replace(year=%d, day=%d)" % (D, r.choice([2021, 2024, 2000, 9999]), r.choice([28, 29, 1]))
    return "str(%s.date(%d, %d, %d)), repr(%s.timedelta(%d))" % (D, r.randint(1, 9999), r.randint(1, 12), r.randint(1, 28), D, r.randint(-10 ** 6, 10 ** 6))


def c_stat(r):
    xs = [rint(r, 40) for _ in range(r.randint(1, 8))]
    fs = [round(rfloat(r) % 1000, 3) for _ in range(r.randint(1, 8))]
    S = "__import__('statistics')"
    op = r.choice(["mean", "median", "fmean", "mode", "pvariance", "stdev", "fsum", "sumsq", "median_low", "quantiles"])
    L = lit(xs if r.random() < 0.5 else fs)
    if op == "stdev" or op == "quantiles":
        L = lit(xs[:1] + [rint(r, 40) for _ in range(3)] if op == "stdev" else fs + fs + [1.5, 2.5])
    return "%s.%s(%s)" % (S, {"sumsq": "pvariance", "fsum": "fmean"}.get(op, op), L)


def c_bytes(r):
    b = bytes(r.getrandbits(8) for _ in range(r.randint(0, 12)))
    B = lit(b)
    op = r.choice(["slice", "hex", "int", "mul", "idx", "find", "split", "join", "count", "bytearray", "decode", "cmp", "fromhex", "xor", "repr", "reversed", "tobig"])
    if op == "slice":
        return "%s[%s:%s:%s]" % (B, lit(r.choice([None, 1, -2, 2 ** 40, -2 ** 40, 2 ** 70])), lit(r.choice([None, 3, -1, 2 ** 40, 2 ** 70])), lit(r.choice([None, 1, 2, -1, 2 ** 33])))
    if op == "hex":
        return "%s.hex(), %s.hex(':'), %s.hex(' ', -2)" % (B, B, B)
    if op == "int":
        return "int.from_bytes(%s, 'little'), int.from_bytes(%s, 'big', signed=True)" % (B, B)
    if op == "mul":
        return "%s * %d, %d * %s" % (B, r.randint(0, 3), r.randint(0, 3), B)
    if op == "idx":
        return "(%s + b'x')[%s]" % (B, lit(r.choice([0, -1, 2 ** 31, 2 ** 70, 3])))
    if op == "find":
        return "%s.find(b'a', %s, %s), %s.count(b'', %s)" % (B, lit(r.choice([0, 1, -3, 2 ** 40])), lit(r.choice([100, 2, -1, 2 ** 40])), B, lit(r.choice([0, 2 ** 40, -2 ** 40])))
    if op == "split":
        return "%s.split(b'\\x00', %d), %s.rsplit(maxsplit=%d)" % (B, r.choice([-1, 0, 1, 2]), B, r.choice([-1, 1, 3]))
    if op == "join":
        return "b'-'.join([%s, b'x', %s])" % (B, B)
    if op == "count":
        return "%s.count(b'\\x00'), %s.startswith(%s[:2]), %s.endswith(b'a')" % (B, B, B, B)
    if op == "bytearray":
        return "(lambda a: (a.extend(%s), a.append(%d), a.insert(%s, 7), a.pop(), a)[-1])(bytearray(b'q'))" % (B, r.randint(0, 255), lit(r.choice([0, 1, 2 ** 40, -2 ** 40])))
    if op == "decode":
        return "%s.decode('latin-1'), %s.decode('utf-8', 'replace'), %s.decode('ascii', 'ignore')" % (B, B, B)
    if op == "cmp":
        return "%s < %s + b'a', %s == %s, sorted([%s, b'a', b''])" % (B, B, B, B, B)
    if op == "fromhex":
        return "bytes.fromhex(%r), bytes.fromhex('0a 0B ff')" % b.hex()
    if op == "xor":
        return "bytes(c ^ 0x55 for c in %s), sum(%s), max(%s + b'\\x00')" % (B, B, B)
    if op == "repr":
        return "repr(%s), str(bytearray(%s)), len(%s)" % (B, B, B)
    if op == "reversed":
        return "bytes(reversed(%s)), list(%s)[::-1], bytes(sorted(%s))" % (B, B, B)
    return "int.from_bytes(%s * %d, 'big'), (%s * %d)[:4]" % (B, r.randint(1, 8), B, r.randint(1, 3))


CATS = [("intarith", c_intarith), ("intpow", c_intpow), ("bitops", c_bitops), ("intconv", c_intconv),
        ("float", c_float), ("float2", c_float2), ("special", c_special), ("str", c_str),
        ("range", c_range), ("seq", c_seq), ("struct", c_struct), ("json", c_json), ("re", c_re),
        ("misc", c_misc), ("lex", c_lex), ("numstr", c_numstr), ("hashmix", c_hashmix), ("iter", c_iter),
        ("dt", c_dt), ("stat", c_stat), ("bytes", c_bytes)]

HEADER = '''import sys
def case(i, f):
    try:
        r = eval(f)
        print(i, repr(r))
    except BaseException as e:
        print(i, '!' + type(e).__name__)
    sys.stdout.flush()
'''


def gen_program(n, seed, cats):
    exprs = []
    lines = [HEADER]
    for name, fn in CATS:
        if cats and name not in cats:
            continue
        r = random.Random("%s/%s" % (seed, name))
        for _ in range(n):
            e = fn(r)
            exprs.append((name, e))
            lines.append("case(%d, %r)" % (len(exprs) - 1, "(" + e + ")"))
    return exprs, lines


def run(cmd, path, timeout):
    try:
        p = subprocess.run(cmd + [path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
        return p.stdout.decode("utf-8", "replace").splitlines(), p.returncode, p.stderr.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return [], -9, "timeout"


def split_cases(exprs, text):
    d = {}
    for ln in text:
        i, _, rest = ln.partition(" ")
        if i.isdigit():
            d[int(i)] = rest
    return d


def build_modes(outdir):
    src = os.path.join(ROOT, "c", "py_all.c")
    lib = '-DPY_LIBDIR="%s"' % os.path.join(ROOT, "pylib")
    modes = [("normal", []), ("si32", ["-DPY_SI32", "-DPY_U64_EMUL"]),
             ("m32", ["-m32", "-msse2", "-mfpmath=sse"])]
    res = []
    for name, flags in modes:
        exe = os.path.join(outdir, "py_" + name)
        cmd = ["cc", "-std=c89", "-O2", "-DPY_POSIX", lib] + flags + ["-o", exe, src, "-lm"]
        if subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            res.append(exe + ":" + name)
        else:
            print("build of mode %s failed (skipped)" % name)
    return res


def main(argv):
    n, seed, cats, keep, show, timeout, build = 300, 1, None, False, 12, 120, False
    selfref = False
    pys = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "-n":
            n = int(argv[i + 1]); i += 1
        elif a == "-s":
            seed = argv[i + 1]; i += 1
        elif a == "-c":
            cats = argv[i + 1].split(","); i += 1
        elif a == "--keep":
            keep = True
        elif a == "--show":
            show = int(argv[i + 1]); i += 1
        elif a == "--build":
            build = True
        elif a == "--self":
            selfref = True
        else:
            pys.append(a)
        i += 1
    tmp = os.environ.get("TMPDIR", "/tmp")
    outdir = os.path.join(tmp, "si32_diff_%d" % os.getpid())
    os.makedirs(outdir)
    if build:
        pys += build_modes(outdir)
    if not pys:
        print(__doc__)
        return 2
    exprs, lines = gen_program(n, seed, cats)
    path = os.path.join(outdir, "cases.py")

    def run_all(cmd):
        """run every case; a crash or hang at case k is recorded and the rest is rerun without it"""
        skip, got, rcs, errs = set(), {}, 0, ""
        while True:
            with open(path, "w", encoding="utf-8") as f:
                f.write(lines[0])
                for k in range(len(exprs)):
                    if k not in skip and k not in got:
                        f.write(lines[k + 1] + "\n")
            out, rc, err = run(cmd, path, timeout)
            d = split_cases(exprs, out)
            got.update(d)
            if rc == 0:
                break
            rcs, errs = rc, err
            todo = [k for k in range(len(exprs)) if k not in skip and k not in got]
            if not todo:
                break
            skip.add(todo[0])
            got[todo[0]] = "!CRASH(%d)" % rc
        return got, rcs, errs

    if selfref and len(pys) > 1:                  # compare the other modes with the first one
        ref, rc, err = run_all([pys[0].partition(":")[0]])
        pys = pys[1:]
    else:
        ref, rc, err = run_all([sys.executable])
    bad_total = 0
    for spec in pys:
        exe, _, label = spec.partition(":")
        label = label or exe
        got, rc, err = run_all([exe])
        bad = []
        for k in range(len(exprs)):
            if got.get(k) != ref.get(k):
                bad.append(k)
        per = {}
        for k in bad:
            per[exprs[k][0]] = per.get(exprs[k][0], 0) + 1
        print("%s: %d cases, %d differ %s%s" % (label, len(exprs), len(bad), sorted(per.items()),
                                                 "" if rc == 0 else " (exit %d: %s)" % (rc, err.strip()[-200:])))
        shown = {}
        for k in bad:
            c = exprs[k][0]
            if shown.get(c, 0) >= show:
                continue
            shown[c] = shown.get(c, 0) + 1
            print("  [%s #%d] %s\n      cpython: %s\n      here:    %s" % (c, k, exprs[k][1][:300], str(ref.get(k))[:200], str(got.get(k))[:200]))
        bad_total += len(bad)
    if not keep:
        for fn in os.listdir(outdir):
            os.remove(os.path.join(outdir, fn))
        os.rmdir(outdir)
    else:
        print("kept", outdir)
    return 1 if bad_total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
