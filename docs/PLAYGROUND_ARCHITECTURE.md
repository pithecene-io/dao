# Playground Architecture — Dao

This document is explanatory.

## Role

The Dao playground is both:
1. a learning surface for users
2. a development and testing surface for the compiler team

It is intended to evolve toward a lightweight web-based IDE.

## Immediate Goals

- example-driven language development
- rapid UAT for syntax and diagnostics
- semantic syntax highlighting
- easy inspection of representative `examples/` programs

## Initial Capabilities

- example loader from `/examples`
- editable source panes
- compiler-backed diagnostics as soon as the frontend analysis exists
- compiler-backed semantic tokens as soon as symbol resolution exists

## Mid-Term Capabilities

- type hover
- symbol navigation
- definition peek
- document symbol outline
- AST / HIR panes

## Long-Term Capabilities

- multi-file workspace view
- MIR inspection
- incremental compilation status
- future debugger / runtime visualizations where warranted

## Architectural Rule

The playground must reuse compiler analysis rather than maintaining its own
parser, semantic model, or token taxonomy.
