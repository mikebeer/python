# py - a Python 3 subset interpreter in strict C89

One amalgamated source file (`py_all.c`, generated from `src/*.c` by
`tools/amalg.sh` and checked in). No dependencies beyond the C library and libm.

## Build and run

    make            # cc -std=c89 -pedantic -Wall -Wextra -O2 -o py py_all.c -lm
    make posix      # same with -DPY_POSIX (isatty prompt detection, clock)
    make test       # runs ../tests/run_tests.sh ./py
    make bench      # sh bench/run.sh, compares against python3
    ./py file.py args...      # sys.argv = [file.py, args...]
    ./py -c 'print(1+2)'
    ./py                      # REPL on stdin (-i forces prompts)

Uncaught exceptions print a traceback and exit with status 1.

## Layout

    src/p0_base.c   types, tagged values, GC, strings, lists, dicts, sets
    src/p1_num.c    bigints (base 10000), float repr (shortest round-trip)
    src/p2_lex.c    lexer (indent/dedent, f-strings, escapes)
    src/p3_parse.c  recursive-descent parser, AST, comprehension desugaring
    src/p4_res.c    scope resolution (local/cell/free/global/class ns)
    src/p6_obj.c    attributes, equality/hash/compare, slicing, operators
    src/p5_eval.c   evaluator, calls, generators, classes, exceptions
    src/p7_fmt.c    format mini-language, % formatting, str.format, f-strings
    src/p7_bi.c     builtins and methods of str/list/dict/set/tuple/int/float
    src/p7_np.c     numpy subset: native ndarray, ufuncs, reductions, indexing, printing
    src/p7_mod.c    math, random, sys, time; exception hierarchy
    src/p8_main.c   REPL and command line
    bench/          fib, loop, collections, strings, numpy_bench + run.sh

## Portability rules

No `//` comments, no mixed declarations, no `long long`, no stdint.h, no VLAs;
all functions static except `main`; character classes use literal sets
(EBCDIC-safe); POSIX only under `-DPY_POSIX`; lines <= ~100 columns.

## Design notes

- 16-byte tagged `Value`; small ints are `long`, larger ones are bigints.
- Mark-sweep GC with explicit gray stack, run only at safe points (call entry,
  loop iterations). `-DPY_GC_STRESS` collects at every safe point.
- Generators have no separate C stack: each compound statement keeps resumable
  state slots in the generator and the executor re-enters along the saved path.
- Exceptions use setjmp/longjmp; recursion limit 1000 raises RecursionError.
- Dicts are insertion-ordered; floats print with the shortest repr that
  round-trips; `sum` uses compensated summation like CPython 3.12.

## Known gaps

Strings are bytes (len/index count UTF-8 bytes, case ops ASCII only); single
inheritance; `x = yield`, send/throw unsupported (yield is statement-level);
no generator finalization; dict views are list snapshots; sets iterate in
insertion order; `& | ^` on huge ints unsupported; no bytes, walrus, match,
async or file imports; modules limited to math/random/sys/time/numpy; random does
not reproduce CPython's sequence.

## numpy subset (src/p7_np.c)

`import numpy as np` gives a native `ndarray` (bool/int64/float64, up to 8 dims, row-major,
heap type `T_ND`) with broadcasting arithmetic and comparisons, `@`, indexing (ints, slices,
tuples, boolean masks, integer lists) and element/slice assignment, reductions with `axis`,
sort/unique/where/concatenate/stack, `dot`/`matmul` (1-d and 2-d), ufuncs, and numpy's
`print`/`repr` layout (line wrapping at 75 columns, `...` summaries above 1000 elements,
scientific notation rules). Float sums use numpy's pairwise summation, so they match bit
for bit. Gaps: slices, `reshape`, `T` and `b[0]` are copies, not views (`b[0][1] = x` does not
modify `b`); no `dtype` objects (`a.dtype` is a string, `np.float64` is the builtin `float`),
no float32/int32, no `np.random`/`np.linalg`, N-d `matmul` and multi-array fancy indexing
are unsupported, `a.round()` on reduction results works through a `float.round` method that
is installed on `import numpy`; float matmul does not use BLAS so it can differ in the last
digits from numpy.
