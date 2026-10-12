#!/usr/bin/env python3
"""Fold a C source file to fixed-width card images (the VM/370 "deck step").

usage: python3 tools/deck.py [--limit N] IN.c > OUT.c     (default N = 71)
       python3 tools/deck.py --check [--limit N] FILE.c   (exit 1 if a line is longer)
       python3 tools/deck.py --verify ORIG.c DECK.c       (same tokens, comments aside)

The reflow is conservative and never changes the token stream of the C
program: lines are broken only BETWEEN tokens (preferably after commas and
before operators, at the lowest parenthesis depth), never inside a string
literal, character constant or identifier.  A string literal longer than a
card is cut into adjacent literals ("ab" "cd"), at an escape boundary.
Comments are re-wrapped word by word; a trailing comment on a line that is too
long moves to its own line(s) after the code.  Lines inside macro definitions
keep their trailing backslash.  No external tools are needed.

The result is also checked to be pure ASCII without tabs, trailing blanks or
trigraph sequences.  Exit status 1 and a message on stderr if a line cannot be
folded (for example one very long identifier or a long #include)."""
import re
import sys

TOKEN = re.compile(r"""
    (?P<ws>[ \t]+)
  | (?P<comment>/\*.*?\*/|/\*.*$)
  | (?P<string>"(?:\\.|[^"\\])*")
  | (?P<char>'(?:\\.|[^'\\])+')
  | (?P<number>\.?[0-9](?:[eEpP][+-]|[A-Za-z0-9_.])*)
  | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<punct><<=|>>=|\.\.\.|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\+=|-=|\*=|/=|%=|&=|\^=|\|=|\#\#|.)
""", re.X)

BINOPS = {"+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=", "&", "|", "^",
          "<<", ">>", "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="}
OPEN = {"(", "[", "{"}
CLOSE = {")", "]", "}"}


class DeckError(Exception):
    pass


def tokenize(text):
    """-> list of (pre, kind, text); pre is the white space before the token"""
    toks, pre, pos = [], "", 0
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if m is None:
            raise DeckError("cannot tokenize: " + text[pos:pos + 20])
        kind = m.lastgroup
        if kind == "ws":
            pre = m.group()
        else:
            toks.append((pre, kind, m.group()))
            pre = ""
        pos = m.end()
    return toks


def scan_state(line, in_comment):
    """is a comment still open at the end of this line?"""
    pos = 0
    while pos < len(line):
        if in_comment:
            k = line.find("*/", pos)
            if k < 0:
                return True
            in_comment, pos = False, k + 2
            continue
        m = TOKEN.match(line, pos)
        if m is None:
            return in_comment
        if m.lastgroup == "comment":
            t = m.group()
            if not (t.endswith("*/") and len(t) >= 4):
                return True
        pos = m.end()
    return in_comment


def split_string(tok, room):
    """cut a string literal "..." into pieces of at most `room` columns"""
    body = tok[1:-1]
    atoms = re.findall(r'\\x[0-9a-fA-F]+|\\[0-7]{1,3}|\\.|.', body, re.S)
    pieces, cur = [], []
    size = 0
    for a in atoms:
        if cur and size + len(a) + 2 > room:
            # prefer to cut after a blank (in the last third of the piece)
            k = len(cur)
            while k > 0 and cur[k - 1] != " ":
                k -= 1
            if k > 0 and sum(len(x) for x in cur[:k]) * 3 >= size * 2:
                pieces.append("".join(cur[:k]))
                cur = cur[k:]
            else:
                pieces.append("".join(cur))
                cur = []
            size = sum(len(x) for x in cur)
        cur.append(a)
        size += len(a)
    pieces.append("".join(cur))
    return ['"%s"' % p for p in pieces]


