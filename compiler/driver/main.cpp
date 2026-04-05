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
#include "ir/mir/mir_monomorphize.h"
#include "ir/mir/mir_printer.h"

#include <llvm/IR/LLVMContext.h>

#include <llvm/Support/Program.h>

#include <algorithm>
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
                             std::span<const dao::Diagnostic> diags,
                             uint32_t line_offset = 0) -> bool {
  for (const auto& diag : diags) {
    auto loc = source.line_col(diag.span.offset);
    auto line = loc.line > line_offset ? loc.line - line_offset : loc.line;
    std::cerr << filename << ":" << line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }
  return !diags.empty();
}

// Print diagnostics with severity labels. Returns true if any errors.
auto print_diagnostics(std::string_view filename,
                       const dao::SourceBuffer& source,
                       std::span<const dao::Diagnostic> diags,
                       uint32_t line_offset = 0) -> bool {
  bool has_errors = false;
  for (const auto& diag : diags) {
    auto loc = source.line_col(diag.span.offset);
    auto line = loc.line > line_offset ? loc.line - line_offset : loc.line;
    const auto* severity = diag.severity == dao::Severity::Error ? "error" : "warning";
    std::cerr << filename << ":" << line << ":" << loc.col
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

// ---------------------------------------------------------------------------
// Prelude loading — stdlib/core/*.dao source is prepended to user source
// so prelude symbols are available without explicit import.
// ---------------------------------------------------------------------------

// Strip a leading `module <path>\n` line (and any preceding blank or
// comment lines) from a source snippet. The transitional driver
// concatenates stdlib and user sources into a single synthetic
// compilation unit and injects one top-level `module` declaration of
// its own; per-file module headers on the inputs would otherwise
// violate the "exactly one module declaration at the start" rule in
// CONTRACT_SYNTAX_SURFACE.md. Real multi-file compilation lands with
// Task 25+.
auto strip_leading_module(std::string_view src) -> std::string_view {
  size_t i = 0;
  while (i < src.size() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n')) {
    ++i;
  }
  if (i + 6 >= src.size() || src.substr(i, 6) != "module") {
    return src;
  }
  char after = src[i + 6];
  if (after != ' ' && after != '\t') {
    return src;
  }
  auto nl = src.find('\n', i);
  if (nl == std::string_view::npos) {
    return src.substr(0, 0);
  }
  return src.substr(nl + 1);
}

auto load_prelude_source() -> std::string {
  std::filesystem::path root(DAO_SOURCE_DIR);
  std::string prelude;

  // Load stdlib directories in order: core first, then io.
  // concepts/ is not auto-loaded yet (Iterable needs more infra).
  const std::filesystem::path dirs[] = {
      root / "stdlib" / "core",
      root / "stdlib" / "io",
  };

  for (const auto& dir : dirs) {
    if (!std::filesystem::exists(dir)) {
      continue;
    }
    // Collect and sort entries so prelude loading order is stable
    // and dependency-aware (e.g. option.dao before overflow.dao).
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() == ".dao") {
        paths.push_back(entry.path());
      }
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& p : paths) {
      auto contents = read_file(p);
      prelude.append(strip_leading_module(contents));
      prelude += '\n';
    }
  }
  return prelude;
}

struct PreludeParsedFile {
  ParsedFile parsed;
  uint32_t prelude_lines = 0;
  uint32_t prelude_bytes = 0;
};

auto lex_and_parse_with_prelude(const std::filesystem::path& path)
    -> PreludeParsedFile {
  auto prelude_source = load_prelude_source();
  auto raw_user_source = read_file(path);
  // Strip the user file's own `module` header; the driver injects a
  // single synthetic `module main` at the top of the combined source.
  // This is transitional until Task 25+ introduces real multi-file
  // compilation where each file keeps its own module identity.
  auto user_source = std::string(strip_leading_module(raw_user_source));

  // Synthetic leading module declaration for the combined compilation
  // unit. Per CONTRACT_SYNTAX_SURFACE.md every source file must begin
  // with exactly one `module` declaration.
  const std::string module_header = "module main\n";

  std::string combined;
  combined.reserve(module_header.size() + prelude_source.size() + user_source.size());
  combined.append(module_header);
  combined.append(prelude_source);

  uint32_t prelude_lines = 0;
  for (char chr : combined) {
    if (chr == '\n') {
      ++prelude_lines;
    }
  }
  auto prelude_bytes = static_cast<uint32_t>(combined.size());

  combined.append(user_source);
  dao::SourceBuffer source(path.filename().string(), std::move(combined));
  auto lex_result = dao::lex(source);

  if (print_error_diagnostics(path.filename().string(), source,
                              lex_result.diagnostics, prelude_lines)) {
    std::exit(EXIT_FAILURE);
  }

  auto parse_result = dao::parse(lex_result.tokens);
  if (print_error_diagnostics(path.filename().string(), source,
                              parse_result.diagnostics, prelude_lines)) {
    std::exit(EXIT_FAILURE);
  }

  return {.parsed = {.source = std::move(source),
                     .lex_result = std::move(lex_result),
                     .parse_result = std::move(parse_result)},
          .prelude_lines = prelude_lines,
          .prelude_bytes = prelude_bytes};
}

