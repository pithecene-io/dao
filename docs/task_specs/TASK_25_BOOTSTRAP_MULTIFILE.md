# Task 25 — Bootstrap Multi-file Compilation + Imports (v1)

Status: implementation spec
Phase: Phase 7 — Bootstrap Compiler
Scope: replace assembly-only single-file assumptions with a
deterministic multi-file substrate and first import/module surface

## 1. Objective

Enable the bootstrap compiler to compile a program spanning multiple
Dao source files, with explicit module declarations and import
parsing, while preserving current Tier A frontend semantics inside
each file.

This task does not attempt to solve the whole module system.  It
establishes the minimum structural substrate required so bootstrap
can stop pretending the world is one file.

## 2. Primary design objective

Break the single-file architectural bottleneck without prematurely
designing package management, visibility modifiers, or stdlib
distribution.

## 3. Why this task exists now

The current bootstrap pipeline is assembled by concatenation:

* `bootstrap/shared/base.dao` is the shared substrate
* `bootstrap/assemble.sh` produces `*.gen.dao`
* all resolver/typecheck/HIR work assumes one file root

That was the right move for Tier A.  It is now the main blocker.

Tier B features become distorted if the compiler still has no notion
of file identity, module identity, import edges, or deterministic
cross-file ordering.

Task 25 introduces that substrate first.

## 4. In scope

### 4.1 Source set / compilation model

Introduce a bootstrap-facing compilation entrypoint that accepts:

* one root file path, or
* an explicit list of file paths

and constructs a single program from multiple files.

### 4.2 Module declarations

Add parsing support for top-level module declarations:

```dao
module app::math
```

Module declarations give a file an explicit module identity.

**Syntax note**: `module` is not currently a reserved keyword in
`dao.lex`, `token.h`, or the bootstrap lexer.  Task 25 must add
`KwModule` to the bootstrap lexer keyword table.  The host compiler
lexer and `dao.lex` should be updated in the same diff or a
coordinated follow-up to keep the token surface consistent.

### 4.3 Import declarations

Add parsing support for top-level imports:

```dao
import app::math
import core::io
```

The bootstrap lexer already has `KwImport` and `ColonColon`.  No new
tokens needed for imports.

No aliasing, selective imports, glob imports, or re-export syntax
in v1.

### 4.4 Module graph construction

Build a deterministic module/import graph from the parsed file set.

### 4.5 File-to-module mapping

Each source file maps to exactly one declared module identity.

### 4.6 Cycle detection

Detect import cycles at the module graph layer and emit explicit
diagnostics.

### 4.7 Deterministic processing order

All graph walking, declaration collection, and later semantic phases
must run in deterministic order independent of host filesystem
traversal order.

## 5. Non-goals

### 5.1 Not in v1

* package manager semantics
* search paths beyond explicit file inputs and simple root-relative
  lookup
* visibility modifiers (`pub`, `internal`, etc.)
* selective imports (`import a::b::{c, d}`)
* alias imports (`import a::b as c`)
* re-exports
* incremental rebuilds / watch mode
* stdlib auto-discovery
* import lowering in generated code
* bootstrap self-host replacement of `assemble.sh`

### 5.2 Still intentionally deferred

* generic/concept-aware import semantics
* method/extension imports
* name qualification beyond existing `::` chain semantics
* package-level cycles involving future re-exports

## 6. Syntax surface

### 6.1 Grammar additions

Top-level only:

```ebnf
module_decl := "module" qualified_name NEWLINE
import_decl := "import" qualified_name NEWLINE
```

Where `qualified_name` reuses the existing `identifier ("::" identifier)*`
production.

### 6.2 Placement rules

`module` and `import` declarations must appear before other top-level
declarations in v1.

Valid:

```dao
module app::math
import core::fmt

fn add(a: i32, b: i32): i32 -> a + b
```

Invalid:

```dao
fn f(): void
    pass
import core::fmt
```

### 6.3 Module declaration cardinality

Per file in v1:

* exactly zero or one `module` declaration while parsing
* semantically, exactly one module identity is required by
  compilation

If omitted, the driver may infer module identity from file path only
if the command explicitly opts into path-based inference.  Default
bootstrap v1 behavior should prefer an explicit module declaration
to avoid ambiguity.

Recommendation: require explicit module declarations in multi-file
mode; permit omission only for legacy single-file tests.

## 7. AST changes

Extend the bootstrap AST with:

```
ModuleDeclN { path_lp: i64 }
ImportDeclN { path_lp: i64 }
```

Where `path_lp` points into the shared child index list holding the
qualified-name segment token indices.

Extend the file root representation:

```
FileN { module_decl: i64, import_list_lp: i64, decl_list_lp: i64 }
```

`module_decl` is `-1` when absent.  `import_list_lp` and
`decl_list_lp` point into the index data list as in existing AST
nodes.

## 8. Compilation model

### 8.1 New input/output layer

Introduce a program-level bootstrap compilation layer above the
current per-file parse/resolve/typecheck/lower calls.

Suggested structures:

```
class SourceFile:
  file_id: i64
  path: string
  module_name: string   // canonical joined form for display
  module_segs: i64      // segment count
  source_text: string
  ast_root: i64

class ProgramInput:
  files: Vector<SourceFile>

class ProgramGraph:
  modules: Vector<SourceFile>
  edges: Vector<i64>        // flat pairs: [from_id, to_id, ...]
  topo_order: Vector<i64>   // file_ids in dependency order
  diags: Vector<Diagnostic>
```

