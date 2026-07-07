CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O0
SRCDIR = ../src

# Stage 1 (Chip-8)
STAGE1_SRCS = $(SRCDIR)/vm.c $(SRCDIR)/debug.c
STAGE1_HDRS = $(SRCDIR)/vm.h

# Stage 2 (Lantern Bytecode)
STAGE2_SRCS = $(SRCDIR)/lantern.c $(SRCDIR)/value.c $(SRCDIR)/chunk.c $(SRCDIR)/disasm.c $(SRCDIR)/assemble.c
STAGE2_HDRS = $(SRCDIR)/lantern.h

# Stage 3 (Compiler)
STAGE3_SRCS = $(STAGE2_SRCS) $(SRCDIR)/compiler.c
STAGE3_HDRS = $(STAGE2_HDRS) $(SRCDIR)/compiler.h

.PHONY: all test valgrind clean test_stage1 test_stage2 test_stage3 valgrind_stage1 valgrind_stage2 valgrind_stage3

all: test_vm test_lantern test_compiler

# Stage 1
test_vm: test_vm.c $(STAGE1_SRCS) $(STAGE1_HDRS)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ test_vm.c $(STAGE1_SRCS)

test_stage1: test_vm
	./test_vm

valgrind_stage1: test_vm
	valgrind --leak-check=full --error-exitcode=1 ./test_vm

# Stage 2
test_lantern: test_lantern.c $(STAGE2_SRCS) $(STAGE2_HDRS)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ test_lantern.c $(STAGE2_SRCS) -lm

test_stage2: test_lantern
	./test_lantern

valgrind_stage2: test_lantern
	valgrind --leak-check=full --error-exitcode=1 ./test_lantern

# Stage 3
test_compiler: test_compiler.c $(STAGE3_SRCS) $(STAGE3_HDRS)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ test_compiler.c $(STAGE3_SRCS) -lm

test_stage3: test_compiler
	./test_compiler

valgrind_stage3: test_compiler
	valgrind --leak-check=full --error-exitcode=1 ./test_compiler

# Convenience targets
test: test_stage1 test_stage2 test_stage3

valgrind: valgrind_stage1 valgrind_stage2 valgrind_stage3

clean:
	rm -f test_vm test_lantern test_compiler
