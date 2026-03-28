#ifndef DAO_FRONTEND_TYPES_TYPE_H
#define DAO_FRONTEND_TYPES_TYPE_H

#include "frontend/types/type_builtin.h"
#include "frontend/types/type_kind.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace dao {

// ---------------------------------------------------------------------------
// Base class — all semantic types derive from this.
// ---------------------------------------------------------------------------

class Type {
public:
  explicit Type(TypeKind kind) : kind_(kind) {
  }
  virtual ~Type() = default;

  Type(const Type&) = delete;
  auto operator=(const Type&) -> Type& = delete;
  Type(Type&&) = delete;
  auto operator=(Type&&) -> Type& = delete;

  [[nodiscard]] auto kind() const -> TypeKind {
    return kind_;
  }

private:
  TypeKind kind_;
};

// ---------------------------------------------------------------------------
// Concrete semantic types
// ---------------------------------------------------------------------------

class TypeBuiltin : public Type {
public:
  explicit TypeBuiltin(BuiltinKind builtin) : Type(TypeKind::Builtin), builtin_(builtin) {
  }

  [[nodiscard]] auto builtin() const -> BuiltinKind {
    return builtin_;
  }

  static auto classof(const Type* t) -> bool {
    return t->kind() == TypeKind::Builtin;
  }

private:
  BuiltinKind builtin_;
};

// Void — compiler-internal return type. Not a builtin scalar.
// See CONTRACT_TYPE_SYSTEM_FOUNDATIONS.md §5.
class TypeVoid : public Type {
public:
  TypeVoid() : Type(TypeKind::Void) {
  }

  static auto classof(const Type* t) -> bool {
    return t->kind() == TypeKind::Void;
  }
};

class TypePointer : public Type {
public:
  explicit TypePointer(const Type* pointee) : Type(TypeKind::Pointer), pointee_(pointee) {
  }

  [[nodiscard]] auto pointee() const -> const Type* {
    return pointee_;
  }

  static auto classof(const Type* t) -> bool {
    return t->kind() == TypeKind::Pointer;
  }

private:
  const Type* pointee_;
};

class TypeFunction : public Type {
public:
  TypeFunction(std::vector<const Type*> param_types, const Type* return_type)
      : Type(TypeKind::Function), param_types_(std::move(param_types)), return_type_(return_type) {
  }

  [[nodiscard]] auto param_types() const -> const std::vector<const Type*>& {
    return param_types_;
  }
  [[nodiscard]] auto return_type() const -> const Type* {
    return return_type_;
  }

  static auto classof(const Type* t) -> bool {
    return t->kind() == TypeKind::Function;
  }

private:
  std::vector<const Type*> param_types_;
  const Type* return_type_;
};

class TypeNamed : public Type {
public:
  TypeNamed(const void* decl_id, std::string_view name, std::vector<const Type*> type_args)
      : Type(TypeKind::Named), decl_id_(decl_id), name_(name), type_args_(std::move(type_args)) {
  }

  [[nodiscard]] auto decl_id() const -> const void* {
    return decl_id_;
  }
  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }
  [[nodiscard]] auto type_args() const -> const std::vector<const Type*>& {
    return type_args_;
  }

  static auto classof(const Type* t) -> bool {
    return t->kind() == TypeKind::Named;
  }

private:
  const void* decl_id_;
  std::string_view name_;
  std::vector<const Type*> type_args_;
};

class TypeGenericParam : public Type {
public:
  TypeGenericParam(const void* binder, std::string_view name, uint32_t index)
      : Type(TypeKind::GenericParam), binder_(binder), name_(name), index_(index) {
  }

  /// The declaration (Decl*) that introduces this type parameter.
  [[nodiscard]] auto binder() const -> const void* {
    return binder_;
  }
  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }
  [[nodiscard]] auto index() const -> uint32_t {
    return index_;
  }

  static auto classof(const Type* t) -> bool {
    return t->kind() == TypeKind::GenericParam;
  }

private:
  const void* binder_;
  std::string_view name_;
  uint32_t index_;
};

struct StructField {
  std::string_view name;
  const Type* type;
};

class TypeStruct : public Type {
public:
  TypeStruct(const void* decl_id, std::string_view name, std::vector<StructField> fields)
      : Type(TypeKind::Struct), decl_id_(decl_id), name_(name), fields_(std::move(fields)) {
  }

  [[nodiscard]] auto decl_id() const -> const void* {
    return decl_id_;
  }
  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }
  [[nodiscard]] auto fields() const -> const std::vector<StructField>& {
    return fields_;
  }

  // Set or replace fields. Used during type-checker initialization to
  // support forward references: shells are registered with empty fields
  // first, then fields are resolved once all type shells exist.
  void set_fields(std::vector<StructField> fields) {
    fields_ = std::move(fields);
  }

  static auto classof(const Type* t) -> bool {
    return t->kind() == TypeKind::Struct;
  }

private:
  const void* decl_id_;
  std::string_view name_;
  std::vector<StructField> fields_;
};

struct EnumVariant {
  std::string_view name;
  std::vector<const Type*> payload_types;    // empty for C-style variants
  std::vector<std::string_view> field_names; // parallel to payload_types (enum class only)
};

class TypeEnum : public Type {
public:
  TypeEnum(const void* decl_id, std::string_view name, std::vector<EnumVariant> variants)
      : Type(TypeKind::Enum), decl_id_(decl_id), name_(name), variants_(std::move(variants)) {
  }

  [[nodiscard]] auto decl_id() const -> const void* {
    return decl_id_;
  }
  [[nodiscard]] auto name() const -> std::string_view {
    return name_;
  }
  [[nodiscard]] auto variants() const -> const std::vector<EnumVariant>& {
    return variants_;
  }

  static auto classof(const Type* t) -> bool {
    return t->kind() == TypeKind::Enum;
  }

private:
  const void* decl_id_;
  std::string_view name_;
  std::vector<EnumVariant> variants_;
};

// Generator<T> — compiler-provided coroutine type. Not user-definable.
// See TASK_13_COROUTINES.md §4.
class TypeGenerator : public Type {
public:
  explicit TypeGenerator(const Type* yield_type)
      : Type(TypeKind::Generator), yield_type_(yield_type) {
  }

  [[nodiscard]] auto yield_type() const -> const Type* {
    return yield_type_;
  }

  static auto classof(const Type* t) -> bool {
    return t->kind() == TypeKind::Generator;
  }

private:
  const Type* yield_type_;
};

} // namespace dao

#endif // DAO_FRONTEND_TYPES_TYPE_H
