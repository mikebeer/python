#!/usr/bin/env python3
"""Static check of the amalgamated C interpreter for VM/370 (CMS, GCCLIB31, EBCDIC cards).

usage: python3 tools/vm370_check.py [--limit N] [--no-nm] [--strict-chars] [FILE.c ...]

With no file the default set is checked: py_all.c (development amalgamation), deck/py_deck.c and,
when present, deck/py_deck_embed.c (the files that go to the card reader).  Files whose path
contains "deck" are "deck builds": the card-image rules (columns, character literals) are errors
there and only reported as information for the development amalgamation.  Exit status 0 means no
error (warnings do not fail the run).

Checks (E = error, exit status 1; W = warning; I = information):
  E  line longer than the card width (default 71) in a deck build, tab, form feed, non-ASCII byte,
     trailing blank, a "??" sequence anywhere (it would be read as a trigraph)
  W  character literal ('x') in code, outside strings and comments, that names a character without a
     stable EBCDIC card code:  ^ [ ] { } ~ \\ | #  (the escape '\\\\' counts as a backslash).  A warning
     because the same characters are the C syntax itself ({ } [ ] # in every line) and so need the
     code page translation of the upload anyway; --strict-chars makes it an error
  E  #include of a header that is not in C89 (unless inside #ifdef PY_POSIX / #if defined(PY_POSIX))
  E  printf/scanf conversions that are not C89: ll, z, j, t, hh, %a %A %F, and "long long"
  E  C99/C11 keywords and library names: // comments, inline, restrict, _Bool, snprintf, strtoll,
     va_copy, __func__, stdint types (int64_t ...), designated initialisers are not checked
  E  an identifier longer than 31 characters (C89 guarantees 31 for internal names)
  E  a logical source line (after joining backslash continuations) longer than 509 characters, or a
     string literal longer than 509 characters
  E  external names: the file is compiled with cc -std=c89 and nm lists the defined global symbols:
     every one other than main is an error (and any name over 8 characters, or differing from
     another only in case); --no-nm skips this step (no compiler available)
  I  how many characters that need trigraphs / code page care appear in code: # [ ] \\ ^ { } | ~
     (the card punch / 3270 upload must map them; see PORTING.md section 9)
"""
import os
import re
import subprocess
import sys
import tempfile

C89_HEADERS = {"assert.h", "ctype.h", "errno.h", "float.h", "limits.h", "locale.h", "math.h",
               "setjmp.h", "signal.h", "stdarg.h", "stddef.h", "stdio.h", "stdlib.h", "string.h",
               "time.h"}
BAD_CHARS = set("^[]{}~\\|#")
TRIGRAPH_CHARS = set("#[]\\^{}|~")
BAD_WORDS = {"inline", "restrict", "_Bool", "snprintf", "vsnprintf", "strtoll", "strtoull", "va_copy",
             "__func__", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
             "uint64_t", "intptr_t", "uintptr_t", "intmax_t", "uintmax_t", "bool", "true", "false",
             "_Static_assert", "_Noreturn", "_Alignas", "_Alignof", "_Generic", "isblank", "strdup",
             "lround", "round", "trunc", "copysign", "isnan", "isinf", "fmin", "fmax", "cbrt", "log2",
             "log1p", "expm1", "hypot", "erf", "tgamma", "lgamma", "nan", "rint", "fma", "asinh",
             "acosh", "atanh", "exp2", "remainder"}
KEYWORDS99 = {"inline", "restrict", "_Bool", "va_copy", "__func__", "bool", "true", "false", "int8_t",
              "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "intptr_t",
              "uintptr_t", "intmax_t", "uintmax_t", "_Static_assert", "_Noreturn", "_Alignas", "_Alignof",
              "_Generic"}
FORMAT = re.compile(r"%[-+ #0]*(?:\*|[0-9]+)?(?:\.(?:\*|[0-9]+))?(hh|ll|[hlLzjt])?([a-zA-Z%])")
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


class Report:
    def __init__(self):
        self.errors = 0
        self.warnings = 0
        self.lines = []

    def err(self, fn, ln, msg):
        self.errors += 1
        if self.errors <= 200:
            self.lines.append("E %s:%d: %s" % (fn, ln, msg))

    def info(self, msg):
        self.lines.append("I " + msg)

    def warn(self, msg):
        self.warnings += 1
        self.lines.append("W " + msg)


