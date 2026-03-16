#ifndef DAO_ANALYSIS_GOTO_DEFINITION_H
#define DAO_ANALYSIS_GOTO_DEFINITION_H

#include "frontend/diagnostics/source.h"
#include "frontend/resolve/resolve.h"

#include <optional>

namespace dao {

struct DefinitionLocation {
  uint32_t offset;
  uint32_t length;
};

/// Find the declaration site for the symbol at the given use-site offset.
/// Returns nullopt if no symbol is found or it has no declaration span.
auto query_definition(uint32_t offset, const ResolveResult& resolve)
    -> std::optional<DefinitionLocation>;

} // namespace dao

#endif // DAO_ANALYSIS_GOTO_DEFINITION_H
