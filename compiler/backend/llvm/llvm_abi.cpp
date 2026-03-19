// llvm_abi.cpp — x86-64 System V ABI struct coercion implementation.
//
// See llvm_abi.h for design rationale.

#include "backend/llvm/llvm_abi.h"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>

#include <algorithm>
#include <cstdint>

namespace dao {

namespace {

// Maximum struct size for direct (register) passing on x86-64 SysV.
constexpr uint64_t kMaxDirectBytes = 16;

// Size of one eightbyte classification unit.
constexpr uint64_t kEightbyteSize = 8;

/// Classify a single LLVM type as INTEGER or SSE.
auto classify_scalar(llvm::Type* type) -> AbiClass {
  if (type->isFloatTy() || type->isDoubleTy()) {
    return AbiClass::Sse;
  }
  // All integer types, pointers, and booleans (i1) are INTEGER class.
  return AbiClass::Integer;
}

/// Merge two ABI classifications per x86-64 SysV rules.
/// NoClass is the identity element — NoClass + X → X.
/// INTEGER + SSE → INTEGER (INTEGER dominates in the same eightbyte).
auto merge_class(AbiClass lhs, AbiClass rhs) -> AbiClass {
  if (lhs == AbiClass::NoClass) return rhs;
  if (rhs == AbiClass::NoClass) return lhs;
  if (lhs == AbiClass::Memory || rhs == AbiClass::Memory) {
    return AbiClass::Memory;
  }
  if (lhs == AbiClass::Integer || rhs == AbiClass::Integer) {
    return AbiClass::Integer;
  }
  return AbiClass::Sse;
}

/// Information about one eightbyte after classification.
struct EightbyteInfo {
  // NoClass: no fields have been classified in this eightbyte yet.
  // This ensures that a pure-SSE eightbyte (e.g. { f64 }) stays SSE
  // instead of being forced to INTEGER by the default.
  AbiClass classification = AbiClass::NoClass;

  // Track SSE field types for precise coercion (float vs double vs <2xfloat>).
  uint32_t num_floats = 0;   // f32 fields in this eightbyte
  uint32_t num_doubles = 0;  // f64 fields in this eightbyte

