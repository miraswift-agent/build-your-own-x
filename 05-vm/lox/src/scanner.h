#ifndef LOX_SCANNER_H
#define LOX_SCANNER_H

#include "token.h"

typedef struct {
    const char* start;
    const char* current;
    int line;
} Scanner;

void scannerInit(Scanner* scanner, const char* source);
Token scannerScanToken(Scanner* scanner);

#endif /* LOX_SCANNER_H */
