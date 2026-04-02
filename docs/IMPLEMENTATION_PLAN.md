# Implementation Plan — Dao

This document is explanatory and sequencing-oriented. It captures the
concrete implementation tasks, toolchain decisions, and delivery order
for the Dao compiler project. It is subordinate to `CLAUDE.md` and
`docs/contracts/`.

## Frozen Toolchain Decisions

### Language and compiler

- Host implementation language: **C++23**
- Primary compiler: **clang++ 17+** (first-class)
- Secondary compiler: **GCC 13+** (best-effort; does not block progress)
- GCC compatibility analysis deferred until enough code exists to
  benchmark compile times, diagnostic quality, and C++23 feature
  coverage

### Build system

- **CMake** with `cmake_minimum_required(VERSION 3.30)`
- CMake presets for common configurations

### Package manager

- **Conan 2.x** in manifest mode
- `conanfile.py` with custom logic for LLVM option configuration
- Compiler profiles managed via Conan profiles
  (e.g. `profiles/clang-17-debug`, `profiles/gcc-13-release`)
- One `conan.lock` per profile, colocated under `profiles/`
  (e.g. `profiles/clang-17-debug.lock`); lockfiles are
  configuration-specific and must not be shared across profiles
- LLVM is managed by Conan like all other dependencies

### Test framework

- **boost-ext/ut 2.3.1** — single-header, macro-free, C++20/23 native
- Managed via Conan
- Test files live alongside source: `*_test.cpp` next to `*.cpp`
- `testdata/` holds fixtures and golden files only

### Formatting and static analysis

- `.clang-format`: LLVM base, 2-space indent, 100-column limit,
  no bin packing, pointer-left, no short control statements
- `.clang-tidy`: `modernize-*`, `performance-*`, `readability-*`

### CI

- **Earthly** for reproducible, cacheable builds
- GitHub Actions as the trigger/runner; Earthly defines the actual
  build pipeline
- Earthly targets produce Docker cache layers so LLVM and Conan
  dependency builds are cached across runs (the primary bottleneck
  without caching is LLVM from-source compilation at 60–90 min)
- Pipeline: checkout → `earthly +build` → `earthly +test`
- CI must verify the CMake version satisfies the `cmake_minimum_required`
  floor before building
- Ubuntu only initially; macOS deferred
- Previous GitHub Actions-only CI was removed due to impractical
  build times without layer caching

## Binary Architecture

### `daoc` — dedicated compiler

The compiler is a standalone binary with focused responsibilities:

- `daoc lex <file>` — emit token stream
- `daoc parse <file>` — emit AST
- `daoc ast <file>` — pretty-print AST
- `daoc build <file>` — compile to object/executable (later)

### `dao` — toolchain orchestrator (reserved)

Reserved for future toolchain commands. Not implemented in the initial
tasks. Potential future subcommands: `dao fmt`, `dao lsp`, `dao test`.

### Tool binaries

Tools under `tools/` (playground, LSP, formatter) get their own binaries
or server processes, kept separate from the compiler binary to avoid
dependency contamination.

## Task Sequence

### Task 0 — Toolchain and Environment

**Objective**: Establish a buildable, testable, formatted C++23 project
with a skeleton compiler driver.

Deliverables:

- `CMakeLists.txt` (root) — top-level build with C++23 target
- `CMakePresets.json` — presets for clang debug/release
- `conanfile.py` — declares boost-ext/ut, cpp-httplib, nlohmann_json, llvm-core
- `profiles/clang-17-debug` — Conan profile for primary dev
- `profiles/clang-17-debug.lock` — committed lockfile for primary profile
- `.clang-format` — LLVM base + Dao overrides
- `.clang-tidy` — strict modernize/performance/readability checks
- `.gitignore` — updated for build/, Conan output
- `.github/workflows/build.yml` — CI pipeline
- `compiler/driver/main.cpp` — skeleton driver that reads a source file
  and exits
- `compiler/driver/CMakeLists.txt` — builds `daoc`

Exit criteria:

- `cmake --build build` succeeds
- the `daoc` binary produced by the build reads a file and exits cleanly
- `clang-format` and `clang-tidy` pass on all source
- CI runs green on push

### Task 1 — Lexer

**Objective**: Translate `spec/grammar/dao.lex` into a working
indentation-aware lexer.

Deliverables:

- `compiler/frontend/lexer/token.h` — token kinds, source spans
- `compiler/frontend/lexer/lexer.h` / `lexer.cpp` — lexer
  implementation
- `compiler/frontend/lexer/lexer_test.cpp` — tests against syntax
  probes
