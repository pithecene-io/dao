#ifndef DAO_FRONTEND_TYPES_TYPE_BUILTIN_H
#define DAO_FRONTEND_TYPES_TYPE_BUILTIN_H

#include <cstdint>
#include <optional>
#include <string_view>

namespace dao {

enum class BuiltinKind : std::uint8_t {
  I8,
  I16,
  I32,
  I64,
  U8,
  U16,
  U32,
  U64,
  F32,
  F64,
  Bool,
  Void, // compiler-internal return type, not a user-facing scalar
};

inline constexpr std::uint8_t kBuiltinKindCount = 12;

auto builtin_kind_name(BuiltinKind kind) -> const char*;
auto builtin_kind_from_name(std::string_view name) -> std::optional<BuiltinKind>;

} // namespace dao

#endif // DAO_FRONTEND_TYPES_TYPE_BUILTIN_H
