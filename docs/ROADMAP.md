# Roadmap — Dao

This document is explanatory and sequencing-oriented. It is not a
normative contract unless a milestone is later promoted into
`docs/contracts/`.

## Guiding Delivery Shape

Dao should be built in stages that preserve feedback loops:
- freeze surface syntax and semantic taxonomy before broad implementation
- get a parser and diagnostics loop working before type-system ambition
- lower into target-agnostic IR before leaning on LLVM-specific features
- stabilize a minimal runtime and stdlib before broad language surface
- self-host only after the implementation language boundary is an aid,
  not an anchor
- treat the playground, semantic highlighting, and IntelliSense as core
  hardening loops rather than as late polish

## Phase 0 — Constitutional Baseline

Status: scaffolded

Goals:
- repository constitution, contracts, and architecture index in place
- initial `.bonsai.yaml` / `.grove.yaml` in place
- language surface captured in syntax and execution-context contracts
- compiler topology frozen at a high level (frontend → HIR → MIR → LLVM)
- baseline semantic token taxonomy and initial LSP slice frozen in tooling
  contracts

Exit criteria:
- governance docs are internally consistent
- repository shape is stable enough for targeted implementation work

## Host Implementation Baseline

Frozen decision:
- the initial host implementation language is C++

Rationale:
- direct LLVM integration is cleaner
- compiler-control and memory-control concerns align with C++ better than
  with Go for this project
- it reduces early cross-language seams in the compiler core

## Phase 1 — Frontend Skeleton

Goals:
- indentation-aware lexer
- parser for declarations, statements, expressions, lambdas, pipelines,
  `mode`, and `resource`
- AST definitions and source-span plumbing
- first-class diagnostics with readable source reporting
- syntax probes and parser golden tests under `spec/` and `testdata/`
- reserve explicit compiler phase roots for `resolve/`, `typecheck/`, and
  lowering rather than allowing a monolithic semantic blob

Non-goals:
- optimization
- self-hosting
- advanced type inference

Exit criteria:
- representative Dao samples parse successfully
- syntax errors produce stable diagnostics
- AST shape is good enough to drive HIR lowering

## Phase 1.5 — Playground and Example Hardening Loop

Goals:
- bring up a small web playground tied to the local `examples/` directory
- support structural highlighting first, then compiler-backed semantic
  highlighting as soon as frontend analysis exists
- make diagnostics readability part of day-to-day UAT
- treat examples as both teaching corpus and regression corpus
- establish semantic token rendering and document-symbol inspection as core
  compiler feedback loops

Exit criteria:
- the playground can load and edit local examples
- compiler-produced diagnostics and semantic token streams are visible in
  the browser once frontend analysis is available

## Phase 2 — Semantic Frontend + HIR

Goals:
- name resolution and scope analysis
- type checking for foundational scalar, pointer, function, and container
  forms
- HIR construction preserving source-level meaning where valuable
- lowering of pipes and lambdas into analyzable HIR forms
- explicit representation of `mode` and `resource` semantics in HIR
- semantic tokens, document symbols, and hover classification driven from
  the same analysis

Focus decisions:
- keep HIR close to Dao mental models
- do not let LLVM details leak upward

Exit criteria:
- small programs type-check end to end
- HIR dumps are readable and useful for debugging

## Phase 3 — MIR + Execution Semantics Lowering

Goals:
- explicit control-flow lowering to MIR
- ownership-free but scoped lowering model for `resource memory ... =>`
- lowering preparation for `mode unsafe`, `mode parallel`, and future
  `mode gpu`
- canonical representation for calls, control flow, temporaries, and
  memory-region lifetimes

Focus decisions:
- MIR is where execution becomes explicit
- MIR remains target-agnostic

Exit criteria:
- MIR can represent A*, ETL pipelines, and numeric kernels without
  surface-language leakage
- region/resource lifetime boundaries are explicit in MIR

## Phase 4 — LLVM Backend + Native Driver

Goals:
- LLVM lowering for scalar arithmetic, control flow, calls, aggregates,
  and resource lifetime intrinsics
- compiler driver capable of producing object files, executables, and IR
  dumps
- host-target compilation on one primary platform first
- baseline debug info and source-location preservation where practical

Initial target posture:
- prioritize one host platform and one LLVM toolchain path
- broaden targets only after correctness and diagnostics stabilize

Exit criteria:
- Dao "hello world" compiles and runs
- small routing / ETL / numerics examples compile through LLVM

## Phase 5 — Runtime and Initial Standard Library

Goals:
- runtime memory support for scoped resource domains
- initial mode plumbing for `unsafe`, `parallel`, and a staged `gpu`
  execution story
- foundational stdlib modules under `stdlib/core`, `stdlib/io`, and
  `stdlib/numerics`
- explicit ABI-facing surface needed for compiler/runtime interop

Stdlib priority order:
1. core scalar/container foundations
2. strings, slices, and iterators/pipeline support
3. IO and file surface
4. numerics / math / vector-friendly primitives
5. concurrency primitives only after semantics are stable

Exit criteria:
- the compiler-generated binaries can rely on a minimal runtime/stdlib
  without ad hoc host-language glue in normal execution

## Phase 6 — C ABI Interop and Host Integration

Goals:
- stable C ABI entry/exit surface for initial foreign function calls
- ability to call C libraries from Dao through explicit declarations
- ability to expose Dao functions as C-callable symbols where practical
- clear boundary between C ABI compatibility and broader C++ ergonomics

Important boundary:
- initial compatibility target is the C ABI
- direct C++ source-level interop is deferred and may be served through C
  shims first

Exit criteria:
- small Dao programs can call into a C library
- Dao-produced artifacts can be linked into a C/C++ host through the C ABI

## Phase 7 — Bootstrap Compiler

Goals:
- begin implementing non-trivial compiler subsystems in Dao itself
- establish a bootstrap chain from host implementation → mixed
  implementation → self-hosted compiler
- keep test parity between the host compiler and the Dao-implemented
  compiler as the handoff proceeds

Recommended bootstrap sequence:
1. keep the initial compiler in an implementation language suited to
   rapid frontend/backend construction
2. implement leaf or utility components in Dao first
3. migrate increasingly central compiler phases only when Dao can
   express them ergonomically and compile them reliably
4. reach stage-2 self-hosting before claiming the compiler is truly
   self-hosted

Exit criteria:
- the Dao compiler can compile itself with only bounded host assistance
- rebuild parity and test parity are stable across repeated bootstrap
  cycles

## Tooling Track — Semantic Tooling, IntelliSense, and Web IDE

### Tooling T1 — Semantic Highlighting
- compiler-produced semantic token streams
- frozen baseline token taxonomy implemented end to end
- category distinction for declarations, calls, types, lambdas, pipes,
  modes, resources, and bindings

### Tooling T2 — Initial IntelliSense Slice
- hover
- completion
- go-to-definition
- references
- document symbols
- symbol identity hardening across compiler sessions

### Tooling T3 — Web IDE North Star
- AST / HIR / MIR panes
- workspace-aware browser surface
- persistent multi-file editing when compiler incrementality is ready
- future rename and refactor support only after edit safety stabilizes

## Phase 8 — GPU and Numerics Expansion

Goals:
- principled `mode gpu =>` semantics
- first-class numerics support mature enough for dense compute kernels
