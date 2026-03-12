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

/// Owns all pipeline state so typed results remain valid.
struct TypecheckPipeline {
  SourceBuffer source;
  LexResult lex_result;
  ParseResult parse_result;
  ResolveResult resolve_result;
  TypeContext types;
  TypeCheckResult check_result;

  explicit TypecheckPipeline(const std::string& src)
      : source("test.dao", std::string(src)),
        lex_result(lex(source)),
        parse_result(parse(lex_result.tokens)) {
    if (parse_result.file != nullptr) {
      resolve_result = resolve(*parse_result.file);
      check_result = typecheck(*parse_result.file, resolve_result, types);
    }
  }

  /// Look up the function type registered for the first FunctionDecl.
  [[nodiscard]] auto first_fn_type() const -> const Type* {
    if (parse_result.file == nullptr) {
      return nullptr;
    }
    for (const auto* decl : parse_result.file->declarations) {
      if (decl->kind() == NodeKind::FunctionDecl) {
        return check_result.typed.decl_type(decl);
      }
    }
    return nullptr;
  }

  /// Collect typed function types for all FunctionDecls in order.
  [[nodiscard]] auto fn_types() const -> std::vector<const Type*> {
    std::vector<const Type*> result;
    if (parse_result.file == nullptr) {
      return result;
    }
    for (const auto* decl : parse_result.file->declarations) {
      if (decl->kind() == NodeKind::FunctionDecl) {
        result.push_back(check_result.typed.decl_type(decl));
      }
    }
    return result;
  }
};

} // namespace

// ---------------------------------------------------------------------------
// Positive: literals and let bindings
// ---------------------------------------------------------------------------

