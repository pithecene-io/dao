#include "frontend/ast/ast_printer.h"

#include <string>

namespace dao {

namespace {

// NOLINTBEGIN(readability-identifier-length)

class AstPrinter {
public:
  explicit AstPrinter(std::ostream& out) : out_(out) {
  }

  void print(const FileNode& file) {
    out_ << "File\n";
    for (const auto* imp : file.imports()) {
      print_import(static_cast<const ImportNode&>(*imp));
    }
    for (const auto* decl : file.declarations()) {
      print_decl(*decl);
    }
  }

private:
  std::ostream& out_;
  int depth_ = 1;

  void indent() {
    for (int i = 0; i < depth_; ++i) {
      out_ << "  ";
    }
  }

  struct Scope {
    int& depth;
    explicit Scope(int& d) : depth(d) {
      ++depth;
    }
    ~Scope() {
      --depth;
    }
    Scope(const Scope&) = delete;
    auto operator=(const Scope&) -> Scope& = delete;
    Scope(Scope&&) = delete;
    auto operator=(Scope&&) -> Scope& = delete;
  };

  // -----------------------------------------------------------------------
  // Import
  // -----------------------------------------------------------------------

  void print_import(const ImportNode& node) {
    indent();
    out_ << "Import ";
    print_qualified_path(node.path());
    out_ << "\n";
  }

  // -----------------------------------------------------------------------
  // Declarations
  // -----------------------------------------------------------------------

  void print_decl(const Decl& decl) {
    switch (decl.kind()) {
    case NodeKind::FunctionDecl:
      print_function_decl(static_cast<const FunctionDeclNode&>(decl));
      break;
    case NodeKind::ClassDecl:
      print_class_decl(static_cast<const ClassDeclNode&>(decl));
      break;
    case NodeKind::AliasDecl:
      print_alias_decl(static_cast<const AliasDeclNode&>(decl));
      break;
    default:
      indent();
      out_ << "UnknownDecl\n";
      break;
    }
  }

  void print_function_decl(const FunctionDeclNode& fn) {
    indent();
    if (fn.is_extern()) {
      out_ << "ExternFunctionDecl " << fn.name() << "\n";
    } else {
      out_ << "FunctionDecl " << fn.name() << "\n";
    }
    Scope scope(depth_);

    for (const auto& param : fn.params()) {
      indent();
      out_ << "Param " << param.name << ": ";
      print_type_inline(*param.type);
      out_ << "\n";
    }

    if (fn.return_type() != nullptr) {
      indent();
      out_ << "ReturnType: ";
      print_type_inline(*fn.return_type());
      out_ << "\n";
    }

    if (fn.is_expr_bodied()) {
      indent();
      out_ << "ExprBody\n";
      {
        Scope body_scope(depth_);
        print_expr(*fn.expr_body());
      }
    } else {
      for (const auto* stmt : fn.body()) {
        print_stmt(*stmt);
      }
    }
  }

  void print_class_decl(const ClassDeclNode& node) {
    indent();
    out_ << "ClassDecl " << node.name() << "\n";
    Scope scope(depth_);
    for (const auto* field : node.fields()) {
      indent();
      out_ << "Field " << field->name() << ": ";
      print_type_inline(*field->type());
      out_ << "\n";
    }
  }

  void print_alias_decl(const AliasDeclNode& node) {
    indent();
    out_ << "AliasDecl " << node.name() << " = ";
    print_type_inline(*node.type());
    out_ << "\n";
  }

