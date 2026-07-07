CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g -O0

SRCDIR = src
TESTDIR = tests
BUILDDIR = build
BINDIR = bin

TARGET = $(BINDIR)/lox
TEST_TARGET = $(BINDIR)/test_lox

SOURCES = $(wildcard $(SRCDIR)/*.c)
# main.c is only for the CLI binary, not the test binary.
LIB_SOURCES = $(filter-out $(SRCDIR)/main.c, $(SOURCES))
TEST_SOURCES = $(wildcard $(TESTDIR)/*.c)

OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))
LIB_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(LIB_SOURCES))
TEST_OBJECTS = $(patsubst $(TESTDIR)/%.c,$(BUILDDIR)/test_%.o,$(TEST_SOURCES))

.PHONY: all test valgrind clean run

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

$(TEST_TARGET): $(LIB_OBJECTS) $(TEST_OBJECTS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(LIB_OBJECTS) $(TEST_OBJECTS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/test_%.o: $(TESTDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR):
	mkdir -p $(BINDIR)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

test: $(TARGET) $(TEST_TARGET)
	./$(TEST_TARGET)

valgrind: $(TEST_TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_TARGET)

run: $(TARGET)
	./$(TARGET) $(FILE)

clean:
	rm -rf $(BUILDDIR) $(BINDIR)
