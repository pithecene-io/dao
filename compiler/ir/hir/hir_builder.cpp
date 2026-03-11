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
  for (const auto* decl : file.declarations) {
    auto* hir_decl = lower_decl(decl);
    if (hir_decl != nullptr) {
      decls.push_back(hir_decl);
    }
  }

  auto* mod = ctx_.alloc<HirModule>(file.span, std::move(decls));
  return {.module = mod, .diagnostics = std::move(diagnostics_)};
}

// ---------------------------------------------------------------------------
// Declaration lowering
// ---------------------------------------------------------------------------

auto HirBuilder::lower_decl(const Decl* decl) -> HirDecl* {
  switch (decl->kind()) {
  case NodeKind::FunctionDecl:
    return lower_function(decl);
  case NodeKind::ClassDecl:
    return lower_class(decl);
  default:
    return nullptr;
  }
}

auto HirBuilder::lower_function(const Decl* decl) -> HirDecl* {
  const auto& fn = decl->as<FunctionDecl>();
  const auto* sym = find_symbol_at_decl(fn.name_span.offset);

  // Build params.
  std::vector<HirParam> hir_params;
  for (const auto& param : fn.params) {
    const auto* param_sym = find_symbol_at_decl(param.name_span.offset);
    const Type* param_type = nullptr;

    if (param_sym != nullptr) {
      const auto* fn_type_raw = typed_.typed.decl_type(decl);
      if (fn_type_raw != nullptr &&
          fn_type_raw->kind() == TypeKind::Function) {
        const auto* fn_type =
            static_cast<const TypeFunction*>(fn_type_raw);
        auto idx = static_cast<size_t>(&param - fn.params.data());
        if (idx < fn_type->param_types().size()) {
          param_type = fn_type->param_types()[idx];
        }
      }
    }

    hir_params.push_back({param_sym, param_type, param.name_span});
  }

  // Return type.
  const Type* ret_type = nullptr;
  const auto* fn_type_raw = typed_.typed.decl_type(decl);
  if (fn_type_raw != nullptr && fn_type_raw->kind() == TypeKind::Function) {
    ret_type = static_cast<const TypeFunction*>(fn_type_raw)->return_type();
  }

  // Body: extern functions have no body to lower.
  std::vector<HirStmt*> hir_body;
  if (fn.is_extern) {
    // No body for extern declarations.
  } else if (fn.is_expr_bodied()) {
    auto* expr = lower_expr(fn.expr_body);
    if (expr != nullptr) {
      auto* ret = ctx_.alloc<HirStmt>(fn.expr_body->span,
                                       HirReturn{expr});
      hir_body.push_back(ret);
    }
  } else {
    hir_body = lower_body(fn.body);
  }

  return ctx_.alloc<HirDecl>(
      decl->span,
      HirFunction{sym, std::move(hir_params), ret_type,
                  std::move(hir_body), fn.is_extern});
}

