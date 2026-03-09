#include "frontend/types/type_context.h"

#include <cstdint>
#include <functional>
#include <ranges>

namespace dao {

// ---------------------------------------------------------------------------
// Arena helpers
// ---------------------------------------------------------------------------

auto TypeContext::allocate(size_t size, size_t align) -> void* {
  offset_ = (offset_ + align - 1) & ~(align - 1);
  if (offset_ + size > kBlockSize) {
    if (size > kBlockSize) {
      auto* mem = ::operator new(size);
      blocks_.push_back(static_cast<Block*>(mem));
      return mem;
    }
    blocks_.push_back(new Block);
    offset_ = 0;
  }
  void* ptr =
      blocks_.back()->data + offset_; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  offset_ += size;
  return ptr;
}

void TypeContext::destroy() {
  for (auto& dtor : std::ranges::reverse_view(dtors_)) {
    dtor();
  }
  for (auto* block : blocks_) {
    ::operator delete(block);
  }
  blocks_.clear();
  offset_ = kBlockSize;
  dtors_.clear();
}

// ---------------------------------------------------------------------------
// Hash helpers
// ---------------------------------------------------------------------------

namespace {

auto hash_combine(size_t seed, size_t value) -> size_t {
  // Boost-style hash combine.
  return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

auto hash_type_ptr(const Type* t) -> size_t {
  return std::hash<const void*>{}(t);
}

} // namespace

auto TypeContext::FnKeyHash::operator()(const FnKey& key) const -> size_t {
  size_t h = hash_type_ptr(key.ret);
  for (const auto* p : key.params) {
    h = hash_combine(h, hash_type_ptr(p));
  }
  return h;
}

auto TypeContext::NamedKeyHash::operator()(const NamedKey& key) const -> size_t {
  size_t h = std::hash<const void*>{}(key.decl_id);
  for (const auto* a : key.type_args) {
    h = hash_combine(h, hash_type_ptr(a));
  }
  return h;
}

auto TypeContext::GenericParamKeyHash::operator()(const GenericParamKey& key) const
    -> size_t {
  size_t h = std::hash<std::string_view>{}(key.name);
  return hash_combine(h, std::hash<uint32_t>{}(key.index));
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TypeContext::TypeContext() {
  // Pre-populate all builtin types.
  for (uint8_t i = 0; i < kBuiltinKindCount; ++i) {
    auto kind = static_cast<BuiltinKind>(i);
    builtins_[i] = alloc<TypeBuiltin>(kind);
  }
}

TypeContext::~TypeContext() { destroy(); }

TypeContext::TypeContext(TypeContext&& other) noexcept
    : blocks_(std::move(other.blocks_)), offset_(other.offset_),
      dtors_(std::move(other.dtors_)), builtins_(other.builtins_),
      pointer_map_(std::move(other.pointer_map_)),
      function_map_(std::move(other.function_map_)),
      named_map_(std::move(other.named_map_)),
      generic_param_map_(std::move(other.generic_param_map_)) {
  other.blocks_.clear();
  other.offset_ = kBlockSize;
  other.dtors_.clear();
  other.builtins_ = {};
}

auto TypeContext::operator=(TypeContext&& other) noexcept -> TypeContext& {
  if (this != &other) {
    destroy();
    blocks_ = std::move(other.blocks_);
    offset_ = other.offset_;
    dtors_ = std::move(other.dtors_);
    builtins_ = other.builtins_;
    pointer_map_ = std::move(other.pointer_map_);
    function_map_ = std::move(other.function_map_);
    named_map_ = std::move(other.named_map_);
    generic_param_map_ = std::move(other.generic_param_map_);
    other.blocks_.clear();
    other.offset_ = kBlockSize;
    other.dtors_.clear();
    other.builtins_ = {};
  }
  return *this;
}

// ---------------------------------------------------------------------------
// Builtin access
// ---------------------------------------------------------------------------

auto TypeContext::builtin(BuiltinKind kind) -> const TypeBuiltin* {
  return builtins_[static_cast<uint8_t>(kind)];
}

// ---------------------------------------------------------------------------
// Interned constructors
// ---------------------------------------------------------------------------

auto TypeContext::pointer_to(const Type* pointee) -> const TypePointer* {
  auto [it, inserted] = pointer_map_.try_emplace(pointee, nullptr);
  if (inserted) {
    it->second = alloc<TypePointer>(pointee);
  }
  return it->second;
}

auto TypeContext::function_type(std::vector<const Type*> params,
                                const Type* ret) -> const TypeFunction* {
  FnKey key{params, ret};
  auto [it, inserted] = function_map_.try_emplace(key, nullptr);
  if (inserted) {
    it->second = alloc<TypeFunction>(std::move(params), ret);
  }
  return it->second;
}

auto TypeContext::named_type(const void* decl_id, std::string_view name,
                             std::vector<const Type*> type_args)
    -> const TypeNamed* {
  NamedKey key{decl_id, type_args};
  auto [it, inserted] = named_map_.try_emplace(key, nullptr);
  if (inserted) {
    it->second = alloc<TypeNamed>(decl_id, name, std::move(type_args));
  }
  return it->second;
}

auto TypeContext::generic_param(std::string_view name, uint32_t index)
    -> const TypeGenericParam* {
  GenericParamKey key{name, index};
  auto [it, inserted] = generic_param_map_.try_emplace(key, nullptr);
  if (inserted) {
    it->second = alloc<TypeGenericParam>(name, index);
  }
  return it->second;
}

// ---------------------------------------------------------------------------
// Nominal constructors
// ---------------------------------------------------------------------------

auto TypeContext::make_struct(const void* decl_id, std::string_view name,
                              std::vector<StructField> fields)
    -> const TypeStruct* {
  return alloc<TypeStruct>(decl_id, name, std::move(fields));
}

auto TypeContext::make_enum(const void* decl_id, std::string_view name,
                             std::vector<EnumVariant> variants)
    -> const TypeEnum* {
  return alloc<TypeEnum>(decl_id, name, std::move(variants));
}

} // namespace dao
