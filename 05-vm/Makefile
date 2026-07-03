CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O0

.PHONY: all test valgrind clean

all:
	$(MAKE) -C tests all

test:
	$(MAKE) -C tests test

valgrind:
	$(MAKE) -C tests valgrind

clean:
	$(MAKE) -C tests clean