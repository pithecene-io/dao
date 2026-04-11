# Task 30 — Bootstrap LLVM Backend (Tier A)

Status: implementation spec
Phase: Phase 7 — Bootstrap Compiler
Scope: lower bootstrap MIR to deterministic textual LLVM IR via a
minimal Dao-side LLVM mini-IR and text serializer

## 1. Objective

Add the first bootstrap LLVM backend.  Task 29 landed the Tier A
bootstrap MIR subsystem; Task 30 is the next layer above it and
closes the bootstrap frontend-to-IR-to-text pipeline:

`lex → parse → resolve → typecheck → HIR → MIR → LLVM text`

The backend consumes a `MirResult` (single-file) or a `Program` with
`HirProgram`/`MirProgram` shape and produces a textual LLVM IR
module that can be written to a `.ll` file and consumed by an
external toolchain (`clang`, `llc`, `opt`) outside the bootstrap
runtime.

This task does not attempt a full LLVM backend.  It establishes the
minimum self-contained substrate required for bootstrap to stop
stopping at MIR.

## 2. Primary design objective

Get bootstrap to textual LLVM IR without dragging in an FFI layer or
a rich IR framework.  Keep the mini-IR thin, backend-private, and
strictly Tier A.  Every bit of LLVM modeling that is not demanded by
the current Task 29 MIR surface stays out.

## 3. Why this task exists now

`bootstrap/README.md` already names "LLVM backend lowering from
bootstrap MIR" as the next bootstrap backend step (updated in the
Task 29 doc sync).  `bootstrap/mir/impl.dao` now carries a stable
Tier A MIR surface that mirrors `compiler/ir/mir/mir.h`:

* `MirModule` / `MirFunction` / `MirLocal` / `MirBlock`
* `MirConstInt` / `MirConstFloat` / `MirConstBool` / `MirConstString`
* `MirBinary` / `MirUnary`
* `MirLoad` / `MirStore`
* `MirFieldAccess` (read, Tier B defers layout)
* `MirFnRef` / `MirCall`
* `MirReturn` / `MirBr` / `MirCondBr`
* `MirErrorExpr`

Task 30 takes exactly that surface and nothing more.  The
deferrals already documented for Task 29 (generics,
monomorphization, generators, mode/resource regions, enum payloads,
lambdas, try, for-over-iterable, index, break/continue) remain
deferred at the backend layer too.

## 4. Chosen approach

### 4.1 Path choice

Task 30 uses a minimal Dao-side LLVM **mini-IR** (backend-private)
plus a text serializer:

```
bootstrap MIR  →  bootstrap LLVM mini-IR  →  textual .ll
```

Considered and rejected:

* **Direct string emission from MIR** — gets ugly as soon as control
  flow, globals, and type decisions accumulate; no structural seam
  for testing or normalization.
* **LLVM C API via `extern fn`** — requires runtime linking
  `libLLVM.so`, opaque-ref ABI discipline, and a per-platform
  toolchain story.  Overkill for Tier A.

The mini-IR matches the rest of `bootstrap/`: flat arenas, indexed
nodes, separation between semantic lowering and output formatting,
testable intermediate structure before final serialization.

### 4.2 Scope discipline

The mini-IR must stay thin.  Task 30 is explicitly not permitted to
grow into a general-purpose LLVM framework.  It models only what is
required to serialize Task 29's Tier A MIR surface.

The mini-IR is **backend-private to `bootstrap/llvm/`**.  It is not
a new compiler-wide public IR tier, and nothing in `bootstrap/mir/`
or earlier subsystems should import it.

## 5. In scope

Task 30 Tier A covers the MIR constructs already supported by Task
29 and maps them to a minimal textual LLVM subset.

### 5.1 Module / function surface

* LLVM module text emission
* `define` for non-extern functions
* `declare` for extern functions
* deterministic function emission order (MirFunction index order)
* deterministic extern-declaration emission (first reference order)

