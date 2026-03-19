// equality.c — Dao runtime equality hooks.
//
// Implements: __dao_eq_* for all numeric types, bool, and string
// Authority:  docs/contracts/CONTRACT_RUNTIME_ABI.md

#include "dao_abi.h"

#include <string.h>

bool __dao_eq_i8(int8_t a, int8_t b)   { return a == b; }
bool __dao_eq_i16(int16_t a, int16_t b) { return a == b; }
bool __dao_eq_i32(int32_t a, int32_t b) { return a == b; }
bool __dao_eq_i64(int64_t a, int64_t b) { return a == b; }

bool __dao_eq_u8(uint8_t a, uint8_t b)   { return a == b; }
bool __dao_eq_u16(uint16_t a, uint16_t b) { return a == b; }
bool __dao_eq_u32(uint32_t a, uint32_t b) { return a == b; }
bool __dao_eq_u64(uint64_t a, uint64_t b) { return a == b; }

bool __dao_eq_f32(float a, float b) { return a == b; }
bool __dao_eq_f64(double a, double b) { return a == b; }

bool __dao_eq_bool(bool a, bool b) { return a == b; }

bool __dao_eq_string(const struct dao_string *a, const struct dao_string *b) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  if (a->len != b->len) {
    return false;
  }
  if (a->ptr == b->ptr) {
    return true;
  }
  return memcmp(a->ptr, b->ptr, (size_t)a->len) == 0;
}
