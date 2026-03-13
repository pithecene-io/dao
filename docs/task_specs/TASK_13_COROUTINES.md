# Task 13 — Coroutines and Iteration

Status: design spec (pre-implementation)

## 1. Motivation

Every significant iteration pattern depends on a bottom primitive.
Task 12 defined `Iterator<T>` and `Iterable<T>` concepts with
`has_next`/`next` methods, but those method names are invisible
keywords — the compiler blesses them without admitting it. A
compiler-blessed `Iterable` concept is a lang item wearing a concept
costume. Coroutines are the honest primitive.

For-loop desugaring needs a bottom that is not duck typing on method
names and not a compiler-blessed concept. Coroutines are that bottom.
They also unlock:

- Lazy sequences without materializing collections
- Async foundations (future work)
- Stream processing pipelines

The key insight: `iter`/`next`/`has_next` as magic method names is
just invisible keywords. A coroutine-based iteration model makes the
magic explicit and bounded — `yield` is the only new keyword, and
the compiler state machine transform is the only irreducible magic.

This spec supersedes the iteration protocol in Task 12 §9. The
concept definitions (`Iterator<T>`, `Iterable<T>`) remain valid as
API-level conventions, but `for...in` desugars to coroutine resume,
not concept method dispatch.

## 2. Design Principles

- `yield` is the only new keyword
- The compiler state machine transform is the only irreducible magic
- `for...in` desugars to coroutine resume, not concept method dispatch
- Everything above the primitive (range, custom iteration, composition)
  is expressible in Dao without additional magic
- Coroutines are stackless (no hidden heap allocation for simple cases)

## 3. Generator Functions

A function containing `yield` is a generator function. Calling it
returns a `Generator<T>` where T is the yield type.

```dao
fn range(n: i32): Generator<i32>
    let i = 0
    while i < n:
        yield i
        i = i + 1
```

Rules:
- `yield` is only valid inside a function body (not at top level, not
  in class bodies)
- The return type must be `Generator<T>` where T matches the yield
  expression type
- Multiple `yield` expressions in the same function must yield the
  same type
- A generator function may also use `return` (without a value) to
  terminate early
- `return value` is not valid in a generator function (yields produce
  values, return terminates)

## 4. Generator<T> Type

`Generator<T>` is a compiler-provided type, not user-definable. It
represents a suspended coroutine that yields values of type T.

Operations (compiler intrinsics, not concept methods):
- Resume: advance to next yield point
- Check done: has the generator returned/exhausted?
- Get value: retrieve the most recently yielded value

These operations are NOT exposed as user-callable methods. They exist
only as compiler intrinsics consumed by `for...in` desugaring.

## 5. For-Loop Desugaring

```dao
for x in expr:
    body(x)
```

The expression `expr` must have type `Generator<T>`. The loop variable
`x` is bound to type `T`.

The desugaring is:

```
_gen = expr
while _gen is not exhausted:
    x = resume _gen
    body(x)
```

This is a compiler-internal transformation, not expressible in surface
Dao syntax. The `for...in` loop is the only way to consume a generator.

## 6. Scalar Iteration (decided: Option B)

`for i in 7:` is not valid. Integers do not implicitly convert to
generators. The stdlib provides `range` as an ordinary generator
function:

```dao
for i in range(7):
    body(i)
```

`range` has no special compiler status. It is a regular function
returning `Generator<i32>`.

Rationale: no implicit conversions in `for...in`. The compiler
accepts exactly `Generator<T>` with no exceptions.

## 7. User-Defined Iterables (decided: explicit call)

User-defined types that want to work with `for...in` must provide a
method returning `Generator<T>`. The caller calls it explicitly:

```dao
for x in my_list.iter():
    body(x)
```

The compiler does not implicitly call `.iter()` or look up any
concept conformance. `for...in` sees `Generator<T>` or it is a type
error.

An `Iterable<T>` concept may be provided in stdlib for API
consistency, but it is not compiler-blessed and `for` does not
recognize it. Implicit `.iter()` desugaring is deferred pending
real-world usage observation (see CONTRACT_SYNTAX_SURFACE.md
Non-Laws).

## 8. Interaction with Concepts

Concepts and coroutines are orthogonal but complementary:

```dao
concept Iterable<T>:
    fn iter(self): Generator<T>
```

This concept is user-defined, not compiler-blessed. It provides API
consistency but `for...in` does not check for it — `for` only cares
about `Generator<T>`. A function constrained by `Iterable<T>` can
call `.iter()` and pass the result to a for-loop.

## 9. Interaction with Modes and Resources

Coroutines and modes are orthogonal. A generator created inside
`mode unsafe =>` follows the same resume semantics. Resource regions
do not affect generator behavior.

Memory note: generator state machines require storage for suspended
locals. The allocation strategy (stack, arena, heap) is a backend
decision, not a language-level concern.

## 10. What This Spec Does Not Cover

- Async/await (coroutines as the foundation for async is future work)
- Bidirectional coroutines (send values INTO a generator)
- Coroutine cancellation
- Generator composition/delegation (`yield from` or similar)
- Heap vs stack allocation strategy for generator frames
- Interaction with generic type parameters on generator functions

## 11. Implementation Sequence

### 11.1 Generator primitive

1. Add `yield` keyword to lexer
2. Parse `yield expr` as a statement
3. Add `Generator<T>` as a compiler-provided type in the type system
4. Typecheck generator functions: validate yield type against explicit
   `Generator<T>` return annotation (inference from yield expressions
   is deferred; see CONTRACT_SYNTAX_SURFACE.md Non-Laws)

### 11.2 For-loop desugaring

5. Wire `check_for` to require `Generator<T>` on the iterable
   expression
6. Derive loop variable type T from `Generator<T>`
7. Implement compiler-internal desugaring at HIR/MIR level

### 11.3 Stdlib `range` function (§6 decided: Option B)

8. Implement `range` as a stdlib generator function in
   `stdlib/core/range.dao`
9. Include in prelude auto-import

### 11.4 Iterable concept (optional, non-blocking)

10. Define stdlib `Iterable<T>` concept returning `Generator<T>`
11. This is API convention only — `for...in` does not recognize it

## 12. Syntax Inventory

New keywords:
- `yield` — suspend generator and produce a value

New types:
- `Generator<T>` — compiler-provided suspended coroutine type

New desugaring:
- `for x in expr:` — requires `Generator<T>`, binds x to T

## 13. Open Questions

- Scalar iteration: intrinsic conversion or explicit `range()`? (see §6)
- Should implicit conversion to `Generator<T>` exist for user-defined
  types, or must the user call a method explicitly? (see §7)
- Should `Generator<T>` be exposed as a first-class type that users can
  pass around, store in variables, etc.?
- What is the allocation strategy for generator frames?
