# py - a Python 3 subset interpreter in strict C89

One amalgamated source file (`py_all.c`, generated from `src/*.c` by
`tools/amalg.sh` and checked in). No dependencies beyond the C library and libm.

## Build and run

    make            # cc -std=c89 -pedantic -Wall -Wextra -O2 -DPY_LIBDIR=... -o py py_all.c -lm
    make LIBDIR=/path/to/pylib     # compile-time fallback library dir (default ../pylib, absolute)
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
    src/p7_la.c     numpy.linalg (LU, Jacobi eigen/SVD, Householder QR, lstsq, norm ...)
    src/p7_np2.c    more numpy: diff/gradient/percentile/histogram/cov/polyfit/convolve/...
    src/p7_rnd.c    numpy.random (xoshiro256**; legacy functions, default_rng, RandomState)
    src/p7_imp.c    import system (files, packages, relative imports), exec/eval/globals, os
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

## Imports

`import x`, `import a.b [as c]`, `from a.b import c, d as e`, `from x import *` (honours
`__all__`, skips `_names`), packages (`__init__.py`), relative imports (`from . import m`,
`from .m import f`, `from .. import n`), `sys.modules` caching, `__name__` (`'__main__'` for the
script), `__file__`, `__path__`, CPython-style circular-import tolerance (a module is in
`sys.modules` before its body runs). Native modules (math random sys time os numpy) win
over files. Every module has its own global namespace (globals are name-prefixed `pkg.mod.x`
in one table; builtins live under plain names).

Search path (`sys.path`): directory of the script, entries of the `PYLIB` environment variable
(`:` separated), `<dir of executable>/pylib`, `<dir of executable>/../pylib`, then the
compile-time `PY_LIBDIR` (`make LIBDIR=...`, default the absolute `../pylib` of the source tree,
which is where the shared pure-Python libraries live). Only stdio `fopen` is used; path
joining is one function (`path_join` in p7_imp.c).

`eval`, `exec`, `globals()`, `locals()`, `vars()`, `dir()`, `__import__` are available.

## Language notes (Stage 2)

Multiple inheritance with C3 MRO (`__mro__`, `super()` follows the MRO), `type(name, bases,
dict)`, `__slots__` ignored, `__getattr__`/`__setattr__`/`__new__`, reflected/in-place/comparison
dunders with `NotImplemented`, `__getitem__/__setitem__/__delitem__` with slice objects,
`__iter__/__next__`, `__contains__ __len__ __bool__ __call__ __format__ __enter__/__exit__`,
`property` with setter/deleter, `staticmethod`, `classmethod`, subclassing `list/dict/set/tuple/
str/Exception`, `x = yield`, `send`/`throw`/`close`, `StopIteration.value`, walrus `:=`
(also in comprehensions), `Ellipsis`, live dict views, minimal `bytes`, function attributes,
nested-quote f-strings and `{x=}`, `print(file=)`, `%` with mapping keys, `dict | dict`.
`io.StringIO` and `warnings` are small pure-Python modules in `../pylib`.

## Known gaps

Strings are bytes (len/index count UTF-8 bytes, case ops ASCII only); sets iterate in
insertion order (their repr differs from CPython for mixed hash orders); `& | ^` on huge ints
unsupported; no match statement, async, complex numbers; `r = yield from g()` (as an
expression) and chained assignment with `yield` are not supported; no generator finalization
on garbage collection; random does not reproduce CPython's sequence (nor numpy's);
`math.erf/erfc/gamma/lgamma` are within about 1e-15 of libm, not bit-identical.

## numpy subset (src/p7_np.c)

`import numpy as np` gives a native `ndarray` (bool/int64/float64, up to 8 dims, row-major,
heap type `T_ND`) with broadcasting arithmetic and comparisons, `@`, indexing (ints, slices,
tuples, boolean masks, integer lists) and element/slice assignment, reductions with `axis`,
sort/unique/where/concatenate/stack, `dot`/`matmul` (1-d and 2-d), ufuncs, and numpy's
`print`/`repr` layout (line wrapping at 75 columns, `...` summaries above 1000 elements,
scientific notation rules). Float sums use numpy's pairwise summation, so they match bit
for bit. Gaps: slices, `reshape`, `T` and `b[0]` are copies, not views (`b[0][1] = x` does not
modify `b`); no `dtype` objects (`a.dtype` is a string, `np.float64` is the builtin `float`),
no float32/int32 (the names alias float64/int64), N-d `matmul` and multi-array fancy indexing
are unsupported, `a.round()` on reduction results works through a `float.round` method that
is installed on `import numpy`; float matmul does not use BLAS so it can differ in the last
digits from numpy.

### numpy.linalg and numpy.random (src/p7_la.c, src/p7_rnd.c)

`numpy.linalg`: `inv det slogdet solve cholesky eigvalsh eigh eigvals eig svd matrix_rank qr
lstsq pinv norm cond matrix_power`, `LinAlgError`. Real double matrices only (2-d, no stacked
matrices). `eigvals`/`eig` raise `NotImplementedError` for complex eigenvalues and return them in
an unspecified order (as LAPACK does); eigenvector signs are normalised, not LAPACK's. `svd`
uses one-sided Jacobi (`full_matrices`, `compute_uv` supported); `qr` follows the LAPACK
Householder sign convention (`reduced`, `complete`, `r`).

`numpy.random`: `seed rand randn random random_sample randint choice permutation shuffle normal
uniform standard_normal exponential poisson binomial`, `default_rng(seed)` and `RandomState(seed)`
objects with the same methods (`integers`, `random`, ...). The generator is xoshiro256** seeded with
splitmix64, so sequences are reproducible per seed but do not match numpy's.

More numpy functions (p7_np2.c): `isclose arctan2 hypot linspace(retstep) meshgrid outer inner
vdot cross diff ediff1d gradient percentile quantile median(axis) histogram bincount cov
corrcoef searchsorted nan_to_num nansum nanmean nanstd nanvar nanmax nanmin tril triu tri kron
polyfit polyval convolve trapz flip roll tile repeat squeeze expand_dims swapaxes argwhere isin
append take logical_* exp2 expm1 log1p cbrt degrees radians ...`.
