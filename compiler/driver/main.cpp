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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

struct LexedFile {
  dao::SourceBuffer source;
  dao::LexResult lex_result;
};

// Lex a file, printing diagnostics on failure.
auto lex_file(const std::filesystem::path& path) -> LexedFile {
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

  return {.source = std::move(source), .lex_result = std::move(lex_result)};
}

struct ParsedFile {
  dao::SourceBuffer source;
  dao::LexResult lex_result;
  dao::ParseResult parse_result;
};

// Lex and parse a file, printing diagnostics on failure.
auto lex_and_parse(const std::filesystem::path& path) -> ParsedFile {
  auto lexed = lex_file(path);
  auto parse_result = dao::parse(lexed.lex_result.tokens);

  if (!parse_result.diagnostics.empty()) {
    for (const auto& diag : parse_result.diagnostics) {
      auto loc = lexed.source.line_col(diag.span.offset);
      std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
                << ": error: " << diag.message << "\n";
    }
    std::exit(EXIT_FAILURE);
  }

  return {.source = std::move(lexed.source),
          .lex_result = std::move(lexed.lex_result),
          .parse_result = std::move(parse_result)};
}

// Debug-only parse diagnostic dump. Output format is not stable.
void cmd_parse(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file != nullptr) {
    std::cout << "File: " << result.parse_result.file->imports.size() << " imports, "
              << result.parse_result.file->declarations.size() << " declarations\n";
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

  // Print diagnostics.
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
  auto result = lex_and_parse(path);
  if (result.parse_result.file == nullptr) {
    return;
  }

  auto resolve_result = dao::resolve(*result.parse_result.file);

  // Print resolve diagnostics.
  for (const auto& diag : resolve_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }

  dao::TypeContext types;
  auto check_result =
      dao::typecheck(*result.parse_result.file, resolve_result, types);

  bool has_errors = !resolve_result.diagnostics.empty();

  for (const auto& diag : check_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    auto severity = diag.severity == dao::Severity::Error ? "error" : "warning";
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": " << severity << ": " << diag.message << "\n";
    if (diag.severity == dao::Severity::Error) {
      has_errors = true;
    }
  }

  if (has_errors) {
    std::exit(EXIT_FAILURE);
  }

  std::cout << "ok\n";
}

// Build and print HIR. Output is deterministic.
void cmd_hir(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file == nullptr) {
    return;
  }

  auto resolve_result = dao::resolve(*result.parse_result.file);

  // Print resolve diagnostics.
  for (const auto& diag : resolve_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }

  dao::TypeContext types;
  auto check_result =
      dao::typecheck(*result.parse_result.file, resolve_result, types);

  bool has_errors = !resolve_result.diagnostics.empty();

  for (const auto& diag : check_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    auto severity = diag.severity == dao::Severity::Error ? "error" : "warning";
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": " << severity << ": " << diag.message << "\n";
    if (diag.severity == dao::Severity::Error) {
      has_errors = true;
    }
  }

  if (has_errors) {
    std::exit(EXIT_FAILURE);
  }

  dao::HirContext hir_ctx;
  auto hir_result = dao::build_hir(*result.parse_result.file, resolve_result,
                                   check_result, hir_ctx);

  for (const auto& diag : hir_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }

  if (hir_result.module != nullptr) {
    dao::print_hir(std::cout, *hir_result.module);
  }
}

// Build and print MIR. Output is deterministic.
void cmd_mir(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file == nullptr) {
    return;
  }

  auto resolve_result = dao::resolve(*result.parse_result.file);

  for (const auto& diag : resolve_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }

  dao::TypeContext types;
  auto check_result =
      dao::typecheck(*result.parse_result.file, resolve_result, types);

  bool has_errors = !resolve_result.diagnostics.empty();

  for (const auto& diag : check_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    auto severity = diag.severity == dao::Severity::Error ? "error" : "warning";
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": " << severity << ": " << diag.message << "\n";
    if (diag.severity == dao::Severity::Error) {
      has_errors = true;
    }
  }

  if (has_errors) {
    std::exit(EXIT_FAILURE);
  }

  dao::HirContext hir_ctx;
  auto hir_result = dao::build_hir(*result.parse_result.file, resolve_result,
                                   check_result, hir_ctx);

  if (hir_result.module == nullptr) {
    return;
  }

  dao::MirContext mir_ctx;
  auto mir_result = dao::build_mir(*hir_result.module, mir_ctx, types);

  for (const auto& diag : mir_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }

  if (mir_result.module != nullptr) {
    dao::print_mir(std::cout, *mir_result.module);
  }
}

