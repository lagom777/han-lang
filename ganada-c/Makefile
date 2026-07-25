CC ?= clang
CFLAGS ?= -O2 -std=c11 -Wall -Wno-unused-function
SRCS = src/main.c src/value.c src/lexer.c src/parser.c src/interp.c src/builtins.c
HDRS = src/ganada.h src/tables.h

ganada: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o ganada $(SRCS) -lm

clean:
	rm -f ganada

.PHONY: clean
