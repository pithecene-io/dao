# IDE and Tooling Posture — Dao

This document is explanatory. It captures why tooling concerns are raised
close to compiler architecture instead of being deferred.

## Principle

Developer experience is a first-class concern for Dao.

Compiler architecture must preserve enough structured semantic information
to support:
- semantic syntax highlighting
- diagnostics with stable spans
- hover and completion
- symbol navigation
- future refactoring tools

## Why Some Decisions Are Contracts and Others Are Freestanding

Contracts freeze the boundary lines that other work must be able to rely
on. For tooling, that means:
- no shadow parser
- no shadow semantic model
- compiler-owned semantic tokens and diagnostics
- the initial minimum LSP slice
- the playground as a first-class development surface

Freestanding tooling docs keep decisions that are still expected to learn
from implementation work, including:
- exact transport shape between UI and compiler
- payload details and caching choices
- editor UX sequencing
- browser IDE layout and panes
- incrementality strategy

A good rule is:
- if violating the decision would fork the language truth, it belongs in a
  contract
- if violating the decision would only change delivery strategy or tooling
  UX, it stays explanatory

## Shared Analysis Surface

The compiler should expose a narrow analysis API that can serve:
- CLI diagnostics and explain views
- playground semantic views
- LSP requests

Expected payload classes:
- tokens
- diagnostics
- symbol identities
- type display payloads
- completion lists
- definition/reference locations
- document symbol trees

## Long-Term North Star

The playground is not only a demo surface. It is the first stage of a
future web-based IDE for Dao.

That north star implies the compiler must preserve:
- stable symbol identity
- incremental-friendly spans
- semantic token classification
- structured hover/completion payloads
- inspectable AST / HIR / MIR views over time
