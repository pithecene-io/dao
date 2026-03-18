#ifndef DAO_ANALYSIS_DOCUMENT_SYMBOLS_H
#define DAO_ANALYSIS_DOCUMENT_SYMBOLS_H

#include "frontend/ast/ast.h"
#include "frontend/diagnostics/source.h"

#include <string>
#include <string_view>
#include <vector>

namespace dao {

struct DocumentSymbol {
  std::string name;
  std::string kind; // "function", "class", "field", "concept", "alias"
  Span span;        // full declaration span
  Span name_span;   // name identifier span
  std::vector<DocumentSymbol> children;
};

/// Collect document symbols from the AST.
/// Returns a hierarchical tree of declarations with their children
/// (e.g. class fields, concept methods).
auto query_document_symbols(const FileNode& file,
                             uint32_t prelude_bytes)
    -> std::vector<DocumentSymbol>;

} // namespace dao

#endif // DAO_ANALYSIS_DOCUMENT_SYMBOLS_H
