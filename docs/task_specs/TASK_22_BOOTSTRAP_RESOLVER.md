# Task 22 — Bootstrap Resolver

Status: implementation spec
Phase: Phase 7 — Bootstrap Compiler
Scope: file-local name resolution over the bootstrap parser's AST

## 1. Objective

Implement file-local name resolution over the arena-indexed AST
produced by the bootstrap parser (Task 21).

This is the first bootstrap subsystem that proves the AST shape is
usable by downstream compiler phases.  It is not a full semantic
analysis pass — it is scope-chain construction, declaration binding,
and identifier lookup.

The result of this task should be:

* a real resolver module under the bootstrap lane
* two-pass resolution: collect top-level declarations, then resolve
  bodies
* a scope chain with parent-linked scopes and declaration maps
* a uses table mapping source locations to resolved symbols
* diagnostics for unknown names and duplicate declarations
* documented Tier A scope and Tier B deferrals

## 2. Strategic intent

Task 22 is the first downstream consumer of the bootstrap AST.

Its purpose is to answer:

> Is the arena-indexed AST from Task 21 actually usable for compiler
> analysis, or does it need restructuring before semantic work can
> proceed?

If the resolver can walk the AST, build scopes, and resolve names
without heroic workarounds, the AST shape is validated.

## 3. Tier A scope

### 3.1 Scopes

* File scope (top-level declarations, forward-referenced)
* Function scope (parameters)
* Block scope (let bindings, for-loop variables, if/while/match
  branches)
* Struct scope (class fields)
* Lambda scope (lambda parameters)

### 3.2 Declarations

* `fn` / `extern fn` / expression-bodied `fn` → Function symbol
* `class` → Type symbol, with fields as nested declarations
* `enum` → Type symbol
* `type` alias → Type symbol
* `let` → Local symbol
* `for` loop variable → Local symbol
* Function parameters → Param symbol
* Lambda parameters → LambdaParam symbol
* Class fields → Field symbol

### 3.3 Resolution

* `IdentE` → scope chain lookup; emit `unknown name` diagnostic on
  failure
* `QualNameE` → resolve first segment against scope; later segments
  are deferred to type checking
* `NamedT` / `GenericT` → scope chain lookup for the type name; no
  diagnostic on unresolved (matches host resolver behavior)
* `PointerT` → recurse into pointee type
* All expression/statement forms → recursive traversal into children

### 3.4 Builtins

Pre-populate file scope with:

* Integer types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`
* Float types: `f32`, `f64`
* Other: `bool`, `string`, `void`

### 3.5 Forward references

Two-pass design ensures forward references work at file scope:

* Pass 1 collects all top-level declarations into file scope
* Pass 2 resolves all bodies against the fully populated file scope

## 4. Tier B deferrals

Explicitly not included in Task 22:

* Import declaration resolution
* Overload resolution by arity / name mangling
* Static method calls (`Type::method`)
* Generic type parameter declarations / where clauses
* Concept / extend resolution
* Mode / resource block scoping
* Yield statement resolution
* Match arm destructuring bindings (patterns resolved as
  expressions, but no binding introduction)
* Reserved `__dao_` prefix enforcement
* Cross-file / module resolution

## 5. Architecture

### 5.1 Data structures

```
enum SymbolKind:
  Builtin
  Function
  Type
  Local
  Param
  Field
  LambdaParam

class Symbol:
  kind: SymbolKind
  name: string
  decl_node: i64    // node index, -1 for builtins
  scope_id: i64     // scope this symbol was declared in

enum ScopeKind:
  FileScope
  FunctionScope
  BlockScope
  StructScope

class Scope:
  kind: ScopeKind
  parent: i64       // scope index, -1 for file scope
  decls: HashMap<i64>  // name → symbol index
```

### 5.2 Resolver state

```
class RS:
  nodes: Vector<Node>
  idx_data: Vector<i64>
  toks: Vector<Token>
  src: string
  symbols: Vector<Symbol>
  scopes: Vector<Scope>
  uses: Vector<i64>      // flat pairs: [tok_idx, sym_idx, ...]
  diags: Vector<Diagnostic>
  current_scope: i64
```

Threaded immutably through all resolver functions, same pattern as
the parser's `PS`.

### 5.3 Uses table

The uses table maps token indices to resolved symbol indices.
Stored as flat pairs in a `Vector<i64>`:
`[tok_idx_0, sym_idx_0, tok_idx_1, sym_idx_1, ...]`

### 5.4 Token model duplication

Same constraint as Task 21: the single-file bootstrap constraint
requires duplicating TK, Token, Node, PS, PR, and the full
lexer/parser in the resolver file.

This is temporary debt.  Follow-up work should centralize shared
definitions once bootstrap module boundaries stabilize.

### 5.5 File shape

Single file: `bootstrap/resolver/resolver.dao`

The file will contain: token model, lexer, parser, resolver data
structures, resolver logic, test harness, tests.

Estimated size: 3500–4000 lines.

## 6. Resolver API

```
fn resolve(src: string): ResolveResult
```

Where `ResolveResult` contains the resolver state (symbols, scopes,
uses, diagnostics) plus the original parse result for context.

The resolver does not print — all output is data.

## 7. Error handling

* Duplicate top-level declarations → diagnostic
* Unknown identifier in expression → diagnostic
* Other resolution failures → diagnostic with span

Fail-soft: the resolver continues past errors, accumulating
diagnostics rather than aborting.

## 8. Test strategy

### 8.1 Golden resolve tests

* Forward reference: fn calls fn defined later
* Scope nesting: local shadows file-level
* Duplicate declaration
* Unknown name
* For-loop variable scoping
* Lambda parameter scoping
* Builtin type recognition
* Qualified name first-segment resolution
* Class field declarations

### 8.2 Self-resolve smoke test

Parse and resolve a real Dao source file (e.g.,
`examples/bootstrap_probe/expr_parser.dao`).  Verify the resolver
produces symbols, scopes, and acceptable diagnostic count.

## 9. Acceptance criteria

1. A maintained bootstrap resolver exists at `bootstrap/resolver/`.
2. Two-pass resolution: collect declarations, then resolve bodies.
3. Scope chain with declaration maps and parent-linked lookups.
4. Uses table mapping identifiers to resolved symbols.
5. Diagnostics for unknown names and duplicate declarations.
6. Golden tests for representative resolution scenarios.
7. Self-resolve smoke test on real Dao source.
8. Tier A scope and Tier B deferrals documented.
9. One authoritative bootstrap resolver implementation.

## 10. Implementation order

1. Finalize this spec
2. Scaffold file with duplicated token/node/lexer/parser
3. Define Symbol, Scope, RS, ResolveResult types
4. Implement builtin population and file scope creation
5. Implement Pass 1 (collect top-level declarations)
6. Implement Pass 2 (resolve declarations — functions first)
7. Implement statement resolution
8. Implement expression resolution
9. Implement type resolution
10. Add golden tests
11. Add self-resolve smoke test
12. Update bootstrap docs / ARCH_INDEX / roadmap

## 11. Risks

### 11.1 File size

~4000 lines due to duplication.  Acceptable under single-file
constraint but pushes toward the practical limit.

### 11.2 HashMap semantics

`HashMap<i64>` from stdlib is value-semantic.  Every scope.decls
mutation creates a new HashMap.  For small scopes this is fine.

### 11.3 Scope of validation

The resolver validates names exist but does not validate types,
arity, or assignability.  That is the type checker's job.
