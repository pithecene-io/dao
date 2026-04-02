#!/usr/bin/env bash
# assemble.sh — Concatenate shared base with subsystem sources.
#
# Run from repository root:
#   bash bootstrap/assemble.sh
#
# Produces *.gen.dao (compiled by daoc) from:
#   bootstrap/shared/base.dao  (token model, lexer, AST, parser)
#   bootstrap/<subsystem>/*.dao  (subsystem-specific source)

set -euo pipefail
# Work from repo root. Prefer git if available, fall back to script location.
if command -v git &>/dev/null && git rev-parse --show-toplevel &>/dev/null; then
  cd "$(git rev-parse --show-toplevel)"
else
  cd "$(dirname "$0")/.."
fi

SHARED=bootstrap/shared/base.dao

assemble() {
  local out="$1"
  shift
  echo "// GENERATED — do not edit. Edit bootstrap/shared/base.dao or" > "$out"
  echo "// the subsystem source instead, then run: bash bootstrap/assemble.sh" >> "$out"
  echo "" >> "$out"
  cat "$SHARED" >> "$out"
  for src in "$@"; do
    echo "" >> "$out"
    cat "$src" >> "$out"
  done
  echo "  assembled $out"
}

assemble bootstrap/lexer/lexer.gen.dao \
  bootstrap/lexer/tests.dao

assemble bootstrap/parser/parser.gen.dao \
  bootstrap/parser/tests.dao

assemble bootstrap/graph/graph.gen.dao \
  bootstrap/graph/tests.dao

assemble bootstrap/resolver/resolver.gen.dao \
  bootstrap/resolver/impl.dao

# Type checker: include resolver library (everything before BEGIN_RESOLVER_TESTS).
RESOLVER_LIB=$(mktemp)
sed '/^\/\/ BEGIN_RESOLVER_TESTS/,$d' bootstrap/resolver/impl.dao > "$RESOLVER_LIB"
assemble bootstrap/typecheck/typecheck.gen.dao \
  "$RESOLVER_LIB" \
  bootstrap/typecheck/impl.dao
rm -f "$RESOLVER_LIB"

# HIR: include resolver library + typecheck library (before their test markers).
RESOLVER_LIB2=$(mktemp)
sed '/^\/\/ BEGIN_RESOLVER_TESTS/,$d' bootstrap/resolver/impl.dao > "$RESOLVER_LIB2"
TYPECHECK_LIB=$(mktemp)
sed '/^\/\/ BEGIN_TYPECHECK_TESTS/,$d' bootstrap/typecheck/impl.dao > "$TYPECHECK_LIB"
assemble bootstrap/hir/hir.gen.dao \
  "$RESOLVER_LIB2" \
  "$TYPECHECK_LIB" \
  bootstrap/hir/impl.dao
rm -f "$RESOLVER_LIB2" "$TYPECHECK_LIB"

echo "done — 6 files assembled"
