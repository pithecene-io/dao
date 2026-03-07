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
- `conanfile.txt` initially; upgrade to `conanfile.py` if custom logic
  is needed
- `conan.lock` committed for reproducible builds
- Compiler profiles managed via Conan profiles
  (e.g. `profiles/clang-17-debug`, `profiles/gcc-13-release`)
- LLVM is a system dependency found via `find_package`, not managed
  by Conan

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

- GitHub Actions
- Pipeline: checkout, install clang 17 + Conan, cmake build, run tests
- Ubuntu only initially; macOS deferred

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
- `conanfile.txt` — declares boost-ext/ut 2.3.1
- `conan.lock` — committed lockfile
- `profiles/clang-17-debug` — Conan profile for primary dev
- `.clang-format` — LLVM base + Dao overrides
- `.clang-tidy` — strict modernize/performance/readability checks
- `.gitignore` — updated for build/, Conan output
- `.github/workflows/build.yml` — CI pipeline
- `compiler/driver/main.cpp` — skeleton driver that reads a source file
  and exits
- `compiler/driver/CMakeLists.txt` — builds `daoc`

Exit criteria:

- `cmake --build build` succeeds
- `./build/daoc <file>` reads a file and exits cleanly
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

### Task 4 — Semantic Token Classification

**Objective**: Produce compiler-backed token classification per the
taxonomy in `CONTRACT_LANGUAGE_TOOLING.md`.

Deliverables:

- `compiler/analysis/semantic_tokens.h` / `semantic_tokens.cpp` —
  classification API
- `daoc tokens <file>` subcommand

Approach:

- lexical tokens (keywords, operators, literals, punctuation) are
  classifiable immediately from the token stream
- declaration/use distinction and type vs. function classification
  require AST — classify what is available, leave unknowns as
  `identifier` until resolution exists
- this layer does not reimplement parsing; it consumes the frontend

Exit criteria:

- all categories from the frozen taxonomy that are lexically or
  structurally determinable are classified
- output is suitable for consumption by playground and future LSP

### Task 5 — Playground Integration

**Objective**: Connect the playground to the compiler frontend as a
feedback loop for syntax, diagnostics, and highlighting.

Deliverables:

- `tools/playground/compiler_service/` — minimal HTTP service wrapping
  compiler analysis
- Endpoints: `/lex`, `/parse`, `/tokens`, `/diagnostics`
- Playground frontend (stack TBD) showing:
  - semantic token highlighting
  - AST panel
  - diagnostics panel
- loads examples from `examples/`

Playground stack decision is deferred to Task 5 execution. The service
layer must consume `compiler/analysis/`, not reimplement language logic.

Prerequisites:

- Tasks 1-4 complete
- parser handles all syntax probes and examples

Exit criteria:

- paste Dao code into the playground and see syntax coloring, AST,
  and diagnostics
- playground consumes compiler analysis, not bespoke regexes
- examples load from the `examples/` directory

## What Comes After

The following are sequenced in `docs/ROADMAP.md` and are not detailed
here:

- Task 6 — Name resolution (`compiler/frontend/resolve/`)
- Task 7 — Type representation (`compiler/frontend/types/`)
- Task 8 — Type checking (`compiler/frontend/typecheck/`)
- Task 9 — HIR construction (`compiler/ir/hir/`)
- Task 10 — MIR lowering (`compiler/ir/mir/`)
- Task 11 — LLVM backend (`compiler/backend/llvm/`)

These are explicitly deferred until the frontend feedback loop
(spec, probes, compiler, playground) is stable.

## Principles

- The grammar in `spec/grammar/` is the parser's source of truth
- Do not invent infrastructure choices beyond what is frozen here
- Minimize external dependencies
- Frontend stability before backend ambition
- The playground is a development tool, not a demo
