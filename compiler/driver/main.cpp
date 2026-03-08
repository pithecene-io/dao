#include "analysis/semantic_tokens.h"
#include "frontend/ast/ast_printer.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"
#include "frontend/resolve/resolve.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  if (!file) {
    std::cerr << "error: could not open: " << path << "\n";
    std::exit(EXIT_FAILURE);
  }
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Debug-only token dump. Output format is not stable and must not be
// relied upon by tests, tooling, or documentation.
void cmd_lex(const std::filesystem::path& path) {
  auto contents = read_file(path);
  dao::SourceBuffer source(path.filename().string(), std::move(contents));
  auto result = dao::lex(source);

  for (const auto& tok : result.tokens) {
    auto loc = source.line_col(tok.span.offset);
    std::cout << loc.line << ":" << loc.col << " " << dao::token_kind_name(tok.kind);
    if (!tok.text.empty() && tok.kind != dao::TokenKind::Newline &&
        tok.kind != dao::TokenKind::Eof) {
      if (tok.kind == dao::TokenKind::StringLiteral) {
        std::cout << " " << tok.text;
      } else {
        std::cout << " \"" << tok.text << "\"";
      }
    }
    std::cout << "\n";
  }

  if (!result.diagnostics.empty()) {
    for (const auto& diag : result.diagnostics) {
      auto loc = source.line_col(diag.span.offset);
      std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
                << ": error: " << diag.message << "\n";
    }
    std::exit(EXIT_FAILURE);
  }
}

struct LexedFile {
  dao::SourceBuffer source;
  dao::LexResult lex_result;
};

// Lex a file, printing diagnostics on failure.
auto lex_file(const std::filesystem::path& path) -> LexedFile {
  auto contents = read_file(path);
  dao::SourceBuffer source(path.filename().string(), std::move(contents));
  auto lex_result = dao::lex(source);

  if (!lex_result.diagnostics.empty()) {
    for (const auto& diag : lex_result.diagnostics) {
      auto loc = source.line_col(diag.span.offset);
      std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
                << ": error: " << diag.message << "\n";
    }
    std::exit(EXIT_FAILURE);
  }

  return {.source = std::move(source), .lex_result = std::move(lex_result)};
}

struct ParsedFile {
  dao::SourceBuffer source;
  dao::LexResult lex_result;
  dao::ParseResult parse_result;
};

// Lex and parse a file, printing diagnostics on failure.
auto lex_and_parse(const std::filesystem::path& path) -> ParsedFile {
  auto lexed = lex_file(path);
  auto parse_result = dao::parse(lexed.lex_result.tokens);

  if (!parse_result.diagnostics.empty()) {
    for (const auto& diag : parse_result.diagnostics) {
      auto loc = lexed.source.line_col(diag.span.offset);
      std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
                << ": error: " << diag.message << "\n";
    }
    std::exit(EXIT_FAILURE);
  }

  return {.source = std::move(lexed.source),
          .lex_result = std::move(lexed.lex_result),
          .parse_result = std::move(parse_result)};
}

// Debug-only parse diagnostic dump. Output format is not stable.
void cmd_parse(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file != nullptr) {
    std::cout << "File: " << result.parse_result.file->imports().size() << " imports, "
              << result.parse_result.file->declarations().size() << " declarations\n";
  }
}

// Emit semantic token classification. Output is deterministic.
void cmd_tokens(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  auto sem_tokens = dao::classify_tokens(result.lex_result.tokens, result.parse_result.file);

  for (const auto& tok : sem_tokens) {
    auto loc = result.source.line_col(tok.span.offset);
    auto text = result.source.text(tok.span);
    std::cout << loc.line << ":" << loc.col << " " << tok.kind << " " << text << "\n";
  }
}

