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

# Strip a leading `module <path>` line (plus blank/comment lines before
# it) from a file on stdout. Used when concatenating real Dao files
# that each carry their own module declaration — the generated output
# file gets exactly one synthetic module declaration at the top.
strip_module() {
  awk '
    BEGIN { found = 0 }
    {
      if (!found) {
        if ($0 ~ /^[[:space:]]*$/) { next }
        if ($0 ~ /^[[:space:]]*\/\//) { next }
        if ($0 ~ /^[[:space:]]*#/) { next }
        if ($0 ~ /^[[:space:]]*module[[:space:]]/) { found = 1; next }
        found = 1
      }
      print
    }
  ' "$1"
}

assemble() {
  local out="$1"
  shift
  local gen_module="${out##*/}"
  gen_module="${gen_module%.gen.dao}"
  # Generated files declare a single synthetic module identity
  # (`bootstrap::<name>::gen`). Per-file module declarations from the
  # concatenated inputs are stripped by strip_module.
  echo "module bootstrap::${gen_module}::gen" > "$out"
  echo "// GENERATED — do not edit. Edit bootstrap/shared/base.dao or" >> "$out"
  echo "// the subsystem source instead, then run: bash bootstrap/assemble.sh" >> "$out"
  echo "" >> "$out"
  strip_module "$SHARED" >> "$out"
  for src in "$@"; do
    echo "" >> "$out"
    strip_module "$src" >> "$out"
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

# MIR: include resolver + typecheck + hir libs (before their test markers).
RESOLVER_LIB3=$(mktemp)
sed '/^\/\/ BEGIN_RESOLVER_TESTS/,$d' bootstrap/resolver/impl.dao > "$RESOLVER_LIB3"
TYPECHECK_LIB2=$(mktemp)
sed '/^\/\/ BEGIN_TYPECHECK_TESTS/,$d' bootstrap/typecheck/impl.dao > "$TYPECHECK_LIB2"
HIR_LIB=$(mktemp)
sed '/^\/\/ BEGIN_HIR_TESTS/,$d' bootstrap/hir/impl.dao > "$HIR_LIB"
assemble bootstrap/mir/mir.gen.dao \
  "$RESOLVER_LIB3" \
  "$TYPECHECK_LIB2" \
  "$HIR_LIB" \
  bootstrap/mir/impl.dao
rm -f "$RESOLVER_LIB3" "$TYPECHECK_LIB2" "$HIR_LIB"

echo "done — 7 files assembled"
