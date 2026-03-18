#ifndef DAO_ANALYSIS_COMPLETION_H
#define DAO_ANALYSIS_COMPLETION_H

#include "frontend/diagnostics/source.h"
#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/type_checker.h"

#include <string>
#include <vector>

namespace dao {

struct CompletionItem {
  std::string label;
  std::string kind;  // "function", "variable", "type", "parameter", "field", etc.
  std::string type;  // printed type signature
};

/// Query completions at a byte offset in the source.
/// Returns all symbols visible at that position, with type info.
auto query_completions(uint32_t offset,
                        const ResolveResult& resolve,
                        const TypeCheckResult& typed)
    -> std::vector<CompletionItem>;

} // namespace dao

#endif // DAO_ANALYSIS_COMPLETION_H
