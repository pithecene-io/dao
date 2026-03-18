// equality.c — Dao runtime equality hooks.
//
// Implements: __dao_eq_i32, __dao_eq_i64, __dao_eq_f64, __dao_eq_bool, __dao_eq_string
// Authority:  docs/contracts/CONTRACT_RUNTIME_ABI.md

#include "dao_abi.h"

#include <string.h>

bool __dao_eq_i32(int32_t a, int32_t b) { return a == b; }

bool __dao_eq_i64(int64_t a, int64_t b) { return a == b; }

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
