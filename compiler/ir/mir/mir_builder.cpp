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
    // StructDecl: no MIR function to produce; skip.
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

  // Create entry block.
  auto* entry = fresh_block();
  switch_to_block(entry);

  // Lower body statements.
  for (const auto* stmt : fn.body()) {
    lower_stmt(*stmt);
  }

  // Ensure the last block has a terminator.
  if (!block_terminated()) {
    auto* ret = ctx_.alloc<MirInst>();
    ret->kind = MirInstKind::Return;
    ret->has_return_value = false;
    ret->span = fn.span();
    emit(ret);
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

    auto* store = ctx_.alloc<MirInst>();
    store->kind = MirInstKind::Store;
    store->span = let_stmt.span();
    store->place = ctx_.alloc<MirPlace>();
    store->place->local = local_id;
    store->store_value = val;
    emit(store);
  }
}

void MirBuilder::lower_assign(const HirAssign& assign) {
  auto place = lower_expr_place(*assign.target());
  auto val = lower_expr_value(*assign.value());

  auto* store = ctx_.alloc<MirInst>();
  store->kind = MirInstKind::Store;
  store->span = assign.span();
  store->place = ctx_.alloc<MirPlace>(place);
  store->store_value = val;
  emit(store);
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

  auto* cond_br = ctx_.alloc<MirInst>();
  cond_br->kind = MirInstKind::CondBr;
  cond_br->span = hir_if.span();
  cond_br->cond = cond_val;
  cond_br->then_block = then_bb->id;
  cond_br->else_block = else_bb->id;
  emit(cond_br);

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
    auto* br = ctx_.alloc<MirInst>();
    br->kind = MirInstKind::Br;
    br->br_target = merge_bb->id;
    br->span = hir_if.span();
    emit(br);
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
      auto* br = ctx_.alloc<MirInst>();
      br->kind = MirInstKind::Br;
      br->br_target = merge_bb->id;
      br->span = hir_if.span();
      emit(br);
    }
  }

  if (merge_bb != nullptr) {
    switch_to_block(merge_bb);
  } else {
    // Both branches terminated; no merge block needed.
    // Set current_block_ to nullptr — subsequent code is dead.
    current_block_ = nullptr;
  }
}

void MirBuilder::lower_while(const HirWhile& hir_while) {
  auto* cond_bb = fresh_block();
  auto* body_bb = fresh_block();
  auto* exit_bb = fresh_block();

  // Branch to condition block.
  auto* br_cond = ctx_.alloc<MirInst>();
  br_cond->kind = MirInstKind::Br;
  br_cond->br_target = cond_bb->id;
  br_cond->span = hir_while.span();
  emit(br_cond);

  // Condition block.
  switch_to_block(cond_bb);
  auto cond_val = lower_expr_value(*hir_while.condition());
  auto* cond_br = ctx_.alloc<MirInst>();
  cond_br->kind = MirInstKind::CondBr;
  cond_br->span = hir_while.span();
  cond_br->cond = cond_val;
  cond_br->then_block = body_bb->id;
  cond_br->else_block = exit_bb->id;
  emit(cond_br);

  // Body block.
  switch_to_block(body_bb);
  for (const auto* stmt : hir_while.body()) {
    lower_stmt(*stmt);
  }
  if (!block_terminated()) {
    auto* br_back = ctx_.alloc<MirInst>();
    br_back->kind = MirInstKind::Br;
    br_back->br_target = cond_bb->id;
    br_back->span = hir_while.span();
    emit(br_back);
  }

  switch_to_block(exit_bb);
}

