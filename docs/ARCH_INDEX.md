# Architecture Index — Dao

Navigation-only lookup table for agents.
Normative behavior lives in `CLAUDE.md` and `docs/contracts/`.

## Root files

| File | Purpose |
|------|---------|
| `CLAUDE.md` | Repo constitution and structural invariants |
| `AGENTS.md` | Contributor and agent guardrails |
| `README.md` | Project overview |
| `.bonsai.yaml` | Repo-local Bonsai routing hints |
| `.grove.yaml` | Grove project metadata and consolidation hints |

## `docs/`

Contracts and explanatory material.

- `ARCH_INDEX.md` — this file
- `contracts/` — normative contracts for structure, syntax, execution contexts, compiler architecture, bootstrap/interop posture, and tooling boundaries
- `ROADMAP.md` — staged implementation plan from frontend skeleton to self-hosting, tooling maturity, and GPU expansion
- `IMPLEMENTATION_PLAN.md` — concrete task sequence, toolchain decisions, and delivery order for Tasks 0–5
- `compiler_bootstrap_and_architecture.md` — explanatory notes on preferred compiler internals and staged bootstrap posture
- `PLAYGROUND_ARCHITECTURE.md` — explanatory architecture for the playground as a first-class development surface and future web IDE
- `IDE_AND_TOOLING.md` — explanatory posture for semantic tooling, LSP, and why some tooling decisions are contract-level while others stay freestanding
- `COMPILER_SERVICE_API.md` — explanatory shared analysis payloads for CLI, playground, and LSP
- `language_vision.md` — explanatory design doctrine, stdlib posture, module/namespace design, and GPU strategy

## `spec/`

Reference language material.

- `grammar/` — parser-facing reference grammar, lexical surface, and indentation rules
- `semantics/` — semantic notes that are below contract level but above examples
- `syntax_probes/` — small snippets used to test readability and parser direction
- `examples/` — reference examples housed under `spec/examples/`

## `compiler/`

Compiler implementation roots only.

### `compiler/frontend/`

Source-facing compiler pipeline.

- `lexer/` — indentation-aware tokenization and lexical rules
- `parser/` — parsing and surface grammar ingestion
- `ast/` — syntax tree / declaration and expression representation
- `diagnostics/` — spans, reporting, and source diagnostics plumbing
- the intended next explicit roots are `resolve/`, `types/`, `typecheck/`, and `lower/` rather than a monolithic semantic pass

### `compiler/ir/`

Compiler-internal target-agnostic representations.

- `hir/` — high-level IR close to Dao semantics
- `mir/` — mid-level IR with explicit control flow and lowering prep

### `compiler/backend/`

Target-specific lowering.

- `llvm/` — initial backend target

### `compiler/driver/`

Compiler orchestration, sessions, target selection, and top-level build
coordination.

### `compiler/analysis/`

Shared analysis APIs that expose semantic tokens, diagnostics, hover,
completion, definitions, references, and document symbol payloads to the
CLI, playground, and LSP.

## `runtime/`

Execution support for lowered programs.

- `memory/` — scoped resource and allocation-domain support
- `modes/` — runtime integration for `mode` semantics
- `gpu/` — GPU/runtime bindings and execution support

## `stdlib/`

Dao standard library surface and future implementation roots.

- `core/` — foundational types/utilities
- `numerics/` — compute- and math-oriented surface
- `io/` — explicit IO-facing surface

## `examples/`

Small Dao programs and human-readable examples. Non-authoritative.
Also serves as a playground corpus and early regression corpus.

## `testdata/`

Fixtures and golden inputs/outputs for future parser/compiler tests.

## `tools/`

Developer-surface tooling built on compiler analysis.

- `playground/` — first-class web playground and future web-IDE surface
- `lsp/` — Language Server Protocol implementation
- `formatter/` — reserved canonical formatter root
- `diagnostics/` — presentation and diagnostics tooling experiments

## `ai/`

Bonsai governance assets.

- `skills/` — repo-local skills
- `baselines/` — accepted baseline findings / JSON snapshots
- `out/` — generated Bonsai outputs (git-ignored)