  // -----------------------------------------------------------------------
  // Statements
  // -----------------------------------------------------------------------

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void print_stmt(const Stmt& stmt) {
    switch (stmt.kind()) {
    case NodeKind::LetStatement:
      print_let(static_cast<const LetStatementNode&>(stmt));
      break;
    case NodeKind::Assignment:
      print_assignment(static_cast<const AssignmentNode&>(stmt));
      break;
    case NodeKind::IfStatement:
      print_if(static_cast<const IfStatementNode&>(stmt));
      break;
    case NodeKind::WhileStatement:
      print_while(static_cast<const WhileStatementNode&>(stmt));
      break;
    case NodeKind::ForStatement:
      print_for(static_cast<const ForStatementNode&>(stmt));
      break;
    case NodeKind::ModeBlock:
      print_mode(static_cast<const ModeBlockNode&>(stmt));
      break;
    case NodeKind::ResourceBlock:
      print_resource(static_cast<const ResourceBlockNode&>(stmt));
      break;
    case NodeKind::ReturnStatement:
      print_return(static_cast<const ReturnStatementNode&>(stmt));
      break;
    case NodeKind::ExpressionStatement:
      print_expr_stmt(static_cast<const ExpressionStatementNode&>(stmt));
      break;
    default:
      indent();
      out_ << "UnknownStmt\n";
      break;
    }
  }

  void print_let(const LetStatementNode& node) {
    indent();
    out_ << "LetStatement " << node.name();
    if (node.type() != nullptr) {
      out_ << ": ";
      print_type_inline(*node.type());
    }
    out_ << "\n";
    if (node.initializer() != nullptr) {
      Scope scope(depth_);
      print_expr(*node.initializer());
    }
  }

  void print_assignment(const AssignmentNode& node) {
    indent();
    out_ << "Assignment\n";
    Scope scope(depth_);
    indent();
    out_ << "Target\n";
    {
      Scope target_scope(depth_);
      print_expr(*node.target());
    }
    indent();
    out_ << "Value\n";
    {
      Scope value_scope(depth_);
      print_expr(*node.value());
    }
  }

  void print_if(const IfStatementNode& node) {
    indent();
    out_ << "IfStatement\n";
    Scope scope(depth_);
    indent();
    out_ << "Condition\n";
    {
      Scope cond_scope(depth_);
      print_expr(*node.condition());
    }
    indent();
    out_ << "Then\n";
    {
      Scope then_scope(depth_);
      for (const auto* stmt : node.then_body()) {
        print_stmt(*stmt);
      }
    }
    if (node.has_else()) {
      indent();
      out_ << "Else\n";
      Scope else_scope(depth_);
      for (const auto* stmt : node.else_body()) {
        print_stmt(*stmt);
      }
    }
  }

  void print_while(const WhileStatementNode& node) {
    indent();
    out_ << "WhileStatement\n";
    Scope scope(depth_);
    indent();
    out_ << "Condition\n";
    {
      Scope cond_scope(depth_);
      print_expr(*node.condition());
    }
    for (const auto* stmt : node.body()) {
      print_stmt(*stmt);
    }
  }

  void print_for(const ForStatementNode& node) {
    indent();
    out_ << "ForStatement " << node.var() << "\n";
    Scope scope(depth_);
    indent();
    out_ << "Iterable\n";
    {
      Scope iter_scope(depth_);
      print_expr(*node.iterable());
    }
    for (const auto* stmt : node.body()) {
      print_stmt(*stmt);
    }
  }

  void print_mode(const ModeBlockNode& node) {
    indent();
    out_ << "ModeBlock " << node.mode_name() << "\n";
    Scope scope(depth_);
    for (const auto* stmt : node.body()) {
      print_stmt(*stmt);
    }
  }

  void print_resource(const ResourceBlockNode& node) {
    indent();
    out_ << "ResourceBlock " << node.resource_kind() << " " << node.resource_name() << "\n";
    Scope scope(depth_);
    for (const auto* stmt : node.body()) {
      print_stmt(*stmt);
    }
  }

  void print_return(const ReturnStatementNode& node) {
    indent();
    out_ << "ReturnStatement\n";
    if (node.value() != nullptr) {
      Scope scope(depth_);
      print_expr(*node.value());
    }
  }

  void print_expr_stmt(const ExpressionStatementNode& node) {
    indent();
    out_ << "ExpressionStatement\n";
    Scope scope(depth_);
    print_expr(*node.expr());
  }

