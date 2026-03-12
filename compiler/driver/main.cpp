#include "analysis/semantic_tokens.h"
#include "frontend/ast/ast_printer.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"
#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/type_checker.h"
#include "frontend/types/type_context.h"
#include "ir/hir/hir_builder.h"
#include "ir/hir/hir_context.h"
#include "ir/hir/hir_printer.h"
#include "backend/llvm/llvm_backend.h"
#include "ir/mir/mir_builder.h"
#include "ir/mir/mir_context.h"
#include "ir/mir/mir_printer.h"

#include <llvm/IR/LLVMContext.h>

#include <llvm/Support/Program.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  if (!file) {
    std::cerr << "error: could not open: " << path << "\n";
    std::exit(EXIT_FAILURE);
  }
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// ---------------------------------------------------------------------------
// Diagnostic helpers
// ---------------------------------------------------------------------------

// Print all diagnostics as errors. Returns true if any were printed.
auto print_error_diagnostics(std::string_view filename,
                             const dao::SourceBuffer& source,
                             std::span<const dao::Diagnostic> diags) -> bool {
  for (const auto& diag : diags) {
    auto loc = source.line_col(diag.span.offset);
    std::cerr << filename << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }
  return !diags.empty();
}

// Print diagnostics with severity labels. Returns true if any errors.
auto print_diagnostics(std::string_view filename,
                       const dao::SourceBuffer& source,
                       std::span<const dao::Diagnostic> diags) -> bool {
  bool has_errors = false;
  for (const auto& diag : diags) {
    auto loc = source.line_col(diag.span.offset);
    const auto* severity = diag.severity == dao::Severity::Error ? "error" : "warning";
    std::cerr << filename << ":" << loc.line << ":" << loc.col
              << ": " << severity << ": " << diag.message << "\n";
    if (diag.severity == dao::Severity::Error) {
      has_errors = true;
    }
  }
  return has_errors;
}

// ---------------------------------------------------------------------------
// Pipeline stages
// ---------------------------------------------------------------------------

struct LexedFile {
  dao::SourceBuffer source;
  dao::LexResult lex_result;
};

auto lex_file(const std::filesystem::path& path) -> LexedFile {
  auto contents = read_file(path);
  dao::SourceBuffer source(path.filename().string(), std::move(contents));
  auto lex_result = dao::lex(source);

  if (print_error_diagnostics(path.filename().string(), source,
                              lex_result.diagnostics)) {
    std::exit(EXIT_FAILURE);
  }

  return {.source = std::move(source), .lex_result = std::move(lex_result)};
}

struct ParsedFile {
  dao::SourceBuffer source;
  dao::LexResult lex_result;
  dao::ParseResult parse_result;
};

auto lex_and_parse(const std::filesystem::path& path) -> ParsedFile {
  auto lexed = lex_file(path);
  auto parse_result = dao::parse(lexed.lex_result.tokens);

  if (print_error_diagnostics(path.filename().string(), lexed.source,
                              parse_result.diagnostics)) {
    std::exit(EXIT_FAILURE);
  }

  return {.source = std::move(lexed.source),
          .lex_result = std::move(lexed.lex_result),
          .parse_result = std::move(parse_result)};
}

struct FrontendResult {
  ParsedFile parsed;
  dao::ResolveResult resolve;
  dao::TypeContext types;
  dao::TypeCheckResult typecheck;
};

// Run lex → parse → resolve → typecheck. Exits on error.
auto run_frontend(const std::filesystem::path& path) -> FrontendResult {
  auto parsed = lex_and_parse(path);
  if (parsed.parse_result.file == nullptr) {
    std::exit(EXIT_FAILURE);
  }

  auto filename = path.filename().string();
  auto resolve_result = dao::resolve(*parsed.parse_result.file);
  bool has_errors = print_error_diagnostics(filename, parsed.source,
                                            resolve_result.diagnostics);

  dao::TypeContext types;
  auto check_result =
      dao::typecheck(*parsed.parse_result.file, resolve_result, types);
  has_errors |= print_diagnostics(filename, parsed.source,
                                  check_result.diagnostics);

  if (has_errors) {
    std::exit(EXIT_FAILURE);
  }

  return {.parsed = std::move(parsed),
          .resolve = std::move(resolve_result),
          .types = std::move(types),
          .typecheck = std::move(check_result)};
}

struct HirResult {
  FrontendResult frontend;
  dao::HirContext hir_ctx;
  dao::HirBuildResult hir;
};

auto run_through_hir(const std::filesystem::path& path) -> HirResult {
  auto frontend = run_frontend(path);
  dao::HirContext hir_ctx;
  auto hir = dao::build_hir(*frontend.parsed.parse_result.file,
                            frontend.resolve, frontend.typecheck, hir_ctx);

  auto filename = path.filename().string();
  bool has_errors = print_error_diagnostics(filename, frontend.parsed.source,
                                            hir.diagnostics);

  if (hir.module == nullptr || has_errors) {
    std::exit(EXIT_FAILURE);
  }

  return {.frontend = std::move(frontend),
          .hir_ctx = std::move(hir_ctx),
          .hir = std::move(hir)};
}

struct MirResult {
  HirResult hir_result;
  dao::MirContext mir_ctx;
  dao::MirBuildResult mir;
};

