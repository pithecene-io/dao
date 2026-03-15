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

## Frontend Toolchain

- **Bundler**: Vite (dev server with HMR, production build to `dist/`)
- **Language**: TypeScript (strict mode)
- **Editor**: CodeMirror 6 (npm, not CDN)
- **Dev workflow**: `task playground-dev` runs Vite dev server (port 5173)
  proxying `/api/*` to C++ backend (port 8090)
- **Prod workflow**: `task playground` builds frontend with Vite then starts
  C++ server serving `dist/`

## Architectural Rule

The playground must reuse compiler analysis rather than maintaining its own
parser, semantic model, or token taxonomy.
