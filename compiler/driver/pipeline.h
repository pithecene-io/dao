#ifndef DAO_DRIVER_PIPELINE_H
#define DAO_DRIVER_PIPELINE_H

#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"
#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/type_checker.h"
#include "frontend/types/type_context.h"
#include "ir/hir/hir_builder.h"
#include "ir/hir/hir_context.h"
#include "ir/mir/mir_builder.h"
#include "ir/mir/mir_context.h"
#include "backend/llvm/llvm_backend.h"

#include <llvm/IR/LLVMContext.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace dao {

// ---------------------------------------------------------------------------
// Diagnostic helpers
// ---------------------------------------------------------------------------

// Print all diagnostics as errors. Returns true if any were printed.
auto print_error_diagnostics(std::string_view filename,
                             const SourceBuffer& source,
                             std::span<const Diagnostic> diags,
                             uint32_t line_offset = 0) -> bool;

// Print diagnostics with severity labels. Returns true if any errors.
auto print_diagnostics(std::string_view filename,
                       const SourceBuffer& source,
                       std::span<const Diagnostic> diags,
                       uint32_t line_offset = 0) -> bool;

// ---------------------------------------------------------------------------
// Pipeline result structs
// ---------------------------------------------------------------------------

struct LexedFile {
  SourceBuffer source;
  LexResult lex_result;
};

struct ParsedFile {
  SourceBuffer source;
  LexResult lex_result;
  ParseResult parse_result;
};

struct PreludeParsedFile {
  ParsedFile parsed;
  uint32_t prelude_lines = 0;
  uint32_t prelude_bytes = 0;
};

struct FrontendResult {
  ParsedFile parsed;
  ResolveResult resolve;
  TypeContext types;
  TypeCheckResult typecheck;
  uint32_t prelude_lines = 0;
  uint32_t prelude_bytes = 0;
};

struct HirResult {
  FrontendResult frontend;
  HirContext hir_ctx;
  HirBuildResult hir;
};

struct MirResult {
  HirResult hir_result;
  MirContext mir_ctx;
  MirBuildResult mir;
};

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

auto read_file(const std::filesystem::path& path) -> std::string;

// ---------------------------------------------------------------------------
// Pipeline stage functions
// ---------------------------------------------------------------------------

auto lex_file(const std::filesystem::path& path) -> LexedFile;
auto lex_and_parse(const std::filesystem::path& path) -> ParsedFile;
auto load_prelude_source() -> std::string;
auto lex_and_parse_with_prelude(const std::filesystem::path& path)
    -> PreludeParsedFile;
auto run_frontend(const std::filesystem::path& path) -> FrontendResult;
auto run_through_hir(const std::filesystem::path& path) -> HirResult;
auto run_through_mir(const std::filesystem::path& path) -> MirResult;
auto lower_to_llvm(const MirResult& mir, llvm::LLVMContext& llvm_ctx,
                   const std::filesystem::path& path) -> LlvmBackendResult;

} // namespace dao

#endif // DAO_DRIVER_PIPELINE_H
