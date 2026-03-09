# CONTRACT_LLVM_BACKEND_BOUNDARY

Status: normative
Scope: Dao LLVM backend boundary
Authority: compiler architecture boundary

## 1. Purpose

This contract freezes the role of the LLVM backend in the Dao compiler.

The LLVM backend lowers MIR into LLVM IR.
It is the first target-specific backend boundary.

## 2. LLVM backend must be

- MIR-driven
- type-lowering-centric
- target-specific only at the backend layer
- cleanly separated from frontend and MIR design

## 3. LLVM backend must not consume

- AST directly
- HIR directly
- unresolved names
- syntax-level type nodes

## 4. LLVM backend responsibilities

The backend is responsible for:

- semantic/MIR type lowering to LLVM types
- function lowering
- basic block lowering
- instruction/value lowering
- textual LLVM IR emission

## 5. Boundary rule

MIR feeds the LLVM backend.

The LLVM backend must not redo:

- parsing
- resolution
- type checking
- HIR/MIR construction

## 6. Unsupported semantics

If a Dao semantic construct has not yet been correctly lowered, the
backend must fail explicitly rather than silently erase it.
