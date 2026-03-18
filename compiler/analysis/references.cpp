#include "analysis/references.h"

namespace dao {

auto query_references(uint32_t offset, const ResolveResult& resolve)
    -> std::vector<ReferenceLocation> {
  // Find the target symbol — check use-sites first, then declarations.
  const Symbol* target = nullptr;

  auto use_it = resolve.uses.find(offset);
  if (use_it != resolve.uses.end()) {
    target = use_it->second;
  }

  if (target == nullptr) {
    for (const auto& sym : resolve.context.symbols()) {
      if (sym->decl_span.offset == offset && sym->decl_span.length > 0) {
        target = sym.get();
        break;
      }
    }
  }

  if (target == nullptr) {
    return {};
  }

  std::vector<ReferenceLocation> results;

  // Include the declaration site.
  if (target->decl_span.length > 0) {
    results.push_back({.span = target->decl_span, .is_definition = true});
  }

  // Collect all use-sites that resolve to the same symbol.
  for (const auto& [use_offset, sym] : resolve.uses) {
    if (sym == target) {
      results.push_back({
          .span = {.offset = use_offset,
                   .length = static_cast<uint32_t>(sym->name.size())},
          .is_definition = false,
      });
    }
  }

  return results;
}

} // namespace dao
