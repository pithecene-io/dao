#ifndef DAO_FRONTEND_RESOLVE_RESOLVE_CONTEXT_H
#define DAO_FRONTEND_RESOLVE_RESOLVE_CONTEXT_H

#include "frontend/resolve/scope.h"
#include "frontend/resolve/symbol.h"

#include <memory>
#include <string>
#include <vector>

namespace dao {

// Arena-style owner for all Symbol and Scope objects produced during
// name resolution. Keeps them alive as long as the ResolveResult exists.
class ResolveContext {
public:
  ResolveContext() = default;

  ResolveContext(const ResolveContext&) = delete;
  auto operator=(const ResolveContext&) -> ResolveContext& = delete;
  ResolveContext(ResolveContext&&) noexcept = default;
  auto operator=(ResolveContext&&) noexcept -> ResolveContext& = default;
  ~ResolveContext() = default;

  auto make_symbol(SymbolKind kind,
                   std::string_view name,
                   Span decl_span,
                   const void* decl) -> Symbol* {
    symbols_.push_back(
        std::make_unique<Symbol>(Symbol{.kind = kind, .name = name, .decl_span = decl_span, .decl = decl}));
    return symbols_.back().get();
  }

  auto make_scope(ScopeKind kind, Scope* parent) -> Scope* {
    scopes_.push_back(std::make_unique<Scope>(kind, parent));
    return scopes_.back().get();
  }

  auto symbols() const -> const std::vector<std::unique_ptr<Symbol>>& {
    return symbols_;
  }

  /// Find the innermost scope whose range contains the given offset.
  /// Walks the scope tree from roots, preferring deeper children.
  /// Returns nullptr if no scope contains the offset.
  [[nodiscard]] auto scope_at_offset(uint32_t offset) const -> Scope* {
    // Find root scopes (no parent).
    Scope* result = nullptr;
    for (const auto& scope : scopes_) {
      if (scope->parent() != nullptr) {
        continue;
      }
      auto range = scope->range();
      if (range.length > 0 && offset >= range.offset &&
          offset <= range.offset + range.length) {
        result = scope.get();
        break;
      }
    }

    if (result == nullptr) {
      return nullptr;
    }

    // Walk down children, always preferring the deepest containing child.
    bool found_child = true;
    while (found_child) {
      found_child = false;
      for (auto* child : result->children()) {
        auto range = child->range();
        if (range.length > 0 && offset >= range.offset &&
            offset <= range.offset + range.length) {
          result = child;
          found_child = true;
          break;
        }
      }
    }

    return result;
  }

  /// Intern a string so it outlives any string_view pointing to it.
  /// Returns a stable string_view into owned storage.
  auto intern(std::string str) -> std::string_view {
    strings_.push_back(std::make_unique<std::string>(std::move(str)));
    return *strings_.back();
  }

private:
  std::vector<std::unique_ptr<Symbol>> symbols_;
  std::vector<std::unique_ptr<Scope>> scopes_;
  std::vector<std::unique_ptr<std::string>> strings_;
};

} // namespace dao

#endif // DAO_FRONTEND_RESOLVE_RESOLVE_CONTEXT_H
