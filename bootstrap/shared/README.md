# Bootstrap Shared Substrate

Single source of truth for the bootstrap frontend pipeline.

## What this contains

`base.dao` provides the canonical definitions consumed by all bootstrap
subsystems:

- Token model (`TK` enum, `Token`, `LexResult`)
- Character / keyword classification
- Token kind names (`tk_name`)
- Lexer (`lex()`)
- AST (`Node` enum)
- Parser types (`PS`, `PR`, `FlushResult`, `flush_list`, `flush_pairs`)
- Full recursive-descent parser (`parse()`)

## How assembly works

Each bootstrap subsystem has a `.part.dao` fragment containing its
subsystem-specific code (tests, resolver logic, etc.).  The
`assemble.sh` script concatenates `base.dao` with each fragment to
produce the final compilable `.dao` files.

```sh
# From repository root:
bash bootstrap/assemble.sh
```

This produces:
- `bootstrap/lexer/lexer.dao` = `base.dao` + `lexer/tests.part.dao`
- `bootstrap/parser/parser.dao` = `base.dao` + `parser/tests.part.dao`
- `bootstrap/resolver/resolver.dao` = `base.dao` + `resolver/impl.part.dao`

## Editing rules

1. **Edit `base.dao` or `.part.dao` fragments** — never edit the
   assembled `.dao` files directly.
2. **Run `bash bootstrap/assemble.sh`** after any edit.
3. **Verify all three subsystems** still compile and pass tests.

## Why this exists

Dao does not yet support multi-file compilation.  Before consolidation,
every bootstrap subsystem duplicated the entire token model, lexer, and
parser (~2000 lines each).  Three subsystems meant ~4000 lines of
triplicated code.

This shared base eliminates the duplication at the source level.
Assembly produces the single-file outputs the compiler requires.

When Dao gains a module system, this concatenation scheme can be
replaced with proper imports.
