# Porting the C interpreter to VM/370 (CMS)

This file lists what was done to make `py` portable, how to build the card
deck, and what is still open. The rules follow `mikebeer/apl` and
`smalltalk/c`: strict C89, one external symbol (`main`), literal character
sets, 71-column source for the deck step, `setjmp`/`longjmp` declared for
GCCLIB31 under `__CMS__`.

## 1. Where things are

    src/p0_os.c        standard headers + the ONLY host/OS adapter (host_* functions)
    src/p0_base.c ...  the interpreter (no OS calls, no ASCII range tests for letters)
    src/pylib_embed.c  GENERATED: pylib/**/*.py as numeric byte arrays (tools/embed_pylib.py)
    py_all.c           amalgamation, development form, lines <= 100 columns
    py_embed_all.c     amalgamation with pylib_embed.c inlined (use with -DPY_EMBED_LIBS)
    deck/py_deck.c     the deck: py_all.c folded to <= 71 columns (checked in)
    deck/py_deck_embed.c  the same for py_embed_all.c (made by `make deck-embed`)
    tools/deck.py      the deck step (reflow); also --check and --verify
    tools/embed_pylib.py   library to C arrays
    tools/strict.sh    conformance matrix (gcc, clang, sanitizers)

## 2. Build steps

On the build host (needs `python3` only for the generator tools; the C
sources need nothing):

    make posix            # development build, ./py (uses pylib/ on disk)
    make test             # 32 tests
    make embed            # ./py_embed with pylib/ compiled in (-DPY_EMBED_LIBS)
    make embed-test       # runs the tests from an EMPTY directory, PYLIB unset, bogus PY_LIBDIR
    make deck             # deck/py_deck.c, every line <= 71 columns
    make deck-embed       # deck/py_deck_embed.c  (the file to take to VM/370)
    make deckcheck        # FAILS if a deck line is > 71 columns, has a tab, non-ASCII or "??"
    make deck-test        # compiles the deck with -std=c89 -pedantic-errors and runs the tests
    make deck-embed-test  # embedded deck, empty directory, no pylib
    make strict           # conformance matrix of section 5
    make linecheck        # development sources stay <= 100 columns

