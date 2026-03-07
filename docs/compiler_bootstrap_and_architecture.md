# Compiler Bootstrap and Architecture Notes — Dao

This document is explanatory. It captures the current preferred
architecture and staged bootstrap posture without freezing every detail as
contract law.

## Current Preferred Architecture

Dao is organized around four major implementation bands and one support
band:
- frontend
- IR layers
- backend
- driver
- runtime

That split is already normative in `CONTRACT_COMPILER_PHASES.md`. What is
not yet fully frozen is the internal shape of each band.

## Frontend

Preferred structure:
- `compiler/frontend/lexer/`
- `compiler/frontend/parser/`
- `compiler/frontend/ast/`
- `compiler/frontend/diagnostics/`
- `compiler/frontend/resolve/`
- `compiler/frontend/types/`
- `compiler/frontend/typecheck/`
- `compiler/frontend/lower/`

Rationale:
- syntax and diagnostics need to become pleasant early
- name resolution and type checking should remain clearly source-facing
  until HIR handoff
- lowering should be an explicit subphase, not hidden inside the parser

## IR Stack

Preferred layers:
- HIR: language-shaped, semantically rich, still close to Dao source
- MIR: explicit control flow, explicit temporaries, explicit resource
  lifetimes, target-agnostic

Possible later layers:
- optional canonical optimization IR beneath MIR if optimization pressure
  grows enough to justify it
- optional GPU kernel IR if unified MIR lowering proves too coarse

Current bias:
- do not add extra IR layers until MIR proves insufficient

## Backend

Initial backend:
- LLVM only

LLVM should give Dao:
- mature code generation
- optimization infrastructure
- debug info and object emission support
- a practical path to C ABI interop

Current boundary:
- LLVM is an implementation target, not a language-shaping authority

## Runtime

Runtime should stay narrow.

It should primarily own:
- resource/memory domain mechanics
- execution-mode support hooks
- GPU/runtime bindings where required
- thin ABI and startup support as needed by generated programs

It should not become:
- a giant framework
- a kitchen-sink concurrency subsystem before semantics are ready
- the place where source-level meaning gets reinvented

## Bootstrap Posture

The self-hosting goal is real, but not first.

Preferred strategy:
1. Build the initial compiler in a host language with strong LLVM and
   systems support.
2. Keep frontend contracts and IR boundaries strict so the host language
   implementation does not leak into Dao semantics.
3. Once Dao can express core compiler data structures and transformations
   cleanly, begin reimplementing leaf or mid-level compiler components in
   Dao.
4. Only migrate the parser/typechecker/backend driver core once the Dao
   toolchain is robust enough to support daily development.

## Interop Posture

Initial interop should target the C ABI.

That means:
- Dao should be able to call C-callable functions cleanly
- Dao-produced symbols should be callable from C/C++ hosts when exposed
  through a C ABI surface
- direct C++ source-model interop is not an initial milestone

This avoids getting trapped in:
- templates
- name mangling edge cases
- exception model mismatches
- unstable compiler-specific ABI details

## Key Decisions Likely Worth Freezing Later

These are strong candidates for future contracts once implementation
begins:
- whether resolution and type checking live under `frontend/` or move
  under a compiler-semantic band of their own
- whether MIR owns all resource lifetime boundaries or whether a
  dedicated region/resource lowering pass deserves a named layer
- whether standard library numerics live partly in runtime for ABI or
  codegen reasons
- whether GPU lowering stays within LLVM-only paths initially or needs a
  sidecar codegen route


## Initial host implementation language

The initial compiler host implementation language is C++. This is frozen
for now because the project intends to target LLVM early and because the
compiler core benefits from direct control over memory, data structures,
and backend integration without introducing a second systems-language seam.

Go remains acceptable for peripheral tools if later justified, but not for
the compiler core in the founding stages.


## Tooling as architecture

The playground, semantic highlighting, and IntelliSense are architectural
consumers of the compiler, not ornamental add-ons. The compiler should be
structured so that semantic tokens, hover, completion, and navigation all
fall naturally out of the same frontend and analysis layers used for normal
compilation.
