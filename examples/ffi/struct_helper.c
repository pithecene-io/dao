// struct_helper.c — C-side helpers for struct-by-value ABI E2E tests.
//
// These functions verify that Dao struct layout matches C struct layout
// by inspecting field values and offsets at runtime. If any field is
// received with the wrong value, the test will produce incorrect output.
//
// Authority: docs/contracts/CONTRACT_C_ABI_INTEROP.md §4.3

#include <stdbool.h>
#include <stdint.h>

// --- Simple struct: two i32 fields ---

struct Point {
  int32_t x;
  int32_t y;
};

// Receive struct by value, return derived scalar.
int64_t point_sum(struct Point p) {
  return (int64_t)p.x + (int64_t)p.y;
}

// Return struct by value from C.
struct Point make_point(int32_t x, int32_t y) {
  return (struct Point){x, y};
}

// --- Mixed-alignment struct: bool + i32 + i64 ---

struct Mixed {
  bool flag;
  int32_t value;
  int64_t wide;
};

// Receive mixed-alignment struct, verify all fields arrived correctly.
// Returns: value if flag is true, wide if flag is false.
int64_t check_mixed(struct Mixed m) {
  if (m.flag) {
    return (int64_t)m.value;
  }
  return m.wide;
}

// Return mixed-alignment struct from C.
struct Mixed make_mixed(bool flag, int32_t value, int64_t wide) {
  return (struct Mixed){flag, value, wide};
}

// --- Nested struct ---

struct Inner {
  int32_t a;
  double b;
};

struct Outer {
  struct Inner inner;
  int32_t tag;
};

// Receive nested struct, extract deep field.
double outer_inner_b(struct Outer o) {
  return o.inner.b;
}

// Return nested struct from C.
struct Outer make_outer(int32_t a, double b, int32_t tag) {
  return (struct Outer){{a, b}, tag};
}

// --- i32 + i64 alignment test ---

struct Pair64 {
  int32_t lo;
  int64_t hi;
};

// Receive struct with alignment padding, verify field values.
int64_t pair64_hi(struct Pair64 p) {
  return p.hi;
}

// Return struct with alignment padding.
struct Pair64 make_pair64(int32_t lo, int64_t hi) {
  return (struct Pair64){lo, hi};
}
