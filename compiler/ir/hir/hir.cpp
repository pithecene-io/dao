#include "ir/hir/hir.h"
#include "ir/hir/hir_kind.h"

namespace dao {

auto hir_kind_name(HirKind kind) -> const char* {
  switch (kind) {
  case HirKind::Module:        return "Module";
  case HirKind::Function:      return "Function";
  case HirKind::ClassDecl:     return "ClassDecl";
  case HirKind::Let:           return "Let";
  case HirKind::Assign:        return "Assign";
  case HirKind::If:            return "If";
  case HirKind::While:         return "While";
  case HirKind::For:           return "For";
  case HirKind::Return:        return "Return";
  case HirKind::ExprStmt:      return "ExprStmt";
  case HirKind::Mode:          return "Mode";
  case HirKind::Resource:      return "Resource";
  case HirKind::IntLiteral:    return "IntLiteral";
  case HirKind::FloatLiteral:  return "FloatLiteral";
  case HirKind::StringLiteral: return "StringLiteral";
  case HirKind::BoolLiteral:   return "BoolLiteral";
  case HirKind::SymbolRef:     return "SymbolRef";
  case HirKind::Unary:         return "Unary";
  case HirKind::Binary:        return "Binary";
  case HirKind::Call:          return "Call";
  case HirKind::Field:         return "Field";
  case HirKind::Index:         return "Index";
  case HirKind::Pipe:          return "Pipe";
  case HirKind::Lambda:        return "Lambda";
  }
  return "<unknown>";
}

auto hir_mode_kind_from_name(std::string_view name) -> HirModeKind {
  if (name == "unsafe") return HirModeKind::Unsafe;
  if (name == "gpu") return HirModeKind::Gpu;
  if (name == "parallel") return HirModeKind::Parallel;
  return HirModeKind::Other;
}

auto hir_mode_kind_name(HirModeKind kind) -> const char* {
  switch (kind) {
  case HirModeKind::Unsafe:   return "unsafe";
  case HirModeKind::Gpu:      return "gpu";
  case HirModeKind::Parallel: return "parallel";
  case HirModeKind::Other:    return "other";
  }
  return "<unknown>";
}

} // namespace dao
