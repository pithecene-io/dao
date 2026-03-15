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
  file_ = &file;

  // Pass 1: register all top-level declaration types.
  register_declarations(file);

  // Pass 1c: compute derived conformances for all classes.
  compute_derived_conformances(file);

  // Pass 1d: build pre-computed method lookup table.
  build_method_table(file);

  // Pass 2: check all declaration bodies.
  for (const auto* decl : file.declarations) {
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
    const auto& named = node->as<NamedType>();
    const auto& path = named.name;
    if (path.segments.size() != 1) {
      error(node->span, "qualified type names are not yet supported");
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

    // Generator<T> — compiler-provided coroutine type.
    if (name == "Generator") {
      if (named.type_args.size() != 1) {
        error(node->span, "Generator requires exactly one type argument");
        return nullptr;
      }
      const auto* yield_type = resolve_type_node(named.type_args[0]);
      if (yield_type == nullptr) {
        return nullptr;
      }
      return types_.generator_type(yield_type);
    }

    // Look up user-defined types via resolver symbols.
    auto it = resolve_.uses.find(node->span.offset);
    if (it != resolve_.uses.end()) {
      const auto* sym = it->second;
      // Generic type parameters resolve to TypeGenericParam.
      if (sym->kind == SymbolKind::GenericParam) {
        // Find the parameter index from the enclosing declaration.
        uint32_t index = find_generic_param_index(sym);
        return types_.generic_param(sym->decl, sym->name, index);
      }
      // Concept name in type position: substitute the conforming type
      // when inside a context that has set concept_self_map_ (§3.2).
      if (sym->kind == SymbolKind::Concept) {
        auto csm = concept_self_map_.find(sym->name);
        if (csm != concept_self_map_.end()) {
          return csm->second;
        }
      }
      return resolve_symbol_type(sym);
    }

    error(node->span, "unknown type '" + std::string(name) + "'");
    return nullptr;
  }

  case NodeKind::PointerType: {
    const auto& ptr = node->as<PointerType>();
    const auto* pointee = resolve_type_node(ptr.pointee);
    if (pointee == nullptr) {
      return nullptr;
    }
    return types_.pointer_to(pointee);
  }

  default:
    error(node->span, "unsupported type syntax");
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
    const auto& fn = sym->decl_as_decl()->as<FunctionDecl>();
    std::vector<const Type*> param_types;
    bool valid = true;
    for (const auto& param : fn.params) {
      const auto* pt = resolve_type_node(param.type);
      if (pt == nullptr) {
        valid = false;
      }
      param_types.push_back(pt);
    }
    const auto* ret = fn.return_type != nullptr
                          ? resolve_type_node(fn.return_type)
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
    // The decl for a param points to the Decl (FunctionDecl payload).
    // We need to find the matching param by name.
    const auto& fn = sym->decl_as_decl()->as<FunctionDecl>();
    for (const auto& p : fn.params) {
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

  case SymbolKind::GenericParam:
    // Generic type parameters resolve to TypeGenericParam. The index
    // is derived from the parameter's position in the declaration.
    // For now, return nullptr — generic params aren't yet type-checkable
    // as values (they are types, not values).
    break;

  case SymbolKind::Concept:
    // Concept symbols are type-level; not values.
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
  // Pass 1a: register type aliases first so that functions and structs
  // can reference them regardless of source order.
  for (const auto* decl : file.declarations) {
    if (decl->kind() != NodeKind::AliasDecl) {
      continue;
    }
    const auto& alias = decl->as<AliasDecl>();
    auto decl_it = decl_symbols_.find(alias.name_span.offset);
    if (decl_it == decl_symbols_.end()) {
      continue;
    }
    const auto* sym = decl_it->second;

    // Resolve the aliased type and cache it so later lookups of the
    // alias name transparently return the underlying type.
    const auto* aliased_type = resolve_type_node(alias.type);
    if (aliased_type != nullptr) {
      symbol_types_[sym] = aliased_type;
      typed_.set_decl_type(decl, aliased_type);
    }
  }

  // Pass 1b: register functions and structs, which may reference aliases.
  for (const auto* decl : file.declarations) {
    switch (decl->kind()) {
    case NodeKind::FunctionDecl: {
      const auto& fn = decl->as<FunctionDecl>();
      // Find the symbol for this function via declaration map.
      auto decl_it = decl_symbols_.find(fn.name_span.offset);
      if (decl_it == decl_symbols_.end()) {
        break;
      }
      const auto* sym = decl_it->second;

      // Build function type.
      std::vector<const Type*> param_types;
      bool valid = true;
      for (const auto& param : fn.params) {
        const auto* pt = resolve_type_node(param.type);
        if (pt == nullptr) {
          valid = false;
        }
        param_types.push_back(pt);
      }
      const auto* ret = fn.return_type != nullptr
                            ? resolve_type_node(fn.return_type)
                            : types_.void_type();
      if (valid && ret != nullptr) {
        const auto* fn_type = types_.function_type(std::move(param_types), ret);
        symbol_types_[sym] = fn_type;
        typed_.set_decl_type(decl, fn_type);
      }
      break;
    }

    case NodeKind::ClassDecl: {
      const auto& st = decl->as<ClassDecl>();
      // Find symbol via declaration map.
      auto decl_it = decl_symbols_.find(st.name_span.offset);
      if (decl_it == decl_symbols_.end()) {
        break;
      }
      const auto* sym = decl_it->second;

      // Build struct type from field specifiers.
      std::vector<StructField> fields;
      for (const auto* field : st.fields) {
        const auto* field_type = resolve_type_node(field->type);
        if (field_type != nullptr) {
          fields.push_back({field->name, field_type});
        }
      }
      const auto* struct_type =
          types_.make_struct(sym, st.name, std::move(fields));
      symbol_types_[sym] = struct_type;
      typed_.set_decl_type(decl, struct_type);
      break;
    }

    case NodeKind::ExtendDecl: {
      // Register extend methods as typed functions so HIR lowering
      // can find their type info via decl_type().
      const auto& ext = decl->as<ExtendDecl>();
      const auto* target_type = resolve_type_node(ext.target_type);
      for (const auto* method : ext.methods) {
        const auto& method_fn = method->as<FunctionDecl>();
        std::vector<const Type*> param_types;
        bool valid = true;
        for (const auto& param : method_fn.params) {
          if (param.name == "self" && param.type == nullptr) {
            // Bare self — use the extend target type.
            if (target_type != nullptr) {
              param_types.push_back(target_type);
            } else {
              valid = false;
              param_types.push_back(nullptr);
            }
          } else {
            const auto* param_type = resolve_type_node(param.type);
            if (param_type == nullptr) {
              valid = false;
            }
            param_types.push_back(param_type);
          }
        }
        const auto* ret = method_fn.return_type != nullptr
                              ? resolve_type_node(method_fn.return_type)
                              : types_.void_type();
        if (valid && ret != nullptr) {
          const auto* fn_type =
              types_.function_type(std::move(param_types), ret);
          auto decl_it = decl_symbols_.find(method_fn.name_span.offset);
          if (decl_it != decl_symbols_.end()) {
            symbol_types_[decl_it->second] = fn_type;
          }
          typed_.set_decl_type(method, fn_type);
        }
      }
      break;
    }

    default:
      break;
    }
  }

  // Pass 1c-prep: collect derived concept declarations.
  for (const auto* decl : file.declarations) {
    if (decl->kind() == NodeKind::ConceptDecl) {
      const auto& cpt = decl->as<ConceptDecl>();
      if (cpt.is_derived) {
        derived_concepts_.push_back(decl);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Derived conformance computation
// ---------------------------------------------------------------------------

auto TypeChecker::type_conforms_to(const Type* type,
                                   const Decl* concept_decl) -> bool {
  // Check explicit conformance: inline `as` blocks on structs.
  if (type->kind() == TypeKind::Struct) {
    const auto* struct_type = static_cast<const TypeStruct*>(type);
    const auto* decl_sym =
        static_cast<const Symbol*>(struct_type->decl_id());
    if (decl_sym != nullptr && decl_sym->decl != nullptr) {
      const auto* decl_node = static_cast<const Decl*>(decl_sym->decl);
      if (decl_node->is<ClassDecl>()) {
        const auto& cls = decl_node->as<ClassDecl>();
        const auto& concept_name = concept_decl->as<ConceptDecl>().name;

        // deny supersedes everything — if present, the type does not
        // conform regardless of explicit `as` blocks. (Having both
        // is a compile error diagnosed in check_class.)
        for (const auto& deny : cls.denials) {
          if (deny.concept_name == concept_name) {
            return false;
          }
        }

        // Check explicit conformance.
        for (const auto& conf : cls.conformances) {
          if (conf.concept_name == concept_name) {
            return true;
          }
        }
      }
    }
  }

  // Check extend declarations.
  if (file_ != nullptr) {
    const auto& cpt_name = concept_decl->as<ConceptDecl>().name;
    for (const auto* decl : file_->declarations) {
      if (decl->kind() != NodeKind::ExtendDecl) {
        continue;
      }
      const auto& ext = decl->as<ExtendDecl>();
      const auto* target = resolve_type_node(ext.target_type);
      if (target == type && ext.concept_name == cpt_name) {
        return true;
      }
    }
  }

  // Check derived conformance (already computed).
  auto it = derived_conformances_.find(type);
  if (it != derived_conformances_.end()) {
    for (const auto* derived : it->second) {
      if (derived == concept_decl) {
        return true;
      }
    }
  }

  return false;
}

void TypeChecker::compute_derived_conformances(const FileNode& file) {
  if (derived_concepts_.empty()) {
    return;
  }

  // Collect class declarations once for iteration.
  struct ClassEntry {
    const Decl* decl;
    const ClassDecl* cls;
    const Type* struct_type;
  };
  std::vector<ClassEntry> classes;
  for (const auto* decl : file.declarations) {
    if (decl->kind() != NodeKind::ClassDecl) {
      continue;
    }
    const auto& cls = decl->as<ClassDecl>();
    auto decl_it = decl_symbols_.find(cls.name_span.offset);
    if (decl_it == decl_symbols_.end()) {
      continue;
    }
    const auto* struct_type = resolve_symbol_type(decl_it->second);
    if (struct_type == nullptr || struct_type->kind() != TypeKind::Struct) {
      continue;
    }
    classes.push_back({decl, &cls, struct_type});
  }

  // Fixpoint loop: repeat until no new conformances are discovered.
  // This ensures transitive derivation is independent of declaration order.
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& entry : classes) {
      for (const auto* concept_decl : derived_concepts_) {
        const auto& cpt = concept_decl->as<ConceptDecl>();

        // Skip if explicit conformance or deny exists.
        bool has_explicit = false;
        for (const auto& conf : entry.cls->conformances) {
          if (conf.concept_name == cpt.name) {
            has_explicit = true;
            break;
          }
        }
        if (has_explicit) {
          continue;
        }

        bool denied = false;
        for (const auto& deny : entry.cls->denials) {
          if (deny.concept_name == cpt.name) {
            denied = true;
            break;
          }
        }
        if (denied) {
          continue;
        }

        // Skip if already derived.
        auto existing_it = derived_conformances_.find(entry.struct_type);
        if (existing_it != derived_conformances_.end()) {
          bool already_derived = false;
          for (const auto* existing : existing_it->second) {
            if (existing == concept_decl) {
              already_derived = true;
              break;
            }
          }
          if (already_derived) {
            continue;
          }
        }

        // Structural check: all fields must conform to this concept.
        const auto* stype = static_cast<const TypeStruct*>(entry.struct_type);
        bool all_fields_conform = true;
        for (const auto& field : stype->fields()) {
          if (!type_conforms_to(field.type, concept_decl)) {
            all_fields_conform = false;
            break;
          }
        }

        if (all_fields_conform) {
          derived_conformances_[entry.struct_type].push_back(concept_decl);
          changed = true;
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Declaration checking (pass 2)
// ---------------------------------------------------------------------------

void TypeChecker::check_declaration(const Decl* decl) {
  switch (decl->kind()) {
  case NodeKind::FunctionDecl:
    check_function(decl);
    break;
  case NodeKind::ClassDecl:
    check_class(decl);
    break;
  case NodeKind::ConceptDecl:
    // Concept default method bodies are abstract over `self`'s type.
    // Full checking requires concept-level type reasoning (deferred).
    break;
  case NodeKind::ExtendDecl: {
    // Check bodies of conformance methods with self_type set to the
    // target type of the extend declaration.
    const auto& ext = decl->as<ExtendDecl>();
    auto saved_ctx = ctx_;
    ctx_.self_type = resolve_type_node(ext.target_type);

    // Diagnose extend targeting a type that denies the concept.
    if (ctx_.self_type != nullptr &&
        ctx_.self_type->kind() == TypeKind::Struct) {
      const auto* st = static_cast<const TypeStruct*>(ctx_.self_type);
      const auto* dsym = static_cast<const Symbol*>(st->decl_id());
      if (dsym != nullptr && dsym->decl != nullptr) {
        const auto* dnode = static_cast<const Decl*>(dsym->decl);
        if (dnode->is<ClassDecl>()) {
          for (const auto& deny : dnode->as<ClassDecl>().denials) {
            if (deny.concept_name == ext.concept_name) {
              error(ext.concept_span,
                    "cannot extend '" + std::string(st->name()) +
                        "' as '" + std::string(ext.concept_name) +
                        "' because the type denies it");
            }
          }
        }
      }
    }
    for (const auto* method : ext.methods) {
      validate_receiver(method, ext.concept_span);
      const auto& fn = method->as<FunctionDecl>();
      if (!fn.body.empty() || fn.expr_body != nullptr) {
        check_function(method);
      }
    }
    ctx_ = saved_ctx;
    break;
  }
  default:
    // AliasDecl — no body checking needed yet.
    break;
  }
}

void TypeChecker::check_function(const Decl* decl) {
  const auto& fn = decl->as<FunctionDecl>();

  // Determine return type.
  const auto* ret_type = fn.return_type != nullptr
                             ? resolve_type_node(fn.return_type)
                             : types_.void_type();

  // Set up param types in symbol cache.
  for (const auto& param : fn.params) {
    auto decl_it = decl_symbols_.find(param.name_span.offset);
    if (decl_it == decl_symbols_.end()) {
      continue;
    }
    if (param.type != nullptr) {
      const auto* pt = resolve_type_node(param.type);
      if (pt != nullptr) {
        symbol_types_[decl_it->second] = pt;
      }
    } else if (param.name == "self" && ctx_.self_type != nullptr) {
      // Bare `self` receiver — type comes from the enclosing class/extend.
      symbol_types_[decl_it->second] = ctx_.self_type;
    }
  }

  // Save and set context.
  auto saved_ctx = ctx_;
  ctx_.return_type = ret_type;

  if (fn.is_expr_bodied()) {
    // Expression-bodied: -> expr
    const auto* expr_type = check_expr(fn.expr_body);
    if (expr_type != nullptr && ret_type != nullptr &&
        !is_assignable(expr_type, ret_type)) {
      error(fn.expr_body->span,
            "expression body type '" + print_type(expr_type) +
                "' does not match return type '" + print_type(ret_type) + "'");
    }
  } else {
    // Block-bodied: check statements.
    check_body(fn.body);
  }

  ctx_ = saved_ctx;
}

void TypeChecker::check_class(const Decl* decl) {
  const auto& cls = decl->as<ClassDecl>();

  // Look up the struct type registered in pass 1.
  auto saved_ctx = ctx_;
  auto decl_it = decl_symbols_.find(cls.name_span.offset);
  if (decl_it != decl_symbols_.end()) {
    ctx_.self_type = resolve_symbol_type(decl_it->second);
  }

  // Diagnose conflicting as + deny for the same concept.
  for (const auto& deny : cls.denials) {
    for (const auto& conf : cls.conformances) {
      if (conf.concept_name == deny.concept_name) {
        error(deny.concept_span,
              "'" + std::string(cls.name) + "' both conforms to and denies '" +
                  std::string(deny.concept_name) + "'");
      }
    }
  }

  // Validate and check conformance-block methods.
  for (const auto& conf : cls.conformances) {
    for (const auto* method : conf.methods) {
      validate_receiver(method, conf.concept_span);
      const auto& fn = method->as<FunctionDecl>();
      if (!fn.body.empty() || fn.expr_body != nullptr) {
        check_function(method);
      }
    }
  }

  ctx_ = saved_ctx;
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
    check_let(stmt);
    break;
  case NodeKind::Assignment:
    check_assignment(stmt);
    break;
  case NodeKind::IfStatement:
    check_if(stmt);
    break;
  case NodeKind::WhileStatement:
    check_while(stmt);
    break;
  case NodeKind::ForStatement:
    check_for(stmt);
    break;
  case NodeKind::YieldStatement:
    check_yield(stmt);
    break;
  case NodeKind::ModeBlock:
    check_mode_block(stmt);
    break;
  case NodeKind::ResourceBlock:
    check_resource_block(stmt);
    break;
  case NodeKind::ReturnStatement:
    check_return(stmt);
    break;
  case NodeKind::ExpressionStatement:
    check_expr_stmt(stmt);
    break;
  default:
    break;
  }
}

void TypeChecker::check_let(const Stmt* stmt) {
  const auto& let = stmt->as<LetStatement>();

  const Type* declared_type = nullptr;
  if (let.type != nullptr) {
    declared_type = resolve_type_node(let.type);
  }

  const Type* init_type = nullptr;
  if (let.initializer != nullptr) {
    init_type = check_expr(let.initializer);
  }

  if (declared_type != nullptr && init_type != nullptr) {
    // let x: T = expr — check assignability.
    if (!is_assignable(init_type, declared_type)) {
      error(let.initializer->span,
            "initializer type '" + print_type(init_type) +
                "' is not assignable to '" + print_type(declared_type) + "'");
    }
    typed_.set_local_type(stmt, declared_type);
  } else if (declared_type != nullptr) {
    // let x: T — type without initializer.
    typed_.set_local_type(stmt, declared_type);
  } else if (init_type != nullptr) {
    // let x = expr — infer from initializer.
    typed_.set_local_type(stmt, init_type);
  } else {
    // let x — no type, no initializer.
    error(stmt->span,
          "declaration without type annotation requires an initializer");
  }

  // Cache in symbol table for later identifier lookups.
  const auto* local_type = typed_.local_type(stmt);
  if (local_type != nullptr) {
    auto decl_it = decl_symbols_.find(let.name_span.offset);
    if (decl_it != decl_symbols_.end()) {
      symbol_types_[decl_it->second] = local_type;
    }
  }
}

void TypeChecker::check_assignment(const Stmt* stmt) {
  const auto& assign = stmt->as<Assignment>();

  if (!is_lvalue(assign.target)) {
    error(assign.target->span, "invalid assignment target");
  }

  const auto* target_type = check_expr(assign.target);
  const auto* value_type = check_expr(assign.value);

  if (target_type != nullptr && value_type != nullptr &&
      !is_assignable(value_type, target_type)) {
    error(assign.value->span,
          "cannot assign '" + print_type(value_type) + "' to '" +
              print_type(target_type) + "'");
  }
}

void TypeChecker::check_if(const Stmt* stmt) {
  const auto& ifn = stmt->as<IfStatement>();

  const auto* cond_type = check_expr(ifn.condition);
  if (cond_type != nullptr && !is_assignable(cond_type, types_.bool_type())) {
    error(ifn.condition->span,
          "condition must be 'bool', got '" + print_type(cond_type) + "'");
  }
  check_body(ifn.then_body);
  if (ifn.has_else()) {
    check_body(ifn.else_body);
  }
}

void TypeChecker::check_while(const Stmt* stmt) {
  const auto& wh = stmt->as<WhileStatement>();

  const auto* cond_type = check_expr(wh.condition);
  if (cond_type != nullptr && !is_assignable(cond_type, types_.bool_type())) {
    error(wh.condition->span,
          "condition must be 'bool', got '" + print_type(cond_type) + "'");
  }
  check_body(wh.body);
}

void TypeChecker::check_for(const Stmt* stmt) {
  const auto& fo = stmt->as<ForStatement>();

  const auto* iter_type = check_expr(fo.iterable);

  // The iterable expression must produce Generator<T>.
  const Type* elem_type = nullptr;
  if (iter_type != nullptr) {
    if (iter_type->kind() == TypeKind::Generator) {
      elem_type = static_cast<const TypeGenerator*>(iter_type)->yield_type();
    } else {
      error(fo.iterable->span,
            "for-in requires Generator<T>, got '" + print_type(iter_type) + "'");
    }
  }

  // Bind loop variable to the element type.
  auto decl_it = decl_symbols_.find(fo.var_span.offset);
  if (decl_it != decl_symbols_.end() && elem_type != nullptr) {
    symbol_types_[decl_it->second] = elem_type;
    typed_.set_local_type(stmt, elem_type);
  }

  check_body(fo.body);
}

void TypeChecker::check_yield(const Stmt* stmt) {
  const auto& yield = stmt->as<YieldStatement>();
  const auto* value_type = check_expr(yield.value);

  // yield is only valid inside a generator function (return type is Generator<T>).
  if (ctx_.return_type == nullptr ||
      ctx_.return_type->kind() != TypeKind::Generator) {
    error(stmt->span, "yield is only valid inside a generator function");
    return;
  }

  // The yielded value must match the Generator's element type.
  const auto* gen = static_cast<const TypeGenerator*>(ctx_.return_type);
  const auto* expected = gen->yield_type();
  if (value_type != nullptr && expected != nullptr && value_type != expected) {
    error(yield.value->span,
          "yield type '" + print_type(value_type) +
              "' does not match Generator element type '" +
              print_type(expected) + "'");
  }

  typed_.set_local_type(stmt, value_type);
}

void TypeChecker::check_mode_block(const Stmt* stmt) {
  const auto& mb = stmt->as<ModeBlock>();

  auto saved_ctx = ctx_;
  ctx_.active_modes.insert(mb.mode_name);
  check_body(mb.body);
  ctx_ = saved_ctx;
}

void TypeChecker::check_resource_block(const Stmt* stmt) {
  const auto& rb = stmt->as<ResourceBlock>();
  check_body(rb.body);
}

void TypeChecker::check_return(const Stmt* stmt) {
  const auto& ret = stmt->as<ReturnStatement>();

  // In generator functions, only bare return is valid (early termination).
  bool in_generator = ctx_.return_type != nullptr &&
                      ctx_.return_type->kind() == TypeKind::Generator;

  if (ret.value != nullptr) {
    const auto* val_type = check_expr(ret.value);
    if (in_generator) {
      error(ret.value->span,
            "'return value' is not valid in a generator function; "
            "use yield to produce values");
    } else if (val_type != nullptr && ctx_.return_type != nullptr &&
               !is_assignable(val_type, ctx_.return_type)) {
      error(ret.value->span,
            "return type '" + print_type(val_type) +
                "' does not match function return type '" +
                print_type(ctx_.return_type) + "'");
    }
  } else {
    // Bare return — valid for void functions and generator functions.
    if (ctx_.return_type != nullptr &&
        ctx_.return_type->kind() != TypeKind::Void && !in_generator) {
      error(stmt->span,
            "bare return in function returning '" +
                print_type(ctx_.return_type) + "'");
    }
  }
}

void TypeChecker::check_expr_stmt(const Stmt* stmt) {
  const auto& es = stmt->as<ExpressionStatement>();
  check_expr(es.expr);
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
    result = check_identifier(expr);
    break;
  case NodeKind::IntLiteral:
    result = check_int_literal(expr);
    break;
  case NodeKind::FloatLiteral:
    result = check_float_literal(expr);
    break;
  case NodeKind::StringLiteral:
    result = check_string_literal(expr);
    break;
  case NodeKind::BoolLiteral:
    result = check_bool_literal(expr);
    break;
  case NodeKind::BinaryExpr:
    result = check_binary(expr);
    break;
  case NodeKind::UnaryExpr:
    result = check_unary(expr);
    break;
  case NodeKind::CallExpr:
    result = check_call(expr);
    break;
  case NodeKind::PipeExpr:
    result = check_pipe(expr);
    break;
  case NodeKind::FieldExpr:
    result = check_field(expr);
    break;
  case NodeKind::IndexExpr:
    result = check_index(expr);
    break;
  case NodeKind::Lambda:
    result = check_lambda(expr, expected);
    break;
  case NodeKind::ListLiteral:
    result = check_list_literal(expr);
    break;
  default:
    error(expr->span, "unsupported expression in type checker");
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

auto TypeChecker::check_identifier(const Expr* expr) -> const Type* {
  const auto& id = expr->as<IdentifierExpr>();
  auto it = resolve_.uses.find(expr->span.offset);
  if (it == resolve_.uses.end()) {
    error(expr->span, "unresolved identifier '" + std::string(id.name) + "'");
    return nullptr;
  }
  const auto* result = resolve_symbol_type(it->second);
  if (result == nullptr && it->second->kind == SymbolKind::Param) {
    error(expr->span,
          "'" + std::string(id.name) + "' has no known type in this context");
  }
  return result;
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

auto TypeChecker::check_int_literal(const Expr* /*expr*/) -> const Type* {
  return types_.i32(); // default integer literal type
}

auto TypeChecker::check_float_literal(const Expr* /*expr*/) -> const Type* {
  return types_.f64(); // default float literal type
}

auto TypeChecker::check_string_literal(const Expr* /*expr*/) -> const Type* {
  return types_.named_type(nullptr, "string", {});
}

auto TypeChecker::check_bool_literal(const Expr* /*expr*/) -> const Type* {
  return types_.bool_type();
}

// ---------------------------------------------------------------------------
// Binary expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_binary(const Expr* expr) -> const Type* {
  const auto& bin = expr->as<BinaryExpr>();
  const auto* lhs = check_expr(bin.left);
  const auto* rhs = check_expr(bin.right);
  if (lhs == nullptr || rhs == nullptr) {
    return nullptr;
  }

  switch (bin.op) {
  // Arithmetic: same numeric type required.
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
  case BinaryOp::Div:
  case BinaryOp::Mod: {
    if (!is_numeric(lhs)) {
      error(bin.left->span,
            "arithmetic requires numeric type, got '" + print_type(lhs) + "'");
      return nullptr;
    }
    if (!is_assignable(lhs, rhs)) {
      error(expr->span,
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
      error(bin.left->span,
            "comparison requires numeric type, got '" + print_type(lhs) + "'");
      return nullptr;
    }
    if (!is_assignable(lhs, rhs)) {
      error(expr->span,
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
      error(expr->span,
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
      error(bin.left->span,
            "logical operator requires 'bool', got '" + print_type(lhs) + "'");
      return nullptr;
    }
    if (!is_assignable(rhs, types_.bool_type())) {
      error(bin.right->span,
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

auto TypeChecker::check_unary(const Expr* expr) -> const Type* {
  const auto& un = expr->as<UnaryExpr>();
  const auto* operand = check_expr(un.operand);
  if (operand == nullptr) {
    return nullptr;
  }

  switch (un.op) {
  case UnaryOp::Negate: {
    if (!is_numeric(operand)) {
      error(un.operand->span,
            "negation requires numeric type, got '" + print_type(operand) + "'");
      return nullptr;
    }
    return operand;
  }

  case UnaryOp::Not: {
    if (!is_assignable(operand, types_.bool_type())) {
      error(un.operand->span,
            "logical not requires 'bool', got '" + print_type(operand) + "'");
      return nullptr;
    }
    return types_.bool_type();
  }

  case UnaryOp::Deref: {
    // Dereference requires mode unsafe.
    if (ctx_.active_modes.find("unsafe") == ctx_.active_modes.end()) {
      error(expr->span, "dereference requires 'mode unsafe =>'");
      return nullptr;
    }
    if (operand->kind() != TypeKind::Pointer) {
      error(un.operand->span,
            "cannot dereference non-pointer type '" + print_type(operand) + "'");
      return nullptr;
    }
    return static_cast<const TypePointer*>(operand)->pointee();
  }

  case UnaryOp::AddrOf: {
    // Address-of requires an lvalue.
    if (!is_lvalue(un.operand)) {
      error(un.operand->span, "cannot take address of non-lvalue");
      return nullptr;
    }
    return types_.pointer_to(operand);
  }
  }

  return nullptr;
}

// ---------------------------------------------------------------------------
// Generic type inference and substitution helpers
// ---------------------------------------------------------------------------

void TypeChecker::infer_type_bindings(
    const Type* pattern, const Type* concrete,
    std::unordered_map<uint32_t, const Type*>& bindings, Span error_span) {
  if (pattern == nullptr || concrete == nullptr) {
    return;
  }

  if (pattern->kind() == TypeKind::GenericParam) {
    const auto* gp = static_cast<const TypeGenericParam*>(pattern);
    auto it = bindings.find(gp->index());
    if (it != bindings.end()) {
      // Already bound — check consistency.
      if (it->second != concrete) {
        error(error_span,
              "conflicting types for generic parameter '" +
                  std::string(gp->name()) + "': '" +
                  print_type(it->second) + "' vs '" +
                  print_type(concrete) + "'");
      }
    } else {
      bindings[gp->index()] = concrete;
    }
    return;
  }

  // Structural recursion for composite types.
  if (pattern->kind() == TypeKind::Pointer &&
      concrete->kind() == TypeKind::Pointer) {
    infer_type_bindings(
        static_cast<const TypePointer*>(pattern)->pointee(),
        static_cast<const TypePointer*>(concrete)->pointee(),
        bindings, error_span);
  } else if (pattern->kind() == TypeKind::Generator &&
             concrete->kind() == TypeKind::Generator) {
    infer_type_bindings(
        static_cast<const TypeGenerator*>(pattern)->yield_type(),
        static_cast<const TypeGenerator*>(concrete)->yield_type(),
        bindings, error_span);
  } else if (pattern->kind() == TypeKind::Function &&
             concrete->kind() == TypeKind::Function) {
    const auto* fp = static_cast<const TypeFunction*>(pattern);
    const auto* fc = static_cast<const TypeFunction*>(concrete);
    if (fp->param_types().size() == fc->param_types().size()) {
      for (size_t j = 0; j < fp->param_types().size(); ++j) {
        infer_type_bindings(fp->param_types()[j], fc->param_types()[j],
                            bindings, error_span);
      }
      infer_type_bindings(fp->return_type(), fc->return_type(),
                          bindings, error_span);
    }
  }
}

auto TypeChecker::substitute_generics(
    const Type* type,
    const std::unordered_map<uint32_t, const Type*>& bindings)
    -> const Type* {
  if (type == nullptr || bindings.empty()) {
    return type;
  }

  switch (type->kind()) {
  case TypeKind::GenericParam: {
    const auto* gp = static_cast<const TypeGenericParam*>(type);
    auto it = bindings.find(gp->index());
    return it != bindings.end() ? it->second : type;
  }
  case TypeKind::Pointer: {
    const auto* ptr = static_cast<const TypePointer*>(type);
    const auto* sub = substitute_generics(ptr->pointee(), bindings);
    return sub == ptr->pointee() ? type : types_.pointer_to(sub);
  }
  case TypeKind::Generator: {
    const auto* gen = static_cast<const TypeGenerator*>(type);
    const auto* sub = substitute_generics(gen->yield_type(), bindings);
    return sub == gen->yield_type() ? type : types_.generator_type(sub);
  }
  case TypeKind::Function: {
    const auto* fn = static_cast<const TypeFunction*>(type);
    bool changed = false;
    std::vector<const Type*> params;
    params.reserve(fn->param_types().size());
    for (const auto* param : fn->param_types()) {
      const auto* sub = substitute_generics(param, bindings);
      if (sub != param) changed = true;
      params.push_back(sub);
    }
    const auto* ret = substitute_generics(fn->return_type(), bindings);
    if (ret != fn->return_type()) changed = true;
    return changed ? types_.function_type(std::move(params), ret) : type;
  }
  default:
    return type;
  }
}

// ---------------------------------------------------------------------------
// Shared generic constraint verification
// ---------------------------------------------------------------------------

void TypeChecker::verify_concept_constraints(
    const Expr* callee_expr, Span error_span,
    const std::unordered_map<uint32_t, const Type*>& bindings) {
  if (bindings.empty() || !callee_expr->is<IdentifierExpr>()) {
    return;
  }
  auto sym_it = resolve_.uses.find(callee_expr->span.offset);
  if (sym_it == resolve_.uses.end() ||
      sym_it->second->kind != SymbolKind::Function ||
      sym_it->second->decl == nullptr) {
    return;
  }
  const auto* fn_decl = static_cast<const Decl*>(sym_it->second->decl);
  if (!fn_decl->is<FunctionDecl>()) {
    return;
  }
  const auto& func = fn_decl->as<FunctionDecl>();
  for (const auto& gp_decl : func.type_params) {
    auto idx = static_cast<uint32_t>(&gp_decl - func.type_params.data());
    auto binding_it = bindings.find(idx);
    if (binding_it == bindings.end()) {
      continue;
    }
    for (const auto* constraint : gp_decl.constraints) {
      auto csym_it = resolve_.uses.find(constraint->span.offset);
      if (csym_it == resolve_.uses.end() ||
          csym_it->second->kind != SymbolKind::Concept ||
          csym_it->second->decl == nullptr) {
        continue;
      }
      const auto* concept_decl =
          static_cast<const Decl*>(csym_it->second->decl);
      if (!type_conforms_to(binding_it->second, concept_decl)) {
        error(error_span,
              "type '" + print_type(binding_it->second) +
                  "' does not satisfy concept '" +
                  std::string(csym_it->second->name) +
                  "' required by generic parameter '" +
                  std::string(gp_decl.name) + "'");
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Call expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_call(const Expr* expr) -> const Type* {
  const auto& call = expr->as<CallExpr>();
  const auto* callee_type = check_expr(call.callee);
  if (callee_type == nullptr) {
    return nullptr;
  }

  // Constructor call: callee must be an identifier that resolves to a
  // Type symbol (e.g. `Point`), not merely any expression whose type
  // happens to be a struct (e.g. `p` where `p: Point`).
  if (callee_type->kind() == TypeKind::Struct &&
      call.callee->is<IdentifierExpr>()) {
    auto sym_it = resolve_.uses.find(call.callee->span.offset);
    if (sym_it != resolve_.uses.end() &&
        sym_it->second->kind == SymbolKind::Type) {
      return check_construct(expr,
                             static_cast<const TypeStruct*>(callee_type));
    }
  }

  if (callee_type->kind() != TypeKind::Function) {
    error(call.callee->span,
          "cannot call non-function type '" + print_type(callee_type) + "'");
    return nullptr;
  }

  const auto* fn_type = static_cast<const TypeFunction*>(callee_type);
  const auto& params = fn_type->param_types();

  if (call.args.size() != params.size()) {
    error(expr->span,
          "expected " + std::to_string(params.size()) + " argument(s), got " +
              std::to_string(call.args.size()));
    return nullptr;
  }

  // Check arguments and infer generic type bindings from call site.
  // type_bindings maps generic param index → concrete type.
  std::unordered_map<uint32_t, const Type*> type_bindings;

  for (size_t i = 0; i < params.size(); ++i) {
    const auto* arg_type = check_expr(call.args[i]);
    if (arg_type == nullptr || params[i] == nullptr) {
      continue;
    }
    if (!is_assignable(arg_type, params[i])) {
      error(call.args[i]->span,
            "argument type '" + print_type(arg_type) +
                "' is not assignable to parameter type '" +
                print_type(params[i]) + "'");
    }
    // Infer generic bindings by structural matching.
    infer_type_bindings(params[i], arg_type, type_bindings,
                        call.args[i]->span);
  }

  verify_concept_constraints(call.callee, expr->span, type_bindings);

  // Substitute generic params in the return type.
  return substitute_generics(fn_type->return_type(), type_bindings);
}

// ---------------------------------------------------------------------------
// Constructor expressions (Point(1, 2))
// ---------------------------------------------------------------------------

auto TypeChecker::check_construct(const Expr* expr,
                                  const TypeStruct* struct_type)
    -> const Type* {
  const auto& call = expr->as<CallExpr>();
  const auto& fields = struct_type->fields();

  if (call.args.size() != fields.size()) {
    error(expr->span,
          "'" + std::string(struct_type->name()) + "' expects " +
              std::to_string(fields.size()) + " field(s), got " +
              std::to_string(call.args.size()));
    return nullptr;
  }

  for (size_t i = 0; i < fields.size(); ++i) {
    const auto* arg_type = check_expr(call.args[i]);
    if (arg_type != nullptr && fields[i].type != nullptr &&
        !is_assignable(arg_type, fields[i].type)) {
      error(call.args[i]->span,
            "field '" + std::string(fields[i].name) + "' expects type '" +
                print_type(fields[i].type) + "', got '" +
                print_type(arg_type) + "'");
    }
  }

  return struct_type;
}

// ---------------------------------------------------------------------------
// Pipe expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_pipe(const Expr* expr) -> const Type* {
  const auto& pipe = expr->as<PipeExpr>();
  const auto* lhs_type = check_expr(pipe.left);
  if (lhs_type == nullptr) {
    return nullptr;
  }

  // The RHS of a pipe is typically a callable. Check it as an expression.
  const auto* rhs_type = check_expr(pipe.right);
  if (rhs_type == nullptr) {
    return nullptr;
  }

  if (rhs_type->kind() != TypeKind::Function) {
    error(pipe.right->span,
          "pipe target must be callable, got '" + print_type(rhs_type) + "'");
    return nullptr;
  }

  const auto* fn_type = static_cast<const TypeFunction*>(rhs_type);
  const auto& params = fn_type->param_types();

  if (params.empty()) {
    error(pipe.right->span,
          "pipe target must accept at least one argument");
    return nullptr;
  }

  // LHS becomes first argument — check assignability and infer generics.
  if (!is_assignable(lhs_type, params[0])) {
    error(pipe.left->span,
          "pipe source type '" + print_type(lhs_type) +
              "' is not assignable to first parameter type '" +
              print_type(params[0]) + "'");
    return nullptr;
  }

  // Infer generic type bindings from the pipe's first argument.
  std::unordered_map<uint32_t, const Type*> type_bindings;
  infer_type_bindings(params[0], lhs_type, type_bindings, pipe.left->span);

  verify_concept_constraints(pipe.right, expr->span, type_bindings);

  // Substitute generic params in the return type.
  return substitute_generics(fn_type->return_type(), type_bindings);
}

// ---------------------------------------------------------------------------
// Field access
// ---------------------------------------------------------------------------

auto TypeChecker::check_field(const Expr* expr) -> const Type* {
  const auto& field = expr->as<FieldExpr>();
  const auto* obj_type = check_expr(field.object);
  if (obj_type == nullptr) {
    return nullptr;
  }

  // Try struct field lookup first.
  if (obj_type->kind() == TypeKind::Struct) {
    const auto* st = static_cast<const TypeStruct*>(obj_type);
    for (const auto& f : st->fields()) {
      if (f.name == field.field) {
        return f.type;
      }
    }
  }

  // Try method lookup on any type (struct conformances + extend decls).
  const Decl* method_decl = nullptr;
  const auto* method_type = lookup_method(obj_type, field.field, &method_decl);
  if (method_type != nullptr) {
    if (method_decl != nullptr) {
      typed_.set_method_resolution(expr, method_decl);
    }
    return method_type;
  }

  if (obj_type->kind() == TypeKind::Struct) {
    error(field.field_span,
          "no field or method '" + std::string(field.field) + "' on type '" +
              print_type(obj_type) + "'");
  } else {
    error(field.field_span,
          "no method '" + std::string(field.field) + "' on type '" +
              print_type(obj_type) + "'");
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// build_method_fn_type — build a function type for a method with self removed.
// ---------------------------------------------------------------------------

auto TypeChecker::build_method_fn_type(const FunctionDecl& method)
    -> const Type* {
  std::vector<const Type*> param_types;
  bool valid = true;
  for (size_t i = 0; i < method.params.size(); ++i) {
    if (i == 0 && method.params[i].name == "self") {
      continue; // skip receiver
    }
    const auto* pt = resolve_type_node(method.params[i].type);
    if (pt == nullptr) {
      valid = false;
    }
    param_types.push_back(pt);
  }
  const auto* ret = method.return_type != nullptr
                        ? resolve_type_node(method.return_type)
                        : types_.void_type();
  if (!valid || ret == nullptr) {
    return nullptr;
  }
  return types_.function_type(std::move(param_types), ret);
}

// ---------------------------------------------------------------------------
// build_method_table — pre-populate method_table_ for O(1) lookup.
// Called once after compute_derived_conformances().
// ---------------------------------------------------------------------------

void TypeChecker::build_method_table(const FileNode& file) {
  // 1. Struct conformance block methods.
  for (const auto* decl : file.declarations) {
    if (decl->kind() != NodeKind::ClassDecl) {
      continue;
    }
    const auto& cls = decl->as<ClassDecl>();
    auto decl_it = decl_symbols_.find(cls.name_span.offset);
    if (decl_it == decl_symbols_.end()) {
      continue;
    }
    const auto* struct_type = resolve_symbol_type(decl_it->second);
    if (struct_type == nullptr || struct_type->kind() != TypeKind::Struct) {
      continue;
    }
    for (const auto& conf : cls.conformances) {
      for (const auto* method_decl : conf.methods) {
        const auto& method = method_decl->as<FunctionDecl>();
        MethodKey key{struct_type, method.name};
        if (method_table_.find(key) == method_table_.end()) {
          const auto* fn_type = build_method_fn_type(method);
          method_table_.insert({key, {fn_type, method_decl}});
        }
      }
    }
  }

  // 2. Top-level extend declarations.
  for (const auto* decl : file.declarations) {
    if (decl->kind() != NodeKind::ExtendDecl) {
      continue;
    }
    const auto& ext = decl->as<ExtendDecl>();
    const auto* target = resolve_type_node(ext.target_type);
    if (target == nullptr) {
      continue;
    }
    for (const auto* method_decl : ext.methods) {
      const auto& method = method_decl->as<FunctionDecl>();
      MethodKey key{target, method.name};
      if (method_table_.find(key) == method_table_.end()) {
        const auto* fn_type = build_method_fn_type(method);
        method_table_.insert({key, {fn_type, method_decl}});
      }
    }
  }

  // 3. Derived conformance concept methods.
  //    For each (type, derived_concept), register each concept method
  //    with the type. Resolve the concrete extend implementation for dispatch.
  for (const auto& [type, concepts] : derived_conformances_) {
    for (const auto* concept_decl : concepts) {
      const auto& cpt = concept_decl->as<ConceptDecl>();
      ConceptSelfMapGuard guard(concept_self_map_);
      concept_self_map_[cpt.name] = type;
      for (const auto* cpt_method_decl : cpt.methods) {
        const auto& method = cpt_method_decl->as<FunctionDecl>();
        MethodKey key{type, method.name};
        if (method_table_.find(key) != method_table_.end()) {
          continue;
        }
        const auto* fn_type = build_method_fn_type(method);
        // Find the concrete extend implementation for HIR lowering.
        const Decl* impl_decl = nullptr;
        for (const auto* decl : file.declarations) {
          if (decl->kind() != NodeKind::ExtendDecl) continue;
          const auto& ext = decl->as<ExtendDecl>();
          const auto* target = resolve_type_node(ext.target_type);
          if (target != type) continue;
          for (const auto* ext_method : ext.methods) {
            if (ext_method->as<FunctionDecl>().name == method.name) {
              impl_decl = ext_method;
              break;
            }
          }
          if (impl_decl != nullptr) break;
        }
        method_table_.insert(
            {key, {fn_type, impl_decl != nullptr ? impl_decl : cpt_method_decl}});
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Method lookup — table-driven with generic param fallback
// ---------------------------------------------------------------------------

auto TypeChecker::lookup_method(const Type* obj_type,
                                std::string_view name,
                                const Decl** resolved_decl) -> const Type* {
  // O(1) table lookup for concrete types (covers struct conformance blocks,
  // extend declarations, and derived conformance methods).
  auto it = method_table_.find(MethodKey{obj_type, name});
  if (it != method_table_.end()) {
    if (resolved_decl != nullptr) {
      *resolved_decl = it->second.method_decl;
    }
    return it->second.fn_type;
  }

  // Generic type parameter constraint search — this can't be pre-built
  // because it's parameterized on the receiver type variable.
  if (obj_type->kind() == TypeKind::GenericParam) {
    const auto* gp = static_cast<const TypeGenericParam*>(obj_type);
    if (gp->binder() != nullptr) {
      const auto* decl_node = static_cast<const Decl*>(gp->binder());
      const std::vector<GenericParam>* type_params = nullptr;
      if (decl_node->is<FunctionDecl>()) {
        type_params = &decl_node->as<FunctionDecl>().type_params;
      } else if (decl_node->is<ClassDecl>()) {
        type_params = &decl_node->as<ClassDecl>().type_params;
      }
      if (type_params != nullptr && gp->index() < type_params->size()) {
        const auto& gp_decl = (*type_params)[gp->index()];
        for (const auto* constraint : gp_decl.constraints) {
          auto sym_it =
              resolve_.uses.find(constraint->span.offset);
          if (sym_it == resolve_.uses.end() ||
              sym_it->second->kind != SymbolKind::Concept) {
            continue;
          }
          const auto* cpt_decl =
              static_cast<const Decl*>(sym_it->second->decl);
          if (cpt_decl == nullptr || !cpt_decl->is<ConceptDecl>()) {
            continue;
          }
          const auto& cpt = cpt_decl->as<ConceptDecl>();
          for (const auto* method_decl : cpt.methods) {
            const auto& method = method_decl->as<FunctionDecl>();
            if (method.name == name) {
              ConceptSelfMapGuard guard(concept_self_map_);
              concept_self_map_[cpt.name] = obj_type;
              return build_method_fn_type(method);
            }
          }
        }
      }
    }
  }

  return nullptr;
}

// ---------------------------------------------------------------------------
// Receiver validation — conformance/extend methods must have self first param
// ---------------------------------------------------------------------------

void TypeChecker::validate_receiver(const Decl* method, Span context_span) {
  const auto& fn_decl = method->as<FunctionDecl>();
  if (fn_decl.params.empty() || fn_decl.params[0].name != "self") {
    error(fn_decl.name_span.length > 0 ? fn_decl.name_span : context_span,
          "method '" + std::string(fn_decl.name) +
              "' must have 'self' as its first parameter");
  }
}

// ---------------------------------------------------------------------------
// Index expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_index(const Expr* expr) -> const Type* {
  const auto& idx = expr->as<IndexExpr>();
  const auto* obj_type = check_expr(idx.object);
  if (obj_type == nullptr) {
    return nullptr;
  }

  // Type-check index expressions but don't enforce semantics yet.
  for (const auto* index : idx.indices) {
    check_expr(index);
  }

  error(expr->span, "indexing is not yet supported in the type checker");
  return nullptr;
}

// ---------------------------------------------------------------------------
// Lambda expressions
// ---------------------------------------------------------------------------

auto TypeChecker::check_lambda(const Expr* expr, const Type* expected)
    -> const Type* {
  const auto& lam = expr->as<LambdaExpr>();

  // Lambdas require a contextual expected function type.
  if (expected == nullptr || expected->kind() != TypeKind::Function) {
    error(expr->span, "lambda requires expected function type context");
    return nullptr;
  }

  const auto* fn_expected = static_cast<const TypeFunction*>(expected);
  const auto& expected_params = fn_expected->param_types();

  if (lam.params.size() != expected_params.size()) {
    error(expr->span,
          "lambda has " + std::to_string(lam.params.size()) +
              " parameter(s), expected " +
              std::to_string(expected_params.size()));
    return nullptr;
  }

  // Bind lambda param types from context and register them.
  for (size_t i = 0; i < lam.params.size(); ++i) {
    auto use_it = resolve_.uses.find(lam.params[i].second.offset);
    if (use_it != resolve_.uses.end()) {
      symbol_types_[use_it->second] = expected_params[i];
    }
  }

  // Check body expression against expected return type.
  const auto* body_type = check_expr(lam.body);
  if (body_type != nullptr && fn_expected->return_type() != nullptr &&
      !is_assignable(body_type, fn_expected->return_type())) {
    error(lam.body->span,
          "lambda body type '" + print_type(body_type) +
              "' does not match expected return type '" +
              print_type(fn_expected->return_type()) + "'");
  }

  return expected;
}

// ---------------------------------------------------------------------------
// List literal
// ---------------------------------------------------------------------------

auto TypeChecker::check_list_literal(const Expr* expr) -> const Type* {
  const auto& list = expr->as<ListLiteral>();

  // Type-check elements but list typing is not yet frozen.
  for (const auto* elem : list.elements) {
    check_expr(elem);
  }

  if (!list.elements.empty()) {
    // All elements must have the same type (if we can check them).
    // For now, just validate they type-check.
  }

  error(expr->span, "list literal typing is not yet supported");
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
    const auto& un = expr->as<UnaryExpr>();
    return un.op == UnaryOp::Deref;
  }
  default:
    return false;
  }
}

auto TypeChecker::find_generic_param_index(const Symbol* sym) -> uint32_t {
  // The symbol's decl points to the enclosing Decl (FunctionDecl or ClassDecl).
  if (sym->decl == nullptr) {
    return 0;
  }
  const auto* decl = static_cast<const Decl*>(sym->decl);

  const std::vector<GenericParam>* type_params = nullptr;
  if (decl->is<FunctionDecl>()) {
    type_params = &decl->as<FunctionDecl>().type_params;
  } else if (decl->is<ClassDecl>()) {
    type_params = &decl->as<ClassDecl>().type_params;
  }

  if (type_params != nullptr) {
    for (uint32_t i = 0; i < type_params->size(); ++i) {
      if ((*type_params)[i].name == sym->name) {
        return i;
      }
    }
  }
  return 0;
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
