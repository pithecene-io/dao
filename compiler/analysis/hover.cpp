#include "analysis/hover.h"

#include "frontend/resolve/symbol.h"
#include "frontend/types/type_printer.h"

namespace dao {

auto query_hover(uint32_t offset, const ResolveResult& resolve,
                 const TypeCheckResult& typed)
    -> std::optional<HoverResult> {
  const Symbol* sym = nullptr;

  // Check use sites first (references like `add(1, 2)`).
  auto use_it = resolve.uses.find(offset);
  if (use_it != resolve.uses.end()) {
    sym = use_it->second;
  }

  // If not a use site, check declaration sites.
  if (sym == nullptr) {
    for (const auto& sym_ptr : resolve.context.symbols()) {
      if (sym_ptr->decl_span.offset == offset && sym_ptr->decl_span.length > 0) {
        sym = sym_ptr.get();
        break;
      }
    }
  }

  if (sym == nullptr) {
    return std::nullopt;
  }

  HoverResult result;
  result.name = std::string(sym->name);

  switch (sym->kind) {
  case SymbolKind::Function:
    result.symbol_kind = "function";
    break;
  case SymbolKind::Type:
    result.symbol_kind = "type";
    break;
  case SymbolKind::Param:
    result.symbol_kind = "parameter";
    break;
  case SymbolKind::Local:
    result.symbol_kind = "variable";
    break;
  case SymbolKind::Field:
    result.symbol_kind = "field";
    break;
  case SymbolKind::Module:
    result.symbol_kind = "module";
    break;
  case SymbolKind::Builtin:
    result.symbol_kind = "type";
    break;
  case SymbolKind::Predeclared:
    result.symbol_kind = "type";
    break;
  case SymbolKind::LambdaParam:
    result.symbol_kind = "parameter";
    break;
  case SymbolKind::GenericParam:
    result.symbol_kind = "type parameter";
    break;
  case SymbolKind::Concept:
    result.symbol_kind = "concept";
    break;
  }

  // Try to find type info from the typed results.
  // For expressions at use sites, the expression type is in the typed
  // results keyed by the Expr*. But we don't have the Expr* from just
  // an offset — we have the Symbol*. Try the decl type instead.
  if (sym->decl != nullptr &&
      (sym->kind == SymbolKind::Function ||
       sym->kind == SymbolKind::Type)) {
    const auto* decl = static_cast<const Decl*>(sym->decl);
    const auto* decl_type = typed.typed.decl_type(decl);
    if (decl_type != nullptr) {
      result.type = print_type(decl_type);
    }
  }

  // For parameters, extract the type from the enclosing function's type
  // by matching the parameter name.
  if (sym->kind == SymbolKind::Param && sym->decl != nullptr) {
    const auto* fn_decl = static_cast<const Decl*>(sym->decl);
    const auto* fn_type = typed.typed.decl_type(fn_decl);
    if (fn_type != nullptr && fn_type->kind() == TypeKind::Function) {
      const auto* ft = static_cast<const TypeFunction*>(fn_type);
      if (fn_decl->is<FunctionDecl>()) {
        const auto& fn = fn_decl->as<FunctionDecl>();
        for (size_t idx = 0; idx < fn.params.size(); ++idx) {
          if (fn.params[idx].name == sym->name &&
              idx < ft->param_types().size()) {
            result.type = print_type(ft->param_types()[idx]);
            break;
          }
        }
      }
    }
  }

  // For local variables, the type comes from the Stmt.
  if (sym->kind == SymbolKind::Local && sym->decl != nullptr) {
    const auto* stmt = static_cast<const Stmt*>(sym->decl);
    const auto* local_type = typed.typed.local_type(stmt);
    if (local_type != nullptr) {
      result.type = print_type(local_type);
    }
  }

  // For builtins and predeclared, use the name as the type.
  if ((sym->kind == SymbolKind::Builtin ||
       sym->kind == SymbolKind::Predeclared) &&
      result.type.empty()) {
    result.type = result.name;
  }

  return result;
}

} // namespace dao
