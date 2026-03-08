#include "frontend/ast/ast_printer.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"

#define BOOST_UT_DISABLE_MODULE
#include <boost/ut.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace boost::ut;
using namespace dao;

auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

auto ast_string(const std::filesystem::path& source_path) -> std::string {
  auto contents = read_file(source_path);
  SourceBuffer source(source_path.filename().string(), contents);
  auto lex_result = lex(source);
  auto parse_result = parse(lex_result.tokens);

  std::ostringstream out;
  if (parse_result.file != nullptr) {
    print_ast(out, *parse_result.file);
  }
  return out.str();
}

// NOLINTBEGIN(readability-function-cognitive-complexity,modernize-use-trailing-return-type)

suite ast_golden_tests = [] {
  "examples match golden files"_test = [] {
    std::filesystem::path root(DAO_SOURCE_DIR);
    auto golden_dir = root / "testdata" / "ast";

    for (const auto& entry : std::filesystem::directory_iterator(root / "examples")) {
      if (entry.path().extension() != ".dao") {
        continue;
      }
      auto golden_path = golden_dir / ("examples_" + entry.path().stem().string() + ".ast");
      if (!std::filesystem::exists(golden_path)) {
        continue;
      }

      auto actual = ast_string(entry.path());
      auto expected = read_file(golden_path);
      expect(actual == expected) << "AST mismatch for " << entry.path().filename().string();
    }
  };

  "syntax probes match golden files"_test = [] {
    std::filesystem::path root(DAO_SOURCE_DIR);
    auto golden_dir = root / "testdata" / "ast";

    for (const auto& entry :
         std::filesystem::directory_iterator(root / "spec" / "syntax_probes")) {
      if (entry.path().extension() != ".dao") {
        continue;
      }
      auto golden_path = golden_dir / ("probes_" + entry.path().stem().string() + ".ast");
      if (!std::filesystem::exists(golden_path)) {
        continue;
      }

      auto actual = ast_string(entry.path());
      auto expected = read_file(golden_path);
      expect(actual == expected) << "AST mismatch for " << entry.path().filename().string();
    }
  };
};

// NOLINTEND(readability-function-cognitive-complexity,modernize-use-trailing-return-type)

} // namespace

auto main() -> int {
} // NOLINT(readability-named-parameter)
