#include "frontend/typecheck/type_checker.h"

#include "frontend/typecheck/type_conversion.h"

namespace dao {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TypeChecker::TypeChecker(TypeContext& types, const ResolveResult& resolve)
    : types_(types), resolve_(resolve) {
  // Build decl_span.offset -> Symbol* map for declaration-site lookups.
  for (const auto& sym : resolve_.context.symbols()) {
    if (sym->decl_span.length > 0) {
      decl_symbols_[sym->decl_span.offset] = sym.get();
    }
  }
}

// ---------------------------------------------------------------------------
// Top-level entry
// ---------------------------------------------------------------------------

auto TypeChecker::check(const FileNode& file) -> TypeCheckResult {
  // Pass 1: register all top-level declaration types.
  register_declarations(file);

  // Pass 2: check all declaration bodies.
  for (const auto* decl : file.declarations()) {
    check_declaration(decl);
  }

  return {.typed = std::move(typed_), .diagnostics = std::move(diagnostics_)};
}

// ---------------------------------------------------------------------------
// TypeNode -> Type* bridge
// ---------------------------------------------------------------------------

auto TypeChecker::resolve_type_node(const TypeNode* node) -> const Type* {
  if (node == nullptr) {
    return nullptr;
  }

  switch (node->kind()) {
  case NodeKind::NamedType: {
    const auto* named = static_cast<const NamedTypeNode*>(node);
    const auto& path = named->name();
    if (path.segments.size() != 1) {
      error(node->span(), "qualified type names are not yet supported");
      return nullptr;
    }
    auto name = path.segments[0];

    // Check builtin scalars.
    auto builtin = builtin_kind_from_name(name);
    if (builtin.has_value()) {
      return types_.builtin(*builtin);
    }

    // Check predeclared types.
    if (name == "void") {
      return types_.void_type();
    }
    if (name == "string") {
      // string is a predeclared named type. For now, use a sentinel
      // named type with a null decl_id.
      return types_.named_type(nullptr, "string", {});
    }

    // Look up user-defined types via resolver symbols.
    auto it = resolve_.uses.find(node->span().offset);
    if (it != resolve_.uses.end()) {
      return resolve_symbol_type(it->second);
    }

    error(node->span(), "unknown type '" + std::string(name) + "'");
    return nullptr;
  }

  case NodeKind::PointerType: {
    const auto* ptr = static_cast<const PointerTypeNode*>(node);
    const auto* pointee = resolve_type_node(ptr->pointee());
    if (pointee == nullptr) {
      return nullptr;
    }
    return types_.pointer_to(pointee);
  }

  default:
    error(node->span(), "unsupported type syntax");
    return nullptr;
  }
}

// ---------------------------------------------------------------------------
// Symbol -> Type* bridge
// ---------------------------------------------------------------------------

auto TypeChecker::resolve_symbol_type(const Symbol* sym) -> const Type* {
  if (sym == nullptr) {
    return nullptr;
  }

  // Check cache first.
  auto it = symbol_types_.find(sym);
  if (it != symbol_types_.end()) {
    return it->second;
  }

  const Type* result = nullptr;

  switch (sym->kind) {
  case SymbolKind::Function: {
    // Function symbol -> derive TypeFunction from declaration.
    if (sym->decl == nullptr) {
      break;
    }
    const auto* fn = static_cast<const FunctionDeclNode*>(sym->decl);
    std::vector<const Type*> param_types;
    bool valid = true;
    for (const auto& param : fn->params()) {
      const auto* pt = resolve_type_node(param.type);
      if (pt == nullptr) {
        valid = false;
      }
      param_types.push_back(pt);
    }
    const auto* ret = fn->return_type() != nullptr
                          ? resolve_type_node(fn->return_type())
                          : types_.void_type();
    if (!valid || ret == nullptr) {
      break;
    }
    result = types_.function_type(std::move(param_types), ret);
    break;
  }

  case SymbolKind::Param: {
    // Parameter symbol -> type from its TypeNode.
    if (sym->decl == nullptr) {
      break;
    }
    // The decl for a param points to the FunctionDeclNode. We need to find
    // the matching param by name.
    const auto* fn = static_cast<const FunctionDeclNode*>(sym->decl);
    for (const auto& p : fn->params()) {
      if (p.name == sym->name) {
        result = resolve_type_node(p.type);
        break;
      }
    }
    break;
  }

  case SymbolKind::Local: {
    // Local type is set during let/for checking. If already cached, we
    // would have returned above. Return nullptr for now — it will be
    // populated during statement checking.
    break;
  }

  case SymbolKind::Type: {
    // Struct type — look up via decl_id.
    if (sym->decl != nullptr) {
      result = resolve_symbol_type_for_type_decl(sym);
    }
    break;
  }

  case SymbolKind::Builtin: {
    auto bk = builtin_kind_from_name(sym->name);
    if (bk.has_value()) {
      result = types_.builtin(*bk);
    }
    break;
  }

  case SymbolKind::Predeclared: {
    if (sym->name == "void") {
      result = types_.void_type();
    } else if (sym->name == "string") {
      result = types_.named_type(nullptr, "string", {});
    }
    break;
  }

  case SymbolKind::LambdaParam:
    // Lambda params are typed contextually; handled in check_lambda.
    break;

  case SymbolKind::Field:
  case SymbolKind::Module:
    // Not yet handled.
    break;
  }

  if (result != nullptr) {
    symbol_types_[sym] = result;
  }
  return result;
}

// Helper for Type-kind symbols (structs).
auto TypeChecker::resolve_symbol_type_for_type_decl(const Symbol* sym) -> const Type* {
  // Check if it was already registered during pass 1.
  auto it = symbol_types_.find(sym);
  if (it != symbol_types_.end()) {
    return it->second;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Pass 1: register declaration types
// ---------------------------------------------------------------------------

void TypeChecker::register_declarations(const FileNode& file) {
  for (const auto* decl : file.declarations()) {
    switch (decl->kind()) {
    case NodeKind::FunctionDecl: {
      const auto* fn = static_cast<const FunctionDeclNode*>(decl);
      // Find the symbol for this function via declaration map.
      auto decl_it = decl_symbols_.find(fn->name_span().offset);
      if (decl_it == decl_symbols_.end()) {
        break;
      }
      const auto* sym = decl_it->second;

      // Build function type.
      std::vector<const Type*> param_types;
      bool valid = true;
      for (const auto& param : fn->params()) {
        const auto* pt = resolve_type_node(param.type);
        if (pt == nullptr) {
          valid = false;
        }
        param_types.push_back(pt);
      }
      const auto* ret = fn->return_type() != nullptr
                            ? resolve_type_node(fn->return_type())
                            : types_.void_type();
      if (valid && ret != nullptr) {
        const auto* fn_type = types_.function_type(std::move(param_types), ret);
        symbol_types_[sym] = fn_type;
        typed_.set_decl_type(fn, fn_type);
      }
      break;
    }

    case NodeKind::StructDecl: {
      const auto* st = static_cast<const StructDeclNode*>(decl);
      // Find symbol via declaration map.
      auto decl_it = decl_symbols_.find(st->name_span().offset);
      if (decl_it == decl_symbols_.end()) {
        break;
      }
      const auto* sym = decl_it->second;

      // Build struct type from member let-statements.
      std::vector<StructField> fields;
      for (const auto* member : st->members()) {
        if (member->kind() == NodeKind::LetStatement) {
          const auto* let = static_cast<const LetStatementNode*>(member);
          const auto* field_type = resolve_type_node(let->type());
          if (field_type != nullptr) {
            fields.push_back({let->name(), field_type});
          }
        }
      }
      const auto* struct_type =
          types_.make_struct(sym, st->name(), std::move(fields));
      symbol_types_[sym] = struct_type;
      typed_.set_decl_type(st, struct_type);
      break;
    }

    case NodeKind::AliasDecl: {
      const auto* alias = static_cast<const AliasDeclNode*>(decl);
      auto decl_it = decl_symbols_.find(alias->name_span().offset);
      if (decl_it == decl_symbols_.end()) {
        break;
      }
      const auto* sym = decl_it->second;

      // Resolve the aliased type and cache it so later lookups of the
      // alias name transparently return the underlying type.
      const auto* aliased_type = resolve_type_node(alias->type());
      if (aliased_type != nullptr) {
        symbol_types_[sym] = aliased_type;
        typed_.set_decl_type(alias, aliased_type);
      }
      break;
    }

    default:
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Declaration checking (pass 2)
// ---------------------------------------------------------------------------

void TypeChecker::check_declaration(const Decl* decl) {
  switch (decl->kind()) {
  case NodeKind::FunctionDecl:
    check_function(static_cast<const FunctionDeclNode*>(decl));
    break;
  case NodeKind::StructDecl:
    check_struct(static_cast<const StructDeclNode*>(decl));
    break;
  default:
    // AliasDecl — no body checking needed yet.
    break;
  }
}

void TypeChecker::check_function(const FunctionDeclNode* fn) {
  // Determine return type.
  const auto* ret_type = fn->return_type() != nullptr
                             ? resolve_type_node(fn->return_type())
                             : types_.void_type();

  // Set up param types in symbol cache.
  for (const auto& param : fn->params()) {
    auto decl_it = decl_symbols_.find(param.name_span.offset);
    if (decl_it != decl_symbols_.end()) {
      const auto* pt = resolve_type_node(param.type);
      if (pt != nullptr) {
        symbol_types_[decl_it->second] = pt;
      }
    }
  }

  // Save and set context.
  auto saved_ctx = ctx_;
  ctx_.return_type = ret_type;

  if (fn->is_expr_bodied()) {
    // Expression-bodied: -> expr
    const auto* expr_type = check_expr(fn->expr_body());
    if (expr_type != nullptr && ret_type != nullptr &&
        !is_assignable(expr_type, ret_type)) {
      error(fn->expr_body()->span(),
            "expression body type '" + print_type(expr_type) +
                "' does not match return type '" + print_type(ret_type) + "'");
    }
  } else {
    // Block-bodied: check statements.
    check_body(fn->body());
  }

  ctx_ = saved_ctx;
}

void TypeChecker::check_struct(const StructDeclNode* st) {
  // Struct members are validated during pass 1 (field types resolved).
  // Nothing further to check in bodies for now.
  (void)st;
}

// ---------------------------------------------------------------------------
// Statement checking
// ---------------------------------------------------------------------------

void TypeChecker::check_body(const std::vector<Stmt*>& body) {
  for (const auto* stmt : body) {
    check_statement(stmt);
  }
}

void TypeChecker::check_statement(const Stmt* stmt) {
  switch (stmt->kind()) {
  case NodeKind::LetStatement:
    check_let(static_cast<const LetStatementNode*>(stmt));
    break;
  case NodeKind::Assignment:
    check_assignment(static_cast<const AssignmentNode*>(stmt));
    break;
  case NodeKind::IfStatement:
    check_if(static_cast<const IfStatementNode*>(stmt));
    break;
  case NodeKind::WhileStatement:
    check_while(static_cast<const WhileStatementNode*>(stmt));
    break;
  case NodeKind::ForStatement:
    check_for(static_cast<const ForStatementNode*>(stmt));
    break;
  case NodeKind::ModeBlock:
    check_mode_block(static_cast<const ModeBlockNode*>(stmt));
    break;
  case NodeKind::ResourceBlock:
    check_resource_block(static_cast<const ResourceBlockNode*>(stmt));
    break;
  case NodeKind::ReturnStatement:
    check_return(static_cast<const ReturnStatementNode*>(stmt));
    break;
  case NodeKind::ExpressionStatement:
    check_expr_stmt(static_cast<const ExpressionStatementNode*>(stmt));
    break;
  default:
    break;
  }
}

void TypeChecker::check_let(const LetStatementNode* let) {
  const Type* declared_type = nullptr;
  if (let->type() != nullptr) {
    declared_type = resolve_type_node(let->type());
  }

  const Type* init_type = nullptr;
  if (let->initializer() != nullptr) {
    init_type = check_expr(let->initializer());
  }

  if (declared_type != nullptr && init_type != nullptr) {
    // let x: T = expr — check assignability.
    if (!is_assignable(init_type, declared_type)) {
      error(let->initializer()->span(),
            "initializer type '" + print_type(init_type) +
                "' is not assignable to '" + print_type(declared_type) + "'");
    }
    typed_.set_local_type(let, declared_type);
  } else if (declared_type != nullptr) {
    // let x: T — type without initializer.
    typed_.set_local_type(let, declared_type);
  } else if (init_type != nullptr) {
    // let x = expr — infer from initializer.
    typed_.set_local_type(let, init_type);
  } else {
    // let x — no type, no initializer.
    error(let->span(),
          "declaration without type annotation requires an initializer");
  }

  // Cache in symbol table for later identifier lookups.
  const auto* local_type = typed_.local_type(let);
  if (local_type != nullptr) {
    auto decl_it = decl_symbols_.find(let->name_span().offset);
    if (decl_it != decl_symbols_.end()) {
      symbol_types_[decl_it->second] = local_type;
    }
  }
}

void TypeChecker::check_assignment(const AssignmentNode* assign) {
  if (!is_lvalue(assign->target())) {
    error(assign->target()->span(), "invalid assignment target");
  }

  const auto* target_type = check_expr(assign->target());
  const auto* value_type = check_expr(assign->value());

  if (target_type != nullptr && value_type != nullptr &&
      !is_assignable(value_type, target_type)) {
    error(assign->value()->span(),
          "cannot assign '" + print_type(value_type) + "' to '" +
              print_type(target_type) + "'");
  }
}

void TypeChecker::check_if(const IfStatementNode* ifn) {
  const auto* cond_type = check_expr(ifn->condition());
  if (cond_type != nullptr && !is_assignable(cond_type, types_.bool_type())) {
    error(ifn->condition()->span(),
          "condition must be 'bool', got '" + print_type(cond_type) + "'");
  }
  check_body(ifn->then_body());
  if (ifn->has_else()) {
    check_body(ifn->else_body());
  }
}

void TypeChecker::check_while(const WhileStatementNode* wh) {
  const auto* cond_type = check_expr(wh->condition());
  if (cond_type != nullptr && !is_assignable(cond_type, types_.bool_type())) {
    error(wh->condition()->span(),
          "condition must be 'bool', got '" + print_type(cond_type) + "'");
  }
  check_body(wh->body());
}

void TypeChecker::check_for(const ForStatementNode* fo) {
  // For now, iteration semantics are not frozen. Accept the iterable
  // expression but do not enforce element type derivation.
  const auto* iter_type = check_expr(fo->iterable());
  (void)iter_type;

  // Bind loop variable — for now treat as untyped placeholder.
  auto decl_it = decl_symbols_.find(fo->var_span().offset);
  if (decl_it != decl_symbols_.end()) {
    // TODO: derive element type from iterable once iteration is frozen.
    // For now, leave as nullptr (untyped).
    (void)decl_it;
  }

  check_body(fo->body());
}

void TypeChecker::check_mode_block(const ModeBlockNode* mb) {
  auto saved_ctx = ctx_;
  ctx_.active_modes.insert(mb->mode_name());
  check_body(mb->body());
  ctx_ = saved_ctx;
}

void TypeChecker::check_resource_block(const ResourceBlockNode* rb) {
  check_body(rb->body());
}

void TypeChecker::check_return(const ReturnStatementNode* ret) {
  if (ret->value() != nullptr) {
    const auto* val_type = check_expr(ret->value());
    if (val_type != nullptr && ctx_.return_type != nullptr &&
        !is_assignable(val_type, ctx_.return_type)) {
      error(ret->value()->span(),
            "return type '" + print_type(val_type) +
                "' does not match function return type '" +
                print_type(ctx_.return_type) + "'");
    }
  } else {
    // Bare return — only valid for void functions.
    if (ctx_.return_type != nullptr &&
        ctx_.return_type->kind() != TypeKind::Void) {
      error(ret->span(),
            "bare return in function returning '" +
                print_type(ctx_.return_type) + "'");
    }
  }
}

void TypeChecker::check_expr_stmt(const ExpressionStatementNode* es) {
  check_expr(es->expr());
}

// ---------------------------------------------------------------------------
// Expression checking
// ---------------------------------------------------------------------------

auto TypeChecker::check_expr(const Expr* expr) -> const Type* {
  return check_expr(expr, nullptr);
}

auto TypeChecker::check_expr(const Expr* expr, const Type* expected)
    -> const Type* {
  if (expr == nullptr) {
    return nullptr;
  }

  const Type* result = nullptr;

  switch (expr->kind()) {
  case NodeKind::Identifier:
    result = check_identifier(static_cast<const IdentifierNode*>(expr));
    break;
  case NodeKind::IntLiteral:
    result = check_int_literal(static_cast<const IntLiteralNode*>(expr));
    break;
  case NodeKind::FloatLiteral:
    result = check_float_literal(static_cast<const FloatLiteralNode*>(expr));
    break;
  case NodeKind::StringLiteral:
    result = check_string_literal(static_cast<const StringLiteralNode*>(expr));
    break;
  case NodeKind::BoolLiteral:
    result = check_bool_literal(static_cast<const BoolLiteralNode*>(expr));
    break;
  case NodeKind::BinaryExpr:
    result = check_binary(static_cast<const BinaryExprNode*>(expr));
    break;
  case NodeKind::UnaryExpr:
    result = check_unary(static_cast<const UnaryExprNode*>(expr));
    break;
  case NodeKind::CallExpr:
    result = check_call(static_cast<const CallExprNode*>(expr));
    break;
  case NodeKind::PipeExpr:
    result = check_pipe(static_cast<const PipeExprNode*>(expr));
    break;
  case NodeKind::FieldExpr:
    result = check_field(static_cast<const FieldExprNode*>(expr));
    break;
  case NodeKind::IndexExpr:
    result = check_index(static_cast<const IndexExprNode*>(expr));
    break;
  case NodeKind::Lambda:
    result = check_lambda(static_cast<const LambdaNode*>(expr), expected);
    break;
  case NodeKind::ListLiteral:
    result = check_list_literal(static_cast<const ListLiteralNode*>(expr));
    break;
  default:
    error(expr->span(), "unsupported expression in type checker");
    break;
  }

  if (result != nullptr) {
    typed_.set_expr_type(expr, result);
  }

  return result;
}

// ---------------------------------------------------------------------------
// Identifier
// ---------------------------------------------------------------------------

auto TypeChecker::check_identifier(const IdentifierNode* id) -> const Type* {
  auto it = resolve_.uses.find(id->span().offset);
  if (it == resolve_.uses.end()) {
    error(id->span(), "unresolved identifier '" + std::string(id->name()) + "'");
    return nullptr;
  }
  return resolve_symbol_type(it->second);
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

auto TypeChecker::check_int_literal(const IntLiteralNode* /*lit*/) -> const Type* {
  return types_.i32(); // default integer literal type
}

auto TypeChecker::check_float_literal(const FloatLiteralNode* /*lit*/) -> const Type* {
  return types_.f64(); // default float literal type
}

auto TypeChecker::check_string_literal(const StringLiteralNode* /*lit*/) -> const Type* {
  return types_.named_type(nullptr, "string", {});
}

auto TypeChecker::check_bool_literal(const BoolLiteralNode* /*lit*/) -> const Type* {
  return types_.bool_type();
}

// ---------------------------------------------------------------------------
// Binary expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_binary(const BinaryExprNode* bin) -> const Type* {
  const auto* lhs = check_expr(bin->left());
  const auto* rhs = check_expr(bin->right());
  if (lhs == nullptr || rhs == nullptr) {
    return nullptr;
  }

  switch (bin->op()) {
  // Arithmetic: same numeric type required.
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
  case BinaryOp::Div:
  case BinaryOp::Mod: {
    if (!is_numeric(lhs)) {
      error(bin->left()->span(),
            "arithmetic requires numeric type, got '" + print_type(lhs) + "'");
      return nullptr;
    }
    if (!is_assignable(lhs, rhs)) {
      error(bin->span(),
            "mismatched types in arithmetic: '" + print_type(lhs) +
                "' and '" + print_type(rhs) + "'");
      return nullptr;
    }
    return lhs;
  }

  // Comparison: same numeric type, result is bool.
  case BinaryOp::Lt:
  case BinaryOp::LtEq:
  case BinaryOp::Gt:
  case BinaryOp::GtEq: {
    if (!is_numeric(lhs)) {
      error(bin->left()->span(),
            "comparison requires numeric type, got '" + print_type(lhs) + "'");
      return nullptr;
    }
    if (!is_assignable(lhs, rhs)) {
      error(bin->span(),
            "mismatched types in comparison: '" + print_type(lhs) +
                "' and '" + print_type(rhs) + "'");
      return nullptr;
    }
    return types_.bool_type();
  }

  // Equality: same type required, result is bool.
  case BinaryOp::EqEq:
  case BinaryOp::BangEq: {
    if (!is_assignable(lhs, rhs)) {
      error(bin->span(),
            "mismatched types in equality: '" + print_type(lhs) +
                "' and '" + print_type(rhs) + "'");
      return nullptr;
    }
    return types_.bool_type();
  }

  // Logical: both bool, result is bool.
  case BinaryOp::And:
  case BinaryOp::Or: {
    if (!is_assignable(lhs, types_.bool_type())) {
      error(bin->left()->span(),
            "logical operator requires 'bool', got '" + print_type(lhs) + "'");
      return nullptr;
    }
    if (!is_assignable(rhs, types_.bool_type())) {
      error(bin->right()->span(),
            "logical operator requires 'bool', got '" + print_type(rhs) + "'");
      return nullptr;
    }
    return types_.bool_type();
  }
  }

  return nullptr;
}

// ---------------------------------------------------------------------------
// Unary expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_unary(const UnaryExprNode* un) -> const Type* {
  const auto* operand = check_expr(un->operand());
  if (operand == nullptr) {
    return nullptr;
  }

  switch (un->op()) {
  case UnaryOp::Negate: {
    if (!is_numeric(operand)) {
      error(un->operand()->span(),
            "negation requires numeric type, got '" + print_type(operand) + "'");
      return nullptr;
    }
    return operand;
  }

  case UnaryOp::Not: {
    if (!is_assignable(operand, types_.bool_type())) {
      error(un->operand()->span(),
            "logical not requires 'bool', got '" + print_type(operand) + "'");
      return nullptr;
    }
    return types_.bool_type();
  }

  case UnaryOp::Deref: {
    // Dereference requires mode unsafe.
    if (ctx_.active_modes.find("unsafe") == ctx_.active_modes.end()) {
      error(un->span(), "dereference requires 'mode unsafe =>'");
      return nullptr;
    }
    if (operand->kind() != TypeKind::Pointer) {
      error(un->operand()->span(),
            "cannot dereference non-pointer type '" + print_type(operand) + "'");
      return nullptr;
    }
    return static_cast<const TypePointer*>(operand)->pointee();
  }

  case UnaryOp::AddrOf: {
    // Address-of requires an lvalue.
    if (!is_lvalue(un->operand())) {
      error(un->operand()->span(), "cannot take address of non-lvalue");
      return nullptr;
    }
    return types_.pointer_to(operand);
  }
  }

  return nullptr;
}

// ---------------------------------------------------------------------------
// Call expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_call(const CallExprNode* call) -> const Type* {
  const auto* callee_type = check_expr(call->callee());
  if (callee_type == nullptr) {
    return nullptr;
  }

  if (callee_type->kind() != TypeKind::Function) {
    error(call->callee()->span(),
          "cannot call non-function type '" + print_type(callee_type) + "'");
    return nullptr;
  }

  const auto* fn_type = static_cast<const TypeFunction*>(callee_type);
  const auto& params = fn_type->param_types();

  if (call->args().size() != params.size()) {
    error(call->span(),
          "expected " + std::to_string(params.size()) + " argument(s), got " +
              std::to_string(call->args().size()));
    return nullptr;
  }

  for (size_t i = 0; i < params.size(); ++i) {
    const auto* arg_type = check_expr(call->args()[i]);
    if (arg_type != nullptr && params[i] != nullptr &&
        !is_assignable(arg_type, params[i])) {
      error(call->args()[i]->span(),
            "argument type '" + print_type(arg_type) +
                "' is not assignable to parameter type '" +
                print_type(params[i]) + "'");
    }
  }

  return fn_type->return_type();
}

// ---------------------------------------------------------------------------
// Pipe expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_pipe(const PipeExprNode* pipe) -> const Type* {
  const auto* lhs_type = check_expr(pipe->left());
  if (lhs_type == nullptr) {
    return nullptr;
  }

  // The RHS of a pipe is typically a callable. Check it as an expression.
  const auto* rhs_type = check_expr(pipe->right());
  if (rhs_type == nullptr) {
    return nullptr;
  }

  if (rhs_type->kind() != TypeKind::Function) {
    error(pipe->right()->span(),
          "pipe target must be callable, got '" + print_type(rhs_type) + "'");
    return nullptr;
  }

  const auto* fn_type = static_cast<const TypeFunction*>(rhs_type);
  const auto& params = fn_type->param_types();

  if (params.empty()) {
    error(pipe->right()->span(),
          "pipe target must accept at least one argument");
    return nullptr;
  }

  // LHS becomes first argument.
  if (!is_assignable(lhs_type, params[0])) {
    error(pipe->left()->span(),
          "pipe source type '" + print_type(lhs_type) +
              "' is not assignable to first parameter type '" +
              print_type(params[0]) + "'");
    return nullptr;
  }

  return fn_type->return_type();
}

// ---------------------------------------------------------------------------
// Field access
// ---------------------------------------------------------------------------

auto TypeChecker::check_field(const FieldExprNode* field) -> const Type* {
  const auto* obj_type = check_expr(field->object());
  if (obj_type == nullptr) {
    return nullptr;
  }

  if (obj_type->kind() != TypeKind::Struct) {
    error(field->object()->span(),
          "field access on non-struct type '" + print_type(obj_type) + "'");
    return nullptr;
  }

  const auto* st = static_cast<const TypeStruct*>(obj_type);
  for (const auto& f : st->fields()) {
    if (f.name == field->field()) {
      return f.type;
    }
  }

  error(field->field_span(),
        "no field '" + std::string(field->field()) + "' on type '" +
            print_type(obj_type) + "'");
  return nullptr;
}

// ---------------------------------------------------------------------------
// Index expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_index(const IndexExprNode* idx) -> const Type* {
  const auto* obj_type = check_expr(idx->object());
  if (obj_type == nullptr) {
    return nullptr;
  }

  // Type-check index expressions but don't enforce semantics yet.
  for (const auto* index : idx->indices()) {
    check_expr(index);
  }

  error(idx->span(), "indexing is not yet supported in the type checker");
  return nullptr;
}

// ---------------------------------------------------------------------------
// Lambda expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_lambda(const LambdaNode* lam, const Type* expected)
    -> const Type* {
  // Lambdas require a contextual expected function type.
  if (expected == nullptr || expected->kind() != TypeKind::Function) {
    error(lam->span(), "lambda requires expected function type context");
    return nullptr;
  }

  const auto* fn_expected = static_cast<const TypeFunction*>(expected);
  const auto& expected_params = fn_expected->param_types();

  if (lam->params().size() != expected_params.size()) {
    error(lam->span(),
          "lambda has " + std::to_string(lam->params().size()) +
              " parameter(s), expected " +
              std::to_string(expected_params.size()));
    return nullptr;
  }

  // Bind lambda param types from context and register them.
  for (size_t i = 0; i < lam->params().size(); ++i) {
    auto use_it = resolve_.uses.find(lam->params()[i].second.offset);
    if (use_it != resolve_.uses.end()) {
      symbol_types_[use_it->second] = expected_params[i];
    }
  }

  // Check body expression against expected return type.
  const auto* body_type = check_expr(lam->body());
  if (body_type != nullptr && fn_expected->return_type() != nullptr &&
      !is_assignable(body_type, fn_expected->return_type())) {
    error(lam->body()->span(),
          "lambda body type '" + print_type(body_type) +
              "' does not match expected return type '" +
              print_type(fn_expected->return_type()) + "'");
  }

  return expected;
}

// ---------------------------------------------------------------------------
// List literal
// ---------------------------------------------------------------------------

auto TypeChecker::check_list_literal(const ListLiteralNode* list) -> const Type* {
  // Type-check elements but list typing is not yet frozen.
  for (const auto* elem : list->elements()) {
    check_expr(elem);
  }

  if (!list->elements().empty()) {
    // All elements must have the same type (if we can check them).
    // For now, just validate they type-check.
  }

  error(list->span(), "list literal typing is not yet supported");
  return nullptr;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void TypeChecker::error(Span span, std::string message) {
  diagnostics_.push_back(Diagnostic::error(span, std::move(message)));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

auto TypeChecker::is_lvalue(const Expr* expr) -> bool {
  if (expr == nullptr) {
    return false;
  }
  switch (expr->kind()) {
  case NodeKind::Identifier:
    return true;
  case NodeKind::FieldExpr:
    return true;
  case NodeKind::IndexExpr:
    return true;
  case NodeKind::UnaryExpr: {
    // Dereferenced pointer is an lvalue.
    const auto* un = static_cast<const UnaryExprNode*>(expr);
    return un->op() == UnaryOp::Deref;
  }
  default:
    return false;
  }
}

// ---------------------------------------------------------------------------
// Free-function entry point
// ---------------------------------------------------------------------------

auto typecheck(const FileNode& file, const ResolveResult& resolve,
               TypeContext& types) -> TypeCheckResult {
  TypeChecker checker(types, resolve);
  return checker.check(file);
}

} // namespace dao
