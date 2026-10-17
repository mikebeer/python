# Python subset interpreter in CREXX

A Python 3 subset interpreter written in CREXX (Level B). It follows `../SPEC.md`
and passes the tests in `../tests` (stage 1 and the language / import / linalg stage 2 tests).

    ./py script.py [args]     run a script
    ./py -c "print(1+2)"      run a command string
    ./py                      read the program from stdin
    ./py -i                   interactive loop on stdin: prompts `>>> ` / `... `, expression values echoed,
                              tracebacks on stderr (same protocol as `c/py -i`; the loop is `pylib/_repl.py`)

Set `CREXX` to the crexx driver if it is not on `PATH`. The first run compiles
`py.crexx` to bytecode (about 5 to 7 minutes); later runs start in about 2 s with a stock VM, 0.45 s with the patched one (see `STARTUP.md`, `CREXX_VM`).

## Layout

- `src/*.crexx`: sources, concatenated by `build.py` into `py.crexx` (generated, checked in).
  a_head (startup, `init`, `boot`), b_lex, c_parse (nodes and scope resolution),
  d_obj (objects, classes, dicts), e_big (big integers, float repr, float parsing), e_big2 (bit operations, modular pow, exact int/float conversion and comparison, hashes, correctly rounded division, exact fsum), f_ops (arithmetic, repr/str),
  e_rng (ranges of any size), e_misc (str.translate/maketrans, isprintable, slice.indices), g_call, g_item, g_eval (tree evaluator), g_gen (generators), h_builtins, h_meth, h_mod,
  h_imp (modules and imports), h_ns (globals/locals/vars/dir/eval/exec, type() with 3 arguments), i_fmt,
  j_np and k_np2 (the built-in `numpy` subset: native ndarray objects of kind `K_ARRAY`, broadcasting,
  reductions, indexing, numpy style array printing), l_bytes (bytes / bytearray, encodings), l_cplx (complex,
  bridge to `pylib/numpy/_cplx.py`), l_sub (subclasses of built-in types, `__new__`), l_sock (`_sock` primitive).
  n_match (`match` patterns, `ExceptionGroup`, `except*`, `__annotations__`).
- `src/consts.txt`, `classes.txt`, `builtins.txt`: tables that `build.py` turns into symbolic constants.
- `build.py`: `python3 build.py` regenerates `py.crexx`.
- `py`: launcher.

`sys.executable` is the absolute path of the `py` launcher (exported as `PY_EXECUTABLE`, read by `pylib/sys.py`).
`dir()` of built-in values (`str list tuple dict set int float bytes complex range`) lists the public method names.
An `exec()` inside exec'd code shares the outer namespace, so `exec("exec('x = 1')")` defines `x` (the `-i` loop relies on it).
Limit of `-i` and `exec()`: every exec'd chunk is parsed as its own module, so a function defined in one chunk reads
globals from that chunk's frame and does not see later rebinding of a global made by another chunk (CPython shares one
globals dict). Simple REPL use is fine; the Workbench probes this and replays history instead of keeping a live session.

## Design

Everything is integer arrays. Values are tagged integers (small int `2n+1`, heap object `2*index`);
the parser produces node arrays and the evaluator walks integer node ids. Frames live in a
shared slot array. Generators keep an explicit resumable statement stack. Big integers use
decimal strings (bit operations, shifts and modular pow convert to 24-bit limbs); `range` bounds of any size,
CPython-compatible modular hashes (`hash(1e10) == hash(10**10)`) and exact int/float comparison are implemented; floats print with the shortest round-trip repr.

## numpy

`import numpy as np` gives a native ndarray (int64, float64, bool; N-d, row-major). The shape lives in
`sl` (raw integers), the data in `sl` (int, bool) or in the float store `npf`; `ob_c` holds the dtype,
`ob_n` the size, `ob_x` the number of dimensions. Basic indexing along the first axis (`a[i]`, `a[i:j]`
with step 1) returns views; everything else copies. Float arrays print like numpy (common precision,
exponent form, line wrapping at 75 columns, `...` summary above 1000 elements) and float sums use
numpy's pairwise summation so results agree to the last digit.

## bytes, complex, subclassing, sockets

