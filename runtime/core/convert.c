// convert.c — Dao runtime scalar-to-string conversion hooks.
//
// Implements: __dao_conv_i32_to_string, __dao_conv_f64_to_string,
//             __dao_conv_bool_to_string
// Authority:  docs/contracts/CONTRACT_RUNTIME_ABI.md
//
// Conversion results use thread-local transient buffers. The returned
// dao_string is valid until the next call to the same hook on the
// same thread. Callers must consume or copy before re-calling.

#include "dao_abi.h"

#include <stdio.h>

struct dao_string __dao_conv_i32_to_string(int32_t x) {
  static _Thread_local char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d", x);
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
