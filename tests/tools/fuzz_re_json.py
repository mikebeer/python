#!/usr/bin/env python3
"""Differential fuzzer: the native `re` / `json` modules of the C interpreter against CPython.

    python3 tests/tools/fuzz_re_json.py [--py ./c/py] [--n 3000] [--seed 1] [--only re|json]
                                        [--keep] [--show 25]

Cases are generated from a seed, written to a data file and evaluated by one runner script that is
executed by CPython and by the C interpreter; the outputs are compared line by line.  Not part of
the test suite (tests/run_tests.sh only looks at tests/*.py).
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# ---------------------------------------------------------------------------------------------
# the runner executed by both interpreters
# ---------------------------------------------------------------------------------------------
RUNNER_RE = r'''
import re, sys

def msum(m):
    if m is None:
        return None
    return (m.span(), m.groups(), m.groupdict(), m.lastindex, m.lastgroup, m.pos, m.endpos, m.regs)

def norm(r):
    if isinstance(r, tuple) and len(r) == 3 and r[0] == "EXC":
        return r
    return r

def cb(m):
    g = m.group(0)
    return "<" + g.upper() + str(m.start()) + ">"

def run(op, pat, flags, s, ex):
    if op == "compile":
        p = re.compile(pat, flags)
        return ("ok", p.groups, sorted(p.groupindex.items()), p.flags)
    p = re.compile(pat, flags)
    if op == "search":
        return msum(p.search(s, ex[0], ex[1]))
    if op == "match":
        return msum(p.match(s, ex[0], ex[1]))
    if op == "fullmatch":
        return msum(p.fullmatch(s, ex[0], ex[1]))
    if op == "findall":
        return p.findall(s, ex[0], ex[1])
    if op == "finditer":
        return [(m.span(), m.groups()) for m in p.finditer(s, ex[0], ex[1])]
    if op == "sub":
        return p.sub(ex[0], s, ex[1])
    if op == "subn":
        return p.subn(ex[0], s, ex[1])
    if op == "subcb":
        return p.subn(cb, s, ex[0])
    if op == "split":
        return p.split(s, ex[0])
    if op == "expand":
        m = p.search(s)
        if m is None:
            return None
        return m.expand(ex[0])
    if op == "mod_search":
        return msum(re.search(pat, s, flags))
    if op == "mod_sub":
        return re.sub(pat, ex[0], s, flags=flags)
    if op == "mod_split":
        return re.split(pat, s, flags=flags)
    if op == "mod_findall":
        return re.findall(pat, s, flags)
    if op == "escape":
        return re.escape(s)
    raise ValueError("bad op")

f = open(sys.argv[1])
text = f.read()
f.close()
for line in text.split("\n"):
    if not line:
        continue
    c = eval(line)
    cid = c[0]
    try:
        r = run(c[1], c[2], c[3], c[4], c[5])
    except Exception as e:
        n = type(e).__name__
        if n == "PatternError":
            n = "error"
        r = ("EXC", n, str(e))
    print(cid, repr(r))
'''

RUNNER_JSON = r'''
import json, sys

def run(op, a, kw):
    if op == "dumps":
        return json.dumps(a, **kw)
    if op == "loads":
        return json.loads(a, **kw)
    if op == "loads_hooks":
        return json.loads(a, object_hook=lambda d: ("O", sorted(d.items())),
                          parse_float=lambda s: ("F", s), parse_int=lambda s: ("I", s),
                          parse_constant=lambda s: ("C", s))
    if op == "loads_pairs":
        return json.loads(a, object_pairs_hook=lambda p: ("P", p))
    if op == "roundtrip":
        return json.loads(json.dumps(a, **kw))
    if op == "enc_default":
        return json.dumps(a, default=lambda o: ("D", repr(o)), **kw)
    if op == "raw":
        return json.JSONDecoder().raw_decode(a, kw["idx"])
    raise ValueError("bad op")

f = open(sys.argv[1])
text = f.read()
f.close()
for line in text.split("\n"):
    if not line:
        continue
    c = eval(line)
    try:
        r = run(c[1], c[2], c[3])
        if isinstance(r, float) and r != r:
            r = "nan"
    except Exception as e:
        n = type(e).__name__
        r = ("EXC", n, str(e))
    print(c[0], repr(r))
'''

# ---------------------------------------------------------------------------------------------
# regex generator
# ---------------------------------------------------------------------------------------------
LITS = ["a", "b", "c", "x", " ", "1", "_", "-", "A", "B", "\\.", "\\-", "\\n", "\\\\", "\\/", "\\ ",
        "\\x41", "\\x62", "\\u0061", "\\0", "\\101", "é"]
SETS = ["[abc]", "[^abc]", "[a-c]", "[^a-c]", "[a-cx]", "[\\d]", "[\\w-]", "[\\s]", "[^\\s]",
        "[a\\-c]", "[-ab]", "[ab-]", "[]a]", "[^]a]", "[\\]a]", "[.]", "[a.b]", "[\\D]",
        "[\\W]", "[A-Za-z]", "[0-9a-f]", "[\\x61-\\x63]", "[ -/]", "[\\n]", "[é]", "[a-cé]"]
CLASSES = ["\\d", "\\D", "\\w", "\\W", "\\s", "\\S", "."]
ANCH = ["^", "$", "\\b", "\\B", "\\A", "\\Z"]
QUANT = ["*", "+", "?", "{2}", "{1,2}", "{0,2}", "{2,}", "{,2}", "{0}", "{1}", "{3}"]


def gen_atom(r, depth, st):
    k = r.random()
    if k < 0.28:
        return r.choice(LITS[:12]) if r.random() < 0.9 else r.choice(LITS)
    if k < 0.40:
        return r.choice(SETS)
    if k < 0.52:
        return r.choice(CLASSES)
    if k < 0.58:
        return r.choice(ANCH)
    if depth <= 0:
        return r.choice(["a", "b", "."])
    if k < 0.74:
        st["ng"] += 1
        return "(" + gen_alt(r, depth - 1, st) + ")"
    if k < 0.80:
        return "(?:" + gen_alt(r, depth - 1, st) + ")"
    if k < 0.84:
        st["ng"] += 1
        st["names"] += 1
        nm = "n%d" % st["names"]
        return "(?P<%s>" % nm + gen_alt(r, depth - 1, st) + ")"
    if k < 0.89:
        look = r.choice(["(?=", "(?!", "(?<=", "(?<!"])
        body = gen_alt(r, depth - 1, st)
        if look.startswith("(?<"):
            body = r.choice(["a", "b", "ab", "[ab]", "\\d", "x|y", "a|b", ".", "\\b", "^", "(?:a|b)", "c{2}"])
        return look + body + ")"
    if k < 0.92:
        return "(?>" + gen_alt(r, depth - 1, st) + ")"
    if k < 0.96 and st["ng"] > 0:
        return "\\%d" % r.randint(1, st["ng"])
    if k < 0.97 and st["names"] > 0:
        return "(?P=n%d)" % r.randint(1, st["names"])
    if k < 0.98 and st["ng"] > 0:
        g = r.randint(1, st["ng"])
        return "(?(%d)%s|%s)" % (g, r.choice(["a", "b", "\\d", ""]), r.choice(["b", "c", "", "x"]))
    k2 = r.choice(["(?i:", "(?s:", "(?m:", "(?-i:", "(?x: ", "(?a:", "(?#comment)", "(?i-s:"])
    if k2 == "(?#comment)":
        return k2 + "a"
    return k2 + gen_alt(r, depth - 1, st) + ")"


def gen_piece(r, depth, st):
    a = gen_atom(r, depth, st)
    if r.random() < 0.45 and not (a in ANCH):
        q = r.choice(QUANT)
        a += q
        k = r.random()
        if k < 0.25:
            a += "?"
        elif k < 0.32:
            a += "+"
    return a


def gen_cat(r, depth, st):
    n = r.choice([1, 1, 2, 2, 3, 3, 4])
    return "".join(gen_piece(r, depth, st) for _ in range(n))


def gen_alt(r, depth, st):
    n = 1 if r.random() < 0.75 else r.randint(2, 3)
    return "|".join(gen_cat(r, depth, st) for _ in range(n))


def gen_pattern(r):
    st = {"ng": 0, "names": 0}
    p = gen_alt(r, r.choice([1, 2, 2, 3]), st)
    k = r.random()
    if k < 0.06:
        p = r.choice(["(?i)", "(?s)", "(?m)", "(?x)", "(?a)", "(?im)", "(?si)"]) + p
    if k > 0.97:
        p = " " + p.replace("|", " | ") + " # c\n"
    return p, st


MUT_CHARS = list("()[]{}*+?|\\^$.-,<>=!:#Pgimsxau0123456789ab&~ \n'")


def mutate(r, p):
    for _ in range(r.randint(1, 3)):
        k = r.random()
        if not p:
            p = r.choice(MUT_CHARS)
            continue
        i = r.randrange(len(p) + 1)
        if k < 0.4:
            p = p[:i] + r.choice(MUT_CHARS) + p[i:]
        elif k < 0.7 and i < len(p):
            p = p[:i] + p[i + 1:]
        elif k < 0.85 and i < len(p):
            p = p[:i] + p[i] + p[i:]
        else:
            frag = r.choice(["(?", "\\", "[", "(?P<", "(?P=", "{", "(?<", "(?(", "\\g<", "(?P>", "[a-", "\\x", "\\u",
                             "\\8", "*", "+*", "{2,1}", "(?i", "(?-", "(?<=a*)", "(?<=\\1)", "\\N{", "(?#"])
            p = p[:i] + frag + p[i:]
    return p


ALPHA = "abcxAB1 _\n-.,"


def gen_string(r, p=None):
    n = r.choice([0, 1, 2, 3, 4, 5, 6, 8, 10, 12])
    s = "".join(r.choice(ALPHA) for _ in range(n))
    if r.random() < 0.3 and p:
        # derive from the pattern's literals
        lits = [c for c in p if c.isalnum()]
        if lits:
            s = "".join(r.choice(lits + list("ab ")) for _ in range(r.randint(0, 10)))
    if r.random() < 0.05:
        s = s + r.choice(["é", "ü1", "日本", "x😀y"])
    return s


def gen_template(r, st):
    n = r.randint(0, 4)
    out = []
    for _ in range(n):
        k = r.random()
        if k < 0.3:
            out.append(r.choice(["x", "-", "Z", " ", "ab", ""]))
        elif k < 0.55 and st["ng"] > 0:
            out.append(r.choice(["\\%d" % r.randint(1, st["ng"]), "\\g<%d>" % r.randint(0, st["ng"])]))
        elif k < 0.65 and st["names"] > 0:
            out.append("\\g<n%d>" % r.randint(1, st["names"]))
        elif k < 0.75:
            out.append(r.choice(["\\0", "\\n", "\\t", "\\\\", "\\g<0>", "\\g<1>", "\\1", "\\12", "\\08", "\\101", "\\x"]))
        elif k < 0.85:
            out.append(r.choice(["\\q", "\\g<", "\\g<x>", "\\g", "\\g<>", "\\9", "\\", "\\g<1", "\\777", "\\g<-1>"]))
        else:
            out.append(r.choice(["é", "[", "$", "."]))
    return "".join(out)


FLAGSET = [0, 0, 0, 0, 2, 8, 16, 64, 256, 2 | 8, 2 | 16, 8 | 16, 2 | 8 | 16, 64 | 2, 256 | 2]


def gen_re_cases(r, n):
    cases = []
    for cid in range(n):
        if r.random() < 0.12:
            p = mutate(r, gen_pattern(r)[0])
            st = {"ng": 3, "names": 1}
        else:
            p, st = gen_pattern(r)
        flags = r.choice(FLAGSET)
        s = gen_string(r, p)
        k = r.random()
        ln = len(s)
        pos = 0
        endpos = 1 << 60
        if r.random() < 0.15:
            pos = r.randint(0, ln + 1)
            if r.random() < 0.5:
                endpos = r.randint(pos, ln + 1)
        if k < 0.07:
            op, ex = "compile", ()
        elif k < 0.22:
            op, ex = "search", (pos, endpos)
        elif k < 0.32:
            op, ex = "match", (pos, endpos)
        elif k < 0.40:
            op, ex = "fullmatch", (pos, endpos)
        elif k < 0.50:
            op, ex = "findall", (pos, endpos)
        elif k < 0.58:
            op, ex = "finditer", (pos, endpos)
        elif k < 0.67:
            op, ex = "sub", (gen_template(r, st), r.choice([0, 0, 0, 1, 2, 3]))
        elif k < 0.72:
            op, ex = "subn", (gen_template(r, st), r.choice([0, 0, 1, 2]))
        elif k < 0.78:
            op, ex = "subcb", (r.choice([0, 0, 1, 2]),)
        elif k < 0.85:
            op, ex = "split", (r.choice([0, 0, 1, 2, 3]),)
        elif k < 0.90:
            op, ex = "expand", (gen_template(r, st),)
        elif k < 0.93:
            op, ex = "mod_search", ()
        elif k < 0.96:
            op, ex = "mod_sub", (gen_template(r, st),)
        elif k < 0.98:
            op, ex = "mod_split", ()
        elif k < 0.99:
            op, ex = "mod_findall", ()
        else:
            op, ex = "escape", ()
            s = "".join(r.choice("ab.*+?()[]{}|^$\\-#&~ \t\n_1é") for _ in range(r.randint(0, 10)))
        if op in ("search", "match", "fullmatch", "findall", "finditer"):
            ex = (pos, 1000000 if endpos == 1 << 60 else endpos)
        cases.append((cid, op, p, flags, s, ex))
    return cases


# ---------------------------------------------------------------------------------------------
# json generator
# ---------------------------------------------------------------------------------------------
STRS = ["", "a", "abc", "a b", "a\"b", "a\\b", "a/b", "tab\there", "nl\nx", "\r\x08\x0c", "\x00\x1f\x7f",
        "é", "ü€", "日本語", "😀", "a😀b", "  ", "key", "k1", "k2", "0", "-1", "true", "null",
        "퟿", "", "￿"]


def gen_value(r, depth, nan=True):
    k = r.random()
    if depth <= 0 or k < 0.45:
        k2 = r.random()
        if k2 < 0.22:
            return r.choice(STRS)
        if k2 < 0.40:
            return r.randint(-5, 100)
        if k2 < 0.50:
            return r.choice([10 ** 20, -10 ** 25, 2 ** 70, 123456789012345678, 0, -0, 999999999999999999])
        if k2 < 0.70:
            return r.choice([0.0, -0.0, 1.5, -2.25, 1e22, 1e-7, 123456.789, 0.1, 1 / 3, 1e16, 1e15, 5e-324,
                             1.7976931348623157e308, 100.0, 1e300, 12345678901234567890.0, 0.30000000000000004,
                             2.5e-5, 1e-4])
        if k2 < 0.78:
            return r.choice([True, False])
        if k2 < 0.86:
            return None
        if nan:
            return r.choice([float("nan"), float("inf"), float("-inf")])
        return 1
    if k < 0.72:
        return [gen_value(r, depth - 1, nan) for _ in range(r.randint(0, 4))]
    if k < 0.76:
        return tuple(gen_value(r, depth - 1, nan) for _ in range(r.randint(0, 3)))
    d = {}
    for _ in range(r.randint(0, 4)):
        kk = r.random()
        if kk < 0.8:
            key = r.choice(STRS)
        elif kk < 0.88:
            key = r.randint(-3, 20)
        elif kk < 0.92:
            key = r.choice([1.5, True, False, None, 1e22])
        elif kk < 0.96:
            key = (1, 2)
        else:
            key = r.randint(0, 5)
        d[key] = gen_value(r, depth - 1, nan)
    return d


def gen_opts(r):
    kw = {}
    if r.random() < 0.4:
        kw["indent"] = r.choice([0, 1, 2, 4, "\t", "--", None])
    if r.random() < 0.3:
        kw["sort_keys"] = r.choice([True, False])
    if r.random() < 0.3:
        kw["separators"] = r.choice([(",", ":"), (", ", ": "), (";", "="), (",", ": ")])
    if r.random() < 0.3:
        kw["ensure_ascii"] = r.choice([True, False])
    if r.random() < 0.15:
        kw["allow_nan"] = False
    if r.random() < 0.2:
        kw["skipkeys"] = True
    return kw


JSON_PIECES = ['{', '}', '[', ']', ',', ':', '"', '"a"', '1', '-', '.', 'e', 'E', '+', '0', 'true', 'false', 'null',
               'nul', 'tru', 'NaN', 'Infinity', '-Infinity', ' ', '\n', '\t', '\\', '\\n', '\\u00e9', '\\ud83d\\ude00',
               '\\u12', '\\x', '\\ud83d', '\\"', '1.5', '1e5', '1.', '.5', '01', '-0', '"\x01"', "'", 'é', '/', '\\/']


def gen_json_text(r):
    k = r.random()
    if k < 0.45:
        v = gen_value(r, 3, True)
        try:
            import json
            s = json.dumps(v, **{x: y for x, y in gen_opts(r).items() if x in ("indent", "separators", "ensure_ascii")})
        except Exception:
            s = "[]"
        if r.random() < 0.6:
            return s
        # mutate valid text
        for _ in range(r.randint(1, 3)):
            if not s:
                break
            i = r.randrange(len(s) + 1)
            kk = r.random()
            if kk < 0.4:
                s = s[:i] + r.choice(JSON_PIECES) + s[i:]
            elif kk < 0.8:
                s = s[:i] + s[i + 1:]
            else:
                s = s[:i]
        return s
    return "".join(r.choice(JSON_PIECES) for _ in range(r.randint(0, 8)))


def gen_json_cases(r, n):
    cases = []
    for cid in range(n):
        k = r.random()
        if k < 0.30:
            cases.append((cid, "dumps", gen_value(r, 3), gen_opts(r)))
        elif k < 0.38:
            cases.append((cid, "roundtrip", gen_value(r, 3, False), {x: y for x, y in gen_opts(r).items() if x in ("indent", "separators", "ensure_ascii")}))
        elif k < 0.43:
            cases.append((cid, "enc_default", [gen_value(r, 2), (7,), object], {}))
        elif k < 0.70:
            cases.append((cid, "loads", gen_json_text(r), {}))
        elif k < 0.78:
            cases.append((cid, "loads_hooks", gen_json_text(r), {}))
        elif k < 0.84:
            cases.append((cid, "loads_pairs", gen_json_text(r), {}))
        elif k < 0.90:
            t = gen_json_text(r)
            cases.append((cid, "loads", t, {"strict": False}))
        else:
            t = gen_json_text(r)
            cases.append((cid, "raw", t, {"idx": r.randint(0, 3)}))
    return cases


# ---------------------------------------------------------------------------------------------
def run_one(cmd, runner, cases_file, timeout):
    try:
        p = subprocess.run(cmd + [runner, cases_file], capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "TIMEOUT"
    return p.stdout.decode("utf-8", "replace").split("\n"), p.stderr.decode("utf-8", "replace")


_UESC = re.compile(r"\\u([0-9a-f]{4})")


def sanitize(line):
    # CPython's repr escapes unprintable code points (\ue000), the interpreter's repr prints them raw
    return _UESC.sub(lambda m: chr(int(m.group(1), 16)), line)


def compare(name, cases, runner_src, py, show, keep, tmp, timeout, batch=400):
    rpath = os.path.join(tmp, name + "_runner.py")
    with open(rpath, "w") as f:
        f.write(runner_src)
    ndiff = 0
    nerr = 0
    total = 0
    shown = 0
    diffs = []
    for b in range(0, len(cases), batch):
        chunk = cases[b:b + batch]
        cpath = os.path.join(tmp, "%s_cases_%d.txt" % (name, b))
        with open(cpath, "w") as f:
            for c in chunk:
                f.write(repr(c) + "\n")
        exp, e1 = run_one([sys.executable, "-I"], rpath, cpath, timeout)
        got, e2 = run_one([py], rpath, cpath, timeout)
        if exp is None or got is None:
            # bisect: run cases individually
            for c in chunk:
                one = os.path.join(tmp, "one.txt")
                with open(one, "w") as f:
                    f.write(repr(c) + "\n")
                exp1, _ = run_one([sys.executable, "-I"], rpath, one, timeout)
                got1, _ = run_one([py], rpath, one, timeout)
                total += 1
                if got1 is None:
                    diffs.append((c, "<timeout>", exp1[0] if exp1 else "<timeout>"))
                    ndiff += 1
                elif exp1 is None:
                    pass
                elif exp1[0] != got1[0]:
                    diffs.append((c, got1[0], exp1[0]))
                    ndiff += 1
            continue
        for i, c in enumerate(chunk):
            total += 1
            a = sanitize(exp[i]) if i < len(exp) else "<missing>"
            g = sanitize(got[i]) if i < len(got) else "<missing>"
            if a != g:
                ndiff += 1
                diffs.append((c, g, a))
    return total, ndiff, diffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--py", default=os.path.join(ROOT, "c", "py"))
    ap.add_argument("--n", type=int, default=3000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--only", default="")
    ap.add_argument("--show", type=int, default=25)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--unicode", action="store_true",
                    help="keep non-ASCII cases (offsets/positions are bytes in the C interpreter, so they differ by design)")
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()
    r = random.Random(args.seed)
    tmp = tempfile.mkdtemp(prefix="fuzz_rj_")
    env_ok = True
    results = []
    if args.only in ("", "re"):
        cases = gen_re_cases(r, args.n)
        if not args.unicode:
            cases = [c for c in cases if all(ord(ch) < 128 for ch in repr(c))]
        total, nd, diffs = compare("re", cases, RUNNER_RE, args.py, args.show, args.keep, tmp, args.timeout)
        results.append(("re", total, nd, diffs))
    if args.only in ("", "json"):
        cases = gen_json_cases(r, args.n)
        total, nd, diffs = compare("json", cases, RUNNER_JSON, args.py, args.show, args.keep, tmp, args.timeout)
        results.append(("json", total, nd, diffs))
    bad = 0
    for name, total, nd, diffs in results:
        print("%s: %d cases, %d differences" % (name, total, nd))
        for c, g, e in diffs[:args.show]:
            print("  case:", repr(c))
            print("    ours:    ", g[:300])
            print("    cpython: ", e[:300])
        bad += nd
    print("work dir:", tmp)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