### 5.2 Type lowering

Bootstrap MIR type indices lower to LLVM textual types for the Tier
A primitive set:

| Dao (bootstrap MIR)  | LLVM textual |
|----------------------|--------------|
| `i32`                | `i32`        |
| `i64`                | `i64`        |
| `f32`                | `float`      |
| `f64`                | `double`     |
| `bool`               | `i1`         |
| `void`               | `void`       |
| `string`             | `ptr` (opaque, Tier A, see §8.4) |

Type-index resolution must be centralized in one helper.  Serializers
must not re-derive primitive-type strings locally.

### 5.3 Control flow

* basic-block labels (`entry:`, `bb1:`, ...)
* unconditional branch: `br label %...`
* conditional branch: `br i1 %cond, label %then, label %else`
* `ret <ty> %val` / `ret void`

Every emitted LLVM block must end in exactly one terminator.

### 5.4 Locals and memory

* `alloca` for every `MirLocal` in the function prologue
* `store` for `MirStore`
* `load` for `MirLoad`

Tier A uses the conservative "alloca-everything" strategy.  No
mem2reg, no SSA reconstruction.  This matches what the host LLVM
backend does before `opt -mem2reg` runs.

### 5.5 Constants

* `MirConstInt`  → integer literal in instruction context
* `MirConstBool` → `0` / `1` as `i1`
* `MirConstFloat` → textual float literal (LLVM-compatible form)
* `MirConstString` → private module-global, referenced by
  `getelementptr` (see §8.4)

### 5.6 Arithmetic and comparisons

Dispatched by MIR operand type, never by textual guesswork during
serialization:

* integer arithmetic: `add` / `sub` / `mul` / `sdiv` / `srem`
* float arithmetic: `fadd` / `fsub` / `fmul` / `fdiv`
* integer comparisons: `icmp eq|ne|slt|sle|sgt|sge`
* float comparisons: `fcmp oeq|one|olt|ole|ogt|oge`

Unary negation lowers to `sub 0, x` (integer) or `fneg` (float).
Logical `!` on `i1` lowers to `xor %x, true`.

### 5.7 Calls

* `MirFnRef` resolves to a textual callee symbol (mangled from the
  resolver sym; exact mangling scheme in §9)
* `MirCall` lowers to `call <ty> @name(args...)`
* Extern declarations emitted on first reference, before any
  `define`

### 5.8 Output artifact

The backend returns textual LLVM IR as a string.  A helper writes
the string to a `.ll` path on disk for harness use.  The backend
does not own linking, object emission, or executable production.

## 6. Non-goals

### 6.1 Not in Task 30 Tier A

* linking against the LLVM C++ API
* linking against the LLVM C API
* JIT execution
* native object file emission from Dao
* executable linking from Dao
* optimization passes
* target machine / data layout configuration beyond minimal module
  headers
* debug info
* source locations in emitted IR
* metadata
* PHI nodes or SSA reconstruction (alloca+load/store is the SSA
  story for Tier A)
* mem2reg
* attribute inference beyond what Tier A correctness requires
* `opt` pipeline integration
* llvmir textual → bitcode conversion

### 6.2 Deferred to later tasks

* struct layout / field-address computation (Task 31 or later)
* enum payload / discriminant lowering
* monomorphization-aware backend lowering
* generators / coroutines
* mode / resource region runtime hooks
* lambda / closure lowering
* try operator
* iterable-based `for`
* index expressions
* `break` / `continue`
* cross-module codegen / linkage policy beyond a single emitted
  module
* ABI coercion for aggregate parameters / returns
* `clang` / `llc` invocation from inside bootstrap

## 7. Input contract

Task 30 consumes bootstrap **MIR only**.

It must not consume:

* AST
* HIR
* resolver tables directly as semantic source of truth

