#ifndef DAO_BACKEND_LLVM_LLVM_BACKEND_H
#define DAO_BACKEND_LLVM_LLVM_BACKEND_H

#include "backend/llvm/llvm_type_lowering.h"
#include "frontend/diagnostics/diagnostic.h"
#include "frontend/diagnostics/source.h"
#include "ir/mir/mir.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dao {

// ---------------------------------------------------------------------------
// LlvmBackendResult — output of LLVM lowering.
// ---------------------------------------------------------------------------

struct LlvmBackendResult {
  std::unique_ptr<llvm::Module> module;
  std::vector<Diagnostic> diagnostics;
};

// ---------------------------------------------------------------------------
// LlvmBackend — top-level MIR → LLVM IR lowering.
//
// Consumes a MirModule and produces an llvm::Module. This is the only
// public entry point for the LLVM backend.
// ---------------------------------------------------------------------------

class LlvmBackend {
public:
  explicit LlvmBackend(llvm::LLVMContext& ctx);

  auto lower(const MirModule& mir_module) -> LlvmBackendResult;

  // Emit textual LLVM IR to a stream.
  static void print_ir(std::ostream& out, const llvm::Module& module);

private:
  llvm::LLVMContext& ctx_;
  LlvmTypeLowering types_;
  std::unique_ptr<llvm::Module> module_;
  std::vector<Diagnostic> diagnostics_;

  void emit_diagnostic(Span span, const std::string& message);

  // Function lowering
  auto lower_function(const MirFunction& fn) -> bool;

  // Per-function state
  struct FunctionState {
    llvm::Function* llvm_fn = nullptr;
    llvm::IRBuilder<>* builder = nullptr;

    // MIR LocalId → LLVM alloca (for named locals / params)
    std::unordered_map<uint32_t, llvm::AllocaInst*> locals;

    // MIR MirValueId → LLVM Value* (for SSA temporaries)
    std::unordered_map<uint32_t, llvm::Value*> values;

    // MIR MirValueId → semantic Type* (for signedness decisions)
    std::unordered_map<uint32_t, const Type*> value_types;

    // MIR BlockId → LLVM BasicBlock*
    std::unordered_map<uint32_t, llvm::BasicBlock*> blocks;
  };

  auto lower_block(const MirBlock& block, const MirFunction& fn,
                    FunctionState& state) -> bool;
  auto lower_inst(const MirInst& inst, const MirFunction& fn,
                   FunctionState& state) -> bool;

  // Instruction lowering helpers
  auto lower_const_int(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_const_float(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_const_bool(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_const_string(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_unary(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_binary(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_store(const MirInst& inst, const MirFunction& fn,
                    FunctionState& state) -> bool;
  auto lower_load(const MirInst& inst, const MirFunction& fn,
                   FunctionState& state) -> bool;
  auto lower_fn_ref(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_call(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_return(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_br(const MirInst& inst, FunctionState& state) -> bool;
  auto lower_cond_br(const MirInst& inst, FunctionState& state) -> bool;

  // Value lookup
  auto get_value(MirValueId id, FunctionState& state) -> llvm::Value*;
};

} // namespace dao

#endif // DAO_BACKEND_LLVM_LLVM_BACKEND_H
