CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g -O0

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

.PHONY: all test test-clox test-repl valgrind clean run run-repl

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

# Generic rule: bin/test_<name> <- tests/test_<name>.c + all library objects.
$(BINDIR)/test_%: $(LIB_OBJECTS) $(BUILDDIR)/test_%.o | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(LIB_OBJECTS) $(BUILDDIR)/test_$*.o

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

valgrind: $(TEST_BINARIES)
	valgrind --leak-check=full --error-exitcode=1 ./$(BINDIR)/test_clox
	valgrind --leak-check=full --error-exitcode=1 ./$(BINDIR)/test_repl

run: $(TARGET)
	./$(TARGET) $(FILE)

run-repl: $(TARGET)
	./$(TARGET) --repl

clean:
	rm -rf $(BUILDDIR) $(BINDIR)
