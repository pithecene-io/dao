# Task 12 — Concepts and Generics

Status: design spec (pre-implementation)

## 1. Motivation

Every significant Phase 5 deliverable depends on a concept and
generics system:

- `print` that works on any type without compiler builtins
- `Iterator[T]` protocol for for-loops
- `List[T]`, `Map[K,V]` for stdlib collections
- `Printable`, `Equatable`, `Hashable` for value-type behavior

Without this, the language cannot express its own primitives. The
runtime would need hardcoded builtins for each type, violating Dao's
design doctrine: *strengthen general mechanisms, don't accumulate
special forms.*

## 2. Design Principles

### 2.1 Implicit where structurally determined, explicit where choice exists

If a behavior is mechanically derivable from a type's structure and
universally expected, it should be available without annotation.

- Printing a class whose fields are all printable? Implicit.
- Custom print format for a class? Explicit conformance.
- Copying a class with all-copyable fields? Implicit.
- Deep-copying a class with pointer fields? Explicit.

The test: *if there is exactly one correct implementation given the
type's structure, it should not require annotation.*

### 2.2 No annotation tax on the common case

Dao must not require the equivalent of
`#[derive(Debug, Clone, PartialEq, Eq, Hash)]` on every type. If 95%
of types want the same behavior, the 5% that don't should opt out —
not the other way around.

### 2.3 Conformance lives with the type

Concept implementations belong at the type's declaration site, not
scattered across separate impl blocks. Related behavior for a type
is colocated, not fragmented.

### 2.4 Static dispatch by default

Concept-constrained generics resolve at compile time via
monomorphization. There is no implicit vtable, no implicit boxing, no
hidden heap allocation. Dynamic dispatch is available through explicit
concept objects when needed.

### 2.5 Concepts are the metalanguage, not syntax

New capabilities are expressed by defining concepts and
implementations, not by adding keywords. `print` is a function
constrained by a concept. `for` loops consume an `Iterator` concept.
This is how the language grows without accumulating special forms.

## 3. Concept Declarations

### 3.1 Syntax

```dao
concept Printable:
    fn to_string(self): string
```

A concept declares a set of required method signatures. The `self`
parameter is the receiver — its type is the conforming type.

### 3.2 Default methods

```dao
concept Equatable:
    fn eq(self, other: Self): bool

    fn ne(self, other: Self): bool -> !self.eq(other)
```

Concepts may provide default method implementations using `->` for
expression bodies or indented blocks. Conforming types inherit defaults
but may override them.

### 3.3 Associated types (deferred)

Associated types (e.g., `type Item` inside a concept) are deferred to
a future spec. The initial system supports only method requirements
and generic parameters.

## 4. Conformance

### 4.1 Inline conformance (primary form)

```dao
class Point:
    x: f64
    y: f64

    as Printable:
        fn to_string(self): string -> "({self.x}, {self.y})"
```

The `as ConceptName:` block inside a class body declares conformance
and provides implementations. This is the primary and preferred form.

Rules:
- `as` introduces a conformance block, not a new scope
- methods inside `as` have access to `self` and all fields
- multiple `as` blocks may appear in one class body
- the conformance block is indented one level deeper than fields

### 4.2 External conformance (for types you don't own)

```dao
extend i32 as Printable:
    fn to_string(self): string -> __i32_to_string(self)
```

The `extend Type as Concept:` form provides conformance for types
declared elsewhere (builtins, other modules). This is necessary for
the stdlib to make builtins conform to core concepts.

Rules:
- `extend` is used only when the type is not yours to modify
- external conformance has the same semantics as inline conformance
- orphan rules (who may extend what) are deferred to the module system
  spec

### 4.3 No standalone impl blocks

There are no free-floating `impl Concept for Type` blocks. Conformance
is either inline (`as` inside the class) or external (`extend`). This
prevents the fragmentation problem where concept implementations
scatter across a codebase.

## 5. Generics

### 5.1 Generic functions

```dao
fn print<T: Printable>(value: T): void
    let text = value.to_string()
    __write_stdout(text)
```