  // -----------------------------------------------------------------------
  // Expressions
  // -----------------------------------------------------------------------

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void print_expr(const Expr& expr) {
    switch (expr.kind()) {
    case NodeKind::IntLiteral:
      print_int_literal(static_cast<const IntLiteralNode&>(expr));
      break;
    case NodeKind::FloatLiteral:
      print_float_literal(static_cast<const FloatLiteralNode&>(expr));
      break;
    case NodeKind::StringLiteral:
      print_string_literal(static_cast<const StringLiteralNode&>(expr));
      break;
    case NodeKind::BoolLiteral:
      print_bool_literal(static_cast<const BoolLiteralNode&>(expr));
      break;
    case NodeKind::Identifier:
      print_identifier(static_cast<const IdentifierNode&>(expr));
      break;
    case NodeKind::QualifiedName:
      print_qualified_name(static_cast<const QualifiedNameNode&>(expr));
      break;
    case NodeKind::BinaryExpr:
      print_binary(static_cast<const BinaryExprNode&>(expr));
      break;
    case NodeKind::UnaryExpr:
      print_unary(static_cast<const UnaryExprNode&>(expr));
      break;
    case NodeKind::CallExpr:
      print_call(static_cast<const CallExprNode&>(expr));
      break;
    case NodeKind::IndexExpr:
      print_index(static_cast<const IndexExprNode&>(expr));
      break;
    case NodeKind::FieldExpr:
      print_field(static_cast<const FieldExprNode&>(expr));
      break;
    case NodeKind::PipeExpr:
      print_pipe(static_cast<const PipeExprNode&>(expr));
      break;
    case NodeKind::Lambda:
      print_lambda(static_cast<const LambdaNode&>(expr));
      break;
    case NodeKind::ListLiteral:
      print_list_literal(static_cast<const ListLiteralNode&>(expr));
      break;
    default:
      indent();
      out_ << "UnknownExpr\n";
      break;
    }
  }

  void print_int_literal(const IntLiteralNode& node) {
    indent();
    out_ << "IntLiteral " << node.text() << "\n";
  }

  void print_float_literal(const FloatLiteralNode& node) {
    indent();
    out_ << "FloatLiteral " << node.text() << "\n";
  }

  void print_string_literal(const StringLiteralNode& node) {
    indent();
    out_ << "StringLiteral " << node.text() << "\n";
  }

  void print_bool_literal(const BoolLiteralNode& node) {
    indent();
    out_ << "BoolLiteral " << (node.value() ? "true" : "false") << "\n";
  }

  void print_identifier(const IdentifierNode& node) {
    indent();
    out_ << "Identifier " << node.name() << "\n";
  }

  void print_qualified_name(const QualifiedNameNode& node) {
    indent();
    out_ << "QualifiedName ";
    for (size_t i = 0; i < node.segments().size(); ++i) {
      if (i > 0) {
        out_ << "::";
      }
      out_ << node.segments()[i];
    }
    out_ << "\n";
  }

  static auto binary_op_str(BinaryOp op) -> const char* {
    switch (op) {
    case BinaryOp::Add:
      return "+";
    case BinaryOp::Sub:
      return "-";
    case BinaryOp::Mul:
      return "*";
    case BinaryOp::Div:
      return "/";
    case BinaryOp::Mod:
      return "%";
    case BinaryOp::EqEq:
      return "==";
    case BinaryOp::BangEq:
      return "!=";
    case BinaryOp::Lt:
      return "<";
    case BinaryOp::LtEq:
      return "<=";
    case BinaryOp::Gt:
      return ">";
    case BinaryOp::GtEq:
      return ">=";
    case BinaryOp::And:
      return "and";
    case BinaryOp::Or:
      return "or";
    }
    return "?";
  }

