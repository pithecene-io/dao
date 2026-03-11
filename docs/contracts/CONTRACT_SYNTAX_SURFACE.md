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
- a non-extern `fn` without a body is a parse error

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

## Non-Laws

This contract does not yet freeze:
- receiver declaration syntax
- class construction syntax
- class method syntax
- pattern matching syntax
- import alias syntax


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
