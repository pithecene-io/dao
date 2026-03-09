#ifndef DAO_IR_MIR_MIR_H
#define DAO_IR_MIR_MIR_H

#include "frontend/ast/ast.h"
#include "frontend/diagnostics/source.h"
#include "frontend/resolve/symbol.h"
#include "frontend/types/type.h"
#include "ir/hir/hir.h"
#include "ir/mir/mir_kind.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace dao {

// ---------------------------------------------------------------------------
// Typed identifiers
// ---------------------------------------------------------------------------

struct MirValueId {
  uint32_t id = UINT32_MAX;
  [[nodiscard]] auto valid() const -> bool { return id != UINT32_MAX; }
};

struct BlockId {
  uint32_t id = UINT32_MAX;
  [[nodiscard]] auto valid() const -> bool { return id != UINT32_MAX; }
};

struct LocalId {
  uint32_t id = UINT32_MAX;
  [[nodiscard]] auto valid() const -> bool { return id != UINT32_MAX; }
};

// ---------------------------------------------------------------------------
// MirLocal — named storage slot
// ---------------------------------------------------------------------------

struct MirLocal {
  LocalId id;
  const Symbol* symbol; // nullable for compiler-introduced temporaries
  const Type* type;
  Span span;
  bool is_param = false;
};

// ---------------------------------------------------------------------------
// MirPlace — addressable storage location with projections
// ---------------------------------------------------------------------------

enum class MirProjectionKind : std::uint8_t { Field, Index, Deref };

struct MirProjection {
  MirProjectionKind kind;
  std::string_view field_name;  // for Field
  uint32_t field_index = 0;     // for Field
  MirValueId index_value;       // for Index
};

struct MirPlace {
  LocalId local;
  std::vector<MirProjection> projections;
};

// ---------------------------------------------------------------------------
// MirInst — a single instruction in a basic block
//
// Each instruction has a kind tag. Value-producing instructions set
// result to a valid MirValueId. Terminators have an invalid result.
// Payload fields are relevant only for their matching kind.
// ---------------------------------------------------------------------------

struct MirInst {
  MirInstKind kind;
  MirValueId result;
  const Type* type = nullptr;
  Span span;

  // --- Constant payloads ---
  int64_t const_int = 0;
  double const_float = 0.0;
  bool const_bool = false;
  std::string_view const_string;

  // --- Unary/binary ---
  UnaryOp unary_op = UnaryOp::Negate;
  BinaryOp binary_op = BinaryOp::Add;
  MirValueId operand;
  MirValueId lhs;
  MirValueId rhs;

  // --- Store/Load/AddrOf ---
  MirPlace* place = nullptr; // arena-allocated; for Store, Load, AddrOf
  MirValueId store_value;

  // --- Field/Index access (value-producing) ---
  MirValueId access_object;
  std::string_view access_field;
  MirValueId access_index;

  // --- Function reference ---
  const Symbol* fn_symbol = nullptr; // for FnRef

  // --- Call ---
  MirValueId callee;
  std::vector<MirValueId>* call_args = nullptr; // arena-allocated

  // --- Iteration ---
  MirValueId iter_operand;

  // --- Mode/resource ---
  HirModeKind mode_kind = HirModeKind::Other;
  std::string_view region_kind;
  std::string_view region_name;

  // --- Lambda ---
  struct MirFunction* lambda_fn = nullptr;

  // --- Terminators ---
  BlockId br_target;
  MirValueId cond;
  BlockId then_block;
  BlockId else_block;
  MirValueId return_value;
  bool has_return_value = false;
};

// ---------------------------------------------------------------------------
// MirBlock — basic block with instruction list and terminator
// ---------------------------------------------------------------------------

struct MirBlock {
  BlockId id;
  std::vector<MirInst*> insts; // last must be a terminator
};

// ---------------------------------------------------------------------------
// MirFunction
// ---------------------------------------------------------------------------

struct MirFunction {
  const Symbol* symbol = nullptr;
  const Type* return_type = nullptr;
  std::vector<MirLocal> locals; // params first, then let-bindings
  std::vector<MirBlock*> blocks; // blocks[0] is entry
  Span span;
};

// ---------------------------------------------------------------------------
// MirModule
// ---------------------------------------------------------------------------

struct MirModule {
  std::vector<MirFunction*> functions;
  Span span;
};

} // namespace dao

#endif // DAO_IR_MIR_MIR_H
