# Python 3 subset interpreter in portable C89
CC ?= cc
CFLAGS ?= -O2
STD = -std=c89 -pedantic -Wall -Wextra
POSIX = -DPY_POSIX

SRC = src/py.c src/p0_base.c src/p1_num.c src/p2_lex.c src/p3_parse.c src/p4_res.c \
      src/p5_eval.c src/p6_obj.c src/p7_fmt.c src/p7_bi.c src/p7_mod.c src/p8_main.c

py: py_all.c
	$(CC) $(STD) $(CFLAGS) -o py py_all.c -lm

posix: py_all.c
	$(CC) $(STD) $(CFLAGS) $(POSIX) -o py py_all.c -lm

py_all.c: $(SRC) tools/amalg.sh
	sh tools/amalg.sh > py_all.c

test: py
	sh ../tests/run_tests.sh ./py

bench: py
	sh bench/run.sh

clean:
	rm -f py
