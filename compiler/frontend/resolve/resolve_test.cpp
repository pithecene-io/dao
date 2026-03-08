#include "frontend/resolve/resolve.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"

#include <boost/ut.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace boost::ut;
using namespace dao;

namespace {

auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct ResolvedSource {
  SourceBuffer source;
  LexResult lex_result;
  ParseResult parse_result;
  ResolveResult resolve_result;
};

auto resolve_source(const std::string& name, std::string contents) -> ResolvedSource {
  SourceBuffer source(name, std::move(contents));
  auto lex_result = lex(source);
  ParseResult parse_result;
  ResolveResult resolve_result;

  if (lex_result.diagnostics.empty()) {
    parse_result = parse(lex_result.tokens);
    if (parse_result.file != nullptr) {
      resolve_result = resolve(*parse_result.file);
    }
  }

  return {std::move(source),
          std::move(lex_result),
          std::move(parse_result),
          std::move(resolve_result)};
}

auto has_diagnostic_containing(const ResolveResult& result, const std::string& text) -> bool {
  for (const auto& diag : result.diagnostics) {
    if (diag.message.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

auto use_resolves_to(const ResolvedSource& result, uint32_t offset, SymbolKind kind) -> bool {
  auto it = result.resolve_result.uses.find(offset);
  if (it == result.resolve_result.uses.end()) {
    return false;
  }
  return it->second->kind == kind;
}

// Find the offset of a substring in the source.
auto find_offset(const ResolvedSource& result, const std::string& text, size_t nth = 0)
    -> uint32_t {
  auto contents = result.source.contents();
  size_t pos = 0;
  for (size_t i = 0; i <= nth; ++i) {
    pos = contents.find(text, pos);
    if (pos == std::string_view::npos) {
      return UINT32_MAX;
    }
    if (i < nth) {
      pos += text.size();
    }
  }
  return static_cast<uint32_t>(pos);
}

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

suite resolve_basic = [] {
  "simple identifier resolves to param"_test = [] {
    auto result = resolve_source("test", "fn foo(x: int32): int32 -> x");
    expect(result.resolve_result.diagnostics.empty());

    // 'x' at position of the expression body should resolve to Param
    auto x_use_offset = find_offset(result, "x", 1); // second 'x' is the use
    expect(use_resolves_to(result, x_use_offset, SymbolKind::Param));
  };

  "simple identifier resolves to local"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(): int32\n"
                                 "    let value: int32 = 42\n"
                                 "    value");
    expect(result.resolve_result.diagnostics.empty());

    // 'value' on the last line should resolve to Local
    auto val_use_offset = find_offset(result, "value", 1);
    expect(use_resolves_to(result, val_use_offset, SymbolKind::Local));
  };

  "unknown identifier produces diagnostic"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(): int32\n"
                                 "    unknown_var");
    expect(has_diagnostic_containing(result.resolve_result, "unknown name 'unknown_var'"));
  };

  "forward reference to function"_test = [] {
    auto result = resolve_source("test",
                                 "fn caller(): int32 -> callee()\n"
                                 "fn callee(): int32 -> 0");
    expect(result.resolve_result.diagnostics.empty());

    auto callee_offset = find_offset(result, "callee", 0); // first occurrence is the call
    expect(use_resolves_to(result, callee_offset, SymbolKind::Function));
  };
};

suite resolve_scoping = [] {
  "let binding not visible before declaration"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(): int32\n"
                                 "    let a: int32 = b\n"
                                 "    let b: int32 = 0\n"
                                 "    a");
    // 'b' is used before its let declaration — produces unknown name.
    expect(has_diagnostic_containing(result.resolve_result, "unknown name 'b'"));
    auto b_use_offset = find_offset(result, "b", 0); // first 'b' in the initializer
    auto it = result.resolve_result.uses.find(b_use_offset);
    expect(it == result.resolve_result.uses.end()) << "b should not resolve before its declaration";
  };

  "if block creates new scope"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(x: int32): int32\n"
                                 "    if x > 0:\n"
                                 "        let inner: int32 = 1\n"
                                 "        inner\n"
                                 "    x");
    // 'inner' should resolve inside the if block, 'x' outside
    expect(result.resolve_result.diagnostics.empty());
  };

  "for loop variable scoped to body"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(xs: int32): int32\n"
                                 "    for item in xs:\n"
                                 "        item\n"
                                 "    0");
    expect(result.resolve_result.diagnostics.empty());

    // 'item' on the body line should resolve to Local
    auto item_use_offset = find_offset(result, "item", 1);
    expect(use_resolves_to(result, item_use_offset, SymbolKind::Local));
  };

  "lambda param scoped to lambda body"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(x: int32): int32 -> |y| -> y + x");
    expect(result.resolve_result.diagnostics.empty());

    // 'y' in the body should resolve to LambdaParam
    auto y_use_offset = find_offset(result, "y", 1);
    expect(use_resolves_to(result, y_use_offset, SymbolKind::LambdaParam));
  };

  "nested scopes shadow outer"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(x: int32): int32\n"
                                 "    let x: int32 = 42\n"
                                 "    x");
    // The inner 'x' shadows the parameter — no error, resolves to Local
    expect(result.resolve_result.diagnostics.empty());
    auto x_use_offset = find_offset(result, "x", 2); // third 'x' is the use
    expect(use_resolves_to(result, x_use_offset, SymbolKind::Local));
  };
};

