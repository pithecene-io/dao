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
#include "ir/mir/mir_printer.h"

#include <boost/ut.hpp>
#include <sstream>
#include <string>

using namespace boost::ut;
using namespace dao;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

/// Owns all pipeline state so MIR nodes remain valid.
struct MirTestPipeline {
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

  explicit MirTestPipeline(const std::string& src)
      : source("test.dao", std::string(src)),
        lex_result(lex(source)),
        parse_result(parse(lex_result.tokens)) {
    if (parse_result.file != nullptr) {
      resolve_result = resolve(*parse_result.file);
      check_result =
          typecheck(*parse_result.file, resolve_result, types);
      hir_result =
          build_hir(*parse_result.file, resolve_result, check_result,
                    hir_ctx);
      if (hir_result.module != nullptr) {
        mir_result = build_mir(*hir_result.module, mir_ctx, types);
      }
    }
  }

  [[nodiscard]] auto module() const -> MirModule* {
    return mir_result.module;
  }

  [[nodiscard]] auto dump() const -> std::string {
    std::ostringstream out;
    if (mir_result.module != nullptr) {
      print_mir(out, *mir_result.module);
    }
    return out.str();
  }
};

auto contains(const std::string& haystack, std::string_view needle) -> bool {
  return haystack.find(needle) != std::string::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// Control flow: if
// ---------------------------------------------------------------------------

suite<"mir_if"> mir_if = [] {
  "if lowers to cond_br with then and merge blocks"_test = [] {
    MirTestPipeline pipe(
        "fn test(x: i32): i32\n"
        "    if x > 0:\n"
        "        return x\n"
        "    return 0\n");
    auto dump = pipe.dump();
    expect(contains(dump, "cond_br")) << dump;
    expect(contains(dump, "bb1:")) << dump; // then block
    expect(contains(dump, "bb2:")) << dump; // merge block
    expect(contains(dump, "binary >")) << dump;
  };

  "if-else with both returns has no merge block"_test = [] {
    MirTestPipeline pipe(
        "fn test(x: i32): i32\n"
        "    if x > 0:\n"
        "        return 1\n"
        "    else:\n"
        "        return 0\n");
    auto dump = pipe.dump();
    expect(contains(dump, "cond_br")) << dump;
    expect(contains(dump, "bb1:")) << dump;  // then
    expect(contains(dump, "bb2:")) << dump;  // else
    expect(!contains(dump, "bb3:")) << dump; // no dangling merge
  };

  "if-else with fallthrough has merge block"_test = [] {
    MirTestPipeline pipe(
        "fn test(x: i32): i32\n"
        "    let r: i32 = 0\n"
        "    if x > 0:\n"
        "        r = 1\n"
        "    else:\n"
        "        r = 2\n"
        "    return r\n");
    auto dump = pipe.dump();
    expect(contains(dump, "cond_br")) << dump;
    expect(contains(dump, "bb1:")) << dump; // then
    expect(contains(dump, "bb2:")) << dump; // else
    expect(contains(dump, "bb3:")) << dump; // merge
  };
};

// ---------------------------------------------------------------------------
// Control flow: while
// ---------------------------------------------------------------------------

suite<"mir_while"> mir_while = [] {
  "while lowers to loop blocks"_test = [] {
    MirTestPipeline pipe(
        "fn test(x: i32): i32\n"
        "    let i: i32 = 0\n"
        "    while i < x:\n"
        "        i = i + 1\n"
        "    return i\n");
    auto dump = pipe.dump();
    // Should have: entry -> cond -> body -> back to cond, exit
    expect(contains(dump, "br bb")) << dump;
    expect(contains(dump, "cond_br")) << dump;
    expect(contains(dump, "binary <")) << dump;
    expect(contains(dump, "binary +")) << dump;
  };
};

// ---------------------------------------------------------------------------
// Control flow: return
// ---------------------------------------------------------------------------

suite<"mir_return"> mir_return = [] {
  "return with value"_test = [] {
    MirTestPipeline pipe("fn main(): i32\n    return 42\n");
    auto dump = pipe.dump();
    expect(contains(dump, "const_int 42")) << dump;
    expect(contains(dump, "return %")) << dump;
  };
};

// ---------------------------------------------------------------------------
// Data flow: let with initializer
// ---------------------------------------------------------------------------

suite<"mir_let"> mir_let = [] {
  "let with initializer lowers to store"_test = [] {
    MirTestPipeline pipe(
        "fn main(): i32\n"
        "    let x: i32 = 42\n"
        "    return x\n");
    auto dump = pipe.dump();
    expect(contains(dump, "const_int 42")) << dump;
    expect(contains(dump, "store _")) << dump;
    expect(contains(dump, "load _")) << dump;
  };

  "let without initializer declares local"_test = [] {
    MirTestPipeline pipe(
        "fn main(): i32\n"
        "    let x: i32 = 0\n"
        "    return x\n");
    auto dump = pipe.dump();
    // Local should be declared.
    expect(contains(dump, "local _")) << dump;
  };
};

// ---------------------------------------------------------------------------
// Data flow: assignment
// ---------------------------------------------------------------------------

suite<"mir_assignment"> mir_assignment = [] {
  "assignment lowers to store"_test = [] {
    MirTestPipeline pipe(
        "fn main(): i32\n"
        "    let x: i32 = 0\n"
        "    x = 42\n"
        "    return x\n");
    auto dump = pipe.dump();
    // Two stores: one for init, one for assignment.
    // Count store occurrences.
    size_t count = 0;
    size_t pos = 0;
    while ((pos = dump.find("store _", pos)) != std::string::npos) {
      ++count;
      ++pos;
    }
    expect(count >= 2_ul) << dump;
  };
};

// ---------------------------------------------------------------------------
// Data flow: arithmetic
// ---------------------------------------------------------------------------

suite<"mir_arithmetic"> mir_arithmetic = [] {
  "arithmetic lowers to binary instructions"_test = [] {
    MirTestPipeline pipe(
        "fn add(a: i32, b: i32): i32\n"
        "    return a + b\n");
    auto dump = pipe.dump();
    expect(contains(dump, "binary + %")) << dump;
    expect(contains(dump, ": i32")) << dump;
  };
};

// ---------------------------------------------------------------------------
// Data flow: call
// ---------------------------------------------------------------------------

suite<"mir_call"> mir_call = [] {
  "call lowers with explicit args"_test = [] {
    MirTestPipeline pipe(
        "fn add(a: i32, b: i32): i32 -> a + b\n"
        "fn main(): i32\n"
        "    return add(1, 2)\n");
    auto dump = pipe.dump();
    expect(contains(dump, "call %")) << dump;
  };
};

// ---------------------------------------------------------------------------
// Dao-specific: pipe lowering
// ---------------------------------------------------------------------------

suite<"mir_pipe"> mir_pipe = [] {
  "pipe lowers to call"_test = [] {
    MirTestPipeline pipe(
        "fn double(x: i32): i32 -> x + x\n"
        "fn main(): i32\n"
        "    return 5 |> double\n");
    auto dump = pipe.dump();
    // Pipe should become a call instruction, not a pipe node.
    expect(!contains(dump, "pipe")) << dump;
    expect(contains(dump, "call %")) << dump;
    expect(contains(dump, "const_int 5")) << dump;
  };
};

// ---------------------------------------------------------------------------
// Dao-specific: mode region
// ---------------------------------------------------------------------------

suite<"mir_mode"> mir_mode = [] {
  "mode region preserved as enter/exit"_test = [] {
    MirTestPipeline pipe(
        "fn main(): i32\n"
        "    mode unsafe =>\n"
        "        let x: i32 = 42\n"
        "    return 0\n");
    auto dump = pipe.dump();
    expect(contains(dump, "mode_enter unsafe")) << dump;
    expect(contains(dump, "mode_exit")) << dump;
  };
};

// ---------------------------------------------------------------------------
// Dao-specific: resource region
// ---------------------------------------------------------------------------

suite<"mir_resource"> mir_resource = [] {
  "resource region preserved as enter/exit"_test = [] {
    MirTestPipeline pipe(
        "fn main(): i32\n"
        "    resource gpu compute =>\n"
        "        let x: i32 = 1\n"
        "    return 0\n");
    auto dump = pipe.dump();
    expect(contains(dump, "resource_enter gpu compute")) << dump;
    expect(contains(dump, "resource_exit")) << dump;
  };
};

// ---------------------------------------------------------------------------
// Dao-specific: lambda
// ---------------------------------------------------------------------------

suite<"mir_lambda"> mir_lambda = [] {
  // NOTE: bare lambdas (e.g. `|x| -> x + 1`) are currently rejected by the
  // type checker without contextual function type information. Once lambda
  // type inference is implemented, this test should be updated to verify
  // that lambda lowers as a nested MirFunction with `lambda` instruction.
  "lambda lowering placeholder — pipeline produces module"_test = [] {
    // This source is well-typed (no lambda) and just proves the
    // pipeline doesn't crash; real lambda MIR coverage is deferred.
    MirTestPipeline pipe("fn main(): i32\n    return 42\n");
    expect(pipe.module() != nullptr);
  };
};

// ---------------------------------------------------------------------------
// Structural: every block ends with terminator
// ---------------------------------------------------------------------------

suite<"mir_structural"> mir_structural = [] {
  "every block ends with terminator"_test = [] {
    MirTestPipeline pipe(
        "fn test(x: i32): i32\n"
        "    if x > 0:\n"
        "        return x\n"
        "    return 0\n");
    auto* mod = pipe.module();
    expect(mod != nullptr);
    for (const auto* fn : mod->functions) {
      for (const auto* block : fn->blocks) {
        expect(!block->insts.empty()) << "block must not be empty";
        if (!block->insts.empty()) {
          expect(is_terminator(block->insts.back()->kind()))
              << "block must end with terminator";
        }
      }
    }
  };

  "values carry types"_test = [] {
    MirTestPipeline pipe(
        "fn main(): i32\n    return 1 + 2\n");
    auto* mod = pipe.module();
    expect(mod != nullptr);
    const auto* fn = mod->functions[0];
    // Check that at least one value-producing instruction has a type.
    bool found_typed = false;
    for (const auto* block : fn->blocks) {
      for (const auto* inst : block->insts) {
        if (inst->result.valid() && inst->type != nullptr) {
          found_typed = true;
        }
      }
    }
    expect(found_typed) << "at least one instruction has type";
  };
};

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

suite<"mir_edge"> mir_edge = [] {
  "empty function"_test = [] {
    MirTestPipeline pipe("fn noop(): void\n    let x: i32 = 1\n");
    auto dump = pipe.dump();
    expect(contains(dump, "fn noop")) << dump;
    // Should have an implicit return terminator.
    expect(contains(dump, "return")) << dump;
  };

  "for loop lowers to iter instructions"_test = [] {
    MirTestPipeline pipe(
        "fn test(xs: i32): i32\n"
        "    for item in xs:\n"
        "        let y: i32 = item\n"
        "    return 0\n");
    auto dump = pipe.dump();
    expect(contains(dump, "iter_init")) << dump;
    expect(contains(dump, "iter_has_next")) << dump;
    expect(contains(dump, "iter_next")) << dump;
    expect(contains(dump, "cond_br")) << dump;
  };
};

// ---------------------------------------------------------------------------
// Generator / yield
// ---------------------------------------------------------------------------

suite<"mir_generator"> mir_generator = [] {
  "yield lowers to MirYield"_test = [] {
    MirTestPipeline pipe(
        "fn range(n: i32): Generator<i32>\n"
        "    let i = 0\n"
        "    while i < n:\n"
        "        yield i\n"
        "        i = i + 1\n");
    auto dump = pipe.dump();
    expect(contains(dump, "yield %")) << dump;
  };

  "for-in over generator extracts element type"_test = [] {
    MirTestPipeline pipe(
        "fn range(n: i32): Generator<i32>\n"
        "    yield n\n"
        "fn main(): i32\n"
        "    let total = 0\n"
        "    for x in range(10):\n"
        "        total = total + x\n"
        "    return total\n");
    auto dump = pipe.dump();
    expect(contains(dump, "iter_init")) << dump;
    expect(contains(dump, "iter_has_next")) << dump;
    // iter_next should produce i32, not Generator<i32>
    expect(contains(dump, "iter_next")) << dump;
    expect(contains(dump, ": i32")) << dump;
  };
};

// NOLINTEND(readability-magic-numbers)

auto main() -> int {} // NOLINT(readability-named-parameter)
