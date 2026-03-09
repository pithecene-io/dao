# CONTRACT_TYPE_SYSTEM_FOUNDATIONS

Status: normative
Scope: Dao semantic type foundations
Authority: language and compiler boundary contract

## 1. Purpose

This contract freezes the foundational type-system direction for Dao.

It does not freeze final surface syntax for generics, conformance, or
pattern matching. It does freeze the semantic categories and
architectural boundaries that compiler work may rely on.

## 2. Type system stance

Dao is:

- statically typed
- nominal for named declarations
- algebraic for enums / sum types
- parametric for generics
- inference-assisted, but not inference-dominated

Dao is not a pure Hindley–Milner language and should not be designed
around whole-program inference as the primary organizing principle.

## 3. Foundational semantic type categories

The compiler semantic type layer must support, at minimum:

- builtin scalar types
- pointer types
- function types
- named types
- generic parameter types
- struct types
- enum types
- alias-backed named references, whether aliases are preserved
  semantically or normalized later

These categories are semantic compiler concepts, not merely parser
syntax.

## 4. Builtin scalar surface

The language-level builtin scalar names are terse.

### Integers
- `i8`
- `i16`
- `i32`
- `i64`
- `u8`
- `u16`
- `u32`
- `u64`

### Floating-point
- `f32`
- `f64`

### Other
- `bool`

Additional builtins such as `isize` or `usize` may be introduced
later, but are not required by this contract.

## 5. Predeclared and compiler-supported types

Dao distinguishes builtin scalar types from compiler-known predeclared
named types.

- `string` is not a builtin scalar. It is a compiler-known predeclared
  named type. Its long-term home is the stdlib/core prelude, but for
  now it is predeclared by the compiler so that examples and early
  programs can use it without import.

- `void` is a compiler-supported return/internal type. It is used as
  the return type for functions that produce no meaningful value. The
  question of whether `void` stays surface-visible or becomes a
  unit-like type is deferred; for now it is available as a return type
  annotation.

These types are not part of the builtin scalar family and should not
be conflated with it.

## 6. Semantic builtin representation

The semantic type system must represent builtin scalar types
explicitly.

The semantic naming convention should follow the `Type*` family, for
example:

- `TypeBuiltin`
- `TypePointer`
- `TypeFunction`
- `TypeNamed`
- `TypeGenericParam`
- `TypeStruct`
- `TypeEnum`

The semantic representation may use enums such as `I32`, `U64`, `F32`,
`F64`, `Bool` internally.

## 7. Enums and algebraic data

Dao enums are true sum types.

This means the language direction assumes:

- enums are not limited to C-style integer tags
- payload-bearing variants are part of the intended model
- later pattern matching and exhaustiveness checking are compatible
  with the type model

The exact surface syntax for payload-bearing variants may evolve, but
the semantic direction is fixed.

## 8. Generics

Dao supports parametric generics.

The type system must be able to represent:

- generic type parameters
- generic arguments on named types
- instantiated generic named types

The semantic type layer must be designed so later generic substitution
and instantiation are possible without replacing the foundational
representation.

## 9. Conformance / trait-interface direction

Dao will support an abstraction and conformance mechanism broadly in
the space of traits, interfaces, or protocols.

This mechanism is intended to be:

- more expressive than Go interfaces
- less surface-heavy and less syntactically dominant than Rust traits
- statically resolved by default
- capable of constraining generics

This contract freezes the direction, not the final syntax or solving
model.

## 10. Nominal identity

Named declarations such as structs, enums, and later
trait/interface-like declarations are nominal.

Two distinct named declarations are not equivalent merely because they
have structurally identical fields or variants.

## 11. Separation of layers

The compiler must preserve a strict separation between:

- parser syntax-level type nodes
- semantic compiler type representations
- type-checking and inference logic

Specifically:

- AST type syntax belongs in `compiler/frontend/ast/`
- semantic types belong in `compiler/frontend/types/`
- type checking belongs in `compiler/frontend/typecheck/`

## 12. Non-goals of this contract

This contract does not freeze:

- final generic syntax
- final conformance syntax
- overload resolution semantics
- inference algorithm details
- pattern matching syntax
- exhaustiveness diagnostics policy
- effect typing
- higher-kinded types
- dependent typing

## 13. Implementation consequences

Compiler work may rely on the following architectural assumptions:

- semantic types are canonical compiler-owned objects
- semantic types are distinct from AST syntax
- semantic type identity is stable enough for later HIR typing and
  tooling
- `types/` must not depend on `typecheck/`

## 14. Stability rule

Any change that would alter Dao from:

- nominal + algebraic + parametric

into:

- purely structural
- purely HM-style
- or trait-surface-dominated

requires explicit reconsideration of this contract.
