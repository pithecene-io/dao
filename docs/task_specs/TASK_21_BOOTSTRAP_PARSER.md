# Task 21 — Bootstrap Parser

Status: implementation spec
Phase: Phase 7 — Bootstrap Compiler
Scope: promote bootstrap parsing from probe form to a real Dao parser subsystem

## 1. Objective

Implement the first real Dao parser subsystem in Dao itself.

Task 21 takes the proven lexer/bootstrap probe path and extends it into
a maintained parser capable of parsing a meaningful Tier A slice of real
Dao source into a structured, arena-indexed AST suitable for later
bootstrap phases.

This is not a parser toy and not a host-compiler replacement.
It is the first maintained bootstrap parser subsystem.

The result of this task should be:

* a real parser module under the bootstrap lane
* a stable parser API
* a stable Tier A AST representation
* parser tests over representative Dao source
* documented Tier A / Tier B boundaries
* a self-parse smoke signal

## 2. Strategic intent

Task 21 is the natural follow-on to the bootstrap lexer and parser
probes.

Its purpose is to answer:

> Can Dao represent and maintain a real parser subsystem for its own
> language, using compiler-shaped data structures and control flow,
> without collapsing into probe-only scaffolding?

This task must preserve the distinction between:

* **exploratory probes** that prove viability
* **maintained bootstrap subsystems** that later self-hosting work
  can build on

## 3. Why this task exists now

The bootstrap probes already established that Dao can support:

* file reading
* structured lexing
* vectors of compiler-shaped values
* payload enums
* match/else-if branching
* recursive descent over token streams
* arena-indexed trees
* `Option<T>` / `Result<T, E>`
* basic compiler-style multi-pass flows

The remaining step is to promote parsing from arithmetic-probe scale
to a real subsystem that can parse a meaningful Dao slice.

## 4. Non-goals

Task 21 must not include:

* host parser replacement
* AST → HIR lowering
* name resolution
* type checking
* conformance checking
* module system implementation
* import graph handling
* full diagnostic renderer
* parser performance tuning beyond obvious correctness-preserving
  cleanup
* grammar completion for the entire language
* bootstrap codegen or backend extraction

This task is about a real parser subsystem, not the whole bootstrap
compiler.

## 5. Deliverable summary

Task 21 must deliver:

1. A maintained bootstrap parser module in a stable bootstrap location
2. A stable AST representation for the Tier A syntax slice
3. A stable parser entrypoint returning parse results as data
4. Golden tests for representative syntax
5. A self-parse smoke/regression path
6. Explicit documentation of Tier A supported grammar and Tier B
   deferrals

## 6. Repository placement

Recommended target shape:

```text
bootstrap/
  README.md
  lexer/
    ...
  parser/
    parser.dao
```

### Placement rule

* `examples/bootstrap_probe/` remains exploratory
* `bootstrap/` is maintained bootstrap infrastructure

Task 21 belongs in the maintained bootstrap lane.

## 7. Tier A syntax scope

Tier A is the minimum parser slice that should be treated as real
bootstrap infrastructure.

### 7.1 Declarations

Supported:

* `fn` declarations
  - block-bodied
  - expression-bodied
* `extern fn`
* `class` declarations
  - fields only
* `enum` declarations
  - including payload variants
* `type` aliases

### 7.2 Statements

Supported:

* `let`
* assignment
* `if` / `else`
* `while`
* `for ... in`
* `return`
* `break`
* `match`
  - including destructuring already supported by the language surface
* expression statements

### 7.3 Expressions

Supported:

* full precedence tower from pipe through primary
* call
* field access
* index
* postfix `?`
* lambda
* literals
* identifiers
* qualified names (`A::B`)
* list literals

### 7.4 Types

Supported:

* named types
* generic instantiation (`Vector<Token>`)
* pointer types (`*T`)

## 8. Tier B deferred syntax

The following are explicitly deferred:

* `concept`
* derived concepts
* `extend`
* class methods / conformance-specific parsing if it complicates
  Tier A
* `mode` / `resource` blocks
* `yield`
* function types
* generic parameter bounds
* static method calls requiring `<T>` disambiguation
* any syntax not required for the current bootstrap parser lane

These deferrals must be documented, not silently omitted.

## 9. Fallback cuts if Tier A balloons

If scope pressure becomes too high during implementation, cut in this
order before destabilizing the task:

1. list literals
2. lambda
3. match destructuring details

Do not cut the core declaration/statement backbone first.

## 10. Architecture

### 10.1 AST shape

Use an arena-indexed AST.

The parser should produce nodes stored in vectors/arenas and reference
children by indices, not recursive by-value object graphs.

This is already the proven bootstrap shape and should remain the
default.

### 10.2 Parser state

Use an explicit `ParserState` object/class threaded through parsing
operations.

Responsibilities include:

* token stream access
* current position
* lookahead helpers
* error accumulation or fail-fast state
* source/token context as needed

### 10.3 Token model

If the single-file bootstrap constraint makes shared token definitions
impractical, token model duplication is acceptable for this task only
as **temporary debt**.

The spec must treat that duplication as temporary, not canonical
architecture.

Required note:

> Tier A may duplicate token definitions from the bootstrap lexer due
> to current bootstrap subsystem constraints.  This is follow-up debt
> and should be centralized once shared bootstrap module boundaries
> stabilize.

### 10.4 File shape

Current implementation target: single-file parser under
`bootstrap/parser/parser.dao`.

That is acceptable for Task 21 if it materially reduces bootstrap
friction.

But the spec must state clearly:

* single-file is a task-scoped implementation constraint
* it is not yet a normative architecture commitment for the
  long-term bootstrap parser

## 11. AST requirements

The AST must be stable enough for later bootstrap consumers.

Minimum requirements:

* declaration node kinds
* statement node kinds
* expression node kinds
* type node kinds
* payload fields appropriate to each variant
* source span attachment or span references where practical

The AST should be shaped for later parsing/lowering work, not for
pretty-print-only tests.

## 12. Parser API

The parser must expose a narrow, data-oriented API.

Recommended shape:

* parse a token stream into a file/module AST root
* return structured result or equivalent
* optionally expose helper entrypoints for focused tests if useful

The parser must not primarily communicate through inline printing or
probe-style debugging output.

## 13. Error handling and recovery

Task 21 should define parser failure behavior explicitly.

### Minimum requirement

* parse errors must be represented as data
* errors must carry enough location/span information for later
  formatting
* malformed inputs in tests must fail predictably

### Recovery expectation

Tier A does not require industrial-grade parser recovery.

Acceptable initial modes:

* fail-fast parse with structured errors
* limited synchronization at declaration/statement boundaries if
  straightforward

Recommended initial stance:

> structured fail-fast, with narrow synchronization only if it falls
> out naturally

Do not overbuild recovery here.

## 14. Parity target

The bootstrap parser does not need full host-parser parity yet.

### Minimum parity target

For the Tier A supported slice, the bootstrap parser should
successfully parse representative Dao source used in bootstrap code
and targeted test fixtures.

### Explicit limitation rule

Known unsupported syntax or mismatches relative to the host parser
must be documented in Task 21 outputs.

Do not imply whole-language parity.

## 15. Test strategy

Tests are mandatory.

### 15.1 Golden parse tests

Add fixtures and expected parse shapes for representative inputs
covering:

* declarations
* statements
* precedence-sensitive expressions
* generics in type syntax
* enums with payloads
* list/qualified names/index/call cases if supported in Tier A

### 15.2 Error tests

Include malformed input cases that assert predictable parse failure.

### 15.3 Self-parse smoke test

The parser must be able to parse a designated bootstrap parser/lexer
source file or equivalent real bootstrap source as a smoke/regression
signal.

This does not require full AST golden coverage for self-parse.
It does require that the parser can consume real source without
probe-only shortcuts.

### 15.4 Probe parity tests

Preserve or adapt useful expr-parser probe cases as locked regressions
for the new subsystem.

## 16. Integration strategy

Task 21 does not need to replace the host compiler parser.

Initial integration target:

* buildable and testable as a maintained bootstrap parser component
* callable from a dedicated bootstrap harness or test entry
* repository-recognized as real bootstrap compiler infrastructure

### Explicit non-goal

Do not wire the bootstrap parser into the production compiler path
in this task.

## 17. Probe-to-subsystem migration plan

Task 21 must explicitly answer how probe code is handled.

Allowed outcomes:

* refactor and promote probe logic into the subsystem
* reimplement cleanly while keeping probe files as historical
  artifacts
* delete or clearly demote obsolete parser probes after promotion

At task completion there should be one clearly authoritative bootstrap
parser implementation.

## 18. Documentation deliverables

Add or update bootstrap documentation covering:

* what the bootstrap parser supports
* Tier A syntax slice
* Tier B deferrals
* how to run parser tests
* how it relates to the bootstrap lexer
* how it differs from the host parser today

Keep this practical.

## 19. Acceptance criteria

Task 21 is complete when all of the following are true:

1. A maintained bootstrap parser exists outside the probe lane.
2. The parser can parse the Tier A syntax slice defined in this spec.
3. The parser returns structured AST and structured errors as data.
4. The AST representation is arena-indexed and usable by later
   bootstrap stages.
5. Golden tests exist for representative syntax.
6. Malformed-input tests exist and behave predictably.
7. A self-parse smoke/regression path exists.
8. Tier A support and Tier B deferrals are documented explicitly.
9. There is one authoritative bootstrap parser implementation rather
   than drifting probe copies.

## 20. Implementation order

Recommended implementation order:

1. Write/finalize this spec
2. Choose stable bootstrap parser location
3. Scaffold parser file with token model, AST node definitions, and
   ParserState
4. Implement expression parsing
5. Implement type parsing
6. Implement statement parsing
7. Implement declaration parsing and file-level loop
8. Add golden tests
9. Add malformed-input tests
10. Add self-parse smoke test
11. Update bootstrap docs / index

## 21. Risks to avoid

### 21.1 Pathname-only promotion

Do not treat moving a probe into `bootstrap/` as sufficient.

### 21.2 Silent scope creep

Do not let parser coverage drift beyond Tier A without updating the
task boundary.

### 21.3 Accidental architecture freeze

Do not let the single-file constraint become an unexamined long-term
parser architecture decision.

### 21.4 Probe duplication

Do not leave multiple parser implementations competing for authority.

### 21.5 Overbuilt recovery

Do not sink the task into industrial parser recovery design.

## 22. Exit statement

Task 21 succeeds when Dao can truthfully claim:

> The project contains a real parser subsystem written in Dao,
> maintained as bootstrap compiler infrastructure, capable of parsing
> a documented Tier A slice of Dao source into structured AST with
> predictable error behavior.

## 23. Likely follow-on tasks enabled by Task 21

A completed Task 21 should make the following next steps much more
concrete:

* bootstrap diagnostic formatting
* bootstrap resolver extraction
* bootstrap type checker extraction
* shared bootstrap token/source/span consolidation
* richer pattern/error/result ergonomics as required by later phases
