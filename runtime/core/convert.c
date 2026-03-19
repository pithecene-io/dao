// convert.c — Dao runtime conversion hooks.
//
// Implements: scalar-to-string, numeric type conversions
// Authority:  docs/contracts/CONTRACT_RUNTIME_ABI.md
//
// String conversion results use thread-local transient buffers.
// Numeric conversions trap (abort) on out-of-range or invalid inputs.

#include "dao_abi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

struct dao_string __dao_conv_i8_to_string(int8_t x) {
  static _Thread_local char buf[8];
  int len = snprintf(buf, sizeof(buf), "%d", (int)x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_i16_to_string(int16_t x) {
  static _Thread_local char buf[8];
  int len = snprintf(buf, sizeof(buf), "%d", (int)x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_i32_to_string(int32_t x) {
  static _Thread_local char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d", x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_i64_to_string(int64_t x) {
  static _Thread_local char buf[32];
  int len = snprintf(buf, sizeof(buf), "%lld", (long long)x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_u8_to_string(uint8_t x) {
  static _Thread_local char buf[8];
  int len = snprintf(buf, sizeof(buf), "%u", (unsigned)x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_u16_to_string(uint16_t x) {
  static _Thread_local char buf[8];
  int len = snprintf(buf, sizeof(buf), "%u", (unsigned)x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_u32_to_string(uint32_t x) {
  static _Thread_local char buf[16];
  int len = snprintf(buf, sizeof(buf), "%u", x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_u64_to_string(uint64_t x) {
  static _Thread_local char buf[32];
  int len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_f32_to_string(float x) {
  static _Thread_local char buf[64];
  int len = snprintf(buf, sizeof(buf), "%g", (double)x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_f64_to_string(double x) {
  static _Thread_local char buf[64];
  int len = snprintf(buf, sizeof(buf), "%g", x);
  return (struct dao_string){.ptr = buf, .len = len};
}

struct dao_string __dao_conv_bool_to_string(bool x) {
  if (x) {
    return (struct dao_string){.ptr = "true", .len = 4};
  }
  return (struct dao_string){.ptr = "false", .len = 5};
}

// ---------------------------------------------------------------------------
// Numeric type conversions
// ---------------------------------------------------------------------------

// i32 -> f64: exact (all i32 values are representable in f64).
double __dao_conv_i32_to_f64(int32_t x) { return (double)x; }

// i32 -> i64: exact (lossless widening).
int64_t __dao_conv_i32_to_i64(int32_t x) { return (int64_t)x; }

// f64 -> i32: truncates toward zero. Traps on NaN, Inf, or out-of-range.
int32_t __dao_conv_f64_to_i32(double x) {
  if (isnan(x) || isinf(x) || x < -2147483648.0 || x > 2147483647.0) {
    fprintf(stderr, "dao: numeric conversion error: f64 value out of i32 range\n");
    abort();
  }
  return (int32_t)x;
}

// i64 -> i32: traps if value does not fit.
int32_t __dao_conv_i64_to_i32(int64_t x) {
  if (x < INT32_MIN || x > INT32_MAX) {
    fprintf(stderr, "dao: numeric conversion error: i64 value out of i32 range\n");
    abort();
  }
  return (int32_t)x;
}
