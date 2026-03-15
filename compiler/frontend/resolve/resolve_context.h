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
