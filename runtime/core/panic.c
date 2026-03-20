// panic.c — Dao runtime panic hook.
//
// Authority: docs/contracts/CONTRACT_RUNTIME_ABI.md

#include "dao_abi.h"

#include <stdio.h>
#include <stdlib.h>

void __dao_panic(const struct dao_string *msg) {
  // Flush stdout so any buffered output is visible before the abort.
  fflush(stdout);
  if (msg != NULL && msg->ptr != NULL && msg->len > 0) {
    fprintf(stderr, "dao panic: ");
    fwrite(msg->ptr, 1, (size_t)msg->len, stderr);
    fputc('\n', stderr);
  } else {
    fprintf(stderr, "dao panic\n");
  }
  abort();
}
