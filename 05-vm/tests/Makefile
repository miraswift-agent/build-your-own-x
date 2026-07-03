CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O0
SRCDIR = ../src

all: test_vm

test_vm: test_vm.c $(SRCDIR)/vm.c $(SRCDIR)/debug.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $^

test: test_vm
	./test_vm

valgrind: test_vm
	valgrind --leak-check=full --error-exitcode=1 ./test_vm

clean:
	rm -f test_vm

.PHONY: all test valgrind clean