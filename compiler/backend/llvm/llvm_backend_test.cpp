#include "backend/llvm/llvm_backend.h"
#include "backend/llvm/llvm_runtime_hooks.h"
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
#include <algorithm>
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

  explicit LlvmTestPipeline(const std::string& src,
                            uint32_t prelude_bytes = 0)
      : source("test.dao", std::string(src)),
        lex_result(lex(source)),
        parse_result(parse(lex_result.tokens)) {
    if (parse_result.file != nullptr) {
      resolve_result = resolve(*parse_result.file, prelude_bytes);
      check_result = typecheck(*parse_result.file, resolve_result, types);
      hir_result = build_hir(*parse_result.file, resolve_result, check_result, hir_ctx);
      if (hir_result.module != nullptr) {
        mir_result = build_mir(*hir_result.module, mir_ctx, types);
        if (mir_result.module != nullptr) {
          LlvmBackend backend(llvm_ctx);
          llvm_result = backend.lower(*mir_result.module, prelude_bytes);
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
    return std::any_of(llvm_result.diagnostics.begin(),
                       llvm_result.diagnostics.end(),
                       [](const Diagnostic& diag) {
                         return diag.severity == Severity::Error;
                       });
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
  "signed integer arithmetic uses checked intrinsics"_test = [] {
    LlvmTestPipeline pipe(
        "fn calc(x: i32, y: i32): i32\n"
        "  return x * y + x - y\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    // Signed ops use overflow-checking intrinsics.
    expect(contains(ir, "llvm.smul.with.overflow.i32")) << ir;
    expect(contains(ir, "llvm.sadd.with.overflow.i32")) << ir;
    expect(contains(ir, "llvm.ssub.with.overflow.i32")) << ir;
    // Overflow branches to trap.
    expect(contains(ir, "overflow.trap")) << ir;
    expect(contains(ir, "llvm.trap")) << ir;
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

  "float == uses ordered equal (oeq)"_test = [] {
    LlvmTestPipeline pipe(
        "fn feq(a: f64, b: f64): bool\n"
        "  return a == b\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "fcmp oeq")) << ir;
  };

  "float != uses unordered not-equal (une)"_test = [] {
    LlvmTestPipeline pipe(
        "fn fne(a: f64, b: f64): bool\n"
        "  return a != b\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    // UNE: true if either operand is NaN OR operands are not equal.
    // ONE would incorrectly return false for NaN comparisons.
    expect(contains(ir, "fcmp une")) << ir;
  };

  "float < uses ordered less-than (olt)"_test = [] {
    LlvmTestPipeline pipe(
        "fn flt(a: f64, b: f64): bool\n"
        "  return a < b\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "fcmp olt")) << ir;
  };

  "float >= uses ordered greater-or-equal (oge)"_test = [] {
    LlvmTestPipeline pipe(
        "fn fge(a: f64, b: f64): bool\n"
        "  return a >= b\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "fcmp oge")) << ir;
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

  "resource memory calls domain hooks"_test = [] {
    LlvmTestPipeline pipe(
        "fn test(): i32\n"
        "  resource memory Arena =>\n"
        "    return 1\n"
        "  return 0\n");
    expect(!pipe.has_errors()) << "resource memory should compile";
    auto ir = pipe.ir();
    expect(contains(ir, "__dao_mem_resource_enter")) << ir;
    expect(contains(ir, "__dao_mem_resource_exit")) << ir;
  };

  "resource gpu is rejected"_test = [] {
    LlvmTestPipeline pipe(
        "fn test(): i32\n"
        "  resource gpu compute =>\n"
        "    return 1\n"
        "  return 0\n");
    expect(pipe.has_errors()) << "resource gpu should fail";
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

// ---------------------------------------------------------------------------
// Runtime ABI — hook declarations and string shape
// ---------------------------------------------------------------------------

suite<"runtime_abi"> runtime_abi = [] {
  "runtime hooks are declared with correct signatures"_test = [] {
    llvm::LLVMContext ctx;
    auto module = std::make_unique<llvm::Module>("abi_test", ctx);
    LlvmTypeLowering types(ctx);
    LlvmRuntimeHooks hooks(*module, types);
    hooks.declare_all();

    // IO hook
    auto* write_fn = module->getFunction("__dao_io_write_stdout");
    expect(write_fn != nullptr) << "write_stdout declared";
    expect(write_fn->getReturnType()->isVoidTy()) << "returns void";
    expect(write_fn->arg_size() == 1u) << "1 param";
    expect(write_fn->getArg(0)->getType()->isPointerTy()) << "string ptr param";

    // Equality hooks
    auto* eq_i32 = module->getFunction("__dao_eq_i32");
    expect(eq_i32 != nullptr) << "eq_i32 declared";
    expect(eq_i32->getReturnType()->isIntegerTy(1)) << "returns i1";
    expect(eq_i32->arg_size() == 2u) << "2 params";

    auto* eq_f64 = module->getFunction("__dao_eq_f64");
    expect(eq_f64 != nullptr) << "eq_f64 declared";
    expect(eq_f64->getReturnType()->isIntegerTy(1)) << "returns i1";

    auto* eq_bool = module->getFunction("__dao_eq_bool");
    expect(eq_bool != nullptr) << "eq_bool declared";

    auto* eq_str = module->getFunction("__dao_eq_string");
    expect(eq_str != nullptr) << "eq_string declared";
    expect(eq_str->arg_size() == 2u) << "2 ptr params";

    // Conversion hooks
    auto* conv_i32 = module->getFunction("__dao_conv_i32_to_string");
    expect(conv_i32 != nullptr) << "conv_i32_to_string declared";
    expect(conv_i32->getReturnType()->isStructTy()) << "returns dao.string";
    expect(conv_i32->arg_size() == 1u) << "1 param";

    auto* conv_f64 = module->getFunction("__dao_conv_f64_to_string");
    expect(conv_f64 != nullptr) << "conv_f64_to_string declared";

    auto* conv_bool = module->getFunction("__dao_conv_bool_to_string");
    expect(conv_bool != nullptr) << "conv_bool_to_string declared";
  };

  "string ABI shape matches contract"_test = [] {
    llvm::LLVMContext ctx;
    LlvmTypeLowering types(ctx);

    auto* str_type = types.string_type();
    expect(str_type != nullptr) << "string type exists";
    expect(str_type->isStructTy()) << "is struct";
    expect(str_type->getStructName() == "dao.string") << "named dao.string";
    expect(str_type->getNumElements() == 2u) << "2 fields: ptr + len";
    expect(str_type->getElementType(0)->isPointerTy()) << "field 0 is ptr";
    expect(str_type->getElementType(1)->isIntegerTy(64)) << "field 1 is i64";
  };

  "is_runtime_hook recognizes all hooks"_test = [] {
    expect(LlvmRuntimeHooks::is_runtime_hook("__dao_io_write_stdout"));
    expect(LlvmRuntimeHooks::is_runtime_hook("__dao_eq_i32"));
    expect(LlvmRuntimeHooks::is_runtime_hook("__dao_eq_f64"));
    expect(LlvmRuntimeHooks::is_runtime_hook("__dao_eq_bool"));
    expect(LlvmRuntimeHooks::is_runtime_hook("__dao_eq_string"));
    expect(LlvmRuntimeHooks::is_runtime_hook("__dao_conv_i32_to_string"));
    expect(LlvmRuntimeHooks::is_runtime_hook("__dao_conv_f64_to_string"));
    expect(LlvmRuntimeHooks::is_runtime_hook("__dao_conv_bool_to_string"));
    expect(!LlvmRuntimeHooks::is_runtime_hook("__write_stdout"));
    expect(!LlvmRuntimeHooks::is_runtime_hook("user_function"));
  };

  "prelude print generates correct runtime call"_test = [] {
    // The extern declaration is "prelude" — mark its region so the
    // resolver allows the __dao_ prefix.
    constexpr std::string_view prelude =
        "extern fn __dao_io_write_stdout(msg: string): void\n";
    LlvmTestPipeline pipe(
        std::string(prelude) +
        "fn print(msg: string): void -> __dao_io_write_stdout(msg)\n"
        "\n"
        "fn main(): void\n"
        "  print(\"hello\")\n",
        static_cast<uint32_t>(prelude.size()));
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "__dao_io_write_stdout")) << ir;
    expect(contains(ir, "dao.string")) << ir;
  };

  "prelude length generates str_length call"_test = [] {
    constexpr std::string_view prelude =
        "extern fn __dao_str_length(s: string): i64\n";
    LlvmTestPipeline pipe(
        std::string(prelude) +
        "fn length(s: string): i64 -> __dao_str_length(s)\n"
        "\n"
        "fn main(): i64\n"
        "  return length(\"hello\")\n",
        static_cast<uint32_t>(prelude.size()));
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "__dao_str_length")) << ir;
    expect(contains(ir, "call i64")) << ir;
  };

  "runtime hooks pre-declared by backend unconditionally"_test = [] {
    // Even without any prelude extern declarations, runtime hooks
    // should be present in the module because LlvmBackend::lower()
    // calls LlvmRuntimeHooks::declare_all() unconditionally.
    LlvmTestPipeline pipe(
        "fn main(): void\n"
        "  return\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    // IO
    expect(contains(ir, "declare void @__dao_io_write_stdout")) << ir;
    // Equality
    expect(contains(ir, "declare i1 @__dao_eq_i32")) << ir;
    expect(contains(ir, "declare i1 @__dao_eq_i64")) << ir;
    expect(contains(ir, "declare i1 @__dao_eq_f64")) << ir;
    // Conversion (to_string)
    expect(contains(ir, "@__dao_conv_i32_to_string")) << ir;
    expect(contains(ir, "@__dao_conv_i64_to_string")) << ir;
    // Numeric conversions
    expect(contains(ir, "declare double @__dao_conv_i32_to_f64(i32)")) << ir;
    expect(contains(ir, "declare i64 @__dao_conv_i32_to_i64(i32)")) << ir;
    expect(contains(ir, "declare i32 @__dao_conv_f64_to_i32(double)")) << ir;
    expect(contains(ir, "declare i32 @__dao_conv_i64_to_i32(i64)")) << ir;
    // String
    expect(contains(ir, "@__dao_str_length")) << ir;
    // Overflow (wrapping + saturating)
    expect(contains(ir, "declare i32 @__dao_wrapping_add_i32(i32, i32)")) << ir;
    expect(contains(ir, "declare i64 @__dao_wrapping_add_i64(i64, i64)")) << ir;
    expect(contains(ir, "declare i32 @__dao_saturating_add_i32(i32, i32)")) << ir;
    expect(contains(ir, "declare i64 @__dao_saturating_add_i64(i64, i64)")) << ir;
  };
};

// ---------------------------------------------------------------------------
// Generator / coroutine lowering
// ---------------------------------------------------------------------------

suite<"generators"> generators = [] {
  "generator declares init and resume functions"_test = [] {
    LlvmTestPipeline pipe(
        "fn single(): Generator<i32>\n"
        "  yield 42\n"
        "\n"
        "fn main(): i32\n"
        "  for x in single():\n"
        "    return x\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    // Init function: returns a generator fat pair.
    expect(contains(ir, "define %dao.generator @single()")) << ir;
    // Resume function: takes ptr, returns void.
    expect(contains(ir, "define void @single.resume(ptr")) << ir;
  };

  "generator init allocates frame"_test = [] {
    LlvmTestPipeline pipe(
        "fn single(): Generator<i32>\n"
        "  yield 1\n"
        "\n"
        "fn main(): i32\n"
        "  for x in single():\n"
        "    return x\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "__dao_gen_alloc")) << ir;
    // Init returns a { ptr, ptr } generator pair.
    expect(contains(ir, "ret %dao.generator")) << ir;
  };

  "generator resume has state dispatch"_test = [] {
    LlvmTestPipeline pipe(
        "fn single(): Generator<i32>\n"
        "  yield 42\n"
        "\n"
        "fn main(): i32\n"
        "  for x in single():\n"
        "    return x\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    expect(contains(ir, "switch i32")) << ir;
    expect(contains(ir, "yield.ptr")) << ir;
  };

  "for-loop over generator produces iterator ops"_test = [] {
    LlvmTestPipeline pipe(
        "fn nums(): Generator<i32>\n"
        "  yield 1\n"
        "  yield 2\n"
        "\n"
        "fn main(): i32\n"
        "  let sum: i32 = 0\n"
        "  for x in nums():\n"
        "    sum = sum + x\n"
        "  return sum\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    // Consumer calls resume, reads done flag, reads yield slot.
    expect(contains(ir, "call void %")) << ir;
    expect(contains(ir, "done.ptr")) << ir;
    expect(contains(ir, "yield.val")) << ir;
    // Destroy calls __dao_gen_free.
    expect(contains(ir, "__dao_gen_free")) << ir;
  };

  "early return from for-loop frees generator frame"_test = [] {
    LlvmTestPipeline pipe(
        "fn single(): Generator<i32>\n"
        "  yield 42\n"
        "\n"
        "fn main(): i32\n"
        "  for x in single():\n"
        "    return x\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    // The early-return path must call __dao_gen_free before returning.
    // Count occurrences: one for the early-return path, one for the
    // normal loop-exit path.
    size_t count = 0;
    std::string::size_type pos = 0;
    while ((pos = ir.find("__dao_gen_free", pos)) != std::string::npos) {
      ++count;
      ++pos;
    }
    expect(count >= 2u) << "expected >=2 gen_free calls (early + normal), got "
                        << count << "\n" << ir;
  };

  "init passes alignof to gen_alloc"_test = [] {
    LlvmTestPipeline pipe(
        "fn single(): Generator<i32>\n"
        "  yield 1\n"
        "\n"
        "fn main(): i32\n"
        "  for x in single():\n"
        "    return x\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    // Alignment is computed via GEP-from-null offsetof trick, not
    // hardcoded.  LLVM constant-folds the GEP to a ConstantExpr,
    // so we check for the { i8, %frame } wrapper struct pattern.
    expect(contains(ir, "{ i8, %dao.gen.single }")) << ir;
  };

  "range generator with params"_test = [] {
    LlvmTestPipeline pipe(
        "fn range(start: i32, end: i32): Generator<i32>\n"
        "  let i: i32 = start\n"
        "  while i < end:\n"
        "    yield i\n"
        "    i = i + 1\n"
        "\n"
        "fn main(): i32\n"
        "  let sum: i32 = 0\n"
        "  for x in range(0, 5):\n"
        "    sum = sum + x\n"
        "  return sum\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors";
    // Init function takes params.
    expect(contains(ir, "define %dao.generator @range(i32 %start, i32 %end)")) << ir;
    // Resume function.
    expect(contains(ir, "define void @range.resume(ptr")) << ir;
    // Frame type exists.
    expect(contains(ir, "dao.gen.range")) << ir;
  };

  "generator through storage"_test = [] {
    LlvmTestPipeline pipe(
        "fn single(): Generator<i32>\n"
        "  yield 42\n"
        "\n"
        "fn main(): i32\n"
        "  let g: Generator<i32> = single()\n"
        "  for x in g:\n"
        "    return x\n"
        "  return 0\n");
    auto ir = pipe.ir();
    expect(!pipe.has_errors()) << "no backend errors: " << ir;
    // Generator pair survives store/load through local.
    expect(contains(ir, "%dao.generator")) << ir;
    expect(contains(ir, "gen.frame")) << ir;
    expect(contains(ir, "gen.resume")) << ir;
    expect(contains(ir, "__dao_gen_free")) << ir;
  };
};

// NOLINTEND(readability-magic-numbers)

auto main() -> int {} // NOLINT(readability-named-parameter)