- `compiler/frontend/diagnostics/source.h` — source buffer
  abstraction
- `compiler/frontend/diagnostics/diagnostic.h` — diagnostic
  structures

Requirements:

- indentation-sensitive: emit INDENT / DEDENT tokens
- tabs are illegal
- recognize all tokens from `spec/grammar/dao.lex`
- token kinds must carry enough information for trivial mapping to the
  semantic token taxonomy in `CONTRACT_LANGUAGE_TOOLING.md`
- run lexer against every file in `spec/syntax_probes/` and
  `examples/`

Exit criteria:

- all syntax probes and examples lex without error
- INDENT/DEDENT pairs are balanced
- token spans are accurate
- tests pass

### Task 2 — Parser

**Objective**: Parse the grammar in `spec/grammar/dao.ebnf` into an AST.

Deliverables:

- `compiler/frontend/ast/ast.h` — AST node definitions
- `compiler/frontend/parser/parser.h` / `parser.cpp` — recursive
  descent parser
- `compiler/frontend/parser/parser_test.cpp` — tests against syntax
  probes

Target AST nodes (minimum):

- `File`
- `Import`
- `FunctionDecl` (block-bodied and expression-bodied)
- `StructDecl`, `AliasDecl`
- `LetStatement`
- `Assignment`
- `IfStatement`, `WhileStatement`, `ForStatement`
- `ModeBlock`, `ResourceBlock`
- `ReturnStatement`
- `BinaryExpr`, `UnaryExpr`, `CallExpr`, `IndexExpr`, `FieldExpr`
- `Lambda`
- `PipeExpr`
- `Literal` (integer, float, string, bool)
- `ListLiteral`
- `Identifier`
- `Type` (named, pointer, parameterized)

Requirements:

- all productions from `spec/grammar/dao.ebnf`
- source spans on every AST node
- diagnostics for syntax errors with readable source reporting
- the parser must not invent semantics beyond the grammar

Exit criteria:

- all syntax probes parse successfully
- all examples parse successfully
- syntax errors produce stable, readable diagnostics
- golden AST snapshots in `testdata/`

### Task 3 — AST Printer

**Objective**: Add a human-readable AST dump to `daoc`.

Deliverables:

- `compiler/frontend/ast/ast_printer.h` / `ast_printer.cpp` —
  structured AST output
- `daoc ast <file>` subcommand wired up

Output format (indicative):

```
File
  FunctionDecl a_star
    Param graph: Graph
    Param start: NodeId
    Param goal: NodeId
    ReturnType: List[NodeId]
    ResourceBlock memory Search
      LetStatement open
      WhileStatement
        ...
```

Exit criteria:

- `daoc ast` produces readable output for all examples and probes
- output is deterministic (suitable for golden-file testing)

### Task 4 — Playground Integration (Structural)

**Objective**: Bring up a minimal playground tied to the lexer and
parser as early as possible, per the ROADMAP Phase 1.5 intent of
"structural highlighting first, compiler-backed semantic highlighting
as soon as frontend analysis exists."

Deliverables:

- `tools/playground/compiler_service/` — minimal service wrapping the
  lexer and parser; transport (in-process, HTTP, or IPC) is decided at
  execution time per `docs/COMPILER_SERVICE_API.md`
- Playground frontend (stack TBD) showing:
  - structural token highlighting (keyword, operator, literal
    classification from the token stream)
  - AST panel
  - diagnostics panel
- loads examples from `examples/`

Playground stack decision is deferred to Task 4 execution. The service
layer must consume compiler frontend output, not reimplement language
logic.

Prerequisites:

- Tasks 1-3 complete
- parser handles all syntax probes and examples

Exit criteria:

- paste Dao code into the playground and see structural coloring, AST,
  and diagnostics
- playground consumes compiler frontend, not bespoke regexes
- examples load from the `examples/` directory

### Task 5 — Semantic Token Classification

**Objective**: Produce compiler-backed token classification per the
taxonomy in `CONTRACT_LANGUAGE_TOOLING.md`, and upgrade the playground
from structural to semantic highlighting.

Deliverables:

- `compiler/analysis/semantic_tokens.h` / `semantic_tokens.cpp` —
  classification API
- `daoc tokens <file>` subcommand
- Playground upgraded: `/tokens` endpoint, semantic highlighting
  replaces structural highlighting

Approach:

- lexical tokens (keywords, operators, literals, punctuation) are
  classifiable immediately from the token stream
- declaration/use distinction and type vs. function classification
  require AST — classify what is available from lexical and
  structural context; tokens that cannot yet be classified are omitted
  from the semantic token stream until resolution exists
