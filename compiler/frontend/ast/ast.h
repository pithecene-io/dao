#ifndef DAO_FRONTEND_AST_AST_H
#define DAO_FRONTEND_AST_AST_H

#include "frontend/diagnostics/source.h"
#include "support/arena.h"

#include <cassert>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace dao {

// ---------------------------------------------------------------------------
// AstContext — owns all AST nodes for a compilation unit.
// ---------------------------------------------------------------------------

class AstContext {
public:
  AstContext() = default;
  ~AstContext() = default;

  AstContext(const AstContext&) = delete;
  auto operator=(const AstContext&) -> AstContext& = delete;
  AstContext(AstContext&&) noexcept = default;
  auto operator=(AstContext&&) noexcept -> AstContext& = default;

  template <typename T, typename... Args> auto alloc(Args&&... args) -> T* {
    return arena_.alloc<T>(std::forward<Args>(args)...);
  }

private:
  Arena arena_;
};

// ---------------------------------------------------------------------------
// Node kind tags
// ---------------------------------------------------------------------------

enum class NodeKind : std::uint8_t {
  // File
  File,
  Import,

  // Declarations
  FunctionDecl,
  ClassDecl,
  AliasDecl,

  // Class members
  FieldSpec,

  // Statements
  LetStatement,
  Assignment,
  IfStatement,
  WhileStatement,
  ForStatement,
  ModeBlock,
  ResourceBlock,
  ReturnStatement,
  ExpressionStatement,

  // Expressions
  BinaryExpr,
  UnaryExpr,
  CallExpr,
  IndexExpr,
  FieldExpr,
  PipeExpr,
  Lambda,
  IntLiteral,
  FloatLiteral,
  StringLiteral,
  BoolLiteral,
  ListLiteral,
  Identifier,
  QualifiedName,

  // Types
  NamedType,
  PointerType,
};

// ---------------------------------------------------------------------------
// Base classes
// ---------------------------------------------------------------------------

class AstNode {
public:
  explicit AstNode(NodeKind kind, Span span) : kind_(kind), span_(span) {
  }
  virtual ~AstNode() = default;

  AstNode(const AstNode&) = delete;
  auto operator=(const AstNode&) -> AstNode& = delete;
  AstNode(AstNode&&) = delete;
  auto operator=(AstNode&&) -> AstNode& = delete;

  [[nodiscard]] auto kind() const -> NodeKind {
    return kind_;
  }
  [[nodiscard]] auto span() const -> Span {
    return span_;
  }

protected:
  void set_span(Span span) {
    span_ = span;
  }

private:
  NodeKind kind_;
  Span span_;
};

class Decl : public AstNode {
public:
  using AstNode::AstNode;
  static auto classof(const AstNode* node) -> bool {
    return node->kind() >= NodeKind::FunctionDecl && node->kind() <= NodeKind::AliasDecl;
  }
};

class Stmt : public AstNode {
public:
  using AstNode::AstNode;
  static auto classof(const AstNode* node) -> bool {
    return node->kind() >= NodeKind::LetStatement && node->kind() <= NodeKind::ExpressionStatement;
  }
};

class Expr : public AstNode {
public:
  using AstNode::AstNode;
  static auto classof(const AstNode* node) -> bool {
    return node->kind() >= NodeKind::BinaryExpr && node->kind() <= NodeKind::QualifiedName;
  }
};

class TypeNode : public AstNode {
public:
  using AstNode::AstNode;
  static auto classof(const AstNode* node) -> bool {
    return node->kind() >= NodeKind::NamedType && node->kind() <= NodeKind::PointerType;
  }
};

// ---------------------------------------------------------------------------
// Utility types
// ---------------------------------------------------------------------------

struct QualifiedPath {
  std::vector<std::string_view> segments;
  Span span;
};

struct Param {
  std::string_view name;
  Span name_span;
  TypeNode* type;
};

// ---------------------------------------------------------------------------
// Binary / Unary operator tags
// ---------------------------------------------------------------------------

enum class BinaryOp : std::uint8_t {
  Add,    // +
  Sub,    // -
  Mul,    // *
  Div,    // /
  Mod,    // %
  EqEq,   // ==
  BangEq, // !=
  Lt,     // <
  LtEq,   // <=
  Gt,     // >
  GtEq,   // >=
  And,    // and
  Or,     // or
};

