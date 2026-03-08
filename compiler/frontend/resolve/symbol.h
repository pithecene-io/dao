#ifndef DAO_FRONTEND_RESOLVE_SYMBOL_H
#define DAO_FRONTEND_RESOLVE_SYMBOL_H

#include "frontend/ast/ast.h"
#include "frontend/diagnostics/source.h"

#include <cstdint>
#include <string_view>

namespace dao {

enum class SymbolKind : std::uint8_t {
  Function,    // top-level fn
  Type,        // struct or alias
  Param,       // function parameter
  Local,       // let binding or for-loop variable
  Field,       // struct member
  Module,      // import binding
  Builtin,     // built-in type (int32, float64, etc.)
  LambdaParam, // lambda |x| parameter
};

auto symbol_kind_name(SymbolKind kind) -> const char*;

struct Symbol {
  SymbolKind kind;
  std::string_view name;
  Span decl_span;          // where the symbol was declared (zero for builtins)
  const AstNode* decl;     // declaration node (nullptr for builtins)
};

} // namespace dao

#endif // DAO_FRONTEND_RESOLVE_SYMBOL_H
