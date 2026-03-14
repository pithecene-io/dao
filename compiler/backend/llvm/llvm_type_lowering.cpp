#include "backend/llvm/llvm_type_lowering.h"

#include "frontend/types/type.h"

#include <llvm/IR/DerivedTypes.h>

namespace dao {

LlvmTypeLowering::LlvmTypeLowering(llvm::LLVMContext& ctx) : ctx_(ctx) {}

auto LlvmTypeLowering::lower(const Type* type) -> llvm::Type* {
  if (type == nullptr) {
    error_ = "cannot lower null type";
    return nullptr;
  }

  switch (type->kind()) {
  case TypeKind::Builtin: {
    const auto* b = static_cast<const TypeBuiltin*>(type);
    return lower_builtin(b->builtin());
  }

  case TypeKind::Void:
    return llvm::Type::getVoidTy(ctx_);

  case TypeKind::Pointer: {
    const auto* p = static_cast<const TypePointer*>(type);
    auto* pointee = lower(p->pointee());
    if (pointee == nullptr) {
      return nullptr;
    }
    return llvm::PointerType::getUnqual(pointee);
  }

  case TypeKind::Function: {
    const auto* f = static_cast<const TypeFunction*>(type);
    auto* ret = lower(f->return_type());
    if (ret == nullptr) {
      return nullptr;
    }
    std::vector<llvm::Type*> params;
    params.reserve(f->param_types().size());
    for (const auto* param : f->param_types()) {
      auto* lowered = lower(param);
      if (lowered == nullptr) {
        return nullptr;
      }
      params.push_back(lowered);
    }
    return llvm::FunctionType::get(ret, params, /*isVarArg=*/false);
  }

  case TypeKind::Named: {
    const auto* n = static_cast<const TypeNamed*>(type);
    if (n->name() == "string") {
      return string_type();
    }
    error_ = "unsupported named type: " + std::string(n->name());
    return nullptr;
  }

  case TypeKind::Struct:
    return lower_struct(static_cast<const TypeStruct*>(type));

  case TypeKind::GenericParam: {
    const auto* g = static_cast<const TypeGenericParam*>(type);
    error_ = "cannot lower unresolved generic parameter: " + std::string(g->name());
    return nullptr;
  }

  case TypeKind::Enum: {
    const auto* e = static_cast<const TypeEnum*>(type);
    error_ = "enum lowering not yet implemented: " + std::string(e->name());
    return nullptr;
  }

  case TypeKind::Generator:
    // Generator<T> is an opaque pointer to a compiler-generated frame.
    return llvm::PointerType::getUnqual(ctx_);
  }

  error_ = "unknown type kind";
  return nullptr;
}

auto LlvmTypeLowering::is_string_type(const Type* type) -> bool {
  if (type == nullptr) {
    return false;
  }
  if (type->kind() != TypeKind::Named) {
    return false;
  }
  const auto* n = static_cast<const TypeNamed*>(type);
  return n->name() == "string" && n->decl_id() == nullptr;
}

auto LlvmTypeLowering::is_unsigned(const Type* type) -> bool {
  if (type == nullptr || type->kind() != TypeKind::Builtin) {
    return false;
  }
  const auto* b = static_cast<const TypeBuiltin*>(type);
  switch (b->builtin()) {
  case BuiltinKind::U8:
  case BuiltinKind::U16:
  case BuiltinKind::U32:
  case BuiltinKind::U64:
    return true;
  default:
    return false;
  }
}

auto LlvmTypeLowering::string_type() -> llvm::StructType* {
  if (string_type_ == nullptr) {
    // String representation: { i8*, i64 } — pointer to bytes + length.
    // This is C-ABI-compatible per CONTRACT_BOOTSTRAP_AND_INTEROP.md.
    string_type_ = llvm::StructType::create(
        ctx_,
        {llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(ctx_)),
         llvm::Type::getInt64Ty(ctx_)},
        "dao.string");
  }
  return string_type_;
}

auto LlvmTypeLowering::lower_builtin(BuiltinKind kind) -> llvm::Type* {
  switch (kind) {
  case BuiltinKind::I8:
  case BuiltinKind::U8:
    return llvm::Type::getInt8Ty(ctx_);
  case BuiltinKind::I16:
  case BuiltinKind::U16:
    return llvm::Type::getInt16Ty(ctx_);
  case BuiltinKind::I32:
  case BuiltinKind::U32:
    return llvm::Type::getInt32Ty(ctx_);
  case BuiltinKind::I64:
  case BuiltinKind::U64:
    return llvm::Type::getInt64Ty(ctx_);
  case BuiltinKind::F32:
    return llvm::Type::getFloatTy(ctx_);
  case BuiltinKind::F64:
    return llvm::Type::getDoubleTy(ctx_);
  case BuiltinKind::Bool:
    return llvm::Type::getInt1Ty(ctx_);
  }
  error_ = "unknown builtin kind";
  return nullptr;
}

auto LlvmTypeLowering::lower_struct(const TypeStruct* type) -> llvm::Type* {
  // Check cache first to break potential cycles.
  auto it = struct_cache_.find(type->decl_id());
  if (it != struct_cache_.end()) {
    return it->second;
  }

  // Create opaque struct first (for potential self-reference).
  auto* st = llvm::StructType::create(ctx_, "dao." + std::string(type->name()));
  struct_cache_[type->decl_id()] = st;

  // Lower field types.
  std::vector<llvm::Type*> field_types;
  field_types.reserve(type->fields().size());
  for (const auto& field : type->fields()) {
    auto* lowered = lower(field.type);
    if (lowered == nullptr) {
      return nullptr;
    }
    field_types.push_back(lowered);
  }

  st->setBody(field_types);
  return st;
}

} // namespace dao