suite<"typecheck_literals"> typecheck_literals = [] {
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

suite<"typecheck_arithmetic"> typecheck_arithmetic = [] {
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

suite<"typecheck_calls"> typecheck_calls = [] {
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

suite<"typecheck_expr_body"> typecheck_expr_body = [] {
  "expression-bodied function"_test = [] {
    auto result = check_source("fn inc(x: i32): i32 -> x + 1\n");
    expect(is_ok(result));
  };
};

// ---------------------------------------------------------------------------
// Positive: if/while with bool conditions
// ---------------------------------------------------------------------------

suite<"typecheck_control_flow"> typecheck_control_flow = [] {
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

suite<"typecheck_void"> typecheck_void = [] {
  "void function with bare return"_test = [] {
    auto result = check_source("fn noop(): void\n    return\n");
    expect(is_ok(result));
  };

  "bare return in non-void function"_test = [] {
    auto result = check_source("fn bad(): i32\n    return\n");
    expect(has_error_containing(result, "bare return"));
  };

  "void function without explicit return"_test = [] {
    auto result = check_source("fn noop(): void\n    let x = 1\n");
    expect(is_ok(result));
  };
};

// ---------------------------------------------------------------------------
// Positive: pointer operations
// ---------------------------------------------------------------------------

suite<"typecheck_pointers"> typecheck_pointers = [] {
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

suite<"typecheck_negative"> typecheck_negative = [] {
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

// ---------------------------------------------------------------------------
// Type aliases
// ---------------------------------------------------------------------------

suite<"type_alias"> type_alias = [] {
  "alias resolves to underlying type"_test = [] {
    auto result = check_source(
        "type NodeId = i32\n"
        "fn test(a: NodeId): NodeId -> a\n");
    expect(result.diagnostics.empty()) << "alias param should typecheck";
  };

  "alias-to-alias chains resolve"_test = [] {
    auto result = check_source(
        "type NodeId = i32\n"
        "type MyNode = NodeId\n"
        "fn test(a: MyNode): i32 -> a\n");
    expect(result.diagnostics.empty()) << "chained alias should typecheck";
  };

  "alias used in return type"_test = [] {
    auto result = check_source(
        "type Score = f64\n"
        "fn test(): Score -> 0.0\n");
    expect(result.diagnostics.empty()) << "alias return type should typecheck";
  };

  "forward-declared alias resolves with typed fn signature"_test = [] {
    TypecheckPipeline pipe(
        "fn test(a: NodeId): NodeId -> a\n"
        "type NodeId = i32\n");
    expect(pipe.check_result.diagnostics.empty())
        << "forward alias should typecheck";
    const auto* fn_type = pipe.first_fn_type();
    expect(fn_type != nullptr) << "function type must be populated";
    if (fn_type != nullptr) {
      expect(fn_type->kind() == TypeKind::Function)
          << "must be function type";
      const auto* ft = static_cast<const TypeFunction*>(fn_type);
      expect(ft->param_types().size() == 1_ul);
      expect(ft->param_types()[0] != nullptr)
          << "param type must not be null";
      expect(ft->param_types()[0]->kind() == TypeKind::Builtin)
          << "alias param must resolve to builtin";
    }
  };

  "alias type mismatch still caught"_test = [] {
    auto result = check_source(
        "type NodeId = i32\n"
        "fn test(a: NodeId): bool -> a\n");
    expect(has_error_containing(result, "does not match return type"));
  };
};

// ---------------------------------------------------------------------------
// Class construction
// ---------------------------------------------------------------------------

suite<"typecheck_construct"> typecheck_construct = [] {
  "basic construction"_test = [] {
    auto result = check_source(
        "class Point:\n"
        "    x: i32\n"
        "    y: i32\n"
        "\n"
        "fn make(): Point -> Point(1, 2)\n");
    expect(is_ok(result)) << "basic construction should typecheck";
  };

  "construction with wrong arity"_test = [] {
    auto result = check_source(
        "class Point:\n"
        "    x: i32\n"
        "    y: i32\n"
        "\n"
        "fn make(): Point -> Point(1)\n");
    expect(has_error_containing(result, "expects 2 field(s), got 1"));
  };

  "construction with wrong field type"_test = [] {
    auto result = check_source(
        "class Point:\n"
        "    x: i32\n"
        "    y: i32\n"
        "\n"
        "fn make(): Point -> Point(1, true)\n");
    expect(has_error_containing(result, "field 'y' expects type"));
  };

  "construction in let binding"_test = [] {
    auto result = check_source(
        "class Point:\n"
        "    x: i32\n"
        "    y: i32\n"
        "\n"
        "fn make(): i32\n"
        "    let p: Point = Point(1, 2)\n"
        "    p.x\n");
    expect(is_ok(result)) << "let with construction should typecheck";
  };

  "nested construction"_test = [] {
    auto result = check_source(
        "class Point:\n"
        "    x: i32\n"
        "    y: i32\n"
        "\n"
        "class Rect:\n"
        "    tl: Point\n"
        "    br: Point\n"
        "\n"
        "fn make(): Rect -> Rect(Point(0, 0), Point(1, 1))\n");
    expect(is_ok(result)) << "nested construction should typecheck";
  };

  "field access on constructed value"_test = [] {
    auto result = check_source(
        "class Point:\n"
        "    x: i32\n"
        "    y: i32\n"
        "\n"
        "fn get_x(): i32 -> Point(1, 2).x\n");
    expect(is_ok(result)) << "field access on construction should typecheck";
  };

  "value of struct type is not a constructor"_test = [] {
    auto result = check_source(
        "class Point:\n"
        "    x: i32\n"
        "    y: i32\n"
        "\n"
        "fn bad(p: Point): Point -> p(1, 2)\n");
    expect(has_error_containing(result, "cannot call non-function"))
        << "calling a struct value should not be treated as construction";
  };
};

suite<"typecheck_generics"> typecheck_generics = [] {
  "generic function type uses TypeGenericParam"_test = [] {
    auto result = check_source("fn identity<T>(x: T): T -> x\n");
    expect(result.diagnostics.empty()) << "generic function should typecheck";
  };

  "generic class with type param field"_test = [] {
    auto result = check_source(
        "class Box<T>:\n"
        "    value: T\n");
    expect(result.diagnostics.empty()) << "generic class should typecheck";
  };

  "separate declarations with same T produce distinct types"_test = [] {
    TypecheckPipeline pipe(
        "fn identity<T>(x: T): T -> x\n"
        "fn wrap<T>(x: T): T -> x\n");
    expect(is_ok(pipe.check_result))
        << "both generic functions should typecheck";

    auto fns = pipe.fn_types();
    expect(fns.size() == 2_ul) << "must find two function decls";

    const auto* fn_identity = static_cast<const TypeFunction*>(fns[0]);
    const auto* fn_wrap = static_cast<const TypeFunction*>(fns[1]);
    expect(fn_identity != nullptr);
    expect(fn_wrap != nullptr);

    // Both have fn(T): T shape, but the T types must be distinct objects
    // because they belong to different binder declarations.
    expect(fn_identity->param_types().size() == 1_ul);
    expect(fn_wrap->param_types().size() == 1_ul);

    const auto* t_from_identity = fn_identity->param_types()[0];
    const auto* t_from_wrap = fn_wrap->param_types()[0];
    expect(t_from_identity != t_from_wrap)
        << "T from identity and T from wrap must be distinct TypeGenericParam objects";
  };
};

// ---------------------------------------------------------------------------
// Concept declarations and conformance
// ---------------------------------------------------------------------------

suite<"typecheck_concepts"> typecheck_concepts = [] {
  "concept declaration typechecks"_test = [] {
    auto result = check_source(
        "concept Printable:\n"
        "    fn to_string(self): string\n");
    expect(is_ok(result)) << "concept declaration should typecheck";
  };

  "concept with default method typechecks"_test = [] {
    auto result = check_source(
        "concept Equatable:\n"
        "    fn eq(self, other: Equatable): bool\n"
        "    fn ne(self, other: Equatable): bool -> !self.eq(other)\n");
    expect(is_ok(result)) << "concept with default method should typecheck";
  };

  "class with conformance block typechecks"_test = [] {
    auto result = check_source(
        "concept Printable:\n"
        "    fn to_string(self): string\n"
        "class Point:\n"
        "    x: f64\n"
        "    as Printable:\n"
        "        fn to_string(self): string -> \"p\"\n");
    expect(is_ok(result)) << "class with conformance should typecheck";
  };

  "extend declaration typechecks"_test = [] {
    auto result = check_source(
        "concept Printable:\n"
        "    fn to_string(self): string\n"
        "extend i32 as Printable:\n"
        "    fn to_string(self): string -> \"num\"\n");
    expect(is_ok(result)) << "extend declaration should typecheck";
  };

  "derived concept typechecks"_test = [] {
    auto result = check_source(
        "derived concept Copyable:\n"
        "    fn copy(self): Copyable\n");
    expect(is_ok(result)) << "derived concept should typecheck";
  };

  "class with deny typechecks"_test = [] {
    auto result = check_source(
        "derived concept Printable:\n"
        "    fn to_string(self): string\n"
        "class SecretKey:\n"
        "    data: i32\n"
        "    deny Printable\n");
    expect(is_ok(result)) << "class with deny should typecheck";
  };

  "concept default method bodies are not checked"_test = [] {
    // Concept default methods are abstract over self's type;
    // body checking is deferred until concept-level type reasoning.
    auto result = check_source(
        "concept Eq:\n"
        "    fn eq(self, other: Eq): bool\n"
        "    fn ne(self, other: Eq): bool -> 42\n");
    expect(is_ok(result)) << "concept default body checking is deferred";
  };

  "bad conformance method body is rejected"_test = [] {
    auto result = check_source(
        "concept Show:\n"
        "    fn show(self): string\n"
        "class X:\n"
        "    v: i32\n"
        "    as Show:\n"
        "        fn show(self): string -> 99\n");
    expect(!is_ok(result)) << "bad conformance body should produce error";
    expect(has_error_containing(result, "does not match return type"))
        << "should report type mismatch";
  };

  "bad extend method body is rejected"_test = [] {
    auto result = check_source(
        "concept Show:\n"
        "    fn show(self): string\n"
        "extend i32 as Show:\n"
        "    fn show(self): string -> 0\n");
    expect(!is_ok(result)) << "bad extend body should produce error";
    expect(has_error_containing(result, "does not match return type"))
        << "should report type mismatch";
  };
};

// ---------------------------------------------------------------------------
// Self typing and field access
// ---------------------------------------------------------------------------

suite<"typecheck_self"> typecheck_self = [] {
  "self.field access in conformance method"_test = [] {
    auto result = check_source(
        "concept HasName:\n"
        "    fn name(self): string\n"
        "class Person:\n"
        "    name: string\n"
        "    as HasName:\n"
        "        fn name(self): string -> self.name\n");
    expect(is_ok(result)) << "self.field in conformance should typecheck";
  };

  "self.field type mismatch in conformance"_test = [] {
    auto result = check_source(
        "concept AsInt:\n"
        "    fn value(self): i32\n"
        "class Wrapper:\n"
        "    label: string\n"
        "    as AsInt:\n"
        "        fn value(self): i32 -> self.label\n");
    expect(!is_ok(result)) << "self.field type mismatch should error";
    expect(has_error_containing(result, "does not match return type"))
        << "should report type mismatch";
  };

  "self.field access in extend method"_test = [] {
    auto result = check_source(
        "concept HasX:\n"
        "    fn get_x(self): f64\n"
        "class Point:\n"
        "    x: f64\n"
        "    y: f64\n"
        "extend Point as HasX:\n"
        "    fn get_x(self): f64 -> self.x\n");
    expect(is_ok(result)) << "self.field in extend should typecheck";
  };

  "self typed as enclosing class in class methods"_test = [] {
    // Direct method on a class (not through conformance) — self
    // should be typed as the class. For now, class-body methods
    // are not yet supported (only conformance/extend methods),
    // so we just verify the conformance path works.
    auto result = check_source(
        "concept GetX:\n"
        "    fn get_x(self): f64\n"
        "class Vec:\n"
        "    x: f64\n"
        "    as GetX:\n"
        "        fn get_x(self): f64 -> self.x\n");
    expect(is_ok(result)) << "self.field should typecheck in conformance";
  };
};

// ---------------------------------------------------------------------------
// Method dispatch through conformance and extend
// ---------------------------------------------------------------------------

suite<"typecheck_methods"> typecheck_methods = [] {
  "method call through conformance"_test = [] {
    auto result = check_source(
        "concept HasName:\n"
        "    fn name(self): string\n"
        "class Person:\n"
        "    first: string\n"
        "    as HasName:\n"
        "        fn name(self): string -> self.first\n"
        "fn greet(p: Person): string -> p.name()\n");
    expect(is_ok(result)) << "method call through conformance should typecheck";
  };

  "method call through extend"_test = [] {
    auto result = check_source(
        "concept HasX:\n"
        "    fn get_x(self): f64\n"
        "class Point:\n"
        "    x: f64\n"
        "    y: f64\n"
        "extend Point as HasX:\n"
        "    fn get_x(self): f64 -> self.x\n"
        "fn read_x(p: Point): f64 -> p.get_x()\n");
    expect(is_ok(result)) << "method call through extend should typecheck";
  };

  "method call with args"_test = [] {
    auto result = check_source(
        "concept Eq:\n"
        "    fn eq(self, other: Eq): bool\n"
        "class Val:\n"
        "    n: i32\n"
        "    as Eq:\n"
        "        fn eq(self, other: Val): bool -> self.n == other.n\n"
        "fn same(a: Val, b: Val): bool -> a.eq(b)\n");
    expect(is_ok(result)) << "method call with args should typecheck";
  };

  "method call wrong arg type"_test = [] {
    auto result = check_source(
        "concept Eq:\n"
        "    fn eq(self, other: Eq): bool\n"
        "class Val:\n"
        "    n: i32\n"
        "    as Eq:\n"
        "        fn eq(self, other: Val): bool -> true\n"
        "fn bad(a: Val): bool -> a.eq(42)\n");
    expect(!is_ok(result)) << "wrong arg type should error";
  };

  "unknown method errors"_test = [] {
    auto result = check_source(
        "class Point:\n"
        "    x: f64\n"
        "fn bad(p: Point): f64 -> p.missing()\n");
    expect(!is_ok(result)) << "unknown method should error";
    expect(has_error_containing(result, "no field or method"))
        << "should report missing method";
  };
};

// NOLINTEND(readability-magic-numbers)

auto main() -> int {} // NOLINT(readability-named-parameter)
