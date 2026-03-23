# Task 19 — Diagnostic Formatter (Phase 7 Entry Leaf)

## Objective

Implement a diagnostic formatter in pure Dao as the first real Phase 7
self-hosting leaf extraction. This forces foundational stdlib
infrastructure (`Span`, `SourceBuffer`, line/column mapping, snippet
extraction) that every subsequent self-hosting subsystem will depend on.

## Motivation

The bootstrap probes (especially `type_checker.dao`) proved that Dao can
express compiler-shaped logic. But they used string-only error messages
with no source location, no context snippets, and no structured
formatting. The diagnostic formatter is the right Phase 7 entry point
because:

- it is a genuine compiler subsystem (not a synthetic probe)
- it is a true leaf — no dependency on scope chains, type universes, or
  module resolution
- it forces exactly the shared substrate needed downstream: spans, source
  buffers, line indexing, string formatting
- it is small enough to finish without dragging in the full frontend

## What already exists in C++

The C++ diagnostic subsystem is minimal and well-understood:

| File | Contents |
|------|----------|
| `compiler/frontend/diagnostics/source.h` | `Span` (offset + length), `LineCol` (line + col), `SourceBuffer` (filename, contents, line index, `text(span)`, `line_col(offset)`) |
| `compiler/frontend/diagnostics/diagnostic.h` | `Severity` enum (Error, Warning, Note), `Diagnostic` struct (severity + span + message) |
| `compiler/driver/main.cpp` lines 51–82 | `print_error_diagnostics` and `print_diagnostics` — iterate diagnostics, resolve span → line:col, format `filename:line:col: severity: message` |

The formatting is currently inlined in the driver as ~30 lines of C++.
There is no context-line extraction, no underline/caret rendering, and no
multi-diagnostic aggregation beyond sequential printing.

## Deliverables

### 1. `stdlib/core/span.dao` — source span types

```dao
struct Span:
  offset: i64
  length: i64

struct LineCol:
  line: i64
  col: i64
```

These are value types. No methods needed initially beyond construction.

### 2. `stdlib/core/source_buffer.dao` — source buffer with line index

```dao
class SourceBuffer:
  filename: string
  contents: string
  line_offsets: Vector<i64>
```

Required methods:

- `make_source_buffer(filename: string, contents: string): SourceBuffer`
  — constructor that builds the line offset index
- `text(buf: SourceBuffer, span: Span): string` — extract span text
- `line_col(buf: SourceBuffer, offset: i64): LineCol` — binary search
  for line containing offset, compute column
- `line_text(buf: SourceBuffer, line: i64): string` — extract full
  source line by 1-based line number
- `line_count(buf: SourceBuffer): i64` — total line count

### 3. `stdlib/core/diagnostic.dao` — diagnostic types

```dao
enum Severity:
  Error
  Warning
  Note

struct Diagnostic:
  severity: Severity
  span: Span
  message: string
```

Constructor helpers:

- `make_error(span: Span, message: string): Diagnostic`
- `make_warning(span: Span, message: string): Diagnostic`

### 4. `examples/bootstrap_probe/diagnostic_formatter.dao` — formatter

The formatter consumes a `SourceBuffer` and a `Vector<Diagnostic>` and
produces formatted output. Target output format:

```
test.dao:3:5: error: undefined variable 'x'
  3 | let y = x + 1
      ^
```

Required capabilities:

- resolve span → line:col via `SourceBuffer`
- extract context line from source
- render `filename:line:col: severity: message`
- render context line with line number gutter
- render caret/underline marker at the error column
- handle multiple diagnostics sequentially
- handle prelude line offset (diagnostics from user code that follows
  a prepended stdlib prelude)

### 5. Test harness

The probe must include a self-test harness (same pattern as
`type_checker.dao`) that:

- constructs a SourceBuffer from an inline source string
- creates diagnostics at known offsets
- formats them and verifies the output contains expected substrings
- tests edge cases: first line, last line, empty source, multi-byte
  offsets, multiple diagnostics on the same line

## Stdlib dependencies

| Dependency | Status | Notes |
|------------|--------|-------|
| `Vector<T>` | ✅ exists | for line_offsets and diagnostic lists |
| `string` ops | ✅ exists | concat, length, char_at, substring |
| `i64` | ✅ exists | all offsets and indices |
| `Span` / `LineCol` | ❌ new | deliverable of this task |
| `SourceBuffer` | ❌ new | deliverable of this task |
| `Diagnostic` / `Severity` | ❌ new | deliverable of this task |
| `i64_to_string` | ✅ exists | for line number rendering |
| `HashMap<V>` | not needed | — |
| `Option<T>` | maybe | for bounds-checked access |

## Language features exercised

This task stress-tests:

- **struct construction and field access** — Span, LineCol, Diagnostic
- **class with methods** — SourceBuffer
- **enum matching** — Severity dispatch
- **Vector iteration** — line offset scanning
- **string building** — concat chains for formatted output
- **binary search** — line_col implementation
- **i64 arithmetic** — offset/length computation

## What this task deliberately avoids

- No scope chains or symbol tables
- No type universe or type checking logic
- No parsing or AST construction
- No module/import system
- No mutable references or unsafe memory
- No generic type parameters beyond existing Vector<T>

## Expected learnings

This task will surface real answers to:

1. **Are string concat chains sufficient for formatting, or do we need a
   string builder?** — If concat-per-segment is too slow or ergonomically
   painful, that's a concrete stdlib gap to fix.
2. **Is char_at + substring sufficient for source text slicing?** — The
   line index builder needs character-level scanning. If the current
   string API is awkward for this, that identifies the next string
   primitive to add.
3. **Does the struct/class split work for value-vs-entity types?** —
   Span and LineCol are pure values; SourceBuffer holds state. This
   exercises both patterns.
4. **What is the next real extraction candidate?** — After diagnostics,
   the next leaf should be whatever this task reveals as painful.

## Exit criteria

- `daoc build examples/bootstrap_probe/diagnostic_formatter.dao`
  compiles and runs successfully
- formatter produces correct `filename:line:col: severity: message`
  output with context lines and caret markers
- self-test harness passes all cases
- `Span`, `LineCol`, `SourceBuffer`, `Diagnostic`, `Severity` are
  usable as stdlib types for future subsystem extractions
- learnings documented: string builder needed? new string primitives?
  struct/class friction?

## Sequencing

This is the **first** Phase 7 task. It must complete before attempting
extraction of resolver, parser, or type-checker subsystems, because
those all depend on the diagnostic infrastructure this task establishes.

Prerequisites:
- None beyond current main. All required language features and stdlib
  types already exist.

Unlocks:
- Phase 7 resolver leaf extraction (needs Span + Diagnostic for error
  reporting)
- Phase 7 parser leaf extraction (needs SourceBuffer for source
  management)
- Future diagnostic improvements (multi-span notes, fix suggestions)
