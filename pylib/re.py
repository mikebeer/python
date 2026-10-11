"""re: a backtracking regular expression engine in pure Python.

The pattern is parsed into a small AST and compiled to a list of instructions that a non-recursive
backtracking VM (explicit stack) executes; runs of single-character repetitions are one instruction,
so the stack does not grow per character.
"""

I = IGNORECASE = 2
L = LOCALE = 4
M = MULTILINE = 8
S = DOTALL = 16
U = UNICODE = 32
X = VERBOSE = 64
DEBUG = 128
A = ASCII = 256
NOFLAG = 0

_ALL = "".join([chr(i) for i in range(1, 128)])
_DIG = "0123456789"
_WORD = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
_SPACE = " \t\n\r\x0b\x0c"
_SPECIAL = "()[]{}?*+-|^$\\.&~# \t\n\r\x0b\x0c"


class error(Exception):
    def __init__(self, msg, pattern=None, pos=None):
        if pattern is not None and pos is not None:
            Exception.__init__(self, "%s at position %d" % (msg, pos))
        else:
            Exception.__init__(self, msg)
        self.msg = msg
        self.pattern = pattern
        self.pos = pos


PatternError = error


def _comp(chars):
    return "".join([c for c in _ALL if c not in chars])


_ESCSETS = {"d": (_DIG, False, False, False, ""), "D": (_DIG, True, False, False, ""),
            "w": (_WORD, False, True, True, ""), "W": (_WORD, True, True, True, ""),
            "s": (_SPACE, False, False, False, ""), "S": (_SPACE, True, False, False, "")}

# ---------------------------------------------------------------------------------------------
# parser: pattern text -> AST tuples
#   ('lit', text, icase) ('any', dotall) ('set', (chars, neg, slow, hi, extra)) ('cat', [nodes])
#   ('alt', [nodes]) ('group', idx|0, node) ('rep', node, min, max, greedy) ('assert', kind)
#   ('backref', idx, icase) ('look', ahead, neg, node) ('empty',)
# ---------------------------------------------------------------------------------------------

