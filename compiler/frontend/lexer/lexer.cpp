#include "frontend/lexer/lexer.h"

#include <cctype>

namespace dao {

auto token_kind_name(TokenKind kind) -> const char* {
  switch (kind) {
  case TokenKind::KwImport:
    return "KwImport";
  case TokenKind::KwExtern:
    return "KwExtern";
  case TokenKind::KwFn:
    return "KwFn";
  case TokenKind::KwStruct:
    return "KwStruct";
  case TokenKind::KwType:
    return "KwType";
  case TokenKind::KwLet:
    return "KwLet";
  case TokenKind::KwIf:
    return "KwIf";
  case TokenKind::KwElse:
    return "KwElse";
  case TokenKind::KwWhile:
    return "KwWhile";
  case TokenKind::KwFor:
    return "KwFor";
  case TokenKind::KwIn:
    return "KwIn";
  case TokenKind::KwReturn:
    return "KwReturn";
  case TokenKind::KwMode:
    return "KwMode";
  case TokenKind::KwResource:
    return "KwResource";
  case TokenKind::KwTrue:
    return "KwTrue";
  case TokenKind::KwFalse:
    return "KwFalse";
  case TokenKind::KwAnd:
    return "KwAnd";
  case TokenKind::KwOr:
    return "KwOr";
  case TokenKind::Colon:
    return "Colon";
  case TokenKind::ColonColon:
    return "ColonColon";
  case TokenKind::Arrow:
    return "Arrow";
  case TokenKind::FatArrow:
    return "FatArrow";
  case TokenKind::Eq:
    return "Eq";
  case TokenKind::EqEq:
    return "EqEq";
  case TokenKind::BangEq:
    return "BangEq";
  case TokenKind::Lt:
    return "Lt";
  case TokenKind::LtEq:
    return "LtEq";
  case TokenKind::Gt:
    return "Gt";
  case TokenKind::GtEq:
    return "GtEq";
  case TokenKind::Plus:
    return "Plus";
  case TokenKind::Minus:
    return "Minus";
  case TokenKind::Star:
    return "Star";
  case TokenKind::Slash:
    return "Slash";
  case TokenKind::Percent:
    return "Percent";
  case TokenKind::Amp:
    return "Amp";
  case TokenKind::Bang:
    return "Bang";
  case TokenKind::Dot:
    return "Dot";
  case TokenKind::Comma:
    return "Comma";
  case TokenKind::Pipe:
    return "Pipe";
  case TokenKind::PipeGt:
    return "PipeGt";
  case TokenKind::LParen:
    return "LParen";
  case TokenKind::RParen:
    return "RParen";
  case TokenKind::LBracket:
    return "LBracket";
  case TokenKind::RBracket:
    return "RBracket";
  case TokenKind::IntLiteral:
    return "IntLiteral";
  case TokenKind::FloatLiteral:
    return "FloatLiteral";
  case TokenKind::StringLiteral:
    return "StringLiteral";
  case TokenKind::Identifier:
    return "Identifier";
  case TokenKind::Newline:
    return "Newline";
  case TokenKind::Indent:
    return "Indent";
  case TokenKind::Dedent:
    return "Dedent";
  case TokenKind::Eof:
    return "Eof";
  case TokenKind::Error:
    return "Error";
  }
  return "Unknown";
}

namespace {

class LexerImpl {
public:
  explicit LexerImpl(const SourceBuffer& source) : source_(source), src_(source.contents()) {
  }

  auto run() -> LexResult {
    while (pos_ < src_.size()) {
      if (at_line_start_) {
        handle_line_start();
        continue;
      }

      skip_horizontal_whitespace();

      if (pos_ >= src_.size()) {
        break;
      }

      char cur = src_[pos_];

      if (cur == '\n') {
        handle_newline();
        continue;
      }

      if (cur == '\r') {
        ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '\n') {
          ++pos_;
        }
        handle_newline_emit();
        continue;
      }

      lex_token();
    }

    // Emit remaining DEDENTs.
    while (indent_stack_.size() > 1) {
      emit(TokenKind::Dedent, pos_, 0);
      indent_stack_.pop_back();
    }

    emit(TokenKind::Eof, pos_, 0);

    return {.tokens = std::move(tokens_), .diagnostics = std::move(diagnostics_)};
  }

private:
  const SourceBuffer& source_;
  std::string_view src_;
  uint32_t pos_ = 0;
  std::vector<uint32_t> indent_stack_ = {0};
  uint32_t paren_depth_ = 0;
  bool at_line_start_ = true;
  std::vector<Token> tokens_;
  std::vector<Diagnostic> diagnostics_;