enum class UnaryOp : std::uint8_t {
  Negate, // -
  Not,    // !
  Deref,  // *
  AddrOf, // &
};

// ---------------------------------------------------------------------------
// File-level nodes
// ---------------------------------------------------------------------------

class FileNode : public AstNode {
public:
  FileNode(Span span, std::vector<AstNode*> imports, std::vector<Decl*> declarations)
      : AstNode(NodeKind::File, span), imports_(std::move(imports)),
        declarations_(std::move(declarations)) {
  }

  [[nodiscard]] auto imports() const -> const std::vector<AstNode*>& {
    return imports_;
  }
  [[nodiscard]] auto declarations() const -> const std::vector<Decl*>& {
    return declarations_;
  }

private:
  std::vector<AstNode*> imports_;
  std::vector<Decl*> declarations_;
};

class ImportNode : public AstNode {
public:
  ImportNode(Span span, QualifiedPath path)
      : AstNode(NodeKind::Import, span), path_(std::move(path)) {
  }

  [[nodiscard]] auto path() const -> const QualifiedPath& {
    return path_;
  }

private:
  QualifiedPath path_;
};

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

class FunctionDeclNode : public Decl {
public:
  FunctionDeclNode(Span span,
                   std::string_view name,
                   Span name_span,
                   std::vector<Param> params,
                   TypeNode* return_type,
                   std::vector<Stmt*> body,
                   Expr* expr_body,
                   bool is_extern = false)
      : Decl(NodeKind::FunctionDecl, span), name_(name), name_span_(name_span),
        params_(std::move(params)), return_type_(return_type), body_(std::move(body)),
        expr_body_(expr_body), is_extern_(is_extern) {
  }

  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }
  [[nodiscard]] auto name_span() const -> Span {
    return name_span_;
  }
  [[nodiscard]] auto params() const -> const std::vector<Param>& {
    return params_;
  }
  [[nodiscard]] auto return_type() const -> TypeNode* {
    return return_type_;
  }
  [[nodiscard]] auto body() const -> const std::vector<Stmt*>& {
    return body_;
  }
  [[nodiscard]] auto expr_body() const -> Expr* {
    return expr_body_;
  }
  [[nodiscard]] auto is_expr_bodied() const -> bool {
    return expr_body_ != nullptr;
  }
  [[nodiscard]] auto is_extern() const -> bool {
    return is_extern_;
  }

private:
  std::string_view name_;
  Span name_span_;
  std::vector<Param> params_;
  TypeNode* return_type_;   // nullable
  std::vector<Stmt*> body_; // empty if expression-bodied or extern
  Expr* expr_body_;         // nullptr if block-bodied or extern
  bool is_extern_ = false;
};

// ---------------------------------------------------------------------------
// Class field specifier
// ---------------------------------------------------------------------------

class FieldSpecNode : public AstNode {
public:
  FieldSpecNode(Span span, std::string_view name, Span name_span, TypeNode* type)
      : AstNode(NodeKind::FieldSpec, span), name_(name), name_span_(name_span), type_(type) {
  }

  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }
  [[nodiscard]] auto name_span() const -> Span {
    return name_span_;
  }
  [[nodiscard]] auto type() const -> TypeNode* {
    return type_;
  }

private:
  std::string_view name_;
  Span name_span_;
  TypeNode* type_;
};

class ClassDeclNode : public Decl {
public:
  ClassDeclNode(Span span, std::string_view name, Span name_span,
                std::vector<FieldSpecNode*> fields)
      : Decl(NodeKind::ClassDecl, span), name_(name), name_span_(name_span),
        fields_(std::move(fields)) {
  }

  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }
  [[nodiscard]] auto name_span() const -> Span {
    return name_span_;
  }
  [[nodiscard]] auto fields() const -> const std::vector<FieldSpecNode*>& {
    return fields_;
  }

private:
  std::string_view name_;
  Span name_span_;
  std::vector<FieldSpecNode*> fields_;
};

class AliasDeclNode : public Decl {
public:
  AliasDeclNode(Span span, std::string_view name, Span name_span, TypeNode* type)
      : Decl(NodeKind::AliasDecl, span), name_(name), name_span_(name_span), type_(type) {
  }

  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }
  [[nodiscard]] auto name_span() const -> Span {
    return name_span_;
  }
  [[nodiscard]] auto type() const -> TypeNode* {
    return type_;
  }

