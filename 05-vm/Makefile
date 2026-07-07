CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O0
SRCDIR = ../src

.PHONY: all test valgrind clean stage1 stage2 stage3 stage3-test stage3-valgrind

all: stage1 stage2 stage3

stage1:
	$(MAKE) -C tests test_vm

stage2:
	$(MAKE) -C tests test_lantern

stage3:
	$(MAKE) -C tests test_compiler

stage3-test:
	$(MAKE) -C tests test_stage3

stage3-valgrind:
	$(MAKE) -C tests valgrind_stage3

test: stage1 stage2 stage3
	$(MAKE) -C tests test_stage1
	$(MAKE) -C tests test_stage2
	$(MAKE) -C tests test_stage3

valgrind:
	$(MAKE) -C tests valgrind_stage1
	$(MAKE) -C tests valgrind_stage2
	$(MAKE) -C tests valgrind_stage3

clean:
	$(MAKE) -C tests clean
