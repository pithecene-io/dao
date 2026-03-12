# Task 12 — Traits and Generics

Status: design spec (pre-implementation)

## 1. Motivation

Every significant Phase 5 deliverable depends on a trait and generics
system:

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

Trait implementations belong at the type's declaration site, not
scattered across separate impl blocks. Related behavior for a type
is colocated, not fragmented.

### 2.4 Static dispatch by default

Trait-constrained generics resolve at compile time via
monomorphization. There is no implicit vtable, no implicit boxing, no
hidden heap allocation. Dynamic dispatch is available through explicit
trait objects when needed.

### 2.5 Traits are the metalanguage, not syntax

New capabilities are expressed by defining traits and implementations,
not by adding keywords. `print` is a function constrained by a trait.
`for` loops consume an `Iterator` trait. This is how the language
grows without accumulating special forms.

## 3. Trait Declarations

### 3.1 Syntax

```dao
trait Printable:
    fn print(self): string
```

A trait declares a set of required method signatures. The `self`
parameter is the receiver — its type is the conforming type.

### 3.2 Default methods

```dao
trait Equatable:
    fn eq(self, other: Self): bool

    fn ne(self, other: Self): bool -> !self.eq(other)
```

Traits may provide default method implementations using `->` for
expression bodies or indented blocks. Conforming types inherit defaults
but may override them.

### 3.3 Associated types (deferred)

Associated types (e.g., `type Item` inside a trait) are deferred to a
future spec. The initial system supports only method requirements and
generic parameters.

## 4. Conformance

### 4.1 Inline conformance (primary form)

```dao
class Point:
    x: f64
    y: f64

    as Printable:
        fn print(self): string -> "({self.x}, {self.y})"
```

The `as TraitName:` block inside a class body declares conformance and
provides implementations. This is the primary and preferred form.

Rules:
- `as` introduces a conformance block, not a new scope
- methods inside `as` have access to `self` and all fields
- multiple `as` blocks may appear in one class body
- the conformance block is indented one level deeper than fields

### 4.2 External conformance (for types you don't own)

```dao
extend i32 as Printable:
    fn print(self): string -> to_string(self)
```

The `extend Type as Trait:` form provides conformance for types
declared elsewhere (builtins, other modules). This is necessary for
the stdlib to make builtins conform to core traits.

Rules:
- `extend` is used only when the type is not yours to modify
- external conformance has the same semantics as inline conformance
- orphan rules (who may extend what) are deferred to the module system
  spec

### 4.3 No standalone impl blocks

There are no free-floating `impl Trait for Type` blocks. Conformance
is either inline (`as` inside the class) or external (`extend`). This
prevents the fragmentation problem where trait implementations scatter
across a codebase.

## 5. Generics

### 5.1 Generic functions

```dao
fn print<T: Printable>(value: T): void
    let text = value.print()
    write_stdout(text)
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
        fn print(self): string
            ...
```

Classes may have type parameters. Conformance blocks may add
additional constraints with `where` clauses.

### 5.3 Multiple constraints

```dao
fn sort<T: Comparable + Printable>(items: List<T>): List<T>
```

Multiple trait constraints use `+`.

### 5.4 Monomorphization

All generic code is monomorphized at compile time. `print<i32>` and
`print<f64>` produce separate compiled functions. There is no type
erasure, no boxing, and no runtime dispatch unless explicitly
requested through trait objects.

## 6. Universal Traits

### 6.1 The problem with opt-in everything

Most value types want the same baseline behaviors: printability,
equality, copying. Requiring explicit annotation for these universally
expected behaviors creates boilerplate without communicating intent.

### 6.2 Universal trait definition

A trait may be declared `universal`:

```dao
universal trait Printable:
    fn print(self): string
```

A universal trait has **automatic structural conformance**: any class
whose fields all conform to the trait automatically conforms, with a
compiler-synthesized implementation. No annotation needed.

### 6.3 Behavior

For a class `Point` with fields `x: f64` and `y: f64`:

- If `f64` conforms to `Printable`, then `Point` automatically
  conforms to `Printable`
- The synthesized `print` produces a structural representation
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

The `deny TraitName` declaration inside a class body suppresses
automatic conformance for a universal trait. This is the explicit
decision — "this type intentionally does not support printing" — and
it's visible at the declaration site.

### 6.5 Initial universal traits

The following traits are candidates for universal status:

| Trait | Synthesized behavior |
|-------|---------------------|
| `Printable` | Structural field printing: `TypeName(field1, field2)` |
| `Equatable` | Field-wise equality: all fields equal ↔ values equal |
| `Copyable` | Bitwise copy (value types are copyable by default) |
| `Hashable` | Field-wise hash combination |

Non-universal (always explicit):
- `Comparable` (ordering is a design choice)
- `Serializable` (format is a design choice)
- `Iterator` (protocol is behavioral, not structural)

### 6.6 Precedence

1. Explicit `as Trait:` block — always wins
2. Explicit `deny Trait` — suppresses automatic conformance
3. Automatic structural conformance — applies if all fields conform
4. No conformance — if any field doesn't conform and no explicit impl

## 7. Self and Receivers

### 7.1 Methods

Methods are functions declared inside a class body or conformance
block that take `self` as their first parameter:

```dao
class Point:
    x: f64
    y: f64

    fn magnitude(self): f64 -> (self.x * self.x + self.y * self.y).sqrt()
```

### 7.2 Self type

Inside a trait, `Self` refers to the conforming type:

```dao
trait Equatable:
    fn eq(self, other: Self): bool
```

### 7.3 Mutating methods (deferred)

Whether methods can mutate `self` (mutable receivers) is deferred.
The initial system treats `self` as an immutable value parameter.
Mutation through methods requires explicit pointer parameters or a
future `mut self` form.

## 8. Iteration Protocol

### 8.1 Iterator trait

```dao
trait Iterator<T>:
    fn has_next(self): bool
    fn next(self): T
```

### 8.2 Iterable trait

```dao
trait Iterable<T>:
    fn iter(self): Iterator<T>
```

### 8.3 For-loop desugaring

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

### 8.4 Range

```dao
fn range(n: i32): Range
```

`range` is a stdlib function returning a `Range` type that conforms
to `Iterable<i32>`. It is not a language primitive.

## 9. Interaction with Modes and Resources

Traits and modes are orthogonal. A method called inside
`mode unsafe =>` follows the same trait resolution as outside it.

Resource regions do not affect trait dispatch. A value allocated in
`resource memory Search =>` conforms to the same traits as one
allocated on the stack.

## 10. Implementation Sequence

### 10.1 Minimal viable generics

1. Parse generic type parameters on functions and classes
2. Resolve generic type parameters in scope
3. Typecheck generic constraints (single trait bound)
4. Monomorphize at call sites (concrete type substitution)

### 10.2 Minimal viable traits

5. Parse trait declarations (method signatures + defaults)
6. Parse `as Trait:` conformance blocks inside classes
7. Parse `extend Type as Trait:` external conformance
8. Typecheck trait satisfaction (does the type implement all methods?)
9. Resolve trait-constrained method calls

### 10.3 Universal traits

10. Parse `universal trait` declarations
11. Implement automatic structural conformance checking
12. Implement `deny Trait` opt-out
13. Synthesize default implementations for universal traits

### 10.4 Iteration protocol

14. Define `Iterator<T>` and `Iterable<T>` traits in stdlib
15. Implement for-loop desugaring to trait method calls
16. Implement `Range` type with `Iterable<i32>` conformance

### 10.5 Self and methods

17. Parse `self` parameter in method declarations
18. Resolve method calls (`value.method()`) through trait dispatch
19. Implement `Self` type alias in trait bodies

## 11. Syntax Inventory

New keywords introduced:
- `trait` — trait declaration
- `universal` — modifier for automatically-derived traits
- `as` — conformance block introducer (inside class body)
- `extend` — external conformance declaration
- `deny` — opt-out from universal trait
- `where` — additional constraints on conformance blocks
- `self` — receiver parameter
- `Self` — the conforming type (inside trait bodies)

New syntax forms:
- `<T>` / `<T: Trait>` / `<T: A + B>` — generic parameters
- `as TraitName:` — inline conformance block
- `extend Type as Trait:` — external conformance
- `deny TraitName` — universal trait opt-out
- `where T: Trait` — constraint clause

## 12. What This Spec Does Not Cover

- Associated types
- Higher-kinded types
- Mutable receivers (`mut self`)
- Trait objects / dynamic dispatch syntax
- Operator overloading (likely expressed through traits, but syntax TBD)
- Conditional conformance beyond `where` clauses
- Blanket implementations
- Module-level orphan rules
- Variance of generic type parameters
