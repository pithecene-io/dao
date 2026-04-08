#include "driver/pipeline.h"

#include "analysis/semantic_tokens.h"
#include "frontend/ast/ast_printer.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"
#include "frontend/resolve/resolve.h"
#include "backend/llvm/llvm_backend.h"
#include "ir/hir/hir_printer.h"
#include "ir/mir/mir_printer.h"

#include <llvm/Support/Program.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

// Debug-only token dump. Output format is not stable and must not be
// relied upon by tests, tooling, or documentation.
void cmd_lex(const std::filesystem::path& path) {
  auto contents = dao::read_file(path);
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

  if (dao::print_error_diagnostics(path.filename().string(), source,
                              result.diagnostics)) {
    std::exit(EXIT_FAILURE);
  }
}

// Debug-only parse diagnostic dump. Output format is not stable.
void cmd_parse(const std::filesystem::path& path) {
  auto result = dao::lex_and_parse(path);
  if (result.parse_result.file != nullptr) {
    std::cout << "File: " << result.parse_result.file->imports.size() << " imports, "
              << result.parse_result.file->declarations.size() << " declarations\n";
  }
}

// Pretty-print AST. Output is deterministic and suitable for golden-file testing.
void cmd_ast(const std::filesystem::path& path) {
  auto result = dao::lex_and_parse(path);
  if (result.parse_result.file != nullptr) {
    dao::print_ast(std::cout, *result.parse_result.file);
  }
}

// Emit semantic token classification. Output is deterministic.
void cmd_tokens(const std::filesystem::path& path) {
  auto result = dao::lex_and_parse_with_prelude(path);

  // Run name resolution for resolve-driven classifications.
  dao::ResolveResult resolve_result;
  if (result.parsed.parse_result.file != nullptr) {
    resolve_result = dao::resolve(*result.parsed.parse_result.file,
                                    result.prelude_bytes);
  }

  auto sem_tokens = dao::classify_tokens(result.parsed.lex_result.tokens,
                                         result.parsed.parse_result.file,
                                         &resolve_result);

  for (const auto& tok : sem_tokens) {
    if (tok.span.offset < result.prelude_bytes) {
      continue;
    }
    auto loc = result.parsed.source.line_col(tok.span.offset);
    auto line = loc.line > result.prelude_lines ? loc.line - result.prelude_lines : loc.line;
    auto text = result.parsed.source.text(tok.span);
    std::cout << line << ":" << loc.col << " " << tok.kind << " " << text << "\n";
  }
}

// Run name resolution and print results.
void cmd_resolve(const std::filesystem::path& path) {
  auto result = dao::lex_and_parse_with_prelude(path);
  if (result.parsed.parse_result.file == nullptr) {
    return;
  }

  auto resolve_result = dao::resolve(*result.parsed.parse_result.file,
                                     result.prelude_bytes);

  // Print declared symbols (user region only).
  std::cout << "Symbols:\n";
  for (const auto& sym : resolve_result.context.symbols()) {
    if (sym->decl_span.offset < result.prelude_bytes) {
      continue;
    }
    std::cout << "  " << dao::symbol_kind_name(sym->kind) << " " << sym->name;
    if (sym->decl_span.length > 0) {
      auto decl_loc = result.parsed.source.line_col(sym->decl_span.offset);
      auto line = decl_loc.line > result.prelude_lines
                      ? decl_loc.line - result.prelude_lines
                      : decl_loc.line;
      std::cout << " [" << line << ":" << decl_loc.col << "]";
    }
    std::cout << "\n";
  }

  // Print uses in user region (resolved references).
  std::cout << "\nUses:\n";
  for (const auto& [offset, sym] : resolve_result.uses) {
    if (offset < result.prelude_bytes) {
      continue;
    }
    auto loc = result.parsed.source.line_col(offset);
    auto line = loc.line > result.prelude_lines ? loc.line - result.prelude_lines : loc.line;
    std::cout << "  " << line << ":" << loc.col << " "
              << result.parsed.source.text(
                     dao::Span{.offset = offset,
                               .length = static_cast<uint32_t>(sym->name.size())})
              << " -> " << dao::symbol_kind_name(sym->kind) << " " << sym->name;
    if (sym->decl_span.length > 0) {
      auto decl_loc = result.parsed.source.line_col(sym->decl_span.offset);
      auto decl_line = decl_loc.line > result.prelude_lines
                           ? decl_loc.line - result.prelude_lines
                           : decl_loc.line;
      std::cout << " [" << decl_line << ":" << decl_loc.col << "]";
    }
    std::cout << "\n";
  }

  // Print diagnostics in user region (to stdout -- this is a debug dump command).
  bool has_user_diags = false;
  for (const auto& diag : resolve_result.diagnostics) {
    if (diag.span.offset < result.prelude_bytes) {
      continue;
    }
    if (!has_user_diags) {
      std::cout << "\nDiagnostics:\n";
      has_user_diags = true;
    }
    auto loc = result.parsed.source.line_col(diag.span.offset);
    auto line = loc.line > result.prelude_lines ? loc.line - result.prelude_lines : loc.line;
    std::cout << "  " << path.filename().string() << ":" << line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }
}

// Run type checking and print diagnostics.
void cmd_check(const std::filesystem::path& path) {
  dao::run_frontend(path);
  std::cout << "ok\n";
}

// Build and print HIR. Output is deterministic.
void cmd_hir(const std::filesystem::path& path) {
  auto result = dao::run_through_hir(path);
  if (result.hir.module != nullptr) {
    dao::print_hir(std::cout, *result.hir.module);
  }
}

