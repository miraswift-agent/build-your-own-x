/*
 * Lantern VM — Stage 2: Execution Engine
 *
 * The fetch-decode-execute cycle, now with typed values,
 * variable-length instructions, and stack frames.
 *
 * "What I cannot execute, I cannot become." — Mira
 */

#include "lantern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* — Error strings — */

const char* lvm_error_string(LVMError err) {
    switch (err) {
    case LVM_OK:                      return "OK";
    case LVM_ERR_STACK_OVERFLOW:       return "Stack overflow";
    case LVM_ERR_STACK_UNDERFLOW:      return "Stack underflow";
    case LVM_ERR_TYPE_ERROR:          return "Type error";
    case LVM_ERR_DIVISION_BY_ZERO:    return "Division by zero";
    case LVM_ERR_UNDEFINED_LOCAL:     return "Undefined local variable";
    case LVM_ERR_INVALID_OPCODE:      return "Invalid opcode";
    case LVM_ERR_CALL_DEPTH_EXCEEDED: return "Call depth exceeded";
    case LVM_ERR_ARITY_MISMATCH:     return "Arity mismatch";
    case LVM_ERR_RUNTIME:             return "Runtime error";
    default:                          return "Unknown error";
    }
}

/* — Stack helpers — */

static inline bool stack_push(LanternVM *vm, Value v) {
    if (vm->stack_top >= vm->stack + LANTERN_STACK_MAX) {
        vm->error = LVM_ERR_STACK_OVERFLOW;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Stack overflow");
        vm->running = false;
        return false;
    }
    *vm->stack_top = v;
    vm->stack_top++;
    return true;
}

static inline Value stack_pop(LanternVM *vm) {
    if (vm->stack_top <= vm->frames[vm->frame_count - 1].stack_base) {
        vm->error = LVM_ERR_STACK_UNDERFLOW;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Stack underflow");
        vm->running = false;
        return NULL_VAL;
    }
    vm->stack_top--;
    return *vm->stack_top;
}

static inline Value stack_peek(LanternVM *vm, int dist) {
    return vm->stack_top[-1 - dist];
}

/* — Reading instruction operands — */

static inline uint8_t read_byte(CallFrame *frame) {
    uint8_t b = *frame->ip;
    frame->ip++;
    return b;
}

static inline uint16_t read_short(CallFrame *frame) {
    uint16_t lo = *frame->ip;
    uint16_t hi = *(frame->ip + 1);
    frame->ip += 2;
    return lo | (hi << 8);
}

static inline int16_t read_signed_short(CallFrame *frame) {
    uint16_t raw = read_short(frame);
    return (int16_t)raw;
}

static inline int32_t read_int32(CallFrame *frame) {
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
        val |= ((uint32_t)(*frame->ip)) << (8 * i);
        frame->ip++;
    }
    return (int32_t)val;
}

static inline double read_float64(CallFrame *frame) {
    double val;
    memcpy(&val, frame->ip, 8);
    frame->ip += 8;
    return val;
}

/* — Type error helper — */

static LVMError type_error(LanternVM *vm, const char *op, ValType got_a, ValType got_b) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: cannot %s %s and %s", op,
             value_type_name(got_a), value_type_name(got_b));
    vm->running = false;
    return LVM_ERR_TYPE_ERROR;
}

static LVMError type_error_unary(LanternVM *vm, const char *op, ValType got) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: cannot %s %s", op, value_type_name(got));
    vm->running = false;
    return LVM_ERR_TYPE_ERROR;
}

/* — VM Lifecycle — */

LanternVM *lvm_new(void) {
    LanternVM *vm = calloc(1, sizeof(LanternVM));
    if (!vm) return NULL;
    lvm_reset(vm);
    return vm;
}

void lvm_free(LanternVM *vm) {
    free(vm);
}

void lvm_reset(LanternVM *vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->func_count = 0;
    vm->error = LVM_OK;
    vm->running = false;
    vm->error_msg[0] = '\0';
}

int lvm_add_function(LanternVM *vm, Chunk *chunk) {
    if (vm->func_count >= LANTERN_FUNCS_MAX) {
        return -1;
    }
    int idx = vm->func_count;
    vm->functions[idx] = chunk;
    vm->func_count++;
    return idx;
}

/* — Execution — */

LVMError lvm_run(LanternVM *vm) {
    vm->running = true;
    vm->error = LVM_OK;
    vm->error_msg[0] = '\0';

    /* Set up initial frame from function 0 (main) */
    if (vm->func_count == 0) {
        vm->error = LVM_ERR_RUNTIME;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "No functions loaded");
        return vm->error;
    }

    Chunk *main_chunk = vm->functions[0];
    CallFrame *frame = &vm->frames[0];
    frame->chunk = main_chunk;
    frame->ip = main_chunk->code;
    frame->stack_base = vm->stack;
    vm->frame_count = 1;

    /* Initialize locals to null */
    for (int i = 0; i < main_chunk->local_count; i++) {
        vm->stack[i] = NULL_VAL;
    }
    vm->stack_top = vm->stack + main_chunk->local_count;

    while (vm->running) {
        LVMError err = lvm_step(vm);
        if (err != LVM_OK) return err;
    }
    return LVM_OK;
}

