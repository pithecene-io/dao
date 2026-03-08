# Task 6 — Name Resolution

## Scope

Name resolution only. This pass binds declarations, resolves identifier
references, and produces symbol-level metadata for downstream consumers
(semantic tokens, hover, go-to-definition, diagnostics).

This pass does **not** perform type checking, overload resolution, or
full module system graph construction.

## Multi-Pass Shape

The resolver uses two passes over the AST:

1. **Pass 1 — Collect top-level declarations.** Walk the file and
   register all top-level function, struct, and alias declarations
   plus import bindings into the file scope. This ensures forward
   references between top-level declarations resolve correctly.

2. **Pass 2 — Resolve bodies.** Walk each declaration body (function
   bodies, struct members, alias targets) with nested scopes. Resolve
   identifier references against the scope chain. Emit diagnostics
   for unresolved names and duplicate declarations.

This two-pass design avoids forward-reference ordering issues at the
file level while keeping function-local resolution single-pass
(declarations in function bodies are visible only after their binding
site, matching typical lexical scoping).

## Data Model

### Symbol

```
struct Symbol {
  SymbolKind kind;
  string_view name;
  Span decl_span;         // where the symbol was declared
  const AstNode* decl;    // declaration node (nullable for builtins)
};
```

### SymbolKind

```
enum class SymbolKind {
  Function,     // top-level fn
  Type,         // struct or alias
  Param,        // function parameter
  Local,        // let binding or for-loop variable
  Field,        // struct member
  Module,       // import binding
  Builtin,      // built-in type (int32, float64, etc.)
  LambdaParam,  // lambda |x| parameter
};
```

### Scope

```
class Scope {
  ScopeKind kind;
  Scope* parent;                            // nullptr for the file scope
  map<string_view, Symbol*> declarations;   // names declared in this scope
};
```

### ScopeKind

```
enum class ScopeKind {
  File,       // top-level file scope
  Function,   // function body
  Block,      // if/while/for/mode/resource body
  Struct,     // struct member scope
};
```

### Resolution Result

A side table mapping individual token spans to their resolved symbols.
The key is a span offset, and there may be **multiple entries per AST
node** — notably, qualified names and qualified type paths produce one
entry per resolved segment:

```
struct ResolveResult {
  ResolveContext context;     // arena owning all Symbols and Scopes
  map<uint32_t, Symbol*> uses;  // token span offset -> resolved Symbol*
  vector<Diagnostic> diagnostics;
};
```

For a qualified name like `http::get`, the resolver computes
per-segment spans (offset + length for each identifier token) and
stores an entry for each segment it can resolve. In Task 6, only the
first segment resolves (against the scope chain): `offset("http") →
Module symbol`. The trailing segment `get` is **not resolvable** in
Task 6 because imported module contents are opaque — it requires
cross-file module member lookup, which is out of scope. This matches
the per-token granularity the semantic token classifier needs;
unresolved segments simply produce no entry.

The `uses` table does **not** mutate the AST. Per-segment span
computation reuses the same offset arithmetic the semantic token
classifier already applies to `QualifiedPath` and
`QualifiedNameNode`.

## Deliverables

### New files

- `compiler/frontend/resolve/symbol.h` — `Symbol`, `SymbolKind`
- `compiler/frontend/resolve/scope.h` — `Scope`, `ScopeKind`
- `compiler/frontend/resolve/resolve.h` — `ResolveResult`, `resolve()` API
- `compiler/frontend/resolve/resolve.cpp` — resolver implementation
- `compiler/frontend/resolve/resolve_test.cpp` — tests
- `compiler/frontend/resolve/resolve_context.h` — arena for symbols/scopes

### Modified files

- `compiler/frontend/CMakeLists.txt` — add resolve sources and test
- `compiler/driver/main.cpp` — add `daoc resolve <file>` subcommand
- `compiler/analysis/semantic_tokens.cpp` — upgrade to consume resolve
  result for `use.variable.param`, `use.variable.local`, `use.function`,
  `use.module`
- `compiler/analysis/semantic_tokens.h` — updated `classify_tokens()`
  signature to accept optional `ResolveResult*`
- `tools/playground/compiler_service/analyze.cpp` — run resolve pass
  and feed result to semantic token classifier
- `Taskfile.yml` — add `resolve` task
- `docs/ARCH_INDEX.md` — add `resolve/` entry under `compiler/frontend/`

## Resolver Behavior

### What gets resolved

- **Identifier expressions** — look up the identifier name in the scope
  chain. If found, record `use → symbol` in the side table.
- **Qualified name expressions** — resolve the first segment in the
  scope chain (must be a module/import binding). Record the resolved
  segment in the `uses` table at its computed span offset. Remaining
  segments cannot be resolved without module contents (no cross-file
  resolution yet) and are left unresolved.
- **Type references** — resolve named type paths against the scope
  chain. Single-segment types check both builtin types and user-declared
  types. Multi-segment type paths resolve the leading segments as
  module references; the final segment is classified structurally
  (type.builtin or type.nominal) as it is today.