For VM/370 (as in apl's README, cross compiler for the GCCLIB31 headers):

    s390x-linux-gnu-gcc -std=c89 -pedantic -m31 -O2 -I gcclib31/src \
        -D__CMS__ -D__CHAR_UNSIGNED__ -DPY_EMBED_LIBS \
        -DVSTACK_SIZE=40000L -c deck/py_deck_embed.c

`__CMS__` selects `PY_HOST_CMS` (see 3). `VSTACK_SIZE` (value stack in 16-byte
Values, default 400000 = 6.4 MB) and `MAX_DEPTH` (Python recursion limit,
default 1000) are `#ifndef`-guarded because the default is too big for a
16 MB CMS machine; both can be set with `-D`. `deck/py_deck_embed.c` is about
89,200 card images (5.0 MB; the library data is roughly 4 MB of it). Use
`deck/py_deck.c` (31,700 cards, 1.0 MB) plus module files on a minidisk if that is
too big.

## 3. The adapter layer (`src/p0_os.c`)

Everything below is the complete list of host dependencies. A port edits this
file only. POSIX-only calls are under `-DPY_POSIX`; without it the program
uses only ISO C (`time`, `clock`, `fopen`, `getenv`).

| function | purpose | POSIX / default | notes for CMS |
|---|---|---|---|
| `PY_DIRSEP`, `PY_PARDIR` | name joining, parent directory | `"/"`, `".."` | override with `-D` |
| `PY_PATHLIST_SEP` | separator inside `$PYLIB` | `":"` | `" "` or `","` |
| `PY_RMODE` | fopen mode for reading | `"rb"` | `"r"` (set for `PY_HOST_CMS`) |
| `PY_EXE_LIBS` | also search `<exe dir>/pylib`, `<exe dir>/../pylib` | 1 | 0 (set for `PY_HOST_CMS`) |
| `host_join` | directory + name | joins with `PY_DIRSEP` | |
| `host_dirname_len` | directory part of the script path | up to last `/` | |
| `host_module_path` | file for module `leaf` in search entry `dir`, module or package init | `dir/leaf.py`, `dir/leaf/__init__.py` | CMS variant: `LEAF PY fm` (module) / `LEAF PYI fm` (package) with the search entry as the file mode, name upper-cased, max 8 chars, packages flat. It compiles but is a starting point: decide the real mapping (see 7) |
| `host_read_file` | whole file (or embedded module) into memory | `fopen`/`getc` | |
| `host_fopen` | Python `open()` | `fopen` | |
| `host_file_exists/isdir/isfile/size/remove/rename` | `os.path.*`, `os.remove`, `os.rename` | stdio | |
| `host_getenv`, `host_getcwd`, `host_environ` | `os.getenv`, `os.getcwd`, `os.environ`, `$PYLIB` | `getenv`; cwd and environ only with `PY_POSIX` | |
| `host_isatty_stdin` | REPL prompt detection | `isatty(0)` with `PY_POSIX` | |
| `host_time`, `host_clock`, `host_sleep`, `host_seed` | `time.time`, `perf_counter`, `sleep`, random seeding | `gettimeofday`, `nanosleep` with `PY_POSIX`; else `time()` / `clock()` busy wait | |

Not used anywhere, so no adapter exists: `popen`, `system`, directory
listing (there is no `os.listdir`), threads. Signals: only `SIGPIPE` is
ignored (with `PY_POSIX`, on the first socket).

### Sockets (`host_sock_*`, last section of `p0_os.c`)

The `socket` and `select` modules (`src/p7_sock.c`) are written only against
this block; BSD sockets appear nowhere else. With `-DPY_POSIX` it is IPv4
TCP on `socket/bind/listen/accept/connect/send/recv/select/fcntl/
getaddrinfo`; without it every call fails and `socket.socket()` raises
`OSError: sockets not available`. A CMS port replaces the `#ifdef PY_POSIX`
branch with the TCP/IP stack's calls (IBM TCP/IP for VM sockets, the
Pascal-style `TcpOpen`/`TcpSend` interface, or an EXEC to a service machine);
it has to supply:

| function | meaning |
|---|---|
| `host_sock_ok()` | 1 when a network exists |
| `host_sock_open/close(fd)` | a TCP endpoint handle (a small int) |
| `host_sock_bind/listen/accept/connect` | addresses are 4 bytes in network order and a port; `accept` returns the new handle and the peer |
| `host_sock_send/recv(fd, buf, n)` | bytes moved, or -1 with `host_sock_err` set |
| `host_sock_blocking(fd, on)` | switch a handle to non-blocking mode (used for timeouts) |
| `host_sock_select(n, fds, modes, ready, timeout)` | wait for readable (1) / writable (2) handles; timeout < 0 waits forever; a stack with no select can poll in a loop with `host_sleep` |
| `host_sock_getname/setopt/getopt/soerror/shutdown` | `getsockname`, `getpeername`, options (`SO_REUSEADDR` ...), the pending connect error |
| `host_sock_hostname`, `host_sock_resolve(name, ip)` | local host name; name or dotted quad to 4 bytes |
| `host_sock_errkind(e)`, `host_sock_strerror(e)` | map the host error number to the Python exception class (refused, reset, aborted, broken pipe, would block, interrupted, timed out, in progress) and its text |
| `host_sock_const(name, &v)` | numeric values of `AF_INET`, `SOCK_STREAM`, `SOL_SOCKET`, `SO_*` ... |

Error numbers are whatever the host uses; they become `OSError.errno`.
Handles are never closed by the garbage collector. Network byte order is
big-endian on S/370 too, so `htons` is the identity there (it is computed at
run time from the byte order).

`os`, `os.path`, `os.sep`, `os.name` and `sys.platform` as seen by Python
programs are POSIX-flavoured emulations built on these functions.

## 4. The deck step (71 columns)

`tools/deck.py` folds a C file to a given width (`--limit`, default 71).
It never changes the token stream of the program:

* lines are broken only between tokens: after `;`, `{`, `}`, `,`, before
  `&&` `||` `?` `:` and binary operators, after `(`, preferring the lowest
  parenthesis depth; statements on one line are put on separate lines;
* a string literal that does not fit a card is cut into adjacent literals
  (`"ab" "cd"`) at an escape-sequence boundary and preferably after a blank;
* comments are re-wrapped word by word (trailing comments that do not fit
  move to a line of their own); runs of `=====` are shortened;
* macro lines keep their trailing backslash, and the leading blanks of macro
  continuation lines are squeezed so the spliced logical line stays below
  the 509 characters C89 guarantees (longest logical line: 444);
* output is checked: no tab, no non-ASCII, no trailing blank, no `??`
  (trigraph) sequence, nothing over the limit; `--verify ORIG DECK` compares
  the two token streams (comments dropped, adjacent string literals merged).

`gcc -S` of `py_all.c` and `deck/py_deck.c` gives byte-identical assembly
(apart from the file name), so the deck compiles identically. Column 72 and
up are never used. A line that cannot be folded (one very long identifier,
a long `#include`) makes the tool fail instead of guessing.

Dev sources are kept at <= 100 columns (`make linecheck`); only generated
files are 71 columns. The deck is committed (`deck/py_deck.c`).

## 5. C89 conformance checks

`make strict` compiles `py_all.c`, `deck/py_deck.c` (with and without
`-DPY_POSIX`, and with `-DPY_HOST_CMS`) and `py_embed_all.c` with gcc and
clang using `-std=c89 -pedantic-errors -Wall -Wextra -Wconversion -Wshadow
-Wdeclaration-after-statement -Wvla -Wstrict-prototypes -Wundef -Wtrigraphs
-Wlong-long`: no diagnostics. It also checks that the object file references
no C99 library function (the list: `strdup snprintf isblank round trunc
copysign isnan isinf fmin fmax lround cbrt log2 log1p expm1 hypot erf tgamma
lgamma strtoll nan rint fma asinh acosh atanh exp2 remainder` ...; the needed
ones exist as static replacements in `p7_la.c` / `p7_mod.c`), and runs the
whole suite under gcc `-O0 -fsanitize=undefined,address` (clean). Also
checked by hand or by grep: no `long long`, `%zu`, `%ll`, `inline`,
`restrict`, `//`, `stdint.h`, `stdbool.h`, `va_copy`, `__func__`, no
designated initialisers or VLAs; the only external symbol is `main`
(`nm`); the longest identifier is 25 characters, longest string literal
181; `-funsigned-char` (the S/370 default) and the non-POSIX build pass.

Fixed in this pass: sign/width conversion warnings (`GC_ACCOUNT`, bitwise
`and/or/xor` on bools, `ord()` UTF-8 decode, numpy `kron`/`norm`); `sys.byteorder`
was hard-wired to `"little"` (S/370 is big-endian) and is now detected;
`numpy.random` used `unsigned long` as a 64-bit type with 64-bit literals,
which fails to compile where `long` has 32 bits, and is now written against a
`u64` abstraction (native, or two 32-bit halves; verified identical streams
with `-DPY_U64_EMUL`, and the emulation checked against 64-bit arithmetic on
200,000 random operand pairs with 32-bit halves); `VSTACK_SIZE` / `MAX_DEPTH`
made overridable.

Simulating a 32-bit `long` on a 64-bit host: `-DPY_SI32 -DPY_U64_EMUL` (small
ints limited to +-999999999, 64-bit emulation) passes all 32 tests. This simulation limits the small-int range but the host
`long` stays 64 bits, so it does not catch code that casts a double to `long` without
testing against `SI_MAX` (that was the bug fixed in commit 6e4a7a5: `int(1e10)`
and `Fraction(0.25)` saturated at 2147483647 on VM/370plus CMS, GCC 13 `-m31`).

