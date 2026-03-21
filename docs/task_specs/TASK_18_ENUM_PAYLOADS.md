# Task 18 — Enum Payloads and Match Destructuring

## Objective

Add payload-bearing enum variants and match-arm destructuring so that
compiler-shaped data (AST nodes, tokens with values, result carriers)
can be represented as honest tagged unions instead of parallel-field
workarounds.

## Governing contracts

- `CONTRACT_TYPE_SYSTEM_FOUNDATIONS.md` §7 — "Dao enums are true sum
  types. Payload-bearing variants are part of the intended model."
- `CONTRACT_SYNTAX_SURFACE.md` — pattern matching syntax is listed
  under Non-Laws (not frozen); this task introduces the first cut.

## What already exists

| Primitive | Status |
|-----------|--------|
| Payload-free `enum` declarations | ✅ Full pipeline |
| Qualified variant access (`E.V`) | ✅ via FieldExpr |
| `match` over enum discriminants | ✅ HIR desugars to if/else chain |
| `EnumVariant.payload_types` vector | ✅ Present in `type.h`, always empty |
| `TypeEnum` with named variants | ✅ Full |
| LLVM lowering of enums | ✅ i32 discriminant only |

## Surface syntax

### Enum declaration with payloads

```dao
enum Token:
  Int(i64)
  Ident(string)
  Plus
  Minus
  Eof

enum Expr:
  Lit(i64)
  Binary(i32, i64, i64)
  Unary(i32, i64)
```

Rules:
- Variants may have zero or more positional payload fields.
- Payload-free variants and payload-bearing variants may coexist.
- Payload types are positional, not named (first cut).
- Each payload type must be a valid Dao type expression.

### Variant construction

```dao
let t: Token = Token.Int(42)
let e: Expr = Expr.Lit(7)
let plus: Token = Token.Plus
```

Rules:
- Payload-bearing variants are constructed with call syntax:
  `EnumName.Variant(arg1, arg2, ...)`.
- Payload-free variants remain as today: `EnumName.Variant`.
- Arity must match the variant declaration exactly.
- Argument types must be assignable to the declared payload types.
- The result type is the enum type, not the variant type.

### Match destructuring

```dao
match token:
  Token.Int(value):
    print(value)
  Token.Ident(name):
    print(name)
  Token.Plus:
    print("+")
  Token.Minus:
    print("-")
  Token.Eof:
    print("eof")
```

Rules:
- Payload-free arms remain as today: `EnumName.Variant:`.
- Payload-bearing arms bind positional fields to fresh local names:
  `EnumName.Variant(binding1, binding2):`.
- Bindings are immutable locals scoped to the arm body.
- Binding count must match the variant's payload arity exactly.
- Nested patterns are **out of scope** — bindings are identifiers only.
- Wildcard / catch-all arms are **out of scope** for this task.
- Guards are **out of scope** for this task.
- Exhaustiveness checking is **out of scope** for this task (but the
  representation should not preclude it).

## Deliverables

### 1. AST — payload types on variant specs

Extend `EnumVariantSpec` to carry optional payload type nodes:

```cpp
struct EnumVariantSpec {
  std::string_view name;
  Span name_span;
  std::vector<TypeNode*> payload_types; // empty for payload-free
};
```

No new AST node kinds needed. Variant construction reuses `CallExpr`
on a `FieldExpr` target. Match arm patterns need a new node or
extension to carry bindings — see §3.

### 2. Parser — variant payload syntax

Extend `parse_enum_decl()`:
- After parsing the variant name, check for `(`.
- If present, parse a comma-separated list of type expressions
  until `)`.
- Store the types in `EnumVariantSpec::payload_types`.

Extend `parse_match_statement()`:
- When parsing an arm pattern, after the qualified enum variant
  name (`Enum.Variant`), check for `(`.
- If present, parse comma-separated identifiers (binding names)
  until `)`.
- Store the bindings on the match arm (see §3 for AST
  representation).

### 3. AST — match arm destructuring bindings

Extend `MatchArm` to carry optional binding names:

```cpp
struct MatchArm {
  Expr* pattern;                           // qualified variant ref
  std::vector<std::string_view> bindings;  // empty for payload-free
  std::vector<Span> binding_spans;         // for diagnostics
  std::vector<Stmt*> body;
};
```

Alternatively, introduce a dedicated `MatchPattern` node — either
approach is acceptable as long as the bindings and their spans are
preserved for diagnostics and semantic tokens.

### 4. Resolver — variant constructor and binding scopes

- Variant construction (`Token.Int(42)`) resolves through the
  existing FieldExpr → CallExpr path. The resolver does not need
  major changes; the type checker handles arity/type validation.
- Match arm bindings must be introduced as local symbols scoped to
  the arm body. The resolver should register each binding name in
  a fresh scope opened for each arm.

### 5. Type checker — payload validation

**Enum declaration:**
- For each variant with payload types, resolve the type nodes to
  `const Type*` and populate `EnumVariant::payload_types`.

**Variant construction:**
- When a `CallExpr` targets a FieldExpr that resolves to an enum
  variant: validate arity matches, validate argument types are
  assignable to payload types, and type the expression as the
  enum type.
- When a payload-bearing variant is used without call syntax
  (no parens), emit a diagnostic.
- When a payload-free variant is used with call syntax, emit a
  diagnostic.

**Match arm destructuring:**
- For each arm with bindings, look up the matched variant's
  payload types.
- Validate binding count matches payload arity.
- Type each binding as the corresponding payload type.
- Register the bindings in the typed results so downstream passes
  can resolve them.

### 6. HIR — variant construction and payload extraction

**Construction:**
- Add `HirEnumConstruct` (or similar) that carries the discriminant
  index, the payload expressions, and the enum type. The HIR
  builder emits this when it sees a variant constructor call.

**Match lowering:**
- The existing if/else-chain strategy over discriminants remains
  valid for dispatch.
- For payload-bearing arms: after the discriminant check, emit
  payload extraction operations (project each payload field from
  the enum value) and bind them to the arm's local symbols.
- The extraction operations are analogous to struct field access
  but indexed by position within the variant's payload.

### 7. MIR — enum representation and instructions

**New or extended instructions:**

| Instruction | Semantics |
|-------------|-----------|
| `MirEnumConstruct` | Build an enum value: discriminant + payload values → enum |
| `MirEnumDiscriminant` | Extract the i32 tag from an enum value |
| `MirEnumPayload` | Extract payload field `i` from variant `v` of an enum value |

The discriminant comparison in match lowering already works via
`MirBinary` with `EqEq`. Payload extraction is the new operation.

### 8. LLVM — discriminated union lowering

**Type lowering:**

A payload-bearing enum lowers to an LLVM struct:

```
{ i32, [max_payload_size x i8] }
```

Where:
- Field 0 is the i32 discriminant tag.
- Field 1 is a byte array sized to the largest variant payload.

Payload-free enums continue to lower to bare `i32`.

**Alternative:** use an LLVM struct with the discriminant and a
union-like nested struct for each variant. The byte-array approach
is simpler and avoids LLVM union complexities. The choice is an
implementation detail — either is acceptable as long as:
- Construction stores the discriminant and bitcasts payload fields
  into the payload region.
- Extraction reads the discriminant and bitcasts the payload region
  back to the expected types.
- Alignment of the payload region respects the maximum alignment
  of any variant's payload types.

**Construction codegen:**
1. Allocate or produce a value of the enum struct type.
2. Store the discriminant index into field 0.
3. Bitcast the payload region pointer to the variant's payload
   struct type and store each field.

**Extraction codegen:**
1. Load the discriminant from field 0 (for match dispatch).
2. Bitcast the payload region pointer to the variant's payload
   struct type and load the requested field.

**Size and alignment:**
- The payload region size is `max(size_of(variant_payload))` across
  all variants.
- The payload region alignment is `max(align_of(variant_field))`
  across all variants and fields.
- `size_of<E>()` and `align_of<E>()` must return correct values
  for payload-bearing enums after this change.

### 9. Runtime — equality and printing