def scan(text):
    """Split C source into code / comment / string / char pieces.
    Yields (kind, text, line) with kind in code comment string char."""
    i, n, line = 0, len(text), 1
    start = 0
    out = []
    code_start = 0

    def flush(end, kind, ln):
        if end > code_start:
            out.append((kind, text[code_start:end], ln))

    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            if i > code_start:
                out.append(("code", text[code_start:i], line))
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(("comment", text[i:j], line))
            line += text.count("\n", i, j)
            i = code_start = j
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            if i > code_start:
                out.append(("code", text[code_start:i], line))
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(("cxxcomment", text[i:j], line))
            i = code_start = j
        elif c == '"' or c == "'":
            if i > code_start:
                out.append(("code", text[code_start:i], line))
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(("string" if c == '"' else "char", text[i:j], line))
            line += text.count("\n", i, j)
            i = code_start = j
        else:
            if c == "\n":
                line += 1
            i += 1
    if code_start < n:
        out.append(("code", text[code_start:], line))
    return out


def char_value(lit):
    """the characters a char literal names (escapes decoded for the two that matter)"""
    body = lit[1:-1]
    if body.startswith("\\"):
        e = body[1:]
        if e in ("\\", "'", '"'):
            return e
        return ""          # \n \0 \x41 \101 ...: a numeric or control escape, no source character
    return body


def check_file(path, limit, rep, deck_build, strict_chars=False):
    raw = open(path, "rb").read()
    base = os.path.basename(path)
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as e:
        rep.err(base, raw[:e.start].count(b"\n") + 1, "non-ASCII byte 0x%02x" % raw[e.start])
        text = raw.decode("latin-1")
    lines = text.split("\n")
    for ln, s in enumerate(lines, 1):
        if "\t" in s:
            rep.err(base, ln, "tab")
        if "\f" in s or "\r" in s:
            rep.err(base, ln, "form feed or carriage return")
        if s.endswith(" "):
            rep.err(base, ln, "trailing blank")
        if "??" in s:
            rep.err(base, ln, "'??' (trigraph introducer)")
        if deck_build and len(s) > limit:
            rep.err(base, ln, "line is %d columns (limit %d)" % (len(s), limit))

    # logical line length (backslash continuations)
    logical, start = "", 1
    for ln, s in enumerate(lines, 1):
        if not logical:
            start = ln
        if s.endswith("\\"):
            logical += s[:-1]
        else:
            logical += s
            if len(logical) > 509 and deck_build:
                rep.err(base, start, "logical line of %d characters (C89 guarantees 509)" % len(logical))
            elif len(logical) > 509:
                rep.info("%s:%d: logical line of %d characters (the deck step squeezes macro continuations)" % (
                    base, start, len(logical)))
            logical = ""

    pieces = scan(text)
    bad_lits = {}
    tri = {}
    longest_id = ("", 0)
    for kind, t, ln in pieces:
        if kind == "cxxcomment":
            rep.err(base, ln, "// comment")
        elif kind == "string":
            if len(t) - 2 > 509:
                rep.err(base, ln, "string literal of %d characters" % (len(t) - 2))
            for m in FORMAT.finditer(t):
                lm, cv = m.group(1), m.group(2)
                if lm in ("ll", "z", "j", "t", "hh") or cv in ("a", "A", "F"):
                    rep.err(base, ln, "non-C89 conversion '%s'" % m.group(0))
        elif kind == "char":
            v = char_value(t)
            if v and v in BAD_CHARS:
                bad_lits.setdefault(t, []).append(ln)
        elif kind == "code":
            for ch in t:
                if ch in TRIGRAPH_CHARS:
                    tri[ch] = tri.get(ch, 0) + 1
            for m in IDENT.finditer(t):
                w = m.group()
                if len(w) > 31:
                    rep.err(base, ln, "identifier '%s' has %d characters" % (w, len(w)))
                if len(w) > len(longest_id[0]):
                    longest_id = (w, len(w))
                if w in BAD_WORDS:
                    is_call = re.match(r"\s*\(", t[m.end():m.end() + 8]) is not None
                    if w in KEYWORDS99 or is_call:
                        rep.err(base, ln, "C99 name '%s'" % w)
            if re.search(r"\blong\s+long\b", t):
                rep.err(base, ln, "long long")
            if "$" in t or "@" in t or "`" in t:
                rep.err(base, ln, "'$', '@' or '`' in code")
    for lit, where in sorted(bad_lits.items()):
        msg = "character literal %s (%d use%s, first at line %d): no stable EBCDIC card code" % (
            lit, len(where), "" if len(where) == 1 else "s", where[0])
        if deck_build and strict_chars:
            rep.err(base, where[0], msg)
        else:
            rep.warn("%s:%d: %s" % (base, where[0], msg))
    rep.info("%s: longest identifier %s (%d); characters needing trigraph care in code: %s" % (
        base, longest_id[0], longest_id[1],
        " ".join("%s=%d" % (k, tri[k]) for k in sorted(tri)) or "none"))

    # includes: C89 or guarded by PY_POSIX
    depth_posix = []   # stack of booleans: is this #if level a PY_POSIX / PY_HOST_* block
    for ln, s in enumerate(lines, 1):
        m = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif|include)\b(.*)", s)
        if not m:
            continue
        d, rest = m.group(1), m.group(2)
        if d in ("if", "ifdef"):
            depth_posix.append(bool(re.search(r"PY_POSIX|PY_HOST_POSIX|_WIN32|__unix", rest)))
        elif d == "ifndef":
            depth_posix.append(False)
        elif d == "endif":
            if depth_posix:
                depth_posix.pop()
        elif d == "include":
            h = re.match(r'\s*[<"]([^>"]+)[>"]', rest)
            if h and h.group(1) not in C89_HEADERS and not any(depth_posix):
                rep.err(base, ln, "#include <%s> is not a C89 header and is not under PY_POSIX" % h.group(1))


