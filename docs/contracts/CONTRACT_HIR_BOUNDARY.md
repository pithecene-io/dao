# CONTRACT_HIR_BOUNDARY

Status: normative
Scope: Dao HIR boundary
Authority: compiler architecture boundary

## 1. Purpose

This contract freezes the role of HIR in the Dao compiler.

HIR is the typed, target-agnostic semantic IR between frontend checking
and lower operational IR.

## 2. HIR must be

- typed
- target-agnostic
- symbol-linked
- simpler than AST
- expressive enough to preserve Dao semantic constructs

## 3. HIR must preserve as first-class constructs

- pipe expressions
- lambda expressions
- mode regions
- resource regions

These may be lowered later, but not erased at the HIR boundary.

## 4. HIR must not contain

- parser-only syntax structures with no semantic meaning
- unresolved names
- AST syntax-level type nodes
- backend-specific details
- LLVM-specific constructs

## 5. HIR expressions

HIR expressions must carry semantic types.

## 6. HIR identity

HIR should reference resolved semantic identities rather than raw source
names wherever practical.

## 7. Boundary rule

AST + resolve + types + typecheck feed HIR.

HIR feeds MIR.

HIR must not redo:

- parsing
- name resolution
- type checking
