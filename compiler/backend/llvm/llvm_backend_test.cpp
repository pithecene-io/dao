#include "backend/llvm/llvm_backend.h"
#include "backend/llvm/llvm_type_lowering.h"
#include "frontend/diagnostics/source.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"
#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/type_checker.h"
#include "frontend/types/type_context.h"
#include "ir/hir/hir_builder.h"
#include "ir/hir/hir_context.h"
#include "ir/mir/mir_builder.h"
#include "ir/mir/mir_context.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <boost/ut.hpp>
#include <sstream>
#include <string>

using namespace boost::ut;
using namespace dao;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Sentinel declaration identity for testing.
const int kDeclSentinel = 1;

/// Full pipeline: source → MIR → LLVM IR.
struct LlvmTestPipeline {
  SourceBuffer source;
  LexResult lex_result;
  ParseResult parse_result;
  ResolveResult resolve_result;
  TypeContext types;
  TypeCheckResult check_result;
  HirContext hir_ctx;
  HirBuildResult hir_result;
  MirContext mir_ctx;
  MirBuildResult mir_result;
  llvm::LLVMContext llvm_ctx;
  LlvmBackendResult llvm_result;

  explicit LlvmTestPipeline(const std::string& src)
      : source("test.dao", std::string(src)),
        lex_result(lex(source)),
        parse_result(parse(lex_result.tokens)) {
    if (parse_result.file != nullptr) {
      resolve_result = resolve(*parse_result.file);
      check_result = typecheck(*parse_result.file, resolve_result, types);
      hir_result = build_hir(*parse_result.file, resolve_result, check_result, hir_ctx);
      if (hir_result.module != nullptr) {
        mir_result = build_mir(*hir_result.module, mir_ctx, types);
        if (mir_result.module != nullptr) {
          LlvmBackend backend(llvm_ctx);
          llvm_result = backend.lower(*mir_result.module);
        }
      }
    }
  }

  [[nodiscard]] auto ir() const -> std::string {
    std::ostringstream out;
    if (llvm_result.module != nullptr) {
      LlvmBackend::print_ir(out, *llvm_result.module);
    }
    return out.str();
  }

  [[nodiscard]] auto has_errors() const -> bool {
    return !llvm_result.diagnostics.empty();
  }
};

