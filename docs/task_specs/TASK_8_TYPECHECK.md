# TASK_8_TYPECHECK

Status: implementation spec
Phase: Semantic Frontend
Scope: `compiler/frontend/typecheck/`

## 1. Objective

Implement the first real type-checking pass for Dao.

Task 8 consumes:

- parsed AST
- name-resolution results
- semantic types from `compiler/frontend/types/`

and produces:

- type validation
- typed expression results
- type-check diagnostics
- enough semantic information for later HIR lowering

Task 8 is the bridge from "syntactically and lexically valid program" to
"semantically typed program."

## 2. Non-goals

Task 8 must not implement:

- full generic instantiation machinery
- trait / interface / protocol conformance solving
- overload resolution
- pattern matching exhaustiveness
- enum payload checking beyond what current syntax requires
- method dispatch design
- implicit numeric promotion policy beyond the initial explicit rules
- HIR construction itself
- MIR lowering
- backend behavior

If a feature is not yet frozen in syntax or contracts, do not invent it
here.

## 3. Architectural role

Task 8 sits after:

- lexer
- parser
- AST
- resolver
- semantic type representation

and before:

- HIR lowering
- later semantic optimization
- codegen

The type checker validates and annotates the source-facing program
model. It must remain source-aware enough to produce good diagnostics.

## 4. Directory

Implementation must live under:

```text
compiler/frontend/typecheck/
```

Suggested shape:

```text
compiler/frontend/typecheck/
    type_checker.h
    type_checker.cpp
    expr_typecheck.h
    expr_typecheck.cpp
    stmt_typecheck.h
    stmt_typecheck.cpp
    type_conversion.h
    type_conversion.cpp
    typed_results.h
    typed_results.cpp
    typecheck_tests.cpp
```

Exact filenames may vary, but the separation between checker core,
expression checking, statement checking, conversions, and result
storage should remain clear.

## 5. Inputs

Task 8 consumes:

* AST nodes from `compiler/frontend/ast/`
* resolution bindings from `compiler/frontend/resolve/`
* semantic types from `compiler/frontend/types/`
* compiler-known predeclared symbols/types such as:

  * builtin scalars
  * `bool`
  * predeclared `string`
  * compiler-supported `void` if currently retained

## 6. Outputs

Task 8 must produce at least:

### 6.1 Typed expression information

A side table or equivalent mapping from expression nodes to semantic
`Type*`.

Preferred shape:

* `Expr* -> Type*`

Do not eagerly mutate the AST to embed a large amount of semantic state.

### 6.2 Statement / declaration validation

Validation that statements and declarations are type-correct under the
current language rules.

### 6.3 Diagnostics

Type-check diagnostics with source spans.

### 6.4 Future-facing typed result surface

Enough typed information must be produced that HIR lowering can consume
it later without redoing type checking.

## 7. Core design constraints

### 7.1 Separate syntax from semantics

Parser `TypeNode` is syntax.
Semantic `Type` is compiler meaning.

Task 8 performs the bridge. It must not collapse the two layers.

### 7.2 Side-table semantics

The primary output should be typed side tables, not semantic mutation
scattered through AST node classes.

### 7.3 Canonical semantic types

All type comparisons should use the canonical semantic type system from
Task 7.

### 7.4 Resolution is authoritative for names

Type checking must consume resolver results. It must not redo name
lookup ad hoc.

### 7.5 Source-facing diagnostics matter

Task 8 must preserve enough source structure to produce useful
diagnostics for the playground, CLI, and later IDE features.

## 8. Scope of the first type-checking slice

Task 8 should cover the currently established language surface.

### 8.1 Function declarations

Check:

* parameter type syntax lowers to semantic types
* declared return type lowers to semantic type
* expression-bodied function result matches declared return type
* block-bodied function trailing expression matches declared return type
* `return` statement expression matches declared return type
* if `void` return functions are currently allowed, enforce the chosen
  return rules consistently

### 8.2 Let statements

Check:

* `let name: T = expr` → initializer is assignable to `T`
* `let name = expr` → infer local type from initializer
* `let name: T` → valid only where allowed by contract; local gets
  type `T`
* declaration without initializer must not appear without an explicit
  type

If zero/default initialization is currently part of the language
contract, the checker should respect that and still record the declared
type.

### 8.3 Assignment

Check:

* left-hand side is assignable
* right-hand side type is assignable to left-hand side type

Initial valid assignment targets should be limited to:

* identifiers bound to mutable locals/params if your current language
  model treats locals as assignable
* field/index targets only if already supported by AST + semantics
* otherwise reject unsupported lvalues clearly