// Build and emit LLVM IR. Output is deterministic.
void cmd_llvm_ir(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file == nullptr) {
    return;
  }

  auto resolve_result = dao::resolve(*result.parse_result.file);

  for (const auto& diag : resolve_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }

  dao::TypeContext types;
  auto check_result =
      dao::typecheck(*result.parse_result.file, resolve_result, types);

  bool has_errors = !resolve_result.diagnostics.empty();

  for (const auto& diag : check_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    auto severity = diag.severity == dao::Severity::Error ? "error" : "warning";
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": " << severity << ": " << diag.message << "\n";
    if (diag.severity == dao::Severity::Error) {
      has_errors = true;
    }
  }

  if (has_errors) {
    std::exit(EXIT_FAILURE);
  }

  dao::HirContext hir_ctx;
  auto hir_result = dao::build_hir(*result.parse_result.file, resolve_result,
                                   check_result, hir_ctx);

  if (hir_result.module == nullptr) {
    return;
  }

  dao::MirContext mir_ctx;
  auto mir_result = dao::build_mir(*hir_result.module, mir_ctx, types);

  for (const auto& diag : mir_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
    has_errors = true;
  }

  if (mir_result.module == nullptr || has_errors) {
    std::exit(EXIT_FAILURE);
  }

  llvm::LLVMContext llvm_ctx;
  dao::LlvmBackend backend(llvm_ctx);
  auto llvm_result = backend.lower(*mir_result.module);

  for (const auto& diag : llvm_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }

  if (llvm_result.module == nullptr || !llvm_result.diagnostics.empty()) {
    std::exit(EXIT_FAILURE);
  }

  dao::LlvmBackend::print_ir(std::cout, *llvm_result.module);
}

// Compile a .dao file to a native executable.
void cmd_build(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file == nullptr) {
    return;
  }

  auto resolve_result = dao::resolve(*result.parse_result.file);

  for (const auto& diag : resolve_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }

  dao::TypeContext types;
  auto check_result =
      dao::typecheck(*result.parse_result.file, resolve_result, types);

  bool has_errors = !resolve_result.diagnostics.empty();

  for (const auto& diag : check_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    auto severity = diag.severity == dao::Severity::Error ? "error" : "warning";
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": " << severity << ": " << diag.message << "\n";
    if (diag.severity == dao::Severity::Error) {
      has_errors = true;
    }
  }

  if (has_errors) {
    std::exit(EXIT_FAILURE);
  }

  dao::HirContext hir_ctx;
  auto hir_result = dao::build_hir(*result.parse_result.file, resolve_result,
                                   check_result, hir_ctx);

  if (hir_result.module == nullptr) {
    std::exit(EXIT_FAILURE);
  }

  dao::MirContext mir_ctx;
  auto mir_result = dao::build_mir(*hir_result.module, mir_ctx, types);

  for (const auto& diag : mir_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
    has_errors = true;
  }

  if (mir_result.module == nullptr || has_errors) {
    std::exit(EXIT_FAILURE);
  }

  llvm::LLVMContext llvm_ctx;
  dao::LlvmBackend backend(llvm_ctx);
  auto llvm_result = backend.lower(*mir_result.module);

  for (const auto& diag : llvm_result.diagnostics) {
    auto loc = result.source.line_col(diag.span.offset);
    std::cerr << path.filename().string() << ":" << loc.line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }

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

  auto cc = llvm::sys::findProgramByName("cc");
  if (!cc) {
    std::cerr << "error: cannot find 'cc' linker: "
              << cc.getError().message() << "\n";
    std::filesystem::remove(obj_path);
    std::exit(EXIT_FAILURE);
  }

  // StringRef does not own data — keep string temporaries alive.
  auto obj_str = obj_path.string();
  auto out_str = output_path.string();
  std::vector<llvm::StringRef> args = {
      *cc,
      obj_str,
      DAO_RUNTIME_LIB,
      "-o",
      out_str,
  };

  std::string link_error;
  int link_status = llvm::sys::ExecuteAndWait(
      *cc, args, /*Env=*/std::nullopt, /*Redirects=*/{},
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

// Pretty-print AST. Output is deterministic and suitable for golden-file testing.
void cmd_ast(const std::filesystem::path& path) {
  auto result = lex_and_parse(path);
  if (result.parse_result.file != nullptr) {
    dao::print_ast(std::cout, *result.parse_result.file);
  }
}

} // namespace

