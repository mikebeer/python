#!/bin/sh
# Inline the parts of src/py.c into one self-contained C file (stdout).
# Usage: sh tools/amalg.sh > py_all.c           (library files are read from disk)
#        sh tools/amalg.sh embed > py_embed_all.c   (pylib/ compiled in; needs
#                                  src/pylib_embed.c from tools/embed_pylib.py;
#                                  build it with -DPY_EMBED_LIBS)
mode="$1"
cd "$(dirname "$0")/../src" || exit 1
awk -v mode="$mode" '
function emit_line(line) {
  if (line == "#include \"pylib_embed.c\"") {
    if (mode == "embed") {
      print "/* ===== pylib_embed.c ===== */";
      while ((getline l2 < "pylib_embed.c") > 0) print l2;
      close("pylib_embed.c");
    } else {
      print "#error \"PY_EMBED_LIBS needs py_embed_all.c (sh tools/amalg.sh embed)\"";
    }
  } else print line;
}
/^#include "p[0-9_a-z]+\.[ch]"$/ {
  f = $2; gsub(/"/, "", f);
  print "/* ===== " f " ===== */";
  while ((getline line < f) > 0) emit_line(line);
  close(f);
  next
}
{ print }' py.c