class _Parser:
    def __init__(self, pat, flags):
        self.p = pat
        self.i = 0
        self.n = len(pat)
        self.flags = flags
        self.ng = 0
        self.names = {}
        self.open = []

    def err(self, msg, pos=None):
        raise error(msg, self.p, self.i if pos is None else pos)

    def parse(self):
        node = self.alt()
        if self.i < self.n:
            if self.p[self.i] == ")":
                self.err("unbalanced parenthesis")
            self.err("unexpected end of pattern")
        return node

    def alt(self):
        branches = [self.cat()]
        while self.i < self.n and self.p[self.i] == "|":
            self.i += 1
            branches.append(self.cat())
        if len(branches) == 1:
            return branches[0]
        return ("alt", branches)

    def skipws(self):
        p = self.p
        while self.i < self.n:
            c = p[self.i]
            if c in " \t\n\r\x0b\x0c":
                self.i += 1
            elif c == "#":
                while self.i < self.n and p[self.i] != "\n":
                    self.i += 1
            else:
                break

    def cat(self):
        items = []
        p = self.p
        while True:
            if self.flags & 64:
                self.skipws()
            if self.i >= self.n:
                break
            c = p[self.i]
            if c == "|" or c == ")":
                break
            atom = self.atom()
            if atom is None:
                continue
            if self.flags & 64:
                self.skipws()
            atom = self.quant(atom)
            if atom[0] == "lit" and items and items[-1][0] == "lit" and items[-1][2] == atom[2]:
                items[-1] = ("lit", items[-1][1] + atom[1], atom[2])
            else:
                items.append(atom)
        if not items:
            return ("empty",)
        if len(items) == 1:
            return items[0]
        return ("cat", items)

    def quant(self, atom):
        p = self.p
        while self.i < self.n:
            c = p[self.i]
            mn = -1
            mx = -1
            start = self.i
            if c == "*":
                mn = 0
                self.i += 1
            elif c == "+":
                mn = 1
                self.i += 1
            elif c == "?":
                mn = 0
                mx = 1
                self.i += 1
            elif c == "{":
                j = p.find("}", self.i)
                if j < 0 or j == self.i + 1:
                    return atom
                body = p[self.i + 1:j]
                if "," in body:
                    lo, hi = body.split(",", 1)
                else:
                    lo = body
                    hi = body
                if not ((lo == "" or lo.isdigit()) and (hi == "" or hi.isdigit())) or \
                        ("," not in body and lo == ""):
                    return atom
                mn = int(lo) if lo != "" else 0
                mx = int(hi) if hi != "" else -1
                if mx != -1 and mx < mn:
                    self.err("min repeat greater than max repeat", start)
                self.i = j + 1
            else:
                return atom
            if c == "*" or c == "+":
                mx = -1
            greedy = True
            if self.i < self.n and p[self.i] == "?":
                greedy = False
                self.i += 1
            elif self.i < self.n and p[self.i] == "+":
                self.i += 1
            atom = ("rep", atom, mn, mx, greedy)
            if self.flags & 64:
                self.skipws()
            if self.i < self.n and p[self.i] in "*+?":
                self.err("multiple repeat")
        return atom

    def lit(self, ch):
        ic = (self.flags & 2) != 0
        if ic:
            ch = ch.lower()
        return ("lit", ch, ic)

    def atom(self):
        p = self.p
        c = p[self.i]
        self.i += 1
        if c == "(":
            return self.group()
        if c == "[":
            return self.charclass()
        if c == ".":
            return ("any", (self.flags & 16) != 0)
        if c == "^":
            return ("assert", 1 if self.flags & 8 else 0)
        if c == "$":
            return ("assert", 3 if self.flags & 8 else 2)
        if c == "\\":
            return self.escape()
        if c == "*" or c == "+" or c == "?":
            self.err("nothing to repeat", self.i - 1)
        return self.lit(c)

    def num_or_name(self, close):
        j = self.p.find(close, self.i)
        if j < 0:
            self.err("missing %s, unterminated name" % close)
        name = self.p[self.i:j]
        self.i = j + 1
        return name

    def group(self):
        p = self.p
        if self.i < self.n and p[self.i] == "?":
            self.i += 1
            if self.i >= self.n:
                self.err("unexpected end of pattern")
            c = p[self.i]
            self.i += 1
            if c == ":":
                node = self.alt_in()
                return ("group", -1, node)
            if c == "P":
                c2 = p[self.i]
                self.i += 1
                if c2 == "<":
                    name = self.num_or_name(">")
                    if not name.isidentifier():
                        self.err("bad character in group name %r" % name, self.i - len(name) - 1)
                    if name in self.names:
                        self.err("redefinition of group name %r as group %d; was group %d" % (
                            name, self.ng + 1, self.names[name]), self.i - len(name) - 1)
                    self.ng += 1
                    idx = self.ng
                    self.names[name] = idx
                    self.open.append(idx)
                    node = self.alt_in()
                    self.open.pop()
                    return ("group", idx, node)
                if c2 == "=":
                    name = self.num_or_name(")")
                    if name not in self.names:
                        self.err("unknown group name %r" % name)
                    if self.names[name] in self.open:
                        self.err("cannot refer to an open group")
                    return ("backref", self.names[name], (self.flags & 2) != 0)
                self.err("unknown extension ?P" + c2)
            if c == "=" or c == "!":
                node = self.alt_in()
                return ("look", True, c == "!", node)
            if c == "<":
                c2 = p[self.i]
                self.i += 1
                if c2 == "=" or c2 == "!":
                    node = self.alt_in()
                    mn, mx = _width(node)
                    if mn != mx:
                        self.err("look-behind requires fixed-width pattern")
                    return ("look", False, c2 == "!", node)
                self.err("unknown extension ?<" + c2)
            if c == "#":
                j = p.find(")", self.i)
                if j < 0:
                    self.err("missing ), unterminated comment")
                self.i = j + 1
                return None
            if c == ">":
                node = self.alt_in()
                return ("atomic", node)
            # inline flags (?imsx-imsx) or (?imsx-imsx:...)
            self.i -= 1
            on = 0
            off = 0
            neg = False
            while self.i < self.n:
                c = p[self.i]
                if c == "-":
                    neg = True
                elif c in "imsxaLu":
                    f = {"i": 2, "m": 8, "s": 16, "x": 64, "a": 256, "L": 4, "u": 32}[c]
                    if neg:
                        off |= f
                    else:
                        on |= f
                elif c == ")" or c == ":":
                    break
                else:
                    self.err("unknown extension ?" + c)
                self.i += 1
            if self.i >= self.n:
                self.err("missing -, : or )")
            if p[self.i] == ")":
                self.i += 1
                self.flags = (self.flags | on) & ~off
                return None
            self.i += 1
            saved = self.flags
            self.flags = (self.flags | on) & ~off
            node = self.alt_in()
            self.flags = saved
            return ("group", -1, node)
        self.ng += 1
        idx = self.ng
        self.open.append(idx)
        node = self.alt_in()
        self.open.pop()
        return ("group", idx, node)

    def alt_in(self):
        saved = self.flags
        node = self.alt()
        self.flags = saved
        if self.i >= self.n or self.p[self.i] != ")":
            self.err("missing ), unterminated subpattern")
        self.i += 1
        return node

    def class_escape(self, c):
        # returns (chars, hi) for \d \w \s and complements, or None
        if c == "d":
            return _DIG, False
        if c == "D":
            return _comp(_DIG), True
        if c == "w":
            return _WORD, True
        if c == "W":
            return _comp(_WORD), False
        if c == "s":
            return _SPACE, False
        if c == "S":
            return _comp(_SPACE), True
        return None

    def simple_escape(self, c):
        # escapes that denote one character; returns the char or None
        if c == "n":
            return "\n"
        if c == "t":
            return "\t"
        if c == "r":
            return "\r"
        if c == "f":
            return "\x0c"
        if c == "v":
            return "\x0b"
        if c == "a":
            return "\x07"
        if c == "b":
            return "\x08"
        if c == "x" or c == "u" or c == "U":
            ln = 2 if c == "x" else 4 if c == "u" else 8
            h = self.p[self.i:self.i + ln]
            if len(h) != ln:
                self.err("incomplete escape \\" + c + h, self.i - 2)
            try:
                v = int(h, 16)
            except ValueError:
                self.err("incomplete escape \\" + c + h, self.i - 2)
            self.i += ln
            return chr(v)
        if c == "0":
            k = 0
            v = 0
            while k < 2 and self.i < self.n and self.p[self.i] in "01234567":
                v = v * 8 + int(self.p[self.i])
                self.i += 1
                k += 1
            return chr(v)
        return None

    def escape(self):
        if self.i >= self.n:
            self.err("bad escape (end of pattern)", self.i - 1)
        c = self.p[self.i]
        self.i += 1
        if c in "dDwWsS":
            return ("set", _ESCSETS[c])
        if c == "b":
            return ("assert", 5)
        if c == "B":
            return ("assert", 6)
        if c == "A":
            return ("assert", 0)
        if c == "Z":
            return ("assert", 4)
        if c in "123456789":
            j = self.i
            if j < self.n and self.p[j].isdigit():
                j += 1
            num = int(self.p[self.i - 1:j])
            if num > self.ng:
                self.err("invalid group reference %d" % num, self.i - 1)
            if num in self.open:
                self.err("cannot refer to an open group", self.i - 1)
            self.i = j
            return ("backref", num, (self.flags & 2) != 0)
        sc = self.simple_escape(c)
        if sc is not None:
            return self.lit(sc)
        if c.isalpha() and c.isascii():
            self.err("bad escape \\" + c, self.i - 2)
        return self.lit(c)

    def charclass(self):
        p = self.p
        start = self.i - 1
        neg = False
        if self.i < self.n and p[self.i] == "^":
            neg = True
            self.i += 1
        chars = []
        extra = []
        hi = False
        first = True
        ic = (self.flags & 2) != 0
        while True:
            if self.i >= self.n:
                self.err("unterminated character set", start)
            c = p[self.i]
            self.i += 1
            if c == "]" and not first:
                break
            first = False
            lo = None
            if c == "\\":
                if self.i >= self.n:
                    self.err("bad escape (end of pattern)", self.i - 1)
                e = p[self.i]
                self.i += 1
                ce = self.class_escape(e)
                if ce is not None:
                    chars.append(ce[0])
                    if ce[1]:
                        hi = True
                    continue
                sc = self.simple_escape(e)
                if sc is not None:
                    lo = sc
                elif e.isalpha() and e.isascii():
                    self.err("bad escape \\" + e, self.i - 2)
                else:
                    lo = e
            else:
                lo = c
            # range?
            if self.i + 1 < self.n and p[self.i] == "-" and p[self.i + 1] != "]":
                self.i += 1
                c2 = p[self.i]
                self.i += 1
                if c2 == "\\":
                    e = p[self.i]
                    self.i += 1
                    if self.class_escape(e) is not None:
                        self.err("bad character range %s-\\%s" % (lo, e), self.i - 4)
                    sc = self.simple_escape(e)
                    hi_c = sc if sc is not None else e
                else:
                    hi_c = c2
                a = ord(lo)
                b = ord(hi_c)
                if b < a:
                    self.err("bad character range %s-%s" % (lo, hi_c), self.i - 3)
                if b < 128:
                    chars.append(_ALL[a - 1:b] if a > 0 else _ALL[0:b])
                else:
                    if a < 128:
                        chars.append(_ALL[a - 1:] if a > 0 else _ALL)
                    # non-ASCII part of a range: approximate with "any non-ASCII"
                    hi = True
            else:
                if ord(lo) < 128:
                    chars.append(lo)
                else:
                    extra.append(lo)
        cs = "".join(chars)
        if ic:
            cs = cs + cs.swapcase()
        return ("set", (cs, neg, hi or len(extra) > 0, hi, "".join(extra)))