auto main(int argc, char* argv[]) -> int {
  if (argc < 2) {
    std::cerr << "usage: daoc <command> <file>\n";
    std::cerr << "commands: lex, parse, ast, tokens, resolve, check, hir, mir, llvm-ir, build\n";
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

  // daoc tokens <file>
  if (arg1 == "tokens") {
    if (argc < 3) {
      std::cerr << "usage: daoc tokens <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path tokens_path(argv[2]);
    if (!std::filesystem::exists(tokens_path)) {
      std::cerr << "error: file not found: " << tokens_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_tokens(tokens_path);
    return EXIT_SUCCESS;
  }

  // daoc resolve <file>
  if (arg1 == "resolve") {
    if (argc < 3) {
      std::cerr << "usage: daoc resolve <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path resolve_path(argv[2]);
    if (!std::filesystem::exists(resolve_path)) {
      std::cerr << "error: file not found: " << resolve_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_resolve(resolve_path);
    return EXIT_SUCCESS;
  }

  // daoc check <file>
  if (arg1 == "check") {
    if (argc < 3) {
      std::cerr << "usage: daoc check <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path check_path(argv[2]);
    if (!std::filesystem::exists(check_path)) {
      std::cerr << "error: file not found: " << check_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_check(check_path);
    return EXIT_SUCCESS;
  }

  // daoc hir <file>
  if (arg1 == "hir") {
    if (argc < 3) {
      std::cerr << "usage: daoc hir <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path hir_path(argv[2]);
    if (!std::filesystem::exists(hir_path)) {
      std::cerr << "error: file not found: " << hir_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_hir(hir_path);
    return EXIT_SUCCESS;
  }

  // daoc mir <file>
  if (arg1 == "mir") {
    if (argc < 3) {
      std::cerr << "usage: daoc mir <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path mir_path(argv[2]);
    if (!std::filesystem::exists(mir_path)) {
      std::cerr << "error: file not found: " << mir_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_mir(mir_path);
    return EXIT_SUCCESS;
  }

  // daoc llvm-ir <file>
  if (arg1 == "llvm-ir") {
    if (argc < 3) {
      std::cerr << "usage: daoc llvm-ir <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path llvm_ir_path(argv[2]);
    if (!std::filesystem::exists(llvm_ir_path)) {
      std::cerr << "error: file not found: " << llvm_ir_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_llvm_ir(llvm_ir_path);
    return EXIT_SUCCESS;
  }

  // daoc build <file>
  if (arg1 == "build") {
    if (argc < 3) {
      std::cerr << "usage: daoc build <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path build_path(argv[2]);
    if (!std::filesystem::exists(build_path)) {
      std::cerr << "error: file not found: " << build_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_build(build_path);
    return EXIT_SUCCESS;
  }

  // daoc ast <file>
  if (arg1 == "ast") {
    if (argc < 3) {
      std::cerr << "usage: daoc ast <file>\n";
      return EXIT_FAILURE;
    }
    std::filesystem::path ast_path(argv[2]);
    if (!std::filesystem::exists(ast_path)) {
      std::cerr << "error: file not found: " << ast_path << "\n";
      return EXIT_FAILURE;
    }
    cmd_ast(ast_path);
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
