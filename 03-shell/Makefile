CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -g -O0
LDFLAGS =
SRCDIR  = src
OBJDIR  = obj

SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

TARGET  = mira

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

test: $(TARGET)
	$(MAKE) -C tests
	cd tests && ./test_shell

valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET) -c "echo hello"

# Run shell interactively for testing
run: $(TARGET)
	./$(TARGET)