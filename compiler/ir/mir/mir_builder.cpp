#include "ir/mir/mir_builder.h"

#include "frontend/types/type_printer.h"

#include <string>

namespace dao {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MirBuilder::MirBuilder(MirContext& ctx, TypeContext& types)
    : ctx_(ctx), types_(types) {}

// ---------------------------------------------------------------------------
// Field index resolution
// ---------------------------------------------------------------------------

auto MirBuilder::resolve_field_index(const Type* obj_type,
                                     std::string_view field_name) -> uint32_t {
  if (obj_type != nullptr && obj_type->kind() == TypeKind::Struct) {
    const auto* st = static_cast<const TypeStruct*>(obj_type);
    for (uint32_t idx = 0; idx < st->fields().size(); ++idx) {
      if (st->fields()[idx].name == field_name) {
        return idx;
      }
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Emit helpers
// ---------------------------------------------------------------------------

auto MirBuilder::emit_value(const HirExpr& expr, MirPayload payload)
    -> MirValueId {
  return emit_value(expr.type(), expr.span(), std::move(payload));
}

auto MirBuilder::emit_value(const Type* type, Span span, MirPayload payload)
    -> MirValueId {
  auto* inst = ctx_.alloc<MirInst>();
  inst->result = fresh_value();
  inst->type = type;
  inst->span = span;
  inst->payload = payload;
  emit(inst);
  return inst->result;
}

void MirBuilder::emit_effect(Span span, MirPayload payload) {
  auto* inst = ctx_.alloc<MirInst>();
  inst->span = span;
  inst->payload = payload;
  emit(inst);
}

void MirBuilder::emit_terminator(Span span, MirPayload payload) {
  auto* inst = ctx_.alloc<MirInst>();
  inst->span = span;
  inst->payload = payload;
  emit(inst);
}

// ---------------------------------------------------------------------------
// Top-level
// ---------------------------------------------------------------------------

auto MirBuilder::build(const HirModule& module) -> MirBuildResult {
  auto* mir_mod = ctx_.alloc<MirModule>();
  mir_mod->span = module.span();
  current_module_ = mir_mod;

  for (const auto* decl : module.declarations()) {
    if (decl->kind() == HirKind::Function) {
      auto* mir_fn =
          lower_function(static_cast<const HirFunction&>(*decl));
      if (mir_fn != nullptr) {
        mir_mod->functions.push_back(mir_fn);
      }
    }
    // ClassDecl: no MIR function to produce; skip.
  }

  return {.module = mir_mod, .diagnostics = std::move(diagnostics_)};
}

// ---------------------------------------------------------------------------
// Function lowering
// ---------------------------------------------------------------------------

auto MirBuilder::lower_function(const HirFunction& fn) -> MirFunction* {
  auto* mir_fn = ctx_.alloc<MirFunction>();
  mir_fn->symbol = fn.symbol();
  mir_fn->return_type = fn.return_type();
  mir_fn->span = fn.span();
  mir_fn->is_extern = fn.is_extern();

  // Reset per-function state.
  current_fn_ = mir_fn;
  current_block_ = nullptr;
  next_value_id_ = 0;
  next_block_id_ = 0;
  symbol_to_local_.clear();
  active_regions_.clear();

  // Create parameter locals.
  for (const auto& param : fn.params()) {
    declare_local(param.symbol, param.type, param.span, /*is_param=*/true);
  }

  // Extern declaration — no blocks to emit.
  if (fn.is_extern()) {
    return mir_fn;
  }

  // Create entry block.
  auto* entry = fresh_block();
  switch_to_block(entry);

  // Lower body statements.
  for (const auto* stmt : fn.body()) {
    lower_stmt(*stmt);
  }

  // Ensure the last block has a terminator.
  if (!block_terminated()) {
    emit_terminator(fn.span(), MirReturn{{}, false});
  }

  return mir_fn;
}

// ---------------------------------------------------------------------------
// Statement lowering
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void MirBuilder::lower_stmt(const HirStmt& stmt) {
  if (block_terminated()) {
    return; // Dead code after terminator.
  }

  switch (stmt.kind()) {
  case HirKind::Let:
    lower_let(static_cast<const HirLet&>(stmt));
    break;
  case HirKind::Assign:
    lower_assign(static_cast<const HirAssign&>(stmt));
    break;
  case HirKind::If:
    lower_if(static_cast<const HirIf&>(stmt));
    break;
  case HirKind::While:
    lower_while(static_cast<const HirWhile&>(stmt));
    break;
  case HirKind::For:
    lower_for(static_cast<const HirFor&>(stmt));
    break;
  case HirKind::Return:
    lower_return(static_cast<const HirReturn&>(stmt));
    break;
  case HirKind::ExprStmt:
    lower_expr_stmt(static_cast<const HirExprStmt&>(stmt));
    break;
  case HirKind::Mode:
    lower_mode(static_cast<const HirMode&>(stmt));
    break;
  case HirKind::Resource:
    lower_resource(static_cast<const HirResource&>(stmt));
    break;
  default:
    break;
  }
}

void MirBuilder::lower_let(const HirLet& let_stmt) {
  auto local_id =
      declare_local(let_stmt.symbol(), let_stmt.type(), let_stmt.span());

  if (let_stmt.initializer() != nullptr) {
    auto val = lower_expr_value(*let_stmt.initializer());
    auto* place = ctx_.alloc<MirPlace>();
    place->local = local_id;
    emit_effect(let_stmt.span(), MirStore{place, val});
  }
}

void MirBuilder::lower_assign(const HirAssign& assign) {
  auto place = lower_expr_place(*assign.target());
  auto val = lower_expr_value(*assign.value());
  emit_effect(assign.span(), MirStore{ctx_.alloc<MirPlace>(place), val});
}

void MirBuilder::lower_if(const HirIf& hir_if) {
  auto cond_val = lower_expr_value(*hir_if.condition());

  auto* then_bb = fresh_block();
  bool has_else = !hir_if.else_body().empty();
  auto* else_bb = has_else ? fresh_block() : nullptr;

  // Defer merge_bb — only create if a branch falls through.
  MirBlock* merge_bb = nullptr;

  // Emit cond_br. If no else, the false branch targets a merge block
  // that we create now (it's guaranteed to be reachable since there's
  // no else to terminate).
  if (!has_else) {
    merge_bb = fresh_block();
    else_bb = merge_bb;
  }

  emit_terminator(hir_if.span(),
                  MirCondBr{cond_val, then_bb->id, else_bb->id});

  // Then block.
  switch_to_block(then_bb);
  for (const auto* stmt : hir_if.then_body()) {
    lower_stmt(*stmt);
  }
  bool then_terminated = block_terminated();
  if (!then_terminated) {
    if (merge_bb == nullptr) {
      merge_bb = fresh_block();
    }
    emit_terminator(hir_if.span(), MirBr{merge_bb->id});
  }

  // Else block.
  bool else_terminated = false;
  if (has_else) {
    switch_to_block(else_bb);
    for (const auto* stmt : hir_if.else_body()) {
      lower_stmt(*stmt);
    }
    else_terminated = block_terminated();
    if (!else_terminated) {
      if (merge_bb == nullptr) {
        merge_bb = fresh_block();
      }
      emit_terminator(hir_if.span(), MirBr{merge_bb->id});
    }
  }

  if (merge_bb != nullptr) {
    switch_to_block(merge_bb);
  } else {
    // Both branches terminated; no merge block needed.
    current_block_ = nullptr;
  }
}

void MirBuilder::lower_while(const HirWhile& hir_while) {
  auto* cond_bb = fresh_block();
  auto* body_bb = fresh_block();
  auto* exit_bb = fresh_block();

  emit_terminator(hir_while.span(), MirBr{cond_bb->id});

  // Condition block.
  switch_to_block(cond_bb);
  auto cond_val = lower_expr_value(*hir_while.condition());
  emit_terminator(hir_while.span(),
                  MirCondBr{cond_val, body_bb->id, exit_bb->id});

  // Body block.
  switch_to_block(body_bb);
  for (const auto* stmt : hir_while.body()) {
    lower_stmt(*stmt);
  }
  if (!block_terminated()) {
    emit_terminator(hir_while.span(), MirBr{cond_bb->id});
  }

  switch_to_block(exit_bb);
}

void MirBuilder::lower_for(const HirFor& hir_for) {
  // Evaluate iterable.
  auto iter_src = lower_expr_value(*hir_for.iterable());

  // IterInit.
  auto iter_val = emit_value(
      hir_for.iterable()->type(), hir_for.span(),
      MirIterInit{iter_src});

  // Declare loop variable.
  const Type* var_type = hir_for.iterable()->type();
  auto var_local =
      declare_local(hir_for.var_symbol(), var_type, hir_for.span());

  auto* cond_bb = fresh_block();
  auto* body_bb = fresh_block();
  auto* exit_bb = fresh_block();

  emit_terminator(hir_for.span(), MirBr{cond_bb->id});

  // Condition: IterHasNext.
  switch_to_block(cond_bb);
  auto has_next_val = emit_value(
      types_.bool_type(), hir_for.span(),
      MirIterHasNext{iter_val});

  emit_terminator(hir_for.span(),
                  MirCondBr{has_next_val, body_bb->id, exit_bb->id});

  // Body: IterNext + store to loop var.
  switch_to_block(body_bb);
  auto next_val = emit_value(
      var_type, hir_for.span(), MirIterNext{iter_val});

  auto* place = ctx_.alloc<MirPlace>();
  place->local = var_local;
  emit_effect(hir_for.span(), MirStore{place, next_val});

  for (const auto* stmt : hir_for.body()) {
    lower_stmt(*stmt);
  }
  if (!block_terminated()) {
    emit_terminator(hir_for.span(), MirBr{cond_bb->id});
  }

  switch_to_block(exit_bb);
}

void MirBuilder::lower_return(const HirReturn& ret) {
  // Emit value before region exits so the value is computed inside the region.
  MirValueId ret_val;
  bool has_val = ret.value() != nullptr;
  if (has_val) {
    ret_val = lower_expr_value(*ret.value());
  }

  // Exit all active mode/resource regions in reverse order.
  emit_region_exits(ret.span());

  emit_terminator(ret.span(), MirReturn{ret_val, has_val});
}

void MirBuilder::lower_expr_stmt(const HirExprStmt& expr_stmt) {
  lower_expr_value(*expr_stmt.expr());
}

void MirBuilder::lower_mode(const HirMode& mode) {
  emit_effect(mode.span(),
              MirModeEnter{mode.mode(), mode.mode_name()});

  active_regions_.push_back(
      {.exit_payload = MirModeExit{mode.mode()},
       .span = mode.span()});

  for (const auto* stmt : mode.body()) {
    lower_stmt(*stmt);
  }

  active_regions_.pop_back();

  if (!block_terminated()) {
    emit_effect(mode.span(), MirModeExit{mode.mode()});
  }
}

void MirBuilder::lower_resource(const HirResource& res) {
  emit_effect(res.span(),
              MirResourceEnter{res.resource_kind(), res.resource_name()});

  active_regions_.push_back(
      {.exit_payload = MirResourceExit{},
       .span = res.span()});

  for (const auto* stmt : res.body()) {
    lower_stmt(*stmt);
  }

  active_regions_.pop_back();

  if (!block_terminated()) {
    emit_effect(res.span(), MirResourceExit{});
  }
}

// ---------------------------------------------------------------------------
// Expression lowering — value
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto MirBuilder::lower_expr_value(const HirExpr& expr) -> MirValueId {
  switch (expr.kind()) {
  case HirKind::IntLiteral: {
    const auto& lit = static_cast<const HirIntLiteral&>(expr);
    return emit_value(expr, MirConstInt{lit.value()});
  }

  case HirKind::FloatLiteral: {
    const auto& lit = static_cast<const HirFloatLiteral&>(expr);
    return emit_value(expr, MirConstFloat{lit.value()});
  }

  case HirKind::BoolLiteral: {
    const auto& lit = static_cast<const HirBoolLiteral&>(expr);
    return emit_value(expr, MirConstBool{lit.value()});
  }

  case HirKind::StringLiteral: {
    const auto& lit = static_cast<const HirStringLiteral&>(expr);
    return emit_value(expr, MirConstString{lit.value()});
  }

  case HirKind::SymbolRef: {
    const auto& ref = static_cast<const HirSymbolRef&>(expr);
    if (ref.symbol() != nullptr) {
      auto it = symbol_to_local_.find(ref.symbol());
      if (it != symbol_to_local_.end()) {
        auto* place = ctx_.alloc<MirPlace>();
        place->local = it->second;
        return emit_value(expr, MirLoad{place});
      }
    }
    return emit_value(expr, MirFnRef{ref.symbol()});
  }

  case HirKind::Unary: {
    const auto& un = static_cast<const HirUnary&>(expr);

    if (un.op() == UnaryOp::AddrOf) {
      auto place = lower_expr_place(*un.operand());
      return emit_value(expr, MirAddrOf{ctx_.alloc<MirPlace>(place)});
    }

    if (un.op() == UnaryOp::Deref) {
      auto place = lower_expr_place(expr);
      return emit_value(expr, MirLoad{ctx_.alloc<MirPlace>(place)});
    }

    auto val = lower_expr_value(*un.operand());
    return emit_value(expr, MirUnary{un.op(), val});
  }

  case HirKind::Binary: {
    const auto& bin = static_cast<const HirBinary&>(expr);
    auto left = lower_expr_value(*bin.left());
    auto right = lower_expr_value(*bin.right());
    return emit_value(expr, MirBinary{bin.op(), left, right});
  }

  case HirKind::Call: {
    const auto& call = static_cast<const HirCall&>(expr);
    auto callee_val = lower_expr_value(*call.callee());
    auto* args = ctx_.alloc<std::vector<MirValueId>>();
    for (const auto* arg : call.args()) {
      args->push_back(lower_expr_value(*arg));
    }
    return emit_value(expr, MirCall{callee_val, args});
  }

  case HirKind::Pipe: {
    const auto& pipe = static_cast<const HirPipe&>(expr);
    auto left_val = lower_expr_value(*pipe.left());
    auto callee_val = lower_expr_value(*pipe.right());
    auto* args = ctx_.alloc<std::vector<MirValueId>>();
    args->push_back(left_val);
    return emit_value(expr, MirCall{callee_val, args});
  }

  case HirKind::Field: {
    const auto& field = static_cast<const HirField&>(expr);
    auto obj = lower_expr_value(*field.object());
    return emit_value(expr, MirFieldAccess{
        obj, field.field_name(),
        resolve_field_index(field.object()->type(), field.field_name())});
  }

  case HirKind::Index: {
    const auto& idx = static_cast<const HirIndex&>(expr);
    auto obj = lower_expr_value(*idx.object());
    MirValueId index_val;
    if (!idx.indices().empty()) {
      index_val = lower_expr_value(*idx.indices()[0]);
    }
    return emit_value(expr, MirIndexAccess{obj, index_val});
  }

  case HirKind::Lambda: {
    const auto& lam = static_cast<const HirLambda&>(expr);

    auto* lam_fn = ctx_.alloc<MirFunction>();
    lam_fn->span = expr.span();
    if (lam.body() != nullptr && lam.body()->type() != nullptr) {
      lam_fn->return_type = lam.body()->type();
    }

    // Save and reset builder state.
    auto* saved_fn = current_fn_;
    auto* saved_block = current_block_;
    auto saved_value_id = next_value_id_;
    auto saved_block_id = next_block_id_;
    auto saved_locals = symbol_to_local_;

    current_fn_ = lam_fn;
    current_block_ = nullptr;
    next_value_id_ = 0;
    next_block_id_ = 0;
    symbol_to_local_.clear();

    for (const auto& param : lam.params()) {
      declare_local(param.symbol, param.type, param.span, /*is_param=*/true);
    }

    auto* entry = fresh_block();
    switch_to_block(entry);

    if (lam.body() != nullptr) {
      auto body_val = lower_expr_value(*lam.body());
      emit_terminator(lam.body()->span(), MirReturn{body_val, true});
    } else if (!block_terminated()) {
      emit_terminator(expr.span(), MirReturn{{}, false});
    }

    // Restore builder state.
    current_fn_ = saved_fn;
    current_block_ = saved_block;
    next_value_id_ = saved_value_id;
    next_block_id_ = saved_block_id;
    symbol_to_local_ = std::move(saved_locals);

    if (current_module_ != nullptr) {
      current_module_->functions.push_back(lam_fn);
    }

    return emit_value(expr, MirLambdaInst{lam_fn});
  }

  default:
    error(expr.span(), "unsupported expression in MIR builder");
    return {};
  }
}

// ---------------------------------------------------------------------------
// Expression lowering — place
// ---------------------------------------------------------------------------

auto MirBuilder::lower_expr_place(const HirExpr& expr) -> MirPlace {
  switch (expr.kind()) {
  case HirKind::SymbolRef: {
    const auto& ref = static_cast<const HirSymbolRef&>(expr);
    if (ref.symbol() != nullptr) {
      auto it = symbol_to_local_.find(ref.symbol());
      if (it != symbol_to_local_.end()) {
        return {.local = it->second, .projections = {}};
      }
    }
    error(expr.span(), "cannot resolve symbol to local for place");
    return {};
  }

  case HirKind::Field: {
    const auto& field = static_cast<const HirField&>(expr);
    auto base = lower_expr_place(*field.object());
    base.projections.push_back(
        {.kind = MirProjectionKind::Field,
         .field_name = field.field_name(),
         .field_index = resolve_field_index(field.object()->type(), field.field_name()),
         .index_value = {}});
    return base;
  }

  case HirKind::Index: {
    const auto& idx = static_cast<const HirIndex&>(expr);
    auto base = lower_expr_place(*idx.object());
    MirValueId index_val;
    if (!idx.indices().empty()) {
      index_val = lower_expr_value(*idx.indices()[0]);
    }
    base.projections.push_back(
        {.kind = MirProjectionKind::Index,
         .field_name = {},
         .field_index = 0,
         .index_value = index_val});
    return base;
  }

  case HirKind::Unary: {
    const auto& un = static_cast<const HirUnary&>(expr);
    if (un.op() == UnaryOp::Deref) {
      auto base = lower_expr_place(*un.operand());
      base.projections.push_back(
          {.kind = MirProjectionKind::Deref,
           .field_name = {},
           .field_index = 0,
           .index_value = {}});
      return base;
    }
    break;
  }

  default:
    break;
  }

  error(expr.span(), "expression is not a valid place");
  return {};
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

auto MirBuilder::fresh_value() -> MirValueId {
  return {.id = next_value_id_++};
}

auto MirBuilder::fresh_block() -> MirBlock* {
  auto* block = ctx_.alloc<MirBlock>();
  block->id = {.id = next_block_id_++};
  current_fn_->blocks.push_back(block);
  return block;
}

auto MirBuilder::declare_local(const Symbol* sym, const Type* type,
                               Span span, bool is_param) -> LocalId {
  LocalId lid = {.id = static_cast<uint32_t>(current_fn_->locals.size())};
  current_fn_->locals.push_back(
      {.id = lid, .symbol = sym, .type = type, .span = span,
       .is_param = is_param});
  if (sym != nullptr) {
    symbol_to_local_[sym] = lid;
  }
  return lid;
}

void MirBuilder::emit(MirInst* inst) {
  if (current_block_ != nullptr) {
    current_block_->insts.push_back(inst);
  }
}

void MirBuilder::switch_to_block(MirBlock* block) {
  current_block_ = block;
}

auto MirBuilder::block_terminated() const -> bool {
  if (current_block_ == nullptr) {
    return true;
  }
  if (current_block_->insts.empty()) {
    return false;
  }
  return is_terminator(current_block_->insts.back()->kind());
}

void MirBuilder::emit_region_exits(Span span) {
  for (auto it = active_regions_.rbegin(); it != active_regions_.rend();
       ++it) {
    emit_effect(span, it->exit_payload);
  }
}

void MirBuilder::error(Span span, std::string message) {
  diagnostics_.push_back(Diagnostic::error(span, std::move(message)));
}

// ---------------------------------------------------------------------------
// Free-function entry point
// ---------------------------------------------------------------------------

auto build_mir(const HirModule& module, MirContext& ctx,
               TypeContext& types) -> MirBuildResult {
  MirBuilder builder(ctx, types);
  return builder.build(module);
}

} // namespace dao