// Run name resolution and print results.
void cmd_resolve(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file == nullptr) {
    return;
  }

  auto resolve_result = dao::resolve(*result.parse_result.file);

  // Print declared symbols.
  std::cout << "Symbols:\n";
  for (const auto& sym : resolve_result.context.symbols()) {
    std::cout << "  " << dao::symbol_kind_name(sym->kind) << " " << sym->name;
    if (sym->decl_span.length > 0) {
      auto decl_loc = result.source.line_col(sym->decl_span.offset);
      std::cout << " [" << decl_loc.line << ":" << decl_loc.col << "]";
    }
    std::cout << "\n";
  }

  // Print uses (resolved references).
  std::cout << "\nUses:\n";
  for (const auto& [offset, sym] : resolve_result.uses) {
    auto loc = result.source.line_col(offset);
    std::cout << "  " << loc.line << ":" << loc.col << " "
              << result.source.text(dao::Span{.offset = offset,
                                              .length = static_cast<uint32_t>(sym->name.size())})
              << " -> " << dao::symbol_kind_name(sym->kind) << " " << sym->name;
    if (sym->decl_span.length > 0) {
      auto decl_loc = result.source.line_col(sym->decl_span.offset);
      std::cout << " [" << decl_loc.line << ":" << decl_loc.col << "]";
    }
    std::cout << "\n";
  }

  // Print diagnostics.
  if (!resolve_result.diagnostics.empty()) {
    std::cout << "\nDiagnostics:\n";
    for (const auto& diag : resolve_result.diagnostics) {
      auto loc = result.source.line_col(diag.span.offset);
      std::cout << "  " << path.filename().string() << ":" << loc.line << ":" << loc.col
                << ": error: " << diag.message << "\n";
    }
  }
}

// Pretty-print AST. Output is deterministic and suitable for golden-file testing.
void cmd_ast(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file != nullptr) {
    dao::print_ast(std::cout, *result.parse_result.file);
  }
}

} // namespace

auto main(int argc, char* argv[]) -> int {
  if (argc < 2) {
    std::cerr << "usage: daoc <command> <file>\n";
    std::cerr << "commands: lex, parse, ast, tokens, resolve\n";
    return EXIT_FAILURE;
  }

  std::string_view arg1(argv[1]);

  // daoc lex <file>
  if (arg1 == "lex") {
    if (argc < 3) {
      std::cerr << "usage: daoc lex <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path path(argv[2]);
    if (!std::filesystem::exists(path)) {
      std::cerr << "error: file not found: " << path << "\n";
      return EXIT_FAILURE;
    }
    cmd_lex(path);
    return EXIT_SUCCESS;
  }

  // daoc parse <file>
  if (arg1 == "parse") {
    if (argc < 3) {
      std::cerr << "usage: daoc parse <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path parse_path(argv[2]);
    if (!std::filesystem::exists(parse_path)) {
      std::cerr << "error: file not found: " << parse_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_parse(parse_path);
    return EXIT_SUCCESS;
  }

  // daoc tokens <file>
  if (arg1 == "tokens") {
    if (argc < 3) {
      std::cerr << "usage: daoc tokens <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path tokens_path(argv[2]);
    if (!std::filesystem::exists(tokens_path)) {
      std::cerr << "error: file not found: " << tokens_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_tokens(tokens_path);
    return EXIT_SUCCESS;
  }

  // daoc resolve <file>
  if (arg1 == "resolve") {
    if (argc < 3) {
      std::cerr << "usage: daoc resolve <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path resolve_path(argv[2]);
    if (!std::filesystem::exists(resolve_path)) {
      std::cerr << "error: file not found: " << resolve_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_resolve(resolve_path);
    return EXIT_SUCCESS;
  }

  // daoc ast <file>
  if (arg1 == "ast") {
    if (argc < 3) {
      std::cerr << "usage: daoc ast <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path ast_path(argv[2]);
    if (!std::filesystem::exists(ast_path)) {
      std::cerr << "error: file not found: " << ast_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_ast(ast_path);
    return EXIT_SUCCESS;
  }

  // daoc <file> — read and exit (Task 0 compat)
  std::filesystem::path path(arg1);
  if (!std::filesystem::exists(path)) {
    std::cerr << "error: file not found: " << path << "\n";
    return EXIT_FAILURE;
  }
  auto contents = read_file(path);
  return EXIT_SUCCESS;
}