### 8.2 Deterministic file identity

Assign stable numeric file IDs in lexical path order after path
normalization.

### 8.3 Deterministic module identity

Store module identity canonically as segment lists, not ad hoc
joined strings, though a joined display form may be cached for
diagnostics.

## 9. Module loading model

### 9.1 v1 loading rules

The bootstrap driver accepts either:

* **explicit file list mode** — all files enumerated by the caller
* **root file mode** — import-driven discovery inside a bounded
  source root

For v1, explicit file list mode is simpler and should be the
required test path.

Optional root-file discovery may be added if it stays deterministic.

### 9.2 Path-to-module consistency

If path-based conventions are used, they must only validate
consistency, not define semantics.

Example: `src/app/math.dao` declares `module app::math`.  If the
implementation checks this convention, mismatches should diagnose
clearly.  Semantic identity comes from the module declaration, not
the folder name.

## 10. Module graph rules

### 10.1 Graph node

Each module is a graph node.

### 10.2 Graph edge

`import a::b` in module `x::y` creates a directed edge from `x::y`
to `a::b`.

### 10.3 Missing target

If an imported module is not present in the provided compilation
set, emit a diagnostic during graph construction.

### 10.4 Duplicate module declarations

If multiple files declare the same module identity in v1, diagnose
as an error.  Do not merge partial modules.

### 10.5 Cycle policy

Import cycles are rejected in v1.

Even though future Dao may support some cyclic visibility model,
bootstrap v1 should stay strict:

* detect cycle
* report cycle path
* abort semantic phases for the strongly connected component

## 11. Parser behavior

### 11.1 Minimal parser change

Do not redesign the parser architecture.

Extend the existing top-level declaration loop to:

1. consume optional leading `module`
2. consume zero or more leading `import`
3. then parse existing top-level declarations as before

### 11.2 Recovery behavior

If `module` or `import` syntax is malformed, recover to newline or
next top-level starter where possible, emit a normal parser
diagnostic, and continue building the file AST.

## 12. Diagnostics

Task 25 diagnostics must cover at least:

* missing module declaration in required-explicit mode
* duplicate module declaration within one file
* `module` after non-import top-level declaration
* `import` after non-import top-level declaration
* malformed module path
* malformed import path
* duplicate module identity across files
* imported module not found
* import cycle with explicit cycle trace
* nondeterministic or invalid file list input normalization errors

Examples:

```
module declaration must appear before top-level declarations
import declaration must appear before top-level declarations
duplicate module 'app::math' declared in 'a.dao' and 'b.dao'
imported module 'core::fmt' not found in compilation set
import cycle detected: app::main -> core::fmt -> app::main
```

## 13. Public API additions

Current per-file API:

```
fn parse(src: string): ParseResult
fn resolve(src: string): ResolveResult
fn typecheck(src: string): TypeCheckResult
fn lower_to_hir(src: string): HirResult
```

Add program-level APIs rather than mutating all existing test
helpers at once.

Suggested additions:

```
fn parse_file(path: string, src: string): ParseResult
fn build_program(files: Vector<SourceInput>): ProgramBuildResult
```

Where `ProgramBuildResult` contains:

* parsed files
* module table
* graph edges
* topological order
* diagnostics

## 14. Test strategy

### 14.1 Parser tests (~8)

* `module` declaration parses
* single `import` parses
* multiple imports parse
* module + imports + declarations parse in correct root shape
* malformed import path reports diagnostic
* malformed module path reports diagnostic
* import after function reports diagnostic
* duplicate module in one file reports diagnostic

### 14.2 Graph tests (~8)

* two files with one import build correctly
* three-module chain topo-sorts correctly
* missing imported module reports error
* duplicate module across files reports error
* self-import cycle reports error
* two-node cycle reports error
* cycle trace is deterministic
* file ordering is deterministic independent of input order

### 14.3 Smoke test

Compile a tiny two-file bootstrap fixture and verify:

* both files parsed
* graph built
* topo order stable
* no semantic work yet beyond graph construction

## 15. Acceptance criteria

1. Bootstrap parser accepts top-level `module` and `import`
   declarations.
2. File AST preserves module declaration and ordered import list.
3. Program-level build layer exists above per-file parsing.
4. Compilation set maps files to unique module identities.
5. Imported modules are resolved at graph-build time.
6. Duplicate modules are rejected.
7. Import cycles are rejected with readable diagnostics.
8. Deterministic file/module ordering is enforced.
9. Existing single-file tests still pass or have explicit
   compatibility shims.
10. At least one two-file smoke fixture passes graph construction.

## 16. Risks

### 16.1 Overreaching into full module design

Task 25 must stop at structural graph formation.  Do not smuggle in
export semantics, visibility, or symbol import rules.

### 16.2 Breaking current single-file tests

Keep current single-file helper paths intact.  Add a program-level
layer in parallel.

### 16.3 Path semantics creep

Avoid turning folder layout into language semantics.  Use paths for
loading, module declarations for meaning.

### 16.4 Keyword introduction

`module` is new syntax surface.  Per AGENTS.md, frozen syntax
decisions require contract awareness.  The keyword must be added to
`dao.lex` and the syntax contract in the same changeset that lands
bootstrap support, or in a coordinated predecessor.

## 17. Explicit deferrals

* exported declaration marking
* imported symbol binding
* cross-file name lookup
* cross-file type lookup
* HIR aggregation
* monomorphization across modules
* stdlib/module search path policy
