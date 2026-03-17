#include "analysis/goto_definition.h"

namespace dao {

auto query_definition(uint32_t offset, const ResolveResult& resolve)
    -> std::optional<DefinitionLocation> {
  const Symbol* sym = nullptr;

  auto use_it = resolve.uses.find(offset);
  if (use_it != resolve.uses.end()) {
    sym = use_it->second;
  }

  if (sym == nullptr) {
    return std::nullopt;
  }

  // Must have a valid declaration span.
  if (sym->decl_span.length == 0) {
    return std::nullopt;
  }

  return DefinitionLocation{sym->decl_span.offset, sym->decl_span.length};
}

} // namespace dao
