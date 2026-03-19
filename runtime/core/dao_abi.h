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

bool __dao_eq_i8(int8_t a, int8_t b);
bool __dao_eq_i16(int16_t a, int16_t b);
bool __dao_eq_i32(int32_t a, int32_t b);
bool __dao_eq_i64(int64_t a, int64_t b);
bool __dao_eq_u8(uint8_t a, uint8_t b);
bool __dao_eq_u16(uint16_t a, uint16_t b);
bool __dao_eq_u32(uint32_t a, uint32_t b);
bool __dao_eq_u64(uint64_t a, uint64_t b);
bool __dao_eq_f32(float a, float b);
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
struct dao_string __dao_conv_i8_to_string(int8_t x);
struct dao_string __dao_conv_i16_to_string(int16_t x);
struct dao_string __dao_conv_i32_to_string(int32_t x);
struct dao_string __dao_conv_i64_to_string(int64_t x);
struct dao_string __dao_conv_u8_to_string(uint8_t x);
struct dao_string __dao_conv_u16_to_string(uint16_t x);
struct dao_string __dao_conv_u32_to_string(uint32_t x);
struct dao_string __dao_conv_u64_to_string(uint64_t x);
struct dao_string __dao_conv_f32_to_string(float x);
struct dao_string __dao_conv_f64_to_string(double x);
struct dao_string __dao_conv_bool_to_string(bool x);

// Numeric type conversions.
// Lossless conversions are exact. Narrowing conversions trap (abort)
// on NaN, Inf, or out-of-range values.
double __dao_conv_i32_to_f64(int32_t x);
int64_t __dao_conv_i32_to_i64(int32_t x);
int32_t __dao_conv_f64_to_i32(double x);
int32_t __dao_conv_i64_to_i32(int64_t x);

// ---------------------------------------------------------------------------
// Runtime hook declarations — Overflow domain (explicit operations)
// ---------------------------------------------------------------------------

// Wrapping arithmetic: two's complement wrap, no trap.
int32_t __dao_wrapping_add_i32(int32_t a, int32_t b);
int32_t __dao_wrapping_sub_i32(int32_t a, int32_t b);
int32_t __dao_wrapping_mul_i32(int32_t a, int32_t b);
int64_t __dao_wrapping_add_i64(int64_t a, int64_t b);
int64_t __dao_wrapping_sub_i64(int64_t a, int64_t b);
int64_t __dao_wrapping_mul_i64(int64_t a, int64_t b);

// Saturating arithmetic: clamp to min/max representable value.
int32_t __dao_saturating_add_i32(int32_t a, int32_t b);
int32_t __dao_saturating_sub_i32(int32_t a, int32_t b);
int32_t __dao_saturating_mul_i32(int32_t a, int32_t b);
int64_t __dao_saturating_add_i64(int64_t a, int64_t b);
int64_t __dao_saturating_sub_i64(int64_t a, int64_t b);
int64_t __dao_saturating_mul_i64(int64_t a, int64_t b);

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

// Return the byte length of a string as i64.
int64_t __dao_str_length(const struct dao_string *s);

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
