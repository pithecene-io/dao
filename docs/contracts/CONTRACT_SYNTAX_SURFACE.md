# CONTRACT_SYNTAX_SURFACE.md

## Purpose

Defines the currently frozen syntax surface for Dao's early design phase.

## Block Structure

- Dao is indentation-significant.
- `if`, `while`, and `for` introduce blocks with `:`.
- Block-bodied functions introduce blocks by indentation alone.
- `mode` and `resource` introduce semantic/resource blocks with `=>`.

## Functions

### Block-bodied form

```dao
fn read(ptr: *i32): i32
    let value: i32
    return value
```

Rules:
- no token follows the return type
- the body begins on the next indented line
- `return` is always required; there is no implicit tail-expression return

### Expression-bodied form

```dao
fn add(a: i32, b: i32): i32 -> a + b
```

Rules:
- `->` denotes a single yielded expression
- expression-bodied forms do not open suites

### Extern declaration form

```dao
extern fn print(msg: string): void
```

Rules:
- `extern fn` declares an externally-provided function with no body
- extern declarations produce `declare` (not `define`) in codegen
- a return type annotation is required on extern declarations
- a non-extern `fn` without a body is a parse error, except inside
  concept bodies where bare signatures declare required methods

## Lambdas

```dao
|x| -> x + 1
|a, b| -> a + b
```

Rules:
- lambdas are expression-only
- `->` in lambdas has the same meaning as in expression-bodied functions

## Pipelines

```dao
data |> filter is_valid |> map normalize
```

Rules:
- `|>` is left-associative
- pipelines are first-class, not macro sugar

## Let Bindings

```dao
let x: i32 = 42
let y = compute()
let value: i32
```

Rules:
- `let name: T = expr` — typed with initializer
- `let name = expr` — inferred type from initializer
- `let name: T` — typed without initializer
- `let name` without type or initializer is illegal
- the uninitialized form requires an explicit type annotation
- initialization semantics for the uninitialized form are not yet frozen

## Class Declarations

```dao
class Point:
    x: f64
    y: f64
```

Rules:
- `class <name> :` introduces a class declaration
- the body is an indented block of field specifiers
- each field specifier is `name: type` on its own line
- field specifiers are not statements; `let` is not used inside class bodies
- the body must contain at least one field
- field names must be unique within a class

## Concepts

### Concept declaration

```dao
concept Printable:
    fn to_string(self): string
```

Rules:
- `concept <name> :` introduces a concept (behavioral contract)
- the body is an indented block of method signatures
- methods use `self` as the receiver parameter
- `self` is a reserved keyword; it is valid only as a method's first
  parameter name and in expression position (e.g. `self.x`)
- methods may have default implementations using `->` or block bodies
- within a concept declaration, the concept name in type position refers
  to the conforming type (there is no `Self` keyword)

### Derived concepts

```dao
derived concept Printable:
    fn to_string(self): string
```

Rules:
- `derived concept` declares a concept with automatic structural
  conformance
- any class whose fields all conform to the concept automatically
  conforms, with a compiler-synthesized implementation
- explicit `as` blocks take precedence over derived conformance
- `deny ConceptName` inside a class body opts out of derivation

### Inline conformance

```dao
class Point:
    x: f64
    y: f64

    as Printable:
        fn to_string(self): string -> "({self.x}, {self.y})"
```

Rules:
- `as ConceptName:` inside a class body declares conformance
- methods inside `as` have access to `self` and all fields
- multiple `as` blocks may appear in one class body

### External conformance

```dao
extend i32 as Printable:
    fn to_string(self): string -> __i32_to_string(self)
```

Rules:
- `extend Type as Concept:` provides conformance for types declared
  elsewhere
- semantically identical to inline conformance
- orphan rules are deferred to the module system spec

### Deny

```dao
class SecretKey:
    data: *u8
    len: i32

    deny Printable
```

Rules:
- `deny ConceptName` suppresses automatic derived conformance
- only meaningful inside a class body for derived concepts

## Generics

### Generic functions

```dao
fn print<T: Printable>(value: T): void
    __write_stdout(value.to_string())
```

Rules:
- `<T>` after a function or class name introduces type parameters
- `T: Concept` constrains a type parameter
- `T: A + B` applies multiple constraints
- `where T: Concept` provides additional constraints on conformance
  blocks

### Generic classes

```dao
class List<T>:
    data: *T
    len: i32
    cap: i32
```

Rules:
- classes may have type parameters
- conformance blocks may add `where` constraints

## Generator Functions

### Generator declaration

```dao
fn range(n: i32): Generator<i32>
    let i = 0
    while i < n:
        yield i
        i = i + 1
```

Rules:
- `yield expr` is a statement that suspends the generator and produces
  a value
- `yield` is only valid inside a function whose return type is
  `Generator<T>`
- the yielded expression type must match the element type `T`
- multiple `yield` statements in the same function must yield the same
  type
- `return` (without a value) terminates the generator early
- `return value` is not valid in a generator function

### Generator<T> type

Rules:
- `Coroutine` is the primitive resumable execution type
- `Generator<T>` is an alias for a coroutine that yields `T` and
  receives nothing
- in surface syntax, `Generator<T>` is the spelling used for
  generator return types; the underlying `Coroutine` primitive is
  not yet directly expressible
- `Generator<T>` requires exactly one type argument
- `Generator<T>` is not user-definable
- coroutine/generator operations (resume, check done, get value) are
  compiler intrinsics not exposed as user-callable methods

### For-in consumption

```dao
for x in range(10):
    body(x)
```

Rules:
- the iterable expression in `for...in` must have type `Generator<T>`
- the loop variable is bound to element type `T`
- `for...in` is the only way to consume a generator in surface syntax

## Non-Laws

This contract does not yet freeze:
- receiver declaration syntax beyond `self`
- mutable receiver syntax (`mut self`)
- class construction syntax
- pattern matching syntax
- import alias syntax
- concept object / dynamic dispatch syntax
- operator overloading syntax
- associated types inside concepts
- `sealed` modifier for concepts and classes
- generator delegation (`yield from` or similar)
- bidirectional coroutines (send values into a generator)
- generator type inference from yield expressions without explicit
  return type annotation
- allocation strategy for generator frames
- `Coroutine` as a directly expressible surface type
- `Coroutine` parameterization (yield type, receive type, return type)

## Modes and Resources

```dao
mode unsafe =>
    value = *ptr

resource memory Search =>
    let open = PriorityQueue[NodeId]()
```

Rules:
- `mode <name> =>` introduces an execution/safety context
- `resource <kind> <name> =>` introduces a resource-binding context
- `=>` is reserved for semantic-context entry, not structural blocks

## Namespace Qualification

```dao
import net::http
http::get(url)
```

Rules:
- `::` is the namespace/module path separator
- `.` is for runtime member access on values
- these two are not interchangeable
- `import a::b` binds the last segment `b` as the local name; qualified
  references use `b::member` not the full import path

## Arrow Taxonomy

- `:` is used for type annotations and control-flow block introducers
- `::` is used for namespace and module path qualification
- `->` is used for expression-bodied functions and lambdas
- `=>` is used only for `mode` and `resource` suites

## Formal Grammar Placement

The parser-facing grammar is maintained under `spec/grammar/`:

- `spec/grammar/dao.ebnf`
- `spec/grammar/dao.lex`
- `spec/grammar/indentation_rules.md`

This contract freezes the language-facing syntax intent. The grammar files carry the technical productions used to guide parser implementation.
