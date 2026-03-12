#ifndef DAO_FRONTEND_TYPECHECK_TYPE_CHECKER_H
#define DAO_FRONTEND_TYPECHECK_TYPE_CHECKER_H

#include "frontend/ast/ast.h"
#include "frontend/diagnostics/diagnostic.h"
#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/typed_results.h"
#include "frontend/types/type_context.h"
#include "frontend/types/type_printer.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dao {

// ---------------------------------------------------------------------------
// TypeCheckResult — output of the type-checking pass.
// ---------------------------------------------------------------------------

struct TypeCheckResult {
  TypedResults typed;
  std::vector<Diagnostic> diagnostics;
};

// ---------------------------------------------------------------------------
// CheckContext — per-function / per-scope typing context.
// ---------------------------------------------------------------------------

struct CheckContext {
  const Type* return_type = nullptr; // enclosing function return type
  const Type* self_type = nullptr;   // type of `self` in current scope (class/extend)
  std::unordered_set<std::string_view> active_modes; // e.g. "unsafe"
};

// ---------------------------------------------------------------------------
// TypeChecker — orchestrates type checking for a file.
//
// Consumes:
//   - parsed AST
//   - resolver results
//   - TypeContext for semantic type construction/interning
//
// Produces:
//   - TypeCheckResult (typed side tables + diagnostics)
// ---------------------------------------------------------------------------

class TypeChecker {
public:
  TypeChecker(TypeContext& types, const ResolveResult& resolve);

  auto check(const FileNode& file) -> TypeCheckResult;

private:
  TypeContext& types_;
  const ResolveResult& resolve_;
  const FileNode* file_ = nullptr;
  TypedResults typed_;
  std::vector<Diagnostic> diagnostics_;
  CheckContext ctx_;

  // Symbol -> semantic type cache (populated in pass 1).
  std::unordered_map<const Symbol*, const Type*> symbol_types_;

  // decl_span.offset -> Symbol* for finding symbols at declaration sites.
  std::unordered_map<uint32_t, const Symbol*> decl_symbols_;

  // Derived concept tracking: ConceptDecl nodes marked `derived`.
  std::vector<const Decl*> derived_concepts_;

  // Derived conformances: type -> list of derived concept Decls it auto-conforms to.
  std::unordered_map<const Type*, std::vector<const Decl*>> derived_conformances_;

  // --- TypeNode -> Type* bridge ---

  auto resolve_type_node(const TypeNode* node) -> const Type*;

  // --- Symbol -> Type* bridge ---

  auto resolve_symbol_type(const Symbol* sym) -> const Type*;
  auto resolve_symbol_type_for_type_decl(const Symbol* sym) -> const Type*;

  // --- Declaration checking ---

  void register_declarations(const FileNode& file);
  void compute_derived_conformances(const FileNode& file);
  auto type_conforms_to(const Type* type, const Decl* concept_decl) -> bool;
  void check_declaration(const Decl* decl);
  void check_function(const Decl* decl);
  void check_class(const Decl* decl);

  // --- Statement checking ---

  void check_statement(const Stmt* stmt);
  void check_let(const Stmt* stmt);
  void check_assignment(const Stmt* stmt);
  void check_if(const Stmt* stmt);
  void check_while(const Stmt* stmt);
  void check_for(const Stmt* stmt);
  void check_mode_block(const Stmt* stmt);
  void check_resource_block(const Stmt* stmt);
  void check_return(const Stmt* stmt);
  void check_expr_stmt(const Stmt* stmt);

  void check_body(const std::vector<Stmt*>& body);

  // --- Expression checking ---

  auto check_expr(const Expr* expr) -> const Type*;
  auto check_expr(const Expr* expr, const Type* expected) -> const Type*;

  auto check_identifier(const Expr* expr) -> const Type*;
  auto check_int_literal(const Expr* expr) -> const Type*;
  auto check_float_literal(const Expr* expr) -> const Type*;
  auto check_string_literal(const Expr* expr) -> const Type*;
  auto check_bool_literal(const Expr* expr) -> const Type*;
  auto check_binary(const Expr* expr) -> const Type*;
  auto check_unary(const Expr* expr) -> const Type*;
  auto check_call(const Expr* expr) -> const Type*;
  auto check_construct(const Expr* expr, const TypeStruct* struct_type)
      -> const Type*;
  auto check_pipe(const Expr* expr) -> const Type*;
  auto check_field(const Expr* expr) -> const Type*;
  auto lookup_method(const Type* obj_type, std::string_view name)
      -> const Type*;
  void validate_receiver(const Decl* method, Span context_span);
  auto check_index(const Expr* expr) -> const Type*;
  auto check_lambda(const Expr* expr, const Type* expected) -> const Type*;
  auto check_list_literal(const Expr* expr) -> const Type*;

  // --- Diagnostics ---

  void error(Span span, std::string message);

  // --- Helpers ---

  auto is_lvalue(const Expr* expr) -> bool;
  auto find_generic_param_index(const Symbol* sym) -> uint32_t;
};

// ---------------------------------------------------------------------------
// Top-level entry point.
// ---------------------------------------------------------------------------

auto typecheck(const FileNode& file, const ResolveResult& resolve,
               TypeContext& types) -> TypeCheckResult;

} // namespace dao

#endif // DAO_FRONTEND_TYPECHECK_TYPE_CHECKER_H
