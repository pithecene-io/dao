#include "frontend/parser/parser.h"

#include <string>

namespace dao {

namespace {

// NOLINTBEGIN(readability-identifier-length)
class ParserImpl {
public:
  explicit ParserImpl(const std::vector<Token>& tokens) : tokens_(tokens) {
  }

  auto run() -> ParseResult {
    auto* file = parse_file();
    return {.context = std::move(ctx_), .file = file, .diagnostics = std::move(diagnostics_)};
  }

private:
  const std::vector<Token>& tokens_;
  uint32_t pos_ = 0;
  AstContext ctx_;
  std::vector<Diagnostic> diagnostics_;
  bool in_pipe_target_ = false;

  // -----------------------------------------------------------------------
  // Token access
  // -----------------------------------------------------------------------

  [[nodiscard]] auto peek() const -> const Token& {
    return tokens_[pos_];
  }

  [[nodiscard]] auto peek_kind() const -> TokenKind {
    return tokens_[pos_].kind;
  }

  [[nodiscard]] auto at_end() const -> bool {
    return peek_kind() == TokenKind::Eof;
  }

  auto advance() -> const Token& {
    const auto& tok = tokens_[pos_];
    if (!at_end()) {
      ++pos_;
    }
    return tok;
  }

  auto consume(TokenKind kind) -> const Token& {
    if (peek_kind() != kind) {
      error("expected " + std::string(token_kind_name(kind)) + ", got " +
            std::string(token_kind_name(peek_kind())));
    }
    return advance();
  }

  auto match(TokenKind kind) -> bool {
    if (peek_kind() == kind) {
      advance();
      return true;
    }
    return false;
  }

  void skip_newlines() {
    while (peek_kind() == TokenKind::Newline) {
      advance();
    }
  }

  void error(const std::string& message) {
    diagnostics_.push_back(Diagnostic::error(peek().span, message));
  }

  // -----------------------------------------------------------------------
  // Error recovery
  // -----------------------------------------------------------------------

  /// Skip tokens until a declaration keyword or EOF.
  void synchronize_to_declaration() {
    while (!at_end()) {
      switch (peek_kind()) {
      case TokenKind::KwFn:
      case TokenKind::KwClass:
      case TokenKind::KwConcept:
      case TokenKind::KwDerived:
      case TokenKind::KwExtend:
      case TokenKind::KwExtern:
      case TokenKind::KwType:
        return;
      case TokenKind::Dedent:
        return;
      default:
        advance();
      }
    }
  }

  /// Skip tokens until a statement boundary (NEWLINE, DEDENT, or EOF).
  void synchronize_to_statement() {
    while (!at_end()) {
      if (peek_kind() == TokenKind::Newline ||
          peek_kind() == TokenKind::Dedent) {
        return;
      }
      advance();
    }
  }

  /// Create an error expression node at the current position.
  auto make_error_expr() -> Expr* {
    return ctx_.alloc<Expr>(peek().span, ErrorExprNode{});
  }

  /// Check whether an expression is null or an error recovery placeholder.
  static auto is_error_expr(const Expr* expr) -> bool {
    return expr == nullptr || expr->kind() == NodeKind::ErrorExpr;
  }

  /// Create an error statement node.
  auto make_error_stmt(Span span) -> Stmt* {
    return ctx_.alloc<Stmt>(span, ErrorStmtNode{});
  }

  /// Create an error declaration node.
  auto make_error_decl(Span span) -> Decl* {
    return ctx_.alloc<Decl>(span, ErrorDeclNode{});
  }

  // -----------------------------------------------------------------------
  // File
  // -----------------------------------------------------------------------

  auto parse_file() -> FileNode* {
    skip_newlines();
    Span file_span = peek().span;

    std::vector<ImportNode*> imports;
    while (peek_kind() == TokenKind::KwImport) {
      imports.push_back(parse_import());
      skip_newlines();
    }

    std::vector<Decl*> declarations;
    while (!at_end()) {
      skip_newlines();
      if (at_end()) {
        break;
      }
      Span before = peek().span;
      auto* decl = parse_declaration();
      if (decl != nullptr) {
        declarations.push_back(decl);
      } else {
        // Recovery: insert an error placeholder and skip to next declaration keyword.
        synchronize_to_declaration();
        Span err_span = {.offset = before.offset,
                         .length = peek().span.offset - before.offset};
        declarations.push_back(make_error_decl(err_span));
      }
      skip_newlines();
    }

    Span total = {.offset = file_span.offset, .length = peek().span.offset - file_span.offset};
    return ctx_.alloc<FileNode>(total, std::move(imports), std::move(declarations));
  }

  // -----------------------------------------------------------------------
  // Import
  // -----------------------------------------------------------------------

  auto parse_import() -> ImportNode* {
    const auto& kw = consume(TokenKind::KwImport);
    auto path = parse_module_path();
    consume(TokenKind::Newline);
    Span span = {.offset = kw.span.offset,
                 .length = (path.span.offset + path.span.length) - kw.span.offset};
    return ctx_.alloc<ImportNode>(span, std::move(path));
  }

