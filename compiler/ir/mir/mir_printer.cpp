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
    out_ << "fn ";
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

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void print_inst(const MirInst& inst) {
    // Print result assignment if value-producing.
    if (inst.result.valid() && !is_terminator(inst.kind)) {
      out_ << "%" << inst.result.id << " = ";
    }

    switch (inst.kind) {
    case MirInstKind::ConstInt:
      out_ << "const_int " << inst.const_int;
      break;
    case MirInstKind::ConstFloat:
      out_ << "const_float " << inst.const_float;
      break;
    case MirInstKind::ConstBool:
      out_ << "const_bool " << (inst.const_bool ? "true" : "false");
      break;
    case MirInstKind::ConstString:
      out_ << "const_string " << inst.const_string;
      break;

    case MirInstKind::Unary:
      out_ << "unary " << unary_op_str(inst.unary_op)
           << " %" << inst.operand.id;
      break;
    case MirInstKind::Binary:
      out_ << "binary " << binary_op_str(inst.binary_op)
           << " %" << inst.lhs.id << ", %" << inst.rhs.id;
      break;

    case MirInstKind::Store:
      out_ << "store ";
      print_place(inst.place);
      out_ << ", %" << inst.store_value.id;
      break;
    case MirInstKind::Load:
      out_ << "load ";
      print_place(inst.place);
      break;
    case MirInstKind::AddrOf:
      out_ << "addr_of ";
      print_place(inst.place);
      break;

    case MirInstKind::FieldAccess:
      out_ << "field %" << inst.access_object.id
           << "." << inst.access_field;
      break;
    case MirInstKind::IndexAccess:
      out_ << "index %" << inst.access_object.id
           << "[%" << inst.access_index.id << "]";
      break;

    case MirInstKind::Call:
      out_ << "call %" << inst.callee.id << "(";
      if (inst.call_args != nullptr) {
        for (size_t i = 0; i < inst.call_args->size(); ++i) {
          if (i > 0) {
            out_ << ", ";
          }
          out_ << "%" << (*inst.call_args)[i].id;
        }
      }
      out_ << ")";
      break;

    case MirInstKind::IterInit:
      out_ << "iter_init %" << inst.iter_operand.id;
      break;
    case MirInstKind::IterHasNext:
      out_ << "iter_has_next %" << inst.iter_operand.id;
      break;
    case MirInstKind::IterNext:
      out_ << "iter_next %" << inst.iter_operand.id;
      break;

    case MirInstKind::ModeEnter:
      out_ << "mode_enter " << inst.region_name;
      break;
    case MirInstKind::ModeExit:
      out_ << "mode_exit " << hir_mode_kind_name(inst.mode_kind);
      break;
    case MirInstKind::ResourceEnter:
      out_ << "resource_enter " << inst.region_kind
           << " " << inst.region_name;
      break;
    case MirInstKind::ResourceExit:
      out_ << "resource_exit";
      break;

    case MirInstKind::Lambda:
      out_ << "lambda";
      if (inst.lambda_fn != nullptr && inst.lambda_fn->symbol != nullptr) {
        out_ << " " << inst.lambda_fn->symbol->name;
      }
      break;

    case MirInstKind::Br:
      out_ << "br bb" << inst.br_target.id;
      break;
    case MirInstKind::CondBr:
      out_ << "cond_br %" << inst.cond.id
           << ", bb" << inst.then_block.id
           << ", bb" << inst.else_block.id;
      break;
    case MirInstKind::Return:
      out_ << "return";
      if (inst.has_return_value) {
        out_ << " %" << inst.return_value.id;
      }
      break;
    }

    // Print type annotation for value-producing instructions.
    if (inst.type != nullptr && !is_terminator(inst.kind)) {
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
