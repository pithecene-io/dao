#include "driver/pipeline.h"

#include "ir/hir/hir_builder.h"
#include "ir/mir/mir_builder.h"
#include "ir/mir/mir_monomorphize.h"
#include "support/module_utils.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace dao {

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

auto print_error_diagnostics(std::string_view filename,
                             const SourceBuffer& source,
                             std::span<const Diagnostic> diags,
                             uint32_t line_offset) -> bool {
  for (const auto& diag : diags) {
    auto loc = source.line_col(diag.span.offset);
    auto line = loc.line > line_offset ? loc.line - line_offset : loc.line;
    std::cerr << filename << ":" << line << ":" << loc.col
              << ": error: " << diag.message << "\n";
  }
  return !diags.empty();
}

auto print_diagnostics(std::string_view filename,
                       const SourceBuffer& source,
                       std::span<const Diagnostic> diags,
                       uint32_t line_offset) -> bool {
  bool has_errors = false;
  for (const auto& diag : diags) {
    auto loc = source.line_col(diag.span.offset);
    auto line = loc.line > line_offset ? loc.line - line_offset : loc.line;
    const auto* severity = diag.severity == Severity::Error ? "error" : "warning";
    std::cerr << filename << ":" << line << ":" << loc.col
              << ": " << severity << ": " << diag.message << "\n";
    if (diag.severity == Severity::Error) {
      has_errors = true;
    }
  }
  return has_errors;
}

// ---------------------------------------------------------------------------
// Pipeline stages
// ---------------------------------------------------------------------------

auto lex_file(const std::filesystem::path& path) -> LexedFile {
  auto contents = read_file(path);
  SourceBuffer source(path.filename().string(), std::move(contents));
  auto lex_result = lex(source);

  if (print_error_diagnostics(path.filename().string(), source,
                              lex_result.diagnostics)) {
    std::exit(EXIT_FAILURE);
  }

  return {.source = std::move(source), .lex_result = std::move(lex_result)};
}

auto lex_and_parse(const std::filesystem::path& path) -> ParsedFile {
  auto lexed = lex_file(path);
  auto parse_result = parse(lexed.lex_result.tokens);

  if (print_error_diagnostics(path.filename().string(), lexed.source,
                              parse_result.diagnostics)) {
    std::exit(EXIT_FAILURE);
  }

  return {.source = std::move(lexed.source),
          .lex_result = std::move(lexed.lex_result),
          .parse_result = std::move(parse_result)};
}

// ---------------------------------------------------------------------------
// Prelude loading
// ---------------------------------------------------------------------------

auto load_prelude_source() -> std::string {
  std::filesystem::path root(DAO_SOURCE_DIR);
  std::string prelude;

  const std::filesystem::path dirs[] = {
      root / "stdlib" / "core",
      root / "stdlib" / "io",
  };

  for (const auto& dir : dirs) {
    if (!std::filesystem::exists(dir)) {
      continue;
    }
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() == ".dao") {
        paths.push_back(entry.path());
      }
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& p : paths) {
      auto contents = read_file(p);
      blank_leading_module(contents);
      prelude.append(contents);
      prelude += '\n';
    }
  }
  return prelude;
}

auto lex_and_parse_with_prelude(const std::filesystem::path& path)
    -> PreludeParsedFile {
  auto prelude_source = load_prelude_source();
  auto user_source = read_file(path);
  blank_leading_module(user_source);

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
  SourceBuffer source(path.filename().string(), std::move(combined));
  auto lex_result = lex(source);

  if (print_error_diagnostics(path.filename().string(), source,
                              lex_result.diagnostics, prelude_lines)) {
    std::exit(EXIT_FAILURE);
  }

  auto parse_result = parse(lex_result.tokens);
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

auto run_frontend(const std::filesystem::path& path) -> FrontendResult {
  auto preparsed = lex_and_parse_with_prelude(path);

  if (preparsed.parsed.parse_result.file == nullptr) {
    std::exit(EXIT_FAILURE);
  }

  auto filename = path.filename().string();
  auto resolve_result = resolve(*preparsed.parsed.parse_result.file,
                                preparsed.prelude_bytes);
  bool has_errors = print_error_diagnostics(
      filename, preparsed.parsed.source, resolve_result.diagnostics,
      preparsed.prelude_lines);

  TypeContext types;
  auto check_result = typecheck(*preparsed.parsed.parse_result.file,
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

auto run_through_hir(const std::filesystem::path& path) -> HirResult {
  auto frontend = run_frontend(path);
  HirContext hir_ctx;
  auto hir = build_hir(*frontend.parsed.parse_result.file,
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

auto run_through_mir(const std::filesystem::path& path) -> MirResult {
  auto hir_result = run_through_hir(path);
  MirContext mir_ctx;
  auto mir = build_mir(*hir_result.hir.module, mir_ctx,
                       hir_result.frontend.types);

  auto filename = path.filename().string();
  bool has_errors = print_error_diagnostics(
      filename, hir_result.frontend.parsed.source, mir.diagnostics,
      hir_result.frontend.prelude_lines);

  if (mir.module == nullptr || has_errors) {
    std::exit(EXIT_FAILURE);
  }

  auto mono_result =
      monomorphize(*mir.module, mir_ctx, hir_result.frontend.types,
                   mir.generic_templates);
  if (!mono_result.diagnostics.empty()) {
    bool mono_errors = print_error_diagnostics(
        filename, hir_result.frontend.parsed.source,
        mono_result.diagnostics, hir_result.frontend.prelude_lines);
    // Monomorphization emits errors for MIR concreteness invariant
    // violations (Task 28 §14.2).  These must halt the pipeline
    // before LLVM lowering — allowing generic residue through would
    // surface as an opaque LLVM DataLayout assertion on unsized
    // types, obscuring the root cause.
    if (mono_errors) {
      std::exit(EXIT_FAILURE);
    }
  }

  return {.hir_result = std::move(hir_result),
          .mir_ctx = std::move(mir_ctx),
          .mir = std::move(mir)};
}

auto lower_to_llvm(const MirResult& mir, llvm::LLVMContext& llvm_ctx,
                   const std::filesystem::path& path) -> LlvmBackendResult {
  LlvmBackend backend(llvm_ctx);
  auto result = backend.lower(*mir.mir.module,
                               mir.hir_result.frontend.prelude_bytes);

  auto prelude_bytes = mir.hir_result.frontend.prelude_bytes;
  std::vector<Diagnostic> user_diags;
  for (const auto& diag : result.diagnostics) {
    if (diag.severity == Severity::Warning &&
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

} // namespace dao