suite resolve_duplicates = [] {
  "duplicate top-level functions"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(): int32 -> 0\n"
                                 "fn foo(): int32 -> 1");
    expect(has_diagnostic_containing(result.resolve_result, "duplicate top-level declaration 'foo'"));
  };

  "duplicate parameters"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(x: int32, x: int32): int32 -> x");
    expect(has_diagnostic_containing(result.resolve_result, "duplicate parameter 'x'"));
  };

  "duplicate let in same scope"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(): int32\n"
                                 "    let a: int32 = 1\n"
                                 "    let a: int32 = 2\n"
                                 "    a");
    expect(has_diagnostic_containing(result.resolve_result, "duplicate declaration 'a'"));
  };
};

suite resolve_types = [] {
  "builtin type resolves"_test = [] {
    auto result = resolve_source("test", "fn foo(x: int32): int32 -> x");
    expect(result.resolve_result.diagnostics.empty());

    // int32 in param type should resolve to Builtin
    // The first 'int32' in "x: int32" — find its offset
    auto int32_offset = find_offset(result, "int32", 0);
    expect(use_resolves_to(result, int32_offset, SymbolKind::Builtin));
  };

  "unknown nominal type does NOT produce diagnostic"_test = [] {
    auto result = resolve_source("test", "fn foo(x: NodeId): int32 -> 0");
    // NodeId is unknown but type-position references are not diagnosed
    expect(result.resolve_result.diagnostics.empty());
  };

  "user-declared type resolves"_test = [] {
    auto result = resolve_source("test",
                                 "struct Point:\n"
                                 "    let x: int32\n"
                                 "    let y: int32\n"
                                 "fn foo(p: Point): int32 -> 0");
    expect(result.resolve_result.diagnostics.empty());

    auto point_offset = find_offset(result, "Point", 1); // second 'Point' is the type use
    expect(use_resolves_to(result, point_offset, SymbolKind::Type));
  };
};

suite resolve_imports = [] {
  "import binds last segment"_test = [] {
    auto result = resolve_source("test",
                                 "import net::http\n"
                                 "fn foo(): int32 -> 0");
    expect(result.resolve_result.diagnostics.empty());

    // 'http' should be declared as a Module symbol
    // We can verify it doesn't produce diagnostics and is in the uses table
    // when referenced
  };

  "qualified name first segment resolves to module"_test = [] {
    auto result = resolve_source("test",
                                 "import net::http\n"
                                 "fn foo(): int32\n"
                                 "    http::get()");
    expect(result.resolve_result.diagnostics.empty());

    // 'http' in 'http::get()' should resolve to Module
    auto http_offset = find_offset(result, "http", 1); // second 'http' is the use
    expect(use_resolves_to(result, http_offset, SymbolKind::Module));
  };

  "unresolved first segment of qualified name produces diagnostic"_test = [] {
    auto result = resolve_source("test",
                                 "fn foo(): int32\n"
                                 "    unknown::get()");
    expect(has_diagnostic_containing(result.resolve_result, "unknown name 'unknown'"));
  };
};

suite resolve_struct = [] {
  "struct fields declared"_test = [] {
    auto result = resolve_source("test",
                                 "struct Point:\n"
                                 "    let x: int32\n"
                                 "    let y: int32");
    expect(result.resolve_result.diagnostics.empty());
  };

  "duplicate struct field"_test = [] {
    auto result = resolve_source("test",
                                 "struct Point:\n"
                                 "    let x: int32\n"
                                 "    let x: int32");
    expect(has_diagnostic_containing(result.resolve_result, "duplicate declaration 'x'"));
  };
};

suite resolve_corpus = [] {
  "all examples resolve without spurious diagnostics"_test = [] {
    std::filesystem::path root(DAO_SOURCE_DIR);
    auto examples_dir = root / "examples";

    for (const auto& entry : std::filesystem::directory_iterator(examples_dir)) {
      if (entry.path().extension() != ".dao") {
        continue;
      }

      auto contents = read_file(entry.path());
      auto result = resolve_source(entry.path().filename().string(), std::move(contents));

      // No value-position diagnostics should fire on example files.
      for (const auto& diag : result.resolve_result.diagnostics) {
        // Print diagnostic for debugging if it fires.
        expect(false) << entry.path().filename().string() << ": " << diag.message;
      }
    }
  };

  "all syntax probes resolve without spurious diagnostics"_test = [] {
    std::filesystem::path root(DAO_SOURCE_DIR);
    auto probes_dir = root / "spec" / "syntax_probes";

    for (const auto& entry : std::filesystem::directory_iterator(probes_dir)) {
      if (entry.path().extension() != ".dao") {
        continue;
      }

      auto contents = read_file(entry.path());
      auto result = resolve_source(entry.path().filename().string(), std::move(contents));

      for (const auto& diag : result.resolve_result.diagnostics) {
        expect(false) << entry.path().filename().string() << ": " << diag.message;
      }
    }
  };
};

// NOLINTEND(readability-magic-numbers)

auto main() -> int {
}