void MirBuilder::lower_for(const HirFor& hir_for) {
  // Evaluate iterable.
  auto iter_src = lower_expr_value(*hir_for.iterable());

  // IterInit.
  auto* init = ctx_.alloc<MirInst>();
  init->kind = MirInstKind::IterInit;
  init->result = fresh_value();
  init->type = hir_for.iterable()->type();
  init->span = hir_for.span();
  init->iter_operand = iter_src;
  emit(init);
  auto iter_val = init->result;

  // Declare loop variable with iterable's element type.
  // Until element-type extraction exists, use iterable's own type.
  const Type* var_type = hir_for.iterable()->type();
  auto var_local =
      declare_local(hir_for.var_symbol(), var_type, hir_for.span());

  auto* cond_bb = fresh_block();
  auto* body_bb = fresh_block();
  auto* exit_bb = fresh_block();

  // Branch to condition.
  auto* br_cond = ctx_.alloc<MirInst>();
  br_cond->kind = MirInstKind::Br;
  br_cond->br_target = cond_bb->id;
  br_cond->span = hir_for.span();
  emit(br_cond);

  // Condition: IterHasNext.
  switch_to_block(cond_bb);
  auto* has_next = ctx_.alloc<MirInst>();
  has_next->kind = MirInstKind::IterHasNext;
  has_next->result = fresh_value();
  has_next->type = types_.bool_type();
  has_next->span = hir_for.span();
  has_next->iter_operand = iter_val;
  emit(has_next);

  auto* cond_br = ctx_.alloc<MirInst>();
  cond_br->kind = MirInstKind::CondBr;
  cond_br->span = hir_for.span();
  cond_br->cond = has_next->result;
  cond_br->then_block = body_bb->id;
  cond_br->else_block = exit_bb->id;
  emit(cond_br);

  // Body: IterNext + store to loop var.
  switch_to_block(body_bb);
  auto* next = ctx_.alloc<MirInst>();
  next->kind = MirInstKind::IterNext;
  next->result = fresh_value();
  next->type = var_type;
  next->span = hir_for.span();
  next->iter_operand = iter_val;
  emit(next);

  auto* store_var = ctx_.alloc<MirInst>();
  store_var->kind = MirInstKind::Store;
  store_var->span = hir_for.span();
  store_var->place = ctx_.alloc<MirPlace>();
  store_var->place->local = var_local;
  store_var->store_value = next->result;
  emit(store_var);

  for (const auto* stmt : hir_for.body()) {
    lower_stmt(*stmt);
  }
  if (!block_terminated()) {
    auto* br_back = ctx_.alloc<MirInst>();
    br_back->kind = MirInstKind::Br;
    br_back->br_target = cond_bb->id;
    br_back->span = hir_for.span();
    emit(br_back);
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

  auto* mir_ret = ctx_.alloc<MirInst>();
  mir_ret->kind = MirInstKind::Return;
  mir_ret->span = ret.span();
  if (has_val) {
    mir_ret->return_value = ret_val;
    mir_ret->has_return_value = true;
  }

  emit(mir_ret);
}

void MirBuilder::lower_expr_stmt(const HirExprStmt& expr_stmt) {
  lower_expr_value(*expr_stmt.expr());
}

void MirBuilder::lower_mode(const HirMode& mode) {
  auto* enter = ctx_.alloc<MirInst>();
  enter->kind = MirInstKind::ModeEnter;
  enter->span = mode.span();
  enter->mode_kind = mode.mode();
  enter->region_name = mode.mode_name();
  emit(enter);

  active_regions_.push_back(
      {.exit_kind = MirInstKind::ModeExit,
       .mode_kind = mode.mode(),
       .span = mode.span()});

  for (const auto* stmt : mode.body()) {
    lower_stmt(*stmt);
  }

  active_regions_.pop_back();

  if (!block_terminated()) {
    auto* exit = ctx_.alloc<MirInst>();
    exit->kind = MirInstKind::ModeExit;
    exit->span = mode.span();
    exit->mode_kind = mode.mode();
    emit(exit);
  }
}

void MirBuilder::lower_resource(const HirResource& res) {
  auto* enter = ctx_.alloc<MirInst>();
  enter->kind = MirInstKind::ResourceEnter;
  enter->span = res.span();
  enter->region_kind = res.resource_kind();
  enter->region_name = res.resource_name();
  emit(enter);

  active_regions_.push_back(
      {.exit_kind = MirInstKind::ResourceExit,
       .mode_kind = {},
       .span = res.span()});

  for (const auto* stmt : res.body()) {
    lower_stmt(*stmt);
  }

  active_regions_.pop_back();

  if (!block_terminated()) {
    auto* exit = ctx_.alloc<MirInst>();
    exit->kind = MirInstKind::ResourceExit;
    exit->span = res.span();
    emit(exit);
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
    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::ConstInt;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->const_int = lit.value();
    emit(inst);
    return inst->result;
  }

  case HirKind::FloatLiteral: {
    const auto& lit = static_cast<const HirFloatLiteral&>(expr);
    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::ConstFloat;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->const_float = lit.value();
    emit(inst);
    return inst->result;
  }

  case HirKind::BoolLiteral: {
    const auto& lit = static_cast<const HirBoolLiteral&>(expr);
    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::ConstBool;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->const_bool = lit.value();
    emit(inst);
    return inst->result;
  }

  case HirKind::StringLiteral: {
    const auto& lit = static_cast<const HirStringLiteral&>(expr);
    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::ConstString;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->const_string = lit.value();
    emit(inst);
    return inst->result;
  }

  case HirKind::SymbolRef: {
    const auto& ref = static_cast<const HirSymbolRef&>(expr);
    if (ref.symbol() != nullptr) {
      auto it = symbol_to_local_.find(ref.symbol());
      if (it != symbol_to_local_.end()) {
        // Load from local.
        auto* load = ctx_.alloc<MirInst>();
        load->kind = MirInstKind::Load;
        load->result = fresh_value();
        load->type = expr.type();
        load->span = expr.span();
        load->place = ctx_.alloc<MirPlace>();
        load->place->local = it->second;
        emit(load);
        return load->result;
      }
    }
    // Function reference — emit FnRef to produce a callable value.
    auto* fn_ref = ctx_.alloc<MirInst>();
    fn_ref->kind = MirInstKind::FnRef;
    fn_ref->result = fresh_value();
    fn_ref->type = expr.type();
    fn_ref->span = expr.span();
    fn_ref->fn_symbol = ref.symbol();
    emit(fn_ref);
    return fn_ref->result;
  }

  case HirKind::Unary: {
    const auto& un = static_cast<const HirUnary&>(expr);

    if (un.op() == UnaryOp::AddrOf) {
      auto place = lower_expr_place(*un.operand());
      auto* inst = ctx_.alloc<MirInst>();
      inst->kind = MirInstKind::AddrOf;
      inst->result = fresh_value();
      inst->type = expr.type();
      inst->span = expr.span();
      inst->place = ctx_.alloc<MirPlace>(place);
      emit(inst);
      return inst->result;
    }

    if (un.op() == UnaryOp::Deref) {
      auto val = lower_expr_value(*un.operand());
      auto* inst = ctx_.alloc<MirInst>();
      inst->kind = MirInstKind::Unary;
      inst->result = fresh_value();
      inst->type = expr.type();
      inst->span = expr.span();
      inst->unary_op = un.op();
      inst->operand = val;
      emit(inst);
      return inst->result;
    }

    auto val = lower_expr_value(*un.operand());
    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::Unary;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->unary_op = un.op();
    inst->operand = val;
    emit(inst);
    return inst->result;
  }

  case HirKind::Binary: {
    const auto& bin = static_cast<const HirBinary&>(expr);
    auto left = lower_expr_value(*bin.left());
    auto right = lower_expr_value(*bin.right());
    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::Binary;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->binary_op = bin.op();
    inst->lhs = left;
    inst->rhs = right;
    emit(inst);
    return inst->result;
  }

  case HirKind::Call: {
    const auto& call = static_cast<const HirCall&>(expr);
    auto callee_val = lower_expr_value(*call.callee());

    auto* args = ctx_.alloc<std::vector<MirValueId>>();
    for (const auto* arg : call.args()) {
      args->push_back(lower_expr_value(*arg));
    }

    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::Call;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->callee = callee_val;
    inst->call_args = args;
    emit(inst);
    return inst->result;
  }

  case HirKind::Pipe: {
    const auto& pipe = static_cast<const HirPipe&>(expr);
    auto left_val = lower_expr_value(*pipe.left());

    // Lower pipe as: call right(left).
    auto callee_val = lower_expr_value(*pipe.right());
    auto* args = ctx_.alloc<std::vector<MirValueId>>();
    args->push_back(left_val);

    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::Call;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->callee = callee_val;
    inst->call_args = args;
    emit(inst);
    return inst->result;
  }

  case HirKind::Field: {
    const auto& field = static_cast<const HirField&>(expr);
    auto obj = lower_expr_value(*field.object());
    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::FieldAccess;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->access_object = obj;
    inst->access_field = field.field_name();
    emit(inst);
    return inst->result;
  }

  case HirKind::Index: {
    const auto& idx = static_cast<const HirIndex&>(expr);
    auto obj = lower_expr_value(*idx.object());
    // Only single-index for now.
    MirValueId index_val;
    if (!idx.indices().empty()) {
      index_val = lower_expr_value(*idx.indices()[0]);
    }
    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::IndexAccess;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->access_object = obj;
    inst->access_index = index_val;
    emit(inst);
    return inst->result;
  }

  case HirKind::Lambda: {
    const auto& lam = static_cast<const HirLambda&>(expr);

    // Lower lambda body as a nested MirFunction registered on the module.
    auto* lam_fn = ctx_.alloc<MirFunction>();
    lam_fn->span = expr.span();
    // Derive return type from body expression type when available.
    if (lam.body() != nullptr && lam.body()->type() != nullptr) {
      lam_fn->return_type = lam.body()->type();
    }
    // Lambda symbol is typically nullptr; symbol field stays null.

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

    // Lambda params.
    for (const auto& param : lam.params()) {
      declare_local(param.symbol, param.type, param.span,
                    /*is_param=*/true);
    }

    auto* entry = fresh_block();
    switch_to_block(entry);

    // Lambda body is an expression; lower and return.
    if (lam.body() != nullptr) {
      auto body_val = lower_expr_value(*lam.body());
      auto* ret = ctx_.alloc<MirInst>();
      ret->kind = MirInstKind::Return;
      ret->span = lam.body()->span();
      ret->return_value = body_val;
      ret->has_return_value = true;
      emit(ret);
    } else if (!block_terminated()) {
      auto* ret = ctx_.alloc<MirInst>();
      ret->kind = MirInstKind::Return;
      ret->has_return_value = false;
      ret->span = expr.span();
      emit(ret);
    }

    // Restore builder state.
    current_fn_ = saved_fn;
    current_block_ = saved_block;
    next_value_id_ = saved_value_id;
    next_block_id_ = saved_block_id;
    symbol_to_local_ = std::move(saved_locals);

    // Register lambda function on the module so printers and backends see it.
    if (current_module_ != nullptr) {
      current_module_->functions.push_back(lam_fn);
    }

    auto* inst = ctx_.alloc<MirInst>();
    inst->kind = MirInstKind::Lambda;
    inst->result = fresh_value();
    inst->type = expr.type();
    inst->span = expr.span();
    inst->lambda_fn = lam_fn;
    emit(inst);
    return inst->result;
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
         .field_index = 0,
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
  // nullptr means all paths already terminated (e.g. both if branches
  // returned). Treat as terminated so subsequent dead code is skipped.
  if (current_block_ == nullptr) {
    return true;
  }
  if (current_block_->insts.empty()) {
    return false;
  }
  return is_terminator(current_block_->insts.back()->kind);
}

void MirBuilder::emit_region_exits(Span span) {
  for (auto it = active_regions_.rbegin(); it != active_regions_.rend();
       ++it) {
    auto* exit_inst = ctx_.alloc<MirInst>();
    exit_inst->kind = it->exit_kind;
    exit_inst->span = span;
    if (it->exit_kind == MirInstKind::ModeExit) {
      exit_inst->mode_kind = it->mode_kind;
    }
    emit(exit_inst);
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