private:
  std::string_view name_;
  Span name_span_;
  TypeNode* type_;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

class LetStatementNode : public Stmt {
public:
  LetStatementNode(
      Span span, std::string_view name, Span name_span, TypeNode* type, Expr* initializer)
      : Stmt(NodeKind::LetStatement, span), name_(name), name_span_(name_span), type_(type),
        initializer_(initializer) {
  }

  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }
  [[nodiscard]] auto name_span() const -> Span {
    return name_span_;
  }
  [[nodiscard]] auto type() const -> TypeNode* {
    return type_;
  }
  [[nodiscard]] auto initializer() const -> Expr* {
    return initializer_;
  }

private:
  std::string_view name_;
  Span name_span_;
  TypeNode* type_;    // nullable
  Expr* initializer_; // nullable
};

class AssignmentNode : public Stmt {
public:
  AssignmentNode(Span span, Expr* target, Expr* value)
      : Stmt(NodeKind::Assignment, span), target_(target), value_(value) {
  }

  [[nodiscard]] auto target() const -> Expr* {
    return target_;
  }
  [[nodiscard]] auto value() const -> Expr* {
    return value_;
  }

private:
  Expr* target_;
  Expr* value_;
};

class IfStatementNode : public Stmt {
public:
  IfStatementNode(Span span,
                  Expr* condition,
                  std::vector<Stmt*> then_body,
                  std::vector<Stmt*> else_body)
      : Stmt(NodeKind::IfStatement, span), condition_(condition), then_body_(std::move(then_body)),
        else_body_(std::move(else_body)) {
  }

  [[nodiscard]] auto condition() const -> Expr* {
    return condition_;
  }
  [[nodiscard]] auto then_body() const -> const std::vector<Stmt*>& {
    return then_body_;
  }
  [[nodiscard]] auto else_body() const -> const std::vector<Stmt*>& {
    return else_body_;
  }
  [[nodiscard]] auto has_else() const -> bool {
    return !else_body_.empty();
  }

private:
  Expr* condition_;
  std::vector<Stmt*> then_body_;
  std::vector<Stmt*> else_body_;
};

class WhileStatementNode : public Stmt {
public:
  WhileStatementNode(Span span, Expr* condition, std::vector<Stmt*> body)
      : Stmt(NodeKind::WhileStatement, span), condition_(condition), body_(std::move(body)) {
  }

  [[nodiscard]] auto condition() const -> Expr* {
    return condition_;
  }
  [[nodiscard]] auto body() const -> const std::vector<Stmt*>& {
    return body_;
  }

private:
  Expr* condition_;
  std::vector<Stmt*> body_;
};

class ForStatementNode : public Stmt {
public:
  ForStatementNode(
      Span span, std::string_view var, Span var_span, Expr* iterable, std::vector<Stmt*> body)
      : Stmt(NodeKind::ForStatement, span), var_(var), var_span_(var_span), iterable_(iterable),
        body_(std::move(body)) {
  }

  [[nodiscard]] auto var() const -> std::string_view {
    return var_;
  }
  [[nodiscard]] auto var_span() const -> Span {
    return var_span_;
  }
  [[nodiscard]] auto iterable() const -> Expr* {
    return iterable_;
  }
  [[nodiscard]] auto body() const -> const std::vector<Stmt*>& {
    return body_;
  }

private:
  std::string_view var_;
  Span var_span_;
  Expr* iterable_;
  std::vector<Stmt*> body_;
};

class ModeBlockNode : public Stmt {
public:
  ModeBlockNode(Span span, std::string_view mode_name, Span name_span, std::vector<Stmt*> body)
      : Stmt(NodeKind::ModeBlock, span), mode_name_(mode_name), name_span_(name_span),
        body_(std::move(body)) {
  }

  [[nodiscard]] auto mode_name() const -> std::string_view {
    return mode_name_;
  }
  [[nodiscard]] auto name_span() const -> Span {
    return name_span_;
  }
  [[nodiscard]] auto body() const -> const std::vector<Stmt*>& {
    return body_;
  }

private:
  std::string_view mode_name_;
  Span name_span_;
  std::vector<Stmt*> body_;
};