- this layer does not reimplement parsing; it consumes the frontend

Exit criteria:

- all categories from the frozen taxonomy that are lexically or
  structurally determinable are classified
- playground shows semantic highlighting
- output is suitable for consumption by future LSP

## What Comes After

Tasks 6–13 (resolve, types, typecheck, HIR, MIR, LLVM backend,
generics, coroutines) are complete or substantially complete.
Task 15 (C ABI interop) v1 and v2 are complete — struct-by-value,
function pointer types, and named-function callbacks all landed.
Task 18 (enum payloads and match destructuring) is complete.
Task 19 (diagnostic formatter, Phase 7 entry leaf) is complete.
Task 20 (bootstrap lexer extraction) is complete — the lexer probe
has been promoted to `bootstrap/lexer/lexer.dao` with verified C++
parity, 97+ golden tests, and self-lex regression.
Task 21 (bootstrap parser extraction) is complete — the parser is
promoted to `bootstrap/parser/parser.dao` with Tier A syntax coverage,
arena-indexed AST, 36 golden tests, and self-parse of real Dao source.
Task 22 (bootstrap resolver) is complete — two-pass name resolution
with scope chains, symbol tables, uses map, 17 tests.
Task 23 (bootstrap type checker) is complete — expression/statement
type checking with 19 tests.  Task 24 (bootstrap HIR) is complete —
typed AST lowered to compiler-owned HIR with 14 tests.  Shared
substrate consolidated in `bootstrap/shared/base.dao`; assembly
via `bootstrap/assemble.sh`.

The Tier A bootstrap pipeline (lex → parse → resolve → typecheck →
HIR) is complete.  Next focus is the multi-file substrate (Tasks
25–27), then Tier B feature slices on top.

### Task 25 — Bootstrap Multi-file Compilation + Imports (v1)

**Objective**: Replace assembly-only single-file assumptions with a
deterministic multi-file substrate and first import/module surface.

See `docs/task_specs/TASK_25_BOOTSTRAP_MULTIFILE.md`.

Deliverables:

- `module` keyword added to bootstrap lexer (and `dao.lex`)
- `ModuleDeclN` and `ImportDeclN` AST nodes
- `FileN` extended with optional module decl and import list
- program-level compilation layer above per-file parsing
- file-to-module mapping with deterministic identity
- module graph construction with cycle detection
- diagnostics for missing/duplicate modules and import cycles

### Task 26 — Bootstrap Cross-file Resolution

**Objective**: Extend the bootstrap resolver from single-file scope
chains to module-aware cross-file name resolution.

See `docs/task_specs/TASK_26_BOOTSTRAP_CROSS_FILE_RESOLUTION.md`.

Deliverables:

- `Module` symbol kind in bootstrap resolver
- per-module export tables populated in topo order
- import bindings create Module symbols in importing scope
- qualified names resolve through module export tables
- diagnostics for missing exports and duplicate import bindings

### Task 27 — Bootstrap Cross-file Typecheck + HIR Aggregation

**Objective**: Extend bootstrap type checking and HIR lowering to
operate over multiple resolved modules with canonical type identity.

See `docs/task_specs/TASK_27_BOOTSTRAP_PROGRAM_TYPECHECK_AND_HIR.md`.

Deliverables:

- program-wide canonical type table spanning all modules
- cross-file function call and nominal type checking
- `HirProgram` root aggregating per-module `HirModule` nodes
- deterministic topo-ordered lowering and HIR dump
- end-to-end multi-file smoke test

After Tasks 25–27, feature-oriented Tier B slices (methods/
associated items, concepts/extend, richer patterns, deferred
expressions) have a sane multi-file substrate to sit on.

### Task 14 — Numeric Type Expansion

**Objective**: Implement the numeric semantics frozen in
`CONTRACT_NUMERIC_SEMANTICS.md` in staged tiers.

Governing contract: `docs/contracts/CONTRACT_NUMERIC_SEMANTICS.md`

#### Tier A — Near-term (Phase 5 tail / pre-Phase 6)

Status: **complete**

- ✓ f64 codegen audited against IEEE 754: all six comparison
  predicates use correct ordered/unordered semantics (oeq, une,
  olt, ole, ogt, oge); NaN propagation preserved (no fast-math
  flags); signed-zero preserved (fneg, fsub, fadd emit bare LLVM
  IR with no nsz/nnan/ninf flags)
- ✓ f32 shares the same IEEE 754-conformant codegen path
- ✓ integer overflow policy frozen: checked by default (trap via
  sadd/ssub/smul.with.overflow intrinsics for all signed types)