  auto parse_module_path() -> QualifiedPath {
    QualifiedPath path;
    const auto& first = consume(TokenKind::Identifier);
    path.segments.push_back(first.text);
    path.span = first.span;

    while (peek_kind() == TokenKind::ColonColon) {
      advance(); // ::
      const auto& seg = consume(TokenKind::Identifier);
      path.segments.push_back(seg.text);
      path.span.length = (seg.span.offset + seg.span.length) - path.span.offset;
    }

    return path;
  }

  // -----------------------------------------------------------------------
  // Declarations
  // -----------------------------------------------------------------------

  auto parse_declaration() -> Decl* {
    switch (peek_kind()) {
    case TokenKind::KwExtern:
      return parse_extern_decl();
    case TokenKind::KwFn:
      return parse_function_decl(/*is_extern=*/false);
    case TokenKind::KwClass:
      return parse_class_decl();
    case TokenKind::KwType:
      return parse_alias_decl();
    case TokenKind::KwConcept:
      return parse_concept_decl(/*is_derived=*/false);
    case TokenKind::KwDerived:
      return parse_concept_decl(/*is_derived=*/true);
    case TokenKind::KwExtend:
      return parse_extend_decl();
    default:
      // Migration diagnostic: 'struct' was renamed to 'class'.
      if (peek_kind() == TokenKind::Identifier && peek().text == "struct") {
        error("'struct' has been renamed to 'class'");
        // Recover by parsing as a class declaration.
        advance(); // consume 'struct' identifier
        const auto& name_tok = consume(TokenKind::Identifier);
        consume(TokenKind::Colon);
        auto fields = parse_field_list();
        Span span = span_from(peek().span);
        return ctx_.alloc<Decl>(
            span, ClassDecl{.name = name_tok.text, .name_span = name_tok.span,
                            .type_params = {}, .fields = std::move(fields),
                            .conformances = {}, .denials = {}});
      }
      error("expected declaration (fn, extern, class, concept, extend, or type)");
      advance(); // skip problematic token
      return nullptr;
    }
  }

  auto parse_extern_decl() -> Decl* {
    advance(); // consume 'extern'
    if (peek_kind() != TokenKind::KwFn) {
      error("expected 'fn' after 'extern'");
      return nullptr;
    }
    return parse_function_decl(/*is_extern=*/true);
  }

  auto parse_type_params() -> std::vector<GenericParam> {
    std::vector<GenericParam> params;
    if (peek_kind() != TokenKind::Lt) {
      return params;
    }
    advance(); // <

    auto parse_one = [&]() -> GenericParam {
      const auto& name_tok = consume(TokenKind::Identifier);
      GenericParam param{.name = name_tok.text, .name_span = name_tok.span};
      if (peek_kind() == TokenKind::Colon) {
        advance(); // :
        param.constraints.push_back(parse_type());
        while (peek_kind() == TokenKind::Plus) {
          advance(); // +
          param.constraints.push_back(parse_type());
        }
      }
      return param;
    };

    params.push_back(parse_one());
    while (peek_kind() == TokenKind::Comma) {
      advance(); // ,
      params.push_back(parse_one());
    }
    consume(TokenKind::Gt);
    return params;
  }

  auto parse_function_decl(bool is_extern) -> Decl* {
    const auto& kw = consume(TokenKind::KwFn);
    const auto& name_tok = consume(TokenKind::Identifier);

    auto type_params = parse_type_params();

    consume(TokenKind::LParen);
    auto params = parse_params();
    consume(TokenKind::RParen);

    TypeNode* return_type = nullptr;
    if (peek_kind() == TokenKind::Colon) {
      advance(); // :
      return_type = parse_type();
    }

    std::vector<Stmt*> body;
    Expr* expr_body = nullptr;

    if (is_extern) {
      // Extern declarations: signature only, no body.
      if (return_type == nullptr) {
        error("extern function declaration requires a return type");
      }
      consume(TokenKind::Newline);
    } else if (peek_kind() == TokenKind::Arrow) {
      // Expression-bodied: -> expression NEWLINE
      advance(); // ->
      expr_body = parse_expression();
      // The trailing newline may have been consumed by pipe continuation.
      match(TokenKind::Newline);
    } else {
      // Consume the newline after the signature.
      consume(TokenKind::Newline);

      if (peek_kind() == TokenKind::Indent) {
        // Block-bodied: INDENT statement+ DEDENT
        advance(); // consume Indent
        body = parse_statement_list();
        consume(TokenKind::Dedent);
      } else {
        // Non-extern function without a body is an error.
        error("expected function body (indented block or -> expression)");
      }
    }

    Span span = span_from(kw.span);
    return ctx_.alloc<Decl>(
        span,
        FunctionDecl{.name = name_tok.text, .name_span = name_tok.span,
                     .type_params = std::move(type_params),
                     .params = std::move(params), .return_type = return_type,
                     .body = std::move(body), .expr_body = expr_body,
                     .is_extern = is_extern});
  }

  auto parse_params() -> std::vector<Param> {
    std::vector<Param> params;
    if (peek_kind() == TokenKind::RParen) {
      return params;
    }
    params.push_back(parse_param());
    while (peek_kind() == TokenKind::Comma) {
      advance(); // ,
      params.push_back(parse_param());
    }
    return params;
  }

