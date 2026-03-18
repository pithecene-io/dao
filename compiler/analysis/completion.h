#ifndef DAO_ANALYSIS_COMPLETION_H
#define DAO_ANALYSIS_COMPLETION_H

#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/type_checker.h"

#include <string>
#include <vector>

namespace dao {

struct CompletionItem {
  std::string label;
  std::string kind;  // "function", "variable", "type", "parameter", "field", "method"
  std::string type;  // printed type signature
};

/// Query identifier completions at a byte offset in the source.
/// Returns all symbols visible at that position, with type info.
auto query_completions(uint32_t offset,
                        const ResolveResult& resolve,
                        const TypeCheckResult& typed)
    -> std::vector<CompletionItem>;

/// Query dot completions for a receiver type.
/// Returns fields (if struct) and methods (from concept extends).
auto query_dot_completions(const Type* receiver_type,
                            const TypeCheckResult& typed)
    -> std::vector<CompletionItem>;

} // namespace dao

#endif // DAO_ANALYSIS_COMPLETION_H