A real 32-bit `long` is available here with `gcc -m32 -msse2 -mfpmath=sse` (multilib; the x87
default gives `-0.0`/`0.0` differences in tests 16 and 30, that is the x87, not `long`).
`tests/tools/si32_diff.py` compares thousands of random expressions (ints around every
boundary, big ints, floats, `str`/`repr`/`format`/`%`/`int()`/`float()`/`round`/`divmod`/`pow`,
bit operations, `hash`, `struct`, `to_bytes`, `range`, slicing, `sort`, `dict`/`set`, `re`, `json`,
`random` ranges ...) against CPython 3 in up to three builds:
`python3 tests/tools/si32_diff.py -n 800 -s SEED /tmp/py64:normal /tmp/pysi:si32 /tmp/py32s:m32`
(`-c CATS` selects categories, `--self` checks the generator against CPython). The pass that
introduced it found and fixed (every one of these was wrong with a 32-bit `long`, or beyond
10^18 on a 64-bit one):

* `mkint` now normalises any `long` outside `+-SI_MAX` to a bigint, so a sum, product, `abs`,
  `-x`, `int(float)`, `len` or `time_ns()` result can no longer carry a value that is not small;
* int/float: exact `int -> float` (correct rounding, any size), exact `float -> int`, exact
  comparison of int and float (`2**53 + 1 > 2.0**53`), correctly rounded true division of huge
  ints, `float ** int`, `round` (also with negative digits), `math.log/log2/log10/isqrt/fsum` for
  big ints, `float.hex/fromhex/as_integer_ratio/is_integer`, `int.bit_length/bit_count`;
* `hash()`: CPython's scheme (value modulo 2**61-1, sign kept, -1 becomes -2, inf 314159,
  complex `hash(re) + 1000003 * hash(im)`) for int, bool, big int, float and complex, so
  `hash(1e10) == hash(10**10)` and `{10**10: 1}[1e10]` hold in every build; `Fraction` equality,
  ordering and hash (`pylib/fractions.py`, also used by CREXX) are exact for floats;
* `& | ^ ~ << >>` on ints of any size with two's complement semantics (digit arrays of 16 bits),
  `pow(a, b, m)` for big values and negative exponents (modular inverse), `int.to_bytes`,
  `from_bytes`, `struct` `q Q` and unsigned 4-byte `I L`, `hex/oct/bin`, `%x %o %d %e %f %g`,
  `format` (`,` `_` grouping, `n`, `#`, precision of `%` types), `str(int)` / `int(str)` for
  any base up to 36;
* `range` with bounds, step and length beyond `long` (len, index, slice, `in`, `count`, `index`,
  `reversed`, iteration, `enumerate(start=)`), `list` repeat counts, `maxsplit`, `insert`, `re`
  `count/maxsplit/pos` (huge values clamp; above 2**63 they raise OverflowError as in CPython);
* `random.randrange/randint/getrandbits/randbytes/choices/gauss/expovariate` with big ranges and
  seeds, `time.time_ns/monotonic_ns/perf_counter_ns/process_time_ns`;
* smaller things found on the way: `float('-nan')`, `-0.0 // x` and `%` signs, `1e308 ** 2`
  (OverflowError), shortest `repr` of a float at a power of two (`2.0**122`), `'{:.3}'.format(x)`,
  `str.split(None, n)` / `rsplit` remainders, `count('')` past the end, `bytes.split(sep, n)`,
  `bytes.hex(sep, n)`, `reversed(bytes)`, `int.__index__`, `math.nextafter/ulp`, `repr()` and
  `isprintable()` of non-printable non-ASCII characters.

Result with `-n 800` per seed, all categories: the normal 64-bit and the `-DPY_SI32` builds
agree with CPython except for the points listed below; the `-m32` build differs from them only by
the 32-bit limits in the second list.

Differences that remain (all builds): `frozenset` is a `set` (no hash, no equality with `set`),
set iteration is insertion ordered, strings are UTF-8 bytes (`len`, indexing, `re` on non-ASCII
text and case conversion beyond Latin-1/Greek/Cyrillic differ), `float('١٢٣')` (non-ASCII digits)
fails, `statistics` results can differ from CPython's exact-fraction arithmetic by one ulp,
`Decimal` hash, `bytearray` repr of a quote.

