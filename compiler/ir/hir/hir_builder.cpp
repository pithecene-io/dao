#include "ir/hir/hir_builder.h"

#include "frontend/types/type_printer.h"

#include <charconv>
#include <cstdlib>
#include <string>

namespace dao {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HirBuilder::HirBuilder(HirContext& ctx, const ResolveResult& resolve,
                       const TypeCheckResult& typed)
    : ctx_(ctx), resolve_(resolve), typed_(typed) {
  for (const auto& sym : resolve_.context.symbols()) {
    if (sym->decl_span.length > 0) {
      decl_symbols_[sym->decl_span.offset] = sym.get();
    }
  }
}

// ---------------------------------------------------------------------------
// Top-level
// ---------------------------------------------------------------------------

auto HirBuilder::build(const FileNode& file) -> HirBuildResult {
  std::vector<HirDecl*> decls;
  for (const auto* decl : file.declarations()) {
    auto* hir_decl = lower_decl(decl);
    if (hir_decl != nullptr) {
      decls.push_back(hir_decl);
    }
  }

  auto* mod = ctx_.alloc<HirModule>(file.span(), std::move(decls));
  return {.module = mod, .diagnostics = std::move(diagnostics_)};
}

// ---------------------------------------------------------------------------
// Declaration lowering
// ---------------------------------------------------------------------------

auto HirBuilder::lower_decl(const Decl* decl) -> HirDecl* {
  switch (decl->kind()) {
  case NodeKind::FunctionDecl:
    return lower_function(static_cast<const FunctionDeclNode*>(decl));
  case NodeKind::ClassDecl:
    return lower_class(static_cast<const ClassDeclNode*>(decl));
  default:
    return nullptr;
  }
}

auto HirBuilder::lower_function(const FunctionDeclNode* fn) -> HirFunction* {
  const auto* sym = find_symbol_at_decl(fn->name_span().offset);

  // Build params.
  std::vector<HirParam> hir_params;
  for (const auto& p : fn->params()) {
    const auto* param_sym = find_symbol_at_decl(p.name_span.offset);
    const Type* param_type = nullptr;

    // Get type from typed results via decl type or symbol cache.
    if (param_sym != nullptr) {
      // Look up from the typed side tables — the type checker cached
      // param types in the symbol_types_ map. We can derive it from
      // the function's TypeFunction.
      const auto* fn_type_raw = typed_.typed.decl_type(fn);
      if (fn_type_raw != nullptr &&
          fn_type_raw->kind() == TypeKind::Function) {
        const auto* fn_type =
            static_cast<const TypeFunction*>(fn_type_raw);
        auto idx = static_cast<size_t>(&p - fn->params().data());
        if (idx < fn_type->param_types().size()) {
          param_type = fn_type->param_types()[idx];
        }
      }
    }

    hir_params.push_back({param_sym, param_type, p.name_span});
  }

  // Return type.
  const Type* ret_type = nullptr;
  const auto* fn_type_raw = typed_.typed.decl_type(fn);
  if (fn_type_raw != nullptr && fn_type_raw->kind() == TypeKind::Function) {
    ret_type = static_cast<const TypeFunction*>(fn_type_raw)->return_type();
  }

  // Body: extern functions have no body to lower.
  std::vector<HirStmt*> hir_body;
  if (fn->is_extern()) {
    // No body for extern declarations.
  } else if (fn->is_expr_bodied()) {
    auto* expr = lower_expr(fn->expr_body());
    if (expr != nullptr) {
      auto* ret = ctx_.alloc<HirReturn>(fn->expr_body()->span(), expr);
      hir_body.push_back(ret);
    }
  } else {
    hir_body = lower_body(fn->body());
  }

  return ctx_.alloc<HirFunction>(fn->span(), sym, std::move(hir_params),
                                 ret_type, std::move(hir_body),
                                 fn->is_extern());
}

auto HirBuilder::lower_class(const ClassDeclNode* st) -> HirClassDecl* {
  const auto* sym = find_symbol_at_decl(st->name_span().offset);

  const TypeStruct* struct_type = nullptr;
  const auto* decl_type = typed_.typed.decl_type(st);
  if (decl_type != nullptr && decl_type->kind() == TypeKind::Struct) {
    struct_type = static_cast<const TypeStruct*>(decl_type);
  }

  return ctx_.alloc<HirClassDecl>(st->span(), sym, struct_type);
}

// ---------------------------------------------------------------------------
// Statement lowering
// ---------------------------------------------------------------------------

auto HirBuilder::lower_body(const std::vector<Stmt*>& body)
    -> std::vector<HirStmt*> {
  std::vector<HirStmt*> result;
  for (const auto* stmt : body) {
    auto* hir = lower_stmt(stmt);
    if (hir != nullptr) {
      result.push_back(hir);
    }
  }
  return result;
}

auto HirBuilder::lower_stmt(const Stmt* stmt) -> HirStmt* {
  switch (stmt->kind()) {
  case NodeKind::LetStatement:
    return lower_let(static_cast<const LetStatementNode*>(stmt));
  case NodeKind::Assignment:
    return lower_assignment(static_cast<const AssignmentNode*>(stmt));
  case NodeKind::IfStatement:
    return lower_if(static_cast<const IfStatementNode*>(stmt));
  case NodeKind::WhileStatement:
    return lower_while(static_cast<const WhileStatementNode*>(stmt));
  case NodeKind::ForStatement:
    return lower_for(static_cast<const ForStatementNode*>(stmt));
  case NodeKind::ModeBlock:
    return lower_mode(static_cast<const ModeBlockNode*>(stmt));
  case NodeKind::ResourceBlock:
    return lower_resource(static_cast<const ResourceBlockNode*>(stmt));
  case NodeKind::ReturnStatement:
    return lower_return(static_cast<const ReturnStatementNode*>(stmt));
  case NodeKind::ExpressionStatement:
    return lower_expr_stmt(
        static_cast<const ExpressionStatementNode*>(stmt));
  default:
    return nullptr;
  }
}

auto HirBuilder::lower_let(const LetStatementNode* let) -> HirLet* {
  const auto* sym = find_symbol_at_decl(let->name_span().offset);
  const auto* type = typed_.typed.local_type(let);

  HirExpr* init = nullptr;
  if (let->initializer() != nullptr) {
    init = lower_expr(let->initializer());
  }

  return ctx_.alloc<HirLet>(let->span(), sym, type, init);
}

auto HirBuilder::lower_assignment(const AssignmentNode* assign) -> HirAssign* {
  auto* target = lower_expr(assign->target());
  auto* value = lower_expr(assign->value());
  return ctx_.alloc<HirAssign>(assign->span(), target, value);
}

auto HirBuilder::lower_if(const IfStatementNode* ifn) -> HirIf* {
  auto* cond = lower_expr(ifn->condition());
  auto then_body = lower_body(ifn->then_body());
  auto else_body = lower_body(ifn->else_body());
  return ctx_.alloc<HirIf>(ifn->span(), cond, std::move(then_body),
                           std::move(else_body));
}

auto HirBuilder::lower_while(const WhileStatementNode* wh) -> HirWhile* {
  auto* cond = lower_expr(wh->condition());
  auto body = lower_body(wh->body());
  return ctx_.alloc<HirWhile>(wh->span(), cond, std::move(body));
}

auto HirBuilder::lower_for(const ForStatementNode* fo) -> HirFor* {
  const auto* var_sym = find_symbol_at_decl(fo->var_span().offset);
  auto* iterable = lower_expr(fo->iterable());
  auto body = lower_body(fo->body());
  return ctx_.alloc<HirFor>(fo->span(), var_sym, iterable, std::move(body));
}

auto HirBuilder::lower_mode(const ModeBlockNode* mb) -> HirMode* {
  auto mode = hir_mode_kind_from_name(mb->mode_name());
  auto body = lower_body(mb->body());
  return ctx_.alloc<HirMode>(mb->span(), mode, mb->mode_name(),
                             std::move(body));
}

auto HirBuilder::lower_resource(const ResourceBlockNode* rb) -> HirResource* {
  auto body = lower_body(rb->body());
  return ctx_.alloc<HirResource>(rb->span(), rb->resource_kind(),
                                 rb->resource_name(), std::move(body));
}

auto HirBuilder::lower_return(const ReturnStatementNode* ret) -> HirReturn* {
  HirExpr* value = nullptr;
  if (ret->value() != nullptr) {
    value = lower_expr(ret->value());
  }
  return ctx_.alloc<HirReturn>(ret->span(), value);
}

auto HirBuilder::lower_expr_stmt(const ExpressionStatementNode* es)
    -> HirExprStmt* {
  auto* expr = lower_expr(es->expr());
  return ctx_.alloc<HirExprStmt>(es->span(), expr);
}

// ---------------------------------------------------------------------------
// Expression lowering
// ---------------------------------------------------------------------------

auto HirBuilder::lower_expr(const Expr* expr) -> HirExpr* {
  if (expr == nullptr) {
    return nullptr;
  }

  const auto* type = expr_type(expr);

  switch (expr->kind()) {
  case NodeKind::IntLiteral: {
    const auto* lit = static_cast<const IntLiteralNode*>(expr);
    int64_t val = 0;
    auto text = lit->text();
    std::from_chars(text.data(), text.data() + text.size(), val);
    return ctx_.alloc<HirIntLiteral>(expr->span(), type, val);
  }

  case NodeKind::FloatLiteral: {
    const auto* lit = static_cast<const FloatLiteralNode*>(expr);
    double val = std::strtod(std::string(lit->text()).c_str(), nullptr);
    return ctx_.alloc<HirFloatLiteral>(expr->span(), type, val);
  }

  case NodeKind::StringLiteral: {
    const auto* lit = static_cast<const StringLiteralNode*>(expr);
    return ctx_.alloc<HirStringLiteral>(expr->span(), type, lit->text());
  }

  case NodeKind::BoolLiteral: {
    const auto* lit = static_cast<const BoolLiteralNode*>(expr);
    return ctx_.alloc<HirBoolLiteral>(expr->span(), type, lit->value());
  }

  case NodeKind::Identifier: {
    const auto* sym = find_symbol_at_use(expr->span().offset);
    return ctx_.alloc<HirSymbolRef>(expr->span(), type, sym);
  }

  case NodeKind::UnaryExpr: {
    const auto* un = static_cast<const UnaryExprNode*>(expr);
    auto* operand = lower_expr(un->operand());
    return ctx_.alloc<HirUnary>(expr->span(), type, un->op(), operand);
  }

  case NodeKind::BinaryExpr: {
    const auto* bin = static_cast<const BinaryExprNode*>(expr);
    auto* left = lower_expr(bin->left());
    auto* right = lower_expr(bin->right());
    return ctx_.alloc<HirBinary>(expr->span(), type, bin->op(), left,
                                right);
  }

  case NodeKind::CallExpr: {
    const auto* call = static_cast<const CallExprNode*>(expr);
    auto* callee = lower_expr(call->callee());
    std::vector<HirExpr*> args;
    for (const auto* arg : call->args()) {
      args.push_back(lower_expr(arg));
    }
    return ctx_.alloc<HirCall>(expr->span(), type, callee, std::move(args));
  }

  case NodeKind::PipeExpr: {
    const auto* pipe = static_cast<const PipeExprNode*>(expr);
    auto* left = lower_expr(pipe->left());
    auto* right = lower_expr(pipe->right());
    return ctx_.alloc<HirPipe>(expr->span(), type, left, right);
  }

  case NodeKind::FieldExpr: {
    const auto* field = static_cast<const FieldExprNode*>(expr);
    auto* object = lower_expr(field->object());
    return ctx_.alloc<HirField>(expr->span(), type, object, field->field());
  }

  case NodeKind::IndexExpr: {
    const auto* idx = static_cast<const IndexExprNode*>(expr);
    auto* object = lower_expr(idx->object());
    std::vector<HirExpr*> indices;
    for (const auto* i : idx->indices()) {
      indices.push_back(lower_expr(i));
    }
    return ctx_.alloc<HirIndex>(expr->span(), type, object,
                                std::move(indices));
  }

  case NodeKind::Lambda: {
    const auto* lam = static_cast<const LambdaNode*>(expr);
    std::vector<HirParam> params;
    // Lambda params: derive types from the function type if available.
    const TypeFunction* fn_type = nullptr;
    if (type != nullptr && type->kind() == TypeKind::Function) {
      fn_type = static_cast<const TypeFunction*>(type);
    }
    for (size_t i = 0; i < lam->params().size(); ++i) {
      const auto& [name, span] = lam->params()[i];
      const auto* param_sym = find_symbol_at_decl(span.offset);
      const Type* param_type = nullptr;
      if (fn_type != nullptr && i < fn_type->param_types().size()) {
        param_type = fn_type->param_types()[i];
      }
      params.push_back({param_sym, param_type, span});
    }
    auto* body = lower_expr(lam->body());
    return ctx_.alloc<HirLambda>(expr->span(), type, std::move(params),
                                body);
  }

  default:
    error(expr->span(), "unsupported expression in HIR builder");
    return nullptr;
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

auto HirBuilder::find_symbol_at_decl(uint32_t offset) -> const Symbol* {
  auto it = decl_symbols_.find(offset);
  return it != decl_symbols_.end() ? it->second : nullptr;
}

auto HirBuilder::find_symbol_at_use(uint32_t offset) -> const Symbol* {
  auto it = resolve_.uses.find(offset);
  return it != resolve_.uses.end() ? it->second : nullptr;
}

auto HirBuilder::expr_type(const Expr* expr) -> const Type* {
  return typed_.typed.expr_type(expr);
}

void HirBuilder::error(Span span, std::string message) {
  diagnostics_.push_back(Diagnostic::error(span, std::move(message)));
}

// ---------------------------------------------------------------------------
// Free-function entry point
// ---------------------------------------------------------------------------

auto build_hir(const FileNode& file, const ResolveResult& resolve,
               const TypeCheckResult& typed, HirContext& ctx)
    -> HirBuildResult {
  HirBuilder builder(ctx, resolve, typed);
  return builder.build(file);
}

} // namespace dao