def rank_of(prev, tok, header_end):
    """quality of a break between tokens prev and tok (lower is better), or None"""
    pk, pt = prev[1], prev[2]
    tk, tt = tok[1], tok[2]
    if pt == ";":
        return 0
    if pt == "}" and tt in ("else", "while"):
        return 20
    if pt in ("{", "}"):
        return 1
    if pk == "string" and tk == "string":
        return 1
    if pt == ",":
        return 2
    if tt in CLOSE or tt in (";", ","):
        return 20
    if header_end and pt == ")":
        return 2
    if tt in ("&&", "||"):
        return 3
    if tt in ("?", ":"):
        return 4
    if tt in BINOPS and (pk in ("ident", "number", "string", "char") or pt in (")", "]")):
        return 5
    if pt == "(":
        return 7
    return 9


def layout(toks, indent, limit):
    """lay the tokens out in lines of at most `limit` columns"""
    if not toks:
        return []
    ind = " " * indent
    cont = " " * (indent + 4)
    depth, cum = [], 0
    for t in toks:
        depth.append(cum)
        if t[2] in OPEN:
            cum += 1
        elif t[2] in CLOSE:
            cum -= 1
    base = min(depth)
    lead = [t[2] for t in toks if t[2] not in ("}", "else")]
    header = bool(lead) and lead[0] in ("if", "while", "for")
    # a string that cannot fit on a card of its own is cut up front
    room = limit - (indent + 4) - 3
    nt = []
    for t in toks:
        if t[1] == "string" and len(t[2]) > room:
            parts = split_string(t[2], room)
            nt.append((t[0], "string", parts[0]))
            for p in parts[1:]:
                nt.append((" ", "string", p))
            continue
        nt.append(t)
    if len(nt) != len(toks):
        # recompute depth for the new token list
        depth, cum = [], 0
        for t in nt:
            depth.append(cum)
            if t[2] in OPEN:
                cum += 1
            elif t[2] in CLOSE:
                cum -= 1
        base = min(depth)
    toks = nt
    out = []
    line = ind + toks[0][2]
    starts = [(0, len(ind))]          # (token index, column) in the current line
    i = 1
    while i < len(toks) or len(line) > limit:
        more = i < len(toks)
        if more:
            pre, kind, text = toks[i]
            if len(line) <= limit and len(line) + len(pre) + len(text) <= limit:
                starts.append((i, len(line) + len(pre)))
                line += pre + text
                i += 1
                continue
        # break needed: pick the best place among the tokens already on the line
        cands = list(starts[1:])
        if more and len(line) <= limit:
            cands.append((i, len(line) + len(pre)))
        best = None
        for j, col in cands:
            r = rank_of(toks[j - 1], toks[j], header and depth[j] == base)
            key = (r + 3 * (depth[j] - base), -j)
            if best is None or key < best[0]:
                best = (key, j, col)
        if best is None:
            raise DeckError("token too long for a card: " + line.strip()[:60])
        _, j, col = best
        if j == i:
            out.append(line.rstrip())
            line = cont + text
            starts = [(i, len(cont))]
            i += 1
        else:
            out.append(line[:col - len(toks[j][0])].rstrip())
            line = cont + line[col:]
            starts = [(jj, c - col + len(cont)) for jj, c in starts if jj >= j]
    out.append(line)
    for ln in out:
        if len(ln) > limit:
            raise DeckError("line still too long: " + ln.strip()[:70])
    return out


def wrap_words(first, cont, words, limit):
    lines, cur = [], first
    empty = True
    for w in words:
        if not empty and len(cur) + 1 + len(w) > limit:
            lines.append(cur)
            cur = cont
            empty = True
        cur += ("" if empty else " ") + w
        empty = False
    lines.append(cur)
    for ln in lines:
        if len(ln) > limit:
            raise DeckError("comment word too long: " + ln.strip()[:70])
    return lines


def shrink_runs(text, over):
    """shorten a run of ====/----/**** by up to `over` characters"""
    m = None
    for m in re.finditer(r"([=\-*#~_])\1{5,}", text):
        pass
    if m is None:
        return text
    run = m.end() - m.start()
    cut = min(over, run - 4)
    if cut <= 0:
        return text
    return text[:m.start()] + text[m.start() + cut:]


