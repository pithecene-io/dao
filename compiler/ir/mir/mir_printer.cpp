#include "ir/mir/mir_printer.h"

#include "frontend/types/type_printer.h"

namespace dao {

namespace {

// NOLINTBEGIN(readability-identifier-length)

class MirPrinter {
public:
  explicit MirPrinter(std::ostream& out) : out_(out) {}

  void print(const MirModule& module) {
    for (size_t i = 0; i < module.functions.size(); ++i) {
      if (i > 0) {
        out_ << "\n";
      }
      print_function(*module.functions[i]);
    }
  }

private:
  std::ostream& out_;

  void print_function(const MirFunction& fn) {
    out_ << (fn.is_extern ? "extern fn " : "fn ");
    if (fn.symbol != nullptr) {
      out_ << fn.symbol->name;
    } else {
      out_ << "<lambda>";
    }
    out_ << "(";
    bool first = true;
    for (const auto& local : fn.locals) {
      if (!local.is_param) {
        continue;
      }
      if (!first) {
        out_ << ", ";
      }
      first = false;
      print_local_name(local);
      print_type_annotation(local.type);
    }
    out_ << ")";
    print_type_annotation(fn.return_type);
    out_ << ":\n";

    // Print non-param locals.
    for (const auto& local : fn.locals) {
      if (local.is_param) {
        continue;
      }
      out_ << "  local ";
      print_local_name(local);
      print_type_annotation(local.type);
      out_ << "\n";
    }

    for (const auto* block : fn.blocks) {
      print_block(*block);
    }
  }

  void print_block(const MirBlock& block) {
    out_ << "  bb" << block.id.id << ":\n";
    for (const auto* inst : block.insts) {
      out_ << "    ";
      print_inst(*inst);
      out_ << "\n";
    }
  }

  void print_inst(const MirInst& inst) {
    // Print result assignment if value-producing.
    if (inst.result.valid() && !is_terminator(inst.kind())) {
      out_ << "%" << inst.result.id << " = ";
    }

    std::visit(overloaded{
        [&](const MirConstInt& p) {
          out_ << "const_int " << p.value;
        },
        [&](const MirConstFloat& p) {
          out_ << "const_float " << p.value;
        },
        [&](const MirConstBool& p) {
          out_ << "const_bool " << (p.value ? "true" : "false");
        },
        [&](const MirConstString& p) {
          out_ << "const_string " << p.value;
        },
        [&](const MirUnary& p) {
          out_ << "unary " << unary_op_str(p.op)
               << " %" << p.operand.id;
        },
        [&](const MirBinary& p) {
          out_ << "binary " << binary_op_str(p.op)
               << " %" << p.lhs.id << ", %" << p.rhs.id;
        },
        [&](const MirStore& p) {
          out_ << "store ";
          print_place(p.place);
          out_ << ", %" << p.value.id;
        },
        [&](const MirLoad& p) {
          out_ << "load ";
          print_place(p.place);
        },
        [&](const MirAddrOf& p) {
          out_ << "addr_of ";
          print_place(p.place);
        },
        [&](const MirFieldAccess& p) {
          out_ << "field %" << p.object.id << "." << p.field;
        },
        [&](const MirIndexAccess& p) {
          out_ << "index %" << p.object.id
               << "[%" << p.index.id << "]";
        },
        [&](const MirFnRef& p) {
          out_ << "fn_ref";
          if (p.symbol != nullptr) {
            out_ << " " << p.symbol->name;
          }
        },
        [&](const MirCall& p) {
          out_ << "call %" << p.callee.id << "(";
          if (p.args != nullptr) {
            for (size_t i = 0; i < p.args->size(); ++i) {
              if (i > 0) {
                out_ << ", ";
              }
              out_ << "%" << (*p.args)[i].id;
            }
          }
          out_ << ")";
        },
        [&](const MirConstruct& p) {
          out_ << "construct ";
          if (p.struct_type != nullptr) {
            out_ << p.struct_type->name();
          }
          out_ << "(";
          if (p.field_values != nullptr) {
            for (size_t i = 0; i < p.field_values->size(); ++i) {
              if (i > 0) {
                out_ << ", ";
              }
              out_ << "%" << (*p.field_values)[i].id;
            }
          }
          out_ << ")";
        },
        [&](const MirIterInit& p) {
          out_ << "iter_init %" << p.iter_operand.id;
        },
        [&](const MirIterHasNext& p) {
          out_ << "iter_has_next %" << p.iter_operand.id;
        },
        [&](const MirIterNext& p) {
          out_ << "iter_next %" << p.iter_operand.id;
        },
        [&](const MirIterDestroy& p) {
          out_ << "iter_destroy %" << p.iter_operand.id;
        },
        [&](const MirYieldInst& p) {
          out_ << "yield %" << p.value.id;
        },
        [&](const MirModeEnter& p) {
          out_ << "mode_enter " << p.region_name;
        },
        [&](const MirModeExit& p) {
          out_ << "mode_exit " << hir_mode_kind_name(p.mode_kind);
        },
        [&](const MirResourceEnter& p) {
          out_ << "resource_enter " << p.region_kind
               << " " << p.region_name;
        },
        [&](const MirResourceExit&) {
          out_ << "resource_exit";
        },
        [&](const MirLambdaInst& p) {
          out_ << "lambda";
          if (p.fn != nullptr && p.fn->symbol != nullptr) {
            out_ << " " << p.fn->symbol->name;
          }
        },
        [&](const MirBr& p) {
          out_ << "br bb" << p.target.id;
        },
        [&](const MirCondBr& p) {
          out_ << "cond_br %" << p.cond.id
               << ", bb" << p.then_block.id
               << ", bb" << p.else_block.id;
        },
        [&](const MirReturn& p) {
          out_ << "return";
          if (p.has_value) {
            out_ << " %" << p.value.id;
          }
        },
    }, inst.payload);

    // Print type annotation for value-producing instructions.
    if (inst.type != nullptr && !is_terminator(inst.kind())) {
      out_ << " : " << print_type(inst.type);
    }
  }

  void print_place(const MirPlace* place) {
    if (place == nullptr) {
      out_ << "<null_place>";
      return;
    }
    out_ << "_" << place->local.id;
    for (const auto& proj : place->projections) {
      switch (proj.kind) {
      case MirProjectionKind::Field:
        out_ << "." << proj.field_name;
        break;
      case MirProjectionKind::Index:
        out_ << "[%" << proj.index_value.id << "]";
        break;
      case MirProjectionKind::Deref:
        out_ << ".*";
        break;
      }
    }
  }

  void print_local_name(const MirLocal& local) {
    out_ << "_" << local.id.id;
    if (local.symbol != nullptr) {
      out_ << "(" << local.symbol->name << ")";
    }
  }

  void print_type_annotation(const Type* type) {
    if (type != nullptr) {
      out_ << " : " << print_type(type);
    }
  }

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
};

// NOLINTEND(readability-identifier-length)

} // namespace

void print_mir(std::ostream& out, const MirModule& module) {
  MirPrinter printer(out);
  printer.print(module);
}

} // namespace dao