Generic type parameters appear in angle brackets after the function
name. Constraints use `:` (the same annotation token used for type
annotations on variables).

### 5.2 Generic classes

```dao
class List<T>:
    data: *T
    len: i32
    cap: i32

    as Printable where T: Printable:
        fn to_string(self): string
            ...
```

Classes may have type parameters. Conformance blocks may add
additional constraints with `where` clauses.

### 5.3 Multiple constraints

```dao
fn sort<T: Comparable + Printable>(items: List<T>): List<T>
```

Multiple concept constraints use `+`.

### 5.4 Monomorphization

All generic code is monomorphized at compile time. `print<i32>` and
`print<f64>` produce separate compiled functions. There is no type
erasure, no boxing, and no runtime dispatch unless explicitly
requested through concept objects.

## 6. Derived Concepts

### 6.1 The problem with opt-in everything

Most value types want the same baseline behaviors: printability,
equality, copying. Requiring explicit annotation for these universally
expected behaviors creates boilerplate without communicating intent.

### 6.2 Derived concept definition

A concept may be declared `derived`:

```dao
derived concept Printable:
    fn to_string(self): string
```

A derived concept has **automatic structural conformance**: any class
whose fields all conform to the concept automatically conforms, with a
compiler-synthesized implementation. No annotation needed.

### 6.3 Behavior

For a class `Point` with fields `x: f64` and `y: f64`:

- If `f64` conforms to `Printable`, then `Point` automatically
  conforms to `Printable`
- The synthesized `to_string` produces a structural representation
  (e.g., `"Point(3.14, 2.72)"`)
- If the automatic behavior is wrong, the class provides an explicit
  `as Printable:` block, which takes precedence

### 6.4 Opting out

```dao
class SecretKey:
    data: *u8
    len: i32

    deny Printable
```

The `deny ConceptName` declaration inside a class body suppresses
automatic conformance for a derived concept. This is the explicit
decision — "this type intentionally does not support printing" — and
it's visible at the declaration site.

### 6.5 Initial derived concepts

The following concepts are candidates for derived status:

| Concept | Synthesized behavior |
|---------|---------------------|
| `Printable` | Structural field printing: `TypeName(field1, field2)` |
| `Equatable` | Field-wise equality: all fields equal ↔ values equal |
| `Copyable` | Bitwise copy (value types are copyable by default) |
| `Hashable` | Field-wise hash combination |

Non-derived (always explicit):
- `Comparable` (ordering is a design choice)
- `Serializable` (format is a design choice)
- `Iterator` (protocol is behavioral, not structural)

### 6.6 Precedence

1. Explicit `as Concept:` block — always wins
2. Explicit `deny Concept` — suppresses automatic conformance
3. Automatic structural conformance — applies if all fields conform
4. No conformance — if any field doesn't conform and no explicit impl

## 7. Scalar Conformance

### 7.1 Principle

Scalars (`i32`, `f64`, `bool`, `string`) conform to concepts the
same way any type does — through explicit conformance declarations.
No compiler magic.

### 7.2 Compiler intrinsics as the floor

A small set of compiler intrinsic functions provide the operations
that genuinely cannot be expressed in Dao because they are below the
abstraction floor:

- `__i32_to_string(x: i32): string`
- `__f64_to_string(x: f64): string`
- `__bool_to_string(x: bool): string`
- `__write_stdout(msg: string): void`

These are the only compiler-provided primitives. Everything above
composes from them in regular Dao code.

### 7.3 Stdlib conformance

The stdlib prelude provides conformance for scalars using `extend`:

```dao
// stdlib/core/printable.dao
derived concept Printable:
    fn to_string(self): string

extend i32 as Printable:
    fn to_string(self): string -> __i32_to_string(self)

extend f64 as Printable:
    fn to_string(self): string -> __f64_to_string(self)

extend bool as Printable:
    fn to_string(self): string -> __bool_to_string(self)

extend string as Printable:
    fn to_string(self): string -> self

fn print<T: Printable>(value: T): void
    __write_stdout(value.to_string())
```

The `__` prefix intrinsics are the absolute minimum compiler floor.
Everything above is composable Dao code that users can read, extend,
and reason about.

