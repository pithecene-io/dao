#ifndef DAO_FRONTEND_TYPES_TYPE_CONTEXT_H
#define DAO_FRONTEND_TYPES_TYPE_CONTEXT_H

#include "frontend/types/type.h"

#include <array>
#include <cstdint>
#include <functional>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dao {

// ---------------------------------------------------------------------------
// TypeContext — arena owner and interning hub for semantic types.
// ---------------------------------------------------------------------------

class TypeContext {
public:
  TypeContext();
  ~TypeContext();

  TypeContext(const TypeContext&) = delete;
  auto operator=(const TypeContext&) -> TypeContext& = delete;
  TypeContext(TypeContext&&) noexcept;
  auto operator=(TypeContext&&) noexcept -> TypeContext&;

  // --- Builtin access (pre-populated, never null) ---

  auto builtin(BuiltinKind kind) -> const TypeBuiltin*;

  auto i8() -> const TypeBuiltin* { return builtin(BuiltinKind::I8); }
  auto i16() -> const TypeBuiltin* { return builtin(BuiltinKind::I16); }
  auto i32() -> const TypeBuiltin* { return builtin(BuiltinKind::I32); }
  auto i64() -> const TypeBuiltin* { return builtin(BuiltinKind::I64); }
  auto u8() -> const TypeBuiltin* { return builtin(BuiltinKind::U8); }
  auto u16() -> const TypeBuiltin* { return builtin(BuiltinKind::U16); }
  auto u32() -> const TypeBuiltin* { return builtin(BuiltinKind::U32); }
  auto u64() -> const TypeBuiltin* { return builtin(BuiltinKind::U64); }
  auto f32() -> const TypeBuiltin* { return builtin(BuiltinKind::F32); }
  auto f64() -> const TypeBuiltin* { return builtin(BuiltinKind::F64); }
  auto bool_type() -> const TypeBuiltin* { return builtin(BuiltinKind::Bool); }
  auto void_type() -> const TypeBuiltin* { return builtin(BuiltinKind::Void); }

  // --- Interned constructors (return canonical pointer) ---

  auto pointer_to(const Type* pointee) -> const TypePointer*;

  auto function_type(std::vector<const Type*> params, const Type* ret)
      -> const TypeFunction*;

  auto named_type(const void* decl_id, std::string_view name,
                  std::vector<const Type*> type_args) -> const TypeNamed*;

  auto generic_param(std::string_view name, uint32_t index)
      -> const TypeGenericParam*;

  // --- Nominal constructors (not interned — each call allocates) ---

  auto make_struct(const void* decl_id, std::string_view name,
                   std::vector<StructField> fields) -> const TypeStruct*;

  auto make_enum(const void* decl_id, std::string_view name,
                 std::vector<EnumVariant> variants) -> const TypeEnum*;

private:
  // --- Arena allocator ---

  static constexpr size_t kBlockSize = 4096;

  struct Block {
    char data[kBlockSize]; // NOLINT(modernize-avoid-c-arrays)
  };

  std::vector<Block*> blocks_;
  size_t offset_ = kBlockSize;
  std::vector<std::function<void()>> dtors_;

  auto allocate(size_t size, size_t align) -> void*;

  template <typename T, typename... Args> auto alloc(Args&&... args) -> T* {
    void* mem = allocate(sizeof(T), alignof(T));
    auto* ptr = new (mem) T(std::forward<Args>(args)...);
    if constexpr (!std::is_trivially_destructible_v<T>) {
      dtors_.push_back([ptr]() { ptr->~T(); }); // NOLINT(modernize-use-trailing-return-type)
    }
    return ptr;
  }

  void destroy();

  // --- Interning maps ---

  std::array<const TypeBuiltin*, kBuiltinKindCount> builtins_{};

  std::unordered_map<const Type*, const TypePointer*> pointer_map_;

  // Function type key: hash of (param types..., return type).
  struct FnKey {
    std::vector<const Type*> params;
    const Type* ret;

    auto operator==(const FnKey& other) const -> bool = default;
  };

  struct FnKeyHash {
    auto operator()(const FnKey& key) const -> size_t;
  };

  std::unordered_map<FnKey, const TypeFunction*, FnKeyHash> function_map_;

  // Named type key: (decl_id, type_args...).
  struct NamedKey {
    const void* decl_id;
    std::vector<const Type*> type_args;

    auto operator==(const NamedKey& other) const -> bool = default;
  };

  struct NamedKeyHash {
    auto operator()(const NamedKey& key) const -> size_t;
  };

  std::unordered_map<NamedKey, const TypeNamed*, NamedKeyHash> named_map_;

  // Generic param key: (name, index).
  struct GenericParamKey {
    std::string_view name;
    uint32_t index;

    auto operator==(const GenericParamKey& other) const -> bool = default;
  };

  struct GenericParamKeyHash {
    auto operator()(const GenericParamKey& key) const -> size_t;
  };

  std::unordered_map<GenericParamKey, const TypeGenericParam*, GenericParamKeyHash>
      generic_param_map_;
};

} // namespace dao

#endif // DAO_FRONTEND_TYPES_TYPE_CONTEXT_H
