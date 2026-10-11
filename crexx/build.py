#!/usr/bin/env python3
"""Assemble src/*.crexx into py.crexx (symbolic constants, class / built-in tables, namespace list)."""
import re, os, sys
root = os.path.dirname(os.path.abspath(__file__))
d = os.path.join(root, 'src')
order = ['a_head', 'b_lex', 'c_parse', 'd_obj', 'e_big', 'f_ops', 'g_call', 'g_item', 'g_eval', 'g_gen',
         'h_builtins', 'h_meth', 'h_mod', 'h_imp', 'h_ns', 'i_fmt', 'j_np', 'k_np2']
src = ''.join(open(os.path.join(d, f + '.crexx')).read() + '\n' for f in order)

consts = {}
for line in open(os.path.join(d, 'consts.txt')):
    line = line.split('#')[0].strip()
    if line:
        k, v = line.split()
        consts[k] = v

# ---- classes
cls_names = []
cls_rows = []
for line in open(os.path.join(d, 'classes.txt')):
    line = line.split('#')[0].strip()
    if not line:
        continue
    nm, base, flags, vis = line.split()
    cname = nm.upper()
    if ':' in nm:
        nm, cname = nm.split(':')
    cls_names.append(nm)
    cls_rows.append((nm, base, flags, vis))
    consts['C_' + cname] = str(len(cls_names))
clsboot = []
for nm, base, flags, vis in cls_rows:
    b = '0' if base == '-' else str(cls_names.index(base) + 1)
    clsboot.append('%s/%s/%s/%s' % (nm, b, flags, vis))

# ---- built-in functions
bnames = []
aliases = []
for line in open(os.path.join(d, 'builtins.txt')):
    line = line.split('#')[0].strip()
    if not line:
        continue
    if '=' in line:
        aliases.append(line)
    else:
        bnames.append(line)
        consts['BI_' + line.upper().replace('.', '_')] = str(len(bnames))
alias_out = []
for a in aliases:
    nm, tgt = a.split('=')
    alias_out.append('%s=%d' % (nm, bnames.index(tgt) + 1))

# ---- name ids (M_...) and node types (N_...)
mset = sorted(set(re.findall(r'\bM_[A-Z0-9_]+\b', src)))
names = [m[2:].lower() for m in mset]
for i, m in enumerate(mset):
    consts[m] = str(i + 1)
nset = sorted(set(re.findall(r'\bN_[A-Z0-9_]+\b', src)))
for i, n in enumerate(nset):
    consts[n] = str(i + 1)

def chunked(var, words, width=150):
    out = []
    cur = ''
    for w in words:
        if len(cur) + len(w) + 1 > width:
            out.append(cur)
            cur = ''
        cur += (' ' if cur else '') + w
    out.append(cur)
    lines = ['  %s = "%s"' % (var, out[0])]
    for c in out[1:]:
        lines.append('  %s = %s || " %s"' % (var, var, c))
    return '\n'.join(lines)

src = src.replace('  nameboot = "@@NAMES@@"', chunked('nameboot', names))
src = src.replace('  clsboot = "@@CLASSES@@"', chunked('clsboot', clsboot))
src = src.replace('  bboot = "@@BUILTINS@@"', chunked('bboot', bnames))
src = src.replace('  aliasboot = "@@ALIASES@@"', chunked('aliasboot', alias_out))

# ---- namespace expose list (every variable assigned in init)
ibody = src[src.index('init: procedure'):]
ibody = ibody[:ibody.index('\n  return\n')]
gnames = []
for ln in ibody.splitlines():
    mm = re.match(r'^  ([a-z][A-Za-z0-9_]*) = ', ln)
    if mm and mm.group(1) not in gnames:
        gnames.append(mm.group(1))
lines = []
cur = 'namespace py expose'
for n in gnames:
    if len(cur) + len(n) > 74:
        lines.append(cur + ',')
        cur = '   ' + n
    else:
        cur += ' ' + n
lines.append(cur)
src = src.replace('@@EXPOSE@@', '\n'.join(lines))

# ---- constants (outside string literals and comments)
tokpat = re.compile(r'/\*.*?\*/|"[^"\n]*"|\'[^\'\n]*\'|[A-Za-z_][A-Za-z0-9_]*', re.S)
cpat = re.compile(r'^(?:(?:T|K|IT|U|SK|O|CMP|N|C|BI|M)_[A-Z0-9_]+|NONE_OOP|TRUE_OOP|FALSE_OOP|ELLIPSIS_OOP|NOTIMPL_OOP|SMALLMAX|BIGOOP)$')
out = []
pos = 0
bad = set()
for mo in tokpat.finditer(src):
    out.append(src[pos:mo.start()])
    tok = mo.group(0)
    pos = mo.end()
    prev = src[mo.start() - 1] if mo.start() > 0 else ' '
    if tok[0] in '/"\'' or prev == '.' or not cpat.match(tok):
        out.append(tok)
        continue
    if tok in consts:
        out.append(consts[tok])
    else:
        bad.add(tok)
        out.append(tok)
out.append(src[pos:])
src = ''.join(out)
if bad:
    sys.exit('unknown constants: ' + ' '.join(sorted(bad)))
open(os.path.join(root, 'py.crexx'), 'w').write(src)
print(len(src.splitlines()), 'lines,', len(gnames), 'globals,', len(names), 'names,', len(bnames), 'builtins')
