# CONTRACT_EXECUTION_CONTEXTS.md

## Purpose

Defines Dao's visual taxonomy for execution and resource context.

## Control vs Context

- structural control flow uses `:`
- semantic or execution context uses `=>`

These forms are not interchangeable.

## Modes

Modes alter execution, safety, or scheduling rules.

Canonical early modes:
- `mode unsafe =>`
- `mode gpu =>`
- `mode parallel =>`

Laws:
1. `mode` blocks are unary.
2. `mode` blocks do not bind user-defined names.
3. `mode` blocks alter execution semantics, not ordinary control flow.

## Resources

Resources bind named scoped domains.

Canonical early resource form:

```dao
resource memory Search =>
    ...
```

Laws:
1. `resource` blocks are parameterized.
2. Resource names are not types.
3. Resource names are not allocator objects.
4. Resource semantics are lexical and scope-bounded.
5. Resource-specific implementation strategies may vary under the hood,
   but visible semantics must remain stable.

## Intent

Dao distinguishes:
- *what code does* (`if`, `while`, `for`)
- *what rules are in effect* (`mode`)
- *what scoped domain is bound* (`resource`)
