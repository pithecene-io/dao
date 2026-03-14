// generator.c — Generator frame allocation for the Dao runtime.
//
// Generator frames are heap-allocated by the compiler-generated init
// function and freed when the for-loop iterator is destroyed.

#include "dao_abi.h"

#include <stdlib.h>
#include <string.h>

void *__dao_gen_alloc(int64_t size, int64_t align) {
  (void)align; // For first cut, rely on malloc's natural alignment.
  void *ptr = malloc((size_t)size);
  if (ptr != NULL) {
    memset(ptr, 0, (size_t)size);
  }
  return ptr;
}

void __dao_gen_free(void *ptr) { free(ptr); }
