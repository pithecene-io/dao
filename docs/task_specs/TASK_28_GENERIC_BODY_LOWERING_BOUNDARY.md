# Task 28 — Proper Generic Body Lowering Boundary

Status: implementation spec
Phase: Post–Tasks 25–27 / Host compiler architectural cleanup
Scope: replace the current workaround for generic MIR lowering with
the proper architectural boundary

## 1. Objective

Prevent the compiler from lowering uninstantiated generic function
bodies to MIR.

Generic function bodies should only be lowered when the compiler has
a concrete instantiation context.  The compiler must stop pretending
that a generic body with unresolved type parameters is a normal
monomorphic body that can flow through MIR unchanged.

This task replaces the current workaround with the intended
architecture.

## 2. Problem statement

The current compiler has a workaround in place for generic body
lowering.  That workaround is sufficient to keep development moving,
but it is structurally wrong: it allows the pipeline to approach or
enter MIR lowering for function bodies whose type parameters have not
yet been instantiated.

That distorts the contract between HIR, MIR, and monomorphization:

- HIR may legitimately represent generic declarations.
- MIR should represent executable, type-concrete control/data flow.
- Monomorphization is the bridge that turns a generic declaration
  plus concrete substitutions into a lowerable body.

Task 28 restores that boundary.

## 3. Primary design objective

Make the compiler's generic pipeline explicit and fail-closed:

- generic declarations may exist in semantic IR
- generic bodies are not lowered to MIR in declaration form
- MIR exists only for:
  - non-generic functions, or
  - concrete generic instantiations

## 4. Why this task exists now

Tasks 25–27 completed the bootstrap multi-file structural arc.  That
is a natural pause point.

The generic MIR-lowering workaround is now the most obvious piece of
architectural debt in the main compiler path.  Leaving it in place
will keep contaminating later work on:

- monomorphization
- generic calls across modules
- method dispatch on generic receivers
- concept-constrained generics
- backend assumptions about function bodies

This fix should land before starting the next substantial
feature-heavy slice.

## 5. In scope

### 5.1 Generic/non-generic body distinction

The compiler must distinguish between:

- monomorphic function bodies that can lower immediately
- generic function declarations whose bodies must remain unlowered
  until instantiated

### 5.2 Proper MIR entry boundary

MIR lowering must reject or skip uninstantiated generic bodies by
construction.

### 5.3 Concrete instantiation path

A generic function body may lower to MIR only through a concrete
instantiation path that provides substituted types for every required
generic parameter.

### 5.4 Declaration retention

Generic declarations remain available in semantic tables / HIR-level
program structures so later instantiations can reference them.

### 5.5 Deterministic behavior

The skip/instantiate behavior must be deterministic and independent
of traversal order.

## 6. Non-goals

### 6.1 Not part of this task

- full redesign of the generic type system
- new syntax for generics
- new trait/concept semantics
- new inference rules
- generic specialization policies
- cross-module caching of monomorphized artifacts
- backend optimization of instantiated generics
- bootstrap Tier B generic implementation

### 6.2 Also not included

- deduplicating equivalent monomorphizations beyond current intended
  identity rules
- broad MIR refactors unrelated to generic-body eligibility
- LLVM/backend calling convention changes

## 7. Desired architecture

### 7.1 Phase boundary

The intended pipeline should be:

```
Parse / Resolve / Typecheck / HIR
  → keep generic declarations in generic form
  → discover concrete uses / instantiations
  → instantiate with concrete substitutions
  → lower instantiated body to MIR
  → backend
```

Not:

```
Generic declaration body
  → lower to MIR directly
  → hope later phases patch in substitutions
```

### 7.2 Semantic rule

A function body is MIR-lowerable iff either:

- the function has no generic parameters, or
- the function is being lowered as a specific instantiation with all
  type parameters concretely bound

Everything else must be skipped or rejected.

## 8. Core invariants

### 8.1 MIR concreteness invariant

Every MIR function body must be type-concrete.

There must be no unresolved generic parameter type in:

- local variable types
- parameter types
- return types
- operand/result types
- control-flow instructions
- callee identities after lowering

### 8.2 Declaration/instantiation separation invariant

The compiler must distinguish the identity of:

- a generic declaration
- a specific instantiated body derived from that declaration

These are related, but not interchangeable.

### 8.3 No eager generic body lowering invariant

The compiler must not lower a generic declaration body merely because
the declaration exists in the program.

### 8.4 Deterministic instantiation identity invariant

Concrete instantiations must have stable identity derived from:

- generic declaration identity
- ordered type argument list
- any other semantically required instantiation key components

## 9. Semantic model

### 9.1 Generic declaration

A generic declaration is a function whose signature contains one or
more generic parameters, whether directly or through the declaration's
generic parameter list.

Examples:

```dao
fn id<T>(x: T): T
fn map<T, U>(xs: List<T>, f: fn(T): U): List<U>
```

These declarations remain at semantic/HIR level as templates.

### 9.2 Instantiation

An instantiation is a concrete application of a generic declaration
to a full set of concrete type arguments.

Examples:

```
id<i32>
map<string, i64>
```

