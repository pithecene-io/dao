# Task 23 — Bootstrap Type Checker

Status: implementation spec
Phase: Phase 7 — Bootstrap Compiler
Scope: expression/statement type checking over the bootstrap resolver's output

## 1. Objective

Implement Tier A type checking over the bootstrap resolver's symbol
tables and scope chains, validating type correctness for the subset of
Dao syntax supported by the bootstrap parser.

This is the fourth bootstrap subsystem and the first that assigns types
to expressions, validates assignability, and checks function signatures.

## 2. Tier A scope

### 2.1 Type universe

* Builtin types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, bool
* void (function return type)
* string (predeclared)
* Struct types (one per class declaration)
* Enum types (one per enum declaration, with variant payloads)
* Function types (param types + return type)
* Pointer types (pointee type)

### 2.2 Pass 1 — Register declarations

* Type aliases resolved first
* Class struct shells registered (forward-reference support via
  two sub-passes: shells first, then field types)
* Enum types with variant payloads
* Function signatures (params + return type)

### 2.3 Pass 2 — Check bodies

* Function bodies: return type validation, statement checking
* Expression-bodied functions: body assignable to return type

### 2.4 Statement checking

* `let`: type annotation, initializer type, assignability
* `assign`: lvalue, type compatibility
* `if`/`while`: condition must be bool
* `for`: check iterable expression (defer Generator element
  extraction to Tier B)
* `return`: value matches function return type
* `break`: loop depth tracking
* `match`: scrutinee type, arm pattern checking
* `expr stmt`: check expression

### 2.5 Expression checking

* Identifiers → symbol type via resolver uses table
* Int literals → i32 default (adopt integer target type)
* Float literals → f64 default (adopt float target type)
* String literals → string
* Bool literals → bool
* Binary: arithmetic (same numeric type), comparison (→ bool),
  equality (same type → bool), logical (bool → bool), string concat
* Unary: negate (numeric), not (bool)
* Call: function arity + argument types, struct constructors, enum
  variant constructors
* Field access: struct field lookup
* Pipe: LHS becomes first arg to RHS function
* Try (`?`): defer to Tier B (requires generic Option/Result)

### 2.6 Assignability

* Type index equality for builtins and named types
* Struct/enum identity via declaration node index
* Structural comparison for function types

## 3. Tier B deferrals

* Generic type parameters, inference, substitution
* Concept / extend / derived conformance
* Method tables and method dispatch
* Lambda contextual typing
* List literal type inference
* Try operator (requires generic Option/Result)
* Deref / addr-of unary operators
* Index expressions
* Mode / resource block semantics
* Yield / Generator checking
* C ABI type validation

## 4. Architecture

### 4.1 Type representation

```
enum TypeKind:
  TBuiltin
  TVoid
  TString
  TPointer
  TFunction
  TStruct
  TEnum

class DaoType:
  kind: TypeKind
  builtin_id: i64      // for TBuiltin: 0=i8..10=bool
  name: string          // display name
  decl_node: i64        // declaration node index (-1 for builtins)
  info_lp: i64          // fields/params list position in type_info
  info_count: i64       // fields/params count
  ret_type: i64         // for TFunction: return type index
  pointee: i64          // for TPointer: pointee type index
```

### 4.2 Builtin registration

Fixed indices: i8=0, i16=1, i32=2, i64=3, u8=4, u16=5, u32=6,
u64=7, f32=8, f64=9, bool=10, void=11, string=12.

### 4.3 Type checker state

```
class TS:
  // AST + resolver data (from resolve())
  nodes, idx_data, toks, src, symbols, scopes, uses: ...
  // Type checker state
  types: Vector<DaoType>
  type_info: Vector<i64>        // struct fields / fn params type indices
  expr_types: HashMap<i64>      // node_idx → type_idx
  sym_types: HashMap<i64>       // sym_idx → type_idx
  diags: Vector<Diagnostic>
  return_type: i64              // current function return type (-1)
  loop_depth: i64
  uses_map: HashMap<i64>        // tok_idx → sym_idx (built from resolver)
```

### 4.4 Output

```
class TypeCheckResult:
  types: Vector<DaoType>
  expr_types: HashMap<i64>
  sym_types: HashMap<i64>
  diags: Vector<Diagnostic>
  node_count: i64
```

## 5. Test strategy

### 5.1 Golden type tests (~15 tests)

* Correct: simple arithmetic, let with type, function call, return
* Error: type mismatch in assignment, non-bool condition,
  arity mismatch, unknown type, break outside loop
* Struct constructor, field access
* Enum variant constructor

### 5.2 Self-typecheck smoke test

Parse + resolve + typecheck a real Dao source file.

## 6. Acceptance criteria

1. A maintained bootstrap type checker exists as `impl.dao` fragment.
2. Two-pass: register declarations, then check bodies.
3. Expression types assigned via side table.
4. Type errors produce diagnostics with spans.
5. Golden tests for correct and incorrect programs.
6. Self-typecheck smoke test.
7. Tier A scope and Tier B deferrals documented.

## 7. Implementation order

1. Finalize this spec
2. Define type representation + TS state + helpers
3. Implement builtin registration
4. Implement type node resolution (AST NamedT/PointerT → type index)
5. Implement Pass 1 (register type aliases, struct shells, enums,
   function signatures)
6. Implement expression checking
7. Implement statement checking
8. Implement Pass 2 (check function bodies)
9. Add golden tests + self-typecheck smoke
10. Update assemble.sh, docs, README