def wrap_comment_line(line, limit, in_comment):
    """re-wrap one line that is a comment (or a comment continuation)"""
    if len(line) > limit:
        line = shrink_runs(line, len(line) - limit)
        if len(line) <= limit:
            return [line]
    m = re.match(r"^(\s*)(/\*+\s*)?(\*\s+)?(.*)$", line)
    ws, open_, star, rest = m.group(1), m.group(2) or "", m.group(3) or "", m.group(4)
    if in_comment:
        open_ = ""
    first = ws + open_ + star
    cont = ws + " " * len(open_) + star
    if open_.strip() == "/*" and not star:
        cont = ws + " " * len(open_)
    return wrap_words(first, cont, rest.split(), limit)


def statements(toks):
    """split a token list after each ';' that is outside parentheses;
    returns (tokens, brace level) pairs: the level indents the statements
    that follow a '{' on the same line"""
    parts, cur, cum, bl, start = [], [], 0, 0, 0
    for k, t in enumerate(toks):
        cur.append(t)
        if t[2] in ("(", "["):
            cum += 1
        elif t[2] in (")", "]"):
            cum -= 1
        elif t[2] == "{":
            bl += 1
        elif t[2] == "}":
            bl -= 1
        if t[2] == ";" and cum == 0 and k < len(toks) - 1:
            parts.append((cur, start))
            cur = []
            start = bl
            if toks[k + 1][2] == "}":
                start = bl - 1
    parts.append((cur, start))
    return [(p, max(lv, 0)) for p, lv in parts]


def reflow_line(line, limit, in_directive):
    bs = line.endswith("\\")
    content = line[:-1].rstrip() if bs else line
    directive = in_directive or content.lstrip().startswith("#")
    eff = limit - 2 if (bs or directive) else limit
    indent = min(len(content) - len(content.lstrip(" ")), 32)
    toks = tokenize(content.lstrip(" "))
    comments = [t for t in toks if t[1] == "comment"]
    code = [t for t in toks if t[1] != "comment"]
    if code:
        code[0] = ("", code[0][1], code[0][2])
    if directive and comments and bs:
        raise DeckError("comment in a long macro line: " + line.strip()[:60])
    try:
        out = []
        for part, lv in (statements(code) if code else []):
            part[0] = ("", part[0][1], part[0][2])
            out.extend(layout(part, indent + 4 * lv, eff))
    except DeckError as e:
        raise DeckError("%s [in: %s]" % (e, line.strip()[:100]))
    if directive:
        # all but the last piece continue; the last continues if the original did
        for k in range(len(out)):
            if k < len(out) - 1 or bs:
                out[k] += " \\"
    elif bs:
        out[-1] += " \\"
    if comments:
        text = " ".join(c[2] for c in comments)
        cl = wrap_comment_line(" " * indent + text, limit, False)
        out = cl + out if directive else out + cl
    return out


def comment_only(line):
    toks = tokenize(line)
    return len(toks) == 1 and toks[0][1] == "comment"


def comment_prefix(line, in_comment):
    """(first-line prefix, continuation prefix, words) of a comment line"""
    m = re.match(r"^(\s*)(/\*+\s*)?(\*\s+)?(.*)$", line)
    ws, open_, star, rest = m.group(1), m.group(2) or "", m.group(3) or "", m.group(4)
    if in_comment:
        open_ = ""
    return ws + open_ + star, ws + " " * len(open_) + star, rest.split()


def plain_continuation(line, cont):
    """is `line` a plain text line that continues a paragraph with prefix cont?"""
    if not line.startswith(cont) or not line.strip():
        return False
    text = line[len(cont):]
    if not text or text[0] == " " or "/*" in text:
        return False
    return re.match(r"([-*+]\s|[0-9]+[.)]\s|=|-)", text) is None


