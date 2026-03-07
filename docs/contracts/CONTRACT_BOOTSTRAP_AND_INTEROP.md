# CONTRACT_BOOTSTRAP_AND_INTEROP.md

## Purpose

Defines the current mandatory bootstrap and foreign-interop posture for
Dao. This contract is normative but intentionally narrow.

## Bootstrap Laws

1. Self-hosting is a strategic goal, not a prerequisite for the first
   working compiler.
2. The initial compiler implementation may be written in a host language.
3. Host-language implementation choices must not leak into Dao surface
   syntax or semantic contracts.
4. Compiler phase boundaries frozen in
   `CONTRACT_COMPILER_PHASES.md` must remain valid across bootstrap
   stages.
5. Migration toward self-hosting must preserve repeatable test and build
   parity between the host compiler and any Dao-implemented compiler
   components.

## Interop Laws

1. The initial foreign interop target is the C ABI.
2. Initial compatibility does not imply direct C++ source-level
   compatibility.
3. If C++ integration is needed early, it must occur through explicit C
   ABI shims unless and until a stronger interop contract is adopted.
4. Runtime and driver work may expose C-facing seams where necessary, but
   those seams must remain narrow and documented.
5. Target-specific interop details must not leak upward into the surface
   syntax contract.
