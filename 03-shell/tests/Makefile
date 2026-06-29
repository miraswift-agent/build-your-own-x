CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -g -O0 -I..
LDFLAGS =

SRC_DIR = ../src
SRCS    = tokenize.c parse.c builtin.c execute.c job.c signal.c

TARGET  = test_shell

.PHONY: all clean run

all: $(TARGET)

$(TARGET): test_shell.c
	$(CC) $(CFLAGS) -o $@ test_shell.c $(addprefix $(SRC_DIR)/,$(SRCS)) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)