#ifndef DAO_IR_HIR_HIR_BUILDER_H
#define DAO_IR_HIR_HIR_BUILDER_H

#include "frontend/ast/ast.h"
#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/type_checker.h"
#include "frontend/types/type_context.h"
#include "ir/hir/hir.h"
#include "ir/hir/hir_context.h"

#include <unordered_map>
#include <vector>

namespace dao {

// ---------------------------------------------------------------------------
// HirBuildResult — output of HIR construction.
// ---------------------------------------------------------------------------

struct HirBuildResult {
  HirModule* module = nullptr;
  std::vector<Diagnostic> diagnostics;
};

// ---------------------------------------------------------------------------
// HirBuilder — lowers checked AST into HIR.
//
// Consumes AST + resolve + typed results.
// Does not redo parsing, resolution, or type checking.
// ---------------------------------------------------------------------------

class HirBuilder {
public:
  HirBuilder(HirContext& ctx, const ResolveResult& resolve,
             const TypeCheckResult& typed);

  auto build(const FileNode& file) -> HirBuildResult;

private:
  HirContext& ctx_;
  const ResolveResult& resolve_;
  const TypeCheckResult& typed_;
  std::vector<Diagnostic> diagnostics_;

  // decl_span.offset -> Symbol* for declaration-site lookups.
  std::unordered_map<uint32_t, const Symbol*> decl_symbols_;

  // --- Declaration lowering ---

  auto lower_decl(const Decl* decl) -> HirDecl*;
  auto lower_function(const FunctionDeclNode* fn) -> HirFunction*;
  auto lower_struct(const StructDeclNode* st) -> HirStructDecl*;

  // --- Statement lowering ---

  auto lower_stmt(const Stmt* stmt) -> HirStmt*;
  auto lower_let(const LetStatementNode* let) -> HirLet*;
  auto lower_assignment(const AssignmentNode* assign) -> HirAssign*;
  auto lower_if(const IfStatementNode* ifn) -> HirIf*;
  auto lower_while(const WhileStatementNode* wh) -> HirWhile*;
  auto lower_for(const ForStatementNode* fo) -> HirFor*;
  auto lower_mode(const ModeBlockNode* mb) -> HirMode*;
  auto lower_resource(const ResourceBlockNode* rb) -> HirResource*;
  auto lower_return(const ReturnStatementNode* ret) -> HirReturn*;
  auto lower_expr_stmt(const ExpressionStatementNode* es) -> HirExprStmt*;

  auto lower_body(const std::vector<Stmt*>& body) -> std::vector<HirStmt*>;

  // --- Expression lowering ---

  auto lower_expr(const Expr* expr) -> HirExpr*;

  // --- Helpers ---

  auto find_symbol_at_decl(uint32_t offset) -> const Symbol*;
  auto find_symbol_at_use(uint32_t offset) -> const Symbol*;
  auto expr_type(const Expr* expr) -> const Type*;

  void error(Span span, std::string message);
};

// ---------------------------------------------------------------------------
// Top-level entry point.
// ---------------------------------------------------------------------------

auto build_hir(const FileNode& file, const ResolveResult& resolve,
               const TypeCheckResult& typed, HirContext& ctx)
    -> HirBuildResult;

} // namespace dao

#endif // DAO_IR_HIR_HIR_BUILDER_H
