// alloc.c — Dao runtime allocation hooks.
//
// Authority: docs/contracts/CONTRACT_RUNTIME_ABI.md
// Placement: runtime/memory/ per docs/ARCH_INDEX.md

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/dao_abi.h"

// Round size up to a multiple of alignment.
static int64_t round_up(int64_t size, int64_t align) {
  return (size + align - 1) & ~(align - 1);
}

void *__dao_mem_alloc(int64_t size, int64_t align) {
  if (size <= 0) {
    size = align; // Allocate at least one unit.
  }
  // aligned_alloc requires size to be a multiple of alignment.
  int64_t rounded = round_up(size, align);
  void *ptr = aligned_alloc((size_t)align, (size_t)rounded);
  if (ptr == NULL) {
    fprintf(stderr, "dao panic: allocation failed (size=%lld, align=%lld)\n",
            (long long)size, (long long)align);
    abort();
  }
  return ptr;
}

void *__dao_mem_realloc(void *ptr, int64_t old_size, int64_t new_size,
                        int64_t align) {
  if (ptr == NULL) {
    return __dao_mem_alloc(new_size, align);
  }
  if (new_size <= 0) {
    free(ptr);
    return NULL;
  }
  // Standard realloc does not preserve alignment beyond max_align_t.
  // For alignments within max_align_t, plain realloc is sufficient.
  // For stronger alignments, allocate new + memcpy + free.
  if (align <= (int64_t)_Alignof(max_align_t)) {
    void *result = realloc(ptr, (size_t)new_size);
    if (result == NULL) {
      fprintf(stderr,
              "dao panic: reallocation failed (new_size=%lld, align=%lld)\n",
              (long long)new_size, (long long)align);
      abort();
    }
    return result;
  }
  // Strong alignment: allocate fresh aligned block and copy.
  void *fresh = __dao_mem_alloc(new_size, align);
  int64_t copy_size = old_size < new_size ? old_size : new_size;
  if (copy_size > 0) {
    memcpy(fresh, ptr, (size_t)copy_size);
  }
  free(ptr);
  return fresh;
}

void __dao_mem_free(void *ptr) {
  free(ptr);
}
