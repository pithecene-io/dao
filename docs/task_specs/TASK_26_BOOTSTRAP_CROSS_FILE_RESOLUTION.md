# Task 26 — Bootstrap Cross-file Resolution

Status: implementation spec
Phase: Phase 7 — Bootstrap Compiler
Scope: extend the bootstrap resolver from single-file scope chains to
module-aware cross-file name resolution

## 1. Objective

Allow bootstrap resolution to bind names across files/modules using
the module graph established in Task 25.

Task 26 answers:

> When a file imports another module, what names become available
> locally, and how are qualified references resolved deterministically?

## 2. Primary design objective

Preserve the current resolver's simplicity — two-pass declaration
collection plus body resolution — while extending it to operate over
a graph of files rather than a single monolith.

## 3. Resolver baseline

The existing bootstrap resolver (Task 22) already has the right
spine:

* two-pass: collect top-level declarations, then resolve bodies
* scope chains with parent-linked lookups
* `QualNameE` first-segment resolution against scope
* forward references within one file
* `SymbolKind` enum with Function, Type, Local, Param, Field,
  LambdaParam, GenericParam

The host compiler resolver already implements Module symbol kind and
opaque import binding (Task 6).  Task 26 graduates this to the
bootstrap with full cross-file resolution.

## 4. In scope

### 4.1 Module-level symbol tables

Each module gets a top-level declaration table containing its
directly declared symbols.

### 4.2 Export model

In v1, every top-level declaration in a module is exported by
default.  No explicit `pub` syntax yet.

### 4.3 Import binding rule

`import a::b` binds the final segment `b` in the importing module's
top-level scope as a Module symbol.

Example:

```dao
module app::main
import core::fmt

fn main(): void
    fmt::print("hi")
```

`fmt` is a bound module symbol referring to `core::fmt`.

This matches the rule frozen in `CONTRACT_SYNTAX_SURFACE.md`:

> `import a::b` binds the last segment `b` as the local name;
> qualified references use `b::member` not the full import path

### 4.4 Qualified resolution

Qualified names resolve segment-by-segment:

* first segment resolves in lexical scope
* if it resolves to a Module symbol, subsequent segments resolve
  inside that module's exported symbol table
* if it resolves to a Type/Enum declaration, continue with existing
  member/variant rules where already supported

### 4.5 Cross-file type resolution

Named types may resolve to declarations defined in imported modules
via qualified references (e.g. `math::Vec2`).

### 4.6 Duplicate and ambiguous import diagnostics

Reject conflicting imported module bindings at a scope level that
would make the first segment ambiguous.

## 5. Non-goals

* overload resolution
* alias imports
* selective imports
* wildcard imports
* visibility modifiers
* re-export chains
* method-set lookup across modules
* generic/concept-aware lookup rules
* implicit prelude/module injection beyond existing builtins

## 6. Semantic model

### 6.1 Module namespace

Each module has a namespace containing:

* functions
* structs/classes
* enums
* type aliases

### 6.2 Imported module exposure

An import exposes the imported module name, not all contained
declarations directly.

```dao
import core::fmt
fmt::print("x")   // valid
print("x")        // invalid unless print is in lexical scope
```

This keeps resolution simple and avoids implicit glob semantics.

### 6.3 Default export rule

All top-level declarations are exported from their declaring module
in v1.

### 6.4 Same-module unqualified access

Inside a module, its own top-level declarations are visible
unqualified, exactly as they are now in single-file bootstrap.

## 7. Resolver architecture changes

### 7.1 New symbol kind

Add `Module` to the bootstrap `SymbolKind` enum.  Module symbols
carry a reference to the target module's ID for export-table lookup.

### 7.2 New program-level resolve input

Introduce `ProgramResolveInput` containing:

* parsed files/modules from Task 25
* graph topo order
* per-file AST roots

### 7.3 Pass 1: module declaration collection

For each module in deterministic topo order:

1. create module scope
2. collect top-level declarations into that module's export table
3. register imported module bindings in the module's top-level scope

Since cycles are rejected by Task 25, topo order guarantees
imported module symbol tables exist before dependent module
collection begins.

### 7.4 Pass 2: body resolution

Resolve bodies using existing scope chain, extended with:

* local/block scope
* function scope
* file/module top-level scope (includes import bindings)
* imported module export tables for qualified-second-segment lookup
* builtin types/symbols

