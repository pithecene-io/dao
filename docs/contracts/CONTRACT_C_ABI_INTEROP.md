# CONTRACT_C_ABI_INTEROP

Status: normative
Scope: Dao foreign function interop via the C ABI
Authority: language and compiler boundary contract

## 1. Purpose

This contract defines the boundary between Dao programs and external
code via the platform C ABI. It specifies what `extern fn` means
semantically, which types are supported, what the compiler guarantees
at the boundary, and what is explicitly out of scope.

## 2. Core doctrine

C ABI interop is an **explicit foreign boundary**, not a general-
purpose extension mechanism. It complements, but does not redefine,
Dao's runtime, stdlib, or semantic model.

Rules:

- `extern fn` declares a function whose calling convention and data
  representation are governed by the target platform C ABI
- Dao semantics do not automatically project onto foreign code
- crossing the boundary is explicit and narrow
- C ABI interop is a capability, not a substitute for designing
  Dao's own runtime and stdlib properly

## 3. What `extern fn` means

An `extern fn` declaration states:

- this function exists in external object code
- its symbol name is exactly as written
- its calling convention is the platform C ABI
- the compiler emits an LLVM `declare` with the appropriate
  signature and linkage
- the compiler does not generate a body, verify the implementation,
  or enforce Dao semantics on the foreign side

The function must be provided at link time via an object file,
static archive, or shared/system library.

## 4. Supported types at the ABI boundary

### 4.1 Initial supported types (v1)

| Dao type  | C ABI type       | Direction       |
|-----------|------------------|-----------------|
| `i32`     | `int32_t`        | arg + return    |
| `i64`     | `int64_t`        | arg + return    |
| `f64`     | `double`         | arg + return    |
| `bool`    | `_Bool` (1 byte) | arg + return    |
| `*T`      | `T*`             | arg + return    |
| `void`    | `void`           | return only     |

### 4.2 Pointer semantics at the boundary

- pointer values are foreign addresses — no ownership implied
- no aliasing or lifetime safety is promised across FFI
- dereference of foreign pointers requires `mode unsafe =>`
- opaque pointers (`*void` equivalent) are the primary interop
  mechanism for foreign aggregate data

### 4.3 Types explicitly not supported in v1

- Dao `string` as C `char*` (requires explicit conversion)
- struct/class by value (target-specific ABI rules)
- function pointers / callbacks
- variadic functions (`printf`-style)
- C enums
- C unions
- arrays / slices by value

If an `extern fn` declaration uses an unsupported type, the compiler
must reject it with a clear diagnostic during type checking or
backend lowering.

### 4.4 Future type expansions

The following may be added in later versions:

- `c_string` or explicit null-terminated byte string type
- by-value struct passing (once Dao struct ABI is stabilized)
- function pointer types for callbacks
- `f32` (when surfaced)

Each expansion requires updating this contract before implementation.

## 5. Name and linkage model

- the symbol name in `extern fn foo(...)` is `foo` — no mangling,
  no prefix
- Dao runtime hooks use the `__dao_` prefix to avoid collisions
  with user-declared foreign symbols
- C++ mangled names are not supported; use `extern "C"` on the
  C++ side
- weak linkage, visibility attributes, and linker scripts are
  not part of the initial model

## 6. Ownership and safety rules

- no ownership transfer is implied by `extern fn` calls
- the caller (Dao side) retains ownership of any data it passes
  unless the foreign function's documentation states otherwise
- the compiler does not insert cleanup, reference counting, or
  lifetime tracking for values passed to or received from foreign
  code
- foreign pointer dereference is an unsafe operation
- integer overflow checking is **not** applied to values received
  from foreign code (they enter Dao's value space as-is)

## 7. Compiler obligations

### 7.1 Frontend

- `extern fn` declarations are parsed and type-checked like other
  function declarations
- the body must be absent (syntax error if present)
- parameter and return types must be in the supported set
- unsupported types must be rejected with a diagnostic

### 7.2 Backend

- `extern fn` is lowered to an LLVM `declare` with
  `ExternalLinkage`
- calling convention follows the platform C ABI (LLVM default)
- argument and return types are lowered per the existing type
  lowering rules
- no body is emitted

### 7.3 Driver

- the driver must support linking additional external inputs:
  - object files (`.o`)
  - static archives (`.a`)
  - system/shared libraries (`-l<name>`)
  - library search paths (`-L<path>`)
- the mechanism for specifying external link inputs is a driver
  concern, not a language concern
- the initial implementation may use simple CLI passthrough to the
  system linker

## 8. What is explicitly out of scope

- header parsing or `#include` integration
- automatic bindgen / declaration generation
- C preprocessor awareness
- C type introspection at compile time
- C++ name mangling or template instantiation
- dynamic loading (`dlopen`-style)
- platform-specific calling conventions beyond the default C ABI
- inline assembly
- ABI stability guarantees for Dao-defined symbols exposed to C
  (reserved for a future "export" contract)

## 9. Relationship to existing contracts

- `CONTRACT_BOOTSTRAP_AND_INTEROP.md` establishes that the C ABI
  is the initial interop target; this contract operationalizes that
- `CONTRACT_RUNTIME_ABI.md` defines Dao's own runtime hooks, which
  are already `extern fn` declarations consumed through this same
  mechanism
- `CONTRACT_SYNTAX_SURFACE.md` freezes `extern fn` as the
  declaration form for externally-provided functions
- `CONTRACT_NUMERIC_SEMANTICS.md` defines the type mappings that
  apply at the ABI boundary

## 10. Implementation status

| Feature                          | Status          |
|----------------------------------|-----------------|
| `extern fn` syntax + parsing     | Implemented     |
| `extern fn` LLVM lowering        | Implemented     |
| Scalar types at boundary         | Implemented     |
| Pointer types at boundary        | Implemented     |
| Driver: link `.o` files          | Not implemented |
| Driver: link `-l` libraries      | Not implemented |
| Driver: `-L` search paths        | Not implemented |
| Unsupported type rejection       | Partial         |
| E2E foreign call example         | Not implemented |
