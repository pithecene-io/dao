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

// ---------------------------------------------------------------------------
// Runtime hook declarations — Generator domain
// ---------------------------------------------------------------------------

// Allocate a generator frame of the given size and alignment.
// Returns a zeroed block. The caller (compiler-generated init function)
// populates the frame after allocation.
void *__dao_gen_alloc(int64_t size, int64_t align);

// Free a generator frame previously allocated by __dao_gen_alloc.
void __dao_gen_free(void *ptr);

// ---------------------------------------------------------------------------
// Runtime hook declarations — String domain
// ---------------------------------------------------------------------------

// Concatenate two strings, returning the result by value.
// The returned ptr points to a freshly malloc-allocated buffer
// owned by the caller. Not automatically freed in the current runtime.
struct dao_string __dao_str_concat(const struct dao_string *a,
                                   const struct dao_string *b);

// Return the byte length of a string as i32.
int32_t __dao_str_length(const struct dao_string *s);

// ---------------------------------------------------------------------------
// Runtime hook declarations — Memory/resource domain
// ---------------------------------------------------------------------------

// Enter a scoped resource domain. Returns an opaque domain handle.
// Current implementation: scope/lifetime bookkeeping only.
// Arena-based allocation semantics are deferred.
void *__dao_mem_resource_enter(void);

// Exit a scoped resource domain. Takes the handle returned by enter.
void __dao_mem_resource_exit(void *domain);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DAO_RUNTIME_ABI_H