// Build and print MIR. Output is deterministic.
void cmd_mir(const std::filesystem::path& path) {
  auto result = dao::run_through_mir(path);
  if (result.mir.module != nullptr) {
    dao::print_mir(std::cout, *result.mir.module);
  }
}

// Build and emit LLVM IR. Output is deterministic.
void cmd_llvm_ir(const std::filesystem::path& path) {
  // Initialize targets so the module gets a correct DataLayout
  // for ABI-sensitive lowering (struct coercion, alignment).
  dao::LlvmBackend::initialize_targets();

  auto mir = dao::run_through_mir(path);
  llvm::LLVMContext llvm_ctx;
  auto llvm_result = dao::lower_to_llvm(mir, llvm_ctx, path);
  dao::LlvmBackend::print_ir(std::cout, *llvm_result.module);
}

// Compile a .dao file to a native executable.
// Extra link inputs (object files, -l flags, -L flags) are forwarded
// to the system linker.
void cmd_build(const std::filesystem::path& path,
               std::span<const std::string> link_extras = {}) {
  // Initialize targets before lowering so the module gets a correct
  // DataLayout for ABI-sensitive struct coercion.
  dao::LlvmBackend::initialize_targets();

  auto mir = dao::run_through_mir(path);
  llvm::LLVMContext llvm_ctx;
  auto llvm_result = dao::lower_to_llvm(mir, llvm_ctx, path);

  auto obj_path = std::filesystem::temp_directory_path() /
                  (path.stem().string() + ".o");
  std::string emit_error;
  if (!dao::LlvmBackend::emit_object(*llvm_result.module,
                                      obj_path.string(), emit_error)) {
    std::cerr << "error: " << emit_error << "\n";
    std::exit(EXIT_FAILURE);
  }

  // Link with system cc: object + runtime library -> executable.
  auto output_path = path.parent_path() / path.stem();

  auto cc_path = llvm::sys::findProgramByName("cc");
  if (!cc_path) {
    std::cerr << "error: cannot find 'cc' linker: "
              << cc_path.getError().message() << "\n";
    std::filesystem::remove(obj_path);
    std::exit(EXIT_FAILURE);
  }

  // StringRef does not own data -- keep string temporaries alive.
  auto obj_str = obj_path.string();
  auto out_str = output_path.string();
  std::vector<llvm::StringRef> args = {
      *cc_path,
      obj_str,
      DAO_RUNTIME_LIB,
      "-o",
      out_str,
  };

  // Append extra link inputs (object files, -l flags, -L flags).
  for (const auto& extra : link_extras) {
    args.push_back(extra);
  }

  std::string link_error;
  int link_status = llvm::sys::ExecuteAndWait(
      *cc_path, args, /*Env=*/std::nullopt, /*Redirects=*/{},
      /*SecondsToWait=*/0, /*MemoryLimit=*/0, &link_error);
  std::filesystem::remove(obj_path);

  if (link_status != 0) {
    std::cerr << "error: linking failed";
    if (!link_error.empty()) {
      std::cerr << ": " << link_error;
    }
    std::cerr << "\n";
    std::exit(EXIT_FAILURE);
  }

  std::cout << output_path.string() << "\n";
}

// ---------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------

using CommandFn = void (*)(const std::filesystem::path&);

struct Command {
  std::string_view name;
  CommandFn handler;
};

constexpr auto commands = std::array{
    Command{.name = "lex", .handler = cmd_lex},
    Command{.name = "parse", .handler = cmd_parse},
    Command{.name = "ast", .handler = cmd_ast},
    Command{.name = "tokens", .handler = cmd_tokens},
    Command{.name = "resolve", .handler = cmd_resolve},
    Command{.name = "check", .handler = cmd_check},
    Command{.name = "hir", .handler = cmd_hir},
    Command{.name = "mir", .handler = cmd_mir},
    Command{.name = "llvm-ir", .handler = cmd_llvm_ir},
};

} // namespace

auto main(int argc, char* argv[]) -> int {
  if (argc < 2) {
    std::cerr << "usage: daoc <command> <file>\n"
              << "commands: lex, parse, ast, tokens, resolve, check, hir, mir, llvm-ir, build\n";
    return EXIT_FAILURE;
  }

  std::string_view arg1(argv[1]);

  for (const auto& [name, handler] : commands) {
    if (arg1 == name) {
      if (argc < 3) {
        std::cerr << "usage: daoc " << name << " <file>\n";
        return EXIT_FAILURE;
      }
      std::filesystem::path path(argv[2]);
      if (!std::filesystem::exists(path)) {
        std::cerr << "error: file not found: " << path << "\n";
        return EXIT_FAILURE;
      }
      handler(path);
      return EXIT_SUCCESS;
    }
  }

  // build -- accepts extra link inputs after the source file.
  if (arg1 == "build") {
    if (argc < 3) {
      std::cerr << "usage: daoc build <file> [link-inputs...]\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path path(argv[2]);
    if (!std::filesystem::exists(path)) {
      std::cerr << "error: file not found: " << path << "\n";
      return EXIT_FAILURE;
    }
    std::vector<std::string> extras;
    for (int i = 3; i < argc; ++i) {
      extras.emplace_back(argv[i]);
    }
    cmd_build(path, extras);
    return EXIT_SUCCESS;
  }

  // daoc <file> -- read and exit (Task 0 compat)
  std::filesystem::path path(arg1);
  if (!std::filesystem::exists(path)) {
    std::cerr << "error: file not found: " << path << "\n";
    return EXIT_FAILURE;
  }
  dao::read_file(path);
  return EXIT_SUCCESS;
}
