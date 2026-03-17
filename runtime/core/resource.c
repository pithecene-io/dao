// resource.c — Scoped resource domain hooks.
//
// Current implementation: scope/lifetime bookkeeping only.
// Enter returns a sentinel token; exit accepts and discards it.
// The handle ABI is designed so future arena/allocator-domain
// implementations fit without signature churn.

#include "dao_abi.h"

#include <stdint.h>

// Monotonic domain counter. Each enter call returns a unique
// non-null token so the compiler can verify scope pairing.
static uint64_t next_domain_id = 1;

void *__dao_mem_resource_enter(void) {
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  return (void *)(uintptr_t)(next_domain_id++);
}

void __dao_mem_resource_exit(void *domain) {
  (void)domain; // Bookkeeping only; no-op in current implementation.
}
