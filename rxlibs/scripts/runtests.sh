#!/bin/sh
# Run all rxnumpy tests; compare with CPython+numpy generated *.expected
HERE=$(cd "$(dirname "$0")/.." && pwd)
fail=0
for t in "$HERE"/rx*/tests/t_*.crexx; do
  T=$(dirname "$t"); python3 "$T/gen_expected.py" || exit 1
  n=$(basename "$t" .crexx)
  "$HERE/scripts/run.sh" "$t" 2>&1 | grep -v "rxc warning" > "$HERE/build/$n.out"
  if diff -u "$T/$n.expected" "$HERE/build/$n.out" > "$HERE/build/$n.diff"; then echo "PASS $n"
  else echo "FAIL $n"; cat "$HERE/build/$n.diff" | head -20; fail=1; fi
done
exit $fail
