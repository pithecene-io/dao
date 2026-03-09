#ifndef DAO_FRONTEND_TYPES_TYPE_KIND_H
#define DAO_FRONTEND_TYPES_TYPE_KIND_H

#include <cstdint>

namespace dao {

enum class TypeKind : std::uint8_t {
  Builtin,
  Void, // compiler-internal return type, not a builtin scalar
  Pointer,
  Function,
  Named,
  GenericParam,
  Struct,
  Enum,
};

auto type_kind_name(TypeKind kind) -> const char*;

} // namespace dao

#endif // DAO_FRONTEND_TYPES_TYPE_KIND_H