Instantiation is the event that authorizes body lowering.

### 9.3 Uninstantiated generic body

An uninstantiated generic body is the declaration body as written
against generic parameters prior to concrete substitution.

This body may be:

- resolved
- typechecked under generic rules
- stored in HIR / semantic tables

but must not become MIR.

## 10. Where the fix belongs

The proper enforcement point is the host compiler's HIR → MIR
lowering boundary, or the orchestration layer immediately above it.

That is where the compiler decides which callable bodies are eligible
for MIR construction.

The key is not merely emitting an error.  The pipeline must be
structured so uninstantiated generic bodies are not even treated as
normal MIR-lowering work items.

## 11. Recommended implementation shape

### 11.1 Introduce explicit lowerability decision

Add a predicate or classification step such as:

```cpp
can_lower_body_to_mir(fn_decl, lowering_context) -> bool
```

or

```cpp
enum class FunctionLoweringMode {
  MonomorphicDecl,
  GenericDeclTemplateOnly,
  ConcreteInstantiation,
};
```

This should be computed from declaration metadata plus lowering
context, not from ad hoc checks buried deep in MIR construction.

### 11.2 Split declaration collection from body lowering

If the current pipeline walks all functions and lowers their bodies
immediately, split it into:

1. declaration discovery / symbol registration
2. lowering worklist construction
3. MIR lowering only for eligible bodies

Generic declarations should still be registered as callable program
entities, but their bodies should not be emitted into MIR until
instantiated.

### 11.3 Represent generic declarations as templates

The semantic/HIR layer should preserve enough information to
instantiate later:

- declaration identity
- type parameter list
- typed body / HIR body
- signature metadata
- source provenance

### 11.4 Instantiate before MIR

When a concrete generic use is discovered, instantiate by:

1. forming the concrete substitution map
2. creating or retrieving the concrete instantiation identity
3. substituting the declaration body/signature into a concrete
   semantic form
4. lowering that concrete form to MIR

This can be eager or on-demand, but the boundary must stay intact.

## 12. Required compiler behavior changes

### 12.1 Function traversal

Any current pass that does "for each function declaration, lower body
to MIR" must be changed so that generic declarations are skipped as
declarations.

### 12.2 Monomorphization handoff

Generic-call handling must enqueue or request concrete instantiations
instead of expecting a pre-lowered generic MIR body to already exist.

### 12.3 Symbol/body lookup

Call resolution and backend-facing lookup must distinguish between:

- declaration symbol
- instantiated function body symbol / instantiation artifact

### 12.4 Diagnostics

The compiler should not emit noisy user-facing diagnostics merely
because a generic declaration body was not lowered.  Skipping is the
intended architecture.

Diagnostics are appropriate only if:

- a concrete instantiation is required but cannot be formed
- substitution is incomplete or inconsistent
- a supposedly concrete MIR body still contains generic residue

## 13. Data model recommendations

These names are illustrative; use repository-native names where they
already exist.

### 13.1 Generic declaration record

```
GenericFunctionDecl
  - decl_id
  - symbol_id
  - type_params
  - signature_hir
  - body_hir
  - provenance
```

### 13.2 Concrete instantiation record

```
GenericFunctionInstance
  - instance_id
  - decl_id
  - type_args
  - instantiated_signature
  - instantiated_body_hir or mir_ref
```

### 13.3 Lowering work item

```
MirWorkItem
  - kind: monomorphic_decl | generic_instance
  - target_id
```

This keeps the worklist honest.

## 14. Enforcement strategy

### 14.1 Prefer fail-closed

If the lowering context is not concretely instantiated, the compiler
should conservatively treat the body as not lowerable.

### 14.2 Add assertion at MIR boundary

Even after orchestration is fixed, add a hard internal assertion or
defensive check that rejects any body whose types still contain
generic parameters.

This is important because it catches future regressions early.

Example intent:

```cpp
assert(!mir_body_contains_generic_params(body));
```

This is an internal invariant check, not necessarily a user-facing
diagnostic.

## 15. Interaction with current workaround

The current workaround should be removed, not layered under the new
system.

Task 28 is successful only if the code path no longer relies on
"temporary allowance" behavior for generic MIR lowering.

Any residual fallback that still lowers uninstantiated generic bodies
should be treated as unfinished work.

## 16. Interaction with multi-file compilation

This task must work across module/file boundaries.

Generic declarations may be defined in one module and instantiated
from another.  The architecture therefore must not assume
"instantiation happens only next to declaration."

Required behavior:

- declaration remains discoverable program-wide
- instantiation identity is program-wide
- MIR is generated for concrete instances regardless of where use
  occurs

## 17. Interaction with methods

If methods may be generic or belong to generic types, the same rule
applies:

- generic method declarations are templates
- concrete receiver/type substitutions must exist before MIR lowering

Task 28 should not special-case free functions in a way that bakes
in future pain for methods.

## 18. Interaction with concepts / conformance

If concept-constrained generic functions already exist or are
planned, Task 28 does not need to solve concept dispatch.

But it must preserve the right boundary:

- constraint checking may occur at semantic/typecheck stage
- MIR lowering still requires a concrete instantiation