def _width(node):
    t = node[0]
    if t == "lit":
        return len(node[1]), len(node[1])
    if t == "any" or t == "set":
        return 1, 1
    if t == "cat":
        a = 0
        b = 0
        for x in node[1]:
            lo, hi = _width(x)
            a += lo
            if b >= 0:
                b = b + hi if hi >= 0 else -1
        return a, b
    if t == "alt":
        a = -1
        b = 0
        for x in node[1]:
            lo, hi = _width(x)
            if a < 0 or lo < a:
                a = lo
            if b >= 0:
                b = -1 if hi < 0 else (hi if hi > b else b)
        return a, b
    if t == "group" or t == "atomic":
        return _width(node[2] if t == "group" else node[1])
    if t == "rep":
        lo, hi = _width(node[1])
        mn = node[2]
        mx = node[3]
        a = lo * mn
        if mx < 0:
            b = -1 if hi != 0 else 0
        else:
            b = -1 if hi < 0 else hi * mx
        return a, b
    if t == "backref":
        return 0, -1
    return 0, 0


# ---------------------------------------------------------------------------------------------
# compiler: AST -> instruction list
#   0 MATCH   1 LIT s len   2 LITI s len   3 ANY nl   4 SET set   5 SPLIT x y   6 JMP x
#   7 SAVE k  8 ASSERT kind   9 BACKREF g icase   10 LOOK ahead neg prog width   11 REP ...
#   12 CHKEMPTY slot target   13 ATOMIC prog
# ---------------------------------------------------------------------------------------------

