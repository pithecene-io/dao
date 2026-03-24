#!/usr/bin/env bash
# assemble.sh — Concatenate shared base with subsystem parts.
#
# Run from repository root:
#   bash bootstrap/assemble.sh
#
# This produces the compilable .dao files from:
#   bootstrap/shared/base.dao  (token model, lexer, AST, parser)
#   bootstrap/<subsystem>/tests.part.dao or impl.part.dao

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

SHARED=bootstrap/shared/base.dao

assemble() {
  local out="$1"
  shift
  echo "// GENERATED — do not edit. Edit bootstrap/shared/base.dao or" > "$out"
  echo "// the .part.dao fragment instead, then run: bash bootstrap/assemble.sh" >> "$out"
  echo "" >> "$out"
  cat "$SHARED" >> "$out"
  for part in "$@"; do
    echo "" >> "$out"
    cat "$part" >> "$out"
  done
  echo "  assembled $out"
}

assemble bootstrap/lexer/lexer.dao \
  bootstrap/lexer/tests.part.dao

assemble bootstrap/parser/parser.dao \
  bootstrap/parser/tests.part.dao

assemble bootstrap/resolver/resolver.dao \
  bootstrap/resolver/impl.part.dao

echo "done — 3 files assembled"
