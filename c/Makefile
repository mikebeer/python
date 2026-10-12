# Python 3 subset interpreter in portable C89
CC ?= cc
CFLAGS ?= -O2
STD = -std=c89 -pedantic -Wall -Wextra
POSIX = -DPY_POSIX
PYTHON ?= python3
DECKCOLS ?= 71
# compile-time fallback for the library search path (after script dir, $PYLIB and <exe>/pylib)
LIBDIR ?= $(abspath $(CURDIR)/../pylib)
LIBFLAG = -DPY_LIBDIR='"$(LIBDIR)"'
BOGUS = -DPY_LIBDIR='"/nonexistent/pylib"'

SRC = src/py.c src/p0_os.c src/p0_base.c src/p1_num.c src/p2_lex.c src/p3_parse.c src/p4_res.c \
      src/p5_eval.c src/p6_obj.c src/p7_fmt.c src/p7_bi.c src/p7_bytes.c src/p7_cplx.c src/p7_np.c src/p7_np2.c src/p7_la.c \
      src/p7_rnd.c src/p7_imp.c src/p7_mod.c src/p7_re.c src/p7_json.c src/p7_sock.c src/p8_main.c
PYLIBS = $(shell find ../pylib -name '*.py' 2>/dev/null)

py: py_all.c
	$(CC) $(STD) $(CFLAGS) $(LIBFLAG) -o py py_all.c -lm

posix: py_all.c
	$(CC) $(STD) $(CFLAGS) $(POSIX) $(LIBFLAG) -o py py_all.c -lm

# the pure-Python library tree compiled into the binary (no pylib directory needed)
embed: py_embed_all.c
	$(CC) $(STD) $(CFLAGS) $(POSIX) -DPY_EMBED_LIBS $(LIBFLAG) -o py_embed py_embed_all.c -lm

py_all.c: $(SRC) tools/amalg.sh
	sh tools/amalg.sh > py_all.c

src/pylib_embed.c: $(PYLIBS) tools/embed_pylib.py
	$(PYTHON) tools/embed_pylib.py ../pylib > src/pylib_embed.c

py_embed_all.c: $(SRC) src/pylib_embed.c tools/amalg.sh
	sh tools/amalg.sh embed > py_embed_all.c

# the card deck for the VM/370 build: every line at most $(DECKCOLS) columns
deck: deck/py_deck.c

deck/py_deck.c: py_all.c tools/deck.py
	mkdir -p deck
	$(PYTHON) tools/deck.py --limit $(DECKCOLS) py_all.c > deck/py_deck.c
	$(PYTHON) tools/deck.py --verify py_all.c deck/py_deck.c

deck-embed: deck/py_deck_embed.c

deck/py_deck_embed.c: py_embed_all.c tools/deck.py
	mkdir -p deck
	$(PYTHON) tools/deck.py --limit $(DECKCOLS) py_embed_all.c > deck/py_deck_embed.c
	$(PYTHON) tools/deck.py --verify py_embed_all.c deck/py_deck_embed.c

# fails if a deck line is longer than $(DECKCOLS) columns (or has tabs, non-ASCII, trigraphs)
deckcheck: deck
	$(PYTHON) tools/deck.py --check --limit $(DECKCOLS) deck/py_deck.c \
	    $(wildcard deck/py_deck_embed.c) src/pylib_embed.c
	@echo "deckcheck: ok"

# development sources stay readable: at most 100 columns
linecheck:
	@! awk 'length > 100 { print FILENAME ":" FNR ": " length " columns" }' \
	    src/p[0-9]*.c src/py.c tools/*.sh | grep . || (echo "linecheck: lines over 100 columns"; exit 1)
	@echo "linecheck: ok"

test: posix
	sh ../tests/run_tests.sh ./py

# build the deck with strict flags and run the whole suite against it
deck-test: deck
	$(CC) $(STD) -pedantic-errors $(CFLAGS) $(POSIX) $(LIBFLAG) -o py_deck deck/py_deck.c -lm
	sh ../tests/run_tests.sh ./py_deck

# embedded libraries: run from an empty directory, no PYLIB, bogus PY_LIBDIR
embed-test: embed
	$(CC) $(STD) $(CFLAGS) $(POSIX) -DPY_EMBED_LIBS $(BOGUS) -o py_embed py_embed_all.c -lm
	d=$$(mktemp -d) && cp py_embed $$d/py && \
	  (cd $$d && env -u PYLIB sh $(CURDIR)/../tests/run_tests.sh $$d/py); r=$$?; rm -rf $$d; exit $$r

deck-embed-test: deck-embed
	$(CC) $(STD) $(CFLAGS) $(POSIX) -DPY_EMBED_LIBS $(BOGUS) -o py_deck_embed deck/py_deck_embed.c -lm
	d=$$(mktemp -d) && cp py_deck_embed $$d/py && \
	  (cd $$d && env -u PYLIB sh $(CURDIR)/../tests/run_tests.sh $$d/py); r=$$?; rm -rf $$d; exit $$r

# strict C89 matrix (gcc, clang, sanitizers)
strict: py_all.c py_embed_all.c deck
	sh tools/strict.sh

bench: py
	sh bench/run.sh

clean:
	rm -f py py_embed py_deck py_deck_embed strict_san

.PHONY: posix embed deck deck-embed deckcheck linecheck test deck-test embed-test deck-embed-test \
        strict bench clean
