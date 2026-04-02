# Task 27 — Bootstrap Cross-file Typecheck + HIR Aggregation

Status: implementation spec
Phase: Phase 7 — Bootstrap Compiler
Scope: extend bootstrap type checking and HIR lowering from single-file
to program-wide operation over the resolved module graph

## 1. Objective

Enable bootstrap type checking and HIR lowering to operate over
multiple resolved modules, producing a unified program-level HIR
suitable for downstream consumption.

Task 27 answers:

> When a function in one module calls a function or uses a type from
> another module, how does type checking validate the usage and how
> does HIR represent the cross-module reference?

## 2. Primary design objective

Lift the existing single-file type checker and HIR builder to
program scope without redesigning their internals.  The per-file
logic stays; a program-level orchestration layer runs it in
dependency order and stitches results.

## 3. Baseline

### 3.1 Type checker (Task 23)

* two-pass: register declaration types, then check bodies
* type universe: `DaoType` with `TypeKind` enum stored in
  `Vector<DaoType>`
* expression types in `HashMap<i64>` (node_idx → type_idx)
* symbol types in `Vector<i64>` (sym_idx → type_idx)

### 3.2 HIR builder (Task 24)

* single `lower_to_hir(src)` entrypoint
* produces `HirFile` root with flat declaration list
* every HIR expression carries resolved type index
* class/extend methods emitted as flat top-level `HirFunction`
  with `class_owner` tracking

Both currently operate on one file at a time.

## 4. In scope

### 4.1 Program-wide type registration

Type tables must be canonical across the whole program, not one
table per file.

When module `app::main` references `math::Vec2`, the type index
used in `app::main` must be the same canonical index registered by
`app::math`.

### 4.2 Cross-file function call checking

If `app::main` calls `fmt::print("x")`, the checker uses the
function signature from `core::fmt`'s resolved and typed declaration.

### 4.3 Cross-file nominal type use

If `math::Vec2` appears in a type annotation, constructor call, or
field access, it type checks against the declaration in `app::math`.

### 4.4 Cross-file enum variant use

Qualified imported enum variants type check using the originating
enum declaration.

### 4.5 Duplicate names across modules

Distinct modules may export the same final symbol name.  No conflict
exists unless local imports create a duplicate binding (already
rejected by Task 26).

### 4.6 HIR program root

Add a program-level HIR container above the per-file root.

### 4.7 Deterministic lowering order

Type check and lower modules in topological order, breaking ties
lexically by canonical module path.

## 5. Non-goals

* cross-module monomorphization strategy
* binary interface serialization
* incremental compilation caches
* backend object/link unit partitioning
* visibility modifiers
* cross-module optimization passes
* method dispatch across modules (Tier B feature slice)

## 6. Type checking changes

### 6.1 Canonical type table

A single program-level type table accumulates types across all
modules.  When module B depends on module A, A's types are already
registered before B's checking begins (guaranteed by topo order).

Suggested approach: the program-level orchestrator creates one
shared type table.  Each per-module checking pass reads from and
appends to this shared table.

### 6.2 Cross-file symbol type lookup

The per-module checker needs access to symbol types from imported
modules.  The program-level `sym_types` vector (or equivalent)
must span all modules.

When a qualified name `fmt::print` resolves through the module
export table (Task 26), the checker looks up `print`'s type index
in the program-wide sym_types.

### 6.3 Cross-file checking rules

* **Imported function call**: checker uses imported function
  signature; arity and argument types validated normally
* **Imported nominal type**: annotation or constructor position
  resolves to the canonical type registered by the declaring module
* **Imported enum variant**: qualified variant checked against
  originating enum's variant list and payload types
* **Return type from imported call**: flows into local type
  checking as the call expression's result type

### 6.4 What does NOT change

Per-module body checking logic stays the same.  The checker already
resolves identifiers via symbol lookup and types via type indices.
Program-level scope means those indices now span multiple modules,
but the per-expression checking logic is unchanged.

## 7. HIR model changes

### 7.1 Program-level root

Add:

```
HirProgram { module_list_lp: i64 }
HirModule  { name_tok: i64, decl_list_lp: i64, file_id: i64 }
```

Preserve the existing `HirFile` internally if it is deeply embedded;
the key addition is `HirProgram` above the per-module roots.

### 7.2 Cross-module references

HIR identifiers and call targets continue to carry resolved symbol
identity (sym_idx + type_idx).  With program-wide symbol and type
tables, these indices are already cross-module.