  auto parse_param() -> Param {
    // Accept `self` (reserved keyword) as a parameter name.
    const Token* name_tok = nullptr;
    if (peek_kind() == TokenKind::KwSelf) {
      name_tok = &advance();
    } else {
      name_tok = &consume(TokenKind::Identifier);
    }

    // `self` without `: type` is a bare receiver; type resolved later.
    if (name_tok->text == "self" && peek_kind() != TokenKind::Colon) {
      return {.name = name_tok->text, .name_span = name_tok->span, .type = nullptr};
    }

    consume(TokenKind::Colon);
    auto* type = parse_type();
    return {.name = name_tok->text, .name_span = name_tok->span, .type = type};
  }

  auto parse_class_decl() -> Decl* {
    const auto& kw = consume(TokenKind::KwClass);
    const auto& name_tok = consume(TokenKind::Identifier);
    auto type_params = parse_type_params();
    consume(TokenKind::Colon);
    auto body = parse_class_body();
    Span span = span_from(kw.span);
    return ctx_.alloc<Decl>(
        span, ClassDecl{.name = name_tok.text, .name_span = name_tok.span,
                        .type_params = std::move(type_params),
                        .fields = std::move(body.fields),
                        .conformances = std::move(body.conformances),
                        .denials = std::move(body.denials)});
  }

  struct ClassBody {
    std::vector<FieldSpec*> fields;
    std::vector<ConformanceBlock> conformances;
    std::vector<DenySpec> denials;
  };

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  auto parse_class_body() -> ClassBody {
    consume(TokenKind::Newline);
    consume(TokenKind::Indent);
    ClassBody body;
    while (peek_kind() != TokenKind::Dedent && peek_kind() != TokenKind::Eof) {
      if (peek_kind() == TokenKind::KwAs) {
        body.conformances.push_back(parse_conformance_block());
      } else if (peek_kind() == TokenKind::KwDeny) {
        body.denials.push_back(parse_deny_spec());
      } else if (peek_kind() == TokenKind::KwFn) {
        // Method declaration inside class body (future: §11.5).
        // For now, skip ahead and emit a diagnostic.
        error("methods inside class bodies are not yet supported");
        advance();
      } else {
        body.fields.push_back(parse_field_spec());
      }
    }
    consume(TokenKind::Dedent);
    if (body.fields.empty() && body.conformances.empty()) {
      error("class body must contain at least one field");
    }
    return body;
  }

  auto parse_conformance_block() -> ConformanceBlock {
    advance(); // consume 'as'
    const auto& concept_tok = consume(TokenKind::Identifier);
    consume(TokenKind::Colon);
    auto methods = parse_method_list();
    return {.concept_name = concept_tok.text, .concept_span = concept_tok.span,
            .methods = std::move(methods)};
  }

  auto parse_deny_spec() -> DenySpec {
    advance(); // consume 'deny'
    const auto& concept_tok = consume(TokenKind::Identifier);
    consume(TokenKind::Newline);
    return {.concept_name = concept_tok.text, .concept_span = concept_tok.span};
  }

  auto parse_method_list() -> std::vector<Decl*> {
    consume(TokenKind::Newline);
    consume(TokenKind::Indent);
    std::vector<Decl*> methods;
    while (peek_kind() != TokenKind::Dedent && peek_kind() != TokenKind::Eof) {
      skip_newlines();
      if (peek_kind() == TokenKind::Dedent || peek_kind() == TokenKind::Eof) {
        break;
      }
      if (peek_kind() != TokenKind::KwFn) {
        error("expected 'fn' in method list");
        advance();
        continue;
      }
      methods.push_back(parse_method_decl());
    }
    consume(TokenKind::Dedent);
    return methods;
  }

  auto parse_method_decl() -> Decl* {
    // Like parse_function_decl but allows bare signatures (no body).
    const auto& kw = consume(TokenKind::KwFn);
    const auto& name_tok = consume(TokenKind::Identifier);

    auto method_type_params = parse_type_params();

    consume(TokenKind::LParen);
    auto params = parse_params();
    consume(TokenKind::RParen);

    TypeNode* return_type = nullptr;
    if (peek_kind() == TokenKind::Colon) {
      advance(); // :
      return_type = parse_type();
    }

    std::vector<Stmt*> fn_body;
    Expr* expr_body = nullptr;

    if (peek_kind() == TokenKind::Arrow) {
      // Expression-bodied default method.
      advance(); // ->
      expr_body = parse_expression();
      match(TokenKind::Newline);
    } else if (peek_kind() == TokenKind::Newline) {
      consume(TokenKind::Newline);
      if (peek_kind() == TokenKind::Indent) {
        // Block-bodied default method.
        advance(); // consume Indent
        fn_body = parse_statement_list();
        consume(TokenKind::Dedent);
      }
      // else: bare signature (required method) — no body is fine.
    }

    Span span = span_from(kw.span);
    return ctx_.alloc<Decl>(
        span,
        FunctionDecl{.name = name_tok.text, .name_span = name_tok.span,
                     .type_params = std::move(method_type_params),
                     .params = std::move(params), .return_type = return_type,
                     .body = std::move(fn_body), .expr_body = expr_body,
                     .is_extern = false});
  }

