#ifndef DAO_FRONTEND_AST_AST_PRINTER_H
#define DAO_FRONTEND_AST_AST_PRINTER_H

#include "frontend/ast/ast.h"

#include <ostream>

namespace dao {

/// Print a human-readable, indented AST dump to the given stream.
/// Output is deterministic and suitable for golden-file testing.
void print_ast(std::ostream& out, const FileNode& file);

} // namespace dao

#endif // DAO_FRONTEND_AST_AST_PRINTER_H
