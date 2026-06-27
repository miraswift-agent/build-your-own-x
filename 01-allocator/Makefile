CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g -O0
# Note: ASan interferes with our sbrk-based allocator (non-contiguous regions).
# Use valgrind for memory safety testing instead.
SRC = src/malloc.c
TEST = tests/test_stage1.c
BUILD_DIR = build

.PHONY: all test clean valgrind

all: $(BUILD_DIR)/test_stage1

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test_stage1: $(SRC) $(TEST) $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) $(TEST) -o $@

test: $(BUILD_DIR)/test_stage1
	./$(BUILD_DIR)/test_stage1

valgrind: $(BUILD_DIR)/test_stage1
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		./$(BUILD_DIR)/test_stage1

clean:
	rm -rf $(BUILD_DIR)