Differences that are inherent to a 32-bit `long`: `len(range(n))` and `sys.maxsize` stay below
2**31 (an OverflowError as with CPython on a 32-bit build), `range`-size list/str repeat counts,
numpy integer arrays are `long` (int64 arithmetic wraps at 32 bits), `-m32` without SSE2 uses the
x87 (extended precision, `-0.0`).

Sizes and results at this revision (32 tests; measured with `wc -l` and `make`):

| file | lines (cards) | bytes | note |
|---|---|---|---|
| `src/*.c` without `pylib_embed.c` | 29,708 | | 23 files, longest line 100 |
| `py_all.c` | 29,708 | | amalgamation |
| `deck/py_deck.c` | 33,960 | 1,070,050 | the deck, checked in |
| `src/pylib_embed.c` | 57,760 | | `pylib/` as byte arrays, 68 files, 1.15 MB of Python |
| `deck/py_deck_embed.c` | 91,720 | 5,131,323 | deck with the library, made by `make deck-embed` |

| check | result |
|---|---|
| `make test`, `deck-test`, `embed-test`, `deck-embed-test` | 32 of 32 each |
| `make deckcheck`, `make linecheck` | ok |
| `make strict` (gcc and clang syntax matrix, C99 symbol check, sanitizers) | all ok, 32 of 32 under ASan/UBSan |
| `-DPY_SI32 -DPY_U64_EMUL` | 32 of 32 |
| `-funsigned-char` | 32 of 32 |
| `-m32 -msse2 -mfpmath=sse` (real 32-bit `long`) | 32 of 32 |
| embedded library table absent (bogus `PY_LIBDIR`, empty directory) | 13 of 32 fail, so the table is used |

`py_embed_all.c`, `src/pylib_embed.c`, `deck/py_deck_embed.c` and the `py_embed*` binaries are
generated and ignored by `c/.gitignore`; `make deck-embed` rebuilds them when anything under
`pylib/` or `src/` changes (they were regenerated for this revision with `make -B`).

`-m32 -msse2 -mfpmath=sse`: 32 of 32 and no warnings. Not available in this environment: clang
sanitizer runtime, a s390 cross compiler, GCCLIB31 headers; so none of those were run.

## 6. Embedded libraries (`-DPY_EMBED_LIBS`)

`tools/embed_pylib.py` walks `pylib/` (all `*.py`, sorted, no `__pycache__`)
and writes `src/pylib_embed.c`: for each file one or more arrays of decimal
byte values (`static const unsigned char pyemb_N_M[] = { 47,42,... };`, at
most 32,000 bytes per array because C89 only guarantees 32,767-byte objects)
and the table `pyemb_files[]` with the relative name (`"numpy/__init__.py"`,
`"pandas/core.py"`), the size and the chunk list. Only names are string
literals; the file contents contain no character literals, so they are
independent of the host character set. The bytes are ASCII codes; when a
module is read, `host_from_ascii()` maps them to the host character set
through a literal set (`" !\"#$%&'()*+,-./0123456789:;<=>?@ABC..."`), so
Python source from the table is correct on EBCDIC. Bytes >= 128 (UTF-8) are
passed through unchanged.

Import: `sys.path` is `[script dir, "<pylib>", $PYLIB entries, <exe>/pylib,
<exe>/../pylib, PY_LIBDIR]`. `"<pylib>"` is a pseudo directory:
`host_module_path` composes `<pylib>/pkg/mod.py` with `/` always and
`host_read_file` answers it from the table, so packages, relative imports and
`numpy._overlay` work unchanged (`__file__` is `<pylib>/scipy/stats.py`). The
script directory comes first (as in Python) so a user module can shadow a
library module; the embedded table comes before every library directory on
disk. Tested: all 31 tests pass in an empty directory with no `PYLIB` and
`PY_LIBDIR=/nonexistent` (`make embed-test`, `make deck-embed-test`); the
same binary without the table fails 13 tests, so the table is really used.
`make deckcheck` also checks that `pylib_embed.c` itself is <= 71 columns.