### 8.4 If statements

Check:

* condition must be `bool`
* both branches type-check independently
* branch result typing only matters if later expression forms exist;
  for now this is statement validation

### 8.5 While statements

Check:

* condition must be `bool`

### 8.6 For statements

Check:

* iterable expression must be a type currently supported by the initial
  iteration model
* if iteration semantics are not fully defined yet, keep this narrow
  and explicit
* bind loop variable/pattern with the element type

If iteration has not yet been semantically frozen, Task 8 may initially
support only a minimal list/slice-like iteration path or postpone full
iterable typing behind a narrow placeholder rule.

### 8.7 Mode blocks

Check mode-specific rules only insofar as they affect typing now.

Examples:

* `mode unsafe =>` may permit unsafe operations like pointer dereference
* `mode gpu =>` may remain mostly syntactic/structural if GPU typing
  rules are not yet frozen
* `mode parallel =>` may remain mostly structural for now

Do not invent broad effect systems here.

### 8.8 Resource blocks

Type check their contents normally. Resource blocks themselves are
primarily execution/resource semantics, not a separate type system
feature at this stage.

### 8.9 Return statements

Check expression type against function return type.

Decide and enforce whether bare `return` is currently allowed only for
`void` functions or not at all.

### 8.10 Expressions

Task 8 must type-check at least:

* identifiers
* literals
* unary expressions
* binary expressions
* pipe expressions
* lambdas
* calls
* field access
* indexing
* list literals, if currently supported
* parenthesized expressions

## 9. Expression typing rules

## 9.1 Literals

### Integer literals

Assign a default integer literal strategy.

Recommended initial policy:

* unsuffixed integer literals type as `i32` unless context requires
  otherwise

This is simple and predictable for the first checker.

### Float literals

Recommended initial policy:

* unsuffixed float literals type as `f64`

### String literals

Type as predeclared `string`.

### Bool literals

Type as `bool`.

## 9.2 Identifier expressions

Use resolver output to fetch the bound symbol and derive its semantic
type.

## 9.3 Unary expressions

### Address-of `&`

`&expr` yields `*T` if `expr` has type `T` and is addressable.

### Dereference `*`

`*expr` requires operand type `*T` and yields `T`.

If dereference is intended to require `mode unsafe`, enforce that here
or through a narrow context check consumed by typecheck.

### Numeric unary ops

If unary numeric operators exist in current syntax, restrict them to
numeric builtins.

### Logical not

Requires `bool`, yields `bool`.

## 9.4 Binary expressions

Initial binary operator rules should be explicit and conservative.

### Arithmetic

For `+ - * /`:

* both operands must be the same numeric builtin type
* result type is that same type

Do not introduce implicit widening or mixed arithmetic yet unless
explicitly specified.

### Comparison

For `< <= > >=`:

* both operands must be same comparable numeric type
* result is `bool`

### Equality

For `== !=`:

* both operands must have the same type, or one of the explicitly
  allowed comparable categories
* result is `bool`

### Logical

For `&& ||`:

* both operands must be `bool`
* result is `bool`

## 9.5 Pipe expressions

Pipe expressions are first-class and deserve explicit type rules.

For:

```dao
lhs |> rhs
```

interpret the RHS as a pipeline target according to the AST shape.

Initial rule:

* piping into a callable target is equivalent to passing `lhs` as the
  first argument

Examples:

```dao
xs |> filter |x| -> x > 0
```

should type-check according to the callable signature of `filter`.

If the RHS is represented as a special AST form, type checking should
preserve that distinction and validate it directly.

Do not lower pipe expressions away before type checking if doing so
would lose source-quality diagnostics.

## 9.6 Lambdas

Lambdas are expression-bodied only.

Initial recommendation:

* require enough contextual type information to type-check lambda
  parameters and result cleanly
* if no contextual function type exists yet, either:

  * reject untyped lambdas for now, or
  * support only narrow inference rules

Do not accidentally build full Hindley-Milner inference here.

A good initial policy is:

* lambda parameter types come from the expected callable context
* lambda body type is checked against the expected result type

If the current syntax does not allow typed lambda parameters yet, keep
this contextual.

## 9.7 Calls

For function calls:

* callee must have `TypeFunction`
* argument count must match
* each argument must be assignable to corresponding parameter type
* result type is function return type

If named/generic call syntax is not yet present, do not invent it.

## 9.8 Field access

For now, restrict field access to known nominal types such as structs
if semantic struct typing is available.

If struct field typing is not yet implemented, this may remain a narrow
placeholder with explicit TODO coverage in tests.

