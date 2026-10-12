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
    make test             # 21 tests
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
70,500 card images (3.9 MB; the library data is roughly 3 MB of it). Use
`deck/py_deck.c` (25,500 cards) plus module files on a minidisk if that is
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
listing (there is no `os.listdir`), signals, sockets, threads.

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
ints limited to +-999999999, 64-bit emulation) passes all 21 tests.

Not available in this environment: clang sanitizer runtime, `-m32`, a
s390 cross compiler, GCCLIB31 headers; so none of those were run.

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
disk. Tested: all 21 tests pass in an empty directory with no `PYLIB` and
`PY_LIBDIR=/nonexistent` (`make embed-test`, `make deck-embed-test`); the
same binary without the table fails 7 tests, so the table is really used.
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
2. Control-character tests use numbers: `repr()` escapes bytes `< 32`,
   `== 127`, `>= 127` (`p6_obj.c`), `str.isprintable` / `isascii`
   (`p7_bi.c`), JSON encoder `c >= 32`, `c < 127` (`p7_json.c`).
3. Sorting and comparison of `str` is `memcmp` of the bytes: EBCDIC collation
   (lower < upper < digits) differs from Python's (digits < upper < lower).
4. `re`: ranges such as `[a-z]` compare code points; on EBCDIC they would
   include non-letters between `i`/`j`, `r`/`s`. Case folding loops
   `for (c = 'a'; c <= 'z'; ...)` are harmless there (unused gaps map to
   themselves). Classes `\w \d \s` use the tables and the Unicode path
   only for bytes >= 128.
5. Hex digit values (`p7_json.c`, `p7_re.c`) use `'0'..'9'`, `'a'..'f'`,
   `'A'..'F'`, which are contiguous in ASCII and EBCDIC.
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
   bigints), `hash()` values differ from CPython on 32-bit hosts,
   `numpy` integer arrays use `long`.
10. `sprintf`/`vsprintf` (C89 has no `snprintf`) write into fixed buffers
    with `%.Ns` precision limits; do not widen those formats.
11. Memory: the value stack is allocated at start (`VSTACK_SIZE`), the
    GC heap grows with `malloc`; C recursion depth follows `MAX_DEPTH`
    (1000 Python calls) which needs several hundred KB of C stack: set the
    CMS stack/region size accordingly or lower `MAX_DEPTH`.
12. `setjmp`/`longjmp` are used for exceptions; `__CMS__` declares them (as
    in apl). `main` is the only external name.
13. `time.sleep` busy-waits without `PY_POSIX`; `time.time()` has one-second
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
* Test on Hercules. Not yet run on any non-x86-64 platform.