class ResourceBlockNode : public Stmt {
public:
  ResourceBlockNode(Span span,
                    std::string_view resource_kind,
                    Span kind_span,
                    std::string_view resource_name,
                    Span name_span,
                    std::vector<Stmt*> body)
      : Stmt(NodeKind::ResourceBlock, span), resource_kind_(resource_kind), kind_span_(kind_span),
        resource_name_(resource_name), name_span_(name_span), body_(std::move(body)) {
  }

  [[nodiscard]] auto resource_kind() const -> std::string_view {
    return resource_kind_;
  }
  [[nodiscard]] auto kind_span() const -> Span {
    return kind_span_;
  }
  [[nodiscard]] auto resource_name() const -> std::string_view {
    return resource_name_;
  }
  [[nodiscard]] auto name_span() const -> Span {
    return name_span_;
  }
  [[nodiscard]] auto body() const -> const std::vector<Stmt*>& {
    return body_;
  }

private:
  std::string_view resource_kind_;
  Span kind_span_;
  std::string_view resource_name_;
  Span name_span_;
  std::vector<Stmt*> body_;
};

class ReturnStatementNode : public Stmt {
public:
  ReturnStatementNode(Span span, Expr* value)
      : Stmt(NodeKind::ReturnStatement, span), value_(value) {
  }

  [[nodiscard]] auto value() const -> Expr* {
    return value_;
  }

private:
  Expr* value_;
};

class ExpressionStatementNode : public Stmt {
public:
  ExpressionStatementNode(Span span, Expr* expr)
      : Stmt(NodeKind::ExpressionStatement, span), expr_(expr) {
  }

  [[nodiscard]] auto expr() const -> Expr* {
    return expr_;
  }

private:
  Expr* expr_;
};

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

class BinaryExprNode : public Expr {
public:
  // NOLINTNEXTLINE(readability-identifier-length)
  BinaryExprNode(Span span, BinaryOp op, Expr* left, Expr* right)
      : Expr(NodeKind::BinaryExpr, span), op_(op), left_(left), right_(right) {
  }

  [[nodiscard]] auto op() const -> BinaryOp {
    return op_;
  }
  [[nodiscard]] auto left() const -> Expr* {
    return left_;
  }
  [[nodiscard]] auto right() const -> Expr* {
    return right_;
  }

private:
  BinaryOp op_;
  Expr* left_;
  Expr* right_;
};

class UnaryExprNode : public Expr {
public:
  // NOLINTNEXTLINE(readability-identifier-length)
  UnaryExprNode(Span span, UnaryOp op, Expr* operand)
      : Expr(NodeKind::UnaryExpr, span), op_(op), operand_(operand) {
  }

  [[nodiscard]] auto op() const -> UnaryOp {
    return op_;
  }
  [[nodiscard]] auto operand() const -> Expr* {
    return operand_;
  }

private:
  UnaryOp op_;
  Expr* operand_;
};

class CallExprNode : public Expr {
public:
  CallExprNode(Span span, Expr* callee, std::vector<Expr*> args)
      : Expr(NodeKind::CallExpr, span), callee_(callee), args_(std::move(args)) {
  }

  [[nodiscard]] auto callee() const -> Expr* {
    return callee_;
  }
  [[nodiscard]] auto args() const -> const std::vector<Expr*>& {
    return args_;
  }

private:
  Expr* callee_;
  std::vector<Expr*> args_;
};

class IndexExprNode : public Expr {
public:
  IndexExprNode(Span span, Expr* object, std::vector<Expr*> indices)
      : Expr(NodeKind::IndexExpr, span), object_(object), indices_(std::move(indices)) {
  }

  [[nodiscard]] auto object() const -> Expr* {
    return object_;
  }
  [[nodiscard]] auto indices() const -> const std::vector<Expr*>& {
    return indices_;
  }

private:
  Expr* object_;
  std::vector<Expr*> indices_;
};

class FieldExprNode : public Expr {
public:
  FieldExprNode(Span span, Expr* object, std::string_view field, Span field_span)
      : Expr(NodeKind::FieldExpr, span), object_(object), field_(field), field_span_(field_span) {
  }

  [[nodiscard]] auto object() const -> Expr* {
    return object_;
  }
  [[nodiscard]] auto field() const -> std::string_view {
    return field_;
  }
  [[nodiscard]] auto field_span() const -> Span {
    return field_span_;
  }

private:
  Expr* object_;
  std::string_view field_;
  Span field_span_;
};

