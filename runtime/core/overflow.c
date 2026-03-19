// overflow.c — Dao runtime explicit overflow operation hooks.
//
// Implements: wrapping and saturating arithmetic for i8, i16, i32, i64.
// Authority:  docs/contracts/CONTRACT_NUMERIC_SEMANTICS.md §4.2
//
// Wrapping operations use unsigned arithmetic to avoid C signed
// overflow UB, then memcpy the result back to signed. The memcpy
// reinterpretation is well-defined in all C standards (unlike
// unsigned-to-signed casts, which are implementation-defined
// pre-C23).
//
// Saturating operations clamp to the type's min/max on overflow.

#include "dao_abi.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

// Helper: reinterpret unsigned bits as signed via memcpy.
// Well-defined in all C standards; the compiler optimizes this to a no-op.
static int8_t  u8_to_i8(uint8_t u)   { int8_t r;  memcpy(&r, &u, sizeof r); return r; }
static int16_t u16_to_i16(uint16_t u) { int16_t r; memcpy(&r, &u, sizeof r); return r; }
static int32_t u32_to_i32(uint32_t u) { int32_t r; memcpy(&r, &u, sizeof r); return r; }
static int64_t u64_to_i64(uint64_t u) { int64_t r; memcpy(&r, &u, sizeof r); return r; }

// ---------------------------------------------------------------------------
// Wrapping operations — two's complement wrap, no trap
// ---------------------------------------------------------------------------

int8_t __dao_wrapping_add_i8(int8_t a, int8_t b) {
  return u8_to_i8((uint8_t)a + (uint8_t)b);
}

int8_t __dao_wrapping_sub_i8(int8_t a, int8_t b) {
  return u8_to_i8((uint8_t)a - (uint8_t)b);
}

int8_t __dao_wrapping_mul_i8(int8_t a, int8_t b) {
  return u8_to_i8((uint8_t)a * (uint8_t)b);
}

int16_t __dao_wrapping_add_i16(int16_t a, int16_t b) {
  return u16_to_i16((uint16_t)a + (uint16_t)b);
}

int16_t __dao_wrapping_sub_i16(int16_t a, int16_t b) {
  return u16_to_i16((uint16_t)a - (uint16_t)b);
}

int16_t __dao_wrapping_mul_i16(int16_t a, int16_t b) {
  return u16_to_i16((uint16_t)a * (uint16_t)b);
}

int32_t __dao_wrapping_add_i32(int32_t a, int32_t b) {
  return u32_to_i32((uint32_t)a + (uint32_t)b);
}

int32_t __dao_wrapping_sub_i32(int32_t a, int32_t b) {
  return u32_to_i32((uint32_t)a - (uint32_t)b);
}

int32_t __dao_wrapping_mul_i32(int32_t a, int32_t b) {
  return u32_to_i32((uint32_t)a * (uint32_t)b);
}

int64_t __dao_wrapping_add_i64(int64_t a, int64_t b) {
  return u64_to_i64((uint64_t)a + (uint64_t)b);
}

int64_t __dao_wrapping_sub_i64(int64_t a, int64_t b) {
  return u64_to_i64((uint64_t)a - (uint64_t)b);
}

int64_t __dao_wrapping_mul_i64(int64_t a, int64_t b) {
  return u64_to_i64((uint64_t)a * (uint64_t)b);
}

// ---------------------------------------------------------------------------
// Saturating operations — clamp to min/max representable value
// ---------------------------------------------------------------------------

int8_t __dao_saturating_add_i8(int8_t a, int8_t b) {
  int32_t r = (int32_t)a + (int32_t)b;
  if (r > INT8_MAX) return INT8_MAX;
  if (r < INT8_MIN) return INT8_MIN;
  return (int8_t)r;
}

int8_t __dao_saturating_sub_i8(int8_t a, int8_t b) {
  int32_t r = (int32_t)a - (int32_t)b;
  if (r > INT8_MAX) return INT8_MAX;
  if (r < INT8_MIN) return INT8_MIN;
  return (int8_t)r;
}

int8_t __dao_saturating_mul_i8(int8_t a, int8_t b) {
  int32_t r = (int32_t)a * (int32_t)b;
  if (r > INT8_MAX) return INT8_MAX;
  if (r < INT8_MIN) return INT8_MIN;
  return (int8_t)r;
}

int16_t __dao_saturating_add_i16(int16_t a, int16_t b) {
  int32_t r = (int32_t)a + (int32_t)b;
  if (r > INT16_MAX) return INT16_MAX;
  if (r < INT16_MIN) return INT16_MIN;
  return (int16_t)r;
}

int16_t __dao_saturating_sub_i16(int16_t a, int16_t b) {
  int32_t r = (int32_t)a - (int32_t)b;
  if (r > INT16_MAX) return INT16_MAX;
  if (r < INT16_MIN) return INT16_MIN;
  return (int16_t)r;
}

int16_t __dao_saturating_mul_i16(int16_t a, int16_t b) {
  int32_t r = (int32_t)a * (int32_t)b;
  if (r > INT16_MAX) return INT16_MAX;
  if (r < INT16_MIN) return INT16_MIN;
  return (int16_t)r;
}

int32_t __dao_saturating_add_i32(int32_t a, int32_t b) {
  int64_t r = (int64_t)a + (int64_t)b;
  if (r > INT32_MAX) return INT32_MAX;
  if (r < INT32_MIN) return INT32_MIN;
  return (int32_t)r;
}

int32_t __dao_saturating_sub_i32(int32_t a, int32_t b) {
  int64_t r = (int64_t)a - (int64_t)b;
  if (r > INT32_MAX) return INT32_MAX;
  if (r < INT32_MIN) return INT32_MIN;
  return (int32_t)r;
}

int32_t __dao_saturating_mul_i32(int32_t a, int32_t b) {
  int64_t r = (int64_t)a * (int64_t)b;
  if (r > INT32_MAX) return INT32_MAX;
  if (r < INT32_MIN) return INT32_MIN;
  return (int32_t)r;
}

int64_t __dao_saturating_add_i64(int64_t a, int64_t b) {
  // Cannot widen to 128-bit portably, so use overflow detection.
  if (b > 0 && a > INT64_MAX - b) return INT64_MAX;
  if (b < 0 && a < INT64_MIN - b) return INT64_MIN;
  return a + b;
}

int64_t __dao_saturating_sub_i64(int64_t a, int64_t b) {
  if (b < 0 && a > INT64_MAX + b) return INT64_MAX;
  if (b > 0 && a < INT64_MIN + b) return INT64_MIN;
  return a - b;
}

int64_t __dao_saturating_mul_i64(int64_t a, int64_t b) {
  if (a == 0 || b == 0) return 0;
  // Check overflow by dividing back.
  if (a > 0) {
    if (b > 0) {
      if (a > INT64_MAX / b) return INT64_MAX;
    } else {
      if (b < INT64_MIN / a) return INT64_MIN;
    }
  } else {
    if (b > 0) {
      if (a < INT64_MIN / b) return INT64_MIN;
    } else {
      if (a < INT64_MAX / b) return INT64_MAX;
    }
  }
  return a * b;
}
