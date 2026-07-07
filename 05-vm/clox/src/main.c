/*
 * clox — Command-line entry point
 *
 * Usage:
 *   clox [path]        run a script file
 *   clox --repl        interactive prompt reading from stdin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "vm.h"

static char* readFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = (size_t)ftell(file);
    rewind(file);

    char *buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

static void repl(void) {
    char line[1024];

    for (;;) {
        printf("> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        interpret(line);
    }
}

static int runFile(const char *path) {
    char *source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
    return 0;
}

int main(int argc, const char *argv[]) {
    initVM();

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--repl") == 0)) {
        repl();
    } else if (argc == 2) {
        int code = runFile(argv[1]);
        freeVM();
        return code;
    } else {
        fprintf(stderr, "Usage: clox [path] [--repl]\n");
        freeVM();
        exit(64);
    }

    freeVM();
    return 0;
}
