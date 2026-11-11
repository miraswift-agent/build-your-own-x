CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g -O0

SRCDIR = src
TESTDIR = tests
BUILDDIR = build

SOURCES = $(wildcard $(SRCDIR)/*.c)
TEST_SOURCES = $(wildcard $(TESTDIR)/*.c)
# Each tests/test_*.c becomes its own binary in build/. A new stage drops in a
# new test file and inherits the runner.
TEST_BINARIES = $(patsubst $(TESTDIR)/test_%.c,$(BUILDDIR)/%,$(TEST_SOURCES))

.PHONY: all test valgrind clean

all: $(TEST_BINARIES)

# Each tests/test_*.c becomes its own binary, linked against the component.
# A new stage drops in a new test file and inherits the runner.
$(BUILDDIR)/%: $(SOURCES) $(TESTDIR)/test_%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(SOURCES) $(TESTDIR)/test_$*.c -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

test: $(TEST_BINARIES)
	@status=0; \
	for t in $(TEST_BINARIES); do \
		echo "==> $$t"; \
		./$$t || status=$$?; \
	done; \
	echo; \
	echo "==> All test binaries done (exit status: $$status)"; \
	exit $$status

valgrind: $(TEST_BINARIES)
	@status=0; \
	for t in $(TEST_BINARIES); do \
		echo "==> valgrind $$t"; \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./$$t || status=$$?; \
	done; \
	exit $$status

clean:
	rm -rf $(BUILDDIR)
