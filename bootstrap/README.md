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

### `parser/`

Recursive-descent parser producing an arena-indexed AST for a Tier A
slice of Dao syntax.

**Status**: promoted from probe (Task 21, Phase 7).

**Tier A syntax** (supported):

- Declarations: `fn` (block + expression-bodied), `extern fn`, `class`
  (fields only), `enum` (with payloads), `type` alias
- Statements: `let`, assignment, `if`/`else`/`else if`, `while`,
  `for...in`, `return`, `break`, `match`, expression statements
- Expressions: full precedence tower (pipe through primary), call,
  field access, index, try (`?`), lambda, list literals, qualified
  names
- Types: named, generic instantiation (`Vector<Token>`), pointer
  (`*T`)

**Tier B deferrals** (explicitly not supported yet):

- `concept`, `derived concept`, `extend`
- Class methods and conformance blocks
- `mode` / `resource` blocks
- `yield`
- Function types (`fn(T): R`)
- Generic parameter bounds and `where` clauses
- `import` declarations
- Pipe continuation across newlines

**Architecture**:

- Single arena `Vector<Node>` with payload enum for all node kinds
- Shared `Vector<i64>` index list for variable-length children
  (params, args, body statements, fields, variants)
- Immutable `ParserState` threaded through all parse functions
- Structured fail-fast with diagnostics as data

**Token model duplication**: The parser duplicates the lexer's token
model (~650 lines) due to the single-file bootstrap constraint.  This
is temporary debt — follow-up work should centralize shared token
definitions once bootstrap module boundaries stabilize.

**How to run tests**:

```sh
# From repository root:
daoc build bootstrap/parser/parser.dao && ./bootstrap/parser/parser
```

Tests include golden parse tree assertions, error recovery tests,
and a self-parse smoke test (lexes and parses a real Dao source file).

## Relationship to probes

The `examples/bootstrap_probe/` directory contains earlier experimental
probes that informed the design of these subsystems.  Probes are
learning artifacts; bootstrap subsystems are maintained code.

After Task 20, `examples/bootstrap_probe/dao_lexer_v2.dao` is
superseded by `bootstrap/lexer/lexer.dao`.

After Task 21, the bootstrap parser supersedes the `expr_parser.dao`
probe. The probe is retained as a historical artifact.

## What comes next

- Bootstrap resolver extraction
- Diagnostic formatting for bootstrap errors
- Shared source buffer / span / token infrastructure
- Stronger parity testing (host parser golden comparison)
- Tier B syntax expansion (concepts, extend, mode/resource)
