// NOLINTBEGIN(readability-magic-numbers)
#include "analyze.h"
#include "pipeline.h"
#include "token_category.h"

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

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

namespace dao::playground {

// ---------------------------------------------------------------------------
// Token serialization (user-visible tokens only, prelude filtered)
// ---------------------------------------------------------------------------

namespace {

void build_token_array(nlohmann::json& out, const LexResult& lex_result,
                       const SourceBuffer& source, uint32_t prelude_bytes,
                       uint32_t prelude_lines) {
  for (const auto& tok : lex_result.tokens) {
    if (is_synthetic_token(tok.kind) || tok.span.offset < prelude_bytes) {
      continue;
    }
    auto loc = source.line_col(tok.span.offset);
    auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
    out.push_back({
        {"kind", token_kind_name(tok.kind)},
        {"category", token_category(tok.kind)},
        {"offset", tok.span.offset - prelude_bytes},
        {"length", tok.span.length},
        {"line", line},
        {"col", loc.col},
        {"text", std::string(tok.text)},
    });
  }
}

void build_semantic_tokens(nlohmann::json& out,
                           const std::vector<SemanticToken>& sem_tokens,
                           const SourceBuffer& source,
                           uint32_t prelude_bytes, uint32_t prelude_lines) {
  for (const auto& stok : sem_tokens) {
    if (stok.span.offset < prelude_bytes) {
      continue;
    }
    auto loc = source.line_col(stok.span.offset);
    auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
    out.push_back({
        {"kind", stok.kind},
        {"offset", stok.span.offset - prelude_bytes},
        {"length", stok.span.length},
        {"line", line},
        {"col", loc.col},
    });
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Main handler
// ---------------------------------------------------------------------------

void handle_analyze(const httplib::Request& req, httplib::Response& res,
                    const std::filesystem::path& repo_root) {
  nlohmann::json request;
  try {
    request = nlohmann::json::parse(req.body);
  } catch (const nlohmann::json::parse_error&) {
    res.status = 400;
    res.set_content(R"({"error":"invalid JSON"})", "application/json");
    return;
  }

  if (!request.contains("source") || !request["source"].is_string()) {
    res.status = 400;
    res.set_content(R"({"error":"missing 'source' field"})", "application/json");
    return;
  }

  // --- Setup ---
  auto prelude_source = load_prelude(repo_root);
  auto prelude_bytes = static_cast<uint32_t>(prelude_source.size());
  auto prelude_lines = count_lines(prelude_source);

  auto user_source = request["source"].get<std::string>();
  SourceBuffer source("<playground>",  prelude_source + user_source);

  // --- Response accumulators ---
  nlohmann::json tokens = nlohmann::json::array();
  nlohmann::json diagnostics = nlohmann::json::array();
  nlohmann::json semantic_tokens_json = nlohmann::json::array();
  std::string ast_text;
  std::string hir_text;
  std::string mir_text;
  std::string llvm_ir_text;

  // --- Lex ---
  auto lex_result = lex(source);
  build_token_array(tokens, lex_result, source, prelude_bytes, prelude_lines);
  collect_diagnostics(diagnostics, source, lex_result.diagnostics,
                      prelude_bytes, prelude_lines);

  if (has_user_error(lex_result.diagnostics, prelude_bytes)) {
    goto respond; // NOLINT(cppcoreguidelines-avoid-goto)
  }

  {
    // --- Parse ---
    auto parse_result = parse(lex_result.tokens);
    collect_diagnostics(diagnostics, source, parse_result.diagnostics,
                        prelude_bytes, prelude_lines);

    if (has_user_error(parse_result.diagnostics, prelude_bytes) ||
        parse_result.file == nullptr) {
      goto respond; // NOLINT(cppcoreguidelines-avoid-goto)
    }

    if (diagnostics.empty()) {
      std::ostringstream ast_out;
      print_ast(ast_out, *parse_result.file);
      ast_text = ast_out.str();
    }

    // --- Resolve ---
    auto resolve_result = resolve(*parse_result.file, prelude_bytes);
    collect_diagnostics(diagnostics, source, resolve_result.diagnostics,
                        prelude_bytes, prelude_lines);

    // Semantic tokens — always classify when lex/parse succeeded.
    auto sem_tokens =
        classify_tokens(lex_result.tokens, parse_result.file, &resolve_result);
    build_semantic_tokens(semantic_tokens_json, sem_tokens, source,
                          prelude_bytes, prelude_lines);

    if (has_user_error(resolve_result.diagnostics, prelude_bytes)) {
      goto respond; // NOLINT(cppcoreguidelines-avoid-goto)
    }

    // --- Typecheck ---
    TypeContext types;
    auto check_result =
        typecheck(*parse_result.file, resolve_result, types);
    collect_diagnostics(diagnostics, source, check_result.diagnostics,
                        prelude_bytes, prelude_lines);

    bool has_tc_errors = false;
    for (const auto& diag : check_result.diagnostics) {
      if (diag.span.offset >= prelude_bytes &&
          diag.severity == Severity::Error) {
        has_tc_errors = true;
      }
    }
    if (has_tc_errors) {
      goto respond; // NOLINT(cppcoreguidelines-avoid-goto)
    }

    // --- HIR ---
    HirContext hir_ctx;
    auto hir_result = build_hir(*parse_result.file, resolve_result,
                                check_result, hir_ctx);
    collect_diagnostics(diagnostics, source, hir_result.diagnostics,
                        prelude_bytes, prelude_lines);

    if (hir_result.module == nullptr) {
      if (!has_user_error(hir_result.diagnostics, prelude_bytes)) {
        diagnostics.push_back(
            make_internal_error("HIR lowering failed (possible prelude error)"));
      }
      goto respond; // NOLINT(cppcoreguidelines-avoid-goto)
    }

    std::ostringstream hir_out;
    print_hir(hir_out, *hir_result.module);
    hir_text = hir_out.str();

    // --- MIR ---
    MirContext mir_ctx;
    auto mir_result = build_mir(*hir_result.module, mir_ctx, types);
    collect_diagnostics(diagnostics, source, mir_result.diagnostics,
                        prelude_bytes, prelude_lines);

    if (mir_result.module == nullptr) {
      if (!has_user_error(mir_result.diagnostics, prelude_bytes)) {
        diagnostics.push_back(
            make_internal_error("MIR lowering failed (possible prelude error)"));
      }
      goto respond; // NOLINT(cppcoreguidelines-avoid-goto)
    }

    monomorphize(*mir_result.module, mir_ctx, types);

    std::ostringstream mir_out;
    print_mir(mir_out, *mir_result.module);
    mir_text = mir_out.str();

    // --- LLVM IR ---
    llvm::LLVMContext llvm_ctx;
    LlvmBackend llvm_backend(llvm_ctx);
    auto llvm_result = llvm_backend.lower(*mir_result.module, prelude_bytes);
    collect_diagnostics(diagnostics, source, llvm_result.diagnostics,
                        prelude_bytes, prelude_lines);

    if (llvm_result.module != nullptr &&
        !has_user_error(llvm_result.diagnostics, prelude_bytes)) {
      std::ostringstream llvm_out;
      LlvmBackend::print_ir(llvm_out, *llvm_result.module);
      llvm_ir_text = llvm_out.str();
    }
  }

respond:
  nlohmann::json response = {
      {"tokens", tokens},
      {"semanticTokens", semantic_tokens_json},
      {"ast", ast_text},
      {"hir", hir_text},
      {"mir", mir_text},
      {"llvm_ir", llvm_ir_text},
      {"diagnostics", diagnostics},
  };

  res.set_content(response.dump(), "application/json");
}

// NOLINTEND(readability-magic-numbers)

} // namespace dao::playground
