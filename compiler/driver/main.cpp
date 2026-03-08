#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"

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

// Debug-only parse diagnostic dump. Output format is not stable.
void cmd_parse(const std::filesystem::path& path) {
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

  auto parse_result = dao::parse(lex_result.tokens);

  if (parse_result.file != nullptr) {
    std::cout << "File: " << parse_result.file->imports().size() << " imports, "
              << parse_result.file->declarations().size() << " declarations\n";
  }

  if (!parse_result.diagnostics.empty()) {
    for (const auto& diag : parse_result.diagnostics) {
      auto loc = source.line_col(diag.span.offset);
      std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
                << ": error: " << diag.message << "\n";
    }
    std::exit(EXIT_FAILURE);
  }
}

} // namespace

auto main(int argc, char* argv[]) -> int {
  if (argc < 2) {
    std::cerr << "usage: daoc <command> <file>\n";
    std::cerr << "commands: lex, parse\n";
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

  // daoc <file> — read and exit (Task 0 compat)
  std::filesystem::path path(arg1);
  if (!std::filesystem::exists(path)) {
    std::cerr << "error: file not found: " << path << "\n";
    return EXIT_FAILURE;
  }
  auto contents = read_file(path);
  return EXIT_SUCCESS;
}