def check_nm(path, rep):
    base = os.path.basename(path)
    cc = os.environ.get("CC", "cc")
    d = tempfile.mkdtemp()
    obj = os.path.join(d, "x.o")
    r = subprocess.run([cc, "-std=c89", "-w", "-c", "-o", obj, path], capture_output=True, text=True)
    if r.returncode != 0:
        rep.info("%s: --nm skipped, compile failed: %s" % (base, r.stderr.strip().split("\n")[0]))
        return
    r = subprocess.run(["nm", "-g", "--defined-only", obj], capture_output=True, text=True)
    names = [l.split()[-1] for l in r.stdout.splitlines() if l.strip()]
    seen = {}
    for nm in names:
        if nm != "main":
            rep.err(base, 0, "nm: external symbol '%s'" % nm)
        if len(nm) > 8:
            rep.err(base, 0, "nm: external name '%s' longer than 8 characters" % nm)
        if nm.lower() in seen and seen[nm.lower()] != nm:
            rep.err(base, 0, "nm: '%s' and '%s' differ only in case" % (nm, seen[nm.lower()]))
        seen[nm.lower()] = nm
    rep.info("%s: nm external symbols: %s" % (base, " ".join(names) or "none"))
    os.remove(obj)
    os.rmdir(d)


def main(argv):
    limit, nm, files, strict_chars = 71, True, [], False
    i = 0
    while i < len(argv):
        if argv[i] == "--limit":
            limit = int(argv[i + 1])
            i += 1
        elif argv[i] == "--strict-chars":
            strict_chars = True
        elif argv[i] == "--no-nm":
            nm = False
        else:
            files.append(argv[i])
        i += 1
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    if not files:
        files = [os.path.join(root, f) for f in ("py_all.c", "deck/py_deck.c", "deck/py_deck_embed.c")
                 if os.path.exists(os.path.join(root, f))]
    rep = Report()
    for f in files:
        check_file(f, limit, rep, "deck" in os.path.basename(f) or "deck" in os.path.dirname(f), strict_chars)
        if nm:
            check_nm(f, rep)
    for l in rep.lines:
        print(l)
    print("vm370_check: %s (%d error%s, %d warning%s)" % ("FAILED" if rep.errors else "ok", rep.errors,
          "" if rep.errors == 1 else "s", rep.warnings, "" if rep.warnings == 1 else "s"))
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
