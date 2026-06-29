# B-tree Storage Engine — Makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11 -D_GNU_SOURCE
LDFLAGS =

SRCDIR = src
OBJDIR = obj

SRCS = $(SRCDIR)/pager.c $(SRCDIR)/btree.c $(SRCDIR)/cursor.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

LIB = libbtree.a
TEST = tests/test_btree

.PHONY: all clean test valgrind

all: $(LIB)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIB): $(OBJS)
	ar rcs $@ $^

test: $(TEST)
	./$(TEST)

valgrind: $(TEST)
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST)

$(TEST): $(OBJS) tests/test_btree.c
	$(CC) $(CFLAGS) -o $@ tests/test_btree.c $(OBJS) $(LDFLAGS)

clean:
	rm -rf $(OBJDIR) $(LIB) $(TEST) tests/*.db