## 7. Known ASCII / host assumptions that remain

Character classes for identifiers, digits, spaces and case conversion of
ASCII letters come from literal sets (`init_chars`, `p2_lex.c`), and
`re`/`str.isalpha` etc. use those tables. What is still ASCII (or Unicode)
based, not host-character based:

1. Python `str` is stored as UTF-8 bytes of Unicode code points. `chr`,
   `ord`, `\x` `\u` escapes in the lexer, `str.encode/decode`, `bytes`,
   `json` `\uXXXX`, `%c`, and the Latin/Greek/Cyrillic case tables in
   `p7_bi.c` (bytes 0xC3, 0xCE, 0xCF, 0xD0, 0xD1) assume that code points
   below 128 are the bytes 0..127. On EBCDIC `chr(65)` would give byte 65
   (not `A`). Needs a translation at the boundaries (source in, output out,
   `chr`/`ord`, encode/decode) or the decision that strings are ASCII
   internally and `fwrite`/`fputs` translate on output.
2. Control-character tests: `repr()` of `str` and `bytes`, `str.isprintable` and `ascii()` use
   the literal set `CS_PRINT` plus the letters and digits (`ch_print`, `init_chars`), not the
   numbers 32 and 127; non-ASCII code points are tested with a range table (`uni_nonprint`,
   `p6_obj.c`). Still numeric: the JSON encoder (`c >= 32`, `c < 127`, `p7_json.c`, where the
   numbers are Unicode code points, as in the JSON standard) and `\b`, `\f` there use the C
   escapes.
3. Sorting and comparison of `str` is `memcmp` of the bytes: EBCDIC collation
   (lower < upper < digits) differs from Python's (digits < upper < lower).
4. `re`: ranges such as `[a-z]` compare code points; on EBCDIC they would
   include non-letters between `i`/`j`, `r`/`s`. Case folding loops over the
   literal alphabet. Classes `\w \d \s` use the tables and the Unicode path
   only for bytes >= 128.
5. Hex and decimal digit values come from `digit_val` (a literal-set lookup) in `json`, `re`,
   the lexer, `int()` and `bytes.fromhex`; the case-insensitive `re` loop runs over the literal
   alphabet.
6. Source files are read as bytes; UTF-8 string literals in `.py` files are
   not translated (the embedded libraries are plain ASCII except where
   noted in point 1).
7. `os.path` functions treat `/` as the separator; `os.sep`, `os.name`,
   `os.devnull` are POSIX constants. File names with CMS `fn ft fm`
   (`host_module_path` CMS branch, `open()` of such names) need a decision:
   this port maps `import a_b` to `A_B PY fm` ignoring packages, which
   cannot represent `pandas.core` vs `scipy.sparse.linalg` and names longer
   than 8 characters (`interpolate`, `groupby`...). Options: an alias table
   in `host_module_path`, or one library file per package, or the embedded
   table (which has no such limit) for everything.
8. Floating point is assumed IEEE-754 double: `inf`/`nan` are made with
   `HUGE_VAL` and `HUGE_VAL - HUGE_VAL`, float `repr` searches the shortest
   `%.17g` that round-trips, `float` formatting and numpy printing use
   `sprintf`. S/370 hexadecimal floating point (range about 1e-78 to 7e75, no
   infinity or NaN, different rounding) will change results, overflow behaviour and the `repr` search; apl's approach
   (limit +-7e75, domain errors) is not applied here.
9. Integers: small ints are `long` (`+-10^18` with 64-bit `long`,
   `+-999999999` with 32-bit, chosen from `LONG_MAX`; larger values are
   bigints; `mkint` converts any other `long` to a bigint), `hash()` follows CPython's
   modular scheme in every build, `numpy` integer arrays use `long` (32 bits wide there).
10. `sprintf`/`vsprintf` (C89 has no `snprintf`) write into fixed buffers
    with `%.Ns` precision limits; do not widen those formats.