### 7.5 Module export table

Suggested structure:

```
class ModuleExports:
  module_id: i64
  symbols: HashMap<i64>   // name -> symbol_idx
```

## 8. Resolution rules

### 8.1 Unqualified identifier

Unqualified identifiers do **not** search imported modules.  They
search only lexical/module-local scope and builtins.

### 8.2 Qualified identifier: imported module prefix

If the first segment resolves to a Module symbol, the remaining
path is resolved against that module's exported declaration table.

### 8.3 Qualified identifier: local type/enum prefix

Existing behavior remains (e.g. `Color::Red` for enum variants).

### 8.4 Imported type names in annotations

Type positions follow the same rule set:

```dao
import app::math
let x: math::Vec2
```

Valid if `Vec2` is exported from `app::math`.

### 8.5 Missing export

If the imported module exists but the requested symbol does not:

```
module 'app::math' has no exported symbol 'Vec3'
```

### 8.6 Duplicate imported local binding

If two imports would bind the same final segment locally:

```dao
import a::fmt
import b::fmt
```

Reject in v1 as a duplicate binding.  No shadowing through imports.

## 9. Data structures

Suggested additions:

```
class ResolvedModule:
  module_id: i64
  file_id: i64
  path_segments: Vector<string>
  exports: HashMap<i64>      // name -> symbol_idx
  imports: Vector<i64>       // module binding symbol indices

class ModuleBinding:
  local_name: string
  target_module_id: i64
  import_node_idx: i64

class ProgramResolveResult:
  modules: Vector<ResolvedModule>
  symbols: Vector<Symbol>
  scopes: Vector<Scope>
  uses: Vector<i64>
  diags: Vector<Diagnostic>
```

The existing `uses` map should now store symbol references with
file/module provenance where needed.

## 10. Diagnostics

Task 26 diagnostics must cover at least:

* unknown imported module binding in qualified lookup
* requested symbol missing from imported module
* duplicate imported local name
* duplicate exported declaration inside a module
* ambiguous or invalid first-segment category for qualified lookup
* cross-file unknown type name in annotation

Examples:

```
duplicate imported name 'fmt' from modules 'a::fmt' and 'b::fmt'
module 'core::fmt' has no exported symbol 'println'
unknown name 'fmt'
unknown type 'math::Vec3'
```

## 11. Test strategy

### 11.1 Resolution tests (~12)

* import binds final segment as Module symbol
* qualified function call into imported module resolves
* qualified type annotation into imported module resolves
* missing imported module symbol diagnoses correctly
* duplicate import final segment diagnoses correctly
* unqualified imported declaration remains unresolved
* same-module unqualified access still works
* imported enum variant qualification resolves
* imported struct field type reference resolves (if supported by
  current type resolver)
* topo-order-independent result stability
* diagnostics include source module/file context
* existing single-file resolver tests still pass

### 11.2 Cross-file smoke fixture

Two or three files:

* `core::fmt` exports a function
* `app::main` imports it
* resolver binds `fmt::print`

Verify: symbols, scopes, uses table populated across files.

## 12. Acceptance criteria

1. Bootstrap resolver accepts a program-level input with multiple
   files/modules.
2. Module symbol kind exists with target-module reference.
3. Import bindings create Module symbols in importing scope.
4. Qualified names resolve through module export tables.
5. Missing exports produce targeted diagnostics.
6. Duplicate import bindings are rejected.
7. Unqualified lookup does not leak across module boundaries.
8. Per-module export tables are populated in topo order.
9. Existing single-file resolver tests still pass.
10. Cross-file smoke fixture passes.

## 13. Risks

### 13.1 Scope chain complexity

Adding module export tables to the lookup chain must not break
existing local/block/function scope resolution.  Module exports are
only consulted through qualified names, never unqualified.

### 13.2 Symbol provenance

Symbols now have cross-file identity.  Diagnostic spans must carry
file context or risk confusing "line 5" messages across different
files.

### 13.3 State threading

The bootstrap uses value-threaded state (RS, TS, etc.).  Program-
level resolution means threading state across multiple file
resolutions.  Keep per-file state isolated where possible and
aggregate only export tables and diagnostics.

## 14. Explicit deferrals

* visibility modifiers (`pub` / `internal`)
* selective or aliased imports
* re-export chains
* method/extension resolution across modules
* generic/concept-aware import semantics
* cross-file overload resolution
