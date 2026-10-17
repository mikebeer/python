# Interactive loop of the CREXX interpreter (`py -i`): reads stdin line by line, runs each complete
# statement or block in this module's namespace, and follows the protocol of the C interpreter:
# the prompts ">>> " / "... " (no newline) before every line read, the repr of an expression
# statement, a traceback on standard error, a final newline at end of input.
# The loop runs at module level because exec() / eval() without namespace arguments use the
# namespace of the module that calls them.
import sys

__name__ = "__main__"


def _repl_depth(s):
    d = 0
    q = ""
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if q:
            if c == "\\":
                i += 1
            elif s.startswith(q, i):
                i += len(q) - 1
                q = ""
        elif c == '"' or c == "'":
            if s.startswith(c * 3, i):
                q = c * 3
                i += 2
            else:
                q = c
        elif c == "#":
            while i < n and s[i] != "\n":
                i += 1
        elif c in "([{":
            d += 1
        elif c in ")]}":
            d -= 1
        i += 1
    return d, q


def _repl_opens_block(line):
    t = line.strip()
    k = t.find("#")
    if k >= 0:
        t = t[:k].rstrip()
    return t.endswith(":") or t.startswith("@")


def _repl_report(e):
    sys.stdout.flush()
    sys.stderr.write("Traceback (most recent call last):\n  File \"<stdin>\", line 1, in <module>\n")
    m = str(e)
    sys.stderr.write(type(e).__name__ + (": " + m if m else "") + "\n")
    sys.stderr.flush()


_repl_acc = ""
_repl_block = False
while True:
    sys.stdout.write("... " if _repl_acc else ">>> ")
    sys.stdout.flush()
    _repl_line = sys.stdin.readline()
    if _repl_line == "":
        break
    if not _repl_line.endswith("\n"):
        _repl_line += "\n"
    _repl_blank = _repl_line.strip() == ""
    if _repl_acc == "" and _repl_blank:
        continue
    if _repl_acc == "" and _repl_opens_block(_repl_line):
        _repl_block = True
    _repl_acc += _repl_line
    if _repl_block:
        if not _repl_blank:
            continue
    else:
        _repl_d, _repl_q = _repl_depth(_repl_acc)
        if _repl_d > 0 or _repl_q:
            continue
    _repl_src = _repl_acc
    _repl_acc = ""
    _repl_block = False
    try:
        try:
            if _repl_src.startswith("exec("):
                raise SyntaxError("statement")
            exec("_repl_r = (" + _repl_src.strip() + "\n)")
            _repl_isexpr = True
        except SyntaxError:
            _repl_isexpr = False
        if _repl_isexpr:
            if _repl_r is not None:
                print(repr(_repl_r))
        else:
            exec(_repl_src)
    except SystemExit:
        break
    except BaseException as _repl_e:
        _repl_report(_repl_e)
    sys.stdout.flush()
if _repl_acc.strip():
    try:
        exec(_repl_acc)
    except BaseException as _repl_e:
        _repl_report(_repl_e)
print()