def deck(text, limit):
    lines = text.split("\n")
    out, in_comment, in_dir = [], False, False
    i = 0
    while i < len(lines):
        line = lines[i].rstrip(" ")
        i += 1
        was_dir = in_dir or (not in_comment and line.lstrip().startswith("#"))
        in_dir = was_dir and line.endswith("\\")
        if was_dir and not line.lstrip().startswith("#"):
            # macro continuation: the spliced logical line must stay short
            line = "  " + line.lstrip()
        after = scan_state(line, in_comment)
        if len(line) <= limit:
            out.append(line)
        elif in_comment or comment_only(line):
            if in_comment and "*/" in line and not line.rstrip().endswith("*/"):
                raise DeckError("code after comment end: " + line.strip()[:60])
            pieces = wrap_comment_line(line, limit, in_comment)
            first, cont, _ = comment_prefix(line, in_comment)
            # pull the next lines of the paragraph up behind the last piece
            while (len(pieces) > 1 and after and i < len(lines)
                   and plain_continuation(lines[i], cont)
                   and "*/" not in lines[i][:-2]):
                nxt = lines[i].rstrip(" ")
                i += 1
                last = pieces.pop()
                pieces.extend(wrap_words(
                    cont, cont, last[len(cont):].split() + nxt[len(cont):].split(), limit))
                after = scan_state(nxt, after)
            if len(pieces) == 1 and not in_comment:
                pass
            out.extend(pieces)
        else:
            out.extend(reflow_line(line, limit, was_dir))
        in_comment = after
    return "\n".join(out)


def problems(text, limit):
    bad = []
    for n, line in enumerate(text.split("\n"), 1):
        if len(line) > limit:
            bad.append("%d: %d columns" % (n, len(line)))
        if "\t" in line or any(ord(c) > 126 for c in line):
            bad.append("%d: tab or non-ASCII character" % n)
        if line != line.rstrip(" "):
            bad.append("%d: trailing blanks" % n)
        if "??" in line:
            bad.append("%d: possible trigraph" % n)
    return bad


def code_tokens(text):
    """token texts without comments; adjacent string literals are merged;
    line ends (and backslash-newline) are just white space"""
    text = text.replace("\\\n", " ").replace("\n", " ")
    pos, out = 0, []
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if m is None:
            raise DeckError("verify: cannot tokenize at %d" % pos)
        kind = m.lastgroup
        if kind == "comment":
            k = text.find("*/", pos + 2)
            pos = len(text) if k < 0 else k + 2
            continue
        pos = m.end()
        if kind == "ws":
            continue
        t = m.group()
        if kind == "string" and out and out[-1][0] == "string":
            out[-1] = ("string", out[-1][1][:-1] + t[1:])
        else:
            out.append((kind, t))
    return [t[1] for t in out]


def main(argv):
    limit = 71
    mode = "deck"
    args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--limit":
            limit = int(argv[i + 1])
            i += 1
        elif a == "--check":
            mode = "check"
        elif a == "--verify":
            mode = "verify"
        else:
            args.append(a)
        i += 1
    if mode == "check":
        bad = []
        for f in args:
            with open(f) as fp:
                bad += ["%s:%s" % (f, b) for b in problems(fp.read(), limit)]
        if bad:
            sys.stderr.write("\n".join(bad[:40]) + "\n%d problem(s)\n" % len(bad))
            return 1
        return 0
    if mode == "verify":
        a = code_tokens(open(args[0]).read())
        b = code_tokens(open(args[1]).read())
        if a != b:
            for k, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    sys.stderr.write("token %d differs: %r vs %r\n" % (k, x, y))
                    break
            else:
                sys.stderr.write("token count differs: %d vs %d\n" % (len(a), len(b)))
            return 1
        return 0
    with open(args[0]) as fp:
        src = fp.read()
    try:
        res = deck(src, limit)
    except DeckError as e:
        sys.stderr.write("deck: %s\n" % e)
        return 1
    bad = problems(res, limit)
    if bad:
        sys.stderr.write("deck: " + "; ".join(bad[:10]) + "\n")
        return 1
    sys.stdout.write(res)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