def _single(node):
    # one-character matcher: (kind, data) or None.  kinds: 0 char, 1 any but newline, 2 any, 3 set
    t = node[0]
    if t == "lit" and len(node[1]) == 1:
        if node[2] and node[1].lower() != node[1].upper():
            return (3, (node[1].lower() + node[1].upper(), False, False, False, ""))
        return (0, node[1])
    if t == "any":
        return (2, None) if node[1] else (1, None)
    if t == "set":
        return (3, node[1])
    return None


class _Compiler:
    def __init__(self, ng):
        self.li = 2 * (ng + 1)            # slot holding lastindex; marks follow
        self.nslot = self.li + 1

    def new_slot(self):
        k = self.nslot
        self.nslot += 1
        return k

    def prog(self, node):
        out = []
        self.emit(node, out)
        out.append((0,))
        return out

    def emit(self, node, out):
        t = node[0]
        if t == "lit":
            s = node[1]
            if node[2] and s.lower() != s.upper():
                out.append((2, s, len(s)))
            else:
                out.append((1, s, len(s)))
        elif t == "any":
            out.append((3, node[1]))
        elif t == "set":
            out.append((4, node[1]))
        elif t == "cat":
            for x in node[1]:
                self.emit(x, out)
        elif t == "alt":
            br = node[1]
            ends = []
            for k in range(len(br)):
                if k < len(br) - 1:
                    sp = len(out)
                    out.append(None)
                    self.emit(br[k], out)
                    ends.append(len(out))
                    out.append(None)
                    out[sp] = (5, sp + 1, len(out))
                else:
                    self.emit(br[k], out)
            for e in ends:
                out[e] = (6, len(out))
        elif t == "group":
            idx = node[1]
            if idx > 0:
                out.append((7, 2 * idx, 0, 0))
                self.emit(node[2], out)
                out.append((7, 2 * idx + 1, idx, self.li))
            else:
                self.emit(node[2], out)
        elif t == "assert":
            out.append((8, node[1]))
        elif t == "backref":
            out.append((9, node[1], node[2]))
        elif t == "look":
            sub = self.prog(node[3])
            w = _width(node[3])[0] if not node[1] else 0
            out.append((10, node[1], node[2], sub, w))
        elif t == "atomic":
            out.append((13, self.prog(node[1])))
        elif t == "rep":
            self.emit_rep(node, out)
        # 'empty' emits nothing

    def emit_rep(self, node, out):
        body = node[1]
        mn = node[2]
        mx = node[3]
        greedy = node[4]
        sg = _single(body)
        if sg is not None:
            out.append((11, sg[0], sg[1], mn, mx, greedy))
            return
        if mx == 0:
            return
        for _ in range(mn):
            self.emit(body, out)
        canempty = _width(body)[0] == 0
        if mx < 0:
            if mn >= 1 and not canempty:
                # x{n,} == x^(n-1) then loop: reuse the last emitted copy via a loop back
                # (simplest: emit star after the mandatory copies)
                pass
            slot = self.new_slot() if canempty else 0
            sp = len(out)
            out.append(None)
            if canempty:
                out.append((7, slot, 0, 0))
            self.emit(body, out)
            ce = len(out)
            if canempty:
                out.append(None)
            out.append((6, sp))
            end = len(out)
            if greedy:
                out[sp] = (5, sp + 1, end)
            else:
                out[sp] = (5, end, sp + 1)
            if canempty:
                out[ce] = (12, slot, end)
        else:
            sps = []
            for _ in range(mx - mn):
                sps.append(len(out))
                out.append(None)
                self.emit(body, out)
            end = len(out)
            for sp in sps:
                if greedy:
                    out[sp] = (5, sp + 1, end)
                else:
                    out[sp] = (5, end, sp + 1)