auto HirBuilder::lower_class(const Decl* decl) -> HirDecl* {
  const auto& st = decl->as<ClassDecl>();
  const auto* sym = find_symbol_at_decl(st.name_span.offset);

  const TypeStruct* struct_type = nullptr;
  const auto* decl_type = typed_.typed.decl_type(decl);
  if (decl_type != nullptr && decl_type->kind() == TypeKind::Struct) {
    struct_type = static_cast<const TypeStruct*>(decl_type);
  }

  return ctx_.alloc<HirDecl>(decl->span, HirClassDecl{sym, struct_type});
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto HirBuilder::lower_stmt(const Stmt* stmt) -> HirStmt* {
  switch (stmt->kind()) {
  case NodeKind::LetStatement: {
    const auto& let = stmt->as<LetStatement>();
    const auto* sym = find_symbol_at_decl(let.name_span.offset);
    const auto* type = typed_.typed.local_type(stmt);
    HirExpr* init = nullptr;
    if (let.initializer != nullptr) {
      init = lower_expr(let.initializer);
    }
    return ctx_.alloc<HirStmt>(stmt->span, HirLet{sym, type, init});
  }

  case NodeKind::Assignment: {
    const auto& assign = stmt->as<Assignment>();
    auto* target = lower_expr(assign.target);
    auto* value = lower_expr(assign.value);
    return ctx_.alloc<HirStmt>(stmt->span, HirAssign{target, value});
  }

  case NodeKind::IfStatement: {
    const auto& ifn = stmt->as<IfStatement>();
    auto* cond = lower_expr(ifn.condition);
    auto then_body = lower_body(ifn.then_body);
    auto else_body = lower_body(ifn.else_body);
    return ctx_.alloc<HirStmt>(
        stmt->span,
        HirIf{cond, std::move(then_body), std::move(else_body)});
  }

  case NodeKind::WhileStatement: {
    const auto& wh = stmt->as<WhileStatement>();
    auto* cond = lower_expr(wh.condition);
    auto body = lower_body(wh.body);
    return ctx_.alloc<HirStmt>(stmt->span,
                                HirWhile{cond, std::move(body)});
  }

  case NodeKind::ForStatement: {
    const auto& fo = stmt->as<ForStatement>();
    const auto* var_sym = find_symbol_at_decl(fo.var_span.offset);
    auto* iterable = lower_expr(fo.iterable);
    auto body = lower_body(fo.body);
    return ctx_.alloc<HirStmt>(
        stmt->span, HirFor{var_sym, iterable, std::move(body)});
  }

  case NodeKind::ModeBlock: {
    const auto& mb = stmt->as<ModeBlock>();
    auto mode = hir_mode_kind_from_name(mb.mode_name);
    auto body = lower_body(mb.body);
    return ctx_.alloc<HirStmt>(
        stmt->span, HirMode{mode, mb.mode_name, std::move(body)});
  }

  case NodeKind::ResourceBlock: {
    const auto& rb = stmt->as<ResourceBlock>();
    auto body = lower_body(rb.body);
    return ctx_.alloc<HirStmt>(
        stmt->span,
        HirResource{rb.resource_kind, rb.resource_name,
                    std::move(body)});
  }

  case NodeKind::ReturnStatement: {
    const auto& ret = stmt->as<ReturnStatement>();
    HirExpr* value = nullptr;
    if (ret.value != nullptr) {
      value = lower_expr(ret.value);
    }
    return ctx_.alloc<HirStmt>(stmt->span, HirReturn{value});
  }

  case NodeKind::ExpressionStatement: {
    const auto& es = stmt->as<ExpressionStatement>();
    auto* expr = lower_expr(es.expr);
    return ctx_.alloc<HirStmt>(stmt->span, HirExprStmt{expr});
  }

  default:
    return nullptr;
  }
}

// ---------------------------------------------------------------------------
// Expression lowering
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto HirBuilder::lower_expr(const Expr* expr) -> HirExpr* {
  if (expr == nullptr) {
    return nullptr;
  }

  const auto* type = expr_type(expr);
  auto span = expr->span;

  switch (expr->kind()) {
  case NodeKind::IntLiteral: {
    const auto& lit = expr->as<IntLiteral>();
    int64_t val = 0;
    auto text = lit.text;
    std::from_chars(text.data(), text.data() + text.size(), val);
    return ctx_.alloc<HirExpr>(span, type, HirIntLiteral{val});
  }

  case NodeKind::FloatLiteral: {
    const auto& lit = expr->as<FloatLiteral>();
    double val = std::strtod(std::string(lit.text).c_str(), nullptr);
    return ctx_.alloc<HirExpr>(span, type, HirFloatLiteral{val});
  }

  case NodeKind::StringLiteral: {
    const auto& lit = expr->as<StringLiteral>();
    return ctx_.alloc<HirExpr>(span, type, HirStringLiteral{lit.text});
  }

  case NodeKind::BoolLiteral: {
    const auto& lit = expr->as<BoolLiteral>();
    return ctx_.alloc<HirExpr>(span, type, HirBoolLiteral{lit.value});
  }

  case NodeKind::Identifier: {
    const auto* sym = find_symbol_at_use(expr->span.offset);
    return ctx_.alloc<HirExpr>(span, type, HirSymbolRef{sym});
  }

  case NodeKind::UnaryExpr: {
    const auto& un = expr->as<UnaryExpr>();
    auto* operand = lower_expr(un.operand);
    return ctx_.alloc<HirExpr>(span, type, HirUnary{un.op, operand});
  }

  case NodeKind::BinaryExpr: {
    const auto& bin = expr->as<BinaryExpr>();
    auto* left = lower_expr(bin.left);
    auto* right = lower_expr(bin.right);
    return ctx_.alloc<HirExpr>(span, type,
                                HirBinary{bin.op, left, right});
  }

  case NodeKind::CallExpr: {
    const auto& call = expr->as<CallExpr>();
    auto* callee = lower_expr(call.callee);
    std::vector<HirExpr*> args;
    for (const auto* arg : call.args) {
      args.push_back(lower_expr(arg));
    }
    return ctx_.alloc<HirExpr>(span, type,
                                HirCall{callee, std::move(args)});
  }

  case NodeKind::PipeExpr: {
    const auto& pipe = expr->as<PipeExpr>();
    auto* left = lower_expr(pipe.left);
    auto* right = lower_expr(pipe.right);
    return ctx_.alloc<HirExpr>(span, type, HirPipe{left, right});
  }

  case NodeKind::FieldExpr: {
    const auto& field = expr->as<FieldExpr>();
    auto* object = lower_expr(field.object);
    return ctx_.alloc<HirExpr>(span, type,
                                HirField{object, field.field});
  }

  case NodeKind::IndexExpr: {
    const auto& idx = expr->as<IndexExpr>();
    auto* object = lower_expr(idx.object);
    std::vector<HirExpr*> indices;
    for (const auto* i : idx.indices) {
      indices.push_back(lower_expr(i));
    }
    return ctx_.alloc<HirExpr>(span, type,
                                HirIndex{object, std::move(indices)});
  }

  case NodeKind::Lambda: {
    const auto& lam = expr->as<LambdaExpr>();
    std::vector<HirParam> params;
    const TypeFunction* fn_type = nullptr;
    if (type != nullptr && type->kind() == TypeKind::Function) {
      fn_type = static_cast<const TypeFunction*>(type);
    }
    for (size_t i = 0; i < lam.params.size(); ++i) {
      const auto& [name, param_span] = lam.params[i];
      const auto* param_sym = find_symbol_at_decl(param_span.offset);
      const Type* param_type = nullptr;
      if (fn_type != nullptr && i < fn_type->param_types().size()) {
        param_type = fn_type->param_types()[i];
      }
      params.push_back({param_sym, param_type, param_span});
    }
    auto* body = lower_expr(lam.body);
    return ctx_.alloc<HirExpr>(span, type,
                                HirLambda{std::move(params), body});
  }

  default:
    error(expr->span, "unsupported expression in HIR builder");
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
