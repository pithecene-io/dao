// dao_abi.h — Canonical Dao runtime ABI declarations.
//
// This header is the single authoritative source for runtime-facing
// type definitions and hook signatures. All runtime implementation
// files include this header. The LLVM backend maintains a parallel
// authoritative declaration layer; both must agree with
// docs/contracts/CONTRACT_RUNTIME_ABI.md.
//
// This file defines the C-side ABI surface. Dao owns the semantics;
// C is the initial implementation vehicle only.

#ifndef DAO_RUNTIME_ABI_H
#define DAO_RUNTIME_ABI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Canonical value representations
// ---------------------------------------------------------------------------

// Dao string: pointer to byte data + byte length.
// Matches LLVM IR: %dao.string = type { ptr, i64 }
//
// - Not required to be null-terminated.
// - Literals may point to static (read-only) storage.
// - Empty strings are valid (ptr may be null when len is 0).
struct dao_string {
  const char *ptr;
  int64_t len;
};

// ---------------------------------------------------------------------------
// Runtime hook declarations — IO domain
// ---------------------------------------------------------------------------

// Write a string to stdout followed by a newline.
// Borrows msg for the duration of the call; no ownership transfer.
void __dao_io_write_stdout(const struct dao_string *msg);

// ---------------------------------------------------------------------------
// Runtime hook declarations — Equality domain
// ---------------------------------------------------------------------------

bool __dao_eq_i32(int32_t a, int32_t b);
bool __dao_eq_f64(double a, double b);
bool __dao_eq_bool(bool a, bool b);
bool __dao_eq_string(const struct dao_string *a, const struct dao_string *b);

// ---------------------------------------------------------------------------
// Runtime hook declarations — Conversion domain (to_string)
// ---------------------------------------------------------------------------

// Scalar-to-string conversions return a dao_string by value.
// The returned ptr points to thread-local transient storage that is
// valid until the next call to the same conversion hook on the same
// thread. Callers must consume or copy the result before the next
// conversion call.
struct dao_string __dao_conv_i32_to_string(int32_t x);
struct dao_string __dao_conv_f64_to_string(double x);
struct dao_string __dao_conv_bool_to_string(bool x);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DAO_RUNTIME_ABI_H