- **Import declarations** — bind the **last segment** of the import
  path as a `SymbolKind::Module` symbol in the file scope. For
  `import net::http`, this binds `http`. Subsequent qualified name
  expressions like `http::get` resolve their first segment `http`
  against this binding. The imported module's contents are opaque
  until cross-file resolution exists.

  This is the only composable rule: the binding name matches the
  first segment of qualified references. Alternative designs (binding
  the root, or binding the full path) are explicitly rejected because
  they don't compose with single-segment scope lookup.

### Scope nesting

- **File scope** — contains top-level declarations (functions, structs,
  aliases) and import bindings. Also contains builtin type symbols.
- **Function scope** — contains parameters. Nested inside file scope.
- **Block scope** — created for if/else/while/for/mode/resource bodies.
  Contains let bindings and for-loop variables.
- **Struct scope** — contains field declarations. Nested inside file
  scope. Fields are not visible outside the struct.
- **Lambda** — lambda parameters are introduced into a block scope for
  the lambda body.

### Visibility rules

- **Top-level**: all file-scope declarations are visible everywhere in
  the file (forward references allowed — this is what pass 1 ensures).
- **Function-local**: let bindings and for-loop variables are visible
  only after their declaration site (no forward references within
  function bodies).
- **Parameters**: visible throughout the function body.
- **Struct fields**: visible only within the struct scope (no `self.`
  lookup yet — fields are just declarations, not references).
- **Lambda params**: visible only in the lambda body expression.

### Builtin types

The resolver pre-populates the file scope with builtin type symbols:
`int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32`,
`uint64`, `float32`, `float64`, `bool`, `string`, `void`.

These are `SymbolKind::Builtin` and have no declaration span or node.

## Diagnostics

The resolver emits the following diagnostics:

| Diagnostic | Condition |
|---|---|
| `unknown name 'x'` | identifier reference with no binding in scope chain |
| `duplicate declaration 'x'` | two declarations with the same name in the same scope |
| `duplicate parameter 'x'` | two parameters with the same name in one function |
| `duplicate top-level declaration 'x'` | two file-scope declarations with the same name |

Assignment-target validation stays in the parser (already implemented).

## Semantic Token Upgrade

With resolve results available, the semantic token classifier gains:

| Category | Source |
|---|---|
| `use.variable.param` | reference to a `SymbolKind::Param` |
| `use.variable.local` | reference to a `SymbolKind::Local` |
| `use.function` | reference to a `SymbolKind::Function` |
| `use.module` | reference to a `SymbolKind::Module` |
| `decl.module` | import binding site (last segment of import path) |

The classifier's existing structural classifications (keywords,
literals, operators, `decl.function`, `decl.type`, `decl.field`,
`type.builtin`, `type.nominal`, `mode.*`, `resource.*`, `lambda.param`)
remain unchanged. The resolve-driven classifications fill in the
gaps that were previously omitted.

`decl.module` is produced at the import declaration site — the last
segment of the import path is both the binding name and the
declaration span. Leading segments of the import path remain
classified as `use.module` structurally (they are module path
references, not the binding being introduced).

When no resolve result is available (e.g., source has errors), the
classifier degrades to structural-only mode (current behavior).

## CLI Surface

### `daoc resolve <file>`

Runs lex → parse → resolve. Prints resolved symbols and diagnostics.

Output format (indicative, not stable):

```
Symbols:
  Function main [1:4]
  Param x [1:8]
  Local value [2:9]
  Builtin int32

Uses:
  1:20 x -> Param x [1:8]
  3:5 value -> Local value [2:9]

Diagnostics:
  test.dao:5:5: error: unknown name 'y'
```

## Exit Criteria

- All `examples/*.dao` and `spec/syntax_probes/*.dao` files resolve
  without spurious diagnostics.
- Unresolved identifiers produce clear, stable diagnostics.
- Duplicate declaration diagnostics fire for obvious conflicts.
- `daoc resolve <file>` produces a readable symbol dump.
- Semantic tokens include `use.variable.param`, `use.variable.local`,
  `use.function` (unqualified calls to file-scope functions),
  `use.module` (first segment of qualified names resolving to an
  import binding), and `decl.module` (import binding sites).
  Qualified member references (e.g. `http::get`) do not produce
  `use.function` for the trailing segment until cross-file module
  resolution exists.
- The playground shows resolve-upgraded semantic highlighting.
- Tests cover: scope nesting, forward references at file level,
  duplicate detection, lambda param scoping, for-loop variable
  scoping, builtin type resolution.

## Architectural Constraints

### Symbol ownership

Symbols and scopes are arena-allocated in `ResolveContext`, owned by
the `ResolveResult`. They are not scattered across walker state.

### Resolution is a side table

The resolve pass does **not** mutate the AST. Resolution results are
stored in `ResolveResult::uses` (span offset → symbol pointer). This
keeps the AST immutable and makes it safe for concurrent read access.

### Separation boundary

`resolve/` knows the AST and produces symbol bindings. It does not
invent types, perform type inference, or check type compatibility.
`typecheck/` (Task 8) consumes resolved symbols later.

## Non-Goals

The following are explicitly out of scope for Task 6:

- Generic constraint resolution
- Overload resolution
- Method lookup or `self.` resolution
- Cross-file / multi-module package graph
- C ABI interop symbol import
- Type inference
- Type compatibility checking
- Expression type computation
- Pattern matching or destructuring
- Visibility modifiers (public/private)
