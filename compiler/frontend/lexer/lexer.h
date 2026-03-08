#ifndef DAO_FRONTEND_LEXER_LEXER_H
#define DAO_FRONTEND_LEXER_LEXER_H

#include "frontend/diagnostics/diagnostic.h"
#include "frontend/diagnostics/source.h"
#include "frontend/lexer/token.h"

#include <vector>

namespace dao {

struct LexResult {
  std::vector<Token> tokens;
  std::vector<Diagnostic> diagnostics;
};

auto lex(const SourceBuffer& source) -> LexResult;

} // namespace dao

#endif // DAO_FRONTEND_LEXER_LEXER_H
