#include "frontend/resolve/resolve.h"

#include <string>
#include <string_view>

namespace dao {

auto symbol_kind_name(SymbolKind kind) -> const char* {
  switch (kind) {
  case SymbolKind::Function:
    return "Function";
  case SymbolKind::Type:
    return "Type";
  case SymbolKind::Param:
    return "Param";
  case SymbolKind::Local:
    return "Local";
  case SymbolKind::Field:
    return "Field";
  case SymbolKind::Module:
    return "Module";
  case SymbolKind::Builtin:
    return "Builtin";
  case SymbolKind::Predeclared:
    return "Predeclared";
  case SymbolKind::LambdaParam:
    return "LambdaParam";
  }
  return "Unknown";
}

namespace {

// ---------------------------------------------------------------------------
// Builtin type names — pre-populated into the file scope
// ---------------------------------------------------------------------------

constexpr std::string_view kBuiltinTypes[] = {
    "i8",  "i16", "i32", "i64", "u8",   "u16",
    "u32", "u64", "f32", "f64", "bool",
};

// Predeclared named types — not builtin scalars, but compiler-known
// so that examples work without imports. See
// CONTRACT_TYPE_SYSTEM_FOUNDATIONS.md §5.
constexpr std::string_view kPredeclaredTypes[] = {
    "string",
    "void",
};

// ---------------------------------------------------------------------------
// Resolver — two-pass name resolution over the AST
// ---------------------------------------------------------------------------

class Resolver {
public:
  auto run(const FileNode& file) -> ResolveResult {
    // Create the file scope and pre-populate builtins.
    file_scope_ = ctx_.make_scope(ScopeKind::File, nullptr);
    populate_builtins();

    // Pass 1: Collect top-level declarations and imports.
    collect_top_level(file);

    // Pass 2: Resolve bodies.
    resolve_bodies(file);

    return ResolveResult{
        .context = std::move(ctx_),
        .uses = std::move(uses_),
        .diagnostics = std::move(diagnostics_),
    };
  }

private:
  ResolveContext ctx_;
  Scope* file_scope_ = nullptr;
  std::unordered_map<uint32_t, Symbol*> uses_;
  std::vector<Diagnostic> diagnostics_;

  // --- Builtin population ---

  void populate_builtins() {
    for (auto name : kBuiltinTypes) {
      auto* sym = ctx_.make_symbol(SymbolKind::Builtin, name, Span{}, nullptr);
      file_scope_->declare(name, sym);
    }
    for (auto name : kPredeclaredTypes) {
      auto* sym = ctx_.make_symbol(SymbolKind::Predeclared, name, Span{}, nullptr);
      file_scope_->declare(name, sym);
    }
  }

  // --- Pass 1: Collect top-level declarations ---

  void collect_top_level(const FileNode& file) {
    // Register imports.
    for (const auto* imp : file.imports()) {
      const auto& import_node = static_cast<const ImportNode&>(*imp);
      collect_import(import_node);
    }

    // Register top-level declarations.
    for (const auto* decl : file.declarations()) {
      collect_decl(*decl);
    }
  }

  void collect_import(const ImportNode& node) {
    const auto& path = node.path();
    if (path.segments.empty()) {
      return;
    }

    // Bind the last segment as a Module symbol.
    auto binding_name = path.segments.back();
    auto binding_len = static_cast<uint32_t>(binding_name.size());

    // Compute the span of the last segment.
    uint32_t offset = path.span.offset;
    for (size_t i = 0; i + 1 < path.segments.size(); ++i) {
      offset += static_cast<uint32_t>(path.segments[i].size()) + 2; // skip "::"
    }
    Span binding_span{.offset = offset, .length = binding_len};

    if (file_scope_->lookup_local(binding_name) != nullptr) {
      diagnostics_.push_back(Diagnostic::error(
          binding_span,
          "duplicate top-level declaration '" + std::string(binding_name) + "'"));
    } else {
      auto* sym = ctx_.make_symbol(SymbolKind::Module, binding_name, binding_span, &node);
      file_scope_->declare(binding_name, sym);
    }
  }

