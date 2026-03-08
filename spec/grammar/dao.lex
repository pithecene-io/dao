# Dao lexical surface (early reference)

Reserved keywords:
- import
- fn
- struct
- type
- let
- if
- else
- while
- for
- in
- return
- mode
- resource
- true
- false
- and
- or

Reserved punctuation/operators:
- :
- ::
- ->
- =>
- =
- ==
- !=
- <
- <=
- >
- >=
- +
- -
- *
- /
- %
- &
- !
- .
- ,
- (
- )
- [
- ]
- |
- |>

Identifiers:
- ASCII identifier baseline for v0
- regex intent: [A-Za-z_][A-Za-z0-9_]*

Numbers:
- integer literals
- float literals
- exact suffix and separator rules intentionally left open for now

Strings:
- double-quoted baseline
- escape handling left to lexer implementation notes

Whitespace:
- Dao is indentation-significant
- the lexer emits INDENT and DEDENT tokens
- tabs are illegal
- indentation must be space-based and consistent within a block
