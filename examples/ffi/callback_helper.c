// callback_helper.c — C-side helpers for function pointer ABI E2E tests.
//
// These functions accept Dao function pointers as callbacks and call
// them, verifying that the callback ABI matches between Dao and C.
//
// Authority: docs/contracts/CONTRACT_C_ABI_INTEROP.md §4.4

#include <stdint.h>

// Accept a binary operation callback and apply it.
int32_t apply_op(int32_t (*op)(int32_t, int32_t), int32_t a, int32_t b) {
  return op(a, b);
}

// Accept a predicate callback and count how many elements pass.
int32_t count_if(const int32_t* arr, int32_t len,
                 int32_t (*pred)(int32_t)) {
  int32_t count = 0;
  for (int32_t i = 0; i < len; i++) {
    if (pred(arr[i])) {
      count++;
    }
  }
  return count;
}

// Accept a void callback and call it with a value.
static int32_t last_seen = 0;

void call_with(void (*cb)(int32_t), int32_t val) {
  cb(val);
}

int32_t get_last_seen(void) {
  return last_seen;
}

// Accept a callback that returns a double.
double transform(double (*fn)(double), double x) {
  return fn(x);
}
