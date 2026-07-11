CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g -O0
LDLIBS = -lm

SRCDIR = src
TESTDIR = tests
BUILDDIR = build
BINDIR = bin

TARGET = $(BINDIR)/clox

# Each tests/test_*.c becomes its own binary at bin/test_<stem>. Each
# binary is linked against the library (everything in src/ except main.c).
TEST_BINARIES = $(patsubst $(TESTDIR)/%.c,$(BINDIR)/%,$(TEST_SOURCES))

SOURCES = $(wildcard $(SRCDIR)/*.c)
LIB_SOURCES = $(filter-out $(SRCDIR)/main.c, $(SOURCES))
TEST_SOURCES = $(wildcard $(TESTDIR)/*.c)

OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o, $(SOURCES))
LIB_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o, $(LIB_SOURCES))
TEST_OBJECTS = $(patsubst $(TESTDIR)/%.c,$(BUILDDIR)/test_%.o, $(TEST_SOURCES))

.PHONY: all test test-clox test-repl test-stdlib test-composability valgrind clean run run-repl examples

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

# Generic rule: bin/test_<name> <- tests/test_<name>.c + all library objects.
$(BINDIR)/test_%: $(LIB_OBJECTS) $(BUILDDIR)/test_%.o | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(LIB_OBJECTS) $(BUILDDIR)/test_$*.o $(LDLIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Object files for test binaries: build/test_clox.o from tests/test_clox.c
# (and similarly for any other tests/test_X.c file).
$(BUILDDIR)/test_%.o: $(TESTDIR)/test_%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR):
	mkdir -p $(BINDIR)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Run every test binary in sequence. Print a header per binary so failures
# are easy to attribute. Exit nonzero if any binary returns nonzero.
test: $(TARGET) $(TEST_BINARIES)
	@status=0; \
	for t in $(TEST_BINARIES); do \
		echo "==> $$t"; \
		./$$t || status=$$?; \
	done; \
	echo; \
	echo "==> All test binaries done (exit status: $$status)"; \
	exit $$status

test-clox: $(BINDIR)/test_clox
	./$(BINDIR)/test_clox

test-repl: $(BINDIR)/test_repl
	./$(BINDIR)/test_repl

test-stdlib: $(BINDIR)/test_stdlib
	./$(BINDIR)/test_stdlib

test-composability: $(BINDIR)/test_composability
	./$(BINDIR)/test_composability

valgrind: $(TEST_BINARIES)
	@status=0; \
	for t in $(TEST_BINARIES); do \
		echo "==> valgrind $$t"; \
		valgrind --leak-check=full --error-exitcode=1 ./$$t || status=$$?; \
	done; \
	exit $$status

run: $(TARGET)
	./$(TARGET) $(FILE)

run-repl: $(TARGET)
	./$(TARGET) --repl

# Smoke-test the example programs. Each example reads a file from CWD,
# so the rule generates a sample input, runs the example, prints the
# output, and cleans up. The test passes if the example exits 0.
EXAMPLES = $(wildcard examples/*.lox)

examples: $(TARGET)
	@status=0; \
	for ex in $(EXAMPLES); do \
		echo "==> $$ex"; \
		case $$ex in \
			*wordcount*) \
				echo "the quick brown fox jumps over the lazy dog" > wordcount-input.txt; \
				./$(TARGET) $$ex; \
				rm -f wordcount-input.txt; \
				;; \
			*csv-roundtrip*) \
				echo "alpha,beta,gamma,delta" > csv-input.txt; \
				./$(TARGET) $$ex; \
				rm -f csv-input.txt csv-output.txt; \
				;; \
			*reverse-lines*) \
				printf "first\nsecond\nthird\nfourth\n" > lines-input.txt; \
				./$(TARGET) $$ex; \
				rm -f lines-input.txt lines-output.txt; \
				;; \
			*) \
				./$(TARGET) $$ex || status=$$?; \
				;; \
		esac; \
	done; \
	exit $$status

clean:
	rm -rf $(BUILDDIR) $(BINDIR)
