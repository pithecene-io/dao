# Task 20 — Bootstrap Lexer

Status: implementation spec
Phase: Phase 7 — Bootstrap Compiler
Scope: promote the existing Dao lexer probe into a real bootstrap subsystem

## 1. Objective

Promote the current Dao lexer bootstrap probe into a real, maintained bootstrap subsystem that can lex Dao source files with behavior close enough to the host compiler lexer to be used as a genuine self-hosting building block.

This task is not "write another probe."
It is the first transition from exploratory bootstrap code into a repository-supported compiler subsystem written in Dao.

The result of this task should be:

* a real Dao lexer module in a stable bootstrap location
* a stable token model and lexer API
* parity-oriented tests against expected token streams
* integration into the repository as bootstrap infrastructure rather than example code
* clear boundaries for what is and is not yet part of the bootstrap compiler stack

## 2. Strategic intent

Task 20 is the Phase 7 entry point.

Its purpose is to answer a concrete question:

> Can a nontrivial, compiler-real subsystem live in Dao as maintained code rather than as a one-off probe?

The lexer is the correct first subsystem because it is:

* small enough to finish
* semantically meaningful
* already proven viable in probe form
* foundational for later parser/bootstrap work
* a good stress test of strings, enums, match, vectors, file IO, and error reporting patterns

This task must preserve the distinction between:

* **probe artifacts** used to learn
* **bootstrap subsystems** intended to persist and evolve

## 3. Why this task exists now

Recent bootstrap probes established that Dao can already support:

* file reading
* character-by-character scanning
* token classification
* payload enums
* vectors of compiler-shaped values
* recursive and multi-pass compiler-style logic
* `Option<T>` / `Result<T, E>` data flow
* map-backed symbol-like structures in later probes

The lexer is therefore no longer blocked on language viability.
The remaining work is to turn the experimental lexer into a properly located, tested, and maintained subsystem.

## 4. Non-goals

Task 20 must not include:

* parser extraction
* resolver extraction
* typechecker extraction
* direct replacement of the host compiler frontend
* module/import system design
* package manager/bootstrap orchestration
* full diagnostic formatting subsystem unless narrowly needed for lexer errors
* string interning unless a clearly unavoidable lexer need appears
* optimization/performance tuning beyond obvious correctness-preserving cleanup
* broad repo reorganization beyond what is needed for the bootstrap lexer lane

This task is about the lexer becoming real bootstrap infrastructure, not about accelerating the entire self-hosting roadmap in one step.

## 5. Deliverable summary

Task 20 must deliver:

1. A stable Dao bootstrap lexer module in a non-example location
2. A stable token representation and lexer entrypoint
3. Tests proving correct tokenization on representative inputs
4. A clear parity target relative to the current host lexer
5. Documentation explaining scope, boundaries, and intended next steps

## 6. Repository placement

The lexer must move out of the examples/probes lane.

Recommended target shape:

```text
bootstrap/
  README.md
  lexer/
    lexer.dao
    token.dao
    span.dao          # optional if shared here initially
    source.dao        # optional if needed
    tests/
      ...
```

Alternative acceptable shape:

```text
compiler/bootstrap/
  README.md
  lexer/
    ...
```

Pick one and standardize it in the task implementation.
Do not scatter bootstrap compiler code across `examples/` after this task.

### Placement rule

Use this rule:

* `examples/bootstrap_probe/` is for experiments and learning artifacts
* `bootstrap/` or `compiler/bootstrap/` is for maintained self-hosting code

Task 20 is the moment where the lexer crosses that boundary.

## 7. Naming and module boundaries

The bootstrap lexer should have a small, explicit surface.

Recommended modules:

* `token.dao` — token kinds and token data structures
* `lexer.dao` — lexer implementation and public entrypoints
* `span.dao` — source span representation if not already shared elsewhere
* `source.dao` — source buffer helpers only if needed

Do not over-modularize.

The initial bootstrap lexer subsystem should remain easy to read in one sitting.

## 8. Functional requirements

The promoted lexer must support at least the token surface already proven in the probes:

* identifiers
* keywords
* integer literals
* string literals
* operators
* punctuation
* comments already supported by the language
* newlines or equivalent trivia handling, according to current lexer design
* source spans for tokens

If the current host lexer distinguishes trivia versus non-trivia, the bootstrap lexer must either:

* match that behavior, or
* document exactly how it differs in Task 20

## 9. Token model

The token representation must be stable enough for parser/bootstrap use.

Minimum requirements:

* token kind
* source span or offset/length representation
* payload where needed for literal/identifier/string forms, according to current bootstrap design

If payload enums are now available and already proven, use them where they make the token representation more honest.
Do not regress to magic-number token conventions in the promoted subsystem.

The token model should be designed for later parser consumption, not for one-off printing.

## 10. Public API

The lexer should expose a narrow public API, for example:

* lex a source buffer into `Vector<Token>`
* lex a file by path into `Result<Vector<Token>, LexError>` or equivalent
* optionally expose helper APIs only if they are genuinely reusable

