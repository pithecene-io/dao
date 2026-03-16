#ifndef DAO_ANALYSIS_HOVER_H
#define DAO_ANALYSIS_HOVER_H

#include "frontend/diagnostics/source.h"
#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/type_checker.h"

#include <optional>
#include <string>

namespace dao {

struct HoverResult {
  std::string type;        // e.g. "(a: i32, b: i32): i32"
  std::string symbol_kind; // e.g. "function", "variable", "type"
  std::string name;        // e.g. "add"
};

/// Query hover info at a byte offset in the source.
/// Returns nullopt if no symbol is found at that offset.
auto query_hover(uint32_t offset, const ResolveResult& resolve,
                 const TypeCheckResult& typed)
    -> std::optional<HoverResult>;

} // namespace dao

#endif // DAO_ANALYSIS_HOVER_H
