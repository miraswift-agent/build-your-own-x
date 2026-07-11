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

/* Stage 11: io_exit() flags defined in vm.c. main.c just consults
 * them on the way out. */
extern int g_exitRequested;
extern int g_exitCode;

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

/*
 * Walk the accumulated REPL input and return the number of unclosed
 * `{` and `(` (with negative values meaning more closing than opening,
 * which is impossible in valid Lox but we just treat as "ready to run").
 * Skips over string literals and line comments. clox has no block
 * comments and no array literals, so we don't track block-comment
 * regions or square brackets.
 */
static int openBraceAndParenCount(const char *source) {
    int braces = 0;
    int parens = 0;
    bool inString = false;
    bool inLineComment = false;
    for (const char *p = source; *p != '\0'; p++) {
        if (inLineComment) {
            if (*p == '\n') inLineComment = false;
            continue;
        }
        if (inString) {
            if (*p == '"') inString = false;
            continue;
        }
        if (*p == '"') { inString = true; continue; }
        if (*p == '/' && *(p + 1) == '/') {
            inLineComment = true;
            p++;
            continue;
        }
        if (*p == '{') braces++;
        else if (*p == '}') braces--;
        else if (*p == '(') parens++;
        else if (*p == ')') parens--;
    }
    return braces > 0 ? braces : (parens > 0 ? parens : 0);
}

static void repl(void) {
    /* Accumulator for the current REPL input. Bounded — 64 KiB is enough
     * for any interactive session; if the user pastes more, we warn and
     * discard. A static buffer avoids malloc churn. */
    static char source[65536];
    source[0] = '\0';
    char line[1024];

    for (;;) {
        /* Prompt: '> ' for a fresh statement, '| ' for a continuation
         * line. The choice tells the user the REPL is waiting for more. */
        printf(source[0] == '\0' ? "> " : "| ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        size_t curLen = strlen(source);
        size_t lineLen = strlen(line);
        if (curLen + lineLen + 1 >= sizeof(source)) {
            fprintf(stderr, "Input too long; discarding.\n");
            source[0] = '\0';
            continue;
        }
        memcpy(source + curLen, line, lineLen + 1);

        /* Keep reading until braces and parens balance. */
        if (openBraceAndParenCount(source) > 0) continue;

        /* Run. interpret() prints its own errors (compile + runtime) and
         * recovers from both via setjmp. Whether the run succeeded or
         * failed, the buffer is reset — a failed REPL input is discarded
         * because partial input is more confusing to recover from than
         * to start fresh. */
        interpret(source);
        source[0] = '\0';
    }
}

static int runFile(const char *path) {
    char *source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
    /* Stage 11: io_exit() takes precedence over both compile and
     * runtime errors. The point of an explicit exit is to honor
     * the user's request, even if it came after an error was logged. */
    if (g_exitRequested) exit(g_exitCode);
    return 0;
}

int main(int argc, const char *argv[]) {
    initVM();

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--repl") == 0)) {
        repl();
        if (g_exitRequested) {
            int code = g_exitCode;
            freeVM();
            return code;
        }
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
