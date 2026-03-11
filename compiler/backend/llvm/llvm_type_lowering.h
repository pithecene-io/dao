#ifndef DAO_BACKEND_LLVM_LLVM_TYPE_LOWERING_H
#define DAO_BACKEND_LLVM_LLVM_TYPE_LOWERING_H

#include "frontend/types/type.h"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

#include <string>
#include <unordered_map>

namespace dao {

// ---------------------------------------------------------------------------
// LlvmTypeLowering — centralized semantic Type* → llvm::Type* mapping.
//
// All LLVM type decisions live here. Backend code must not create LLVM
// types ad hoc; it must go through this layer.
// ---------------------------------------------------------------------------

class LlvmTypeLowering {
public:
  explicit LlvmTypeLowering(llvm::LLVMContext& ctx);

  // Lower a Dao semantic type to an LLVM type.
  // Returns nullptr and sets error if the type cannot be lowered.
  auto lower(const Type* type) -> llvm::Type*;

  // Check whether a type is the predeclared string type.
  static auto is_string_type(const Type* type) -> bool;

  // Check whether a type is an unsigned integer builtin.
  static auto is_unsigned(const Type* type) -> bool;

  // Get the LLVM string representation type: { i8*, i64 } (ptr + length).
  auto string_type() -> llvm::StructType*;

  // Access the last lowering error (empty if none).
  [[nodiscard]] auto error() const -> const std::string& { return error_; }

private:
  llvm::LLVMContext& ctx_;
  llvm::StructType* string_type_ = nullptr;
  std::string error_;

  // Cache for struct type lowering to break cycles.
  std::unordered_map<const void*, llvm::StructType*> struct_cache_;

  auto lower_builtin(BuiltinKind kind) -> llvm::Type*;
  auto lower_struct(const TypeStruct* type) -> llvm::Type*;
};

} // namespace dao

#endif // DAO_BACKEND_LLVM_LLVM_TYPE_LOWERING_H
