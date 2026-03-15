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

// NOLINTBEGIN(readability-magic-numbers)
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

  // Load prelude and prepend to user source.
  auto prelude_source = load_prelude(repo_root);
  auto prelude_bytes = static_cast<uint32_t>(prelude_source.size());
  auto prelude_lines = count_lines(prelude_source);

  auto user_source = request["source"].get<std::string>();
  auto combined = prelude_source + user_source;
  SourceBuffer source("<playground>", std::move(combined));
  auto lex_result = lex(source);

  // Build token array (filtering synthetic tokens, skipping prelude tokens).
  nlohmann::json tokens = nlohmann::json::array();
  for (const auto& tok : lex_result.tokens) {
    if (is_synthetic_token(tok.kind)) {
      continue;
    }
    if (tok.span.offset < prelude_bytes) {
      continue;
    }
    auto loc = source.line_col(tok.span.offset);
    auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
    tokens.push_back({
        {"kind", token_kind_name(tok.kind)},
        {"category", token_category(tok.kind)},
        {"offset", tok.span.offset - prelude_bytes},
        {"length", tok.span.length},
        {"line", line},
        {"col", loc.col},
        {"text", std::string(tok.text)},
    });
  }

  // Collect diagnostics from lexer (skip prelude-origin).
  nlohmann::json diagnostics = nlohmann::json::array();
  for (const auto& diag : lex_result.diagnostics) {
    if (diag.span.offset < prelude_bytes) {
      continue;
    }
    auto loc = source.line_col(diag.span.offset);
    auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
    diagnostics.push_back({
        {"severity", "error"},
        {"offset", diag.span.offset - prelude_bytes},
        {"length", diag.span.length},
        {"line", line},
        {"col", loc.col},
        {"message", diag.message},
    });
  }

  // Only parse if lexing succeeded — matches CLI behavior.
  std::string ast_text;
  ParseResult parse_result;
  if (!has_user_error(lex_result.diagnostics, prelude_bytes)) {
    parse_result = parse(lex_result.tokens);

    for (const auto& diag : parse_result.diagnostics) {
      if (diag.span.offset < prelude_bytes) {
        continue;
      }
      auto loc = source.line_col(diag.span.offset);
      auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
      diagnostics.push_back({
          {"severity", "error"},
          {"offset", diag.span.offset - prelude_bytes},
          {"length", diag.span.length},
          {"line", line},
          {"col", loc.col},
          {"message", diag.message},
      });
    }

    // Only emit AST when there are no diagnostics at all.
    if (diagnostics.empty() && parse_result.file != nullptr) {
      std::ostringstream ast_out;
      print_ast(ast_out, *parse_result.file);
      ast_text = ast_out.str();
    }
  }

  // Run name resolution when lex/parse succeeded without errors.
  // Resolve diagnostics are surfaced to the user but do NOT gate
  // semantic token classification — partial resolve results still
  // improve highlighting for the tokens that did resolve.
  ResolveResult resolve_result;
  bool lex_parse_clean = !has_user_error(lex_result.diagnostics, prelude_bytes) &&
                         !has_user_error(parse_result.diagnostics, prelude_bytes) &&
                         parse_result.file != nullptr;
  if (lex_parse_clean) {
    resolve_result = resolve(*parse_result.file, prelude_bytes);

    for (const auto& diag : resolve_result.diagnostics) {
      if (diag.span.offset < prelude_bytes) {
        continue;
      }
      auto loc = source.line_col(diag.span.offset);
      auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
      diagnostics.push_back({
          {"severity", "error"},
          {"offset", diag.span.offset - prelude_bytes},
          {"length", diag.span.length},
          {"line", line},
          {"col", loc.col},
          {"message", diag.message},
      });
    }
  }

  // Produce semantic token classification when lex/parse succeeded.
  // Resolve diagnostics do not suppress tokens — classify_tokens()
  // gracefully handles partial resolve results.
  nlohmann::json semantic_tokens_json = nlohmann::json::array();
  if (lex_parse_clean) {
    auto sem_tokens =
        classify_tokens(lex_result.tokens, parse_result.file, &resolve_result);
    for (const auto& stok : sem_tokens) {
      if (stok.span.offset < prelude_bytes) {
        continue;
      }
      auto loc = source.line_col(stok.span.offset);
      auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
      semantic_tokens_json.push_back({
          {"kind", stok.kind},
          {"offset", stok.span.offset - prelude_bytes},
          {"length", stok.span.length},
          {"line", line},
          {"col", loc.col},
      });
    }
  }

  // Run type checking, HIR, MIR, and LLVM IR when lex/parse/resolve
  // succeeded without errors. Typecheck warnings are surfaced but do
  // not gate IR.
  std::string hir_text;
  std::string mir_text;
  std::string llvm_ir_text;
  TypeContext types;
  bool resolve_clean =
      lex_parse_clean && !has_user_error(resolve_result.diagnostics, prelude_bytes);
  if (resolve_clean) {
    auto check_result =
        typecheck(*parse_result.file, resolve_result, types);

    bool has_errors = false;
    for (const auto& diag : check_result.diagnostics) {
      if (diag.span.offset < prelude_bytes) {
        continue;
      }
      auto loc = source.line_col(diag.span.offset);
      auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
      diagnostics.push_back({
          {"severity",
           diag.severity == Severity::Error ? "error" : "warning"},
          {"offset", diag.span.offset - prelude_bytes},
          {"length", diag.span.length},
          {"line", line},
          {"col", loc.col},
          {"message", diag.message},
      });
      if (diag.severity == Severity::Error) {
        has_errors = true;
      }
    }

    if (!has_errors) {
      HirContext hir_ctx;
      auto hir_result = build_hir(*parse_result.file, resolve_result,
                                  check_result, hir_ctx);

      for (const auto& diag : hir_result.diagnostics) {
        if (diag.span.offset < prelude_bytes) {
          continue;
        }
        auto loc = source.line_col(diag.span.offset);
        auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
        diagnostics.push_back({
            {"severity", "error"},
            {"offset", diag.span.offset - prelude_bytes},
            {"length", diag.span.length},
            {"line", line},
            {"col", loc.col},
            {"message", diag.message},
        });
      }

      if (hir_result.module != nullptr) {
        std::ostringstream hir_out;
        print_hir(hir_out, *hir_result.module);
        hir_text = hir_out.str();

        MirContext mir_ctx;
        auto mir_result =
            build_mir(*hir_result.module, mir_ctx, types);

        if (mir_result.module != nullptr) {
          auto mono = monomorphize(*mir_result.module, mir_ctx, types);
          collect_diagnostics(diagnostics, source, mono.diagnostics,
                              prelude_bytes, prelude_lines);
        }

        for (const auto& diag : mir_result.diagnostics) {
          if (diag.span.offset < prelude_bytes) {
            continue;
          }
          auto loc = source.line_col(diag.span.offset);
          auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
          diagnostics.push_back({
              {"severity", "error"},
              {"offset", diag.span.offset - prelude_bytes},
              {"length", diag.span.length},
              {"line", line},
              {"col", loc.col},
              {"message", diag.message},
          });
        }

        if (mir_result.module == nullptr &&
            !has_user_error(mir_result.diagnostics, prelude_bytes)) {
          diagnostics.push_back(
              make_internal_error("MIR lowering failed (possible prelude error)"));
        }

        if (mir_result.module != nullptr) {
          std::ostringstream mir_out;
          print_mir(mir_out, *mir_result.module);
          mir_text = mir_out.str();

          // LLVM IR lowering — gate on MIR success.
          llvm::LLVMContext llvm_ctx;
          LlvmBackend llvm_backend(llvm_ctx);
          auto llvm_result = llvm_backend.lower(*mir_result.module, prelude_bytes);

          for (const auto& diag : llvm_result.diagnostics) {
            if (diag.severity == Severity::Warning &&
                diag.span.offset < prelude_bytes) {
              continue;
            }
            if (diag.span.offset < prelude_bytes) {
              continue;
            }
            auto loc = source.line_col(diag.span.offset);
            auto line = loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
            diagnostics.push_back({
                {"severity",
                 diag.severity == Severity::Error ? "error" : "warning"},
                {"offset", diag.span.offset - prelude_bytes},
                {"length", diag.span.length},
                {"line", line},
                {"col", loc.col},
                {"message", diag.message},
            });
          }

          if (llvm_result.module != nullptr &&
              !has_user_error(llvm_result.diagnostics, prelude_bytes)) {
            std::ostringstream llvm_out;
            LlvmBackend::print_ir(llvm_out, *llvm_result.module);
            llvm_ir_text = llvm_out.str();
          }
        }
      } else if (!has_user_error(hir_result.diagnostics, prelude_bytes)) {
        diagnostics.push_back(
            make_internal_error("HIR lowering failed (possible prelude error)"));
      }
    }
  }

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
