// NOLINTBEGIN(readability-magic-numbers)
#include "analyze.h"
#include "pipeline.h"
#include "token_category.h"

#include "analysis/document_symbols.h"
#include "analysis/goto_definition.h"
#include "analysis/hover.h"
#include "analysis/references.h"
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

    auto mono_result = monomorphize(*mir_result.module, mir_ctx, types);
    collect_diagnostics(diagnostics, source, mono_result.diagnostics,
                        prelude_bytes, prelude_lines);

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

// ---------------------------------------------------------------------------
// Shared lightweight pipeline for hover/goto-def (lex → parse → resolve → typecheck)
// ---------------------------------------------------------------------------

namespace {

struct LightPipeline {
  SourceBuffer source;
  LexResult lex_result;
  ParseResult parse_result;
  ResolveResult resolve_result;
  TypeCheckResult check_result;
  TypeContext types;
  uint32_t prelude_bytes = 0;
  bool ok = false;
};

auto run_light_pipeline(const nlohmann::json& request,
                        const std::filesystem::path& repo_root)
    -> LightPipeline {
  LightPipeline pipe{SourceBuffer("", ""), {}, {}, {}, {}, {}, 0, false};

  auto prelude_source = load_prelude(repo_root);
  pipe.prelude_bytes = static_cast<uint32_t>(prelude_source.size());

  auto user_source = request["source"].get<std::string>();
  pipe.source = SourceBuffer("<playground>", prelude_source + user_source);
  pipe.lex_result = lex(pipe.source);

  if (!pipe.lex_result.diagnostics.empty()) {
    return pipe;
  }

  pipe.parse_result = parse(pipe.lex_result.tokens);
  if (pipe.parse_result.file == nullptr) {
    return pipe;
  }

  pipe.resolve_result = resolve(*pipe.parse_result.file, pipe.prelude_bytes);

  pipe.check_result =
      typecheck(*pipe.parse_result.file, pipe.resolve_result, pipe.types);

  pipe.ok = true;
  return pipe;
}

/// Find the token span offset that contains the given byte offset.
/// Returns the token's span.offset, or the offset itself if no token found.
auto find_token_offset(uint32_t offset, const LexResult& lex_result)
    -> uint32_t {
  for (const auto& tok : lex_result.tokens) {
    if (tok.span.offset <= offset &&
        offset < tok.span.offset + tok.span.length) {
      return tok.span.offset;
    }
  }
  return offset;
}

} // namespace

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

