CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O0
SRCDIR = ../src

# Stage 1 (Chip-8)
STAGE1_SRCS = $(SRCDIR)/vm.c $(SRCDIR)/debug.c
STAGE1_HDRS = $(SRCDIR)/vm.h

# Stage 2 (Lantern Bytecode)
STAGE2_SRCS = $(SRCDIR)/lantern.c $(SRCDIR)/value.c $(SRCDIR)/chunk.c $(SRCDIR)/disasm.c $(SRCDIR)/assemble.c
STAGE2_HDRS = $(SRCDIR)/lantern.h

.PHONY: all test valgrind clean test_stage1 test_stage2 valgrind_stage1 valgrind_stage2

all: test_vm test_lantern

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

# Convenience targets
test: test_stage1 test_stage2

valgrind: valgrind_stage1 valgrind_stage2

clean:
	rm -f test_vm test_lantern