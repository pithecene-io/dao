#include "ir/mir/mir_kind.h"

namespace dao {

auto mir_inst_kind_name(MirInstKind kind) -> const char* {
  switch (kind) {
  case MirInstKind::ConstInt:       return "const_int";
  case MirInstKind::ConstFloat:     return "const_float";
  case MirInstKind::ConstBool:      return "const_bool";
  case MirInstKind::ConstString:    return "const_string";
  case MirInstKind::Unary:          return "unary";
  case MirInstKind::Binary:         return "binary";
  case MirInstKind::Store:          return "store";
  case MirInstKind::Load:           return "load";
  case MirInstKind::AddrOf:         return "addr_of";
  case MirInstKind::FieldAccess:    return "field";
  case MirInstKind::IndexAccess:    return "index";
  case MirInstKind::FnRef:          return "fn_ref";
  case MirInstKind::Call:           return "call";
  case MirInstKind::IterInit:       return "iter_init";
  case MirInstKind::IterHasNext:    return "iter_has_next";
  case MirInstKind::IterNext:       return "iter_next";
  case MirInstKind::ModeEnter:      return "mode_enter";
  case MirInstKind::ModeExit:       return "mode_exit";
  case MirInstKind::ResourceEnter:  return "resource_enter";
  case MirInstKind::ResourceExit:   return "resource_exit";
  case MirInstKind::Lambda:         return "lambda";
  case MirInstKind::Br:             return "br";
  case MirInstKind::CondBr:         return "cond_br";
  case MirInstKind::Return:         return "return";
  }
  return "<unknown>";
}

auto is_terminator(MirInstKind kind) -> bool {
  switch (kind) {
  case MirInstKind::Br:
  case MirInstKind::CondBr:
  case MirInstKind::Return:
    return true;
  default:
    return false;
  }
}

} // namespace dao
