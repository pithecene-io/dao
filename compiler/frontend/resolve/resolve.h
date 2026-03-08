#ifndef DAO_FRONTEND_RESOLVE_RESOLVE_H
#define DAO_FRONTEND_RESOLVE_RESOLVE_H

#include "frontend/ast/ast.h"
#include "frontend/diagnostics/diagnostic.h"
#include "frontend/resolve/resolve_context.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dao {

struct ResolveResult {
  ResolveContext context;
  std::unordered_map<uint32_t, Symbol*> uses; // token span offset -> resolved Symbol*
  std::vector<Diagnostic> diagnostics;
};

// Run name resolution over a parsed file.
// The FileNode must remain valid for the lifetime of the returned result
// (symbol name string_views point into the AST's source buffer).
auto resolve(const FileNode& file) -> ResolveResult;

} // namespace dao

#endif // DAO_FRONTEND_RESOLVE_RESOLVE_H