void handle_hover(const httplib::Request& req, httplib::Response& res,
                  const std::filesystem::path& repo_root) {
  nlohmann::json request;
  try {
    request = nlohmann::json::parse(req.body);
  } catch (const nlohmann::json::parse_error&) {
    res.status = 400;
    res.set_content(R"({"error":"invalid JSON"})", "application/json");
    return;
  }

  if (!request.contains("source") || !request.contains("offset")) {
    res.status = 400;
    res.set_content(R"({"error":"missing 'source' or 'offset'"})",
                    "application/json");
    return;
  }

  auto user_offset = request["offset"].get<uint32_t>();
  auto pipe = run_light_pipeline(request, repo_root);

  if (!pipe.ok) {
    res.set_content("null", "application/json");
    return;
  }

  // Find the token at this offset and use its span start for lookup.
  auto absolute_offset = pipe.prelude_bytes + user_offset;
  auto token_offset = find_token_offset(absolute_offset, pipe.lex_result);

  auto result = dao::query_hover(token_offset, pipe.resolve_result,
                                 pipe.check_result);
  if (!result) {
    res.set_content("null", "application/json");
    return;
  }

  nlohmann::json response = {
      {"name", result->name},
      {"kind", result->symbol_kind},
      {"type", result->type},
  };
  res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// Go to definition
// ---------------------------------------------------------------------------

void handle_goto_def(const httplib::Request& req, httplib::Response& res,
                     const std::filesystem::path& repo_root) {
  nlohmann::json request;
  try {
    request = nlohmann::json::parse(req.body);
  } catch (const nlohmann::json::parse_error&) {
    res.status = 400;
    res.set_content(R"({"error":"invalid JSON"})", "application/json");
    return;
  }

  if (!request.contains("source") || !request.contains("offset")) {
    res.status = 400;
    res.set_content(R"({"error":"missing 'source' or 'offset'"})",
                    "application/json");
    return;
  }

  auto user_offset = request["offset"].get<uint32_t>();
  auto pipe = run_light_pipeline(request, repo_root);

  if (!pipe.ok) {
    res.set_content("null", "application/json");
    return;
  }

  auto absolute_offset = pipe.prelude_bytes + user_offset;
  auto token_offset = find_token_offset(absolute_offset, pipe.lex_result);

  auto result = dao::query_definition(token_offset, pipe.resolve_result);
  if (!result) {
    res.set_content("null", "application/json");
    return;
  }

  // If the definition is inside the prelude, it's not navigable
  // in the user's source — return null.
  if (result->offset < pipe.prelude_bytes) {
    res.set_content("null", "application/json");
    return;
  }

  auto user_def_offset = result->offset - pipe.prelude_bytes;
  auto loc = pipe.source.line_col(result->offset);
  auto prelude_text = std::string(
      pipe.source.contents().substr(0, pipe.prelude_bytes));
  auto prelude_lines = count_lines(prelude_text);
  auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;

  nlohmann::json response = {
      {"offset", user_def_offset},
      {"length", result->length},
      {"line", line},
      {"col", loc.col},
  };
  res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// Document symbols
// ---------------------------------------------------------------------------

void handle_document_symbols(const httplib::Request& req,
                              httplib::Response& res,
                              const std::filesystem::path& repo_root) {
  nlohmann::json request;
  try {
    request = nlohmann::json::parse(req.body);
  } catch (const nlohmann::json::parse_error&) {
    res.status = 400;
    res.set_content(R"({"error":"invalid JSON"})", "application/json");
    return;
  }

  if (!request.contains("source")) {
    res.status = 400;
    res.set_content(R"({"error":"missing 'source'"})", "application/json");
    return;
  }

  auto pipe = run_light_pipeline(request, repo_root);

  if (!pipe.ok || pipe.parse_result.file == nullptr) {
    res.set_content("[]", "application/json");
    return;
  }

  auto symbols = dao::query_document_symbols(*pipe.parse_result.file,
                                              pipe.prelude_bytes);

  // Build JSON response.
  std::function<nlohmann::json(const dao::DocumentSymbol&)> to_json;
  to_json = [&](const dao::DocumentSymbol& sym) -> nlohmann::json {
    auto user_offset = sym.span.offset >= pipe.prelude_bytes
                           ? sym.span.offset - pipe.prelude_bytes
                           : sym.span.offset;
    nlohmann::json children = nlohmann::json::array();
    for (const auto& child : sym.children) {
      children.push_back(to_json(child));
    }
    return {
        {"name", sym.name},
        {"kind", sym.kind},
        {"offset", user_offset},
        {"length", sym.span.length},
        {"children", children},
    };
  };

  nlohmann::json response = nlohmann::json::array();
  for (const auto& sym : symbols) {
    response.push_back(to_json(sym));
  }
  res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// References
// ---------------------------------------------------------------------------

void handle_references(const httplib::Request& req, httplib::Response& res,
                        const std::filesystem::path& repo_root) {
  nlohmann::json request;
  try {
    request = nlohmann::json::parse(req.body);
  } catch (const nlohmann::json::parse_error&) {
    res.status = 400;
    res.set_content(R"({"error":"invalid JSON"})", "application/json");
    return;
  }

  if (!request.contains("source") || !request.contains("offset")) {
    res.status = 400;
    res.set_content(R"({"error":"missing 'source' or 'offset'"})",
                    "application/json");
    return;
  }

  auto user_offset = request["offset"].get<uint32_t>();
  auto pipe = run_light_pipeline(request, repo_root);

  if (!pipe.ok) {
    res.set_content("[]", "application/json");
    return;
  }

  auto absolute_offset = pipe.prelude_bytes + user_offset;
  auto token_offset = find_token_offset(absolute_offset, pipe.lex_result);

  auto results = dao::query_references(token_offset, pipe.resolve_result);

  nlohmann::json response = nlohmann::json::array();
  for (const auto& ref : results) {
    // Skip references inside the prelude.
    if (ref.span.offset < pipe.prelude_bytes) {
      continue;
    }
    auto ref_user_offset = ref.span.offset - pipe.prelude_bytes;
    response.push_back({
        {"offset", ref_user_offset},
        {"length", ref.span.length},
        {"isDefinition", ref.is_definition},
    });
  }
  res.set_content(response.dump(), "application/json");
}

// NOLINTEND(readability-magic-numbers)

} // namespace dao::playground