## 8. Self and Receivers

### 8.1 Methods

Methods are functions declared inside a class body or conformance
block that take `self` as their first parameter:

```dao
class Point:
    x: f64
    y: f64

    fn magnitude(self): f64 -> (self.x * self.x + self.y * self.y).sqrt()
```

### 8.2 Self type

Inside a concept, `Self` refers to the conforming type:

```dao
concept Equatable:
    fn eq(self, other: Self): bool
```

### 8.3 Mutating methods (deferred)

Whether methods can mutate `self` (mutable receivers) is deferred.
The initial system treats `self` as an immutable value parameter.
Mutation through methods requires explicit pointer parameters or a
future `mut self` form.

## 9. Iteration Protocol

### 9.1 Iterator concept

```dao
concept Iterator<T>:
    fn has_next(self): bool
    fn next(self): T
```

### 9.2 Iterable concept

```dao
concept Iterable<T>:
    fn iter(self): Iterator<T>
```

### 9.3 For-loop desugaring

```dao
for x in collection:
    body(x)
```

desugars to:

```dao
let _iter = collection.iter()
while _iter.has_next():
    let x = _iter.next()
    body(x)
```

The for-loop is not a special form. It is syntax sugar for the
`Iterable`/`Iterator` protocol. Any type conforming to `Iterable`
works in a for-loop.

### 9.4 Range

```dao
fn range(n: i32): Range
```

`range` is a stdlib function returning a `Range` type that conforms
to `Iterable<i32>`. It is not a language primitive.

## 10. Interaction with Modes and Resources

Concepts and modes are orthogonal. A method called inside
`mode unsafe =>` follows the same concept resolution as outside it.

Resource regions do not affect concept dispatch. A value allocated in
`resource memory Search =>` conforms to the same concepts as one
allocated on the stack.

## 11. Implementation Sequence

### 11.1 Minimal viable generics

1. Parse generic type parameters on functions and classes
2. Resolve generic type parameters in scope
3. Typecheck generic constraints (single concept bound)
4. Monomorphize at call sites (concrete type substitution)

### 11.2 Minimal viable concepts

5. Parse concept declarations (method signatures + defaults)
6. Parse `as Concept:` conformance blocks inside classes
7. Parse `extend Type as Concept:` external conformance
8. Typecheck concept satisfaction (does the type implement all methods?)
9. Resolve concept-constrained method calls

### 11.3 Derived concepts

10. Parse `derived concept` declarations
11. Implement automatic structural conformance checking
12. Implement `deny Concept` opt-out
13. Synthesize default implementations for derived concepts

### 11.4 Iteration protocol

14. Define `Iterator<T>` and `Iterable<T>` concepts in stdlib
15. Implement for-loop desugaring to concept method calls
16. Implement `Range` type with `Iterable<i32>` conformance

### 11.5 Self and methods

17. Parse `self` parameter in method declarations
18. Resolve method calls (`value.method()`) through concept dispatch
19. Implement `Self` type alias in concept bodies

## 12. Syntax Inventory

New keywords introduced:
- `concept` — concept declaration
- `derived` — modifier for structurally-derived concepts
- `as` — conformance block introducer (inside class body)
- `extend` — external conformance declaration
- `deny` — opt-out from derived concept
- `where` — additional constraints on conformance blocks
- `self` — receiver parameter
- `Self` — the conforming type (inside concept bodies)

New syntax forms:
- `<T>` / `<T: Concept>` / `<T: A + B>` — generic parameters
- `as ConceptName:` — inline conformance block
- `extend Type as Concept:` — external conformance
- `deny ConceptName` — derived concept opt-out
- `where T: Concept` — constraint clause

## 13. What This Spec Does Not Cover

- Associated types
- Higher-kinded types
- Mutable receivers (`mut self`)
- Concept objects / dynamic dispatch syntax
- Operator overloading (likely expressed through concepts, but syntax TBD)
- Conditional conformance beyond `where` clauses
- Blanket implementations
- Module-level orphan rules
- Variance of generic type parameters