  void collect_decl(const Decl& decl) {
    std::string_view name;
    Span name_span{};
    SymbolKind kind{};

    switch (decl.kind()) {
    case NodeKind::FunctionDecl: {
      const auto& fn = static_cast<const FunctionDeclNode&>(decl);
      name = fn.name();
      name_span = fn.name_span();
      kind = SymbolKind::Function;
      break;
    }
    case NodeKind::StructDecl: {
      const auto& st = static_cast<const StructDeclNode&>(decl);
      name = st.name();
      name_span = st.name_span();
      kind = SymbolKind::Type;
      break;
    }
    case NodeKind::AliasDecl: {
      const auto& alias = static_cast<const AliasDeclNode&>(decl);
      name = alias.name();
      name_span = alias.name_span();
      kind = SymbolKind::Type;
      break;
    }
    default:
      return;
    }

    if (file_scope_->lookup_local(name) != nullptr) {
      diagnostics_.push_back(Diagnostic::error(
          name_span,
          "duplicate top-level declaration '" + std::string(name) + "'"));
    } else {
      auto* sym = ctx_.make_symbol(kind, name, name_span, &decl);
      file_scope_->declare(name, sym);
    }
  }

  // --- Pass 2: Resolve bodies ---

  void resolve_bodies(const FileNode& file) {
    for (const auto* decl : file.declarations()) {
      resolve_decl(*decl, file_scope_);
    }
  }

  void resolve_decl(const Decl& decl, Scope* scope) {
    switch (decl.kind()) {
    case NodeKind::FunctionDecl:
      resolve_function(static_cast<const FunctionDeclNode&>(decl), scope);
      break;
    case NodeKind::StructDecl:
      resolve_struct(static_cast<const StructDeclNode&>(decl), scope);
      break;
    case NodeKind::AliasDecl:
      resolve_alias(static_cast<const AliasDeclNode&>(decl), scope);
      break;
    default:
      break;
    }
  }

  void resolve_function(const FunctionDeclNode& fn, Scope* parent) {
    auto* fn_scope = ctx_.make_scope(ScopeKind::Function, parent);

    // Declare parameters.
    for (const auto& param : fn.params()) {
      if (fn_scope->lookup_local(param.name) != nullptr) {
        diagnostics_.push_back(Diagnostic::error(
            param.name_span,
            "duplicate parameter '" + std::string(param.name) + "'"));
      } else {
        auto* sym = ctx_.make_symbol(SymbolKind::Param, param.name, param.name_span, &fn);
        fn_scope->declare(param.name, sym);
      }

      // Resolve parameter type (type-position, no diagnostic on unknown).
      if (param.type != nullptr) {
        resolve_type(*param.type, fn_scope);
      }
    }

    // Resolve return type.
    if (fn.return_type() != nullptr) {
      resolve_type(*fn.return_type(), fn_scope);
    }

    // Resolve body statements in a nested block scope so that let
    // bindings can shadow parameters without triggering duplicate errors.
    auto* body_scope = ctx_.make_scope(ScopeKind::Block, fn_scope);
    for (const auto* stmt : fn.body()) {
      resolve_stmt(*stmt, body_scope);
    }

    // Resolve expression body (same body scope).
    if (fn.expr_body() != nullptr) {
      resolve_expr(*fn.expr_body(), body_scope);
    }
  }

  void resolve_struct(const StructDeclNode& st, Scope* parent) {
    auto* struct_scope = ctx_.make_scope(ScopeKind::Struct, parent);

    for (const auto* member : st.members()) {
      if (member->kind() == NodeKind::LetStatement) {
        const auto& let_stmt = static_cast<const LetStatementNode&>(*member);
        if (struct_scope->lookup_local(let_stmt.name()) != nullptr) {
          diagnostics_.push_back(Diagnostic::error(
              let_stmt.name_span(),
              "duplicate declaration '" + std::string(let_stmt.name()) + "'"));
        } else {
          auto* sym =
              ctx_.make_symbol(SymbolKind::Field, let_stmt.name(), let_stmt.name_span(), &let_stmt);
          struct_scope->declare(let_stmt.name(), sym);
        }

        if (let_stmt.type() != nullptr) {
          resolve_type(*let_stmt.type(), struct_scope);
        }
        if (let_stmt.initializer() != nullptr) {
          resolve_expr(*let_stmt.initializer(), struct_scope);
        }
      }
    }
  }

  void resolve_alias(const AliasDeclNode& alias, Scope* scope) {
    if (alias.type() != nullptr) {
      resolve_type(*alias.type(), scope);
    }
  }

  // --- Statements ---