def _first_info(prog):
    # literal prefix / first-character set of a program, used to speed up searching
    k = 0
    while k < len(prog) and prog[k][0] == 7:
        k += 1
    ins = prog[k]
    op = ins[0]
    if op == 1 and ins[2] > 0:
        return ins[1], None
    if op == 4:
        st = ins[1]
        if not st[1] and not st[2]:
            return None, st[0]
    if op == 11 and ins[3] >= 1:
        if ins[1] == 0:
            return ins[2], None
        if ins[1] == 3:
            st = ins[2]
            if not st[1] and not st[2]:
                return None, st[0]
    return None, None


def _m1(kind, data, ch):
    if kind == 0:
        return ch == data
    if kind == 1:
        return ch != "\n"
    if kind == 2:
        return True
    return _inset(data, ch)


def _slow(st, ch):
    if st[3] and not ch.isascii():
        return True
    return st[4] != "" and ch in st[4]


def _inset(st, ch):
    r = ch in st[0]
    if not r and st[2]:
        r = _slow(st, ch)
    return r != st[1]


def _isw(ch):
    return ch.isalnum() or ch == "_"


def _run(prog, s, pos, n, caps, start, nostart, endreq):
    stack = []
    push = stack.append
    pc = 0
    while True:
        ins = prog[pc]
        op = ins[0]
        if op == 1:
            if s.startswith(ins[1], pos) and pos + ins[2] <= n:
                pos += ins[2]
                pc += 1
                continue
        elif op == 11:
            kind = ins[1]
            data = ins[2]
            mn = ins[3]
            mx = ins[4]
            lim = n
            if mx >= 0 and pos + mx < n:
                lim = pos + mx
            i = pos
            if ins[5]:
                if kind == 0:
                    while i < lim and s[i] == data:
                        i += 1
                elif kind == 1:
                    j = s.find("\n", pos, lim)
                    i = lim if j < 0 else j
                elif kind == 2:
                    i = lim
                else:
                    cs = data[0]
                    if not data[2]:
                        if data[1]:
                            while i < lim and s[i] not in cs:
                                i += 1
                        else:
                            while i < lim and s[i] in cs:
                                i += 1
                    else:
                        while i < lim and _inset(data, s[i]):
                            i += 1
                if i - pos >= mn:
                    if i - pos > mn:
                        nx = prog[pc + 1]
                        push((2, pc + 1, pos + mn, i, nx[1] if nx[0] == 1 else ""))
                    pos = i
                    pc += 1
                    continue
            else:
                ok = True
                while i < pos + mn:
                    if i >= n or not _m1(kind, data, s[i]):
                        ok = False
                        break
                    i += 1
                if ok:
                    if mx < 0 or i < pos + mx:
                        push((3, pc, i, lim))
                    pos = i
                    pc += 1
                    continue
        elif op == 4:
            if pos < n:
                st = ins[1]
                ch = s[pos]
                r = ch in st[0]
                if not r and st[2]:
                    r = _slow(st, ch)
                if r != st[1]:
                    pos += 1
                    pc += 1
                    continue
        elif op == 5:
            push((0, ins[2], pos))
            pc = ins[1]
            continue
        elif op == 6:
            pc = ins[1]
            continue
        elif op == 7:
            k = ins[1]
            if ins[2]:
                li = ins[3]
                push((4, k, caps[k], li, caps[li]))
                caps[k] = pos
                caps[li] = ins[2]
            else:
                push((1, k, caps[k]))
                caps[k] = pos
            pc += 1
            continue
        elif op == 3:
            if pos < n and (ins[1] or s[pos] != "\n"):
                pos += 1
                pc += 1
                continue
        elif op == 2:
            ln = ins[2]
            if pos + ln <= n and s[pos:pos + ln].lower() == ins[1]:
                pos += ln
                pc += 1
                continue
        elif op == 8:
            a = ins[1]
            if a == 0:
                ok = pos == 0
            elif a == 1:
                ok = pos == 0 or s[pos - 1] == "\n"
            elif a == 2:
                ok = pos == n or (pos == n - 1 and s[pos] == "\n")
            elif a == 3:
                ok = pos == n or s[pos] == "\n"
            elif a == 4:
                ok = pos == n
            else:
                w1 = pos > 0 and _isw(s[pos - 1])
                w2 = pos < n and _isw(s[pos])
                ok = (w1 != w2) if a == 5 else (w1 == w2)
            if ok:
                pc += 1
                continue
        elif op == 12:
            if pos == caps[ins[1]]:
                pc = ins[2]
            else:
                pc += 1
            continue
        elif op == 0:
            if endreq >= 0 and pos != endreq:
                pass
            elif pos == nostart:
                pass
            else:
                return pos
        elif op == 9:
            a = caps[2 * ins[1]]
            b = caps[2 * ins[1] + 1]
            if a >= 0 and b >= 0:
                t = s[a:b]
                ln = b - a
                if pos + ln <= n:
                    u = s[pos:pos + ln]
                    if u == t or (ins[2] and u.lower() == t.lower()):
                        pos += ln
                        pc += 1
                        continue
        elif op == 10:
            if ins[1]:
                st = pos
                end = -1
            else:
                st = pos - ins[4]
                end = pos
            matched = False
            saved = None
            if st >= 0:
                saved = caps[:]
                matched = _run(ins[3], s, st, n, caps, st, -1, end) >= 0
            if matched != ins[2]:
                if matched:
                    push((5, saved))
                pc += 1
                continue
            if matched:
                caps[:] = saved
        elif op == 13:
            saved = caps[:]
            e = _run(ins[1], s, pos, n, caps, pos, -1, -1)
            if e >= 0:
                push((5, saved))
                pos = e
                pc += 1
                continue
        # ---- failure: backtrack ----
        while True:
            if not stack:
                return -1
            e = stack.pop()
            t = e[0]
            if t == 0:
                pc = e[1]
                pos = e[2]
                break
            if t == 1:
                caps[e[1]] = e[2]
            elif t == 2:
                cur = e[3] - 1
                lo = e[2]
                nx = e[4]
                if nx:
                    while cur >= lo and not s.startswith(nx, cur):
                        cur -= 1
                if cur >= lo:
                    if cur > lo:
                        push((2, e[1], lo, cur, nx))
                    pc = e[1]
                    pos = cur
                    break
            elif t == 3:
                cur = e[2]
                rins = prog[e[1]]
                if cur < e[3] and _m1(rins[1], rins[2], s[cur]):
                    cur += 1
                    if cur < e[3]:
                        push((3, e[1], cur, e[3]))
                    pc = e[1] + 1
                    pos = cur
                    break
            elif t == 4:
                caps[e[1]] = e[2]
                caps[e[3]] = e[4]
            else:
                caps[:] = e[1]




