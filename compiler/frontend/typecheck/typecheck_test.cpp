#include "frontend/diagnostics/source.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"
#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/type_checker.h"
#include "frontend/types/type_context.h"
#include "frontend/types/type_printer.h"

#include <boost/ut.hpp>
#include <string>

using namespace boost::ut;
using namespace dao;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

/// Parse, resolve, and typecheck a source string. Returns TypeCheckResult.
auto check_source(const std::string& source) -> TypeCheckResult {
  SourceBuffer buf("test.dao", std::string(source));
  auto lex_result = lex(buf);
  auto parse_result = parse(lex_result.tokens);
  auto resolve_result = resolve(*parse_result.file);

  TypeContext types;
  return typecheck(*parse_result.file, resolve_result, types);
}

/// Returns true if any diagnostic message contains the substring.
auto has_error_containing(const TypeCheckResult& result, std::string_view sub)
    -> bool {
  for (const auto& diag : result.diagnostics) {
    if (diag.severity == Severity::Error &&
        diag.message.find(sub) != std::string::npos) {
      return true;
    }
  }
  return false;
}

/// Returns true if there are no type-check errors.
auto is_ok(const TypeCheckResult& result) -> bool {
  for (const auto& diag : result.diagnostics) {
    if (diag.severity == Severity::Error) {
      return false;
    }
  }
  return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Positive: literals and let bindings
// ---------------------------------------------------------------------------

suite typecheck_literals = [] {
  "int literal types as i32"_test = [] {
    auto result = check_source("fn main(): i32\n    return 42\n");
    expect(is_ok(result)) << "should typecheck cleanly";
  };

  "float literal types as f64"_test = [] {
    auto result = check_source("fn main(): f64\n    return 3.14\n");
    expect(is_ok(result)) << "should typecheck cleanly";
  };

  "bool literal types as bool"_test = [] {
    auto result = check_source("fn main(): bool\n    return true\n");
    expect(is_ok(result)) << "should typecheck cleanly";
  };

  "string literal types as string"_test = [] {
    auto result = check_source("fn main(): string\n    return \"hello\"\n");
    expect(is_ok(result)) << "should typecheck cleanly";
  };

  "let with type annotation"_test = [] {
    auto result = check_source(
        "fn main(): i32\n"
        "    let x: i32 = 10\n"
        "    return x\n");
    expect(is_ok(result)) << "typed let should work";
  };

  "let with inferred type"_test = [] {
    auto result = check_source(
        "fn main(): i32\n"
        "    let x = 10\n"
        "    return x\n");
    expect(is_ok(result)) << "inferred let should work";
  };
};

// ---------------------------------------------------------------------------
// Positive: arithmetic
// ---------------------------------------------------------------------------

suite typecheck_arithmetic = [] {
  "i32 addition"_test = [] {
    auto result = check_source("fn add(a: i32, b: i32): i32 -> a + b\n");
    expect(is_ok(result));
  };

  "f64 multiplication"_test = [] {
    auto result = check_source("fn mul(a: f64, b: f64): f64 -> a * b\n");
    expect(is_ok(result));
  };

  "comparison yields bool"_test = [] {
    auto result = check_source("fn lt(a: i32, b: i32): bool -> a < b\n");
    expect(is_ok(result));
  };

  "equality yields bool"_test = [] {
    auto result = check_source("fn eq(a: i32, b: i32): bool -> a == b\n");
    expect(is_ok(result));
  };

  "logical and"_test = [] {
    auto result = check_source("fn both(a: bool, b: bool): bool -> a and b\n");
    expect(is_ok(result));
  };
};

// ---------------------------------------------------------------------------
// Positive: function calls
// ---------------------------------------------------------------------------

suite typecheck_calls = [] {
  "simple function call"_test = [] {
    auto result = check_source(
        "fn double(x: i32): i32 -> x + x\n"
        "\n"
        "fn main(): i32 -> double(5)\n");
    expect(is_ok(result));
  };
};

// ---------------------------------------------------------------------------
// Positive: expression-bodied functions
// ---------------------------------------------------------------------------

suite typecheck_expr_body = [] {
  "expression-bodied function"_test = [] {
    auto result = check_source("fn inc(x: i32): i32 -> x + 1\n");
    expect(is_ok(result));
  };
};

// ---------------------------------------------------------------------------
// Positive: if/while with bool conditions
// ---------------------------------------------------------------------------

suite typecheck_control_flow = [] {
  "if with bool condition"_test = [] {
    auto result = check_source(
        "fn abs(x: i32): i32\n"
        "    if x > 0:\n"
        "        return x\n"
        "    0 - x\n");
    expect(is_ok(result));
  };

  "while with bool condition"_test = [] {
    auto result = check_source(
        "fn countdown(n: i32): i32\n"
        "    let x = n\n"
        "    while x > 0:\n"
        "        x = x - 1\n"
        "    x\n");
    expect(is_ok(result));
  };
};

// ---------------------------------------------------------------------------
// Positive: void functions
// ---------------------------------------------------------------------------

suite typecheck_void = [] {
  // NOTE: bare "return" (without expression) is not yet supported by the
  // parser. When it is, add:
  //   "void function with bare return" — should typecheck cleanly
  //   "bare return in non-void function" — should error

  "void function without explicit return"_test = [] {
    auto result = check_source("fn noop(): void\n    let x = 1\n");
    expect(is_ok(result));
  };
};

// ---------------------------------------------------------------------------
// Positive: pointer operations
// ---------------------------------------------------------------------------

suite typecheck_pointers = [] {
  "address-of and deref in unsafe"_test = [] {
    auto result = check_source(
        "fn ptr_test(x: i32): i32\n"
        "    let p: *i32 = &x\n"
        "    mode unsafe =>\n"
        "        *p\n");
    expect(is_ok(result));
  };
};

// ---------------------------------------------------------------------------
// Negative: type mismatches
// ---------------------------------------------------------------------------

suite typecheck_negative = [] {
  "let type mismatch"_test = [] {
    auto result = check_source(
        "fn main(): i32\n"
        "    let x: i32 = true\n"
        "    x\n");
    expect(has_error_containing(result, "not assignable"));
  };

  "return type mismatch"_test = [] {
    auto result = check_source("fn main(): i32\n    return true\n");
    expect(has_error_containing(result, "does not match"));
  };

  "non-bool condition"_test = [] {
    auto result = check_source(
        "fn test(): i32\n"
        "    if 42:\n"
        "        return 1\n"
        "    0\n");
    expect(has_error_containing(result, "must be 'bool'"));
  };

  "mixed arithmetic types"_test = [] {
    auto result = check_source("fn bad(a: i32, b: f64): i32 -> a + b\n");
    expect(has_error_containing(result, "mismatched types"));
  };

  "wrong arg count"_test = [] {
    auto result = check_source(
        "fn one(x: i32): i32 -> x\n"
        "\n"
        "fn main(): i32 -> one(1, 2)\n");
    expect(has_error_containing(result, "expected 1 argument"));
  };

  "wrong arg type"_test = [] {
    auto result = check_source(
        "fn takes_int(x: i32): i32 -> x\n"
        "\n"
        "fn main(): i32 -> takes_int(true)\n");
    expect(has_error_containing(result, "not assignable"));
  };

  "deref non-pointer"_test = [] {
    auto result = check_source(
        "fn bad(x: i32): i32\n"
        "    mode unsafe =>\n"
        "        *x\n");
    expect(has_error_containing(result, "non-pointer"));
  };

  "deref outside unsafe"_test = [] {
    auto result = check_source(
        "fn bad(p: *i32): i32\n"
        "    *p\n");
    expect(has_error_containing(result, "mode unsafe"));
  };

  // "bare return in non-void function" — deferred: parser does not yet
  // support bare return.

  "logical not on non-bool"_test = [] {
    auto result = check_source("fn bad(): bool -> !42\n");
    expect(has_error_containing(result, "requires 'bool'"));
  };

  "assignment type mismatch"_test = [] {
    auto result = check_source(
        "fn bad(): i32\n"
        "    let x: i32 = 1\n"
        "    x = true\n"
        "    x\n");
    expect(has_error_containing(result, "cannot assign"));
  };

  "expression-body type mismatch"_test = [] {
    auto result = check_source("fn bad(): i32 -> true\n");
    expect(has_error_containing(result, "does not match return type"));
  };
};

// NOLINTEND(readability-magic-numbers)

auto main() -> int {} // NOLINT(readability-named-parameter)