LVMError lvm_step(LanternVM *vm) {
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
    Chunk *chunk = frame->chunk;

    /* Bounds check */
    if (frame->ip < chunk->code || frame->ip >= chunk->code + chunk->code_size) {
        if (frame->ip == chunk->code + chunk->code_size) {
            /* Fell off the end — implicit return null */
            if (vm->frame_count == 1) {
                vm->running = false;
                return LVM_OK;
            }
        }
        vm->error = LVM_ERR_RUNTIME;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "IP out of bounds");
        vm->running = false;
        return vm->error;
    }

    uint8_t opcode = read_byte(frame);

    switch (opcode) {

    /* ===== Control ===== */
    case OP_HALT:
        vm->running = false;
        return LVM_OK;

    /* ===== Constants ===== */
    case OP_CONST: {
        uint16_t idx = read_short(frame);
        if (idx >= (uint16_t)chunk->const_size) {
            vm->error = LVM_ERR_RUNTIME;
            snprintf(vm->error_msg, sizeof(vm->error_msg),
                     "Constant index %d out of range (max %d)", idx, chunk->const_size);
            vm->running = false;
            return vm->error;
        }
        if (!stack_push(vm, chunk->constants[idx])) return vm->error;
        break;
    }
    case OP_INT8: {
        int8_t val = (int8_t)read_byte(frame);
        if (!stack_push(vm, INT_VAL(val))) return vm->error;
        break;
    }
    case OP_INT32: {
        int32_t val = read_int32(frame);
        if (!stack_push(vm, INT_VAL((int64_t)val))) return vm->error;
        break;
    }
    case OP_FLOAT64: {
        double val = read_float64(frame);
        if (!stack_push(vm, FLOAT_VAL(val))) return vm->error;
        break;
    }
    case OP_TRUE:
        if (!stack_push(vm, BOOL_VAL(true))) return vm->error;
        break;
    case OP_FALSE:
        if (!stack_push(vm, BOOL_VAL(false))) return vm->error;
        break;
    case OP_NULL:
        if (!stack_push(vm, NULL_VAL)) return vm->error;
        break;

    /* ===== Stack ===== */
    case OP_POP:
        stack_pop(vm);
        if (vm->error != LVM_OK) return vm->error;
        break;
    case OP_DUP: {
        Value v = stack_peek(vm, 0);
        if (!stack_push(vm, v)) return vm->error;
        break;
    }

    /* ===== Local Variables ===== */
    case OP_LOAD_LOCAL: {
        uint8_t slot = read_byte(frame);
        Value *base = frame->stack_base;
        if (slot >= (uint8_t)chunk->local_count) {
            /* Allow reading beyond declared locals if they're on the stack */
            /* This is fine for stack slots that were pushed */
        }
        if (!stack_push(vm, base[slot])) return vm->error;
        break;
    }
    case OP_STORE_LOCAL: {
        uint8_t slot = read_byte(frame);
        Value val = stack_pop(vm);
        if (vm->error != LVM_OK) return vm->error;
        frame->stack_base[slot] = val;
        break;
    }

    /* ===== Arithmetic ===== */
    case OP_ADD: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if (a.type != VAL_INT && a.type != VAL_FLOAT) return type_error(vm, "add", b.type, a.type);
        if (b.type != VAL_INT && b.type != VAL_FLOAT) return type_error(vm, "add", b.type, a.type);
        Value result = value_add(a, b);
        if (!stack_push(vm, result)) return vm->error;
        break;
    }
    case OP_SUBTRACT: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if (a.type != VAL_INT && a.type != VAL_FLOAT) return type_error(vm, "subtract", b.type, a.type);
        if (b.type != VAL_INT && b.type != VAL_FLOAT) return type_error(vm, "subtract", b.type, a.type);
        Value result = value_subtract(a, b);
        if (!stack_push(vm, result)) return vm->error;
        break;
    }
    case OP_MULTIPLY: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if (a.type != VAL_INT && a.type != VAL_FLOAT) return type_error(vm, "multiply", b.type, a.type);
        if (b.type != VAL_INT && b.type != VAL_FLOAT) return type_error(vm, "multiply", b.type, a.type);
        Value result = value_multiply(a, b);
        if (!stack_push(vm, result)) return vm->error;
        break;
    }
    case OP_DIVIDE: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if (a.type != VAL_INT && a.type != VAL_FLOAT) return type_error(vm, "divide", b.type, a.type);
        if (b.type != VAL_INT && b.type != VAL_FLOAT) return type_error(vm, "divide", b.type, a.type);
        /* Check for division by zero — divisor is b (second from top) in our convention:
         * push dividend, push divisor, div → dividend / divisor.
         * value_divide(a, b) computes a / b, so divisor is b. */
        if ((b.type == VAL_INT && b.as.integer == 0) ||
            (b.type == VAL_FLOAT && b.as.floating == 0.0)) {
            vm->error = LVM_ERR_DIVISION_BY_ZERO;
            snprintf(vm->error_msg, sizeof(vm->error_msg), "Division by zero");
            vm->running = false;
            return vm->error;
        }
        Value result = value_divide(a, b);
        if (!stack_push(vm, result)) return vm->error;
        break;
    }
    case OP_MODULO: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if (a.type != VAL_INT && a.type != VAL_FLOAT) return type_error(vm, "modulo", b.type, a.type);
        if (b.type != VAL_INT && b.type != VAL_FLOAT) return type_error(vm, "modulo", b.type, a.type);
        if ((b.type == VAL_INT && b.as.integer == 0) ||
            (b.type == VAL_FLOAT && b.as.floating == 0.0)) {
            vm->error = LVM_ERR_DIVISION_BY_ZERO;
            snprintf(vm->error_msg, sizeof(vm->error_msg), "Modulo by zero");
            vm->running = false;
            return vm->error;
        }
        Value result = value_modulo(a, b);
        if (!stack_push(vm, result)) return vm->error;
        break;
    }
    case OP_NEGATE: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if (a.type != VAL_INT && a.type != VAL_FLOAT) return type_error_unary(vm, "negate", a.type);
        Value result = value_negate(a);
        if (!stack_push(vm, result)) return vm->error;
        break;
    }

    /* ===== Comparison ===== */
    case OP_EQUAL: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if (!stack_push(vm, BOOL_VAL(value_equal(b, a)))) return vm->error;
        break;
    }
    case OP_NOT_EQUAL: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if (!stack_push(vm, BOOL_VAL(!value_equal(b, a)))) return vm->error;
        break;
    }
    case OP_LESS: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if ((a.type != VAL_INT && a.type != VAL_FLOAT) ||
            (b.type != VAL_INT && b.type != VAL_FLOAT))
            return type_error(vm, "compare <", b.type, a.type);
        /* Coerce for comparison */
        if (b.type == VAL_INT && a.type == VAL_INT) {
            if (!stack_push(vm, BOOL_VAL(b.as.integer < a.as.integer))) return vm->error;
        } else {
            double ba = (b.type == VAL_FLOAT) ? b.as.floating : (double)b.as.integer;
            double aa = (a.type == VAL_FLOAT) ? a.as.floating : (double)a.as.integer;
            if (!stack_push(vm, BOOL_VAL(ba < aa))) return vm->error;
        }
        break;
    }
    case OP_LESS_EQUAL: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if ((a.type != VAL_INT && a.type != VAL_FLOAT) ||
            (b.type != VAL_INT && b.type != VAL_FLOAT))
            return type_error(vm, "compare <=", b.type, a.type);
        if (b.type == VAL_INT && a.type == VAL_INT) {
            if (!stack_push(vm, BOOL_VAL(b.as.integer <= a.as.integer))) return vm->error;
        } else {
            double ba = (b.type == VAL_FLOAT) ? b.as.floating : (double)b.as.integer;
            double aa = (a.type == VAL_FLOAT) ? a.as.floating : (double)a.as.integer;
            if (!stack_push(vm, BOOL_VAL(ba <= aa))) return vm->error;
        }
        break;
    }
    case OP_GREATER: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if ((a.type != VAL_INT && a.type != VAL_FLOAT) ||
            (b.type != VAL_INT && b.type != VAL_FLOAT))
            return type_error(vm, "compare >", b.type, a.type);
        if (b.type == VAL_INT && a.type == VAL_INT) {
            if (!stack_push(vm, BOOL_VAL(b.as.integer > a.as.integer))) return vm->error;
        } else {
            double ba = (b.type == VAL_FLOAT) ? b.as.floating : (double)b.as.integer;
            double aa = (a.type == VAL_FLOAT) ? a.as.floating : (double)a.as.integer;
            if (!stack_push(vm, BOOL_VAL(ba > aa))) return vm->error;
        }
        break;
    }
    case OP_GREATER_EQUAL: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        Value b = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if ((a.type != VAL_INT && a.type != VAL_FLOAT) ||
            (b.type != VAL_INT && b.type != VAL_FLOAT))
            return type_error(vm, "compare >=", b.type, a.type);
        if (b.type == VAL_INT && a.type == VAL_INT) {
            if (!stack_push(vm, BOOL_VAL(b.as.integer >= a.as.integer))) return vm->error;
        } else {
            double ba = (b.type == VAL_FLOAT) ? b.as.floating : (double)b.as.integer;
            double aa = (a.type == VAL_FLOAT) ? a.as.floating : (double)a.as.integer;
            if (!stack_push(vm, BOOL_VAL(ba >= aa))) return vm->error;
        }
        break;
    }

    /* ===== Logic ===== */
    case OP_NOT: {
        Value a = stack_pop(vm); if (vm->error != LVM_OK) return vm->error;
        if (!stack_push(vm, value_not(a))) return vm->error;
        break;
    }

    /* ===== Control Flow ===== */
    case OP_JUMP: {
        int16_t offset = read_signed_short(frame);
        frame->ip += offset;
        break;
    }
    case OP_JUMP_IF_FALSE: {
        int16_t offset = read_signed_short(frame);
        Value cond = stack_pop(vm);
        if (vm->error != LVM_OK) return vm->error;
        if (!value_is_truthy(cond)) {
            frame->ip += offset;
        }
        break;
    }
    case OP_JUMP_IF_TRUE: {
        int16_t offset = read_signed_short(frame);
        Value cond = stack_pop(vm);
        if (vm->error != LVM_OK) return vm->error;
        if (value_is_truthy(cond)) {
            frame->ip += offset;
        }
        break;
    }
    case OP_LOOP: {
        int16_t offset = read_signed_short(frame);
        frame->ip += offset;  /* offset is negative, so this jumps back */
        break;
    }

    /* ===== Functions ===== */
    case OP_CALL: {
        uint16_t func_idx = read_short(frame);
        uint8_t argc = read_byte(frame);

        if (func_idx >= (uint16_t)vm->func_count) {
            vm->error = LVM_ERR_RUNTIME;
            snprintf(vm->error_msg, sizeof(vm->error_msg),
                     "Call to undefined function index %d", func_idx);
            vm->running = false;
            return vm->error;
        }

        Chunk *callee = vm->functions[func_idx];

        if (argc != (uint8_t)callee->arity) {
            vm->error = LVM_ERR_ARITY_MISMATCH;
            snprintf(vm->error_msg, sizeof(vm->error_msg),
                     "Function %s expects %d args, got %d",
                     callee->name ? callee->name : "?", callee->arity, argc);
            vm->running = false;
            return vm->error;
        }

        if (vm->frame_count >= LANTERN_FRAMES_MAX) {
            vm->error = LVM_ERR_CALL_DEPTH_EXCEEDED;
            snprintf(vm->error_msg, sizeof(vm->error_msg), "Call depth exceeded (max %d)", LANTERN_FRAMES_MAX);
            vm->running = false;
            return vm->error;
        }

        /* Arguments are on the stack. The new frame's base starts at
         * stack_top - argc, so that locals[0..argc-1] = arguments */
        Value *new_base = vm->stack_top - argc;

        /* Set up the new frame */
        CallFrame *new_frame = &vm->frames[vm->frame_count];
        new_frame->chunk = callee;
        new_frame->ip = callee->code;
        new_frame->stack_base = new_base;
        vm->frame_count++;

        /* Initialize remaining locals to null */
        for (int i = argc; i < callee->local_count; i++) {
            if (!stack_push(vm, NULL_VAL)) return vm->error;
        }

        break;
    }
    case OP_RETURN: {
        Value return_val = stack_pop(vm);
        if (vm->error != LVM_OK) return vm->error;

        /* Close current frame */
        Value *old_base = frame->stack_base;
        vm->frame_count--;

        if (vm->frame_count == 0) {
            /* Returning from main — halt */
            vm->running = false;
            return LVM_OK;
        }

        /* Restore previous frame */
        frame = &vm->frames[vm->frame_count - 1];
        vm->stack_top = old_base;  /* Discard all locals from the returning frame */

        /* Push return value */
        if (!stack_push(vm, return_val)) return vm->error;
        break;
    }

    /* ===== Built-in ===== */
    case OP_PRINT: {
        Value v = stack_pop(vm);
        if (vm->error != LVM_OK) return vm->error;
        value_print(v);
        break;
    }
    case OP_PRINT_LN: {
        Value v = stack_pop(vm);
        if (vm->error != LVM_OK) return vm->error;
        value_println(v);
        break;
    }

    default:
        vm->error = LVM_ERR_INVALID_OPCODE;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "Invalid opcode 0x%02X at offset %td",
                 opcode, (ptrdiff_t)(frame->ip - 1 - chunk->code));
        vm->running = false;
        return vm->error;
    }

    return LVM_OK;
}