  static auto unary_op_str(UnaryOp op) -> const char* {
    switch (op) {
    case UnaryOp::Negate:
      return "-";
    case UnaryOp::Not:
      return "!";
    case UnaryOp::Deref:
      return "*";
    case UnaryOp::AddrOf:
      return "&";
    }
    return "?";
  }

  void print_binary(const BinaryExprNode& node) {
    indent();
    out_ << "BinaryExpr " << binary_op_str(node.op()) << "\n";
    Scope scope(depth_);
    print_expr(*node.left());
    print_expr(*node.right());
  }

  void print_unary(const UnaryExprNode& node) {
    indent();
    out_ << "UnaryExpr " << unary_op_str(node.op()) << "\n";
    Scope scope(depth_);
    print_expr(*node.operand());
  }

  void print_call(const CallExprNode& node) {
    indent();
    out_ << "CallExpr\n";
    Scope scope(depth_);
    indent();
    out_ << "Callee\n";
    {
      Scope callee_scope(depth_);
      print_expr(*node.callee());
    }
    if (!node.args().empty()) {
      indent();
      out_ << "Args\n";
      Scope args_scope(depth_);
      for (const auto* arg : node.args()) {
        print_expr(*arg);
      }
    }
  }

  void print_index(const IndexExprNode& node) {
    indent();
    out_ << "IndexExpr\n";
    Scope scope(depth_);
    print_expr(*node.object());
    indent();
    out_ << "Indices\n";
    {
      Scope indices_scope(depth_);
      for (const auto* idx : node.indices()) {
        print_expr(*idx);
      }
    }
  }

  void print_field(const FieldExprNode& node) {
    indent();
    out_ << "FieldExpr ." << node.field() << "\n";
    Scope scope(depth_);
    print_expr(*node.object());
  }

  void print_pipe(const PipeExprNode& node) {
    indent();
    out_ << "PipeExpr\n";
    Scope scope(depth_);
    print_expr(*node.left());
    print_expr(*node.right());
  }

  void print_lambda(const LambdaNode& node) {
    indent();
    out_ << "Lambda";
    for (const auto& [name, span] : node.params()) {
      out_ << " " << name;
    }
    out_ << "\n";
    Scope scope(depth_);
    print_expr(*node.body());
  }

  void print_list_literal(const ListLiteralNode& node) {
    indent();
    out_ << "ListLiteral\n";
    if (!node.elements().empty()) {
      Scope scope(depth_);
      for (const auto* elem : node.elements()) {
        print_expr(*elem);
      }
    }
  }

  // -----------------------------------------------------------------------
  // Types (inline rendering)
  // -----------------------------------------------------------------------

  void print_type_inline(const TypeNode& type) {
    switch (type.kind()) {
    case NodeKind::NamedType:
      print_named_type_inline(static_cast<const NamedTypeNode&>(type));
      break;
    case NodeKind::PointerType:
      print_pointer_type_inline(static_cast<const PointerTypeNode&>(type));
      break;
    default:
      out_ << "?";
      break;
    }
  }

  void print_named_type_inline(const NamedTypeNode& type) {
    print_qualified_path(type.name());
    if (!type.type_args().empty()) {
      out_ << "[";
      for (size_t i = 0; i < type.type_args().size(); ++i) {
        if (i > 0) {
          out_ << ", ";
        }
        print_type_inline(*type.type_args()[i]);
      }
      out_ << "]";
    }
  }

  void print_pointer_type_inline(const PointerTypeNode& type) {
    out_ << "*";
    print_type_inline(*type.pointee());
  }

  void print_qualified_path(const QualifiedPath& path) {
    for (size_t i = 0; i < path.segments.size(); ++i) {
      if (i > 0) {
        out_ << "::";
      }
      out_ << path.segments[i];
    }
  }
};

// NOLINTEND(readability-identifier-length)

} // namespace

void print_ast(std::ostream& out, const FileNode& file) {
  AstPrinter printer(out);
  printer.print(file);
}

} // namespace dao