# ---------------------------------------------------------------------------------------------
# public API
# ---------------------------------------------------------------------------------------------

class Match:
    def __init__(self, pattern, string, caps, pos, endpos):
        self.re = pattern
        self.string = string
        self._caps = caps
        self.pos = pos
        self.endpos = endpos
        li = caps[2 * (pattern.groups + 1)]
        self.lastindex = li if li > 0 else None

    @property
    def lastgroup(self):
        if self.lastindex is None:
            return None
        for k in self.re.groupindex:
            if self.re.groupindex[k] == self.lastindex:
                return k
        return None

    def _idx(self, g):
        if isinstance(g, int):
            if g < 0 or g > self.re.groups:
                raise IndexError("no such group")
            return g
        if g in self.re.groupindex:
            return self.re.groupindex[g]
        raise IndexError("no such group")

    def _grp(self, k):
        a = self._caps[2 * k]
        b = self._caps[2 * k + 1]
        if a < 0 or b < 0:
            return None
        return self.string[a:b]

    def group(self, *args):
        if not args:
            return self.string[self._caps[0]:self._caps[1]]
        if len(args) == 1:
            return self._grp(self._idx(args[0]))
        return tuple([self._grp(self._idx(g)) for g in args])

    def __getitem__(self, g):
        return self.group(g)

    def groups(self, default=None):
        r = []
        for k in range(1, self.re.groups + 1):
            v = self._grp(k)
            r.append(default if v is None else v)
        return tuple(r)

    def groupdict(self, default=None):
        d = {}
        gi = self.re.groupindex
        for name in gi:
            v = self._grp(gi[name])
            d[name] = default if v is None else v
        return d

    def start(self, g=0):
        k = self._idx(g)
        a = self._caps[2 * k]
        return a if self._caps[2 * k + 1] >= 0 else -1

    def end(self, g=0):
        k = self._idx(g)
        b = self._caps[2 * k + 1]
        return b if self._caps[2 * k] >= 0 else -1

    def span(self, g=0):
        k = self._idx(g)
        a = self._caps[2 * k]
        b = self._caps[2 * k + 1]
        if a < 0 or b < 0:
            return (-1, -1)
        return (a, b)

    def expand(self, template):
        return _expand(_parse_template(self.re, template), self)

    def __bool__(self):
        return True

    def __repr__(self):
        return "<re.Match object; span=(%d, %d), match=%r>" % (self._caps[0], self._caps[1], self.group())


