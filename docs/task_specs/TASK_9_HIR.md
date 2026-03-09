# TASK_9_HIR

Status: implementation spec
Phase: Semantic Frontend + HIR
Scope: `compiler/ir/hir/`

## 1. Objective

Implement the first High-Level Intermediate Representation (HIR) for Dao.

Task 9 consumes:

- parsed AST
- resolution results
- semantic types
- typed expression side tables

and produces:

- a typed, target-agnostic HIR
- explicit semantic structure suitable for later lowering to MIR
- a form easier to analyze than AST while still preserving Dao-level
  constructs

HIR is the semantic bridge between source-facing frontend structures and
lower operational IR.

## 2. Non-goals

Task 9 must not implement:

- MIR construction
- control-flow graph normalization
- LLVM lowering
- backend-specific calling convention details
- borrow/lifetime systems
- full closure conversion
- generic monomorphization
- trait/interface conformance solving
- optimization passes

HIR should remain high-level and target-agnostic.

## 3. HIR role

HIR should:

- be simpler and more explicit than AST
- retain semantic constructs that matter to Dao
- rely on resolved symbols and semantic types
- remove parser-only syntax noise
- still preserve source-correlated structure for diagnostics and tooling

HIR should not be a glorified AST clone.
HIR should not be a low-level CFG form.

## 4. Directory

Implementation must live under:

```text
compiler/ir/hir/
```

Suggested shape:

```text
compiler/ir/hir/
    hir.h
    hir.cpp
    hir_kind.h
    hir_context.h
    hir_context.cpp
    hir_module.h
    hir_decl.h
    hir_stmt.h
    hir_expr.h
    hir_builder.h
    hir_builder.cpp
    hir_printer.h
    hir_printer.cpp
    hir_tests.cpp
```

Exact filenames may vary, but the conceptual split should remain clear.

## 5. Inputs

Task 9 consumes:

* AST
* resolver bindings
* semantic types from `frontend/types`
* typed results from `frontend/typecheck`

Resolution and type checking are authoritative. HIR construction must
not redo those passes.

## 6. Outputs

Task 9 must produce:

* a compiler-owned HIR module/unit representation
* typed HIR expressions
* typed HIR statements/declarations
* explicit HIR nodes for Dao-specific semantics
* stable printer/debug dump support

## 7. Core design principles

### 7.1 Typed by construction

Every HIR expression node must carry a semantic `Type*`.

HIR should not require re-deriving expression types from scratch.

### 7.2 Symbol-linked, not name-string-based

HIR should reference resolved semantic identities, not raw source names
wherever practical.

### 7.3 Preserve Dao semantics

HIR must preserve the constructs that are semantically important to Dao:

* pipes
* lambdas
* mode regions
* resource regions

Do not erase these too early.

### 7.4 Remove syntax-only noise

HIR should not preserve parser-only grouping constructs unless needed
for source fidelity.

Examples:

* parenthesized expressions should usually disappear
* syntax-only type nodes should not survive
* punctuation distinctions that have no semantic meaning should not
  survive

### 7.5 Target-agnostic

HIR must not contain LLVM types, backend handles, ABI lowering
artifacts, or machine details.

## 8. What HIR should preserve vs lower

### 8.1 Preserve in HIR

Preserve as first-class HIR nodes:

* function declarations
* parameters
* local bindings
* assignments
* if / while / for
* return
* call
* field access
* indexing
* pipe expressions
* lambdas
* mode blocks
* resource blocks
* literals
* identifiers / symbol refs

### 8.2 Lower away before or during HIR construction

Lower or discard:

* syntax-only `TypeNode`
* parenthesized expressions
* purely lexical grouping with no semantic meaning
* unresolved names

## 9. HIR ownership and allocation

HIR should use compiler-owned arena/context allocation, consistent with
AST and semantic types.

Recommended model:

* `HirContext` owns all HIR nodes
* HIR nodes have stable identity
* node categories use class hierarchy + kind tags, not giant
  `std::variant`

## 10. HIR layers and major node categories

Suggested hierarchy:

* `HirNode`
  * `HirDecl`
  * `HirStmt`
  * `HirExpr`

All HIR expressions must carry semantic `Type*`.

### 10.1 Declaration categories

At minimum:

* `HirFunction`
* `HirStructDecl` if current syntax already supports structs
* `HirAliasDecl` if aliases are in current surface

### 10.2 Statement categories

At minimum:

* `HirLet`
* `HirAssign`
* `HirIf`
* `HirWhile`
* `HirFor`
* `HirReturn`
* `HirExprStmt`
* `HirMode`
* `HirResource`

### 10.3 Expression categories

At minimum:

* `HirLiteral`
* `HirSymbolRef`
* `HirUnary`
* `HirBinary`
* `HirCall`
* `HirField`
* `HirIndex`
* `HirPipe`
* `HirLambda`

## 11. Treatment of specific language features

### 11.1 Functions

HIR function nodes should contain:

* symbol identity
* parameter list with semantic types
* declared return type
* body form

Block-bodied functions should become explicit HIR bodies.
Expression-bodied functions may either:

* remain as a compact expression form in HIR, or
* normalize to an explicit return/body form

Recommended:

normalize expression-bodied functions to a standard body shape for
uniformity.

