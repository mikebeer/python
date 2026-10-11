#!/bin/sh
# usage: sh bench/run.sh   (run from c/ after "make"); compares ./py with python3
cd "$(dirname "$0")" || exit 1
for f in fib loop collections strings numpy_bench; do
  s=$(date +%s.%N); ../py $f.py > /dev/null; e=$(date +%s.%N)
  ours=$(echo "$e - $s" | bc)
  if command -v python3 > /dev/null 2>&1; then
    s=$(date +%s.%N); python3 -I $f.py > /dev/null 2>&1; e=$(date +%s.%N)
    ref=$(echo "$e - $s" | bc)
  else ref="n/a"; fi
  echo "$f: py=${ours}s python3=${ref}s"
done

# native re/json vs the pure-Python pylib versions (copied next to the scripts as pyre.py / pyjson.py)
cp ../../pylib/re.py pyre.py; cp ../../pylib/json.py pyjson.py
for f in re_bench json_bench; do
  s=$(date +%s.%N); ../py $f.py > /dev/null; e=$(date +%s.%N); nat=$(echo "$e - $s" | bc)
  s=$(date +%s.%N); ../py $f.py pure > /dev/null; e=$(date +%s.%N); pure=$(echo "$e - $s" | bc)
  if command -v python3 > /dev/null 2>&1; then
    s=$(date +%s.%N); python3 -I $f.py > /dev/null 2>&1; e=$(date +%s.%N); ref=$(echo "$e - $s" | bc)
  else ref="n/a"; fi
  echo "$f: native=${nat}s pylib=${pure}s python3=${ref}s"
done
rm -f pyre.py pyjson.py
