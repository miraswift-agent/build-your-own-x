/*
 * Lantern VM — Value Operations
 *
 * Typed values: int64, float64, bool, null.
 * Every operation checks types and produces clear errors.
 */

#include "lantern.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

/* — Type name — */

const char* value_type_name(ValType type) {
    switch (type) {
    case VAL_INT:   return "int";
    case VAL_FLOAT: return "float";
    case VAL_BOOL:  return "bool";
    case VAL_NULL:  return "null";
    default:        return "unknown";
    }
}

/* — Truthiness — */

bool value_is_truthy(Value v) {
    switch (v.type) {
    case VAL_INT:   return v.as.integer != 0;
    case VAL_FLOAT: return v.as.floating != 0.0;
    case VAL_BOOL:  return v.as.boolean;
    case VAL_NULL:  return false;
    default:        return false;
    }
}

/* — Equality — */

bool value_equal(Value a, Value b) {
    if (a.type != b.type) {
        /* Allow int/float comparison */
        if (a.type == VAL_INT && b.type == VAL_FLOAT) {
            return (double)a.as.integer == b.as.floating;
        }
        if (a.type == VAL_FLOAT && b.type == VAL_INT) {
            return a.as.floating == (double)b.as.integer;
        }
        return false;
    }
    switch (a.type) {
    case VAL_INT:   return a.as.integer == b.as.integer;
    case VAL_FLOAT: return a.as.floating == b.as.floating;
    case VAL_BOOL:  return a.as.boolean == b.as.boolean;
    case VAL_NULL:  return true;  /* null == null */
    default:        return false;
    }
}

/* — Arithmetic — */

/* Helper: coerce two values for arithmetic. If mixed int/float, promote to float. */
static bool coerce_numeric(Value a, Value b, Value *out_a, Value *out_b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        *out_a = a;
        *out_b = b;
        return true;
    }
    if (a.type == VAL_FLOAT && b.type == VAL_FLOAT) {
        *out_a = a;
        *out_b = b;
        return true;
    }
    /* Mixed int/float: promote int to float */
    if (a.type == VAL_INT && b.type == VAL_FLOAT) {
        *out_a = FLOAT_VAL((double)a.as.integer);
        *out_b = b;
        return true;
    }
    if (a.type == VAL_FLOAT && b.type == VAL_INT) {
        *out_a = a;
        *out_b = FLOAT_VAL((double)b.as.integer);
        return true;
    }
    return false;
}

Value value_add(Value a, Value b) {
    Value ca, cb;
    if (!coerce_numeric(a, b, &ca, &cb)) {
        /* String concatenation or type error — for now, type error */
        return NULL_VAL;  /* Caller should check types */
    }
    if (ca.type == VAL_INT) {
        return INT_VAL(ca.as.integer + cb.as.integer);
    }
    return FLOAT_VAL(ca.as.floating + cb.as.floating);
}

Value value_subtract(Value a, Value b) {
    /* a - b: note stack order — b was pushed first, then a */
    Value ca, cb;
    if (!coerce_numeric(a, b, &ca, &cb)) return NULL_VAL;
    if (ca.type == VAL_INT) {
        return INT_VAL(ca.as.integer - cb.as.integer);
    }
    return FLOAT_VAL(ca.as.floating - cb.as.floating);
}

Value value_multiply(Value a, Value b) {
    Value ca, cb;
    if (!coerce_numeric(a, b, &ca, &cb)) return NULL_VAL;
    if (ca.type == VAL_INT) {
        return INT_VAL(ca.as.integer * cb.as.integer);
    }
    return FLOAT_VAL(ca.as.floating * cb.as.floating);
}

Value value_divide(Value a, Value b) {
    Value ca, cb;
    if (!coerce_numeric(a, b, &ca, &cb)) return NULL_VAL;
    /* Check for division by zero */
    if (cb.type == VAL_INT && cb.as.integer == 0) {
        return NULL_VAL;  /* Caller should check for error */
    }
    if (cb.type == VAL_FLOAT && cb.as.floating == 0.0) {
        return NULL_VAL;
    }
    if (ca.type == VAL_INT) {
        /* Integer division truncates toward zero (C99 semantics) */
        return INT_VAL(ca.as.integer / cb.as.integer);
    }
    return FLOAT_VAL(ca.as.floating / cb.as.floating);
}

Value value_modulo(Value a, Value b) {
    Value ca, cb;
    if (!coerce_numeric(a, b, &ca, &cb)) return NULL_VAL;
    if (cb.type == VAL_INT && cb.as.integer == 0) {
        return NULL_VAL;
    }
    if (cb.type == VAL_FLOAT && cb.as.floating == 0.0) {
        return NULL_VAL;
    }
    if (ca.type == VAL_INT) {
        return INT_VAL(ca.as.integer % cb.as.integer);
    }
    return FLOAT_VAL(fmod(ca.as.floating, cb.as.floating));
}

Value value_negate(Value v) {
    if (v.type == VAL_INT) {
        return INT_VAL(-v.as.integer);
    }
    if (v.type == VAL_FLOAT) {
        return FLOAT_VAL(-v.as.floating);
    }
    return NULL_VAL;
}

Value value_not(Value v) {
    return BOOL_VAL(!value_is_truthy(v));
}

/* — Printing — */

void value_print(Value v) {
    switch (v.type) {
    case VAL_INT:   printf("%" PRId64, v.as.integer); break;
    case VAL_FLOAT: printf("%g", v.as.floating); break;
    case VAL_BOOL:  printf("%s", v.as.boolean ? "true" : "false"); break;
    case VAL_NULL:  printf("null"); break;
    default:        printf("<unknown>"); break;
    }
}

void value_println(Value v) {
    value_print(v);
    printf("\n");
}