  auto parse_field_list() -> std::vector<FieldSpec*> {
    consume(TokenKind::Newline);
    consume(TokenKind::Indent);
    std::vector<FieldSpec*> fields;
    while (peek_kind() != TokenKind::Dedent && peek_kind() != TokenKind::Eof) {
      fields.push_back(parse_field_spec());
    }
    consume(TokenKind::Dedent);
    if (fields.empty()) {
      error("class body must contain at least one field");
    }
    return fields;
  }

  auto parse_field_spec() -> FieldSpec* {
    const auto& name_tok = consume(TokenKind::Identifier);
    consume(TokenKind::Colon);
    auto* type = parse_type();
    consume(TokenKind::Newline);
    Span span = span_from(name_tok.span);
    return ctx_.alloc<FieldSpec>(span, name_tok.text, name_tok.span, type);
  }

  auto parse_alias_decl() -> Decl* {
    const auto& kw = consume(TokenKind::KwType);
    const auto& name_tok = consume(TokenKind::Identifier);
    consume(TokenKind::Eq);
    auto* type = parse_type();
    consume(TokenKind::Newline);
    Span span = span_from(kw.span);
    return ctx_.alloc<Decl>(
        span, AliasDecl{.name = name_tok.text, .name_span = name_tok.span,
                        .type = type});
  }

  auto parse_concept_decl(bool is_derived) -> Decl* {
    const auto& kw = is_derived ? advance() : peek(); // consume 'derived' if present
    if (is_derived) {
      consume(TokenKind::KwConcept);
    } else {
      advance(); // consume 'concept'
    }
    const auto& name_tok = consume(TokenKind::Identifier);
    auto type_params = parse_type_params();
    consume(TokenKind::Colon);
    auto methods = parse_method_list();
    Span span = span_from(kw.span);
    return ctx_.alloc<Decl>(
        span, ConceptDecl{.name = name_tok.text, .name_span = name_tok.span,
                          .type_params = std::move(type_params),
                          .methods = std::move(methods),
                          .is_derived = is_derived});
  }

  auto parse_extend_decl() -> Decl* {
    const auto& kw = advance(); // consume 'extend'
    auto* target_type = parse_type();
    consume(TokenKind::KwAs);
    const auto& concept_tok = consume(TokenKind::Identifier);
    consume(TokenKind::Colon);
    auto methods = parse_method_list();
    Span span = span_from(kw.span);
    return ctx_.alloc<Decl>(
        span, ExtendDecl{.target_type = target_type,
                         .concept_name = concept_tok.text,
                         .concept_span = concept_tok.span,
                         .methods = std::move(methods)});
  }

  // -----------------------------------------------------------------------
  // Suites and statements
  // -----------------------------------------------------------------------

  auto parse_suite() -> std::vector<Stmt*> {
    consume(TokenKind::Newline);
    consume(TokenKind::Indent);
    auto stmts = parse_statement_list();
    consume(TokenKind::Dedent);
    return stmts;
  }

