// Minimal Dao runtime — linked into every executable produced by daoc.
// This file provides C implementations for Dao builtins that the
// compiler emits as extern symbols.
//
// The calling convention for dao.string is: pointer to { i8*, i64 }
// (passed by pointer per the LLVM backend's string ABI).

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Matches the LLVM IR struct %dao.string = type { ptr, i64 }.
struct dao_string {
  const char *ptr;
  int64_t len;
};

// ---------------------------------------------------------------------------
// IO
// ---------------------------------------------------------------------------

// extern fn __write_stdout(msg: string): void
void __write_stdout(const struct dao_string *msg) {
  if (msg == NULL || msg->ptr == NULL || msg->len <= 0) {
    return;
  }

  const char *start = msg->ptr;
  int64_t len = msg->len;

  // String literals are stored without quotes by the compiler.
  if (len > 0) {
    fwrite(start, 1, (size_t)len, stdout);
  }
  fputc('\n', stdout);
}

// ---------------------------------------------------------------------------
// Equatable
// ---------------------------------------------------------------------------

// extern fn __i32_eq(a: i32, b: i32): bool
bool __i32_eq(int32_t a, int32_t b) { return a == b; }

// extern fn __f64_eq(a: f64, b: f64): bool
bool __f64_eq(double a, double b) { return a == b; }

// extern fn __bool_eq(a: bool, b: bool): bool
bool __bool_eq(bool a, bool b) { return a == b; }

// extern fn __string_eq(a: string, b: string): bool
bool __string_eq(const struct dao_string *a, const struct dao_string *b) {
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

// ---------------------------------------------------------------------------
// Printable (to_string)
// ---------------------------------------------------------------------------

// extern fn __i32_to_string(x: i32): string
//
// Returns a stack-allocated dao_string. Since Dao returns structs by
// value, the caller receives a copy.
struct dao_string __i32_to_string(int32_t x) {
  // Use a thread-local buffer for the conversion.
  static _Thread_local char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d", x);
  return (struct dao_string){.ptr = buf, .len = len};
}

// extern fn __f64_to_string(x: f64): string
struct dao_string __f64_to_string(double x) {
  static _Thread_local char buf[64];
  int len = snprintf(buf, sizeof(buf), "%g", x);
  return (struct dao_string){.ptr = buf, .len = len};
}

// extern fn __bool_to_string(x: bool): string
struct dao_string __bool_to_string(bool x) {
  if (x) {
    return (struct dao_string){.ptr = "true", .len = 4};
  }
  return (struct dao_string){.ptr = "false", .len = 5};
}
