# Dao Language Vision

Dao aims for:
- Rust-lite memory flexibility without lifetime syntax
- ML-inspired pipelines and concise lambdas
- Swift-like clarity and visual elegance
- Julia-adjacent seriousness about numeric and GPU workloads

This file is explanatory only.
Normative decisions live under `docs/contracts/`.

## Design Doctrine

Dao grows by strengthening general mechanisms, not by accumulating
special forms. Before adding a keyword, sigil, or language-level
construct, ask whether an existing mechanism can absorb the need
through compiler recognition or library design.

Principles:
- Minimize keywords and syntactic sigils
- Prefer compiler pattern recognition over syntax promotion
- New constructs must justify themselves against composition of existing ones
- "Special form" is a cost; generality is a feature

## Standard Library Posture

**Small standard library, large standard platform.**

The `stdlib` ships only foundational types and low-level primitives that
the compiler or runtime depend on. Everything above that threshold lives
in first-party packages maintained alongside the compiler but versioned
and distributed independently.

### stdlib (ships with compiler)

Core types, numeric primitives, IO, and minimal runtime support. This is
the floor — programs that import nothing beyond `stdlib` must still
compile and link. Foundational modules include `core`, `io`, and
`numerics`.

### First-party packages (batteries-included, separately versioned)

Dao provides a curated set of first-party packages covering common needs:
- `net` — sockets, DNS, low-level networking
- `net::http` — HTTP client/server
- `sync` — concurrency primitives
- `time` — clocks, durations, formatting
- `json` — serialization
- `web` — routing, middleware, templating
- `db` — connection pooling, SQL, query building
- `test` — test runner, assertions, benchmarking
- `obs` — logging, metrics, tracing

These are official but not compiler-coupled. They follow the same module
conventions as third-party code. A package graduating from first-party
to stdlib (or being removed) is a semver event.

### Early dogfooding targets

A strong web framework and wire protocol support are priority
dogfooding surfaces. Getting `net::http`, `web`, and `json` to
production quality early exercises the full stack — stdlib primitives,
first-party package conventions, module system, error handling, and
concurrency — under real workload pressure. If Dao cannot build a
competitive web server from its own libraries, the platform story is
not credible.

## Module and Namespace System

- `::` is the namespace/module path separator
- `.` is for runtime member access on values
- these two are not interchangeable

Modules use hierarchical `::` paths that map to logical identities, not
repository URLs or filesystem paths. Import examples:

```dao
import net::http
import db::sql
```

Visibility tiers (not yet frozen):
- module-private (default)
- package-visible
- public

The module system distinguishes packages, modules, and workspaces as
separate concepts with explicit boundaries.

## GPU and Accelerator Support

GPU workloads are expressed through the existing `mode` and `resource`
constructs. There is no special GPU syntax.

```dao
fn vector_add(a: Tensor, b: Tensor): Tensor
    resource memory result =>
        let out = Tensor::zeros(a.shape)
    mode gpu =>
        for i in range(a.len):
            out[i] = a[i] + b[i]
    out
```

The compiler recognizes kernel-eligible functions from structural
patterns (mode entry, resource binding, loop shape) and lowers them
to GPU targets. This is compiler recognition, not syntax promotion.

Key constraints:
- No `@kernel` annotations or GPU-specific keywords
- `mode gpu =>` is the same `mode` construct used for `unsafe`, `parallel`, etc.
- `resource memory =>` is the same `resource` construct used for CPU allocation domains
- GPU is a deployment target for existing language semantics, not a separate sublanguage
