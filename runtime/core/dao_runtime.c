// Minimal Dao runtime — linked into every executable produced by daoc.
// This file provides C implementations for Dao builtins that the
// compiler emits as extern symbols.

#include <stdint.h>
#include <stdio.h>

// Matches the LLVM IR struct %dao.string = type { ptr, i64 }.
// String parameters are passed by pointer in the Dao calling convention.
struct dao_string {
  const char *ptr;
  int64_t len;
};

// fn print(msg: string): void
//
// The compiler stores string literals with their surrounding quotes
// (e.g. "hello" has ptr pointing to '"hello"' with len 7). Strip the
// outer quotes before printing.
void print(const struct dao_string *msg) {
  if (msg == NULL || msg->ptr == NULL || msg->len == 0) {
    return;
  }

  const char *start = msg->ptr;
  int64_t len = msg->len;

  // Strip surrounding double quotes if present.
  if (len >= 2 && start[0] == '"' && start[len - 1] == '"') {
    start++;
    len -= 2;
  }

  if (len > 0) {
    fwrite(start, 1, (size_t)len, stdout);
  }
  fputc('\n', stdout);
}
