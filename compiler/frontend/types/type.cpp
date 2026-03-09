#include "frontend/types/type_builtin.h"
#include "frontend/types/type_kind.h"

#include <string_view>

namespace dao {

auto type_kind_name(TypeKind kind) -> const char* {
  switch (kind) {
  case TypeKind::Builtin:
    return "Builtin";
  case TypeKind::Void:
    return "Void";
  case TypeKind::Pointer:
    return "Pointer";
  case TypeKind::Function:
    return "Function";
  case TypeKind::Named:
    return "Named";
  case TypeKind::GenericParam:
    return "GenericParam";
  case TypeKind::Struct:
    return "Struct";
  case TypeKind::Enum:
    return "Enum";
  }
  return "Unknown";
}

auto builtin_kind_name(BuiltinKind kind) -> const char* {
  switch (kind) {
  case BuiltinKind::I8:
    return "i8";
  case BuiltinKind::I16:
    return "i16";
  case BuiltinKind::I32:
    return "i32";
  case BuiltinKind::I64:
    return "i64";
  case BuiltinKind::U8:
    return "u8";
  case BuiltinKind::U16:
    return "u16";
  case BuiltinKind::U32:
    return "u32";
  case BuiltinKind::U64:
    return "u64";
  case BuiltinKind::F32:
    return "f32";
  case BuiltinKind::F64:
    return "f64";
  case BuiltinKind::Bool:
    return "bool";
  }
  return "unknown";
}

auto builtin_kind_from_name(std::string_view name) -> std::optional<BuiltinKind> {
  if (name == "i8") return BuiltinKind::I8;
  if (name == "i16") return BuiltinKind::I16;
  if (name == "i32") return BuiltinKind::I32;
  if (name == "i64") return BuiltinKind::I64;
  if (name == "u8") return BuiltinKind::U8;
  if (name == "u16") return BuiltinKind::U16;
  if (name == "u32") return BuiltinKind::U32;
  if (name == "u64") return BuiltinKind::U64;
  if (name == "f32") return BuiltinKind::F32;
  if (name == "f64") return BuiltinKind::F64;
  if (name == "bool") return BuiltinKind::Bool;
  return std::nullopt;
}

} // namespace dao