struct FrontendResult {
  ParsedFile parsed;
  dao::ResolveResult resolve;
  dao::TypeContext types;
  dao::TypeCheckResult typecheck;
  uint32_t prelude_lines = 0;
  uint32_t prelude_bytes = 0;
};

// Run lex → parse → resolve → typecheck. Exits on error.
auto run_frontend(const std::filesystem::path& path) -> FrontendResult {
  auto preparsed = lex_and_parse_with_prelude(path);

  if (preparsed.parsed.parse_result.file == nullptr) {
    std::exit(EXIT_FAILURE);
  }

  auto filename = path.filename().string();
  auto resolve_result = dao::resolve(*preparsed.parsed.parse_result.file,
                                     preparsed.prelude_bytes);
  bool has_errors = print_error_diagnostics(
      filename, preparsed.parsed.source, resolve_result.diagnostics,
      preparsed.prelude_lines);

  dao::TypeContext types;
  auto check_result = dao::typecheck(*preparsed.parsed.parse_result.file,
                                     resolve_result, types);
  has_errors |= print_diagnostics(filename, preparsed.parsed.source,
                                  check_result.diagnostics,
                                  preparsed.prelude_lines);

  if (has_errors) {
    std::exit(EXIT_FAILURE);
  }

  return {.parsed = std::move(preparsed.parsed),
          .resolve = std::move(resolve_result),
          .types = std::move(types),
          .typecheck = std::move(check_result),
          .prelude_lines = preparsed.prelude_lines,
          .prelude_bytes = preparsed.prelude_bytes};
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
                                            hir.diagnostics,
                                            frontend.prelude_lines);

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
      filename, hir_result.frontend.parsed.source, mir.diagnostics,
      hir_result.frontend.prelude_lines);

  if (mir.module == nullptr || has_errors) {
    std::exit(EXIT_FAILURE);
  }

  // Monomorphize generic functions before LLVM lowering.
  auto mono_result =
      dao::monomorphize(*mir.module, mir_ctx, hir_result.frontend.types);
  if (!mono_result.diagnostics.empty()) {
    print_error_diagnostics(filename, hir_result.frontend.parsed.source,
                            mono_result.diagnostics,
                            hir_result.frontend.prelude_lines);
  }

  return {.hir_result = std::move(hir_result),
          .mir_ctx = std::move(mir_ctx),
          .mir = std::move(mir)};
}

// Lower MIR to LLVM IR, check diagnostics, and exit on error.
auto lower_to_llvm(const MirResult& mir, llvm::LLVMContext& llvm_ctx,
                   const std::filesystem::path& path) -> dao::LlvmBackendResult {
  dao::LlvmBackend backend(llvm_ctx);
  auto result = backend.lower(*mir.mir.module,
                               mir.hir_result.frontend.prelude_bytes);

  // Filter out prelude-origin warnings — they are expected (e.g. range's
  // generator return type can't be lowered yet) and not actionable by
  // the user. Prelude errors were already downgraded to warnings by the
  // backend, so any warning in the prelude region is safe to suppress.
  auto prelude_bytes = mir.hir_result.frontend.prelude_bytes;
  std::vector<dao::Diagnostic> user_diags;
  for (const auto& diag : result.diagnostics) {
    if (diag.severity == dao::Severity::Warning &&
        diag.span.offset < prelude_bytes) {
      continue;
    }
    user_diags.push_back(diag);
  }

  auto filename = path.filename().string();
  bool has_errors = print_diagnostics(filename, mir.hir_result.frontend.parsed.source,
                                      user_diags,
                                      mir.hir_result.frontend.prelude_lines);

  if (result.module == nullptr || has_errors) {
    std::exit(EXIT_FAILURE);
  }

  return result;
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
  auto result = lex_and_parse_with_prelude(path);

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
  auto result = lex_and_parse_with_prelude(path);
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

  // Print diagnostics in user region (to stdout — this is a debug dump command).
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
  // Initialize targets so the module gets a correct DataLayout
  // for ABI-sensitive lowering (struct coercion, alignment).
  dao::LlvmBackend::initialize_targets();

  auto mir = run_through_mir(path);
  llvm::LLVMContext llvm_ctx;
  auto llvm_result = lower_to_llvm(mir, llvm_ctx, path);
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

  auto mir = run_through_mir(path);
  llvm::LLVMContext llvm_ctx;
  auto llvm_result = lower_to_llvm(mir, llvm_ctx, path);

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

  // build — accepts extra link inputs after the source file.
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

  // daoc <file> — read and exit (Task 0 compat)
  std::filesystem::path path(arg1);
  if (!std::filesystem::exists(path)) {
    std::cerr << "error: file not found: " << path << "\n";
    return EXIT_FAILURE;
  }
  read_file(path);
  return EXIT_SUCCESS;
}