- `bytes` / `bytearray` (kind `K_BYTES`, one character per byte in `ob_s`, `ob_y = 1` for bytearray): literals (`b"..."`,
  `rb`, adjacent literals), indexing gives ints, slicing, `+ * in < == hash`, `repr`, iteration, `bytes(n)`, `bytes([..])`,
  `bytes(str, enc)`, `str.encode` / `bytes.decode` (utf-8, ascii, latin-1; `strict replace ignore backslashreplace`),
  `hex fromhex find index count split rsplit join replace strip startswith endswith partition ...`, mutation for
  bytearray (`append extend insert pop remove`, item and slice assignment), `int.from_bytes` / `to_bytes`.
  `pylib/struct.py` (pure Python, also fine on the C interpreter) implements `pack unpack calcsize pack_into
  unpack_from iter_unpack Struct` for `x c b B ? h H i I l L q Q n N f d s p` with `@ = < > !`.
- `complex` (kind `K_COMPLEX`, parts in `ob_f` / `ob_g`): `1j` literals, `complex()` incl. strings, all arithmetic
  (CPython's division / power algorithms), `abs pow sum == hash format repr str`, `.real .imag .conjugate()`,
  `ZeroDivisionError: complex division by zero`. `pylib/cmath.py` (pure Python, follows CPython's cmathmodule.c,
  works on both interpreters; the C interpreter's native `cmath` wins there).
- numpy `complex128`: an array with complex elements (`np.array([1+2j, ..])`, `dtype=complex`, `astype(complex)`,
  array op complex scalar, complex function arguments) is a `_CArr` instance defined in `pylib/numpy/_cplx.py`
  (two float64 ndarrays `re`/`im`); the evaluator hands such calls to it (`cx_call1`, `cx_dispatch` in `l_cplx.crexx`).
  Supports arithmetic with broadcasting, `.real .imag .conj() .T`, `np.abs angle sqrt exp log sin cos ... sum mean
  prod dot`, indexing / assignment / iteration / `tolist`, numpy style printing (`[1.+2.j 3.-1.j]`, exponent form,
  wrapping). `np.roots`, `np.linalg.eigvals` / `eig` return complex results when needed (2x2 blocks ordered like
  LAPACK's `dlanv2`). Also `cumsum sort argsort unique diag trace count_nonzero allclose array_equal`, `isinstance(a, np.ndarray)` and `a.dtype == complex`. Not supported: complex `linspace` and most other helpers (TypeError).
  `a.dtype` is a `str` subclass (`pylib/numpy/_dt.py`) that also compares equal to `float` / `int` / `complex` / `bool` and to dtype names.
  `np.fft` is a lazy proxy that loads `numpy.fft` (`pylib/numpy/fft.py`), `numpy._overlay.fft` on the C interpreter.
- `frozenset` is its own class (not a subclass of `set`): a `K_SET` object whose `ob_c` is `C_FROZENSET` (or a subclass of it).
  No mutating methods (`AttributeError`), `hash()` follows CPython's `frozenset_hash` (`fs_hash`, `_shuffle_bits` on 64 bits, so
  int-only sets hash like CPython), set operators and `union` etc. return the type of the left operand (`frozenset | set` is a
  `frozenset`), `copy()` returns the same object, `|=` rebinds. A plain `set`, `list`, `dict` and `bytearray` are unhashable: as a set element, dict key, tuple
  item in a key, or argument of `dict.get/setdefault/pop/fromkeys`, `hash()` and `frozenset([..])` they raise
  `TypeError: unhashable type: 'set'` (class name of a subclass unless it defines `__hash__`); `hashkey` (`d_obj`)
  raises and `dict_put` ignores the key `"?"` so a failed insertion leaves nothing behind. A set probe of
  `in`, `discard` and `remove` is converted to a frozenset (`hashkeyp`: `{1} in {frozenset({1})}` is True).
  Dicts and sets are keyed by that text key, not by hash values.
- Subclassing built-in types: `class X(list | dict | set | frozenset | tuple | str | int | float | bytes | bytearray |
  Exception | object)`. An instance is a native object (`K_LIST`, ...) whose `ob_c` holds the class id; instance
  attributes live in a `K_INST` holder (stem `nat_at`). Operators, `len`, `in`, iteration, `x[i]`, `repr`, `str`,
  `hash`, `format`, comparison, `__missing__` and `print` look for an override first (`nsov` / `ovr`); calling the built-in
  implementation explicitly (`dict.__setitem__(self, k, v)`, `super().append(v)`, `list.append(self, v)`) skips the
  override once (`nat_skip`). `type.__new__` protocol: calling a class runs `__new__` then `__init__` when the result is an
  instance; `object.__new__(cls)`, `super().__new__(cls, ...)`, `tuple.__new__(cls, it)`, `int.__new__`, `float.__new__`,
  `str.__new__`, `frozenset.__new__`. Unbound built-in methods are values
  (`push = list.append`).
- `socket` (`crexx/pylib/socket.py`) and `select` (`crexx/pylib/select.py`): blocking IPv4 TCP over the `rxsocket`
  library through the internal builtin `_sock(op, handle, a, b)` (`l_sock.crexx`): `socket bind listen accept connect
  connect_ex send sendall recv settimeout setblocking setsockopt getsockname getpeername shutdown close makefile`,
  `create_connection create_server socketpair gethostname gethostbyname inet_aton inet_ntoa htons ntohs htonl`,
  constants, `socket.error = OSError`, `socket.timeout = TimeoutError`, `ConnectionRefusedError` and friends,
  `OSError(errno, msg)` with `.errno`, `.strerror` and `[Errno N] msg`. No UDP / IPv6 / DNS; `select.select` polls the
  pending byte count; a listening socket is readable when a connection is waiting (it is accepted with a short
  timeout and kept for the next `accept()`). `time.sleep` really sleeps (`rxplatform..sleep`).

## Files, sys.path, subprocess

- Text files: `open(name)` / `open(name, "r", encoding=...)` reads the bytes and decodes them (utf-8 default, `ascii`,
  `latin-1`; `errors=` as for `bytes.decode`), so non-ASCII text round-trips through `write` / `read` / `print`,
  `str.encode` / `bytes.decode("utf-8")` and binary mode (test 73). Invalid UTF-8 raises `UnicodeDecodeError` as in CPython.
- `sys.path` is live: `sys.path.insert/append/remove` and assignment take effect for the next `import` (`sync_syspath`
  in `h_imp.crexx` re-reads the list before every module search; test 74).
- `subprocess` (test 71, 72): `pylib/subprocess.py` runs unchanged on top of `crexx/pylib/_proc.py`, which uses one
  primitive, the builtin `_sys(cmd)` (`l_sock.crexx`: `ADDRESS SYSTEM`, i.e. `sh -c`, returns the exit status). Streams
  go through files in a private directory under `$TMPDIR` (`/tmp`), so output is byte exact. `run/call/check_call/
  check_output/Popen(...).communicate()` work with `input`, `cwd`, `shell`, `timeout` (via `timeout(1)`), `stderr=STDOUT`.
  `Popen(stdin=PIPE, stdout=PIPE)` is live: a feeder loop in the child's shell picks up numbered chunk files that
  `p.stdin.write()` renames into place (latency about 30 ms), the child output file is polled by
  `p.stdout.read1(n, timeout)` / `p.read_nonblock(timeout)`; `kill` signals the child's process group
  (ADDRESS SYSTEM ends the command's own process group, so the supervisor runs under `setsid`). Needs a POSIX `sh`,
  `timeout` and `setsid`; each spawn costs a few milliseconds of shell start. The launcher `py` starts `rxvme` directly
  (next to the driver) because the driver holds an output lock on `py.crexx` for the whole run, so an interpreter started
  from a running script through the driver would wait for its parent forever.
- Workbench (`../../workbench/tests`, `PY=crexx/py WB_PY=crexx/py`): t_bundle, t_git, t_port, t_regina, t_repo, t_rexxmembers,
  t_screen, t_ui pass; t_adapters/t_live/t_scratch/t_cli/t_dogfood/t_api/t_http/t_plain/t_tui still fail (live session driver, see the report).

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
The launcher also puts `crexx/pylib` (CREXX-only modules: `socket`, `select`) first on the path.

Language features added for the libraries: multiple inheritance (C3 linearisation, `super()` along the MRO),
`__getattr__ __setattr__ __delattr__`, `object.__setattr__`, reflected / in-place / unary operators, rich comparison
results of any type, `__abs__ __round__ __format__ __call__ __enter__ __exit__`, live `dict.keys()/values()/items()` views,
`slice` objects, function attributes, `x = yield`, `send/throw/close`, generator return values (`StopIteration.value`,
`yield from` results), `yield` inside `with`, `globals() locals() vars() dir() eval() exec() __import__()`,
`type(name, bases, dict)`, `print(file=obj)`, nested same-type quotes in f-strings, float `sum` with compensation,
`-0.0`.

`eval`/`exec` run the text in a temporary module whose names are copied from and back to the given namespace dict
(`globals()` and `locals()` return snapshots; `exec` at module level writes new names back to the module).

## Newer syntax (Build 9)

- `match` / `case` (soft keyword: a line that starts with `match`, ends in `:` and is followed by an indented block). The parser
  rewrites it into `.mN = subject` plus an `if / elif` chain whose conditions are `N_PMATCH` nodes (pattern, subject temp) joined
  with `and` for the guard, so generators, `return`, `break` and `continue` in a case body need nothing special. Patterns are
  the nodes `N_PWILD N_PCAP N_PVAL N_PSING N_POR N_PAS N_PSEQ N_PSTAR N_PMAP N_PCLS N_PKW`, matched by `pat_match` (`n_match.crexx`):
  literals and dotted values (`==`), `None/True/False` (identity), capture, `_`, OR, `as`, sequences with `*rest` (list, tuple, range;
  not str / bytes), mappings with `**rest` (dicts), class patterns with `__match_args__` (dataclasses set it) and keyword
  sub-patterns (`int(n)`, `str() as s` match the subject itself), guards, nesting. A temp `.mN` and the captures are ordinary variables of the scope.
- `:=` (`N_WALRUS`): in a comprehension the target is bound in the nearest enclosing non-comprehension scope (the comprehension
  scope marks the name as `nonlocal`); allowed in `if` / `while` conditions, call arguments, subscripts, f-string fields.
- `except*` and `ExceptionGroup` / `BaseExceptionGroup` (classes without a real multiple inheritance: `BaseExceptionGroup` derives from
  `Exception`, so `except Exception` also catches a group of `BaseException`s). Attributes `message`, `exceptions`; `split` and
  `subgroup` take a type or tuple of types (no predicate); `derive`, notes and tracebacks do not exist. `exec_star` splits the
  raised group per handler (a plain exception is wrapped as `ExceptionGroup("", [e])`), merges what handlers raise with the
  unhandled rest. A `yield` inside an `except*` handler is not supported.
- Annotations: `x: T = v` at module or class level stores `__annotations__["x"]` (an annotation that fails to evaluate is skipped, `list[int]`
  evaluates to `list`); `f.__annotations__` is built on request from the parameter / return annotations in the defining frame.
  Annotations in functions are not evaluated. `/` in parameter lists marks positional-only parameters (`pr_po`; passing one by keyword is a `TypeError` unless the function has `**kwargs`). Star expressions in
  list / tuple / set / dict displays and calls worked before.

## Introspection (Build 10, `o_intro.crexx`, `pylib/_intro.py`)

- `f.__code__` (`co_name co_filename co_firstlineno co_argcount co_posonlyargcount co_kwonlyargcount co_varnames co_flags`; the
  object is a `_intro.code` instance built from the function record, `fd_ln` holds the `def` line), `__defaults__`, `__kwdefaults__`,
  `__globals__`; a bound method forwards unknown attributes to its function.
- Line tracking: `ex` stores the node line in `cs_ln[cdepth + 1]`; `callfunc` fills `cs_fd`, `cs_ln`, `cs_fb` per call depth
  (index 1 is module level). While an exception leaves a frame (`callfunc`) or is caught (`exec_try`) `tb_add` appends `fd:line;` to the
  hidden attribute `__tbraw__`; `e.__traceback__` (`exc_tb`) turns that record into `_intro.traceback` / `frame` objects once and caches them.
  Nodes get their line after parsing, so a statement spanning lines reports its last line.
- `sys.exc_info()` (from `exc_cur`), `sys._getframe(n)` (frame chain from `cs_*`, `f_locals` via `ns_scope_dict`), `raise X from Y`
  (`__cause__`, `__suppress_context__`), `__context__` set in `exec_try` when a handler raises, `Class.__new__(Class)`.
- `os.mkdir rmdir remove unlink rename listdir` (`os_fs` in `h_imp`, over `rxfs`), `os.path.normcase`.
- Tests 64 to 69 pass (Build 11/12). Only `34_hash64` stays in `tests/crexx_skip.txt`.
- Build 12: a metaclass `__call__` is honoured (`docall` for `K_CLASS` looks it up in `cl_meta`; `type.__call__(cls, ...)` is `bmeth_unbound` -> `instantiate`). Enum (pure Python in `pylib/enum.py`) accepts a custom `__new__` with tuple values; `IntEnum` derives from `int`. pandas time zones (`tz=`, `tz_localize`, `tz_convert`, `.dt.tz`) run over `pylib/zoneinfo` (UTC, fixed offsets and about 150 zones with hard-coded EU/US/AU/NZ rules; datetime has no `fold`).
- Text-mode `open()` reads the whole file with `bin_load` and turns CRLF into LF; a last line without a newline gets none appended (test 44). A generic alias as a base class (`class X(list[int])`) stands for its origin class (`exec_class`).

## Hash values

`hash()` follows CPython 3.13 for int, bool, big int, float, complex, None (4238894112), tuples (xxHash scheme,
`tup_xx` in `f_ops.crexx`: 64-bit unsigned arithmetic done on two 32-bit halves, products with 16-bit limbs, because the
VM multiply does not wrap) and frozensets. The tuple hash is only computed when `hash()` or a `__hash__` method asks for it;
dict and set lookups with tuple keys use the text key and are not slower. Hashes of str and bytes differ from CPython's
(randomized there), so a tuple containing a str hashes differently. The result of a user `__hash__` follows CPython (`hash_ret`): an int that fits 64 bits is the hash (`-1` becomes `-2`), a larger one
is reduced like `hash(int)` (`2**63` gives 4, `2**64 + 5` gives 13), any other result raises `TypeError: __hash__ method should return an integer`
(also for `{x}` and `{x: 1}`, `hk_user`). Dict and set lookups use the raw result as text key (`"h" + value`), so objects whose hashes collide only
after the reduction stay distinct and equal ones need equal results; `tests/34b_hash_values.py` covers all of this. `tests/34_hash64.py` stays in
`crexx_skip.txt` only for its set-order half. Set iteration order is still insertion order: emulating
CPython's table would need a numeric hash on every insertion, which the text-keyed dicts do not compute.

## Memory management (src/m_gc.crexx)

Generational-style mark and sweep over the object table. Objects born while a "window" (a loop
or a function call) is open are recorded in the young list `yl`. At the end of a loop iteration or
just before a function returns, if enough objects (or numpy float storage, `npw`) were allocated,
`gc_run` marks from the roots (frame stack `vp`, argument stack, interned tables, class/module
tables, generator stacks, exception state, the return value) plus every old mutable container
(`ctl`), and frees the unmarked young objects. Collection only happens at those safe points, never
inside a primitive. Freed slots, slot blocks (size classes), dict entries, dict ids, generator
regions and numpy float blocks are reused. Frames of finished functions that define closures are
kept in `cfl` and freed when no live function object or frame reaches them.
Classes, modules, files, builtins and functions with attributes are never freed. Survivors become part of the
enclosing window, there is no tenuring. Options: `--memstat` (stats on stderr), `--nogc`,
`--gcfix=N` (threshold N, reuse), `--gcstress=N` (like gcfix but never reuses slots, so stale
references show up as errors).

## Known gaps

- numpy: no structured/string/object dtypes (complex128 lives in `pylib/numpy/_cplx.py`, see above); `reshape`, `T`, strided slices and fancy indexing return
  copies; integer arrays wrap at 64 bits. `numpy.linalg`, `numpy.random` and many helper functions are pure
  Python in `../pylib/numpy/`.
- Metaclasses are ignored.
- `str` holds codepoints (the C interpreter uses byte strings); tracebacks always report line 1.
- `globals()` / `locals()` are snapshots; `exec` inside a function does not write back to its locals.
- Sets keep insertion order (the C interpreter emulates CPython's table); `random` does not reproduce CPython's sequence; `time.time` is approximate.
- Set union/intersection print in insertion order, CPython orders by hash. UTF-8 decoding with `replace` follows the maximal-subpart rule, but `repr` of unassigned code points uses the table from the C interpreter (Unicode version differences remain).
- Recursion limit is about 900 frames; a stock VM pays about 2 s per run linking the 3.1 MB bytecode (see `STARTUP.md`).

## CREXX pitfalls found

`main`, `self`, `dummy` are reserved; plain-variable arguments are passed by reference (pass `x + 0`
to copy); the literal `1.7976931348623157e308` behaves as infinity (compare via `x - x`);
orphan `rxvme` processes can hold the driver lock after a timeout; the `as .int` / `as .float` conversions are lossy beyond 15 digits (use `i2f` / `f2i` in `e_big2`), and a local variable named like a global (`pe`, `sm`) silently shadows it.
