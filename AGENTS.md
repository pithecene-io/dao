# AGENTS.md — Dao Guardrails

This file defines non-negotiable working rules for Dao.

## Core Principles

- Clarity over cleverness
- Semantics over novelty theater
- Explicit structure over hidden magic
- Minimal syntax with strong visual taxonomy
- Spec-first changes before implementation-first changes

## Scope Discipline

Agents must not:
- invent subsystems beyond the declared repository layout
- redesign syntax casually or opportunistically
- add build tools, CI, or language backends without explicit request
- create new top-level directories without updating `CLAUDE.md` and
  `docs/ARCH_INDEX.md`
- blur normative docs and explanatory docs

## Syntax Discipline

The current frozen surface assumptions are:
- indentation-significant blocks
- `if` / `while` / `for` use `:`
- block-bodied functions use no token after the return type
- expression-bodied functions use `->`
- `extern fn` declares externally-provided functions with no body
- lambdas use `->`
- `mode <name> =>` enters an execution/safety mode
- `resource <kind> <name> =>` binds a scoped resource domain
- `|>` is a first-class pipeline operator

Agents must not introduce alternate spellings for these without an
explicit syntax revision task.

## Dependency Discipline

- All dependencies are managed via **Conan 2.x** in manifest mode
  (`conanfile.txt` or `conanfile.py`).
- Agents must **never** install, suggest, or attempt to install system
  packages (e.g. via `zypper`, `apt`, `dnf`, `brew`, or any other
  system package manager). No exceptions.
- If a dependency is not available in Conan, escalate — do not
  work around it with system packages.

## Repository Discipline

- `docs/contracts/` contains binding contracts only
- `docs/` normal-case prose is explanatory only
- `spec/` holds grammar fragments, reference examples, and syntax probes
- `examples/` holds human-readable Dao snippets, not normative law
- `testdata/` holds fixtures for future parser/compiler tests
- `ai/skills/` holds Bonsai skills only

## Priority Order

1. Correctness of contracts
2. Structural coherence
3. Readability
4. Convenience
5. Extensibility
