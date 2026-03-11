#include "frontend/ast/ast.h"

namespace dao {

// ---------------------------------------------------------------------------
// kind() — derived from active variant alternative.
// ---------------------------------------------------------------------------

auto Decl::kind() const -> NodeKind {
  return std::visit(overloaded{
      [](const FunctionDecl&) { return NodeKind::FunctionDecl; },
      [](const ClassDecl&) { return NodeKind::ClassDecl; },
      [](const AliasDecl&) { return NodeKind::AliasDecl; },
  }, payload);
}

auto Stmt::kind() const -> NodeKind {
  return std::visit(overloaded{
      [](const LetStatement&) { return NodeKind::LetStatement; },
      [](const Assignment&) { return NodeKind::Assignment; },
      [](const IfStatement&) { return NodeKind::IfStatement; },
      [](const WhileStatement&) { return NodeKind::WhileStatement; },
      [](const ForStatement&) { return NodeKind::ForStatement; },
      [](const ModeBlock&) { return NodeKind::ModeBlock; },
      [](const ResourceBlock&) { return NodeKind::ResourceBlock; },
      [](const ReturnStatement&) { return NodeKind::ReturnStatement; },
      [](const ExpressionStatement&) { return NodeKind::ExpressionStatement; },
  }, payload);
}

auto Expr::kind() const -> NodeKind {
  return std::visit(overloaded{
      [](const BinaryExpr&) { return NodeKind::BinaryExpr; },
      [](const UnaryExpr&) { return NodeKind::UnaryExpr; },
      [](const CallExpr&) { return NodeKind::CallExpr; },
      [](const IndexExpr&) { return NodeKind::IndexExpr; },
      [](const FieldExpr&) { return NodeKind::FieldExpr; },
      [](const PipeExpr&) { return NodeKind::PipeExpr; },
      [](const LambdaExpr&) { return NodeKind::Lambda; },
      [](const IntLiteral&) { return NodeKind::IntLiteral; },
      [](const FloatLiteral&) { return NodeKind::FloatLiteral; },
      [](const StringLiteral&) { return NodeKind::StringLiteral; },
      [](const BoolLiteral&) { return NodeKind::BoolLiteral; },
      [](const ListLiteral&) { return NodeKind::ListLiteral; },
      [](const IdentifierExpr&) { return NodeKind::Identifier; },
      [](const QualifiedName&) { return NodeKind::QualifiedName; },
  }, payload);
}

auto TypeNode::kind() const -> NodeKind {
  return std::visit(overloaded{
      [](const NamedType&) { return NodeKind::NamedType; },
      [](const PointerType&) { return NodeKind::PointerType; },
  }, payload);
}

// ---------------------------------------------------------------------------
// node_kind_name
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto node_kind_name(NodeKind kind) -> const char* {
  switch (kind) {
  case NodeKind::File:
    return "File";
  case NodeKind::Import:
    return "Import";
  case NodeKind::FunctionDecl:
    return "FunctionDecl";
  case NodeKind::ClassDecl:
    return "ClassDecl";
  case NodeKind::AliasDecl:
    return "AliasDecl";
  case NodeKind::FieldSpec:
    return "FieldSpec";
  case NodeKind::LetStatement:
    return "LetStatement";
  case NodeKind::Assignment:
    return "Assignment";
  case NodeKind::IfStatement:
    return "IfStatement";
  case NodeKind::WhileStatement:
    return "WhileStatement";
  case NodeKind::ForStatement:
    return "ForStatement";
  case NodeKind::ModeBlock:
    return "ModeBlock";
  case NodeKind::ResourceBlock:
    return "ResourceBlock";
  case NodeKind::ReturnStatement:
    return "ReturnStatement";
  case NodeKind::ExpressionStatement:
    return "ExpressionStatement";
  case NodeKind::BinaryExpr:
    return "BinaryExpr";
  case NodeKind::UnaryExpr:
    return "UnaryExpr";
  case NodeKind::CallExpr:
    return "CallExpr";
  case NodeKind::IndexExpr:
    return "IndexExpr";
  case NodeKind::FieldExpr:
    return "FieldExpr";
  case NodeKind::PipeExpr:
    return "PipeExpr";
  case NodeKind::Lambda:
    return "Lambda";
  case NodeKind::IntLiteral:
    return "IntLiteral";
  case NodeKind::FloatLiteral:
    return "FloatLiteral";
  case NodeKind::StringLiteral:
    return "StringLiteral";
  case NodeKind::BoolLiteral:
    return "BoolLiteral";
  case NodeKind::ListLiteral:
    return "ListLiteral";
  case NodeKind::Identifier:
    return "Identifier";
  case NodeKind::QualifiedName:
    return "QualifiedName";
  case NodeKind::NamedType:
    return "NamedType";
  case NodeKind::PointerType:
    return "PointerType";
  }
  return "Unknown";
}

} // namespace dao
