#ifndef DAO_ANALYSIS_SEMANTIC_TOKENS_H
#define DAO_ANALYSIS_SEMANTIC_TOKENS_H

#include "frontend/ast/ast.h"
#include "frontend/diagnostics/source.h"
#include "frontend/lexer/token.h"

#include <string_view>
#include <vector>

namespace dao {

/// A single semantic token with its category from the frozen taxonomy
/// in CONTRACT_LANGUAGE_TOOLING.md.
struct SemanticToken {
  Span span;
  std::string_view kind; // e.g. "keyword.fn", "decl.function", "type.builtin"
};

/// Classify all tokens in a source file into semantic categories.
///
/// This combines lexical classification (keywords, literals, operators)
/// with structural classification from the AST (declarations, types,
/// parameters, mode/resource names). Structural classifications take
/// priority over lexical ones for the same span.
///
/// Tokens that cannot be classified at this stage (e.g. identifiers
/// whose role depends on name resolution) are omitted from the result.
///
/// The returned vector is sorted by span offset.
auto classify_tokens(const std::vector<Token>& tokens, const FileNode* file)
    -> std::vector<SemanticToken>;

} // namespace dao

#endif // DAO_ANALYSIS_SEMANTIC_TOKENS_H
