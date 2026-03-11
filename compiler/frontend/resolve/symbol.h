#ifndef DAO_FRONTEND_RESOLVE_SYMBOL_H
#define DAO_FRONTEND_RESOLVE_SYMBOL_H

#include "frontend/diagnostics/source.h"

#include <cassert>
#include <cstdint>
#include <string_view>

namespace dao {

// Forward declarations for typed decl accessors.
struct Decl;
struct Stmt;
struct Expr;
struct ImportNode;
struct FieldSpec;

enum class SymbolKind : std::uint8_t {
  Function,    // top-level fn
  Type,        // struct or alias
  Param,       // function parameter
  Local,       // let binding or for-loop variable
  Field,       // struct member
  Module,      // import binding
  Builtin,      // built-in scalar type (i32, f64, bool)
  Predeclared,  // compiler-known predeclared named type (string, void)
  LambdaParam,  // lambda |x| parameter
};

auto symbol_kind_name(SymbolKind kind) -> const char*;

struct Symbol {
  SymbolKind kind;
  std::string_view name;
  Span decl_span;            // where the symbol was declared (zero for builtins)
  const void* decl;          // declaration node (nullptr for builtins)

  // --- Typed accessors --------------------------------------------------
  // Each accessor asserts the SymbolKind matches the expected pointer type,
  // concentrating the void* → typed* cast at a single checked choke point.

  [[nodiscard]] auto decl_as_decl() const -> const Decl* {
    assert((kind == SymbolKind::Function || kind == SymbolKind::Type ||
            kind == SymbolKind::Param) &&
           "decl_as_decl requires Function, Type, or Param symbol");
    return static_cast<const Decl*>(decl);
  }

  [[nodiscard]] auto decl_as_stmt() const -> const Stmt* {
    assert(kind == SymbolKind::Local &&
           "decl_as_stmt requires Local symbol");
    return static_cast<const Stmt*>(decl);
  }

  [[nodiscard]] auto decl_as_expr() const -> const Expr* {
    assert(kind == SymbolKind::LambdaParam &&
           "decl_as_expr requires LambdaParam symbol");
    return static_cast<const Expr*>(decl);
  }

  [[nodiscard]] auto decl_as_import() const -> const ImportNode* {
    assert(kind == SymbolKind::Module &&
           "decl_as_import requires Module symbol");
    return static_cast<const ImportNode*>(decl);
  }

  [[nodiscard]] auto decl_as_field() const -> const FieldSpec* {
    assert(kind == SymbolKind::Field &&
           "decl_as_field requires Field symbol");
    return static_cast<const FieldSpec*>(decl);
  }
};

} // namespace dao

#endif // DAO_FRONTEND_RESOLVE_SYMBOL_H
