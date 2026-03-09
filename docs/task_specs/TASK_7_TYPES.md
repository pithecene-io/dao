# TASK_7_TYPES

Status: implementation spec
Phase: Semantic Frontend
Scope: `compiler/frontend/types/`

## 1. Objective

Implement the canonical semantic type representation layer for Dao.

This task establishes the compiler-owned type universe used by later
type checking, HIR annotation, semantic tooling, and
generic/conformance expansion.

Task 7 is not type checking. It is not inference. It is not
conformance solving.

## 2. Non-goals

Task 7 must not implement:

- expression type checking
- statement type checking
- local type inference
- overload resolution
- trait/interface conformance solving
- generic instantiation algorithms
- enum exhaustiveness
- method lookup
- pattern matching semantics

Those belong to later tasks.

## 3. Architectural role

Task 7 sits between:

- parser / AST / resolve

and

- typecheck / HIR

It defines the semantic type objects that later passes consume.

## 4. Directory

The implementation must live under:

```text
compiler/frontend/types/
```

## 5. Required deliverables

### Core files

Suggested shape:

```text
compiler/frontend/types/
    type.h
    type.cpp
    type_kind.h
    type_context.h
    type_context.cpp
    type_builtin.h
    type_builtin.cpp
    type_printer.h
    type_printer.cpp
    type_tests.cpp
```

Exact filenames may vary slightly, but the conceptual split must
remain clear.

### Core semantic entities

Implement at least:

- `Type`
- `TypeKind`
- `TypeContext`
- `TypeBuiltin`
- `TypePointer`
- `TypeFunction`
- `TypeNamed`
- `TypeGenericParam`
- `TypeStruct`
- `TypeEnum`

Alias handling may be explicit or represented through named
declarations, but the layer must be ready for aliases.

## 6. Ownership and allocation

Semantic types should be compiler-owned canonical objects allocated
from a context-owned arena or equivalent stable storage.

Do not use per-edge `std::unique_ptr` ownership for the type graph.

Do not use a giant `std::variant` for the entire semantic type
universe.

The intended shape is:

- context-owned semantic objects
- stable identity
- canonicalization / interning where appropriate

## 7. Canonicalization

The semantic type layer must canonicalize types.

At minimum, the following must be interned or otherwise canonicalized:

- builtin types
- pointer types
- function types
- named types with generic arguments
- generic parameter types where identity rules require it

The goal is that canonical semantic equality can rely on stable
identity for equivalent types.

## 8. Separation from syntax

Parser type syntax must remain separate from semantic types.

The parser produces syntax-level type nodes. Task 7 produces semantic
types.

Task 7 may introduce the semantic structures only. Lowering from
syntax `TypeNode` to semantic `Type` may be partially scaffolded, but
full type-lowering logic belongs with later semantic passes.

## 9. Required foundational builtin set

The initial builtin scalar set is:

- `i8`
- `i16`
- `i32`
- `i64`
- `u8`
- `u16`
- `u32`
- `u64`
- `f32`
- `f64`
- `bool`

Additionally, `string` is a compiler-known predeclared named type (not
a builtin scalar) and `void` is a compiler-supported return/internal
type. See `CONTRACT_TYPE_SYSTEM_FOUNDATIONS.md` §5 for the
distinction.

## 10. Initial semantic type categories

### 10.1 TypeBuiltin

Represents builtin scalar types.

### 10.2 TypePointer

Represents `*T`.

### 10.3 TypeFunction

Represents function types. Must contain:

- ordered parameter types
- return type

### 10.4 TypeNamed

Represents a nominal named type reference, optionally with generic
arguments. Should be declaration-backed or symbol-backed, not just a
string.

### 10.5 TypeGenericParam

Represents a generic type parameter in semantic form.

### 10.6 TypeStruct

Represents semantic struct declarations or canonical struct type
identity.

### 10.7 TypeEnum

Represents semantic enum declarations or canonical enum type identity.

## 11. Dependency boundaries

### Allowed dependencies

`types/` may depend on:

- stable basic support utilities
- resolved declaration identity, if needed
- AST declaration identity, only if no better resolved identity
  exists yet

### Forbidden dependencies

`types/` must not depend on:

- `typecheck/`
- HIR
- MIR
- backend
- tooling-specific code

## 12. Type printing

A type printer must exist for debugging, diagnostics, and tests.

Printing should be stable and deterministic.

Examples of expected readable output:

- `i32`
- `*i32`
- `fn(i32, f64): bool`
- `List[i32]`

The exact function-type print spelling may vary, but it must be
consistent.

## 13. Equality

Task 7 must provide reliable semantic type equality.

Preferred model:

- canonicalized pointer/handle identity for equal types

If some categories cannot use direct identity initially, the
implementation must still provide a clear equality API and be designed
toward full canonicalization.

## 14. Tests

Task 7 is not complete without tests.

Required test coverage:

- builtin type creation
- pointer type canonicalization
- function type canonicalization
- named type representation with zero and non-zero generic arguments
- type printing stability
- semantic equality behavior

Examples of required assertions:

- requesting `*i32` twice yields the same canonical semantic type
- requesting `fn(i32): i32` twice yields the same canonical semantic
  type
- `*i32` and `*u32` are distinct
- `List[i32]` and `List[f64]` are distinct

## 15. Exit criteria

Task 7 is complete when:

- the compiler has a stable `types/` layer
- semantic builtin, pointer, function, named, generic-param, struct,
  and enum type categories exist
- canonicalization is implemented for the foundational categories
- tests pass
- the implementation is cleanly separated from both syntax-level
  `TypeNode` and later `typecheck/`

## 16. Follow-on consumers

Task 7 must leave a clean path for:

- Task 8 type checking
- HIR type annotation
- semantic hover / tooling
- future generic instantiation work
- future enum/pattern-matching work
- future conformance/trait-interface work

## 17. Design notes for later work

The Task 7 implementation should anticipate, but not yet solve:

- generic substitution
- declaration-backed nominal identities
- alias normalization policy
- enum variant payload typing
- constrained generic parameters
- static-dispatch conformance mechanisms

## 18. Stability rule

If implementation pressure suggests collapsing semantic types into
syntax nodes, or letting `typecheck/` define the type universe
implicitly, stop and revisit the architecture before proceeding.