Any resolver / type data used by the backend must already be
reflected through MIR lowering decisions or carried as MIR-side type
indices and resolver symbol identities on `MirFunction` / `MirLocal`
/ `MirFnRef`.

This mirrors the host compiler contract: MIR feeds the LLVM backend.

The program-pipeline adapter (§11) handles the path
`HirProgram → MirProgram → llvm text`, but the backend itself still
only reads from MIR + sym / type tables.

## 8. Mini-IR design

### 8.1 Purpose

Separate two concerns:

1. MIR → LLVM *lowering* decisions (what LLVM op, what type, what
   operand)
2. LLVM *text* serialization (what the characters on disk look like)

A thin mini-IR gives us a place to land (1) and test it before (2)
runs.

### 8.2 Shape

Flat arena / indexed nodes, consistent with the rest of bootstrap:

```
LlModule
  globals: list of LlGlobal indices
  functions: list of LlFunction indices

LlGlobal
  name: string (e.g. ".str.0")
  ty: LlType
  value: string  // textual initializer, e.g. c"hello\00"
  linkage: string  // "private unnamed_addr constant"

LlFunction
  name: string  // mangled from MirFunction.sym
  ret_ty: LlType
  params: list of (name, LlType)
  blocks: list of LlBlock indices
  is_extern: bool

LlBlock
  label: string  // "entry", "bb1", ...
  insts: list of LlInst indices

LlInst
  kind: LlInstKind (enum class)
  result_name: string (may be empty for terminators / effect-only)
  result_ty: LlType
  // payload fields per kind
```

`LlInstKind` covers only what Task 29's MIR surface demands:

```
Alloca(ty)
Store(ptr, value)
Load(ptr, ty)
BinIntAdd | BinIntSub | BinIntMul | BinIntSDiv | BinIntSRem
BinFAdd | BinFSub | BinFMul | BinFDiv
ICmp(pred) | FCmp(pred)
Call(callee, args, ret_ty)
Ret(value_or_none)
Br(label)
CondBr(cond, then_label, else_label)
GepString(global_name)  // materialize i8* to a global string
```

Exact enum naming may differ, but the important constraint is that
the list stays small and backend-private.

### 8.3 Values and naming

Task 30 generates deterministic local value names and block labels:

* local values: `%0`, `%1`, `%2` — a per-function counter incremented
  for every instruction that produces a value
* block labels: `entry`, `bb1`, `bb2` — a per-function counter starting
  at `bb1` after `entry`
* globals: `@.str.0`, `@.str.1` — a per-module counter

Names must be stable across runs for identical MIR input.

### 8.4 String literals

`MirConstString` lowers to a private module-global:

```llvm
@.str.0 = private unnamed_addr constant [N x i8] c"hello\00"
```

Materialization at use site uses a canonical GEP form:

```llvm
%1 = getelementptr inbounds [N x i8], ptr @.str.0, i64 0, i64 0
```

The string type itself is opaque `ptr` in Tier A — Task 30 does not
define a string runtime representation.  If a call takes a `string`
parameter in Tier A MIR, the backend passes a `ptr` pointing at the
null-terminated literal.  Anything more elaborate (length prefix,
string struct, runtime calls) is a follow-up task.

## 9. Symbol mangling

Tier A uses a simple scheme:

* `MirFunction.sym` → resolver symbol → `module::name` form
* LLVM emission: `@module_name_fn` with `::` replaced by `_`

Example: `app::math::add` → `@app_math_add`.

This is not ABI-stable and not a long-term mangling story.  It is a
deterministic, grep-able placeholder until a real mangling contract
is written.

Anonymous / synthetic symbols (sym == -1) are rejected with a
diagnostic.

## 10. Lowering rules by MIR node

### 10.1 `MirModule`

Lowers to one `LlModule`.

### 10.2 `MirFunction`

Lowers to `LlFunction` with:

* `declare` when `is_extern` is set
* `define` otherwise
* params emitted in MIR order
* extern functions skip block lowering entirely