### 11.2 Let statements

HIR let bindings should contain:

* bound symbol
* semantic declared or inferred type
* optional initializer

If the source allowed `let x: T` without initializer, preserve that
explicitly.

### 11.3 Assignment

HIR assignments should have explicit assignable target categories.

Do not keep arbitrary AST expression as a left-hand side without
classifying it.

Recommended target categories:

* local/symbol assignment
* field assignment
* index assignment

If only symbol assignment is supported yet, keep it narrow and explicit.

### 11.4 Pipes

HIR must preserve pipe expressions as first-class nodes.

Do not desugar pipes into generic nested calls in Task 9.

Reasons:

* source-faithful diagnostics
* tooling / semantic visualization
* Dao-specific semantics
* later MIR lowering can choose the operational lowering

A `HirPipe` node should link:

* left input expression
* right pipeline target expression/form

### 11.5 Lambdas

HIR must preserve lambdas as first-class expressions.

Do not perform full closure conversion in Task 9.

HIR lambda should include:

* parameter semantic identities/types
* return/result semantic type if available
* body expression or normalized body form

If lambda bodies are expression-only in source, they may remain
expression-bodied in HIR.

### 11.6 Modes

HIR must preserve mode regions as explicit nodes.

At minimum:

* unsafe
* gpu
* parallel

A `HirMode` node should encode:

* mode kind
* enclosed statement/body region

Do not flatten mode information away into booleans.

### 11.7 Resources

HIR must preserve resource regions as explicit nodes.

A `HirResource` node should encode:

* resource kind
* resource instance name/symbol
* enclosed statement/body region

This is important for later lowering into allocation / execution
regions.

### 11.8 Literals

Literals in HIR must carry resolved semantic types.

Integer/float literal typing chosen by Task 8 should already be
reflected.

### 11.9 Symbol references

Identifier expressions should become symbol references, not raw names.

Use:

* `Symbol*`
* declaration id
* or equivalent stable semantic handle

Do not keep unresolved textual references in HIR.

## 12. Builder responsibilities

Task 9 should implement a dedicated HIR builder pass.

`HirBuilder` should:

* walk checked AST
* consume resolution + typed results
* allocate HIR nodes
* validate required semantic prerequisites exist
* report internal construction errors cleanly if invariants are broken

It must not:

* redo parsing
* redo resolution
* redo type inference

## 13. Structural normalization policy

HIR should apply modest normalization, not aggressive lowering.

Recommended normalizations:

* remove parentheses
* normalize expression-bodied function declarations into standard
  function bodies
* normalize identifier uses into symbol refs
* normalize AST type syntax away into semantic types
* preserve semantic constructs like pipe/mode/resource/lambda

Do not:

* flatten structured control flow into labels/gotos/CFG
* desugar pipe away
* closure-convert lambdas
* erase mode/resource boundaries

Those belong later.

## 14. Source locations

HIR nodes should retain source span information sufficient for:

* diagnostics
* printers/debug dumps
* playground/IDE inspection
* traceability back to source constructs

Every major HIR node should have a span.

## 15. HIR printing and debug surface

Task 9 must include a stable HIR printer.

Suggested CLI surface:

```text
daoc hir file.dao
```

The printer should show semantic structure, not just AST syntax.

Example direction:

```text
Function positive_squares : fn(List[i32]) -> List[i32]
  Body
    Pipe : List[i32]
      SymbolRef xs : List[i32]
      CallTarget filter ...
```

Exact formatting may vary, but the dump must be useful.

## 16. Tests

Task 9 is not complete without HIR golden tests.

Required coverage:

### Positive coverage

* expression-bodied function normalized to HIR
* block-bodied function
* let with inferred type
* let with explicit type and no initializer
* assignment
* if / while / for
* return
* pipe expression
* lambda expression
* mode block
* resource block
* function call
* pointer operations if currently supported
* string / bool / integer / float literals

### Semantic preservation coverage

* typed expression nodes have semantic `Type*`
* symbol references point to resolved semantic identities
* mode/resource nodes remain explicit
* pipe remains explicit

### Golden output

Include golden HIR dumps for selected examples and syntax probes.

## 17. Exit criteria

Task 9 is complete when:

* typed AST can be lowered into a stable HIR form
* HIR is clearly simpler than AST but preserves Dao semantic constructs
* HIR printer exists
* tests cover the supported language slice
* HIR is ready to feed Task 10 MIR lowering

## 18. Boundary rules

HIR may depend on:

* semantic types
* resolution results
* typed expression side tables
* source spans
* stable compiler support utilities

HIR must not depend on:

* LLVM
* backend code
* MIR
* tooling-specific parsers or semantic duplication

## 19. Stability rules

If implementation pressure suggests any of the following, stop and
revisit before proceeding:

* making HIR a near-copy of AST
* erasing pipe/lambda/mode/resource semantics too early
* introducing backend-specific details
* redoing type checking inside HIR construction
* depending on raw source names instead of resolved semantic identity

## 20. Recommended next seam

Task 9 should leave a very clear path for Task 10 MIR lowering.

MIR will be the place to decide:

* call normalization
* explicit control flow form
* operational lowering of pipes
* operational lowering of mode/resource regions
* closure strategy
* target-independent execution details