def _parse_template(pat, t):
    if "\\" not in t:
        return [t]
    parts = []
    buf = []
    i = 0
    n = len(t)
    while i < n:
        c = t[i]
        i += 1
        if c != "\\":
            buf.append(c)
            continue
        if i >= n:
            raise error("bad escape (end of pattern)", t, i - 1)
        c = t[i]
        i += 1
        if c == "g":
            if i >= n or t[i] != "<":
                raise error("missing <", t, i)
            j = t.find(">", i)
            if j < 0:
                raise error("missing >, unterminated name", t, i)
            name = t[i + 1:j]
            i = j + 1
            if name.isdigit():
                g = int(name)
            elif name in pat.groupindex:
                g = pat.groupindex[name]
            else:
                raise IndexError("unknown group name '%s'" % name)
            if g > pat.groups:
                raise error("invalid group reference %d" % g, t, i)
            if buf:
                parts.append("".join(buf))
                buf = []
            parts.append(g)
        elif c.isdigit():
            j = i
            if j < n and t[j].isdigit():
                j += 1
            if c == "0":
                v = 0
                k = i
                while k < n and k < i + 2 and t[k] in "01234567":
                    k += 1
                buf.append(chr(int(t[i - 1:k], 8)))
                i = k
                continue
            g = int(t[i - 1:j])
            if g > pat.groups:
                raise error("invalid group reference %d" % g, t, i - 1)
            i = j
            if buf:
                parts.append("".join(buf))
                buf = []
            parts.append(g)
        elif c == "n":
            buf.append("\n")
        elif c == "t":
            buf.append("\t")
        elif c == "r":
            buf.append("\r")
        elif c == "f":
            buf.append("\x0c")
        elif c == "v":
            buf.append("\x0b")
        elif c == "a":
            buf.append("\x07")
        elif c == "b":
            buf.append("\x08")
        elif c == "\\":
            buf.append("\\")
        elif c.isalpha() and c.isascii():
            raise error("bad escape \\" + c, t, i - 2)
        else:
            buf.append("\\" + c)
    if buf:
        parts.append("".join(buf))
    return parts


def _expand(parts, m):
    out = []
    for p in parts:
        if isinstance(p, str):
            out.append(p)
        else:
            v = m._grp(p)
            if v is not None:
                out.append(v)
    return "".join(out)


