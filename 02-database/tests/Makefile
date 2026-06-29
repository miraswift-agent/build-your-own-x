CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11 -D_GNU_SOURCE -I../src
LDFLAGS =

SRCDIR = ../src
OBJDIR = ../obj

SRCS = $(SRCDIR)/pager.c $(SRCDIR)/btree.c $(SRCDIR)/cursor.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

test_btree: test_btree.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS) $(LDFLAGS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean valgrind

valgrind: test_btree
	valgrind --leak-check=full --error-exitcode=1 ./test_btree

clean:
	rm -f test_btree *.db