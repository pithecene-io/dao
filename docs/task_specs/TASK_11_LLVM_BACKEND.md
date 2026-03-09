# TASK_11_LLVM_BACKEND

Status: implementation spec
Phase: Backend
Scope: `compiler/backend/llvm/`

## 1. Objective

Implement the first LLVM backend for Dao.

Task 11 consumes:

- MIR
- semantic types
- function/module identity
- preserved execution/resource annotations from MIR

and produces:

- LLVM IR modules
- LLVM IR functions
- target-independent LLVM lowering output suitable for JIT or object
  generation later

Task 11 is the first backend realization of Dao programs.

## 2. Non-goals

Task 11 must not implement:

- a full optimizer pipeline strategy
- platform ABI perfection across all targets
- cross-platform runtime packaging
- final GPU backend lowering
- final resource-region runtime semantics beyond the current supported
  slice
- debug info completeness
- linker driver behavior
- object/executable packaging for every platform
- advanced exception handling
- generics monomorphization if not already solved upstream

The goal is a correct and disciplined first LLVM lowering, not backend
completeness.

## 3. Backend role

The LLVM backend should:

- consume MIR as the operational source of truth
- map Dao semantic types to LLVM types
- lower MIR control flow to LLVM basic blocks
- lower MIR values/instructions to LLVM IR
- preserve enough source correlation for debugging and diagnostics where
  practical
- remain cleanly separated from frontend and MIR construction concerns

The LLVM backend should not:

- reinterpret source syntax
- redo semantic analysis
- become a second MIR

## 4. Directory

Implementation must live under:

```text
compiler/backend/llvm/
```

Suggested shape:

```text
compiler/backend/llvm/
    llvm_backend.h
    llvm_backend.cpp
    llvm_context.h
    llvm_context.cpp
    llvm_module_builder.h
    llvm_module_builder.cpp
    llvm_function_builder.h
    llvm_function_builder.cpp
    llvm_type_lowering.h
    llvm_type_lowering.cpp
    llvm_value_lowering.h
    llvm_value_lowering.cpp
    llvm_runtime_hooks.h
    llvm_runtime_hooks.cpp
    llvm_printer.h
    llvm_printer.cpp
    llvm_tests.cpp
```

Exact filenames may vary, but the conceptual split should remain clear.

## 5. Inputs

Task 11 consumes:

* MIR module/function/block/instruction structure
* semantic `Type*`
* MIR type information
* MIR source spans where available
* backend configuration/target triple information as needed

MIR is authoritative for backend lowering.
The LLVM backend must not consume AST or HIR directly.

## 6. Outputs

Task 11 must produce:

* an LLVM Module
* lowered LLVM Functions
* lowered LLVM BasicBlocks
* LLVM values/instructions corresponding to MIR
* textual LLVM IR dump support
* a stable CLI surface for IR emission

Optional later outputs:

* object files
* assembly
* executable linking

These are not required for the initial Task 11 slice unless explicitly
added.

## 7. Core design principles

### 7.1 MIR is the backend input boundary

LLVM lowering starts from MIR, not from AST or HIR.

### 7.2 Semantic type lowering is centralized

Semantic/MIR type to LLVM type mapping must live in a dedicated
type-lowering layer.

### 7.3 Control flow remains explicit

MIR basic blocks and terminators should lower directly to LLVM control
flow.

### 7.4 Backend details stay in backend

Do not leak LLVM types or APIs back upward into MIR, HIR, or frontend
layers.

### 7.5 Start narrow, correct, and inspectable

The first backend should favor correctness and readable IR over
premature sophistication.

## 8. LLVM backend responsibilities

Task 11 must implement:

* module creation
* function signature lowering
* local value/lifetime mapping
* basic block creation
* instruction lowering
* terminator lowering
* semantic type lowering
* string constant/materialization support for the current language slice
* CLI/debug printing support

## 9. Type lowering

Task 11 must introduce a dedicated LLVM type lowering layer.

### Required initial mappings

#### Scalars

* `i8` → LLVM `i8`
* `i16` → LLVM `i16`
* `i32` → LLVM `i32`
* `i64` → LLVM `i64`
* `u8` → LLVM `i8`
* `u16` → LLVM `i16`
* `u32` → LLVM `i32`
* `u64` → LLVM `i64`
* `bool` → LLVM `i1`
* `f32` → LLVM `float`
* `f64` → LLVM `double`

#### Pointer types

* `*T` → LLVM pointer to lowered `T`

#### Void

* compiler-supported `void` → LLVM `void` in function returns where
  applicable

#### Predeclared string

For the first backend slice, choose and document a narrow
representation.

Recommended initial representation:

* pointer-like runtime string handle, or
* simple pointer-to-bytes convention if that is already the chosen
  runtime boundary

Do not leave this implicit.

#### Structs / enums

If semantic structs/enums are already part of the supported slice, lower
them conservatively and explicitly.
If not fully ready yet, keep the initial backend slice scoped
accordingly.

## 10. Function lowering

Each MIR function lowers to one LLVM function.

Task 11 must lower:

* function name / symbol identity
* parameter types
* return type
* entry block
* all MIR basic blocks
* terminators

Recommended first policy:

* use straightforward LLVM function creation
* map MIR locals and temporaries cleanly
* avoid clever ABI shaping in the first pass

## 11. Local storage and values

The backend must define a clear strategy for lowering:

* MIR locals / places
* MIR temporaries / values
* loads and stores

Recommended first strategy:

* lower MIR locals to LLVM allocas in the entry block
* lower MIR temporaries directly to LLVM SSA values where possible
* lower explicit MIR store/load operations faithfully

