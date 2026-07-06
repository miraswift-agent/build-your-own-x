CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O0
SRCDIR = ../src

.PHONY: all test valgrind clean stage1 stage2 stage2-test stage2-valgrind

all: stage1 stage2

stage1:
	$(MAKE) -C tests test_vm

stage2:
	$(MAKE) -C tests test_lantern

test: stage1 stage2
	$(MAKE) -C tests test_stage1
	$(MAKE) -C tests test_stage2

valgrind:
	$(MAKE) -C tests valgrind_stage1
	$(MAKE) -C tests valgrind_stage2

clean:
	$(MAKE) -C tests clean