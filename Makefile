CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -pedantic

.PHONY: all clean

all: code

code: code.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f code
