# Python subset interpreter in CREXX

A Python 3 subset interpreter written in CREXX (Level B). It follows `../SPEC.md`
and passes the tests in `../tests` (stage 1 and the language / import / linalg stage 2 tests).

    ./py script.py [args]     run a script
    ./py -c "print(1+2)"      run a command string
    ./py                      read the program from stdin

Set `CREXX` to the crexx driver if it is not on `PATH`. The first run compiles
`py.crexx` to bytecode (about two minutes); later runs start in about 2 s with a stock VM, 0.4 s with the patched one (see `STARTUP.md`, `CREXX_VM`).

## Layout

- `src/*.crexx`: sources, concatenated by `build.py` into `py.crexx` (generated, checked in).
  a_head (startup, `init`, `boot`), b_lex, c_parse (nodes and scope resolution),
  d_obj (objects, classes, dicts), e_big (big integers, float repr), f_ops (arithmetic, repr/str),
  g_call, g_item, g_eval (tree evaluator), g_gen (generators), h_builtins, h_meth, h_mod,
  h_imp (modules and imports), h_ns (globals/locals/vars/dir/eval/exec, type() with 3 arguments), i_fmt,
  j_np and k_np2 (the built-in `numpy` subset: native ndarray objects of kind `K_ARRAY`, broadcasting,
  reductions, indexing, numpy style array printing).
- `src/consts.txt`, `classes.txt`, `builtins.txt`: tables that `build.py` turns into symbolic constants.
- `build.py`: `python3 build.py` regenerates `py.crexx`.
- `py`: launcher.

## Design

Everything is integer arrays. Values are tagged integers (small int `2n+1`, heap object `2*index`);
the parser produces node arrays and the evaluator walks integer node ids. Frames live in a
shared slot array. Generators keep an explicit resumable statement stack. Big integers use
decimal limbs; floats print with the shortest round-trip repr.

## numpy

`import numpy as np` gives a native ndarray (int64, float64, bool; N-d, row-major). The shape lives in
`sl` (raw integers), the data in `sl` (int, bool) or in the float store `npf`; `ob_c` holds the dtype,
`ob_n` the size, `ob_x` the number of dimensions. Basic indexing along the first axis (`a[i]`, `a[i:j]`
with step 1) returns views; everything else copies. Float arrays print like numpy (common precision,
exponent form, line wrapping at 75 columns, `...` summary above 1000 elements) and float sums use
numpy's pairwise summation so results agree to the last digit.

## Modules and libraries

`import` of Python files works as in CPython: packages with `__init__.py`, `import a.b`, `from a.b import c as d`,
`from x import *` (honours `__all__`), relative imports (`from . import m`, `from ..p import q`), `sys.modules`
caching, `__name__` (`'__main__'` for the script), circular imports. Search path: the script directory, the
`PYLIB` environment variable, the directory passed by the launcher (`pylib` next to `py`), then
`/home/claude/python/pylib`. Native modules (`math random sys time os numpy itertools functools collections copy
string abc typing warnings`) win; a pylib file with the same name only supplies the names the native module lacks
(`hybrid_get`), e.g. `pylib/numpy/linalg.py`, `pylib/math.py` (erf, gamma, ...). The list `shadow` in `boot`
(`a_head.crexx`) names native attributes that the pylib file replaces when it defines them
(`functools.wraps`, `lru_cache`, `cache`).

Language features added for the libraries: multiple inheritance (C3 linearisation, `super()` along the MRO),
`__getattr__ __setattr__ __delattr__`, `object.__setattr__`, reflected / in-place / unary operators, rich comparison
results of any type, `__abs__ __round__ __format__ __call__ __enter__ __exit__`, live `dict.keys()/values()/items()` views,
`slice` objects, function attributes, `x = yield`, `send/throw/close`, generator return values (`StopIteration.value`,
`yield from` results), `yield` inside `with`, `globals() locals() vars() dir() eval() exec() __import__()`,
`type(name, bases, dict)`, `print(file=obj)`, nested same-type quotes in f-strings, float `sum` with compensation,
`-0.0`.

`eval`/`exec` run the text in a temporary module whose names are copied from and back to the given namespace dict
(`globals()` and `locals()` return snapshots; `exec` at module level writes new names back to the module).

## Known gaps

- numpy: no structured/complex/string/object dtypes; `reshape`, `T`, strided slices and fancy indexing return
  copies; integer arrays wrap at 64 bits. `numpy.linalg`, `numpy.random` and many helper functions are pure
  Python in `../pylib/numpy/`.
- Subclassing built-in types (`class X(dict)`, `tuple`, `list`) and `__new__` are not supported; metaclasses are ignored.
- `str` holds codepoints (the C interpreter uses byte strings); tracebacks always report line 1.
- `globals()` / `locals()` are snapshots; `exec` inside a function does not write back to its locals.
- Sets keep insertion order; `random` does not reproduce CPython's sequence; `time.time` is approximate.
- Float literals near the end of the range and denormals print with fewer digits.
- Recursion limit is about 900 frames; a stock VM pays about 2 s per run linking the 2.7 MB bytecode (see `STARTUP.md`).

## CREXX pitfalls found

`main`, `self`, `dummy` are reserved; plain-variable arguments are passed by reference (pass `x + 0`
to copy); the literal `1.7976931348623157e308` behaves as infinity (compare via `x - x`);
orphan `rxvme` processes can hold the driver lock after a timeout.
