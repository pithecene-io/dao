#include "ir/hir/hir_printer.h"

#include "frontend/types/type_printer.h"

#include <string>

namespace dao {

namespace {

// NOLINTBEGIN(readability-identifier-length)

class HirPrinter {
public:
  explicit HirPrinter(std::ostream& out) : out_(out) {}

  void print(const HirModule& module) {
    out_ << "Module\n";
    for (const auto* decl : module.declarations()) {
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
    explicit Scope(int& d) : depth(d) { ++depth; }
    ~Scope() { --depth; }
    Scope(const Scope&) = delete;
    auto operator=(const Scope&) -> Scope& = delete;
    Scope(Scope&&) = delete;
    auto operator=(Scope&&) -> Scope& = delete;
  };

  // --- Type annotation ---

  void print_type_annotation(const Type* type) {
    if (type != nullptr) {
      out_ << " : " << print_type(type);
    }
  }

  // --- Symbol name ---

  void print_symbol_name(const Symbol* sym) {
    if (sym != nullptr) {
      out_ << sym->name;
    } else {
      out_ << "<unknown>";
    }
  }

  // --- Declarations ---

  void print_decl(const HirDecl& decl) {
    switch (decl.kind()) {
    case HirKind::Function:
      print_function(static_cast<const HirFunction&>(decl));
      break;
    case HirKind::StructDecl:
      print_struct(static_cast<const HirStructDecl&>(decl));
      break;
    default:
      indent();
      out_ << "UnknownDecl\n";
      break;
    }
  }

  void print_function(const HirFunction& fn) {
    indent();
    out_ << (fn.is_extern() ? "ExternFunction " : "Function ");
    print_symbol_name(fn.symbol());
    print_type_annotation(fn.return_type());
    out_ << "\n";
    Scope scope(depth_);

    for (const auto& param : fn.params()) {
      indent();
      out_ << "Param ";
      print_symbol_name(param.symbol);
      print_type_annotation(param.type);
      out_ << "\n";
    }

    for (const auto* stmt : fn.body()) {
      print_stmt(*stmt);
    }
  }

  void print_struct(const HirStructDecl& st) {
    indent();
    out_ << "StructDecl ";
    print_symbol_name(st.symbol());
    if (st.struct_type() != nullptr) {
      out_ << " : " << print_type(st.struct_type());
    }
    out_ << "\n";
  }

  // --- Statements ---

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void print_stmt(const HirStmt& stmt) {
    switch (stmt.kind()) {
    case HirKind::Let:
      print_let(static_cast<const HirLet&>(stmt));
      break;
    case HirKind::Assign:
      print_assign(static_cast<const HirAssign&>(stmt));
      break;
    case HirKind::If:
      print_if(static_cast<const HirIf&>(stmt));
      break;
    case HirKind::While:
      print_while(static_cast<const HirWhile&>(stmt));
      break;
    case HirKind::For:
      print_for(static_cast<const HirFor&>(stmt));
      break;
    case HirKind::Return:
      print_return(static_cast<const HirReturn&>(stmt));
      break;
    case HirKind::ExprStmt:
      print_expr_stmt(static_cast<const HirExprStmt&>(stmt));
      break;
    case HirKind::Mode:
      print_mode(static_cast<const HirMode&>(stmt));
      break;
    case HirKind::Resource:
      print_resource(static_cast<const HirResource&>(stmt));
      break;
    default:
      indent();
      out_ << "UnknownStmt\n";
      break;
    }
  }

  void print_let(const HirLet& node) {
    indent();
    out_ << "Let ";
    print_symbol_name(node.symbol());
    print_type_annotation(node.type());
    out_ << "\n";
    if (node.initializer() != nullptr) {
      Scope scope(depth_);
      print_expr(*node.initializer());
    }
  }

  void print_assign(const HirAssign& node) {
    indent();
    out_ << "Assign\n";
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

  void print_if(const HirIf& node) {
    indent();
    out_ << "If\n";
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
    if (!node.else_body().empty()) {
      indent();
      out_ << "Else\n";
      Scope else_scope(depth_);
      for (const auto* stmt : node.else_body()) {
        print_stmt(*stmt);
      }
    }
  }

  void print_while(const HirWhile& node) {
    indent();
    out_ << "While\n";
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

  void print_for(const HirFor& node) {
    indent();
    out_ << "For ";
    print_symbol_name(node.var_symbol());
    out_ << "\n";
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

  void print_return(const HirReturn& node) {
    indent();
    out_ << "Return\n";
    if (node.value() != nullptr) {
      Scope scope(depth_);
      print_expr(*node.value());
    }
  }

  void print_expr_stmt(const HirExprStmt& node) {
    indent();
    out_ << "ExprStmt\n";
    Scope scope(depth_);
    print_expr(*node.expr());
  }

  void print_mode(const HirMode& node) {
    indent();
    out_ << "Mode " << node.mode_name() << "\n";
    Scope scope(depth_);
    for (const auto* stmt : node.body()) {
      print_stmt(*stmt);
    }
  }

  void print_resource(const HirResource& node) {
    indent();
    out_ << "Resource " << node.resource_kind() << " " << node.resource_name()
         << "\n";
    Scope scope(depth_);
    for (const auto* stmt : node.body()) {
      print_stmt(*stmt);
    }
  }

  // --- Expressions ---

  static auto binary_op_str(BinaryOp op) -> const char* {
    switch (op) {
    case BinaryOp::Add:    return "+";
    case BinaryOp::Sub:    return "-";
    case BinaryOp::Mul:    return "*";
    case BinaryOp::Div:    return "/";
    case BinaryOp::Mod:    return "%";
    case BinaryOp::EqEq:   return "==";
    case BinaryOp::BangEq: return "!=";
    case BinaryOp::Lt:     return "<";
    case BinaryOp::LtEq:   return "<=";
    case BinaryOp::Gt:     return ">";
    case BinaryOp::GtEq:   return ">=";
    case BinaryOp::And:    return "and";
    case BinaryOp::Or:     return "or";
    }
    return "?";
  }

  static auto unary_op_str(UnaryOp op) -> const char* {
    switch (op) {
    case UnaryOp::Negate: return "-";
    case UnaryOp::Not:    return "!";
    case UnaryOp::Deref:  return "*";
    case UnaryOp::AddrOf: return "&";
    }
    return "?";
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void print_expr(const HirExpr& expr) {
    switch (expr.kind()) {
    case HirKind::IntLiteral:
      print_int_literal(static_cast<const HirIntLiteral&>(expr));
      break;
    case HirKind::FloatLiteral:
      print_float_literal(static_cast<const HirFloatLiteral&>(expr));
      break;
    case HirKind::StringLiteral:
      print_string_literal(static_cast<const HirStringLiteral&>(expr));
      break;
    case HirKind::BoolLiteral:
      print_bool_literal(static_cast<const HirBoolLiteral&>(expr));
      break;
    case HirKind::SymbolRef:
      print_symbol_ref(static_cast<const HirSymbolRef&>(expr));
      break;
    case HirKind::Unary:
      print_unary(static_cast<const HirUnary&>(expr));
      break;
    case HirKind::Binary:
      print_binary(static_cast<const HirBinary&>(expr));
      break;
    case HirKind::Call:
      print_call(static_cast<const HirCall&>(expr));
      break;
    case HirKind::Field:
      print_field(static_cast<const HirField&>(expr));
      break;
    case HirKind::Index:
      print_index(static_cast<const HirIndex&>(expr));
      break;
    case HirKind::Pipe:
      print_pipe(static_cast<const HirPipe&>(expr));
      break;
    case HirKind::Lambda:
      print_lambda(static_cast<const HirLambda&>(expr));
      break;
    default:
      indent();
      out_ << "UnknownExpr\n";
      break;
    }
  }

  void print_int_literal(const HirIntLiteral& node) {
    indent();
    out_ << "IntLiteral " << node.value();
    print_type_annotation(node.type());
    out_ << "\n";
  }

  void print_float_literal(const HirFloatLiteral& node) {
    indent();
    out_ << "FloatLiteral " << node.value();
    print_type_annotation(node.type());
    out_ << "\n";
  }

  void print_string_literal(const HirStringLiteral& node) {
    indent();
    out_ << "StringLiteral " << node.value();
    print_type_annotation(node.type());
    out_ << "\n";
  }

  void print_bool_literal(const HirBoolLiteral& node) {
    indent();
    out_ << "BoolLiteral " << (node.value() ? "true" : "false");
    print_type_annotation(node.type());
    out_ << "\n";
  }

  void print_symbol_ref(const HirSymbolRef& node) {
    indent();
    out_ << "SymbolRef ";
    print_symbol_name(node.symbol());
    print_type_annotation(node.type());
    out_ << "\n";
  }

  void print_unary(const HirUnary& node) {
    indent();
    out_ << "Unary " << unary_op_str(node.op());
    print_type_annotation(node.type());
    out_ << "\n";
    Scope scope(depth_);
    print_expr(*node.operand());
  }

  void print_binary(const HirBinary& node) {
    indent();
    out_ << "Binary " << binary_op_str(node.op());
    print_type_annotation(node.type());
    out_ << "\n";
    Scope scope(depth_);
    print_expr(*node.left());
    print_expr(*node.right());
  }

  void print_call(const HirCall& node) {
    indent();
    out_ << "Call";
    print_type_annotation(node.type());
    out_ << "\n";
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

  void print_field(const HirField& node) {
    indent();
    out_ << "Field ." << node.field_name();
    print_type_annotation(node.type());
    out_ << "\n";
    Scope scope(depth_);
    print_expr(*node.object());
  }

  void print_index(const HirIndex& node) {
    indent();
    out_ << "Index";
    print_type_annotation(node.type());
    out_ << "\n";
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

  void print_pipe(const HirPipe& node) {
    indent();
    out_ << "Pipe";
    print_type_annotation(node.type());
    out_ << "\n";
    Scope scope(depth_);
    print_expr(*node.left());
    print_expr(*node.right());
  }

  void print_lambda(const HirLambda& node) {
    indent();
    out_ << "Lambda";
    print_type_annotation(node.type());
    out_ << "\n";
    Scope scope(depth_);
    for (const auto& param : node.params()) {
      indent();
      out_ << "Param ";
      print_symbol_name(param.symbol);
      print_type_annotation(param.type);
      out_ << "\n";
    }
    indent();
    out_ << "Body\n";
    {
      Scope body_scope(depth_);
      print_expr(*node.body());
    }
  }
};

// NOLINTEND(readability-identifier-length)

} // namespace

void print_hir(std::ostream& out, const HirModule& module) {
  HirPrinter printer(out);
  printer.print(module);
}

} // namespace dao