class PipeExprNode : public Expr {
public:
  PipeExprNode(Span span, Expr* left, Expr* right)
      : Expr(NodeKind::PipeExpr, span), left_(left), right_(right) {
  }

  [[nodiscard]] auto left() const -> Expr* {
    return left_;
  }
  [[nodiscard]] auto right() const -> Expr* {
    return right_;
  }

private:
  Expr* left_;
  Expr* right_;
};

class LambdaNode : public Expr {
public:
  LambdaNode(Span span, std::vector<std::pair<std::string_view, Span>> params, Expr* body)
      : Expr(NodeKind::Lambda, span), params_(std::move(params)), body_(body) {
  }

  [[nodiscard]] auto params() const -> const std::vector<std::pair<std::string_view, Span>>& {
    return params_;
  }
  [[nodiscard]] auto body() const -> Expr* {
    return body_;
  }

private:
  std::vector<std::pair<std::string_view, Span>> params_;
  Expr* body_;
};

class IntLiteralNode : public Expr {
public:
  IntLiteralNode(Span span, std::string_view text) : Expr(NodeKind::IntLiteral, span), text_(text) {
  }

  [[nodiscard]] auto text() const -> std::string_view {
    return text_;
  }

private:
  std::string_view text_;
};

class FloatLiteralNode : public Expr {
public:
  FloatLiteralNode(Span span, std::string_view text)
      : Expr(NodeKind::FloatLiteral, span), text_(text) {
  }

  [[nodiscard]] auto text() const -> std::string_view {
    return text_;
  }

private:
  std::string_view text_;
};

class StringLiteralNode : public Expr {
public:
  StringLiteralNode(Span span, std::string_view text)
      : Expr(NodeKind::StringLiteral, span), text_(text) {
  }

  [[nodiscard]] auto text() const -> std::string_view {
    return text_;
  }

private:
  std::string_view text_;
};

class BoolLiteralNode : public Expr {
public:
  BoolLiteralNode(Span span, bool value) : Expr(NodeKind::BoolLiteral, span), value_(value) {
  }

  [[nodiscard]] auto value() const -> bool {
    return value_;
  }

private:
  bool value_;
};

class ListLiteralNode : public Expr {
public:
  ListLiteralNode(Span span, std::vector<Expr*> elements)
      : Expr(NodeKind::ListLiteral, span), elements_(std::move(elements)) {
  }

  [[nodiscard]] auto elements() const -> const std::vector<Expr*>& {
    return elements_;
  }

private:
  std::vector<Expr*> elements_;
};

class IdentifierNode : public Expr {
public:
  IdentifierNode(Span span, std::string_view name) : Expr(NodeKind::Identifier, span), name_(name) {
  }

  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }

private:
  std::string_view name_;
};

class QualifiedNameNode : public Expr {
public:
  QualifiedNameNode(Span span, std::vector<std::string_view> segments)
      : Expr(NodeKind::QualifiedName, span), segments_(std::move(segments)) {
  }

  [[nodiscard]] auto segments() const -> const std::vector<std::string_view>& {
    return segments_;
  }

private:
  std::vector<std::string_view> segments_;
};

// ---------------------------------------------------------------------------
// Type nodes
// ---------------------------------------------------------------------------

class NamedTypeNode : public TypeNode {
public:
  NamedTypeNode(Span span, QualifiedPath name, std::vector<TypeNode*> type_args)
      : TypeNode(NodeKind::NamedType, span), name_(std::move(name)),
        type_args_(std::move(type_args)) {
  }

  [[nodiscard]] auto name() const -> const QualifiedPath& {
    return name_;
  }
  [[nodiscard]] auto type_args() const -> const std::vector<TypeNode*>& {
    return type_args_;
  }

private:
  QualifiedPath name_;
  std::vector<TypeNode*> type_args_;
};

class PointerTypeNode : public TypeNode {
public:
  PointerTypeNode(Span span, TypeNode* pointee)
      : TypeNode(NodeKind::PointerType, span), pointee_(pointee) {
  }

  [[nodiscard]] auto pointee() const -> TypeNode* {
    return pointee_;
  }

private:
  TypeNode* pointee_;
};

// ---------------------------------------------------------------------------
// Convenience: node_kind_name
// ---------------------------------------------------------------------------

auto node_kind_name(NodeKind kind) -> const char*;

} // namespace dao

#endif // DAO_FRONTEND_AST_AST_H
