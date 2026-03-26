# ADR: `enum class` — Closed Structured Alternatives

Status: **accepted**
Date: 2026-03-26

## Decision

Add `enum class` as a compound declaration form for closed
structured alternatives with named fields.

## Context

Dao has two foundational type-declaration keywords:

- `enum` — closed set of atomic variants (classification)
- `class` — open-ended nominal product type (composition)

There is no way to declare a closed set of structured variants where
each variant carries named fields.  This gap forces two bad patterns:

1. **Payload-enum sludge**: `enum` variants with anonymous positional
   fields that grow unbounded (`HirFunction(i64, i64, i64, i64, i64,
   i64, i64)`).  Every match is a puzzle of positional guessing.
   Adding a field breaks every match site.

2. **Wrapper laundering**: a separate `class` for each variant's data,
   wrapped in a 1-field enum variant.  Two declarations per variant,
   two levels of indirection in every match.

Both are structurally dishonest.  The missing construct is the
synthesis of `enum` (closedness, exhaustive matching) with `class`
(named fields, structural clarity).

## The construct

`enum class` is a compound declaration.  `enum` contributes closedness
and exhaustive matching.  `class` contributes named fields and
structural identity.

```dao
enum class Expr:
  IntLit(value: i64)
  Call(
    callee: ExprId,
    args: List[ExprId]
  )
  Function(
    name: NameId,
    tparams: ListId,
    params: ListId,
    ret: TypeId,
    body: NodeId,
    is_extern: bool,
    class_owner: ClassId
  )
```

### Syntax rules

- `enum class Name:` introduces the declaration
- Each variant is `VariantName(field: Type, field: Type, ...)`
- Fields are always named — anonymous positional payloads are not
  allowed in `enum class`
- Multiline variants use parenthesized field lists (newlines inside
  parens are suppressed, same as function parameters)
- Fieldless variants are allowed: `None` (no parens needed)
- Generic type parameters follow the name: `enum class Option<T>:`

### Construction

Named fields at construction site:

```dao
let e = Expr::Call(callee = f, args = my_args)
```

Fieldless variants:

```dao
let o = Option::None
```

### Match destructuring

Named destructuring:

```dao
match expr:
  Expr::IntLit(value):
    print(value)
  Expr::Call(callee, args):
    dispatch(callee, args)
  Expr::Function(name, ret, ..):
    // .. ignores remaining fields
```

Rules:
- Destructuring bindings follow field declaration order
- `..` at the end ignores remaining fields (adding fields does not
  break existing matches)
- Exhaustive: the compiler requires all variants to be covered

### Field access after match

```dao
match expr:
  Expr::Call as c:
    let callee = c.callee
    let args = c.args
```

The `as` form binds the whole variant, enabling named field access
without positional destructuring.

## What changes about existing `enum`

`enum` (without `class`) becomes fieldless only:

```dao
// This remains valid:
enum Visibility:
  Public
  Private
  Internal

// This becomes ILLEGAL — use enum class instead:
enum Option<T>:
  Some(T)        // ERROR: enum variants cannot carry payloads
  None
```

Payload-bearing variants move exclusively to `enum class`:

```dao
enum class Option<T>:
  Some(value: T)
  None
```

This is a **breaking change** to existing payload-bearing `enum`
declarations.  All existing code using `enum` with payloads must
migrate to `enum class`.

## Design rationale

### Why not a third keyword?

Adding `family`, `choice`, `variant`, `case`, or any other third
keyword creates a new ontological category that must be explained
relative to both `enum` and `class`.  The compound form `enum class`
communicates the semantics directly: it IS the synthesis of enum-ness
and class-ness.

### Why not extend `enum` with named fields?

Because `enum` should stay clean.  Once `enum` can carry rich
structured data, it becomes the junk drawer of the language — every
distinction becomes a mega-sum, every mega-sum becomes a giant match,
and basic structural decomposition dies.

The hard rule: **enum classifies, class composes, enum class
branches.**

### Why not sealed class hierarchies?

Sealed class hierarchies require inheritance machinery (subtyping,
method resolution, potentially vtables).  Dao classes explicitly do
not participate in inheritance hierarchies
(CONTRACT_TYPE_SYSTEM_FOUNDATIONS §8.2).  `enum class` achieves
closedness without introducing subtyping.

### Why named fields only (no positional)?

Positional fields are the root cause of the problem.
`HirFunction(i64, i64, i64, i64, i64, i64, i64)` is the direct
result of allowing anonymous positional payloads.  Named fields
prevent this by construction.

### Why `..` in match?

Without `..`, adding a field to a variant breaks every match site —
the same brittleness that positional payloads cause.  `..` allows
matches that don't care about all fields to remain stable.

## Semantic type category

`enum class` occupies the fourth quadrant:

|            | Atomic    | Structured    |
|------------|-----------|---------------|
| **Closed** | `enum`    | `enum class`  |
| **Open**   | —         | `class`       |

In the compiler's semantic type layer, `enum class` is represented
as `TypeEnum` with named variant fields — the same representation as
current payload enums, but with field name metadata attached.

## Lowering

The compiler controls the lowering strategy.  Valid options include:

- Tagged union (tag + largest-variant payload)
- Arena nodes with kind header
- Per-variant compact storage

No specific lowering strategy is mandated.  The language promises
semantics, not representation.

## Variants do not have methods

`enum class` variants cannot declare methods.  Methods belong on the
outer type:

Methods on the outer `enum class` type are attached through the same
mechanism as class methods — either inside the declaration body after
all variants, or through `extend` blocks once plain-extend syntax is
frozen (currently only `extend Type as Concept:` is frozen in the
syntax contract).

This preserves the principle that variants are structured data, not
behavioral objects.  The exact method-attachment syntax for `enum
class` is deferred until plain `extend` is frozen.

## Migration path

1. All existing `enum` declarations with payload variants must be
   rewritten as `enum class` with named fields.
2. All existing `match` arms that destructure payloads positionally
   must adopt named destructuring.
3. All existing `Enum::Variant(value)` construction must adopt named
   construction: `Enum::Variant(field = value)`.

This affects:
- stdlib (`Option<T>`, `Result<T, E>`)
- examples
- bootstrap probes and subsystems
- test fixtures

## Interaction with other features

- **Generics**: `enum class Option<T>: Some(value: T) | None` works
  naturally.
- **Concepts**: `enum class` types can conform to concepts via
  `extend` blocks.
- **Pattern matching**: exhaustive matching is the primary dispatch
  mechanism.  Future pattern matching extensions (guards, nested
  patterns) apply to `enum class` variants.
- **C ABI**: `enum class` types are not repr-C-compatible unless
  all variants have identical layout.  Interop uses explicit
  conversion.
