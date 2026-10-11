#!/bin/sh
# usage: sh bench/run.sh   (run from c/ after "make"); compares ./py with python3
cd "$(dirname "$0")" || exit 1
for f in fib loop collections strings; do
  s=$(date +%s.%N); ../py $f.py > /dev/null; e=$(date +%s.%N)
  ours=$(echo "$e - $s" | bc)
  if command -v python3 > /dev/null 2>&1; then
    s=$(date +%s.%N); python3 $f.py > /dev/null; e=$(date +%s.%N)
    ref=$(echo "$e - $s" | bc)
  else ref="n/a"; fi
  echo "$f: py=${ours}s python3=${ref}s"
done