This is simple, conventional, and easy to inspect.

## 12. Control-flow lowering

Task 11 must lower MIR terminators into LLVM terminators.

### Required baseline

* `Br` → unconditional branch
* `CondBr` → conditional branch
* `Return` → return instruction

LLVM block structure should closely mirror MIR block structure.

Do not invent extra backend-only control flow unless required.

## 13. Instruction lowering

Task 11 must lower, at minimum, the currently supported MIR
instruction/value categories.

### Constants

* integer constants
* float constants
* bool constants
* string constants

### Arithmetic and logical ops

* add/sub/mul/div for the supported numeric types
* comparisons
* boolean logic

### Memory-ish operations

* local declaration / alloca lowering
* load
* store
* address-of lowering as appropriate
* dereference/load-indirect lowering as appropriate

### Calls

* direct function calls
* runtime hook calls where needed

### Field/index access

Only if these are already fully represented in MIR for the current
supported slice.

## 14. String handling

Task 11 must choose an explicit first lowering strategy for string
literals and `string`.

Recommended initial strategy:

* lower string literals as LLVM global constants
* materialize a runtime-compatible handle/pointer value at use sites

The exact runtime-facing string ABI may remain narrow for now, but it
must be explicit and consistent.

## 15. Runtime hooks

The backend may need a small, explicit set of runtime hooks for the
current language slice.

Examples might include:

* printing
* allocation helpers
* string helpers
* resource-region entry/exit helpers later

If runtime hooks are needed:

* define them explicitly
* isolate them behind `llvm_runtime_hooks.*`
* do not scatter ad hoc intrinsic-like calls throughout lowering code

## 16. Lowering of Dao-specific execution semantics

### 16.1 Mode regions

#### `mode unsafe`

This may have no direct LLVM manifestation beyond permitting the MIR
operations that reached backend lowering.
Do not invent unnecessary IR markers for pure permission contexts.

#### `mode parallel`

If parallel execution semantics are not yet fully implemented, preserve
enough structure to diagnose unsupported lowering cleanly rather than
silently erasing semantics.

#### `mode gpu`

For the first LLVM backend slice, it is acceptable to:

* reject unsupported GPU mode lowering clearly, or
* preserve it as unsupported backend territory

Do not fake GPU support through ordinary CPU lowering unless explicitly
intended.

### 16.2 Resource regions

`resource memory X =>` and future resource constructs must not be
forgotten casually.

For the first backend slice, acceptable strategies include:

* explicit no-op region markers in backend bookkeeping if runtime
  behavior is not yet implemented
* lowering to narrow runtime hook boundaries
* lowering only the currently implemented memory semantics

But the backend must remain honest about what is and is not implemented.

## 17. Error policy for unsupported lowering

Task 11 should fail clearly for MIR constructs not yet supported.

Examples:

* unimplemented enum lowering
* unsupported lambda capture strategy
* unsupported GPU region lowering
* unsupported advanced resource behavior

Do not emit half-correct LLVM IR.

## 18. Builder structure

A good implementation split is:

* `LlvmBackend` — top-level orchestration
* `LlvmModuleBuilder` — module-wide lowering
* `LlvmFunctionBuilder` — per-function lowering
* `LlvmTypeLowering` — semantic/MIR type to LLVM type mapping
* `LlvmValueLowering` — MIR instruction/value lowering helpers

This keeps responsibilities clean.

## 19. Value mapping

Task 11 must maintain a clear mapping from MIR entities to LLVM values.

Recommended structures:

* MIR local/place → LLVM alloca/value handle
* MIR SSA-like temporary/value → LLVM `Value*`
* MIR block → LLVM `BasicBlock*`

This mapping should be explicit and owned by the function builder.

## 20. Debug / printing surface

Task 11 must support emitting textual LLVM IR.

Suggested CLI surfaces:

```text
daoc llvm-ir file.dao
```

One stable textual IR surface is enough initially.

The output should be useful for:

* backend debugging
* golden tests
* validating MIR lowering decisions

## 21. Tests

Task 11 is not complete without backend golden tests.

### Required coverage

#### Positive backend coverage

* arithmetic function lowering
* bool conditions and branches
* while loop lowering
* local bindings and assignment
* function call lowering
* return lowering
* pointer address/deref where supported
* string literal lowering
* print/runtime-hook lowering if part of the current slice

#### Structural backend coverage

* valid LLVM module emission
* valid function signatures
* valid basic block/terminator structure
* deterministic IR output where practical

#### Failure coverage

* unsupported construct diagnostics/errors for:
  * unsupported GPU lowering
  * unsupported advanced lambda lowering
  * unsupported resource semantics beyond the implemented slice

## 22. Exit criteria

Task 11 is complete when:

* MIR lowers to valid LLVM IR for the supported language slice
* textual LLVM IR can be emitted through the CLI
* type lowering is centralized and clean
* control flow lowering mirrors MIR structure correctly
* tests cover supported lowering and intentional unsupported cases
* the backend boundary remains cleanly separated from frontend/MIR
  concerns

## 23. Boundary rules

The LLVM backend may depend on:

* MIR
* semantic types
* backend support utilities
* LLVM APIs

The LLVM backend must not depend on:

* AST
* HIR
* resolver internals
* type checker internals beyond consumed typed MIR facts
* playground/LSP-specific logic

## 24. Stability rules

If implementation pressure suggests any of the following, stop and
revisit before proceeding:

* lowering directly from HIR or AST
* leaking LLVM types into MIR
* making string/resource behavior implicit or magical
* silently erasing unsupported mode/resource semantics
* scattering type lowering logic throughout random instruction builders