  auto parse_statement_list() -> std::vector<Stmt*> {
    std::vector<Stmt*> stmts;
    while (peek_kind() != TokenKind::Dedent && peek_kind() != TokenKind::Eof) {
      skip_newlines();
      if (peek_kind() == TokenKind::Dedent || peek_kind() == TokenKind::Eof) {
        break;
      }
      Span before = peek().span;
      auto* stmt = parse_statement();
      if (stmt != nullptr) {
        stmts.push_back(stmt);
      } else {
        // Recovery: insert an error placeholder and skip to next statement boundary.
        synchronize_to_statement();
        Span err_span = {.offset = before.offset,
                         .length = peek().span.offset - before.offset};
        stmts.push_back(make_error_stmt(err_span));
      }
    }
    return stmts;
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  auto parse_statement() -> Stmt* {
    switch (peek_kind()) {
    case TokenKind::KwLet:
      return parse_let_statement();
    case TokenKind::KwIf:
      return parse_if_statement();
    case TokenKind::KwWhile:
      return parse_while_statement();
    case TokenKind::KwFor:
      return parse_for_statement();
    case TokenKind::KwMode:
      return parse_mode_block();
    case TokenKind::KwResource:
      return parse_resource_block();
    case TokenKind::KwReturn:
      return parse_return_statement();
    case TokenKind::KwYield:
      return parse_yield_statement();
    default:
      break;
    }

    // Expression or assignment.
    auto* expr = parse_expression();
    if (expr == nullptr || expr->kind() == NodeKind::ErrorExpr) {
      return nullptr;
    }

    // Check for assignment: expr = expr
    if (peek_kind() == TokenKind::Eq) {
      // Validate LHS is a legal assignment target.
      if (expr->kind() != NodeKind::Identifier && expr->kind() != NodeKind::FieldExpr &&
          expr->kind() != NodeKind::IndexExpr) {
        error("invalid assignment target");
      }
      advance(); // =
      auto* value = parse_expression();
      if (is_error_expr(value)) {
        match(TokenKind::Newline);
        return make_error_stmt(span_from(expr->span));
      }
      // Trailing newline may have been consumed by pipe continuation.
      match(TokenKind::Newline);
      Span span = {.offset = expr->span.offset,
                   .length = (value->span.offset + value->span.length) - expr->span.offset};
      return ctx_.alloc<Stmt>(span, Assignment{.target = expr, .value = value});
    }

    // Trailing newline may have been consumed by pipe continuation.
    match(TokenKind::Newline);
    return ctx_.alloc<Stmt>(expr->span, ExpressionStatement{expr});
  }

  auto parse_let_statement() -> Stmt* {
    const auto& kw = consume(TokenKind::KwLet);
    const auto& name_tok = consume(TokenKind::Identifier);

    TypeNode* type = nullptr;
    Expr* init = nullptr;

    if (peek_kind() == TokenKind::Colon) {
      advance(); // :
      type = parse_type();
      if (peek_kind() == TokenKind::Eq) {
        advance(); // =
        init = parse_expression();
      }
    } else if (peek_kind() == TokenKind::Eq) {
      advance(); // =
      init = parse_expression();
    } else {
      error("expected ':' or '=' after let binding name");
    }

    // If the initializer is an error placeholder, promote the whole
    // statement to an error so downstream passes never see it.
    // Note: init == nullptr is valid (e.g. `let x: i32`), so only
    // check when init was actually parsed but came back as an error.
    if (init != nullptr && init->kind() == NodeKind::ErrorExpr) {
      match(TokenKind::Newline);
      return make_error_stmt(span_from(kw.span));
    }

    // Trailing newline may have been consumed by pipe continuation.
    match(TokenKind::Newline);
    Span span = span_from(kw.span);
    return ctx_.alloc<Stmt>(
        span, LetStatement{.name = name_tok.text, .name_span = name_tok.span,
                           .type = type, .initializer = init});
  }

  auto parse_if_statement() -> Stmt* {
    const auto& kw = consume(TokenKind::KwIf);
    auto* condition = parse_expression();
    if (is_error_expr(condition)) {
      synchronize_to_statement();
      return make_error_stmt(span_from(kw.span));
    }
    consume(TokenKind::Colon);
    auto then_body = parse_suite();

    std::vector<Stmt*> else_body;
    if (peek_kind() == TokenKind::KwElse) {
      advance(); // else
      consume(TokenKind::Colon);
      else_body = parse_suite();
    }

    Span span = span_from(kw.span);
    return ctx_.alloc<Stmt>(
        span, IfStatement{.condition = condition,
                          .then_body = std::move(then_body),
                          .else_body = std::move(else_body)});
  }

  auto parse_while_statement() -> Stmt* {
    const auto& kw = consume(TokenKind::KwWhile);
    auto* condition = parse_expression();
    if (is_error_expr(condition)) {
      synchronize_to_statement();
      return make_error_stmt(span_from(kw.span));
    }
    consume(TokenKind::Colon);
    auto body = parse_suite();
    Span span = span_from(kw.span);
    return ctx_.alloc<Stmt>(span, WhileStatement{.condition = condition,
                                                    .body = std::move(body)});
  }

  auto parse_for_statement() -> Stmt* {
    const auto& kw = consume(TokenKind::KwFor);
    const auto& var_tok = consume(TokenKind::Identifier);
    consume(TokenKind::KwIn);
    auto* iterable = parse_expression();
    if (is_error_expr(iterable)) {
      synchronize_to_statement();
      return make_error_stmt(span_from(kw.span));
    }
    consume(TokenKind::Colon);
    auto body = parse_suite();
    Span span = span_from(kw.span);
    return ctx_.alloc<Stmt>(
        span,
        ForStatement{.var = var_tok.text, .var_span = var_tok.span,
                     .iterable = iterable, .body = std::move(body)});
  }

  auto parse_mode_block() -> Stmt* {
    const auto& kw = consume(TokenKind::KwMode);
    const auto& name_tok = consume(TokenKind::Identifier);
    consume(TokenKind::FatArrow);
    auto body = parse_suite();
    Span span = span_from(kw.span);
    return ctx_.alloc<Stmt>(
        span, ModeBlock{.mode_name = name_tok.text,
                        .name_span = name_tok.span,
                        .body = std::move(body)});
  }

  auto parse_resource_block() -> Stmt* {
    const auto& kw = consume(TokenKind::KwResource);
    const auto& kind_tok = consume(TokenKind::Identifier);
    const auto& name_tok = consume(TokenKind::Identifier);
    consume(TokenKind::FatArrow);
    auto body = parse_suite();
    Span span = span_from(kw.span);
    return ctx_.alloc<Stmt>(
        span,
        ResourceBlock{.resource_kind = kind_tok.text,
                      .kind_span = kind_tok.span,
                      .resource_name = name_tok.text,
                      .name_span = name_tok.span,
                      .body = std::move(body)});
  }

  auto parse_yield_statement() -> Stmt* {
    const auto& kw = consume(TokenKind::KwYield);
    auto* value = parse_expression();
    if (is_error_expr(value)) {
      match(TokenKind::Newline);
      return make_error_stmt(span_from(kw.span));
    }
    match(TokenKind::Newline);
    Span span = span_from(kw.span);
    return ctx_.alloc<Stmt>(span, YieldStatement{value});
  }

  auto parse_return_statement() -> Stmt* {
    const auto& kw = consume(TokenKind::KwReturn);
    Expr* value = nullptr;
    if (peek_kind() != TokenKind::Newline &&
        peek_kind() != TokenKind::Dedent &&
        peek_kind() != TokenKind::Eof) {
      value = parse_expression();
      if (is_error_expr(value)) {
        match(TokenKind::Newline);
        return make_error_stmt(span_from(kw.span));
      }
    }
    // Trailing newline may have been consumed by pipe continuation.
    match(TokenKind::Newline);
    Span span = span_from(kw.span);
    return ctx_.alloc<Stmt>(span, ReturnStatement{value});
  }

  // -----------------------------------------------------------------------
  // Expressions — precedence climbing
  // -----------------------------------------------------------------------

  auto parse_expression() -> Expr* {
    return parse_pipe_expression();
  }

  auto parse_pipe_expression() -> Expr* {
    auto* left = parse_logical_or();

    // Track whether we consumed an INDENT for pipe continuation so we
    // can consume the matching DEDENT when the chain ends.
    int pipe_indents = 0;

    while (is_pipe_continuation()) {
      // Consume whitespace tokens before |>.
      if (peek_kind() == TokenKind::Newline) {
        advance();
      }
      if (peek_kind() == TokenKind::Indent) {
        advance();
        pipe_indents++;
      }
      advance(); // |>
      Expr* right = nullptr;
      if (peek_kind() == TokenKind::Pipe) {
        right = parse_lambda();
      } else {
        in_pipe_target_ = true;
        right = parse_application();
        in_pipe_target_ = false;
      }
      Span span = {.offset = left->span.offset,
                   .length = (right->span.offset + right->span.length) - left->span.offset};
      left = ctx_.alloc<Expr>(span, PipeExpr{.left = left, .right = right});
    }

    // Consume matching DEDENTs for pipe continuation indents.
    for (int i = 0; i < pipe_indents; ++i) {
      // There may be a Newline before DEDENT.
      if (peek_kind() == TokenKind::Newline) {
        advance();
      }
      if (peek_kind() == TokenKind::Dedent) {
        advance();
      }
    }

    return left;
  }

  [[nodiscard]] auto is_pipe_continuation() const -> bool {
    if (peek_kind() == TokenKind::PipeGt) {
      return true;
    }
    // Check for newline (optionally followed by indent) then |>.
    uint32_t look = pos_;
    if (look < tokens_.size() && tokens_[look].kind == TokenKind::Newline) {
      ++look;
    } else {
      return false;
    }
    if (look < tokens_.size() && tokens_[look].kind == TokenKind::Indent) {
      ++look;
    }
    return look < tokens_.size() && tokens_[look].kind == TokenKind::PipeGt;
  }

  auto parse_logical_or() -> Expr* {
    auto* left = parse_logical_and();
    while (peek_kind() == TokenKind::KwOr) {
      advance();
      auto* right = parse_logical_and();
      left = make_binary(BinaryOp::Or, left, right);
    }
    return left;
  }

  auto parse_logical_and() -> Expr* {
    auto* left = parse_equality();
    while (peek_kind() == TokenKind::KwAnd) {
      advance();
      auto* right = parse_equality();
      left = make_binary(BinaryOp::And, left, right);
    }
    return left;
  }

  auto parse_equality() -> Expr* {
    auto* left = parse_relational();
    while (peek_kind() == TokenKind::EqEq || peek_kind() == TokenKind::BangEq) {
      auto op = peek_kind() == TokenKind::EqEq ? BinaryOp::EqEq : BinaryOp::BangEq;
      advance();
      auto* right = parse_relational();
      left = make_binary(op, left, right);
    }
    return left;
  }

  auto parse_relational() -> Expr* {
    auto* left = parse_additive();
    while (peek_kind() == TokenKind::Lt || peek_kind() == TokenKind::Gt ||
           peek_kind() == TokenKind::LtEq || peek_kind() == TokenKind::GtEq) {
      BinaryOp op{};
      switch (peek_kind()) {
      case TokenKind::Lt:
        op = BinaryOp::Lt;
        break;
      case TokenKind::Gt:
        op = BinaryOp::Gt;
        break;
      case TokenKind::LtEq:
        op = BinaryOp::LtEq;
        break;
      case TokenKind::GtEq:
        op = BinaryOp::GtEq;
        break;
      default:
        break;
      }
      advance();
      auto* right = parse_additive();
      left = make_binary(op, left, right);
    }
    return left;
  }

  auto parse_additive() -> Expr* {
    auto* left = parse_multiplicative();
    while (peek_kind() == TokenKind::Plus || peek_kind() == TokenKind::Minus) {
      auto op = peek_kind() == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Sub;
      advance();
      auto* right = parse_multiplicative();
      left = make_binary(op, left, right);
    }
    return left;
  }

  auto parse_multiplicative() -> Expr* {
    auto* left = parse_unary();
    while (peek_kind() == TokenKind::Star || peek_kind() == TokenKind::Slash ||
           peek_kind() == TokenKind::Percent) {
      BinaryOp op{};
      switch (peek_kind()) {
      case TokenKind::Star:
        op = BinaryOp::Mul;
        break;
      case TokenKind::Slash:
        op = BinaryOp::Div;
        break;
      case TokenKind::Percent:
        op = BinaryOp::Mod;
        break;
      default:
        break;
      }
      advance();
      auto* right = parse_unary();
      left = make_binary(op, left, right);
    }
    return left;
  }

  auto parse_unary() -> Expr* {
    if (peek_kind() == TokenKind::Bang || peek_kind() == TokenKind::Minus ||
        peek_kind() == TokenKind::Star || peek_kind() == TokenKind::Amp) {
      const auto& op_tok = advance();
      UnaryOp op{};
      switch (op_tok.kind) {
      case TokenKind::Bang:
        op = UnaryOp::Not;
        break;
      case TokenKind::Minus:
        op = UnaryOp::Negate;
        break;
      case TokenKind::Star:
        op = UnaryOp::Deref;
        break;
      case TokenKind::Amp:
        op = UnaryOp::AddrOf;
        break;
      default:
        break;
      }
      auto* operand = parse_unary();
      Span span = {.offset = op_tok.span.offset,
                   .length =
                       (operand->span.offset + operand->span.length) - op_tok.span.offset};
      return ctx_.alloc<Expr>(span, UnaryExpr{.op = op, .operand = operand});
    }
    return parse_application();
  }

  auto parse_application() -> Expr* {
    auto* callee = parse_postfix();

    // In pipe target position, a bare identifier/qualified-name followed
    // by another primary (lambda or identifier) is juxtaposition application:
    //   xs |> filter |x| -> x > 0    →  filter(|x| -> x > 0)
    //   xs |> filter is_valid         →  filter(is_valid)
    if (in_pipe_target_ && is_pipe_argument_start()) {
      std::vector<Expr*> args;
      while (is_pipe_argument_start()) {
        if (peek_kind() == TokenKind::Pipe) {
          args.push_back(parse_lambda());
        } else {
          args.push_back(parse_postfix());
        }
      }
      Span span = {.offset = callee->span.offset,
                   .length = (args.back()->span.offset + args.back()->span.length) -
                             callee->span.offset};
      return ctx_.alloc<Expr>(span, CallExpr{.callee = callee,
                                               .args = std::move(args)});
    }

    return callee;
  }

  [[nodiscard]] auto is_pipe_argument_start() const -> bool {
    auto kind = peek_kind();
    return kind == TokenKind::Pipe || kind == TokenKind::Identifier ||
           kind == TokenKind::IntLiteral || kind == TokenKind::FloatLiteral ||
           kind == TokenKind::StringLiteral || kind == TokenKind::KwTrue ||
           kind == TokenKind::KwFalse;
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  auto parse_postfix() -> Expr* {
    auto* expr = parse_primary();

    while (true) {
      if (peek_kind() == TokenKind::LParen) {
        // Call: expr(args)
        advance(); // (
        std::vector<Expr*> args;
        if (peek_kind() != TokenKind::RParen) {
          args.push_back(parse_expression());
          while (peek_kind() == TokenKind::Comma) {
            advance();
            args.push_back(parse_expression());
          }
        }
        const auto& rparen = consume(TokenKind::RParen);
        Span span = {.offset = expr->span.offset,
                     .length = (rparen.span.offset + rparen.span.length) - expr->span.offset};
        expr = ctx_.alloc<Expr>(span, CallExpr{.callee = expr,
                                                  .args = std::move(args)});
      } else if (peek_kind() == TokenKind::Dot) {
        // Field access: expr.field
        advance(); // .
        const auto& field_tok = consume(TokenKind::Identifier);
        Span span = {.offset = expr->span.offset,
                     .length =
                         (field_tok.span.offset + field_tok.span.length) - expr->span.offset};
        expr = ctx_.alloc<Expr>(span, FieldExpr{.object = expr,
                                                  .field = field_tok.text,
                                                  .field_span = field_tok.span});
      } else if (peek_kind() == TokenKind::LBracket) {
        // Index or type-parameter application: expr[args]
        advance(); // [
        std::vector<Expr*> indices;
        indices.push_back(parse_expression());
        while (peek_kind() == TokenKind::Comma) {
          advance();
          indices.push_back(parse_expression());
        }
        const auto& rbracket = consume(TokenKind::RBracket);
        Span span = {.offset = expr->span.offset,
                     .length = (rbracket.span.offset + rbracket.span.length) - expr->span.offset};
        expr = ctx_.alloc<Expr>(span, IndexExpr{.object = expr,
                                                  .indices = std::move(indices)});
      } else {
        break;
      }
    }

    return expr;
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  auto parse_primary() -> Expr* {
    switch (peek_kind()) {
    case TokenKind::IntLiteral: {
      const auto& tok = advance();
      return ctx_.alloc<Expr>(tok.span, IntLiteral{tok.text});
    }
    case TokenKind::FloatLiteral: {
      const auto& tok = advance();
      return ctx_.alloc<Expr>(tok.span, FloatLiteral{tok.text});
    }
    case TokenKind::StringLiteral: {
      const auto& tok = advance();
      return ctx_.alloc<Expr>(tok.span, StringLiteral{tok.text});
    }
    case TokenKind::KwTrue: {
      const auto& tok = advance();
      return ctx_.alloc<Expr>(tok.span, BoolLiteral{true});
    }
    case TokenKind::KwFalse: {
      const auto& tok = advance();
      return ctx_.alloc<Expr>(tok.span, BoolLiteral{false});
    }
    case TokenKind::LParen: {
      advance(); // (
      auto* expr = parse_expression();
      consume(TokenKind::RParen);
      return expr;
    }
    case TokenKind::LBracket:
      return parse_list_literal();
    case TokenKind::Pipe:
      return parse_lambda();
    case TokenKind::Identifier:
      return parse_qualified_name_or_identifier();
    case TokenKind::KwSelf: {
      // `self` in expression position acts as an identifier.
      const auto& tok = advance();
      return ctx_.alloc<Expr>(tok.span, IdentifierExpr{tok.text});
    }
    default: {
      auto err_span = peek().span;
      error("expected expression");
      advance(); // skip the offending token to guarantee progress
      return ctx_.alloc<Expr>(err_span, ErrorExprNode{});
    }
    }
  }

  auto parse_list_literal() -> Expr* {
    const auto& lbracket = advance(); // [
    std::vector<Expr*> elements;
    if (peek_kind() != TokenKind::RBracket) {
      elements.push_back(parse_expression());
      while (peek_kind() == TokenKind::Comma) {
        advance();
        elements.push_back(parse_expression());
      }
    }
    const auto& rbracket = consume(TokenKind::RBracket);
    Span span = {.offset = lbracket.span.offset,
                 .length = (rbracket.span.offset + rbracket.span.length) - lbracket.span.offset};
    return ctx_.alloc<Expr>(span, ListLiteral{std::move(elements)});
  }

  auto parse_lambda() -> Expr* {
    const auto& open_pipe = consume(TokenKind::Pipe);
    std::vector<std::pair<std::string_view, Span>> params;
    if (peek_kind() != TokenKind::Pipe) {
      const auto& first = consume(TokenKind::Identifier);
      params.emplace_back(first.text, first.span);
      while (peek_kind() == TokenKind::Comma) {
        advance();
        const auto& param = consume(TokenKind::Identifier);
        params.emplace_back(param.text, param.span);
      }
    }
    consume(TokenKind::Pipe);
    consume(TokenKind::Arrow);
    auto* body = parse_expression();
    Span span = {.offset = open_pipe.span.offset,
                 .length = (body->span.offset + body->span.length) - open_pipe.span.offset};
    return ctx_.alloc<Expr>(span, LambdaExpr{.params = std::move(params),
                                               .body = body});
  }

  auto parse_qualified_name_or_identifier() -> Expr* {
    const auto& first = consume(TokenKind::Identifier);
    if (peek_kind() != TokenKind::ColonColon) {
      return ctx_.alloc<Expr>(first.span, IdentifierExpr{first.text});
    }

    // Qualified name: ident :: ident (:: ident)*
    std::vector<std::string_view> segments;
    segments.push_back(first.text);
    Span span = first.span;

    while (peek_kind() == TokenKind::ColonColon) {
      advance(); // ::
      const auto& seg = consume(TokenKind::Identifier);
      segments.push_back(seg.text);
      span.length = (seg.span.offset + seg.span.length) - span.offset;
    }

    return ctx_.alloc<Expr>(span, QualifiedName{std::move(segments)});
  }

  // -----------------------------------------------------------------------
  // Types
  // -----------------------------------------------------------------------

  auto parse_type() -> TypeNode* {
    if (peek_kind() == TokenKind::Star) {
      const auto& star = advance();
      auto* pointee = parse_type();
      Span span = {.offset = star.span.offset,
                   .length = (pointee->span.offset + pointee->span.length) - star.span.offset};
      return ctx_.alloc<TypeNode>(span, PointerType{pointee});
    }

    return parse_named_type();
  }

  auto parse_named_type() -> TypeNode* {
    auto path = parse_type_path();

    std::vector<TypeNode*> type_args;
    if (peek_kind() == TokenKind::Lt) {
      advance(); // <
      type_args.push_back(parse_type());
      while (peek_kind() == TokenKind::Comma) {
        advance();
        type_args.push_back(parse_type());
      }
      const auto& gt = consume(TokenKind::Gt);
      path.span.length = (gt.span.offset + gt.span.length) - path.span.offset;
    }

    return ctx_.alloc<TypeNode>(path.span, NamedType{.name = std::move(path),
                                                        .type_args = std::move(type_args)});
  }

  auto parse_type_path() -> QualifiedPath {
    QualifiedPath path;
    const auto& first = consume(TokenKind::Identifier);
    path.segments.push_back(first.text);
    path.span = first.span;

    while (peek_kind() == TokenKind::ColonColon) {
      advance(); // ::
      const auto& seg = consume(TokenKind::Identifier);
      path.segments.push_back(seg.text);
      path.span.length = (seg.span.offset + seg.span.length) - path.span.offset;
    }

    return path;
  }

  // -----------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------

  auto make_binary(BinaryOp op, Expr* left, Expr* right) -> Expr* {
    Span span = {.offset = left->span.offset,
                 .length = (right->span.offset + right->span.length) - left->span.offset};
    return ctx_.alloc<Expr>(span, BinaryExpr{.op = op, .left = left,
                                               .right = right});
  }

  auto span_from(Span start) -> Span {
    // Span from start to the token just before current position.
    if (pos_ > 0) {
      const auto& prev = tokens_[pos_ - 1];
      return {.offset = start.offset,
              .length = (prev.span.offset + prev.span.length) - start.offset};
    }
    return start;
  }
};
// NOLINTEND(readability-identifier-length)

} // namespace

auto parse(const std::vector<Token>& tokens) -> ParseResult {
  ParserImpl parser(tokens);
  return parser.run();
}

} // namespace dao
