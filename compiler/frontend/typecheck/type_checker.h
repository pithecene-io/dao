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
  TypedResults typed_;
  std::vector<Diagnostic> diagnostics_;
  CheckContext ctx_;

  // Symbol -> semantic type cache (populated in pass 1).
  std::unordered_map<const Symbol*, const Type*> symbol_types_;

  // decl_span.offset -> Symbol* for finding symbols at declaration sites.
  std::unordered_map<uint32_t, const Symbol*> decl_symbols_;

  // --- TypeNode -> Type* bridge ---

  auto resolve_type_node(const TypeNode* node) -> const Type*;

  // --- Symbol -> Type* bridge ---

  auto resolve_symbol_type(const Symbol* sym) -> const Type*;
  auto resolve_symbol_type_for_type_decl(const Symbol* sym) -> const Type*;

  // --- Declaration checking ---

  void register_declarations(const FileNode& file);
  void check_declaration(const Decl* decl);
  void check_function(const FunctionDeclNode* fn);
  void check_struct(const StructDeclNode* st);

  // --- Statement checking ---

  void check_statement(const Stmt* stmt);
  void check_let(const LetStatementNode* let);
  void check_assignment(const AssignmentNode* assign);
  void check_if(const IfStatementNode* ifn);
  void check_while(const WhileStatementNode* wh);
  void check_for(const ForStatementNode* fo);
  void check_mode_block(const ModeBlockNode* mb);
  void check_resource_block(const ResourceBlockNode* rb);
  void check_return(const ReturnStatementNode* ret);
  void check_expr_stmt(const ExpressionStatementNode* es);

  void check_body(const std::vector<Stmt*>& body);

  // --- Expression checking ---

  auto check_expr(const Expr* expr) -> const Type*;
  auto check_expr(const Expr* expr, const Type* expected) -> const Type*;

  auto check_identifier(const IdentifierNode* id) -> const Type*;
  auto check_int_literal(const IntLiteralNode* lit) -> const Type*;
  auto check_float_literal(const FloatLiteralNode* lit) -> const Type*;
  auto check_string_literal(const StringLiteralNode* lit) -> const Type*;
  auto check_bool_literal(const BoolLiteralNode* lit) -> const Type*;
  auto check_binary(const BinaryExprNode* bin) -> const Type*;
  auto check_unary(const UnaryExprNode* un) -> const Type*;
  auto check_call(const CallExprNode* call) -> const Type*;
  auto check_pipe(const PipeExprNode* pipe) -> const Type*;
  auto check_field(const FieldExprNode* field) -> const Type*;
  auto check_index(const IndexExprNode* idx) -> const Type*;
  auto check_lambda(const LambdaNode* lam, const Type* expected) -> const Type*;
  auto check_list_literal(const ListLiteralNode* list) -> const Type*;

  // --- Diagnostics ---

  void error(Span span, std::string message);

  // --- Helpers ---

  auto is_lvalue(const Expr* expr) -> bool;
};

// ---------------------------------------------------------------------------
// Top-level entry point.
// ---------------------------------------------------------------------------

auto typecheck(const FileNode& file, const ResolveResult& resolve,
               TypeContext& types) -> TypeCheckResult;

} // namespace dao

#endif // DAO_FRONTEND_TYPECHECK_TYPE_CHECKER_H
