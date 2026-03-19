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
/// INTEGER + SSE → INTEGER (INTEGER dominates in the same eightbyte).
auto merge_class(AbiClass lhs, AbiClass rhs) -> AbiClass {
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
  AbiClass classification = AbiClass::Integer;

  // Track SSE field types for precise coercion (float vs double vs <2xfloat>).
  uint32_t num_floats = 0;   // f32 fields in this eightbyte
  uint32_t num_doubles = 0;  // f64 fields in this eightbyte

  // Byte range this eightbyte covers within the struct.
  uint64_t start_byte = 0;
  uint64_t end_byte = 0; // one past last byte with data
};

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

  // Classify each field into its eightbyte.
  for (unsigned field_idx = 0; field_idx < struct_type->getNumElements();
       ++field_idx) {
    uint64_t field_offset = layout->getElementOffset(field_idx);
    llvm::Type* field_type = struct_type->getElementType(field_idx);
    uint64_t field_size =
        data_layout.getTypeAllocSize(field_type);

    // Determine which eightbyte this field starts in.
    uint32_t eb_idx = static_cast<uint32_t>(field_offset / kEightbyteSize);
    if (eb_idx >= num_eightbytes) {
      // Field beyond expected range — shouldn't happen for valid structs.
      result.indirect = true;
      return result;
    }

    // If a field spans two eightbytes, pass indirectly (conservative).
    uint64_t field_end = field_offset + field_size;
    uint64_t eb_boundary = (eb_idx + 1) * kEightbyteSize;
    if (field_end > eb_boundary && eb_idx + 1 < num_eightbytes) {
      // Field straddles eightbyte boundary — handle the split.
      // For nested structs this can happen; pass indirectly for safety.
      if (!field_type->isStructTy()) {
        // Scalar fields should not straddle; this would be a layout error.
        result.indirect = true;
        return result;
      }
      // Nested struct: recursively classify it.
      // For simplicity in the initial implementation, if a nested struct
      // straddles an eightbyte boundary, we classify each half by walking
      // the nested struct's fields. But the simple approach here is to
      // just mark as indirect for any straddling.
      result.indirect = true;
      return result;
    }

    // Update the end_byte tracker for this eightbyte.
    eightbytes[eb_idx].end_byte =
        std::max(eightbytes[eb_idx].end_byte, field_end);

    // Classify the field.
    if (field_type->isStructTy()) {
      // Nested struct within one eightbyte: classify as INTEGER
      // (conservative but correct — nested struct fields are scalar).
      eightbytes[eb_idx].classification =
          merge_class(eightbytes[eb_idx].classification, AbiClass::Integer);
    } else {
      AbiClass field_class = classify_scalar(field_type);
      eightbytes[eb_idx].classification =
          merge_class(eightbytes[eb_idx].classification, field_class);

      if (field_type->isFloatTy()) {
        eightbytes[eb_idx].num_floats++;
      } else if (field_type->isDoubleTy()) {
        eightbytes[eb_idx].num_doubles++;
      }
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
