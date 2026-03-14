// generator.c — Generator frame allocation for the Dao runtime.
//
// Generator frames are heap-allocated by the compiler-generated init
// function and freed when the for-loop iterator is destroyed.

#include "dao_abi.h"

#include <stdlib.h>
#include <string.h>

void *__dao_gen_alloc(int64_t size, int64_t align) {
  // Clamp alignment to at least sizeof(void*) — aligned_alloc requires
  // a power-of-two alignment that the implementation supports, and
  // values below pointer size are not guaranteed to be accepted.
  size_t min_align = sizeof(void *);
  size_t a = (size_t)align;
  if (a < min_align) {
    a = min_align;
  }
  size_t s = (size_t)size;
  // aligned_alloc requires size to be a multiple of alignment.
  size_t padded = (s + a - 1) & ~(a - 1);
  void *ptr = aligned_alloc(a, padded);
  if (ptr != NULL) {
    memset(ptr, 0, s);
  }
  return ptr;
}

void __dao_gen_free(void *ptr) { free(ptr); }
