# Python subset interpreter in CREXX

A Python 3 subset interpreter written in CREXX (Level B). It follows `../SPEC.md`
and passes all eleven tests in `../tests` (including the `numpy` subset).

    ./py script.py [args]     run a script
    ./py -c "print(1+2)"      run a command string
    ./py                      read the program from stdin

Set `CREXX` to the crexx driver if it is not on `PATH`. The first run compiles
`py.crexx` to bytecode (about two minutes); later runs start in about 1.2 s.

## Layout

- `src/*.crexx`: sources, concatenated by `build.py` into `py.crexx` (generated, checked in).
  a_head (startup, `init`, `boot`), b_lex, c_parse (nodes and scope resolution),
  d_obj (objects, classes, dicts), e_big (big integers, float repr), f_ops (arithmetic, repr/str),
  g_call, g_item, g_eval (tree evaluator), g_gen (generators), h_builtins, h_meth, h_mod, i_fmt,
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

## Known gaps

- numpy: no `np.random`, `np.linalg`, structured/complex/string dtypes; `reshape`, `T`, strided slices and
  fancy indexing return copies; integer arrays wrap at 64 bits; `-0.0` prints as `0.0`.
- `yield` inside `with` is unsupported.
- Dict views are lists; sets keep insertion order; `len` counts codepoints.
- No `re` or `json`; `random` does not reproduce CPython's sequence; `functools.partial` binds one argument; `time.time` is approximate.
- Recursion limit is about 900 frames.

## CREXX pitfalls found

`main`, `self`, `dummy` are reserved; plain-variable arguments are passed by reference (pass `x + 0`
to copy); the literal `1.7976931348623157e308` behaves as infinity (compare via `x - x`);
orphan `rxvme` processes can hold the driver lock after a timeout.
