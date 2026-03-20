#ifndef DAO_FRONTEND_TYPECHECK_TYPED_RESULTS_H
#define DAO_FRONTEND_TYPECHECK_TYPED_RESULTS_H

#include "frontend/ast/ast.h"
#include "frontend/resolve/symbol.h"
#include "frontend/types/type.h"

#include <unordered_map>

namespace dao {

// ---------------------------------------------------------------------------
// TypedResults — side tables mapping AST nodes to semantic types.
//
// The type checker populates this structure. HIR lowering and tooling
// consume it without redoing type checking.
// ---------------------------------------------------------------------------

class TypedResults {
public:
  // --- Expression types ---

  void set_expr_type(const Expr* expr, const Type* type) {
    expr_types_[expr] = type;
  }

  [[nodiscard]] auto expr_type(const Expr* expr) const -> const Type* {
    auto it = expr_types_.find(expr);
    return it != expr_types_.end() ? it->second : nullptr;
  }

  [[nodiscard]] auto expr_types() const
      -> const std::unordered_map<const Expr*, const Type*>& {
    return expr_types_;
  }

  // --- Local variable types (let bindings, for variables) ---

  void set_local_type(const Stmt* stmt, const Type* type) {
    local_types_[stmt] = type;
  }

  [[nodiscard]] auto local_type(const Stmt* stmt) const -> const Type* {
    auto it = local_types_.find(stmt);
    return it != local_types_.end() ? it->second : nullptr;
  }

  // --- Function declaration types ---

  void set_decl_type(const Decl* decl, const Type* type) {
    decl_types_[decl] = type;
  }

  [[nodiscard]] auto decl_type(const Decl* decl) const -> const Type* {
    auto it = decl_types_.find(decl);
    return it != decl_types_.end() ? it->second : nullptr;
  }

  // --- Method resolution (field access → method function symbol) ---

  // --- Method resolution (field expr → resolved method FunctionDecl) ---

  void set_method_resolution(const Expr* field_expr, const Decl* method_decl) {
    method_resolutions_[field_expr] = method_decl;
  }

  [[nodiscard]] auto method_resolution(const Expr* field_expr) const
      -> const Decl* {
    auto it = method_resolutions_.find(field_expr);
    return it != method_resolutions_.end() ? it->second : nullptr;
  }

  // --- Call-site explicit type arguments ---

  void set_call_type_args(const Expr* call_expr,
                          std::vector<const Type*> type_args) {
    call_type_args_[call_expr] = std::move(type_args);
  }

  [[nodiscard]] auto call_type_args(const Expr* call_expr) const
      -> const std::vector<const Type*>* {
    auto it = call_type_args_.find(call_expr);
    return it != call_type_args_.end() ? &it->second : nullptr;
  }

private:
  std::unordered_map<const Expr*, const Type*> expr_types_;
  std::unordered_map<const Stmt*, const Type*> local_types_;
  std::unordered_map<const Decl*, const Type*> decl_types_;
  std::unordered_map<const Expr*, const Decl*> method_resolutions_;
  std::unordered_map<const Expr*, std::vector<const Type*>> call_type_args_;
};

} // namespace dao

#endif // DAO_FRONTEND_TYPECHECK_TYPED_RESULTS_H