  // Byte range this eightbyte covers within the struct.
  uint64_t start_byte = 0;
  uint64_t end_byte = 0; // one past last byte with data
};

/// Recursively classify a field into the eightbyte array.
/// For scalar fields, classifies directly. For nested struct fields,
/// walks their sub-fields recursively using the DataLayout to compute
/// sub-field offsets relative to the parent struct.
///
/// `base_offset` is the byte offset of `field_type` within the
/// top-level struct.
///
/// Returns false if the struct should be passed indirectly.
auto classify_field(llvm::Type* field_type, uint64_t base_offset,
                    const llvm::DataLayout& data_layout,
                    std::vector<EightbyteInfo>& eightbytes,
                    uint32_t num_eightbytes) -> bool {
  uint64_t field_size = data_layout.getTypeAllocSize(field_type);
  uint64_t field_end = base_offset + field_size;

  if (field_type->isStructTy()) {
    // Nested struct: recursively classify each sub-field.
    auto* nested = llvm::cast<llvm::StructType>(field_type);
    if (nested->isOpaque()) return false;
    const auto* nested_layout = data_layout.getStructLayout(nested);
    for (unsigned sub_idx = 0; sub_idx < nested->getNumElements(); ++sub_idx) {
      uint64_t sub_offset = base_offset + nested_layout->getElementOffset(sub_idx);
      if (!classify_field(nested->getElementType(sub_idx), sub_offset,
                          data_layout, eightbytes, num_eightbytes)) {
        return false;
      }
    }
    return true;
  }

  // Scalar field.
  auto eb_idx = static_cast<uint32_t>(base_offset / kEightbyteSize);
  if (eb_idx >= num_eightbytes) return false;

  // Scalar fields must not straddle eightbyte boundaries.
  uint64_t eb_boundary = (eb_idx + 1) * kEightbyteSize;
  if (field_end > eb_boundary && eb_idx + 1 < num_eightbytes) {
    return false; // misaligned scalar — layout error
  }

  eightbytes[eb_idx].end_byte =
      std::max(eightbytes[eb_idx].end_byte, field_end);

  AbiClass field_class = classify_scalar(field_type);
  eightbytes[eb_idx].classification =
      merge_class(eightbytes[eb_idx].classification, field_class);

  if (field_type->isFloatTy()) {
    eightbytes[eb_idx].num_floats++;
  } else if (field_type->isDoubleTy()) {
    eightbytes[eb_idx].num_doubles++;
  }

  return true;
}

} // namespace

auto classify_struct_for_c_abi(llvm::StructType* struct_type,
                                const llvm::DataLayout& data_layout,
                                llvm::LLVMContext& ctx) -> AbiCoercion {
  AbiCoercion result;

  if (struct_type == nullptr || struct_type->isOpaque()) {
    result.indirect = true;
    return result;
  }

  const auto* layout = data_layout.getStructLayout(struct_type);
  uint64_t struct_size = layout->getSizeInBytes();

  // Structs > 16 bytes are passed indirectly.
  if (struct_size > kMaxDirectBytes || struct_size == 0) {
    result.indirect = true;
    return result;
  }

  // Number of eightbytes (1 or 2).
  uint32_t num_eightbytes = (struct_size <= kEightbyteSize) ? 1 : 2;

  // Initialize eightbyte info.
  std::vector<EightbyteInfo> eightbytes(num_eightbytes);
  for (uint32_t eb_idx = 0; eb_idx < num_eightbytes; ++eb_idx) {
    eightbytes[eb_idx].start_byte = eb_idx * kEightbyteSize;
    // The end_byte starts at the eightbyte boundary; we'll update it
    // as we find fields that occupy this eightbyte.
    eightbytes[eb_idx].end_byte = eightbytes[eb_idx].start_byte;
  }

  // Classify each field into its eightbyte. Nested structs are walked
  // recursively so their scalar sub-fields are classified individually
  // — this handles nested structs that span eightbyte boundaries and
  // preserves SSE classification for float-only nested structs.
  for (unsigned field_idx = 0; field_idx < struct_type->getNumElements();
       ++field_idx) {
    uint64_t field_offset = layout->getElementOffset(field_idx);
    llvm::Type* field_type = struct_type->getElementType(field_idx);

    if (!classify_field(field_type, field_offset, data_layout,
                        eightbytes, num_eightbytes)) {
      result.indirect = true;
      return result;
    }
  }

  // Generate coerced types for each eightbyte.
  for (uint32_t eb_idx = 0; eb_idx < num_eightbytes; ++eb_idx) {
    const auto& ebi = eightbytes[eb_idx];

    // Skip empty eightbytes (no fields placed there).
    if (ebi.end_byte <= ebi.start_byte) {
      continue;
    }

    uint64_t data_bytes = ebi.end_byte - ebi.start_byte;

    if (ebi.classification == AbiClass::Sse) {
      // SSE eightbyte coercion:
      //   - one f64 → double
      //   - one f32 → float
      //   - two f32 → <2 x float>
      if (ebi.num_doubles == 1 && ebi.num_floats == 0) {
        result.coerced_types.push_back(llvm::Type::getDoubleTy(ctx));
      } else if (ebi.num_floats == 1 && ebi.num_doubles == 0) {
        result.coerced_types.push_back(llvm::Type::getFloatTy(ctx));
      } else if (ebi.num_floats == 2 && ebi.num_doubles == 0) {
        result.coerced_types.push_back(
            llvm::FixedVectorType::get(llvm::Type::getFloatTy(ctx), 2));
      } else {
        // Fallback: coerce to integer.
        result.coerced_types.push_back(
            llvm::IntegerType::get(ctx, data_bytes * 8));
      }
    } else {
      // INTEGER eightbyte: coerce to iN where N = data_bytes * 8.
      result.coerced_types.push_back(
          llvm::IntegerType::get(ctx, data_bytes * 8));
    }
  }

  return result;
}

} // namespace dao
