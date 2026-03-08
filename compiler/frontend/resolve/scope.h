#ifndef DAO_FRONTEND_RESOLVE_SCOPE_H
#define DAO_FRONTEND_RESOLVE_SCOPE_H

#include "frontend/resolve/symbol.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace dao {

enum class ScopeKind : std::uint8_t {
  File,     // top-level file scope
  Function, // function body
  Block,    // if/while/for/mode/resource body
  Struct,   // struct member scope
};

class Scope {
public:
  Scope(ScopeKind kind, Scope* parent) : kind_(kind), parent_(parent) {
  }

  [[nodiscard]] auto kind() const -> ScopeKind {
    return kind_;
  }
  [[nodiscard]] auto parent() const -> Scope* {
    return parent_;
  }

  // Look up a name in this scope only (no parent traversal).
  [[nodiscard]] auto lookup_local(std::string_view name) const -> Symbol* {
    auto it = declarations_.find(name);
    if (it != declarations_.end()) {
      return it->second;
    }
    return nullptr;
  }

  // Look up a name in the scope chain (this scope, then parent, etc.).
  [[nodiscard]] auto lookup(std::string_view name) const -> Symbol* {
    auto* sym = lookup_local(name);
    if (sym != nullptr) {
      return sym;
    }
    if (parent_ != nullptr) {
      return parent_->lookup(name);
    }
    return nullptr;
  }

  // Declare a symbol in this scope. Returns false if duplicate.
  auto declare(std::string_view name, Symbol* sym) -> bool {
    auto [it, inserted] = declarations_.try_emplace(name, sym);
    return inserted;
  }

private:
  ScopeKind kind_;
  Scope* parent_;
  std::unordered_map<std::string_view, Symbol*> declarations_;
};

} // namespace dao

#endif // DAO_FRONTEND_RESOLVE_SCOPE_H
