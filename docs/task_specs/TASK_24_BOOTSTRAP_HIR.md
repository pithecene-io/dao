# Task 24 — Bootstrap HIR

Status: implementation spec
Phase: Phase 7 — Bootstrap Compiler
Scope: lower typed AST into compiler-owned HIR for the Tier A subset

## 1. Objective

Prove the bootstrap compiler can lower typed AST into compiler-owned
HIR for the already-supported Tier A language slice.

This is not "design the perfect HIR" — it is the first semantic
backbone that validates the AST-to-IR boundary and gives every future
frontend feature a place to land.

## 2. Primary design objective

Lock the semantic-to-IR boundary before expanding language surface
area further.

## 3. What HIR is for

HIR is where Dao reveals its true shape:
- what a function really is after desugaring
- how name binding survives lowering
- whether features compose into a coherent internal model
- where implicit behavior becomes explicit

HIR is a language-design instrument, not just a compiler milestone.

## 4. Tier A HIR scope

### 4.1 Declarations

* `HirFunction` — name, params (typed), return type, body stmts or
  expr body. Extern fns included (no body).
* `HirStruct` — name, fields (typed). No methods in Tier A.
* `HirEnum` — name, variants with optional payload types.
* `HirTypeAlias` — name, target type.
* `HirFile` — top-level container of declarations.

### 4.2 Statements

* `HirLet` — name, type, initializer (optional).
* `HirAssign` — target (lvalue), value.
* `HirIf` — condition, then body, else body.
* `HirWhile` — condition, body.
* `HirFor` — variable, iterable, body.
* `HirReturn` — value (optional).
* `HirBreak`
* `HirMatch` — scrutinee, arms. Reserved for Tier B pattern
  expansion; Tier A desugars to if/else chain per the C++ builder.
* `HirExprStmt` — expression.

### 4.3 Expressions

* `HirIntLit`, `HirFloatLit`, `HirStringLit`, `HirBoolLit`
* `HirIdent` — resolved symbol reference (sym_idx + type_idx)
* `HirBinary` — op, left, right, result type
* `HirUnary` — op, operand, result type
* `HirCall` — callee, args, result type
* `HirFieldAccess` — object, field name, result type
* `HirPipe` — left, right (preserved per contract)
* `HirQualName` — segments (for enum variant access)

### 4.4 Types

HIR nodes reference type indices from the type checker's type table.
No new type representation — reuse `DaoType` / `TypeKind` from
Task 23.

### 4.5 Desugaring performed

* Expression-bodied fns → block with return statement
* Match → if/else chain with discriminant comparisons (Tier A)
* Pipe preserved as-is (MIR lowers it)

### 4.6 What is NOT in Tier A HIR

* Generics / monomorphization (MIR concern)
* Methods / extend (desugared before HIR in Tier B)
* Concepts / conformance
* Mode / resource blocks (reserved in enum, not implemented)
* Lambda lowering
* List literal lowering
* Try operator
* CFG construction (MIR concern)
* Optimization hooks
* Backend concerns

## 5. HIR node model

Single `enum HirNode` with payload variants, stored in
`Vector<HirNode>` arena (same pattern as AST `Node` enum).

Shared index list `Vector<i64>` for variable-length children
(params, args, body stmts).

Every HIR expression carries its resolved type index.

## 6. Lowering input / output

**Input**: `TypeCheckResult` from Task 23 (contains typed AST,
type tables, expression types, symbol types, diagnostics).

**Output**: `HirResult` containing:
* `hir_nodes: Vector<HirNode>` — HIR arena
* `hir_idx: Vector<i64>` — shared index list
* `types: Vector<DaoType>` — type table (passed through)
* `type_info: Vector<i64>` — type field/param info
* `diags: Vector<Diagnostic>` — lowering diagnostics
* `root: i64` — HirFile node index

## 7. Public API

```
fn lower_to_hir(src: string): HirResult
```

Internally: calls `typecheck(src)`, then lowers.

## 8. HIR textual dump

A `hir_dump(result: HirResult): string` or print-based dump for
golden tests.  Format: indented tree showing node kinds, names,
types, and children.

## 9. Test strategy

### 9.1 Golden HIR dump tests (~12 tests)

* Simple fn with return
* Let binding with type
* If/else
* While loop
* Binary expression with type annotation
* Function call
* Struct constructor + field access
* Enum declaration
* Expression-bodied fn
* Extern fn (no body)
* Full program (enum + class + fn + main)
* Unsupported construct produces clear error

### 9.2 Self-lower smoke test

Lower a real Dao source file; verify HIR node count > 0.

## 10. Acceptance criteria

1. HIR node model covers Tier A constructs.
2. Lowering pass consumes TypeCheckResult, produces HirResult.
3. Every HIR expression carries its resolved type.
4. Desugaring: expr-bodied fns become block+return.
5. Match desugared to if/else chain (HirMatch reserved for Tier B).
6. HIR textual dump exists for golden testing.
7. Golden tests for representative programs.
8. Unsupported Tier B constructs fail intentionally.
9. Self-lower smoke test passes.

## 11. Risks

### 11.1 Type table access

The lowering pass needs the type checker's expr_types and sym_types
maps. These are `HashMap<i64>` — O(1) lookup, no issue.

### 11.2 File size

The `impl.dao` fragment will be ~800-1200 lines (HIR nodes, lowering
pass, dump, tests). Assembled with base + resolver + typechecker,
total ~5000 lines. Pushable but acceptable.

### 11.3 Match desugaring

For Tier A, match desugars to if/else chain with value comparisons.
`HirMatch` variant is reserved in the enum for Tier B pattern
compilation. This matches the C++ HIR builder's approach.
