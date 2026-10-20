#!/usr/bin/env python3
"""Print the CREXX-facing API of a plugin by scanning its P(...) registrations."""
import re, sys, os
here = os.path.dirname(os.path.abspath(__file__))
name = sys.argv[1]
src = open(os.path.join(here, '..', name, 'plugin', name + '.c')).read()
for m in re.finditer(r'P\(\w+,\s*"(\w+)",\s*"(\.\w+)",\s*((?:"[^"]*"|\w+)(?:\s*)?)\)', src):
    fn, ret, args = m.groups()
    a = args.strip()
    if a == 'A1': a = '"a = .binary"'
    if a == 'A2': a = '"a = .binary, b = .binary"'
    print("| `%s..%s(%s)` | `%s` |" % (name, fn, a.strip('"'), ret))
