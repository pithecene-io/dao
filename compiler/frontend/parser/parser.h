#ifndef DAO_FRONTEND_PARSER_PARSER_H
#define DAO_FRONTEND_PARSER_PARSER_H

#include "frontend/ast/ast.h"
#include "frontend/diagnostics/diagnostic.h"
#include "frontend/lexer/token.h"

#include <vector>

namespace dao {

struct ParseResult {
  AstContext context;
  FileNode* file = nullptr;
  std::vector<Diagnostic> diagnostics;
};

auto parse(const std::vector<Token>& tokens) -> ParseResult;

} // namespace dao

#endif // DAO_FRONTEND_PARSER_PARSER_H
