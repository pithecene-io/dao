// io.c — Dao runtime IO hooks.
//
// Implements: __dao_io_write_stdout
// Authority:  docs/contracts/CONTRACT_RUNTIME_ABI.md

#include "dao_abi.h"

#include <stdio.h>

void __dao_io_write_stdout(const struct dao_string *msg) {
  if (msg == NULL || msg->ptr == NULL || msg->len <= 0) {
    fputc('\n', stdout);
    return;
  }

  fwrite(msg->ptr, 1, (size_t)msg->len, stdout);
  fputc('\n', stdout);
}
