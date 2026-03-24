# Bootstrap Compiler — Dao

Self-hosting compiler subsystems written in Dao.

This directory contains maintained bootstrap infrastructure — code that
is intended to evolve into the self-hosted Dao compiler.  It is **not**
a probe or experiment directory.

## Subsystems

### `lexer/`

Indentation-aware lexer matching the host compiler's token surface
(`compiler/frontend/lexer/`).

**Status**: promoted from probe (Task 20, Phase 7).

**What it supports**:

- All token kinds from `compiler/frontend/lexer/token.h`
- Indentation-sensitive INDENT/DEDENT emission
- Newline suppression inside parentheses and brackets
- Error tokens for tabs, inconsistent indentation, unterminated strings,
  and unexpected characters
- Source span tracking (offset + length) for all tokens
- Diagnostics as data (`LexResult.diagnostics`) — no inline printing

**Parity with host lexer**:

The bootstrap lexer matches the C++ lexer on:

- Token kind sequences for well-formed source
- Error-token emission (TK.Error) matching `emit_error()` behavior
- INDENT/DEDENT spans (`line_begin` position, correct lengths)
- Comment handling (skipped as trivia, comment-only lines don't
  affect indentation)

**Known limitations**:

- Single-file only — no imports/modules (Dao limitation, not lexer)
- No hex/octal/binary integer literal support
- No multiline string literals
- No raw string literals
- Escape sequences are consumed but not validated beyond `\\`
- No Unicode identifier support
- `vec_pop` is O(n) — rebuilds the vector on every pop

**How to run tests**:

```sh
# From repository root:
daoc build bootstrap/lexer/lexer.dao && ./bootstrap/lexer/lexer
```

Tests include golden token stream assertions, malformed-input tests,
a comprehensive Dao snippet regression, and a self-lex smoke test
(reads and tokenizes its own source file).

## Relationship to probes

The `examples/bootstrap_probe/` directory contains earlier experimental
probes that informed the design of these subsystems.  Probes are
learning artifacts; bootstrap subsystems are maintained code.

After Task 20, `examples/bootstrap_probe/dao_lexer_v2.dao` is
superseded by `bootstrap/lexer/lexer.dao`.

## What comes next

- Bootstrap parser extraction
- Diagnostic formatting for bootstrap errors
- Shared source buffer / span infrastructure
- Stronger parity testing (host lexer golden comparison)
