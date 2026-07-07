/*
 * clox — Common definitions
 *
 * Shared macros and types used by the compiler, VM, and runtime.
 */

#ifndef CLOX_COMMON_H
#define CLOX_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define DEBUG_PRINT_CODE    0
#define DEBUG_TRACE_EXEC    0
#define DEBUG_STRESS_GC     0
#define DEBUG_LOG_GC        0

#define UINT8_COUNT         (UINT8_MAX + 1)
#define FRAMES_MAX           64

#endif /* CLOX_COMMON_H */