Do not expose internal scanning helpers as public bootstrap surface unless necessary.

### API principle

The public interface should be parser-ready:

* returns token data
* returns errors as data
* does not print inline as its primary behavior

The probe phase is over.

## 11. Error model

The bootstrap lexer must stop behaving like a pure demo and behave like a subsystem.

So:

* lexing failures must be represented as data, not just printed side effects
* use existing `Result`/error structures where practical
* errors should include span/location information sufficient for later formatting

Task 20 does **not** need the full final diagnostic renderer.
It does need enough structure that later formatting is possible.

## 12. Parity target

Task 20 must define explicit parity expectations with the existing host lexer.

### Minimum parity target

For the supported lexical slice, the bootstrap lexer should match host-compiler tokenization on:

* token kind sequence
* source span correctness
* representative malformed input behavior where practical

### Acceptable initial limitations

If there are known gaps, they must be listed explicitly, for example:

* unsupported escapes
* trivia behavior mismatch
* incomplete literal classes
* known differences in malformed-input recovery

Do not claim "bootstrap lexer complete" unless parity is actually demonstrated for the intended slice.

## 13. Test strategy

This task lives or dies by tests.

Required test layers:

### 13.1 Golden token stream tests

Add representative source snippets and assert expected token streams.

Cover at least:

* simple declarations
* operators and punctuation
* comments
* string literals
* integer literals
* malformed lexical cases

### 13.2 Self-lex regression

Preserve the existing self-lex legitimacy signal:

* the bootstrap lexer should lex its own source or a designated bootstrap source file
* at minimum this should be a regression/smoke test, even if not asserting every token exhaustively

### 13.3 Probe parity tests

Use one or more previously proven probe inputs as locked regressions.

### 13.4 Host parity comparison where feasible

If the test harness can reasonably compare bootstrap token output to host lexer output for selected files, use checked-in golden expectations derived from the current compiler.

## 14. Integration strategy

The bootstrap lexer does not need to replace the host lexer in the main compiler pipeline yet.

Initial integration target:

* buildable and testable as a first-class bootstrap component
* callable from a dedicated bootstrap harness or tool entry
* repository-recognized as real subsystem code

### Explicit non-goal

Do not wire it into the production compiler path as the default lexer in this task.

Task 20 is about subsystem promotion, not host compiler replacement.

## 15. Probe-to-subsystem migration plan

Task 20 should include an explicit migration decision:

* either move/refactor `dao_lexer_v2.dao` into the new subsystem location
* or copy and then delete/repoint the probe version once the subsystem is stable

Avoid ending up with two drifting lexer implementations.

At the end of Task 20 there should be one clearly authoritative Dao bootstrap lexer implementation.

## 16. Documentation deliverables

Add short documentation under the bootstrap lane, for example `bootstrap/README.md`, covering:

* what the bootstrap lexer is
* what it currently supports
* what it does not yet support
* how to run its tests
* how it relates to the host compiler lexer
* what likely comes next (parser extraction, diagnostic formatter, etc.)

This should be brief and practical.

## 17. Acceptance criteria

Task 20 is complete when all of the following are true:

1. The Dao lexer probe has been promoted into a maintained bootstrap subsystem outside `examples/`.
2. The subsystem exposes a stable lexer API returning token data rather than relying on inline printing behavior.
3. Token kinds/spans/payloads are represented in a parser-usable form.
4. The bootstrap lexer passes golden tests for representative lexical inputs.
5. The bootstrap lexer successfully self-lexes designated bootstrap source as a regression/smoke signal.
6. Known parity goals and known limitations relative to the host lexer are documented explicitly.
7. There is one authoritative Dao bootstrap lexer implementation rather than multiple drifting probe copies.

## 18. Implementation order

Recommended implementation order:

1. Choose and create the stable bootstrap location
2. Extract/refactor token model into stable modules
3. Extract/refactor lexer implementation into subsystem form
4. Replace print-oriented probe behavior with data-oriented API where needed
5. Add golden tests
6. Add self-lex regression
7. Add README / parity notes
8. Remove or clearly demote obsolete probe copies

## 19. Risks to avoid

### 19.1 Pathname-only promotion

Do not "promote" the probe by merely moving files without improving API/tests/boundary clarity.

### 19.2 Probe duplication

Do not leave two similar lexer implementations alive unless one is clearly marked obsolete.

### 19.3 Premature production integration

Do not wire the bootstrap lexer into the main compiler path too early.

### 19.4 Over-design

Do not turn Task 20 into a general bootstrap architecture rewrite.

## 20. Exit statement

Task 20 succeeds when Dao can truthfully claim:

> The project contains a real lexer subsystem written in Dao, maintained as bootstrap compiler infrastructure, tested for representative lexical parity, and ready to serve as the base for later parser/bootstrap extraction.

## 21. Likely follow-on tasks enabled by Task 20

A completed Task 20 should make the following next steps much more concrete:

* bootstrap parser extraction
* diagnostic formatting for bootstrap errors
* source buffer/span shared infrastructure
* stronger parity checking between bootstrap and host frontend behavior