## 19. Migration plan

### 19.1 Step 1 — identify current lowering entrypoints

Find every place where function bodies are lowered to MIR and
classify whether they are operating on:

- all declarations indiscriminately
- only monomorphic declarations
- already-instantiated concrete bodies

### 19.2 Step 2 — insert explicit eligibility classification

Introduce the lowerability classification near the orchestration
layer.

### 19.3 Step 3 — stop enqueuing generic declaration bodies

Change worklist construction so generic declarations are retained
but not lowered.

### 19.4 Step 4 — route generic uses through instantiation

Where concrete generic uses require executable code, ensure the
pipeline creates or retrieves an instantiation record and lowers
that instance.

### 19.5 Step 5 — add invariant assertion

Add a defensive check at or just inside MIR construction to reject
residual generic parameters.

### 19.6 Step 6 — remove workaround

Delete the workaround path from PR #237's behavior once the new
path is passing.

## 20. Diagnostics guidance

### 20.1 User-facing diagnostics should remain semantic

Do not introduce a user-facing error like "generic body skipped" as
normal compiler output.

That is internal mechanics.

### 20.2 Internal errors should be crisp

If a regression occurs, internal diagnostics/assertions should be
specific:

- `attempted MIR lowering of uninstantiated generic function`
- `generic parameter residue found in concrete MIR lowering`
- `missing concrete instantiation for generic callee`

## 21. Test plan

### 21.1 Regression tests for the bug/workaround boundary

At minimum:

1. **Generic declaration not lowered eagerly** — a program defines a
   generic function but never instantiates it; compiler succeeds; no
   MIR body is emitted for that declaration.
2. **Concrete instantiation lowered** — a generic function is called
   with concrete type arguments or inferable concrete use;
   corresponding concrete body is lowered to MIR.
3. **Multiple instantiations** — same generic declaration
   instantiated with two different concrete type argument lists; two
   distinct concrete MIR bodies or instance records exist as
   appropriate.
4. **Deduplicated same instantiation** — same concrete instantiation
   requested multiple times; only one canonical lowered instance is
   produced.

### 21.2 Cross-module tests

1. **Cross-module generic declaration / use** — module A defines
   generic function; module B calls it concretely; declaration is
   retained in A, concrete instance lowered for program use.
2. **Unused exported generic** — generic function exported but never
   instantiated; no MIR body emitted.

### 21.3 Invariant tests

1. **MIR generic residue assertion** — a test or internal harness
   ensures MIR construction rejects generic residue if forced down
   the wrong path.
2. **Non-generic body still lowers normally** — ordinary monomorphic
   functions unchanged.

### 21.4 Method-oriented forward-compat test

1. **Generic method declaration skipped until instantiated** — if
   method generics already exist in the host compiler; otherwise
   mark this test deferred but name it now.

## 22. Acceptance criteria

1. Uninstantiated generic function bodies are not lowered to MIR.
2. Monomorphic function bodies still lower exactly as before.
3. Concrete generic instantiations lower successfully.
4. No MIR body contains unresolved generic parameter types.
5. The current workaround path is removed or made unreachable.
6. Cross-module generic instantiation works.
7. Duplicate requests for the same instantiation do not produce
   duplicate concrete bodies.
8. At least one invariant/assertion guards against regression at the
   MIR boundary.
9. Existing non-generic MIR tests continue to pass.

## 23. Risks

### 23.1 Hidden assumptions in current MIR pipeline

Some downstream logic may currently assume every function
declaration has a MIR body.

That assumption must be found and corrected.

### 23.2 Instantiation identity drift

If instance identity is underspecified, the compiler may emit
duplicates or conflate distinct instances.

### 23.3 Cross-module ownership confusion

If the compiler has not clearly separated declaration ownership from
instantiation ownership, multi-file generic usage may produce
confusing lookup behavior.

### 23.4 Workaround residue

The biggest practical risk is leaving part of the workaround alive,
producing a half-old, half-new pipeline.

Task 28 should finish with one coherent model.

## 24. Explicit deferrals

- generic specialization heuristics
- concept-based dispatch lowering
- aggressive instance dedup beyond semantic identity
- instance caching across compiler runs
- bootstrap compiler generic MIR architecture
- backend optimization of instantiated generic bodies

## 25. Recommended immediate implementation order

1. Identify all MIR body creation entrypoints.
2. Add explicit lowerability classification.
3. Stop generic declaration bodies from entering the MIR worklist.
4. Route concrete generic uses through instantiation.
5. Add MIR-boundary invariant assertion.
6. Remove workaround.
7. Land regression tests.

## 26. Recommended follow-up after Task 28

Once Task 28 lands:

- update roadmap / implementation plan to mark the generic MIR
  boundary as corrected
- then scope the next bootstrap Tier B slice

The likely next bootstrap slice after that should be one coherent
vertical feature, not a grab-bag — most likely either:

- generic type parameters through bootstrap semantic frontend, or
- method/associated-item semantics

But that is separate from Task 28.

## 27. One-sentence summary

Task 28 makes generic declarations stay generic until concretely
instantiated, and only concrete instantiations are allowed to become
MIR.