class Pattern:
    def __init__(self, pattern, flags, ast, ng, names):
        self.pattern = pattern
        self.groups = ng
        self.groupindex = names
        if not (flags & 256):
            flags |= 32
        self.flags = flags
        cp = _Compiler(ng)
        self._prog = cp.prog(ast)
        self._ncap = cp.nslot
        self._prefix, self._first = _first_info(self._prog)
        k = 0
        while k < len(self._prog) and self._prog[k][0] == 7:
            k += 1
        ins = self._prog[k]
        self._bos = ins[0] == 8 and ins[1] == 0

    def _exec(self, s, pos, endpos, mode, na):
        # mode 0 search, 1 match, 2 fullmatch; na: position where an empty match is not allowed
        ln = len(s)
        if endpos is None or endpos > ln:
            endpos = ln
        if pos < 0:
            pos = 0
        if pos > endpos:
            return None
        prog = self._prog
        caps = [-1] * self._ncap
        endreq = endpos if mode == 2 else -1
        if mode != 0 or self._bos:
            if mode == 0 and pos > 0:
                return None
            e = _run(prog, s, pos, endpos, caps, pos, pos if pos == na else -1, endreq)
            if e < 0:
                return None
            caps[0] = pos
            caps[1] = e
            return Match(self, s, caps, pos, endpos)
        start = pos
        pre = self._prefix
        fs = self._first
        while start <= endpos:
            if pre:
                start = s.find(pre, start, endpos)
                if start < 0:
                    return None
            elif fs is not None:
                while start < endpos and s[start] not in fs:
                    start += 1
                if start >= endpos:
                    return None
            e = _run(prog, s, start, endpos, caps, start, start if start == na else -1, -1)
            if e >= 0:
                caps[0] = start
                caps[1] = e
                return Match(self, s, caps, pos, endpos)
            start += 1
        return None

    def match(self, string, pos=0, endpos=None):
        return self._exec(string, pos, endpos, 1, -1)

    def fullmatch(self, string, pos=0, endpos=None):
        return self._exec(string, pos, endpos, 2, -1)

    def search(self, string, pos=0, endpos=None):
        return self._exec(string, pos, endpos, 0, -1)

    def _iter(self, string, pos=0, endpos=None):
        na = -1
        n = len(string) if endpos is None else min(endpos, len(string))
        while pos <= n:
            m = self._exec(string, pos, endpos, 0, na)
            if m is None:
                return
            yield m
            e = m._caps[1]
            if e == m._caps[0]:
                na = e
            else:
                na = -1
            pos = e

    def finditer(self, string, pos=0, endpos=None):
        return self._iter(string, pos, endpos)

    def findall(self, string, pos=0, endpos=None):
        res = []
        ng = self.groups
        for m in self._iter(string, pos, endpos):
            if ng == 0:
                res.append(string[m._caps[0]:m._caps[1]])
            elif ng == 1:
                v = m._grp(1)
                res.append("" if v is None else v)
            else:
                res.append(m.groups(""))
        return res

    def split(self, string, maxsplit=0):
        res = []
        last = 0
        cnt = 0
        ng = self.groups
        for m in self._iter(string):
            if maxsplit > 0 and cnt >= maxsplit:
                break
            res.append(string[last:m._caps[0]])
            for k in range(1, ng + 1):
                res.append(m._grp(k))
            last = m._caps[1]
            cnt += 1
        res.append(string[last:])
        return res

    def subn(self, repl, string, count=0):
        if callable(repl):
            parts = None
        else:
            parts = _parse_template(self, repl)
        out = []
        last = 0
        cnt = 0
        for m in self._iter(string):
            if count > 0 and cnt >= count:
                break
            out.append(string[last:m._caps[0]])
            if parts is None:
                out.append(repl(m))
            elif len(parts) == 1 and isinstance(parts[0], str):
                out.append(parts[0])
            else:
                out.append(_expand(parts, m))
            last = m._caps[1]
            cnt += 1
        out.append(string[last:])
        return "".join(out), cnt

    def sub(self, repl, string, count=0):
        return self.subn(repl, string, count)[0]

    def scanner(self, string, pos=0, endpos=None):
        return _Scanner(self, string, pos, endpos)

    def __repr__(self):
        return "re.compile(%r)" % (self.pattern,) if self.flags == 32 else \
            "re.compile(%r, %s)" % (self.pattern, _flagstr(self.flags))

    def __eq__(self, o):
        return isinstance(o, Pattern) and o.pattern == self.pattern and o.flags == self.flags

    def __hash__(self):
        return hash((self.pattern, self.flags))

    def __copy__(self):
        return self

    def __deepcopy__(self, memo):
        return self


def _flagstr(f):
    names = [("re.IGNORECASE", 2), ("re.LOCALE", 4), ("re.MULTILINE", 8), ("re.DOTALL", 16),
             ("re.UNICODE", 32), ("re.VERBOSE", 64), ("re.DEBUG", 128), ("re.ASCII", 256)]
    parts = []
    for nm, v in names:
        if f & v and not (v == 32 and f & 256):
            parts.append(nm)
    return "|".join(parts) if parts else "0"


class _Scanner:
    def __init__(self, pat, string, pos, endpos):
        self.pattern = pat
        self._it = pat._iter(string, pos, endpos)

    def search(self):
        for m in self._it:
            return m
        return None


_cache = {}


def compile(pattern, flags=0):
    if isinstance(pattern, Pattern):
        if flags:
            raise ValueError("cannot process flags argument with a compiled pattern")
        return pattern
    key = (pattern, int(flags))
    p = _cache.get(key)
    if p is not None:
        return p
    ps = _Parser(pattern, int(flags))
    ast = ps.parse()
    p = Pattern(pattern, ps.flags, ast, ps.ng, ps.names)
    if len(_cache) > 200:
        _cache.clear()
    _cache[key] = p
    return p


def purge():
    _cache.clear()


def match(pattern, string, flags=0):
    return compile(pattern, flags)._exec(string, 0, None, 1, -1)


def fullmatch(pattern, string, flags=0):
    return compile(pattern, flags)._exec(string, 0, None, 2, -1)


def search(pattern, string, flags=0):
    return compile(pattern, flags)._exec(string, 0, None, 0, -1)


def findall(pattern, string, flags=0):
    return compile(pattern, flags).findall(string)


def finditer(pattern, string, flags=0):
    return compile(pattern, flags)._iter(string)


def split(pattern, string, maxsplit=0, flags=0):
    return compile(pattern, flags).split(string, maxsplit)


def sub(pattern, repl, string, count=0, flags=0):
    return compile(pattern, flags).subn(repl, string, count)[0]


def subn(pattern, repl, string, count=0, flags=0):
    return compile(pattern, flags).subn(repl, string, count)


def escape(pattern):
    out = []
    for c in pattern:
        if c in _SPECIAL:
            out.append("\\" + c)
        else:
            out.append(c)
    return "".join(out)