11. Memory: the value stack is allocated at start (`VSTACK_SIZE`), the
    GC heap grows with `malloc`; C recursion depth follows `MAX_DEPTH`
    (1000 Python calls) which needs several hundred KB of C stack: set the
    CMS stack/region size accordingly or lower `MAX_DEPTH`.
12. `setjmp`/`longjmp` are used for exceptions; `__CMS__` declares them (as
    in apl). `main` is the only external name.
13. `struct` and `int.to_bytes` with floats assume IEEE-754 doubles and the
    LP64/ILP32 native sizes chosen from `LONG_MAX`; the 'f'/'d' formats on
    S/370 hex floating point would need a conversion to IEEE bytes.
14. `time.sleep` busy-waits without `PY_POSIX`; `time.time()` has one-second
    resolution there.

## 8. What remains for the real VM/370 port

* Compile `deck/py_deck_embed.c` against the real GCCLIB31 headers
  (`-D__CMS__ -D__CHAR_UNSIGNED__`) and fix whatever the old gcc/library
  rejects (untested: the headers, `clock()`/`CLOCKS_PER_SEC`, `qsort`,
  `strtod`, `%g`, `vsprintf` in GCCLIB31).
* Produce the TEXT file / load deck, check storage use (value stack, heap,
  embedded data) in a 16 MB machine; tune `VSTACK_SIZE`/`MAX_DEPTH`.
* Decide the character-set strategy (point 1 above): the simplest is to keep
  all Python data ASCII internally and translate in `host_*` I/O and in the
  few C literals used as Python names, or to compile the interpreter itself
  as ASCII-in-EBCDIC with translation of terminal I/O only.
* Decide CMS file naming for user programs and modules (point 7) and write the
  `fopen` string conversion in `host_fopen` / `host_read_file`
  (`"fn ft fm"` or `"fn.ft"` syntax of the C library in use).
* Terminal I/O: line mode with 80 column records, `stdin` prompt behaviour
  (`host_isatty_stdin` currently 0 without `PY_POSIX`, so use `-i`).
* Floating point (point 8) and the 32-bit `long` limits (point 9) need a run
  of the test suite on the target; expect differences in float tests.
* Test on Hercules. The test corpus has been run once on VM/370plus CMS (GCC 13, `-m31`,
  32-bit `long`): that run found the `SI_MAX` defect above (10_misc, 19_modules), fixed in
  commit 6e4a7a5; its complete result is not recorded here. Otherwise not run on any
  non-x86-64 platform.

## Sets and hashes

`set` / `frozenset` emulate CPython's table (`src/p0_base.c`, section "sets"; the operations are in
`src/p6_obj.c` and `src/p7_bi.c`) so that the iteration order of sets of ints (and bool, None,
float, complex, tuples and frozensets of those) equals CPython 3.13's. This needs the CPython
values of `hash()`: ints, floats, complex and `None` already had them, `tuple` now uses the
xxHash scheme and `frozenset` the `_shuffle_bits` scheme. Both need a 64-bit `unsigned long`
(constants are guarded by `ULONG_MAX`); with a 32-bit one (VM/370) they are self-consistent
32-bit variants, the sets work the same but the iteration order of colliding keys can differ
from CPython. `Decimal.__hash__` (`pylib/decimal.py`) uses the same modulo-(2**61-1) scheme as
`Fraction`.

A user `__hash__` result is taken as CPython does (`user_hash_val` in `src/p1_num.c`): an int that
fits a signed 64-bit value is the hash itself (-1 becomes -2), anything larger is reduced with
`hash(int)` (modulo 2**61-1), a non-int raises TypeError. Such instances count as reproducible
keys (`key_det`), so sets of them follow the CPython table order; instances without `__hash__`
(identity hash) still switch the set to insertion order. With a 32-bit `unsigned long` the value
is truncated to its low word (self-consistent, order can differ). `tests/34_hash64.py` is C only:
`tests/crexx_skip.txt` is read by `run_tests.sh` when the interpreter path contains `crexx`, and
`SKIPFILE=tests/m32_skip.txt` skips it (and `33_hashing`) for a real 32-bit `long`.
