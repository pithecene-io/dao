// llvm_abi.h — x86-64 System V ABI struct coercion for extern fn.
//
// Implements the C ABI calling convention for struct-by-value parameters
// and returns at the LLVM IR level. LLVM does not automatically coerce
// struct types to match the platform C ABI — the frontend must do this.
//
// The x86-64 System V ABI classifies each 8-byte "eightbyte" of a struct
// independently as INTEGER, SSE, or MEMORY, then coerces them to scalar
// types for register passing. Structs > 16 bytes are passed indirectly.
//
// This implementation handles repr-C-compatible Dao structs only
// (CONTRACT_C_ABI_INTEROP.md §4.3).
//
// Authority: docs/contracts/CONTRACT_C_ABI_INTEROP.md

#ifndef DAO_BACKEND_LLVM_ABI_H
#define DAO_BACKEND_LLVM_ABI_H

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Type.h>

#include <cstdint>
#include <vector>

namespace dao {

/// Classification of one eightbyte in the x86-64 System V ABI.
/// NoClass is the identity element for merge — an eightbyte with no
/// fields starts as NoClass and takes the class of the first field.
enum class AbiClass : uint8_t { NoClass, Integer, Sse, Memory };

/// Result of classifying a struct for the C ABI.
struct AbiCoercion {
  /// If true, the struct is passed indirectly (byval for params, sret
  /// for returns). `coerced_types` is empty in this case.
  bool indirect = false;

  /// The coerced LLVM types for each eightbyte. For direct passing,
  /// these replace the struct type in the function signature.
  std::vector<llvm::Type*> coerced_types;
};

/// Classify a struct type for x86-64 System V C ABI passing.
///
/// `struct_type` must be a non-opaque LLVM struct type with a body.
/// Uses `dl` to compute field offsets and struct size.
///
/// Returns an AbiCoercion describing how the struct should be passed
/// at the C ABI boundary.
auto classify_struct_for_c_abi(llvm::StructType* struct_type,
                                const llvm::DataLayout& data_layout,
                                llvm::LLVMContext& ctx) -> AbiCoercion;

} // namespace dao

#endif // DAO_BACKEND_LLVM_ABI_H