  void resolve_stmt(const Stmt& stmt, Scope* scope) {
    switch (stmt.kind()) {
    case NodeKind::LetStatement: {
      const auto& let_stmt = static_cast<const LetStatementNode&>(stmt);

      // Resolve type and initializer BEFORE declaring (prevents self-reference).
      if (let_stmt.type() != nullptr) {
        resolve_type(*let_stmt.type(), scope);
      }
      if (let_stmt.initializer() != nullptr) {
        resolve_expr(*let_stmt.initializer(), scope);
      }

      // Declare the local variable (visible after this point).
      if (scope->lookup_local(let_stmt.name()) != nullptr) {
        diagnostics_.push_back(Diagnostic::error(
            let_stmt.name_span(),
            "duplicate declaration '" + std::string(let_stmt.name()) + "'"));
      } else {
        auto* sym =
            ctx_.make_symbol(SymbolKind::Local, let_stmt.name(), let_stmt.name_span(), &let_stmt);
        scope->declare(let_stmt.name(), sym);
      }
      break;
    }
    case NodeKind::Assignment: {
      const auto& assign = static_cast<const AssignmentNode&>(stmt);
      resolve_expr(*assign.target(), scope);
      resolve_expr(*assign.value(), scope);
      break;
    }
    case NodeKind::IfStatement: {
      const auto& if_stmt = static_cast<const IfStatementNode&>(stmt);
      resolve_expr(*if_stmt.condition(), scope);

      auto* then_scope = ctx_.make_scope(ScopeKind::Block, scope);
      for (const auto* s : if_stmt.then_body()) {
        resolve_stmt(*s, then_scope);
      }

      if (if_stmt.has_else()) {
        auto* else_scope = ctx_.make_scope(ScopeKind::Block, scope);
        for (const auto* s : if_stmt.else_body()) {
          resolve_stmt(*s, else_scope);
        }
      }
      break;
    }
    case NodeKind::WhileStatement: {
      const auto& while_stmt = static_cast<const WhileStatementNode&>(stmt);
      resolve_expr(*while_stmt.condition(), scope);

      auto* while_scope = ctx_.make_scope(ScopeKind::Block, scope);
      for (const auto* s : while_stmt.body()) {
        resolve_stmt(*s, while_scope);
      }
      break;
    }
    case NodeKind::ForStatement: {
      const auto& for_stmt = static_cast<const ForStatementNode&>(stmt);

      // Resolve iterable in the outer scope.
      resolve_expr(*for_stmt.iterable(), scope);

      // Create block scope for the loop body; declare the loop variable.
      auto* for_scope = ctx_.make_scope(ScopeKind::Block, scope);
      auto* sym = ctx_.make_symbol(
          SymbolKind::Local, for_stmt.var(), for_stmt.var_span(), &for_stmt);
      for_scope->declare(for_stmt.var(), sym);

      for (const auto* s : for_stmt.body()) {
        resolve_stmt(*s, for_scope);
      }
      break;
    }
    case NodeKind::ModeBlock: {
      const auto& mode = static_cast<const ModeBlockNode&>(stmt);
      auto* mode_scope = ctx_.make_scope(ScopeKind::Block, scope);
      for (const auto* s : mode.body()) {
        resolve_stmt(*s, mode_scope);
      }
      break;
    }
    case NodeKind::ResourceBlock: {
      const auto& res = static_cast<const ResourceBlockNode&>(stmt);
      auto* res_scope = ctx_.make_scope(ScopeKind::Block, scope);
      for (const auto* s : res.body()) {
        resolve_stmt(*s, res_scope);
      }
      break;
    }
    case NodeKind::ReturnStatement: {
      const auto& ret = static_cast<const ReturnStatementNode&>(stmt);
      if (ret.value() != nullptr) {
        resolve_expr(*ret.value(), scope);
      }
      break;
    }
    case NodeKind::ExpressionStatement: {
      const auto& expr_stmt = static_cast<const ExpressionStatementNode&>(stmt);
      resolve_expr(*expr_stmt.expr(), scope);
      break;
    }
    default:
      break;
    }
  }

  // --- Expressions ---