auto contains(const std::string& haystack, std::string_view needle) -> bool {
  return haystack.find(needle) != std::string::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// Type lowering
// ---------------------------------------------------------------------------

suite<"type_lowering"> type_lowering = [] {
  "builtin scalars lower correctly"_test = [] {
    llvm::LLVMContext ctx;
    LlvmTypeLowering lowering(ctx);
    TypeContext types;

    expect(lowering.lower(types.i32()) != nullptr);
    expect(lowering.lower(types.i64()) != nullptr);
    expect(lowering.lower(types.f32()) != nullptr);
    expect(lowering.lower(types.f64()) != nullptr);
    expect(lowering.lower(types.bool_type()) != nullptr);
    expect(lowering.lower(types.u8()) != nullptr);
    expect(lowering.lower(types.u64()) != nullptr);
  };

  "void type lowers"_test = [] {
    llvm::LLVMContext ctx;
    LlvmTypeLowering lowering(ctx);
    TypeContext types;

    auto* lowered = lowering.lower(types.void_type());
    expect(lowered != nullptr);
    expect(lowered->isVoidTy());
  };

  "pointer type lowers"_test = [] {
    llvm::LLVMContext ctx;
    LlvmTypeLowering lowering(ctx);
    TypeContext types;

    auto* lowered = lowering.lower(types.pointer_to(types.i32()));
    expect(lowered != nullptr);
    expect(lowered->isPointerTy());
  };

  "string type lowers to struct"_test = [] {
    llvm::LLVMContext ctx;
    LlvmTypeLowering lowering(ctx);
    TypeContext types;

    auto* str = types.named_type(nullptr, "string", {});
    auto* lowered = lowering.lower(str);
    expect(lowered != nullptr);
    expect(lowered->isStructTy());
  };

  "null type returns error"_test = [] {
    llvm::LLVMContext ctx;
    LlvmTypeLowering lowering(ctx);

    expect(lowering.lower(nullptr) == nullptr);
    expect(!lowering.error().empty());
  };

  "generic param fails"_test = [] {
    llvm::LLVMContext ctx;
    LlvmTypeLowering lowering(ctx);
    TypeContext types;

    auto* gp = types.generic_param(&kDeclSentinel, "T", 0);
    expect(lowering.lower(gp) == nullptr);
    expect(contains(lowering.error(), "generic"));
  };
};

// ---------------------------------------------------------------------------
// Simple function lowering
// ---------------------------------------------------------------------------

suite<"simple_functions"> simple_functions = [] {
  "empty function with i32 return"_test = [] {
    LlvmTestPipeline pipe(
        "fn answer(): i32\n"
        "  return 42\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "define i32 @answer()")) << ir;
    expect(contains(ir, "ret i32 42")) << ir;
  };

  "void function"_test = [] {
    LlvmTestPipeline pipe(
        "fn noop(): void\n"
        "  return\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "define void @noop()")) << ir;
    expect(contains(ir, "ret void")) << ir;
  };

  "function with params"_test = [] {
    LlvmTestPipeline pipe(
        "fn add(a: i32, b: i32): i32\n"
        "  return a + b\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "define i32 @add(i32 %a, i32 %b)")) << ir;
    expect(contains(ir, "add")) << ir;
    expect(contains(ir, "ret i32")) << ir;
  };
};

// ---------------------------------------------------------------------------
// Arithmetic and comparisons
// ---------------------------------------------------------------------------

suite<"arithmetic"> arithmetic = [] {
  "integer arithmetic"_test = [] {
    LlvmTestPipeline pipe(
        "fn calc(x: i32, y: i32): i32\n"
        "  return x * y + x - y\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "mul")) << ir;
    expect(contains(ir, "add")) << ir;
    expect(contains(ir, "sub")) << ir;
  };

  "float arithmetic"_test = [] {
    LlvmTestPipeline pipe(
        "fn calc(x: f64, y: f64): f64\n"
        "  return x + y\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "fadd")) << ir;
  };

  "comparison produces i1"_test = [] {
    LlvmTestPipeline pipe(
        "fn lt(a: i32, b: i32): bool\n"
        "  return a < b\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "icmp slt")) << ir;
  };
};

// ---------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------

suite<"control_flow"> control_flow = [] {
  "if-else produces conditional branch"_test = [] {
    LlvmTestPipeline pipe(
        "fn abs(x: i32): i32\n"
        "  if x < 0:\n"
        "    return 0 - x\n"
        "  else:\n"
        "    return x\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "br i1")) << ir;
    expect(contains(ir, "ret i32")) << ir;
  };

  "while loop"_test = [] {
    LlvmTestPipeline pipe(
        "fn countdown(n: i32): i32\n"
        "  let x: i32 = n\n"
        "  while x > 0:\n"
        "    x = x - 1\n"
        "  return x\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "br i1")) << ir;
    expect(contains(ir, "icmp sgt")) << ir;
  };
};

// ---------------------------------------------------------------------------
// String literals
// ---------------------------------------------------------------------------

suite<"strings"> strings = [] {
  "string constant creates global"_test = [] {
    LlvmTestPipeline pipe(
        "fn print(msg: string): void\n"
        "  return\n"
        "\n"
        "fn main(): i32\n"
        "  print(\"hello\")\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "@.str")) << ir;
    expect(contains(ir, "hello")) << ir;
    expect(contains(ir, "dao.string")) << ir;
  };
};

// ---------------------------------------------------------------------------
// Function calls
// ---------------------------------------------------------------------------

suite<"calls"> calls = [] {
  "direct function call"_test = [] {
    LlvmTestPipeline pipe(
        "fn double(x: i32): i32\n"
        "  return x + x\n"
        "\n"
        "fn main(): i32\n"
        "  return double(21)\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "call i32 @double")) << ir;
  };
};

// ---------------------------------------------------------------------------
// Extern declarations (no body)
// ---------------------------------------------------------------------------

suite<"externs"> externs = [] {
  "extern declaration produces declare"_test = [] {
    LlvmTestPipeline pipe(
        "extern fn print(msg: string): void\n"
        "\n"
        "fn main(): i32\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "declare void @print")) << ir;
  };
};

// ---------------------------------------------------------------------------
// Module structure
// ---------------------------------------------------------------------------

suite<"module_structure"> module_structure = [] {
  "module contains all functions"_test = [] {
    LlvmTestPipeline pipe(
        "fn foo(): i32\n"
        "  return 1\n"
        "\n"
        "fn bar(): i32\n"
        "  return 2\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "@foo")) << ir;
    expect(contains(ir, "@bar")) << ir;
  };
};

// ---------------------------------------------------------------------------
// Class field access
// ---------------------------------------------------------------------------

suite<"field_access"> field_access = [] {
  "field read via extractvalue"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: f64\n"
        "  y: f64\n"
        "\n"
        "fn getx(p: Point): f64\n"
        "  return p.x\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "%dao.Point")) << ir;
  };

  "second field read"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: f64\n"
        "  y: f64\n"
        "\n"
        "fn gety(p: Point): f64\n"
        "  return p.y\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "%dao.Point")) << ir;
  };

  "field write and read"_test = [] {
    LlvmTestPipeline pipe(
        "class Vec2:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn set_and_get(v: Vec2): i32\n"
        "  v.x = 42\n"
        "  return v.y\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "getelementptr inbounds %dao.Vec2")) << ir;
    expect(contains(ir, "store i32")) << ir;
  };

  "pointer-to-struct field store (Deref -> Field)"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn setx(p: *Point): void\n"
        "  mode unsafe =>\n"
        "    (*p).x = 99\n"
        "  return\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "deref.ptr")) << ir;
    expect(contains(ir, "getelementptr inbounds %dao.Point")) << ir;
    expect(contains(ir, "store i32")) << ir;
  };

  "pointer-to-struct field read (Deref then extractvalue)"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn gety(p: *Point): i32\n"
        "  mode unsafe =>\n"
        "    return (*p).y\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "deref.ptr")) << ir;
    expect(contains(ir, "extractvalue %dao.Point")) << ir;
  };

  "struct type appears in LLVM IR"_test = [] {
    LlvmTestPipeline pipe(
        "class Pair:\n"
        "  a: i32\n"
        "  b: i64\n"
        "\n"
        "fn first(p: Pair): i32\n"
        "  return p.a\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "%dao.Pair = type { i32, i64 }")) << ir;
  };
};

// ---------------------------------------------------------------------------
// Unsupported constructs — must fail explicitly
// ---------------------------------------------------------------------------

suite<"unsupported_constructs"> unsupported_constructs = [] {
  "mode parallel is rejected"_test = [] {
    LlvmTestPipeline pipe(
        "fn test(): i32\n"
        "  mode parallel =>\n"
        "    return 1\n"
        "  return 0\n");
    expect(pipe.has_errors()) << "mode parallel should fail";
  };

  "mode unsafe is accepted"_test = [] {
    LlvmTestPipeline pipe(
        "fn test(x: i32): i32\n"
        "  mode unsafe =>\n"
        "    return x\n"
        "  return 0\n");
    expect(!pipe.has_errors()) << "mode unsafe should be no-op";
  };

  "resource region is rejected"_test = [] {
    LlvmTestPipeline pipe(
        "fn test(): i32\n"
        "  resource memory Arena =>\n"
        "    return 1\n"
        "  return 0\n");
    expect(pipe.has_errors()) << "resource should fail";
  };

  "field assignment via store"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn sety(p: Point): i32\n"
        "  p.y = 1\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "getelementptr inbounds %dao.Point")) << ir;
    expect(contains(ir, "store i32")) << ir;
  };

  "field read via extractvalue"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn gety(p: Point): i32\n"
        "  return p.y\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "extractvalue %dao.Point")) << ir;
    expect(contains(ir, "ret i32")) << ir;
  };

  "address-of field via GEP"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn addr(p: Point): *i32\n"
        "  mode unsafe =>\n"
        "    return &p.y\n"
        "  return &p.x\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "getelementptr inbounds %dao.Point")) << ir;
  };
};

// ---------------------------------------------------------------------------
// Class construction
// ---------------------------------------------------------------------------

suite<"construction"> construction = [] {
  "basic construction produces struct value"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn make(): Point\n"
        "  return Point(1, 2)\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "%dao.Point")) << ir;
    expect(contains(ir, "ret %dao.Point")) << ir;
  };

  "construction and field access roundtrip"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn get_x(): i32\n"
        "  let p: Point = Point(10, 20)\n"
        "  return p.x\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "store %dao.Point")) << ir;
    expect(contains(ir, "extractvalue")) << ir;
  };

  "nested construction"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "class Rect:\n"
        "  tl: Point\n"
        "  br: Point\n"
        "\n"
        "fn make(): Rect\n"
        "  return Rect(Point(0, 0), Point(1, 1))\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "%dao.Rect")) << ir;
    expect(contains(ir, "%dao.Point")) << ir;
    expect(contains(ir, "ret %dao.Rect")) << ir;
  };

  "pass constructed value to function"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn get_x(p: Point): i32\n"
        "  return p.x\n"
        "\n"
        "fn main(): i32\n"
        "  return get_x(Point(42, 0))\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "%dao.Point")) << ir;
    expect(contains(ir, "call i32 @get_x")) << ir;
  };

  "construction with non-constant args uses insertvalue"_test = [] {
    LlvmTestPipeline pipe(
        "class Point:\n"
        "  x: i32\n"
        "  y: i32\n"
        "\n"
        "fn make(a: i32, b: i32): Point\n"
        "  return Point(a, b)\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "insertvalue")) << ir;
    expect(contains(ir, "%dao.Point")) << ir;
  };
};

// NOLINTEND(readability-magic-numbers)

auto main() -> int {} // NOLINT(readability-named-parameter)