  void emit(TokenKind kind, uint32_t offset, uint32_t length) {
    tokens_.push_back({.kind = kind,
                       .span = {.offset = offset, .length = length},
                       .text = src_.substr(offset, length)});
  }

  void emit_error(uint32_t offset, uint32_t length, std::string message) {
    emit(TokenKind::Error, offset, length);
    diagnostics_.push_back(
        Diagnostic::error({.offset = offset, .length = length}, std::move(message)));
  }

  [[nodiscard]] auto peek() const -> char {
    if (pos_ < src_.size()) {
      return src_[pos_];
    }
    return '\0';
  }

  [[nodiscard]] auto peek_at(uint32_t offset) const -> char {
    if (offset < src_.size()) {
      return src_[offset];
    }
    return '\0';
  }

  auto advance() -> char {
    char cur = src_[pos_];
    ++pos_;
    return cur;
  }

  void skip_horizontal_whitespace() {
    while (pos_ < src_.size() && src_[pos_] == ' ') {
      ++pos_;
    }
  }

  void handle_newline() {
    ++pos_;
    handle_newline_emit();
  }

  void handle_newline_emit() {
    if (paren_depth_ == 0) {
      emit(TokenKind::Newline, pos_ - 1, 1);
    }
    at_line_start_ = true;
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void handle_line_start() {
    // Count leading spaces. Tabs are illegal.
    uint32_t line_begin = pos_;
    uint32_t spaces = 0;
    while (pos_ < src_.size()) {
      if (src_[pos_] == ' ') {
        ++spaces;
        ++pos_;
      } else if (src_[pos_] == '\t') {
        emit_error(pos_, 1, "tabs are not allowed in Dao source");
        ++pos_;
        ++spaces; // treat tab as one space for recovery
      } else {
        break;
      }
    }

    // Blank line — skip without affecting indentation.
    if (pos_ >= src_.size() || src_[pos_] == '\n' || src_[pos_] == '\r') {
      at_line_start_ = false;
      if (pos_ < src_.size()) {
        // Consume the newline but don't emit NEWLINE for blank lines
        // (previous line's NEWLINE already emitted).
        if (src_[pos_] == '\r') {
          ++pos_;
          if (pos_ < src_.size() && src_[pos_] == '\n') {
            ++pos_;
          }
        } else {
          ++pos_;
        }
        at_line_start_ = true;
      }
      return;
    }

    at_line_start_ = false;

    // Inside parentheses/brackets, indentation is ignored.
    if (paren_depth_ > 0) {
      return;
    }

    uint32_t current_indent = indent_stack_.back();

    if (spaces > current_indent) {
      indent_stack_.push_back(spaces);
      emit(TokenKind::Indent, line_begin, spaces);
    } else if (spaces < current_indent) {
      while (indent_stack_.size() > 1 && indent_stack_.back() > spaces) {
        indent_stack_.pop_back();
        emit(TokenKind::Dedent, line_begin, 0);
      }
      if (indent_stack_.back() != spaces) {
        emit_error(line_begin, spaces, "inconsistent indentation");
      }
    }
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void lex_token() {
    uint32_t start = pos_;
    char cur = advance();

    switch (cur) {
    case ':':
      if (peek() == ':') {
        ++pos_;
        emit(TokenKind::ColonColon, start, 2);
      } else {
        emit(TokenKind::Colon, start, 1);
      }
      return;
    case '+':
      emit(TokenKind::Plus, start, 1);
      return;
    case '%':
      emit(TokenKind::Percent, start, 1);
      return;
    case '&':
      emit(TokenKind::Amp, start, 1);
      return;
    case '.':
      emit(TokenKind::Dot, start, 1);
      return;
    case ',':
      emit(TokenKind::Comma, start, 1);
      return;

    case '(':
      emit(TokenKind::LParen, start, 1);
      ++paren_depth_;
      return;
    case ')':
      emit(TokenKind::RParen, start, 1);
      if (paren_depth_ > 0) {
        --paren_depth_;
      }
      return;
    case '[':
      emit(TokenKind::LBracket, start, 1);
      ++paren_depth_;
      return;
    case ']':
      emit(TokenKind::RBracket, start, 1);
      if (paren_depth_ > 0) {
        --paren_depth_;
      }
      return;

    case '-':
      if (peek() == '>') {
        ++pos_;
        emit(TokenKind::Arrow, start, 2);
      } else {
        emit(TokenKind::Minus, start, 1);
      }
      return;

    case '=':
      if (peek() == '=') {
        ++pos_;
        emit(TokenKind::EqEq, start, 2);
      } else if (peek() == '>') {
        ++pos_;
        emit(TokenKind::FatArrow, start, 2);
      } else {
        emit(TokenKind::Eq, start, 1);
      }
      return;

    case '!':
      if (peek() == '=') {
        ++pos_;
        emit(TokenKind::BangEq, start, 2);
      } else {
        emit(TokenKind::Bang, start, 1);
      }
      return;

    case '<':
      if (peek() == '=') {
        ++pos_;
        emit(TokenKind::LtEq, start, 2);
      } else {
        emit(TokenKind::Lt, start, 1);
      }
      return;

    case '>':
      if (peek() == '=') {
        ++pos_;
        emit(TokenKind::GtEq, start, 2);
      } else {
        emit(TokenKind::Gt, start, 1);
      }
      return;

    case '|':
      if (peek() == '>') {
        ++pos_;
        emit(TokenKind::PipeGt, start, 2);
      } else {
        emit(TokenKind::Pipe, start, 1);
      }
      return;

    case '*':
      emit(TokenKind::Star, start, 1);
      return;
    case '/':
      emit(TokenKind::Slash, start, 1);
      return;

    case '"':
      lex_string(start);
      return;

    default:
      break;
    }

    // Numbers
    if (std::isdigit(static_cast<unsigned char>(cur)) != 0) {
      lex_number(start);
      return;
    }

    // Identifiers and keywords
    if (cur == '_' || std::isalpha(static_cast<unsigned char>(cur)) != 0) {
      lex_identifier(start);
      return;
    }

    emit_error(start, 1, std::string("unexpected character: ") + cur);
  }

  void lex_string(uint32_t start) {
    // start is the position of the opening quote, already consumed.
    while (pos_ < src_.size()) {
      char cur = src_[pos_];
      if (cur == '"') {
        ++pos_;
        emit(TokenKind::StringLiteral, start, pos_ - start);
        return;
      }
      if (cur == '\\') {
        ++pos_; // skip escaped character
        if (pos_ < src_.size()) {
          ++pos_;
        }
        continue;
      }
      if (cur == '\n') {
        break;
      }
      ++pos_;
    }
    emit_error(start, pos_ - start, "unterminated string literal");
  }

  void lex_number(uint32_t start) {
    // First digit already consumed via advance().
    while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_])) != 0) {
      ++pos_;
    }

