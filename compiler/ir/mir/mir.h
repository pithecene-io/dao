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
#include <variant>
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
// Forward declarations for payload types
// ---------------------------------------------------------------------------

struct MirFunction;

// ---------------------------------------------------------------------------
// Instruction payloads — one per MirInstKind, carrying only that kind's data.
// All payloads are trivially destructible (scalars, pointers, string_views).
// ---------------------------------------------------------------------------

struct MirConstInt    { int64_t value; };
struct MirConstFloat  { double value; };
struct MirConstBool   { bool value; };
struct MirConstString { std::string_view value; };

struct MirUnary  { UnaryOp op; MirValueId operand; };
struct MirBinary { BinaryOp op; MirValueId lhs; MirValueId rhs; };

struct MirStore { MirPlace* place; MirValueId value; };
struct MirLoad  { MirPlace* place; };
struct MirAddrOf { MirPlace* place; };

struct MirFieldAccess { MirValueId object; std::string_view field; uint32_t field_index; };
struct MirIndexAccess { MirValueId object; MirValueId index; };

struct MirFnRef { const Symbol* symbol; };
struct MirCall  { MirValueId callee; std::vector<MirValueId>* args;
                  std::vector<const Type*>* explicit_type_args = nullptr; };
struct MirConstruct { const TypeStruct* struct_type; std::vector<MirValueId>* field_values; };

struct MirIterInit    { MirValueId iter_operand; };
struct MirIterHasNext { MirValueId iter_operand; };
struct MirIterNext    { MirValueId iter_operand; };
struct MirIterDestroy { MirValueId iter_operand; };

struct MirYieldInst   { MirValueId value; };

struct MirModeEnter     { HirModeKind mode_kind; std::string_view region_name; };
struct MirModeExit      { HirModeKind mode_kind; };
struct MirResourceEnter { std::string_view region_kind; std::string_view region_name; };
struct MirResourceExit  { std::string_view region_kind; std::string_view region_name; MirValueId domain_handle; };

struct MirLambdaInst { MirFunction* fn; };

struct MirBr     { BlockId target; };
struct MirCondBr { MirValueId cond; BlockId then_block; BlockId else_block; };
struct MirReturn { MirValueId value; bool has_value; };

// ---------------------------------------------------------------------------
// MirPayload — variant of all instruction payloads.
// Variant alternative order matches MirInstKind enum order.
// ---------------------------------------------------------------------------

using MirPayload = std::variant<
    MirConstInt, MirConstFloat, MirConstBool, MirConstString,
    MirUnary, MirBinary,
    MirStore, MirLoad, MirAddrOf,
    MirFieldAccess, MirIndexAccess,
    MirFnRef, MirCall, MirConstruct,
    MirIterInit, MirIterHasNext, MirIterNext, MirIterDestroy,
    MirYieldInst,
    MirModeEnter, MirModeExit, MirResourceEnter, MirResourceExit,
    MirLambdaInst,
    MirBr, MirCondBr, MirReturn>;

// ---------------------------------------------------------------------------
// MirInst — a single instruction in a basic block.
//
// The payload variant carries kind-specific data. kind() is derived from
// the active variant alternative. Value-producing instructions set result
// to a valid MirValueId; terminators and effects leave it invalid.
// ---------------------------------------------------------------------------

struct MirInst {
  MirValueId result;
  const Type* type = nullptr;
  Span span;
  MirPayload payload;

  [[nodiscard]] auto kind() const -> MirInstKind;
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
  bool is_extern = false;
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
