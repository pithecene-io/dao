# tools/playground

First-class web playground for Dao.

This surface is not a disposable demo. It is part of the compiler
hardening loop and should consume compiler-produced diagnostics,
semantic tokens, hover payloads, and document symbol information as those
services come online.

Long-term north star: lightweight web-based IDE.

## Quick start

```
# from repo root, after cmake build:
build/debug/tools/playground/compiler_service/dao_playground

# open http://localhost:8080
```

Options:
- `--port N` — listen port (default 8080)
- `--root DIR` — repository root (default: compile-time `DAO_SOURCE_DIR`)

## Architecture

- `compiler_service/` — C++ HTTP server (cpp-httplib) wrapping the
  compiler frontend (lexer, parser, AST printer)
- `frontend/` — vanilla HTML/CSS/JS with CodeMirror 6 (loaded from CDN)

## API

- `POST /api/analyze` — accepts `{"source": "..."}`, returns tokens,
  AST text, and diagnostics
- `GET /api/examples` — lists `examples/*.dao` files
- `GET /api/examples/:name` — returns example source