### 10.3 `MirLocal`

Becomes an `LlInst::Alloca` in the entry block prologue.  Params get
their alloca first, in declaration order; then non-param locals in
declaration order.

### 10.4 Constants

| MIR node        | LlInst emission                                |
|-----------------|------------------------------------------------|
| `MirConstInt`   | inline operand; no separate inst               |
| `MirConstBool`  | inline operand                                 |
| `MirConstFloat` | inline operand (textual)                       |
| `MirConstString`| module-global + `GepString` inst at use site   |

Inlining constant operands means `MirConstInt` does not produce an
`LlInst` — it becomes a textual operand inside the instruction that
consumes it.  This avoids wasting `%N` slots on constants.

### 10.5 `MirBinary` / `MirUnary`

Dispatch on the operand MIR type:

* integer type → `BinInt*`
* float type → `BinF*`
* bool AND/OR → integer ops on `i1`

The op_tok lexeme picks the exact LLVM op within a category.  Op
dispatch must live in one helper, not scattered through serialization.

### 10.6 `MirLoad` / `MirStore`

* `MirLoad(local, ty)` → `LlInst::Load` of the local's `alloca` slot
* `MirStore(local, value)` → `LlInst::Store` into the local's slot

### 10.7 `MirFieldAccess`

**Deferred in Task 30 Tier A.**

Task 29 includes `MirFieldAccess` reads, but Task 30 Tier A explicitly
defers struct layout / field-address computation.  If
`MirFieldAccess` reaches the backend in Task 30, emit a diagnostic:

```
bootstrap llvm backend does not support MirFieldAccess in Tier A
```

and produce a backend error result.  Do not half-support struct
addressing.  Aggregate ABI is a separate task.

### 10.8 `MirFnRef` / `MirCall`

* `MirFnRef(sym)` resolves to a textual callee name via §9.  If the
  symbol is external (extern fn), a `declare` is emitted once per
  module at first reference.
* `MirCall(callee, args_lp, arg_count)` → `LlInst::Call` with args
  lowered in declaration order.

### 10.9 Terminators

* `MirReturn(value, has_value)` → `ret <ty> %val` or `ret void`
* `MirBr(target)` → `br label %...`
* `MirCondBr(cond, then, else)` → `br i1 %cond, label %then, label %else`

### 10.10 `MirErrorExpr`

Rejected with a diagnostic.  The backend refuses to emit for MIR
modules containing error sentinels.

## 11. Public API

Backend entry points live in `bootstrap/llvm/impl.dao`:

```dao
fn lower_mir_to_llvm_text(mir: MirResult): LlvmTextResult
fn lower_program_to_llvm_text(p: Program): LlvmTextResult
```

`LlvmTextResult`:

```dao
class LlvmTextResult:
  text: string
  diags: Vector<Diagnostic>
  ok: bool
```

A helper writes text to disk:

```dao
fn write_llvm_text(result: LlvmTextResult, path: string): bool
```

The program-pipeline adapter mirrors `lower_to_mir`: it routes
through `build_program → program_run_resolve → program_run_typecheck
→ program_run_hir → program_run_mir` and then drives the backend
over each `MirFunction` in deterministic order.

No existing bootstrap subsystem API changes.

## 12. Determinism requirements

Task 30 output must be byte-identical across runs for identical
MIR input.  This includes:

* function emission order (MIR index order)
* extern `declare` order (first-reference order, stable due to MIR
  walk order)
* global-string numbering (`@.str.0`, `@.str.1`, ...)
* block labels (`bb1`, `bb2`, ...)
* local value numbering (`%0`, `%1`, ...)
* whitespace / indentation / newlines

Determinism is part of the acceptance criteria.  Tests that run the
backend twice must compare byte-for-byte.

## 13. Diagnostics

Task 30 diagnostics must be explicit and grep-able.  The backend is
fail-closed: unsupported MIR does not silently produce partial
output.

