# CONTRACT_REPOSITORY_LAYOUT.md

## Purpose

Defines the allowed top-level ontology and major subroots for Dao.

## Required Top-Level Directories

- `compiler/`
- `runtime/`
- `stdlib/`
- `spec/`
- `docs/`
- `examples/`
- `testdata/`
- `ai/`

## Required Major Subroots

### Under `compiler/`
- `frontend/`
- `ir/`
- `backend/`
- `driver/`
- `analysis/`

### Under `compiler/frontend/`
- `lexer/`
- `parser/`
- `ast/`
- `resolve/`
- `diagnostics/`
- `types/`
- `typecheck/`

Expected next subroots once implementation begins:
- `lower/`

### Under `compiler/ir/`
- `hir/`
- `mir/`

### Under `compiler/backend/`
- `llvm/`

### Under `runtime/`
- `memory/`
- `modes/`
- `gpu/`

## Directory Semantics

- `compiler/` owns compiler implementation concerns only.
- `compiler/frontend/` owns source-facing compiler stages only.
- `compiler/ir/` owns target-agnostic program representations only.
- `compiler/backend/` owns target-specific lowering only.
- `compiler/driver/` owns orchestration only.
- `runtime/` owns runtime and backend execution support only.
- `stdlib/` owns standard-library surface, contracts, and future
  implementation roots only.
- `spec/` owns grammar fragments, language notes, and semantic probes only.
- `docs/contracts/` owns normative repository and language contracts.
- `examples/` owns illustrative Dao programs only.
- `testdata/` owns future fixtures only.
- `ai/` owns Bonsai governance artifacts only.

## Laws

1. No top-level directory may exist without an explicit `ARCH_INDEX.md`
   entry.
2. No two top-level directories may claim the same primary
   responsibility.
3. Required major subroots may not be collapsed without a contract
   change.
4. Normative documents must live under `docs/contracts/`.
5. Non-normative prose must not introduce guarantees not present in a
   contract.
6. Generated output must not be committed under `ai/out/`.
