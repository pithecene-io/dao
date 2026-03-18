# Task 15 — C ABI Interop

## Objective

Enable Dao programs to call external C ABI functions by linking
against user-provided object files, static archives, or system
libraries. Limited to scalar and pointer types per
`CONTRACT_C_ABI_INTEROP.md`.

## Governing contract

`docs/contracts/CONTRACT_C_ABI_INTEROP.md`

## Deliverables

### 1. Driver: external link inputs

Extend `daoc build` to accept additional link inputs:

```
daoc build program.dao helper.o
daoc build program.dao -lm
daoc build program.dao helper.o -lm -L/opt/libs
```

Implementation:

- collect extra CLI arguments after the source file
- pass them through to the `cc` linker invocation
- object files (`.o`, `.a`) and linker flags (`-l`, `-L`) are
  forwarded verbatim
- no validation of the external inputs beyond linker errors

### 2. Frontend: type validation for extern fn

Verify that `extern fn` declarations only use supported types at
the ABI boundary. Reject unsupported types (structs by value,
function pointers, etc.) with clear diagnostics.

Current state: `extern fn` is already parsed and lowered. The
type checker accepts any type. This task adds a validation pass
that rejects unsupported ABI types in `extern fn` declarations
(excluding `__dao_` runtime hooks, which are handled separately).

### 3. Examples

Two end-to-end examples proving the feature:

#### Example A: custom C helper

`examples/ffi/helper.c`:
```c
#include <stdint.h>
int64_t add_i64(int64_t a, int64_t b) { return a + b; }
double scale(double x, int32_t factor) { return x * factor; }
```

`examples/ffi/ffi_helper.dao`:
```dao
extern fn add_i64(a: i64, b: i64): i64
extern fn scale(x: f64, factor: i32): f64

fn main(): i32
  print(add_i64(100, 200))
  print(scale(3.14, 2))
  return 0
```

Build: `daoc build ffi_helper.dao helper.o`

#### Example B: system library (libm)

`examples/ffi/ffi_libm.dao`:
```dao
extern fn sqrt(x: f64): f64
extern fn pow(base: f64, exp: f64): f64

fn main(): i32
  print(sqrt(2.0))
  print(pow(2.0, 10.0))
  return 0
```

Build: `daoc build ffi_libm.dao -lm`

### 4. Tests

- backend test: verify `extern fn` with C ABI types emits correct
  LLVM `declare`
- driver test (manual): compile and run both examples

## Non-deliverables

- no struct-by-value ABI
- no string interop
- no callbacks / function pointers
- no variadics
- no header parsing
- no dynamic loading

## Exit criteria

1. `daoc build program.dao helper.o` links and runs correctly
2. `daoc build program.dao -lm` links and runs correctly
3. unsupported types in `extern fn` produce clear diagnostics
4. both examples produce correct output
