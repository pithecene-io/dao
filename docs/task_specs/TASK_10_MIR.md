# TASK_10_MIR

Status: implementation spec
Phase: Mid-level IR
Scope: `compiler/ir/mir/`

## 1. Objective

Implement the first Mid-Level Intermediate Representation (MIR) for Dao.

Task 10 consumes:

- HIR
- semantic types
- resolved symbol identities

and produces:

- a typed, target-independent operational IR
- explicit control flow
- explicit evaluation order
- explicit storage and assignment forms
- a representation suitable for LLVM lowering in later tasks

MIR is where Dao moves from source-semantic structure to executable
program structure.

## 2. Non-goals

Task 10 must not implement:

- LLVM lowering
- register allocation
- machine ABI lowering
- monomorphization
- optimization pipelines
- borrow/lifetime systems
- full closure conversion if lambda capture is not yet frozen
- backend-specific GPU codegen
- runtime implementation details

MIR should be lower than HIR, but still machine-independent.

## 3. MIR role

MIR should:

- make control flow explicit
- make temporary values explicit
- make evaluation order explicit
- lower high-level constructs into operational forms
- preserve enough source span information for diagnostics/debugging
- remain target-agnostic

MIR should not be:

- a source-like tree
- a backend IR
- an LLVM-shaped mirror

## 4. Directory

Implementation must live under:

```text
compiler/ir/mir/
```

Suggested shape:

```text
compiler/ir/mir/
    mir.h
    mir.cpp
    mir_kind.h
    mir_context.h
    mir_context.cpp
    mir_module.h
    mir_function.h
    mir_block.h
    mir_inst.h
    mir_value.h
    mir_builder.h
    mir_builder.cpp
    mir_printer.h
    mir_printer.cpp
    mir_tests.cpp
```

## 5. Inputs

Task 10 consumes:

* HIR nodes
* semantic `Type*`
* symbol identities
* source spans

HIR is authoritative for source-semantic structure.
MIR construction must not redo parsing, resolution, or type checking.

## 6. Outputs

Task 10 must produce:

* MIR module/unit representation
* MIR functions
* basic blocks
* typed MIR values and instructions
* explicit control flow edges
* stable MIR printer/debug dump support

## 7. Core design principles

### 7.1 Explicit control flow

MIR must represent control flow with basic blocks and terminators.

### 7.2 Explicit values

MIR must make intermediate values explicit rather than relying on nested
expression trees for everything.

### 7.3 Still typed

MIR values must carry semantic types or reference typed value
definitions.

### 7.4 Target-independent

No LLVM types, no SSA values from LLVM, no backend handles.

### 7.5 Source traceability

Instructions and major values should retain spans where practical.

## 8. MIR structural model

Recommended top-level shape:

* `MirModule`
  * `MirFunction`
    * `MirBlock`
      * `MirInst`
      * `MirValue`

### 8.1 Functions

A MIR function contains:

* function identity/symbol
* parameter list
* local storage declarations as needed
* ordered basic blocks
* entry block

### 8.2 Basic blocks

A MIR block contains:

* ordered instruction list
* exactly one terminator instruction at end

### 8.3 Instructions vs values

A clean first design is:

* some instructions define values
* terminators do not
* locals/storage are distinct from SSA-like temporaries

Do not overcomplicate this into a perfect SSA design yet if it slows
progress.

## 9. Recommended MIR semantic stance

MIR should be hybrid operational IR:

* expression trees lowered into temporaries/instructions
* block control flow explicit
* locals/storage explicit
* not necessarily full SSA in Task 10
* ready for later SSA conversion or direct lowering strategy

This is the safest first MIR.

## 10. What must be lowered from HIR

Task 10 should lower:

* structured expressions into instruction/value form
* `if` into conditional branching between blocks
* `while` into loop blocks and explicit back-edges
* `for` into explicit lowered loop structure according to current
  iteration semantics
* `return` into function terminators
* calls into explicit call instructions
* field/index access into explicit operational instructions
* assignments into explicit store/assign instructions
* temporary evaluation order for nested expressions

## 11. What must remain semantically visible in MIR

Some Dao constructs should still remain visible in MIR, even if
operationalized.

### 11.1 Mode regions

`mode unsafe`, `mode gpu`, `mode parallel` must not be erased into
nothing.

MIR should preserve them either as:

* region markers
* block annotations
* dedicated mode-enter/mode-exit instructions
* function/block attributes where semantically appropriate

Do not discard this information before backend strategy is decided.

### 11.2 Resource regions

`resource memory X =>` must remain visible in MIR.

At minimum MIR should preserve:

* resource kind
* resource instance identifier
* region boundaries

This is critical for later allocation/runtime lowering.

### 11.3 Pipe semantics

Pipe expressions need not remain expression-tree nodes in MIR, but
their evaluation and call-target semantics must lower in a way that
preserves Dao behavior and debugging intelligibility.

It is acceptable for Task 10 to lower `HirPipe` into explicit call-like
instruction sequences, provided source span traceability remains
reasonable.

### 11.4 Lambdas

Lambdas may remain as MIR-level function/lambda objects or lower into
function-like nested entities plus environment scaffolding, but Task 10
should avoid full, final closure conversion unless absolutely necessary.

If lambda capture semantics are still narrow, keep MIR lambda handling
correspondingly narrow and explicit.

## 12. MIR control-flow form

Task 10 must introduce explicit basic blocks and terminators.

Minimum terminator set:

* `Br` (unconditional branch)
* `CondBr` (conditional branch)
* `Return`

Optional later:

* `Unreachable`