auto run_through_mir(const std::filesystem::path& path) -> MirResult {
  auto hir_result = run_through_hir(path);
  dao::MirContext mir_ctx;
  auto mir = dao::build_mir(*hir_result.hir.module, mir_ctx,
                            hir_result.frontend.types);

  auto filename = path.filename().string();
  bool has_errors = print_error_diagnostics(
      filename, hir_result.frontend.parsed.source, mir.diagnostics);

  if (mir.module == nullptr || has_errors) {
    std::exit(EXIT_FAILURE);
  }

  return {.hir_result = std::move(hir_result),
          .mir_ctx = std::move(mir_ctx),
          .mir = std::move(mir)};
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

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

  if (print_error_diagnostics(path.filename().string(), source,
                              result.diagnostics)) {
    std::exit(EXIT_FAILURE);
  }
}

// Debug-only parse diagnostic dump. Output format is not stable.
void cmd_parse(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file != nullptr) {
    std::cout << "File: " << result.parse_result.file->imports.size() << " imports, "
              << result.parse_result.file->declarations.size() << " declarations\n";
  }
}

// Pretty-print AST. Output is deterministic and suitable for golden-file testing.
void cmd_ast(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file != nullptr) {
    dao::print_ast(std::cout, *result.parse_result.file);
  }
}

// Emit semantic token classification. Output is deterministic.
void cmd_tokens(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);

  // Run name resolution for resolve-driven classifications.
  dao::ResolveResult resolve_result;
  if (result.parse_result.file != nullptr) {
    resolve_result = dao::resolve(*result.parse_result.file);
  }

  auto sem_tokens =
      dao::classify_tokens(result.lex_result.tokens, result.parse_result.file, &resolve_result);

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

  // Print diagnostics (to stdout — this is a debug dump command).
  if (!resolve_result.diagnostics.empty()) {
    std::cout << "\nDiagnostics:\n";
    for (const auto& diag : resolve_result.diagnostics) {
      auto loc = result.source.line_col(diag.span.offset);
      std::cout << "  " << path.filename().string() << ":" << loc.line << ":" << loc.col
                << ": error: " << diag.message << "\n";
    }
  }
}

// Run type checking and print diagnostics.
void cmd_check(const std::filesystem::path& path) {
  run_frontend(path);
  std::cout << "ok\n";
}

// Build and print HIR. Output is deterministic.
void cmd_hir(const std::filesystem::path& path) {
  auto result = run_through_hir(path);
  if (result.hir.module != nullptr) {
    dao::print_hir(std::cout, *result.hir.module);
  }
}

// Build and print MIR. Output is deterministic.
void cmd_mir(const std::filesystem::path& path) {
  auto result = run_through_mir(path);
  if (result.mir.module != nullptr) {
    dao::print_mir(std::cout, *result.mir.module);
  }
}

// Build and emit LLVM IR. Output is deterministic.
void cmd_llvm_ir(const std::filesystem::path& path) {
  auto mir = run_through_mir(path);

  llvm::LLVMContext llvm_ctx;
  dao::LlvmBackend backend(llvm_ctx);
  auto llvm_result = backend.lower(*mir.mir.module);

  auto filename = path.filename().string();
  print_error_diagnostics(filename, mir.hir_result.frontend.parsed.source,
                          llvm_result.diagnostics);

  if (llvm_result.module == nullptr || !llvm_result.diagnostics.empty()) {
    std::exit(EXIT_FAILURE);
  }

  dao::LlvmBackend::print_ir(std::cout, *llvm_result.module);
}

// Compile a .dao file to a native executable.
void cmd_build(const std::filesystem::path& path) {
  auto mir = run_through_mir(path);

  llvm::LLVMContext llvm_ctx;
  dao::LlvmBackend backend(llvm_ctx);
  auto llvm_result = backend.lower(*mir.mir.module);

  auto filename = path.filename().string();
  print_error_diagnostics(filename, mir.hir_result.frontend.parsed.source,
                          llvm_result.diagnostics);

  if (llvm_result.module == nullptr || !llvm_result.diagnostics.empty()) {
    std::exit(EXIT_FAILURE);
  }

  // Initialize LLVM targets and emit object file.
  dao::LlvmBackend::initialize_targets();

  auto obj_path = std::filesystem::temp_directory_path() /
                  (path.stem().string() + ".o");
  std::string emit_error;
  if (!dao::LlvmBackend::emit_object(*llvm_result.module,
                                      obj_path.string(), emit_error)) {
    std::cerr << "error: " << emit_error << "\n";
    std::exit(EXIT_FAILURE);
  }

  // Link with system cc: object + runtime library → executable.
  auto output_path = path.parent_path() / path.stem();

  auto cc_path = llvm::sys::findProgramByName("cc");
  if (!cc_path) {
    std::cerr << "error: cannot find 'cc' linker: "
              << cc_path.getError().message() << "\n";
    std::filesystem::remove(obj_path);
    std::exit(EXIT_FAILURE);
  }

  // StringRef does not own data — keep string temporaries alive.
  auto obj_str = obj_path.string();
  auto out_str = output_path.string();
  std::vector<llvm::StringRef> args = {
      *cc_path,
      obj_str,
      DAO_RUNTIME_LIB,
      "-o",
      out_str,
  };

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
    Command{.name = "build", .handler = cmd_build},
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

  // daoc <file> — read and exit (Task 0 compat)
  std::filesystem::path path(arg1);
  if (!std::filesystem::exists(path)) {
    std::cerr << "error: file not found: " << path << "\n";
    return EXIT_FAILURE;
  }
  read_file(path);
  return EXIT_SUCCESS;
}