Required diagnostics:

* `bootstrap llvm backend does not support MirFieldAccess in Tier A`
* `bootstrap llvm backend encountered unsupported type kind <kind>`
* `bootstrap llvm backend expected block terminator in block <id>`
* `bootstrap llvm backend cannot lower unresolved generic residue`
* `bootstrap llvm backend rejects MirErrorExpr sentinel`
* `bootstrap llvm backend cannot mangle anonymous symbol`
* `bootstrap llvm backend expected MirFnRef callee, got <kind>`

All diagnostics carry a span when one is available.

## 14. Verification strategy

### 14.1 Required in Task 30

Substring / golden-text Dao tests, consistent with the existing
`bootstrap/*/impl.dao` test style.  Tests assert on stable emitted
IR fragments such as function headers, block labels, `alloca` /
`store` / `load`, arithmetic op choice, comparison op choice,
`call`, and string global definitions.

### 14.2 Not required in Task 30

`llc` / `clang` execution in CI.  Toolchain validation is
intentionally deferred.

### 14.3 Follow-up expectation

The emitted IR must be **intended to be valid LLVM IR**, not just
"text that looks LLVM-ish".  Toolchain validation (`llc test.ll -o
/dev/null`) is the first follow-up once the textual surface
stabilizes.  That follow-up should land as Task 30.5 or as a CI
addition and must not require changes to the Task 30 surface.

## 15. Test plan

### 15.1 Backend unit tests (~12)

* `extern_declare` — extern fn emits `declare i32 @puts(ptr)`
* `minimal_define` — `fn main(): i32` emits `define i32 @main()`
  with `entry:` label and `ret i32 0`
* `int_arithmetic` — `1 + 2 * 3` lowers to `add` / `mul` in correct
  order
* `float_arithmetic` — `1.0 + 2.0` lowers to `fadd`
* `int_comparison` — `x < y` lowers to `icmp slt`
* `float_comparison` — `a < b` lowers to `fcmp olt`
* `let_store_load` — `let x = 1; return x` emits `alloca` + `store`
  + `load` + `ret`
* `if_else_cfg` — emits `br i1 ... label %bb1, label %bb2` with
  `bb1:` / `bb2:` / merge block
* `while_cfg` — emits header / body / exit blocks with back-edge
* `call_extern` — call to `extern fn puts` uses the emitted
  `declare`
* `string_global` — string literal produces
  `@.str.0 = private unnamed_addr constant ... c"..."`
* `deterministic_repeat` — two consecutive calls on identical MIR
  produce byte-identical output

### 15.2 Unsupported-surface tests (~3)

* `field_access_rejected` — MIR with `MirFieldAccess` produces the
  expected diagnostic and `ok == false`
* `error_expr_rejected` — MIR with `MirErrorExpr` is rejected
* `anonymous_sym_rejected` — MirFunction with `sym == -1` is
  rejected

### 15.3 Integration smoke test

Run `lex → parse → resolve → typecheck → HIR → MIR → LLVM text` for
a small fixture (e.g. `fn main(): i32 -> 42\n`) and compare against
a golden `.ll` string.

## 16. Deliverables

### 16.1 Spec PR (this task)

* `docs/task_specs/TASK_30_BOOTSTRAP_LLVM_BACKEND.md` (this document)
* `docs/ROADMAP.md` reference to Task 30 as the next Phase 7 slice
* `docs/IMPLEMENTATION_PLAN.md` Task 30 entry

### 16.2 Implementation PR (follow-up)

* `bootstrap/llvm/impl.dao` — mini-IR + lowering + serializer + tests
* `bootstrap/assemble.sh` — new `llvm.gen.dao` assembly pulling in
  resolver + typecheck + hir + mir libs + llvm impl
