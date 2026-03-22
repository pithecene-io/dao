#ifndef DAO_FRONTEND_LEXER_TOKEN_H
#define DAO_FRONTEND_LEXER_TOKEN_H

#include "frontend/diagnostics/source.h"

#include <cstdint>
#include <string_view>

namespace dao {

// Token kinds map directly to the lexical surface in spec/grammar/dao.lex.
// Fine-grained keyword kinds support trivial mapping to the semantic token
// taxonomy in CONTRACT_LANGUAGE_TOOLING.md.
enum class TokenKind : std::uint8_t {
  // Keywords — control
  KwImport,
  KwExtern,
  KwFn,
  KwClass,
  KwEnum,
  KwType,
  KwLet,
  KwIf,
  KwElse,
  KwWhile,
  KwFor,
  KwIn,
  KwReturn,
  KwYield,
  KwBreak,
  KwMatch,

  // Keywords — execution / resource constructs
  KwMode,
  KwResource,

  // Keywords — literals
  KwTrue,
  KwFalse,

  // Keywords — logical operators
  KwAnd,
  KwOr,

  // Keywords — concepts and conformance
  KwConcept,
  KwDerived,
  KwAs,
  KwExtend,
  KwDeny,
  KwSelf,
  KwWhere,

  // Operators
  Colon,      // :
  ColonColon, // ::
  Arrow,      // ->
  FatArrow,   // =>
  Eq,         // =
  EqEq,       // ==
  BangEq,     // !=
  Lt,         // <
  LtEq,       // <=
  Gt,         // >
  GtEq,       // >=
  Plus,       // +
  Minus,      // -
  Star,       // *
  Slash,      // /
  Percent,    // %
  Amp,        // &
  Bang,       // !
  Dot,        // .
  Comma,      // ,
  Pipe,       // |
  PipeGt,     // |>
  Question,   // ?

  // Delimiters
  LParen,   // (
  RParen,   // )
  LBracket, // [
  RBracket, // ]

  // Literals
  IntLiteral,
  FloatLiteral,
  StringLiteral,

  // Identifier
  Identifier,

  // Synthetic
  Newline,
  Indent,
  Dedent,
  Eof,

  // Error recovery
  Error,
};

struct Token {
  TokenKind kind;
  Span span;
  std::string_view text;
};

auto token_kind_name(TokenKind kind) -> const char*;

} // namespace dao

#endif // DAO_FRONTEND_LEXER_TOKEN_H