  void resolve_expr(const Expr& expr, Scope* scope) {
    switch (expr.kind()) {
    case NodeKind::Identifier: {
      const auto& ident = static_cast<const IdentifierNode&>(expr);
      auto* sym = scope->lookup(ident.name());
      if (sym != nullptr) {
        uses_[expr.span().offset] = sym;
      } else {
        diagnostics_.push_back(Diagnostic::error(
            expr.span(),
            "unknown name '" + std::string(ident.name()) + "'"));
      }
      break;
    }
    case NodeKind::QualifiedName: {
      const auto& qn = static_cast<const QualifiedNameNode&>(expr);
      if (qn.segments().empty()) {
        break;
      }

      // Resolve first segment against the scope chain — must be a
      // module/import binding per TASK_6_RESOLVE.md.
      auto first_seg = qn.segments().front();
      auto seg_len = static_cast<uint32_t>(first_seg.size());
      Span seg_span{.offset = expr.span().offset, .length = seg_len};
      auto* sym = scope->lookup(first_seg);

      if (sym == nullptr) {
        diagnostics_.push_back(Diagnostic::error(
            seg_span,
            "unknown name '" + std::string(first_seg) + "'"));
      } else if (sym->kind != SymbolKind::Module) {
        diagnostics_.push_back(Diagnostic::error(
            seg_span,
            "'" + std::string(first_seg) + "' is not a module"));
      } else {
        // Record the first segment's resolution.
        uses_[expr.span().offset] = sym;
        // Trailing segments are unresolvable in Task 6 (no cross-file
        // module resolution) — left without entries in the uses table.
      }
      break;
    }
    case NodeKind::BinaryExpr: {
      const auto& bin = static_cast<const BinaryExprNode&>(expr);
      resolve_expr(*bin.left(), scope);
      resolve_expr(*bin.right(), scope);
      break;
    }
    case NodeKind::UnaryExpr: {
      const auto& unary = static_cast<const UnaryExprNode&>(expr);
      resolve_expr(*unary.operand(), scope);
      break;
    }
    case NodeKind::CallExpr: {
      const auto& call = static_cast<const CallExprNode&>(expr);
      resolve_expr(*call.callee(), scope);
      for (const auto* arg : call.args()) {
        resolve_expr(*arg, scope);
      }
      break;
    }
    case NodeKind::IndexExpr: {
      const auto& idx = static_cast<const IndexExprNode&>(expr);
      resolve_expr(*idx.object(), scope);
      for (const auto* i : idx.indices()) {
        resolve_expr(*i, scope);
      }
      break;
    }
    case NodeKind::FieldExpr: {
      const auto& field = static_cast<const FieldExprNode&>(expr);
      resolve_expr(*field.object(), scope);
      // Field member access is not resolved in Task 6 (needs type info).
      break;
    }
    case NodeKind::PipeExpr: {
      const auto& pipe = static_cast<const PipeExprNode&>(expr);
      resolve_expr(*pipe.left(), scope);
      resolve_expr(*pipe.right(), scope);
      break;
    }
    case NodeKind::Lambda: {
      const auto& lam = static_cast<const LambdaNode&>(expr);

      // Create a block scope for the lambda body.
      auto* lam_scope = ctx_.make_scope(ScopeKind::Block, scope);
      for (const auto& [name, span] : lam.params()) {
        if (lam_scope->lookup_local(name) != nullptr) {
          diagnostics_.push_back(Diagnostic::error(
              span,
              "duplicate parameter '" + std::string(name) + "'"));
        } else {
          auto* sym = ctx_.make_symbol(SymbolKind::LambdaParam, name, span, &lam);
          lam_scope->declare(name, sym);
        }
      }
      resolve_expr(*lam.body(), lam_scope);
      break;
    }
    case NodeKind::ListLiteral: {
      const auto& list = static_cast<const ListLiteralNode&>(expr);
      for (const auto* elem : list.elements()) {
        resolve_expr(*elem, scope);
      }
      break;
    }
    // Terminals — no resolution needed.
    case NodeKind::IntLiteral:
    case NodeKind::FloatLiteral:
    case NodeKind::StringLiteral:
    case NodeKind::BoolLiteral:
      break;
    default:
      break;
    }
  }

  // --- Types ---

  void resolve_type(const TypeNode& type, Scope* scope) {
    switch (type.kind()) {
    case NodeKind::NamedType: {
      const auto& named = static_cast<const NamedTypeNode&>(type);
      const auto& path = named.name();

      if (path.segments.empty()) {
        break;
      }

      if (path.segments.size() == 1) {
        // Single-segment type: look up in scope chain. No diagnostic if
        // not found (type-position references are allowed unresolved).
        auto type_name = path.segments.front();
        auto* sym = scope->lookup(type_name);
        if (sym != nullptr) {
          uses_[path.span.offset] = sym;
        }
      } else {
        // Multi-segment type: resolve leading segment as module reference.
        // Only Module symbols are valid as leading segments of qualified
        // type paths — other kinds are silently ignored (type-position
        // references are not diagnosed for unknown names).
        auto first_seg = path.segments.front();
        auto* sym = scope->lookup(first_seg);
        if (sym != nullptr && sym->kind == SymbolKind::Module) {
          uses_[path.span.offset] = sym;
        }
        // Trailing segments are unresolvable without cross-file resolution.
      }

      // Resolve type arguments recursively.
      for (const auto* arg : named.type_args()) {
        resolve_type(*arg, scope);
      }
      break;
    }
    case NodeKind::PointerType: {
      const auto& ptr = static_cast<const PointerTypeNode&>(type);
      resolve_type(*ptr.pointee(), scope);
      break;
    }
    default:
      break;
    }
  }
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

auto resolve(const FileNode& file) -> ResolveResult {
  Resolver resolver;
  return resolver.run(file);
}

} // namespace dao