* `bootstrap/README.md` — new `### llvm/` subsystem section
* `docs/ARCH_INDEX.md` — new `bootstrap/llvm/` entry
* All bootstrap subsystems still 275+ tests passing; new llvm tests
  additive

## 17. Acceptance criteria

1. Bootstrap MIR can be lowered to textual LLVM IR for the Tier A
   supported surface.
2. Extern functions emit `declare`; normal functions emit `define`.
3. Locals lower through deterministic `alloca` / `store` / `load`.
4. Arithmetic and comparison ops choose correct integer vs float
   LLVM instructions based on MIR type, not text.
5. Calls lower correctly for `MirFnRef` callees; extern `declare`
   emitted on first reference.
6. String literals lower to deterministic private module-globals
   with canonical GEP materialization.
7. Emitted IR is byte-deterministic across runs for identical MIR.
8. Unsupported Tier B MIR constructs (`MirFieldAccess`,
   `MirErrorExpr`, anonymous symbols, unresolved generic residue)
   produce clear diagnostics and `ok == false`.
9. Task 29 MIR surface remains unchanged; Task 30 does not require
   MIR redesign.
10. `bootstrap/README.md` and `docs/ARCH_INDEX.md` reflect the new
    `bootstrap/llvm/` subsystem.
11. Mini-IR stays backend-private to `bootstrap/llvm/`.

## 18. Risks

### 18.1 Overbuilding the mini-IR

The biggest design risk.  Every LlInst kind beyond the §5 set is
scope creep.  Review should bounce any new kind that is not
directly demanded by a Task 29 MIR node.

### 18.2 Scope creep from aggregates

Struct / field lowering pulls in layout, GEP computation, and ABI
issues.  `MirFieldAccess` stays deferred.  Any attempt to "just add
field access while we're here" is out of scope.

### 18.3 String model ambiguity

§8.4 locks the string model explicitly: opaque `ptr` to a private
null-terminated global in Tier A.  Do not leave this implicit.  Do
not invent a string struct, length prefix, or runtime call inside
Task 30.

### 18.4 Verification gap

Substring testing is sufficient for Tier A landing but not the end
state.  Code must be written so `llc` validation can be bolted on
as a follow-up without restructuring the emitter.

### 18.5 Mangling hard-coding

§9 defines a placeholder scheme.  It must not be referenced outside
`bootstrap/llvm/` and must not be treated as stable.  When a real
mangling contract is written (likely alongside C ABI or cross-module
linkage work), Task 30's scheme gets replaced wholesale.

## 19. Explicit deferrals

* struct layout / field-address computation
* enum payload / discriminant lowering
* monomorphization-aware backend lowering
* generators / coroutines
* mode / resource region runtime hooks
* lambda / closure lowering
* try operator
* iterable-based `for`
* index expressions
* `break` / `continue`
* LLVM C API integration
* target-specific machine code emission from Dao
* `clang` / `llc` invocation from Dao
* stable symbol mangling contract

## 20. Recommended implementation order

1. Land this spec PR (docs only).
2. Define minimal mini-IR types in `bootstrap/llvm/impl.dao`.
3. Implement primitive type lowering helpers (§5.2).
4. Implement function / module / extern-declare emission (§5.1, §5.7).
5. Implement block / terminator emission (§5.3).
6. Implement local / load / store lowering (§5.4).
7. Implement arithmetic / comparison lowering (§5.6).
8. Implement call lowering (§5.7).
9. Implement string-literal globals and GEP materialization (§8.4).
10. Implement fail-closed diagnostics for §13 cases.
11. Add the §15 test battery.
12. Update `bootstrap/README.md`, `docs/ARCH_INDEX.md`,
    `bootstrap/assemble.sh`.

## 21. One-sentence summary

Task 30 adds a self-contained bootstrap LLVM backend that lowers
Task 29 bootstrap MIR into deterministic textual LLVM IR via a
minimal, backend-private Dao-side LLVM mini-IR and text serializer.
