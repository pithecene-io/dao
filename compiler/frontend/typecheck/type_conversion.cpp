#include "frontend/typecheck/type_conversion.h"

namespace dao {

auto is_assignable(const Type* source, const Type* target) -> bool {
  if (source == target) {
    return true;
  }

  // A generic type parameter accepts any concrete type.
  // Concept constraint checking is handled separately in check_call.
  if (target->kind() == TypeKind::GenericParam) {
    return true;
  }

  // Structural recursion for composite types containing generic params.
  if (source->kind() == target->kind()) {
    switch (target->kind()) {
    case TypeKind::Pointer:
      return is_assignable(
          static_cast<const TypePointer*>(source)->pointee(),
          static_cast<const TypePointer*>(target)->pointee());
    case TypeKind::Generator:
      return is_assignable(
          static_cast<const TypeGenerator*>(source)->yield_type(),
          static_cast<const TypeGenerator*>(target)->yield_type());
    case TypeKind::Function: {
      const auto* sf = static_cast<const TypeFunction*>(source);
      const auto* tf = static_cast<const TypeFunction*>(target);
      if (sf->param_types().size() != tf->param_types().size()) {
        return false;
      }
      for (size_t i = 0; i < sf->param_types().size(); ++i) {
        if (!is_assignable(sf->param_types()[i], tf->param_types()[i])) {
          return false;
        }
      }
      return is_assignable(sf->return_type(), tf->return_type());
    }
    default:
      break;
    }
  }

  return false;
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

auto is_c_abi_compatible(const Type* type) -> bool {
  if (type == nullptr) {
    return false;
  }
  switch (type->kind()) {
  case TypeKind::Builtin: {
    // All numeric builtins and bool are C ABI compatible — they map
    // directly to C scalar types (int8_t through uint64_t, float,
    // double, bool).
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
    case BuiltinKind::Bool:
      return true;
    }
    return false;
  }
  case TypeKind::Pointer:
    return true; // raw pointers
  case TypeKind::Void:
    return true; // void return
  default:
    return false; // string, struct, function, generator, named, enum, generic
  }
}

} // namespace dao