    bool is_float = false;

    // Check for decimal point followed by a digit.
    if (pos_ < src_.size() && src_[pos_] == '.' && pos_ + 1 < src_.size() &&
        std::isdigit(static_cast<unsigned char>(src_[pos_ + 1])) != 0) {
      is_float = true;
      ++pos_; // consume '.'
      while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_])) != 0) {
        ++pos_;
      }
    }

    emit(is_float ? TokenKind::FloatLiteral : TokenKind::IntLiteral, start, pos_ - start);
  }

  void lex_identifier(uint32_t start) {
    // First character already consumed.
    while (pos_ < src_.size()) {
      char cur = src_[pos_];
      if (cur == '_' || std::isalnum(static_cast<unsigned char>(cur)) != 0) {
        ++pos_;
      } else {
        break;
      }
    }

    std::string_view word = src_.substr(start, pos_ - start);
    TokenKind kind = classify_keyword(word);
    emit(kind, start, pos_ - start);
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  static auto classify_keyword(std::string_view word) -> TokenKind {
    if (word == "import") {
      return TokenKind::KwImport;
    }
    if (word == "extern") {
      return TokenKind::KwExtern;
    }
    if (word == "fn") {
      return TokenKind::KwFn;
    }
    if (word == "struct") {
      return TokenKind::KwStruct;
    }
    if (word == "type") {
      return TokenKind::KwType;
    }
    if (word == "let") {
      return TokenKind::KwLet;
    }
    if (word == "if") {
      return TokenKind::KwIf;
    }
    if (word == "else") {
      return TokenKind::KwElse;
    }
    if (word == "while") {
      return TokenKind::KwWhile;
    }
    if (word == "for") {
      return TokenKind::KwFor;
    }
    if (word == "in") {
      return TokenKind::KwIn;
    }
    if (word == "return") {
      return TokenKind::KwReturn;
    }
    if (word == "mode") {
      return TokenKind::KwMode;
    }
    if (word == "resource") {
      return TokenKind::KwResource;
    }
    if (word == "true") {
      return TokenKind::KwTrue;
    }
    if (word == "false") {
      return TokenKind::KwFalse;
    }
    if (word == "and") {
      return TokenKind::KwAnd;
    }
    if (word == "or") {
      return TokenKind::KwOr;
    }
    return TokenKind::Identifier;
  }
};

} // namespace

auto lex(const SourceBuffer& source) -> LexResult {
  LexerImpl lexer(source);
  return lexer.run();
}

} // namespace dao