No string-based rebinding in HIR.  No inter-module link table.

### 7.3 Deterministic lowering order

Lower modules in topological order, breaking ties lexically by
canonical module path.  The HIR dump must be deterministic for
golden testing.

### 7.4 Per-module declaration preservation

Each `HirModule` preserves its own declaration list.  Methods
continue to be emitted as flat top-level `HirFunction` nodes within
their declaring module.

## 8. Public API additions

Suggested new APIs:

```
fn typecheck_program(input: ProgramResolveResult): ProgramTypeCheckResult
fn lower_program_to_hir(input: ProgramTypeCheckResult): ProgramHirResult
```

Where:

```
class ProgramTypeCheckResult:
  types: Vector<DaoType>
  type_info: Vector<i64>
  expr_types: HashMap<i64>
  sym_types: Vector<i64>
  modules: Vector<ModuleCheckResult>
  diags: Vector<Diagnostic>

class ProgramHirResult:
  hir_nodes: Vector<HirNode>
  hir_idx: Vector<i64>
  types: Vector<DaoType>
  type_info: Vector<i64>
  toks: Vector<Token>       // concatenated or per-module
  diags: Vector<Diagnostic>
  root: i64                 // HirProgram node index
```

Preserve existing single-file helper APIs for tests and bootstrap
development convenience.

## 9. Diagnostics

Task 27 diagnostics must cover at least:

* imported function call argument type mismatch
* imported function call arity mismatch
* imported return type mismatch in local context
* unknown imported type that escaped resolver due to prior recovery
* imported nominal constructor misuse
* incompatible cross-module assignment
* duplicate canonical type registration errors if invariants break

Examples:

```
expected argument of type 'string' for 'fmt::print', got 'i32'
cannot assign 'math::Vec2' to 'physics::Vec2'
enum variant 'Option::Some' expects 1 argument, got 0
```

## 10. Test strategy

### 10.1 Type check tests (~12)

* imported function call type checks
* imported function call arg mismatch diagnoses
* imported struct type annotation checks
* imported nominal type mismatch diagnoses
* imported enum variant constructor checks
* imported type alias use checks
* cross-file return statement type checks
* deterministic multi-module checking order
* existing single-file typecheck tests still pass
* one module depends on another without textual concatenation
* duplicate canonical type registration is rejected if triggered
* recovered parser/resolver errors do not crash program checker

### 10.2 HIR tests (~8)

* multi-file program lowers to HirProgram
* module count in HIR matches source set
* imported call lowers with correct resolved target/type
* imported nominal type is preserved in HIR typing
* deterministic HIR dump ordering
* per-module declarations preserved
* single-file lowering compatibility still works
* unsupported deferred feature still fails intentionally

### 10.3 End-to-end smoke test

A tiny three-file program:

* `core::fmt` exports a print-like function
* `app::math` exports a struct or function
* `app::main` imports both and type checks + lowers successfully

Verify: program HIR root exists, module count is 3, cross-module
call types are correct, HIR dump is deterministic.

## 11. Acceptance criteria

1. Program-wide type registration exists above the single-file
   checker.
2. Imported declarations participate in type checking.
3. Imported function calls and nominal types check correctly.
4. Type identity is canonical across the whole program.
5. HIR gains a program-level root aggregating modules.
6. Lowering order is deterministic.
7. Multi-file HIR dump/golden tests exist.
8. Existing single-file typecheck/HIR helpers still work or have
   explicit wrappers.
9. At least one end-to-end multi-file smoke test passes through
   typecheck and HIR.

## 12. Risks

### 12.1 Type table unification

Some current side tables may assume one file root.  Lift these
carefully.  The key invariant: type indices are global, not
per-file.

### 12.2 Premature separate-compilation design

Task 27 is whole-program bootstrap semantics, not a module ABI
project.  Do not introduce compilation-unit boundaries or object
interfaces.

### 12.3 HIR root churn

Keep the HIR shape change minimal: add `HirProgram` above the
existing root rather than rewriting all node shapes.

### 12.4 State threading across modules

The bootstrap's value-threaded state pattern (TS, HS) must
accumulate across modules without losing earlier module state.
Ensure type/symbol vectors grow monotonically through the topo-
ordered checking sequence.

## 13. Explicit deferrals

* cross-module monomorphization strategy
* binary interface serialization
* incremental compilation caches
* backend object/link unit partitioning
* visibility modifiers
* cross-module optimization passes
* method/extend dispatch across modules