Every block must end in exactly one terminator.

## 13. MIR value/instruction categories

A good initial instruction/value set includes:

### Constants / literals

* integer constant
* float constant
* bool constant
* string constant reference

### Value-producing instructions

* unary op
* binary op
* call
* load
* address-of
* dereference/load-indirect if modeled separately
* field access
* index access
* cast/conversion later, if needed
* aggregate/list construction only if current surface requires it

### Storage / state instructions

* local alloc or local binding declaration
* store / assign
* resource region start/end or equivalent annotation mechanism

### Control instructions

* branch
* conditional branch
* return

## 14. Storage model

Task 10 should make local storage explicit.

Recommended model:

* locals are declared in MIR
* assignments become explicit stores to locals or addressable places
* pure expression temporaries are separate MIR values

Do not keep assignment as a vague high-level source construct.

## 15. Place vs value distinction

MIR should begin distinguishing:

* **places**: assignable/addressable storage locations
* **values**: computed results

This distinction is important for:

* assignment
* address-of
* dereference
* field assignment
* index assignment

The first MIR version does not need a sophisticated place system, but it
should not pretend everything is just an expression value.

## 16. Type policy

MIR remains typed.

Every MIR value-producing instruction must have a semantic `Type*`.

Locals/places should also carry semantic type information.

MIR must consume the semantic type universe from `frontend/types`.
It must not invent backend-specific type models.

## 17. Lowering policy for specific constructs

### 17.1 Functions

Lower HIR functions to MIR functions with:

* explicit entry block
* explicit parameter values/storage
* explicit body blocks
* explicit returns

### 17.2 Let bindings

Lower to:

* local declaration/storage slot
* initializer evaluation if present
* store/init into that slot if needed

If uninitialized-but-typed locals are currently legal via zero/default
initialization semantics, MIR should represent that explicitly:

* either via default-init instruction
* or via guaranteed local initialization lowering

### 17.3 Assignment

Lower to explicit store/assign instructions against valid places.

### 17.4 If

Lower into:

* condition eval
* conditional branch
* then block
* else block if present
* continuation block as needed

### 17.5 While

Lower into canonical loop form, e.g.:

* entry → condition block
* condition true → body
* body → condition
* condition false → exit

### 17.6 For

Lower according to the currently supported iteration model.
If iteration semantics remain narrow, MIR lowering may be
correspondingly narrow.

### 17.7 Return

Lower to explicit terminator with optional return value.

### 17.8 Calls

Lower call evaluation order explicitly.

### 17.9 Pipe

Lower pipe into explicit target evaluation/call sequence.
Do not preserve pipe as a dedicated MIR node unless it clearly improves
the operational model; MIR is a reasonable place to operationalize it.

### 17.10 Lambda

Keep lambda lowering conservative.
If needed, MIR may represent lambdas as nested function-like entities
with explicit capture placeholders, but do not overbuild full closure
conversion unless the language slice requires it.

### 17.11 Mode/resource

Preserve region semantics explicitly through MIR constructs or
annotations.
These are part of Dao's core semantic identity.

## 18. Builder responsibilities

`MirBuilder` should:

* walk HIR
* construct MIR functions/blocks/instructions
* create temporaries as needed
* manage block construction and terminators
* preserve semantic type/spans
* validate major lowering invariants

It must not:

* redo type checking
* redo resolution
* introduce backend-only concepts

## 19. Suggested implementation shape

A good first builder strategy is:

* one `MirBuilder` per function
* helper routines:
  * `lower_stmt`
  * `lower_expr_value`
  * `lower_expr_place`
  * `lower_cond`
  * `lower_block_region`

This helps maintain the place/value distinction cleanly.

## 20. MIR printer

Task 10 must include a stable MIR printer.

Suggested CLI:

```text
daoc mir file.dao
```

The dump should reveal:

* function boundaries
* block labels
* instruction order
* value definitions
* terminators
* mode/resource region info

Example direction:

```text
fn positive_squares
block %entry:
  %0 = local xs : List[i32]
  %1 = call filter(%0, <lambda>)
  %2 = call map(%1, <lambda>)
  return %2
```

Exact formatting may vary, but it must be operationally legible.

## 21. Tests

Task 10 is not complete without MIR golden tests.

Required coverage:

### Control flow

* if
* while
* return
* block continuation shape

### Data flow

* let with initializer
* let with explicit type and no initializer
* assignment
* arithmetic expressions
* call argument evaluation

### Dao-specific semantics

* pipe lowering
* mode region preservation
* resource region preservation
* pointer address/deref if supported
* lambda presence/lowering in current supported slice

### Structural invariants

* every block ends in terminator
* no dangling blocks
* values/instructions carry types
* source spans preserved where required

## 22. Exit criteria

Task 10 is complete when:

* HIR lowers into a stable MIR form
* MIR has explicit basic blocks and terminators
* MIR is typed and target-independent
* Dao-specific semantic regions remain representable
* MIR printer and tests exist
* the result is ready to feed LLVM lowering

## 23. Boundary rules

MIR may depend on:

* HIR
* semantic types
* symbols
* support utilities
* source spans

MIR must not depend on:

* LLVM APIs
* `backend/llvm`
* playground/LSP-specific tooling logic
* parser or resolver internals beyond consumed semantic results

## 24. Stability rules

If implementation pressure suggests any of the following, stop and
revisit before proceeding:

* making MIR just another tree-shaped AST clone
* pulling LLVM concepts into MIR
* erasing mode/resource semantics entirely
* keeping evaluation order implicit
* avoiding the place/value distinction where addressability matters