**Equality (`==`, `!=`):**
- Payload-free enum equality remains as-is (i32 comparison).
- For payload-bearing enums: first compare discriminants. If equal,
  compare payload fields. This is analogous to struct equality but
  variant-aware.
- First cut: payload-bearing enum equality is **deferred**. Only
  discriminant comparison is required (sufficient for match).
  Full structural equality can follow when needed.

**Printing:**
- First cut: printing a payload-bearing enum can show the variant
  name and payload values, e.g. `Token.Int(42)`. Implementation
  via a generated or runtime-assisted print hook.
- Alternatively, defer pretty-printing of payload enums and only
  require that payload-free enum printing continues to work.
  The bootstrap probes will use explicit match-based formatting.

### 10. Analysis APIs

- **Semantic tokens:** variant names in declarations and patterns
  should continue to classify as `enumMember`. Binding names in
  match arms should classify as `variable` (declaration).
- **Hover:** hovering a variant constructor should show the variant
  name and payload types. Hovering a match binding should show its
  inferred type.
- **Completion:** after `EnumName.`, payload-bearing variants should
  appear with their payload signature in the completion detail.
- **Go-to-definition:** a match binding should jump to the match arm
  where it is introduced.

## Proof of concept

After all deliverables land, rewrite `examples/bootstrap_probe/expr_parser.dao`
to use payload enums for tokens and/or AST nodes. The rewrite should
eliminate the parallel-field workarounds currently in `ExprNode`:

```dao
// Before (parallel fields):
class ExprNode:
  kind: NK
  op: TK
  value: i64
  left: i64
  right: i64

// After (payload enum):
enum ExprNode:
  IntLit(i64)
  Binary(TK, i64, i64)
  Unary(TK, i64)
```

And the match dispatch becomes:

```dao
// Before:
match node.kind:
  NK.IntLit:
    return node.value
  NK.Binary:
    let lval = eval_node(arena, node.left)
    ...

// After:
match node:
  ExprNode.IntLit(value):
    return value
  ExprNode.Binary(op, lhs, rhs):
    let lval = eval_node(arena, lhs)
    ...
```

Also add `examples/enums_payload.dao` as a standalone example
demonstrating the feature.

## What this task does NOT include

- Named payload fields (first cut is positional only)
- Nested patterns (`Expr.Binary(TK.Plus, _, _)`)
- Wildcard / catch-all arms (`_:`)
- Guard clauses (`Expr.Binary(op, l, r) if op == TK.Plus:`)
- Exhaustiveness checking
- Generic payload enums (`enum Option<T>: Some(T) | None`)
- Payload-bearing enum equality beyond discriminant comparison
- `match` as an expression (returns a value)
- Irrefutable let-destructuring (`let Token.Int(v) = tok`)

## What is deliberately deferred

| Topic | Reason |
|-------|--------|
| Named fields on variants | Adds syntax complexity; positional is sufficient for bootstrap probes |
| Generic ADTs (`Option<T>`, `Result<T, E>`) | Requires generic enum instantiation; separate task after this proves the base |
| Exhaustiveness checking | Valuable but not required for correctness; separate hardening task |
| Wildcard / catch-all arms | Simple addition once the core works; can follow quickly |
| Match expressions | Requires expression-position match; separate task |
| Nested patterns | Full pattern language is a separate design surface |
| Payload equality | Needs recursive structural comparison; follow-up task |

## Exit criteria

1. Enum variants with positional payload types parse and type-check
2. Variant constructors (`Token.Int(42)`) compile end-to-end
3. Match arms with destructuring bindings (`Token.Int(v):`) compile
   and bind correctly
4. Mixed enums (payload-free + payload-bearing variants) work
5. LLVM lowering produces correct discriminated-union layout
6. `size_of<E>()` and `align_of<E>()` return correct values for
   payload-bearing enums
7. `examples/enums_payload.dao` compiles, runs, and produces correct
   output
8. The expr_parser bootstrap probe can be rewritten to use payload
   enums for at least one of its data types (token or AST node)
9. Payload-free enum behavior is unchanged (no regressions)
