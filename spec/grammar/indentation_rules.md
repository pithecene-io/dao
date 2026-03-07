# Indentation Rules

Dao uses indentation to delimit suites.

## Baseline rules

- suites begin after a block introducer and the following newline
- the lexer emits `INDENT` and `DEDENT` tokens
- tabs are illegal in source files
- indentation must use spaces only
- indentation width is not semantically fixed, but must be consistent within a block sequence

## Block introducers

The following forms introduce suites:

- block-bodied functions
- `if ...:`
- `else:`
- `while ...:`
- `for ... in ...:`
- `mode ... =>`
- `resource ... ... =>`

## Arrow taxonomy

- `:` marks structural blocks and type annotations
- `->` marks yielded expressions in lambdas and expression-bodied functions
- `=>` marks semantic-context entry for `mode` and `resource`

## Rationale

This split is intentional:

- control flow remains visually light
- semantic/execution context is visually explicit
- function blocks do not need an extra token because indentation already carries structure