- ✓ no fast-math flags anywhere in the backend (CONTRACT §8.1)
- ✓ float-to-int conversions trap on NaN/Inf/out-of-range

#### Tier A+ — Post-baseline (no blocking dependency)

Status: **complete**

- ✓ explicit wrapping operations for i32 and i64: `wrapping_add`,
  `wrapping_sub`, `wrapping_mul` (+ `_i64` variants)
- ✓ explicit saturating operations for i32 and i64: `saturating_add`,
  `saturating_sub`, `saturating_mul` (+ `_i64` variants)
- ✓ explicit checked operations for all signed types (i8–i64):
  `checked_add`, `checked_sub`, `checked_mul` — return
  `Option.None` on overflow, `Option.Some(result)` otherwise;
  pure Dao implementations (no runtime hooks), enabled by
  `Option<T>` prelude promotion

#### Tier B — Phase 6 prerequisite

Status: **complete**

- ✓ `i64` surface exposure: type system (`BuiltinKind::I64`,
  `type_context.i64()`), parser recognition, LLVM backend lowering
  (`llvm::Type::getInt64Ty`), runtime hooks (`__dao_eq_i64`,
  `__dao_conv_i64_to_string`), stdlib extensions (`extend i64 as
  Numeric`), working example (`examples/i64.dao`)
- ✓ explicit numeric conversions between i32/i64 and f32/f64 with
  trapping semantics (27-function conversion matrix)
- ✓ `__dao_str_length` returns `int64_t`

#### Tier C — Phase 6+ dedicated task

Status: **complete**

- ✓ full integer width expansion: i8, i16, u8, u16, u32, u64 —
  type system, LLVM codegen, equality, to_string, C ABI, Equatable,
  Printable, Numeric concept extensions
- ✓ `f32` surface exposure: type, codegen, stdlib formatting,
  conversion, runtime hooks, equality, printing
- ✓ float-to-int trapping for all combinations (f32/f64 → i32/i64)
- ✓ full numeric conversion matrix: 27 explicit conversion functions
  covering widening, narrowing, sign-changing, and float↔int/float
- ✓ wrapping and saturating overflow for all signed types (i8–i64)

#### Tier D — Phase 8

Priority: **low** — depends on GPU and optimization infrastructure.

- fast-math / relaxed numeric opt-in mode
- GPU numeric profiles
- decimal type design and initial implementation

Exit criteria (Tier A+B): **met**

- ✓ f64 comparisons emit correct IEEE predicates in LLVM IR
- ✓ i32 overflow behavior is defined and tested
- ✓ i64 is usable end-to-end (type, codegen, runtime, examples)
- ✓ explicit numeric conversions exist between i32 and f64

### Task 16 — Error-Tolerant Parsing and Tooling Hardening

**Status**: complete

**Objective**: Make the parser produce partial ASTs for incomplete
or broken source, enabling completion and diagnostics while typing.

Governing contracts: `CONTRACT_LANGUAGE_TOOLING.md`

Deliverables:

- error recovery at ~18 parser error points: skip to next statement
  or insert synthetic nodes instead of bailing
- partial AST consumers: resolver, type checker, and analysis APIs
  must tolerate missing/error nodes gracefully
- dot completion for general expressions: use AST expression types
  (`typed.expr_type()`) instead of symbol-only heuristics, so
  `make_point().`, `p.x.`, and `arr[i].` work
- playground diagnostics suppression for incomplete constructs
  (e.g. don't report "function not found" while user is typing
  an opening paren)

Implementation summary:

- parser produces ErrorExpr/ErrorStmt/ErrorDecl placeholders with
  synchronization at statement and declaration boundaries
- statement parsers (let, if, while, for, yield, return, assignment)
  detect ErrorExpr children and promote to ErrorStmt
- resolver, type checker, and HIR builder tolerate error nodes via
  explicit cases and silent default fallthrough
- dot completion uses expr_types map scan for expression receivers
  when symbol lookup fails (handles calls, field chains, indexing)
- analyze pipeline continues through resolve/typecheck on parse
  errors to produce partial semantic tokens and AST; downstream
  cascade diagnostics are suppressed when parse errors exist;
  HIR/MIR/LLVM lowering is skipped when parse errors exist

Exit criteria:

- ✓ typing `p.` mid-statement shows dot completions without parser
  failure
- ✓ incomplete source produces partial results instead of no results
- ✓ diagnostics for in-progress constructs are deferred or suppressed

## Principles

- The grammar in `spec/grammar/` is the parser's source of truth
- Do not invent infrastructure choices beyond what is frozen here
- Minimize external dependencies
- Frontend stability before backend ambition
- The playground is a development tool, not a demo
