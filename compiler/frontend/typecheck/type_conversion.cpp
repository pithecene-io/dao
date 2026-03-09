#include "frontend/typecheck/type_conversion.h"

namespace dao {

auto is_assignable(const Type* source, const Type* target) -> bool {
  // Exact semantic type equality via pointer identity.
  // Canonical types are interned, so pointer equality is correct.
  return source == target;
}

auto is_numeric(const Type* type) -> bool {
  if (type == nullptr || type->kind() != TypeKind::Builtin) {
    return false;
  }
  auto kind = static_cast<const TypeBuiltin*>(type)->builtin();
  switch (kind) {
  case BuiltinKind::I8:
  case BuiltinKind::I16:
  case BuiltinKind::I32:
  case BuiltinKind::I64:
  case BuiltinKind::U8:
  case BuiltinKind::U16:
  case BuiltinKind::U32:
  case BuiltinKind::U64:
  case BuiltinKind::F32:
  case BuiltinKind::F64:
    return true;
  case BuiltinKind::Bool:
    return false;
  }
  return false;
}

auto is_integer(const Type* type) -> bool {
  if (type == nullptr || type->kind() != TypeKind::Builtin) {
    return false;
  }
  auto kind = static_cast<const TypeBuiltin*>(type)->builtin();
  switch (kind) {
  case BuiltinKind::I8:
  case BuiltinKind::I16:
  case BuiltinKind::I32:
  case BuiltinKind::I64:
  case BuiltinKind::U8:
  case BuiltinKind::U16:
  case BuiltinKind::U32:
  case BuiltinKind::U64:
    return true;
  default:
    return false;
  }
}

auto is_float(const Type* type) -> bool {
  if (type == nullptr || type->kind() != TypeKind::Builtin) {
    return false;
  }
  auto kind = static_cast<const TypeBuiltin*>(type)->builtin();
  return kind == BuiltinKind::F32 || kind == BuiltinKind::F64;
}

} // namespace dao
