#include "analyze.h"
#include "token_category.h"

#include "analysis/semantic_tokens.h"
#include "frontend/ast/ast_printer.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

namespace dao::playground {

// NOLINTBEGIN(readability-magic-numbers)
void handle_analyze(const httplib::Request& req, httplib::Response& res) {
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

  auto source_text = request["source"].get<std::string>();
  SourceBuffer source("<playground>", std::move(source_text));
  auto lex_result = lex(source);

  // Build token array (filtering synthetic tokens).
  nlohmann::json tokens = nlohmann::json::array();
  for (const auto& tok : lex_result.tokens) {
    if (is_synthetic_token(tok.kind)) {
      continue;
    }
    auto loc = source.line_col(tok.span.offset);
    tokens.push_back({
        {"kind", token_kind_name(tok.kind)},
        {"category", token_category(tok.kind)},
        {"offset", tok.span.offset},
        {"length", tok.span.length},
        {"line", loc.line},
        {"col", loc.col},
        {"text", std::string(tok.text)},
    });
  }

  // Collect diagnostics from lexer.
  nlohmann::json diagnostics = nlohmann::json::array();
  for (const auto& diag : lex_result.diagnostics) {
    auto loc = source.line_col(diag.span.offset);
    diagnostics.push_back({
        {"severity", "error"},
        {"offset", diag.span.offset},
        {"length", diag.span.length},
        {"line", loc.line},
        {"col", loc.col},
        {"message", diag.message},
    });
  }

  // Only parse if lexing succeeded — matches CLI behavior.
  std::string ast_text;
  ParseResult parse_result;
  if (lex_result.diagnostics.empty()) {
    parse_result = parse(lex_result.tokens);

    for (const auto& diag : parse_result.diagnostics) {
      auto loc = source.line_col(diag.span.offset);
      diagnostics.push_back({
          {"severity", "error"},
          {"offset", diag.span.offset},
          {"length", diag.span.length},
          {"line", loc.line},
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

  // Produce semantic token classification (AST + lexical).
  auto sem_tokens = classify_tokens(lex_result.tokens, parse_result.file);
  nlohmann::json semantic_tokens_json = nlohmann::json::array();
  for (const auto& stok : sem_tokens) {
    auto loc = source.line_col(stok.span.offset);
    semantic_tokens_json.push_back({
        {"kind", stok.kind},
        {"offset", stok.span.offset},
        {"length", stok.span.length},
        {"line", loc.line},
        {"col", loc.col},
    });
  }

  nlohmann::json response = {
      {"tokens", tokens},
      {"semanticTokens", semantic_tokens_json},
      {"ast", ast_text},
      {"diagnostics", diagnostics},
  };

  res.set_content(response.dump(), "application/json");
}

// NOLINTEND(readability-magic-numbers)

} // namespace dao::playground
