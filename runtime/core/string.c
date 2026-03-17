// string.c — String operation hooks.

#include "dao_abi.h"

#include <stdlib.h>
#include <string.h>

// __dao_str_concat allocates via malloc. The result is a fresh heap
// allocation that the caller owns. In the current runtime there is no
// automatic deallocation — these allocations leak until process exit.
// Future arena/GC integration will reclaim them.

struct dao_string __dao_str_concat(const struct dao_string *a,
                                   const struct dao_string *b) {
  int64_t a_len = (a != NULL) ? a->len : 0;
  int64_t b_len = (b != NULL) ? b->len : 0;
  int64_t total = a_len + b_len;

  if (total == 0) {
    return (struct dao_string){.ptr = NULL, .len = 0};
  }

  char *buf = (char *)malloc((size_t)total);
  if (buf == NULL) {
    return (struct dao_string){.ptr = NULL, .len = 0};
  }

  if (a_len > 0 && a->ptr != NULL) {
    memcpy(buf, a->ptr, (size_t)a_len);
  }
  if (b_len > 0 && b->ptr != NULL) {
    memcpy(buf + a_len, b->ptr, (size_t)b_len);
  }

  return (struct dao_string){.ptr = buf, .len = total};
}

int32_t __dao_str_length(const struct dao_string *s) {
  if (s == NULL) {
    return 0;
  }
  return (int32_t)s->len;
}
