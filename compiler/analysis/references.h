#ifndef DAO_ANALYSIS_REFERENCES_H
#define DAO_ANALYSIS_REFERENCES_H

#include "frontend/diagnostics/source.h"
#include "frontend/resolve/resolve.h"

#include <vector>

namespace dao {

struct ReferenceLocation {
  Span span;
  bool is_definition; // true for declaration site, false for use site
};

/// Find all references to the symbol at the given byte offset.
/// Returns the declaration site (if any) plus all use sites.
auto query_references(uint32_t offset, const ResolveResult& resolve)
    -> std::vector<ReferenceLocation>;

} // namespace dao

#endif // DAO_ANALYSIS_REFERENCES_H
