#include "ir/hir/hir_printer.h"

#include "frontend/types/type_printer.h"
#include "support/variant.h"

#include <string>

namespace dao {

namespace {

// NOLINTBEGIN(readability-identifier-length)

class HirPrinter {
public:
  explicit HirPrinter(std::ostream& out) : out_(out) {}

  void print(const HirModule& module) {
    out_ << "Module\n";
    for (const auto* decl : module.declarations) {
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
    std::visit(overloaded{
        [&](const HirFunction& fn) { print_function(fn); },
        [&](const HirClassDecl& cls) { print_class(cls); },
    }, decl.payload);
  }

  void print_function(const HirFunction& fn) {
    indent();
    out_ << (fn.is_extern ? "ExternFunction " : "Function ");
    print_symbol_name(fn.symbol);
    print_type_annotation(fn.return_type);
    out_ << "\n";
    Scope scope(depth_);

    for (const auto& param : fn.params) {
      indent();
      out_ << "Param ";
      print_symbol_name(param.symbol);
      print_type_annotation(param.type);
      out_ << "\n";
    }

    for (const auto* stmt : fn.body) {
      print_stmt(*stmt);
    }
  }

  void print_class(const HirClassDecl& cls) {
    indent();
    out_ << "ClassDecl ";
    print_symbol_name(cls.symbol);
    if (cls.struct_type != nullptr) {
      out_ << " : " << print_type(cls.struct_type);
    }
    out_ << "\n";
  }

  // --- Statements ---

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void print_stmt(const HirStmt& stmt) {
    std::visit(overloaded{
        [&](const HirLet& node) {
          indent();
          out_ << "Let ";
          print_symbol_name(node.symbol);
          print_type_annotation(node.type);
          out_ << "\n";
          if (node.initializer != nullptr) {
            Scope scope(depth_);
            print_expr(*node.initializer);
          }
        },
        [&](const HirAssign& node) {
          indent();
          out_ << "Assign\n";
          Scope scope(depth_);
          indent();
          out_ << "Target\n";
          {
            Scope target_scope(depth_);
            print_expr(*node.target);
          }
          indent();
          out_ << "Value\n";
          {
            Scope value_scope(depth_);
            print_expr(*node.value);
          }
        },
        [&](const HirIf& node) {
          indent();
          out_ << "If\n";
          Scope scope(depth_);
          indent();
          out_ << "Condition\n";
          {
            Scope cond_scope(depth_);
            print_expr(*node.condition);
          }
          indent();
          out_ << "Then\n";
          {
            Scope then_scope(depth_);
            for (const auto* s : node.then_body) {
              print_stmt(*s);
            }
          }
          if (!node.else_body.empty()) {
            indent();
            out_ << "Else\n";
            Scope else_scope(depth_);
            for (const auto* s : node.else_body) {
              print_stmt(*s);
            }
          }
        },
        [&](const HirWhile& node) {
          indent();
          out_ << "While\n";
          Scope scope(depth_);
          indent();
          out_ << "Condition\n";
          {
            Scope cond_scope(depth_);
            print_expr(*node.condition);
          }
          for (const auto* s : node.body) {
            print_stmt(*s);
          }
        },
        [&](const HirFor& node) {
          indent();
          out_ << "For ";
          print_symbol_name(node.var_symbol);
          out_ << "\n";
          Scope scope(depth_);
          indent();
          out_ << "Iterable\n";
          {
            Scope iter_scope(depth_);
            print_expr(*node.iterable);
          }
          for (const auto* s : node.body) {
            print_stmt(*s);
          }
        },
        [&](const HirReturn& node) {
          indent();
          out_ << "Return\n";
          if (node.value != nullptr) {
            Scope scope(depth_);
            print_expr(*node.value);
          }
        },
        [&](const HirYield& node) {
          indent();
          out_ << "Yield\n";
          Scope scope(depth_);
          print_expr(*node.value);
        },
        [&](const HirBreak&) {
          indent();
          out_ << "Break\n";
        },
        [&](const HirExprStmt& node) {
          indent();
          out_ << "ExprStmt\n";
          Scope scope(depth_);
          print_expr(*node.expr);
        },
        [&](const HirMode& node) {
          indent();
          out_ << "Mode " << node.mode_name << "\n";
          Scope scope(depth_);
          for (const auto* s : node.body) {
            print_stmt(*s);
          }
        },
        [&](const HirResource& node) {
          indent();
          out_ << "Resource " << node.resource_kind << " "
               << node.resource_name << "\n";
          Scope scope(depth_);
          for (const auto* s : node.body) {
            print_stmt(*s);
          }
        },
    }, stmt.payload);
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
    std::visit(overloaded{
        [&](const HirIntLiteral& node) {
          indent();
          out_ << "IntLiteral " << node.value;
          print_type_annotation(expr.type);
          out_ << "\n";
        },
        [&](const HirFloatLiteral& node) {
          indent();
          out_ << "FloatLiteral " << node.value;
          print_type_annotation(expr.type);
          out_ << "\n";
        },
        [&](const HirStringLiteral& node) {
          indent();
          out_ << "StringLiteral " << node.value;
          print_type_annotation(expr.type);
          out_ << "\n";
        },
        [&](const HirBoolLiteral& node) {
          indent();
          out_ << "BoolLiteral " << (node.value ? "true" : "false");
          print_type_annotation(expr.type);
          out_ << "\n";
        },
        [&](const HirSymbolRef& node) {
          indent();
          out_ << "SymbolRef ";
          print_symbol_name(node.symbol);
          print_type_annotation(expr.type);
          out_ << "\n";
        },
        [&](const HirUnary& node) {
          indent();
          out_ << "Unary " << unary_op_str(node.op);
          print_type_annotation(expr.type);
          out_ << "\n";
          Scope scope(depth_);
          print_expr(*node.operand);
        },
        [&](const HirBinary& node) {
          indent();
          out_ << "Binary " << binary_op_str(node.op);
          print_type_annotation(expr.type);
          out_ << "\n";
          Scope scope(depth_);
          print_expr(*node.left);
          print_expr(*node.right);
        },
        [&](const HirCall& node) {
          indent();
          out_ << "Call";
          print_type_annotation(expr.type);
          out_ << "\n";
          Scope scope(depth_);
          indent();
          out_ << "Callee\n";
          {
            Scope callee_scope(depth_);
            print_expr(*node.callee);
          }
          if (!node.args.empty()) {
            indent();
            out_ << "Args\n";
            Scope args_scope(depth_);
            for (const auto* arg : node.args) {
              print_expr(*arg);
            }
          }
        },
        [&](const HirConstruct& node) {
          indent();
          out_ << "Construct ";
          if (node.struct_type != nullptr) {
            out_ << node.struct_type->name();
          }
          print_type_annotation(expr.type);
          out_ << "\n";
          if (!node.args.empty()) {
            Scope scope(depth_);
            indent();
            out_ << "Args\n";
            Scope args_scope(depth_);
            for (const auto* arg : node.args) {
              print_expr(*arg);
            }
          }
        },
        [&](const HirField& node) {
          indent();
          out_ << "Field ." << node.field_name;
          print_type_annotation(expr.type);
          out_ << "\n";
          Scope scope(depth_);
          print_expr(*node.object);
        },
        [&](const HirIndex& node) {
          indent();
          out_ << "Index";
          print_type_annotation(expr.type);
          out_ << "\n";
          Scope scope(depth_);
          print_expr(*node.object);
          indent();
          out_ << "Indices\n";
          {
            Scope indices_scope(depth_);
            for (const auto* idx : node.indices) {
              print_expr(*idx);
            }
          }
        },
        [&](const HirPipe& node) {
          indent();
          out_ << "Pipe";
          print_type_annotation(expr.type);
          out_ << "\n";
          Scope scope(depth_);
          print_expr(*node.left);
          print_expr(*node.right);
        },
        [&](const HirLambda& node) {
          indent();
          out_ << "Lambda";
          print_type_annotation(expr.type);
          out_ << "\n";
          Scope scope(depth_);
          for (const auto& param : node.params) {
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
            print_expr(*node.body);
          }
        },
    }, expr.payload);
  }
};

// NOLINTEND(readability-identifier-length)

} // namespace

void print_hir(std::ostream& out, const HirModule& module) {
  HirPrinter printer(out);
  printer.print(module);
}

} // namespace dao
