#!/bin/sh
# usage: tests/run_tests.sh <interpreter command...>   e.g.  tests/run_tests.sh ./c/py
# runs every tests/NN_*.py and compares stdout with the .out file (made with CPython 3).
# VERBOSE=1 shows a diff for failures.  Names listed in $SKIP (space separated) are skipped.
DIR=$(cd "$(dirname "$0")" && pwd)
pass=0; fail=0; skip=0
for f in "$DIR"/*.py; do
  n=$(basename "$f" .py)
  case " $SKIP " in *" $n "*) echo "skip $n"; skip=$((skip+1)); continue;; esac
  out=$("$@" "$f" 2>/dev/null)
  if [ "$out" = "$(cat "$DIR/$n.out")" ]; then echo "ok   $n"; pass=$((pass+1))
  else echo "FAIL $n"; fail=$((fail+1))
    [ -n "$VERBOSE" ] && printf '%s\n' "$out" | diff - "$DIR/$n.out" | head -${VERBOSE_LINES:-10}
  fi
done
echo "passed $pass, failed $fail, skipped $skip"
[ $fail -eq 0 ]