## 9.9 Indexing

Only support indexing where the indexed type category is already defined
semantically. If arrays/slices/lists are not frozen yet, keep this
narrow or staged.

## 10. Conversion and assignability policy

Task 8 should define an explicit initial assignability relation.

Recommended first rule:

* assignable means exact semantic type equality

Optional narrow exceptions:

* integer literal to declared integer type if within representable
  range later
* float literal to declared float type later

But for the first implementation, exact type equality is the safest and
clearest.

This avoids accidentally committing Dao to broad implicit conversions
too early.

## 11. Local inference policy

Dao should be inference-assisted, not inference-dominated.

So the initial inference surface should be narrow:

* `let x = expr` infers `x` from `expr`
* expression-bodied function return is checked against the declared
  return type
* lambda typing may use expected context, if implemented narrowly

Do not implement:

* whole-function inference
* cross-binding inference
* global HM-style inference

## 12. Function body checking policy

### Block-bodied functions

A block-bodied function may end with a trailing expression whose type
must match the declared return type.

If the function declares `void`, define whether trailing expression is
disallowed or ignored. Be consistent.

### Expression-bodied functions

The single expression after `->` must type-check and match the declared
return type.

## 13. Typed results representation

Task 8 should introduce a stable typed-results surface, for example:

* `Expr* -> Type*`
* `Decl* -> Type*` where useful
* `Symbol* -> Type*` for declared entities where useful

This can live in a dedicated structure such as:

```text
TypedResults
```

owned by the semantic pipeline.

This structure should later be consumable by HIR lowering and tooling.

## 14. Diagnostics

Required initial diagnostic categories include:

* unknown type name
* invalid type annotation
* initializer type mismatch
* assignment type mismatch
* invalid condition type
* invalid operand type for unary operator
* invalid operand types for binary operator
* invalid dereference target
* invalid address-of target
* invalid call target
* wrong argument count
* wrong argument type
* return type mismatch
* unsupported expression in current type checker slice
* lambda lacks required type context, if that policy is chosen

Diagnostics must carry source spans and should aim to be
playground-friendly.

## 15. Context-sensitive rules

Task 8 may need a small notion of typing context, including:

* current function return type
* current mode stack (`unsafe`, `gpu`, `parallel`)
* expected type for contextual expression checking, especially lambdas
* current symbol/type environment as needed from resolution results

Do not turn this into a general effect system.

## 16. Suggested implementation shape

A good implementation split is:

* `TypeChecker`

  * entry point
  * orchestrates checking per file / declaration
* `ExprTypeChecker`

  * returns semantic `Type*`
* `StmtTypeChecker`

  * validates statements under context
* `TypeConversion`

  * assignability / conversion checks
* `TypedResults`

  * side tables for checked semantic output

## 17. Tests

Task 8 is not complete without a serious test corpus.

Required coverage should include:

### Positive cases

* typed and inferred let bindings
* pointer address/deref in allowed contexts
* arithmetic on matching numeric types
* boolean conditions
* function call correctness
* expression-bodied functions
* block-bodied functions with trailing expressions
* simple pipe expressions
* simple lambda-in-context cases, if supported

### Negative cases

* let type mismatch
* assignment mismatch
* non-bool condition
* dereference of non-pointer
* call on non-function
* wrong arg count
* wrong arg type
* return type mismatch
* unsupported mixed numeric arithmetic, if not allowed
* lambda without enough type context, if rejected

### Probe coverage

All syntax probes and relevant examples should either:

* type-check successfully, or
* fail with intentional, asserted diagnostics

## 18. Exit criteria

Task 8 is complete when:

* the compiler can type-check the currently supported source surface
* semantic types are attached through typed side tables
* core diagnostics are emitted reliably
* examples and syntax probes can be checked through the CLI
* the result is ready to feed HIR lowering without redesigning the
  typed layer

## 19. CLI / tooling integration

Add or extend a CLI surface such as:

```text
daoc check file.dao
```

This should run at least:

* parse
* resolve
* typecheck

and print diagnostics.

If helpful, a debug surface may be added for typed expression dumps.

The playground and language tooling should be able to consume Task 8
diagnostics and typed semantic categories without reimplementing
checking logic.

## 20. Stability rules

If implementation pressure suggests any of the following, stop and
revisit the spec before proceeding:

* collapsing semantic `Type` back into syntax-level `TypeNode`
* redoing name resolution inside typecheck
* broad implicit conversion rules without a contract
* unconstrained HM-style inference
* hiding pipe/lambda semantics through premature desugaring that harms
  diagnostics
