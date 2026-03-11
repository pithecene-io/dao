#include "backend/llvm/llvm_backend.h"

#include "ir/mir/mir.h"
#include "ir/mir/mir_kind.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <cassert>
#include <string>

namespace dao {

// ---------------------------------------------------------------------------
// LlvmBackend
// ---------------------------------------------------------------------------

LlvmBackend::LlvmBackend(llvm::LLVMContext& ctx) : ctx_(ctx), types_(ctx) {}

auto LlvmBackend::lower(const MirModule& mir_module) -> LlvmBackendResult {
  module_ = std::make_unique<llvm::Module>("dao_module", ctx_);
  diagnostics_.clear();

  // First pass: declare all functions (forward declarations).
  for (const auto* fn : mir_module.functions) {
    if (fn->symbol == nullptr) {
      continue;
    }

    // Build LLVM function type.
    auto* ret_type = types_.lower(fn->return_type);
    if (ret_type == nullptr) {
      emit_diagnostic(fn->span, "cannot lower return type: " + types_.error());
      continue;
    }

    std::vector<llvm::Type*> param_types;
    for (const auto& local : fn->locals) {
      if (!local.is_param) {
        break; // params come first
      }
      auto* pt = types_.lower(local.type);
      if (pt == nullptr) {
        emit_diagnostic(local.span, "cannot lower param type: " + types_.error());
        break;
      }
      // String params are passed by pointer for C ABI compatibility.
      if (LlvmTypeLowering::is_string_type(local.type)) {
        pt = llvm::PointerType::getUnqual(pt);
      }
      param_types.push_back(pt);
    }

    auto* fn_type = llvm::FunctionType::get(ret_type, param_types, /*isVarArg=*/false);
    auto* llvm_fn = llvm::Function::Create(
        fn_type, llvm::Function::ExternalLinkage,
        std::string(fn->symbol->name), module_.get());

    // Name parameters.
    size_t idx = 0;
    for (auto& arg : llvm_fn->args()) {
      if (idx < fn->locals.size() && fn->locals[idx].is_param) {
        if (fn->locals[idx].symbol != nullptr) {
          arg.setName(std::string(fn->locals[idx].symbol->name));
        }
      }
      ++idx;
    }
  }

  // Second pass: lower function bodies.
  for (const auto* fn : mir_module.functions) {
    if (!lower_function(*fn)) {
      // Diagnostics already emitted.
    }
  }

  // Do not return a partial module when lowering produced errors.
  if (!diagnostics_.empty()) {
    module_.reset();
  }

  return {.module = std::move(module_), .diagnostics = std::move(diagnostics_)};
}

void LlvmBackend::print_ir(std::ostream& out, const llvm::Module& module) {
  llvm::raw_os_ostream llvm_out(out);
  module.print(llvm_out, nullptr);
}

void LlvmBackend::initialize_targets() {
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllAsmPrinters();
}

auto LlvmBackend::emit_object(llvm::Module& module,
                               const std::string& output_path,
                               std::string& error_out) -> bool {
  auto triple = llvm::sys::getDefaultTargetTriple();
  module.setTargetTriple(triple);

  std::string lookup_error;
  const auto* target = llvm::TargetRegistry::lookupTarget(triple, lookup_error);
  if (target == nullptr) {
    error_out = "failed to look up target: " + lookup_error;
    return false;
  }

  llvm::TargetOptions opts;
  auto target_machine = std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(triple, "generic", "", opts,
                                  llvm::Reloc::PIC_));
  if (target_machine == nullptr) {
    error_out = "failed to create target machine";
    return false;
  }

  module.setDataLayout(target_machine->createDataLayout());

  std::error_code ec;
  llvm::raw_fd_ostream dest(output_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    error_out = "failed to open output file: " + ec.message();
    return false;
  }

  llvm::legacy::PassManager pass;
  if (target_machine->addPassesToEmitFile(pass, dest, nullptr,
                                           llvm::CodeGenFileType::ObjectFile)) {
    error_out = "target machine cannot emit object files";
    return false;
  }

  pass.run(module);
  dest.flush();
  return true;
}

void LlvmBackend::emit_diagnostic(Span span, const std::string& message) {
  diagnostics_.push_back(
      Diagnostic{.severity = Severity::Error, .span = span, .message = message});
}

// ---------------------------------------------------------------------------
// Function lowering
// ---------------------------------------------------------------------------

auto LlvmBackend::lower_function(const MirFunction& fn) -> bool {
  if (fn.symbol == nullptr) {
    return true; // skip anonymous functions (lambdas handled separately)
  }

  auto* llvm_fn = module_->getFunction(std::string(fn.symbol->name));
  if (llvm_fn == nullptr) {
    emit_diagnostic(fn.span, "function not declared: " + std::string(fn.symbol->name));
    return false;
  }

  // Extern declaration — no body to lower, leave as LLVM declare.
  if (fn.is_extern) {
    return true;
  }

  FunctionState state;
  state.llvm_fn = llvm_fn;

  // Create all basic blocks first (for forward branches).
  for (const auto* block : fn.blocks) {
    auto* bb = llvm::BasicBlock::Create(
        ctx_, "bb" + std::to_string(block->id.id), llvm_fn);
    state.blocks[block->id.id] = bb;
  }

  // Create entry block allocas for all locals.
  llvm::IRBuilder<> builder(ctx_);
  state.builder = &builder;

  auto* entry_bb = state.blocks[fn.blocks[0]->id.id];
  builder.SetInsertPoint(entry_bb);

  // Allocate locals. Params get their alloca here too (store from arg below).
  for (const auto& local : fn.locals) {
    auto* local_type = types_.lower(local.type);
    if (local_type == nullptr) {
      emit_diagnostic(local.span, "cannot lower local type: " + types_.error());
      return false;
    }
    // String locals store the struct, not a pointer.
    auto* alloca = builder.CreateAlloca(
        local_type, nullptr,
        local.symbol != nullptr ? std::string(local.symbol->name) : "");
    state.locals[local.id.id] = alloca;
  }

  // Store function parameters into their allocas.
  size_t param_idx = 0;
  for (auto& arg : llvm_fn->args()) {
    if (param_idx < fn.locals.size() && fn.locals[param_idx].is_param) {
      auto* alloca = state.locals[fn.locals[param_idx].id.id];
      // For string params (passed by pointer), load the struct value.
      if (LlvmTypeLowering::is_string_type(fn.locals[param_idx].type)) {
        auto* loaded = builder.CreateLoad(types_.string_type(), &arg);
        builder.CreateStore(loaded, alloca);
      } else {
        builder.CreateStore(&arg, alloca);
      }
    }
    ++param_idx;
  }

  // Lower each block.
  for (const auto* block : fn.blocks) {
    if (!lower_block(*block, fn, state)) {
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// Block lowering
// ---------------------------------------------------------------------------

auto LlvmBackend::lower_block(const MirBlock& block, const MirFunction& fn,
                                FunctionState& state) -> bool {
  auto* bb = state.blocks[block.id.id];
  state.builder->SetInsertPoint(bb);

  for (const auto* inst : block.insts) {
    if (!lower_inst(*inst, fn, state)) {
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// Instruction dispatch
// ---------------------------------------------------------------------------

auto LlvmBackend::lower_inst(const MirInst& inst, const MirFunction& fn,
                               FunctionState& state) -> bool {
  switch (inst.kind) {
  case MirInstKind::ConstInt:
    return lower_const_int(inst, state);
  case MirInstKind::ConstFloat:
    return lower_const_float(inst, state);
  case MirInstKind::ConstBool:
    return lower_const_bool(inst, state);
  case MirInstKind::ConstString:
    return lower_const_string(inst, state);
  case MirInstKind::Unary:
    return lower_unary(inst, state);
  case MirInstKind::Binary:
    return lower_binary(inst, state);
  case MirInstKind::Store:
    return lower_store(inst, fn, state);
  case MirInstKind::Load:
    return lower_load(inst, fn, state);
  case MirInstKind::FnRef:
    return lower_fn_ref(inst, state);
  case MirInstKind::Call:
    return lower_call(inst, state);
  case MirInstKind::Return:
    return lower_return(inst, state);
  case MirInstKind::Br:
    return lower_br(inst, state);
  case MirInstKind::CondBr:
    return lower_cond_br(inst, state);

  // Mode unsafe — permission semantics enforced upstream; no-op in codegen.
  case MirInstKind::ModeEnter:
    if (inst.mode_kind != HirModeKind::Unsafe) {
      emit_diagnostic(inst.span,
                       "mode '" + std::string(inst.region_name) +
                       "' lowering not yet supported");
      return false;
    }
    return true;
  case MirInstKind::ModeExit:
    if (inst.mode_kind != HirModeKind::Unsafe) {
      // Entry already diagnosed; skip duplicate.
    }
    return true;

  // Resource regions — no runtime hook or upstream enforcement exists yet.
  // Reject explicitly per CONTRACT_EXECUTION_CONTEXTS.md and TASK_11 §16.2:
  // resource constructs must not be forgotten casually.
  case MirInstKind::ResourceEnter:
    emit_diagnostic(inst.span,
                     "resource region lowering not yet supported");
    return false;
  case MirInstKind::ResourceExit:
    return true;

  // AddrOf — address of a place.
  case MirInstKind::AddrOf: {
    if (inst.place == nullptr) {
      emit_diagnostic(inst.span, "AddrOf with null place");
      return false;
    }
    auto it = state.locals.find(inst.place->local.id);
    if (it == state.locals.end()) {
      emit_diagnostic(inst.span, "AddrOf: unknown local");
      return false;
    }
    if (!inst.place->projections.empty()) {
      auto* ptr = resolve_place(*inst.place, fn, state);
      if (ptr == nullptr) {
        emit_diagnostic(inst.span, "cannot resolve AddrOf projected place");
        return false;
      }
      state.values[inst.result.id] = ptr;
      return true;
    }
    state.values[inst.result.id] = it->second;
    return true;
  }

  // Field access — extractvalue on a struct SSA value.
  case MirInstKind::FieldAccess:
    return lower_field_access(inst, state);
  case MirInstKind::IndexAccess:
    emit_diagnostic(inst.span, "index access lowering not yet implemented");
    return false;
  case MirInstKind::IterInit:
  case MirInstKind::IterHasNext:
  case MirInstKind::IterNext:
    emit_diagnostic(inst.span, "iteration lowering not yet implemented");
    return false;
  case MirInstKind::Lambda:
    emit_diagnostic(inst.span, "lambda lowering not yet implemented");
    return false;
  }

  emit_diagnostic(inst.span, "unknown MIR instruction kind");
  return false;
}

// ---------------------------------------------------------------------------
// Value lookup
// ---------------------------------------------------------------------------

auto LlvmBackend::get_value(MirValueId id, FunctionState& state) -> llvm::Value* {
  auto it = state.values.find(id.id);
  if (it != state.values.end()) {
    return it->second;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Constant lowering
// ---------------------------------------------------------------------------

auto LlvmBackend::lower_const_int(const MirInst& inst, FunctionState& state) -> bool {
  auto* type = types_.lower(inst.type);
  if (type == nullptr) {
    emit_diagnostic(inst.span, "cannot lower const int type: " + types_.error());
    return false;
  }
  auto* val = llvm::ConstantInt::get(type, static_cast<uint64_t>(inst.const_int),
                                     /*isSigned=*/!LlvmTypeLowering::is_unsigned(inst.type));
  state.values[inst.result.id] = val;
  state.value_types[inst.result.id] = inst.type;
  return true;
}

auto LlvmBackend::lower_const_float(const MirInst& inst, FunctionState& state) -> bool {
  auto* type = types_.lower(inst.type);
  if (type == nullptr) {
    emit_diagnostic(inst.span, "cannot lower const float type: " + types_.error());
    return false;
  }
  auto* val = llvm::ConstantFP::get(type, inst.const_float);
  state.values[inst.result.id] = val;
  state.value_types[inst.result.id] = inst.type;
  return true;
}

auto LlvmBackend::lower_const_bool(const MirInst& inst, FunctionState& state) -> bool {
  auto* val = llvm::ConstantInt::get(llvm::Type::getInt1Ty(ctx_),
                                     inst.const_bool ? 1 : 0);
  state.values[inst.result.id] = val;
  state.value_types[inst.result.id] = inst.type;
  return true;
}

auto LlvmBackend::lower_const_string(const MirInst& inst, FunctionState& state) -> bool {
  // Create a global constant for the string data.
  auto str = std::string(inst.const_string);
  auto* str_constant = llvm::ConstantDataArray::getString(ctx_, str, /*AddNull=*/true);
  auto* global = new llvm::GlobalVariable(
      *module_, str_constant->getType(), /*isConstant=*/true,
      llvm::GlobalValue::PrivateLinkage, str_constant, ".str");
  global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // Build a dao.string struct value: { i8* ptr, i64 len }.
  auto* str_type = types_.string_type();
  auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0);
  auto* gep = state.builder->CreateInBoundsGEP(
      str_constant->getType(), global, {zero, zero}, "str.ptr");
  auto* len = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), str.size());

  // Build the struct aggregate.
  llvm::Value* agg = llvm::UndefValue::get(str_type);
  agg = state.builder->CreateInsertValue(agg, gep, 0, "str.with_ptr");
  agg = state.builder->CreateInsertValue(agg, len, 1, "str.with_len");

  state.values[inst.result.id] = agg;
  state.value_types[inst.result.id] = inst.type;
  return true;
}

// ---------------------------------------------------------------------------
// Unary / binary operations
// ---------------------------------------------------------------------------

auto LlvmBackend::lower_unary(const MirInst& inst, FunctionState& state) -> bool {
  auto* operand = get_value(inst.operand, state);
  if (operand == nullptr) {
    emit_diagnostic(inst.span, "unary operand not found");
    return false;
  }

  llvm::Value* result = nullptr;
  switch (inst.unary_op) {
  case UnaryOp::Negate:
    if (operand->getType()->isFloatingPointTy()) {
      result = state.builder->CreateFNeg(operand, "fneg");
    } else {
      result = state.builder->CreateNeg(operand, "neg");
    }
    break;
  case UnaryOp::Not:
    result = state.builder->CreateNot(operand, "not");
    break;
  case UnaryOp::Deref:
  case UnaryOp::AddrOf:
    // These should be lowered to Load/AddrOf MIR instructions by the MIR builder.
    emit_diagnostic(inst.span, "unexpected Deref/AddrOf as unary op in MIR");
    return false;
  }

  if (result != nullptr) {
    state.values[inst.result.id] = result;
    state.value_types[inst.result.id] = inst.type;
  }
  return result != nullptr;
}

auto LlvmBackend::lower_binary(const MirInst& inst, FunctionState& state) -> bool {
  auto* lhs = get_value(inst.lhs, state);
  auto* rhs = get_value(inst.rhs, state);
  if (lhs == nullptr || rhs == nullptr) {
    emit_diagnostic(inst.span, "binary operand not found");
    return false;
  }

  bool is_float = lhs->getType()->isFloatingPointTy();
  // For signedness, check the LHS operand's semantic type, not the result type.
  // Comparisons have result type bool, but signedness comes from operands.
  auto lhs_type_it = state.value_types.find(inst.lhs.id);
  const Type* operand_type = lhs_type_it != state.value_types.end() ? lhs_type_it->second : inst.type;
  bool is_unsigned_op = LlvmTypeLowering::is_unsigned(operand_type);
  llvm::Value* result = nullptr;

  switch (inst.binary_op) {
  case BinaryOp::Add:
    result = is_float ? state.builder->CreateFAdd(lhs, rhs, "fadd")
                      : state.builder->CreateAdd(lhs, rhs, "add");
    break;
  case BinaryOp::Sub:
    result = is_float ? state.builder->CreateFSub(lhs, rhs, "fsub")
                      : state.builder->CreateSub(lhs, rhs, "sub");
    break;
  case BinaryOp::Mul:
    result = is_float ? state.builder->CreateFMul(lhs, rhs, "fmul")
                      : state.builder->CreateMul(lhs, rhs, "mul");
    break;
  case BinaryOp::Div:
    if (is_float) {
      result = state.builder->CreateFDiv(lhs, rhs, "fdiv");
    } else if (is_unsigned_op) {
      result = state.builder->CreateUDiv(lhs, rhs, "udiv");
    } else {
      result = state.builder->CreateSDiv(lhs, rhs, "sdiv");
    }
    break;
  case BinaryOp::Mod:
    if (is_float) {
      result = state.builder->CreateFRem(lhs, rhs, "fmod");
    } else if (is_unsigned_op) {
      result = state.builder->CreateURem(lhs, rhs, "urem");
    } else {
      result = state.builder->CreateSRem(lhs, rhs, "srem");
    }
    break;

  // Comparisons
  case BinaryOp::EqEq:
    result = is_float ? state.builder->CreateFCmpOEQ(lhs, rhs, "feq")
                      : state.builder->CreateICmpEQ(lhs, rhs, "eq");
    break;
  case BinaryOp::BangEq:
    result = is_float ? state.builder->CreateFCmpONE(lhs, rhs, "fne")
                      : state.builder->CreateICmpNE(lhs, rhs, "ne");
    break;
  case BinaryOp::Lt:
    if (is_float) {
      result = state.builder->CreateFCmpOLT(lhs, rhs, "flt");
    } else if (is_unsigned_op) {
      result = state.builder->CreateICmpULT(lhs, rhs, "ult");
    } else {
      result = state.builder->CreateICmpSLT(lhs, rhs, "slt");
    }
    break;
  case BinaryOp::LtEq:
    if (is_float) {
      result = state.builder->CreateFCmpOLE(lhs, rhs, "fle");
    } else if (is_unsigned_op) {
      result = state.builder->CreateICmpULE(lhs, rhs, "ule");
    } else {
      result = state.builder->CreateICmpSLE(lhs, rhs, "sle");
    }
    break;
  case BinaryOp::Gt:
    if (is_float) {
      result = state.builder->CreateFCmpOGT(lhs, rhs, "fgt");
    } else if (is_unsigned_op) {
      result = state.builder->CreateICmpUGT(lhs, rhs, "ugt");
    } else {
      result = state.builder->CreateICmpSGT(lhs, rhs, "sgt");
    }
    break;
  case BinaryOp::GtEq:
    if (is_float) {
      result = state.builder->CreateFCmpOGE(lhs, rhs, "fge");
    } else if (is_unsigned_op) {
      result = state.builder->CreateICmpUGE(lhs, rhs, "uge");
    } else {
      result = state.builder->CreateICmpSGE(lhs, rhs, "sge");
    }
    break;

  // Boolean logic
  case BinaryOp::And:
    result = state.builder->CreateAnd(lhs, rhs, "and");
    break;
  case BinaryOp::Or:
    result = state.builder->CreateOr(lhs, rhs, "or");
    break;
  }

  if (result != nullptr) {
    state.values[inst.result.id] = result;
    state.value_types[inst.result.id] = inst.type;
  }
  return result != nullptr;
}

// ---------------------------------------------------------------------------
// Place resolution — walk a MirPlace projection chain to an LLVM pointer.
// ---------------------------------------------------------------------------

auto LlvmBackend::resolve_place(const MirPlace& place,
                                 const MirFunction& fn,
                                 FunctionState& state) -> llvm::Value* {
  auto it = state.locals.find(place.local.id);
  if (it == state.locals.end()) {
    return nullptr;
  }

  llvm::Value* ptr = it->second;

  for (const auto& proj : place.projections) {
    switch (proj.kind) {
    case MirProjectionKind::Deref: {
      auto* ptr_type = ptr->getType();
      // ptr is an alloca holding a pointer value — load the pointer.
      auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(ptr);
      if (alloca != nullptr) {
        ptr = state.builder->CreateLoad(alloca->getAllocatedType(), ptr,
                                        "deref.ptr");
      } else {
        // Already a raw pointer from a previous GEP.
        // We need the semantic type to know what to load — bail for now.
        return nullptr;
      }
      break;
    }
    case MirProjectionKind::Field: {
      // ptr points to a struct — GEP into the field.
      // Determine the struct's LLVM type from the alloca or previous GEP.
      llvm::Type* struct_type = nullptr;
      if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(ptr)) {
        struct_type = alloca->getAllocatedType();
      } else if (auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(ptr)) {
        struct_type = gep->getResultElementType();
      }
      if (struct_type == nullptr || !struct_type->isStructTy()) {
        return nullptr;
      }
      auto* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 0);
      auto* idx = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_),
                                          proj.field_index);
      ptr = state.builder->CreateInBoundsGEP(
          struct_type, ptr, {zero, idx},
          "field." + std::string(proj.field_name));
      break;
    }
    case MirProjectionKind::Index:
      // Not yet supported.
      return nullptr;
    }
  }

  return ptr;
}

// ---------------------------------------------------------------------------
// Memory operations
// ---------------------------------------------------------------------------

auto LlvmBackend::lower_store(const MirInst& inst, const MirFunction& fn,
                                FunctionState& state) -> bool {
  if (inst.place == nullptr) {
    emit_diagnostic(inst.span, "store with null place");
    return false;
  }

  auto* value = get_value(inst.store_value, state);
  if (value == nullptr) {
    emit_diagnostic(inst.span, "store value not found");
    return false;
  }

  auto it = state.locals.find(inst.place->local.id);
  if (it == state.locals.end()) {
    emit_diagnostic(inst.span, "store target local not found");
    return false;
  }

  if (!inst.place->projections.empty()) {
    auto* ptr = resolve_place(*inst.place, fn, state);
    if (ptr == nullptr) {
      emit_diagnostic(inst.span, "cannot resolve projected store target");
      return false;
    }
    state.builder->CreateStore(value, ptr);
    return true;
  }

  state.builder->CreateStore(value, it->second);
  return true;
}

auto LlvmBackend::lower_load(const MirInst& inst, const MirFunction& fn,
                               FunctionState& state) -> bool {
  if (inst.place == nullptr) {
    emit_diagnostic(inst.span, "load with null place");
    return false;
  }

  auto it = state.locals.find(inst.place->local.id);
  if (it == state.locals.end()) {
    emit_diagnostic(inst.span, "load source local not found");
    return false;
  }

  if (!inst.place->projections.empty()) {
    auto* ptr = resolve_place(*inst.place, fn, state);
    if (ptr == nullptr) {
      emit_diagnostic(inst.span, "cannot resolve projected load source");
      return false;
    }
    auto* load_type = types_.lower(inst.type);
    if (load_type == nullptr) {
      emit_diagnostic(inst.span,
                      "cannot lower projected load type: " + types_.error());
      return false;
    }
    auto* val = state.builder->CreateLoad(load_type, ptr, "proj.load");
    state.values[inst.result.id] = val;
    state.value_types[inst.result.id] = inst.type;
    return true;
  }

  auto* local_type = types_.lower(inst.type);
  if (local_type == nullptr) {
    emit_diagnostic(inst.span, "cannot lower load type: " + types_.error());
    return false;
  }

  auto* val = state.builder->CreateLoad(local_type, it->second, "load");
  state.values[inst.result.id] = val;
  state.value_types[inst.result.id] = inst.type;
  return true;
}

// ---------------------------------------------------------------------------
// Field access — extractvalue on struct SSA values
// ---------------------------------------------------------------------------

auto LlvmBackend::lower_field_access(const MirInst& inst,
                                      FunctionState& state) -> bool {
  auto* obj = get_value(inst.access_object, state);
  if (obj == nullptr) {
    emit_diagnostic(inst.span, "field access: object value not found");
    return false;
  }

  if (!obj->getType()->isStructTy()) {
    emit_diagnostic(inst.span, "field access: object is not a struct type");
    return false;
  }

  auto* val = state.builder->CreateExtractValue(
      obj, inst.access_field_index,
      "field." + std::string(inst.access_field));
  state.values[inst.result.id] = val;
  state.value_types[inst.result.id] = inst.type;
  return true;
}

// ---------------------------------------------------------------------------
// Function reference and calls
// ---------------------------------------------------------------------------

auto LlvmBackend::lower_fn_ref(const MirInst& inst, FunctionState& state) -> bool {
  if (inst.fn_symbol == nullptr) {
    emit_diagnostic(inst.span, "FnRef with null symbol");
    return false;
  }

  auto* fn = module_->getFunction(std::string(inst.fn_symbol->name));
  if (fn == nullptr) {
    emit_diagnostic(inst.span,
                    "function not found: " + std::string(inst.fn_symbol->name));
    return false;
  }

  state.values[inst.result.id] = fn;
  state.value_types[inst.result.id] = inst.type;
  return true;
}

auto LlvmBackend::lower_call(const MirInst& inst, FunctionState& state) -> bool {
  auto* callee_val = get_value(inst.callee, state);
  if (callee_val == nullptr) {
    emit_diagnostic(inst.span, "call callee not found");
    return false;
  }

  auto* callee_fn = llvm::dyn_cast<llvm::Function>(callee_val);
  if (callee_fn == nullptr) {
    emit_diagnostic(inst.span, "call callee is not a function");
    return false;
  }

  std::vector<llvm::Value*> args;
  if (inst.call_args != nullptr) {
    args.reserve(inst.call_args->size());
    for (size_t i = 0; i < inst.call_args->size(); ++i) {
      auto* arg_val = get_value((*inst.call_args)[i], state);
      if (arg_val == nullptr) {
        emit_diagnostic(inst.span, "call argument not found");
        return false;
      }
      // If the callee expects a pointer to string struct, create a
      // temporary alloca and pass its address.
      if (i < callee_fn->getFunctionType()->getNumParams()) {
        auto* param_type = callee_fn->getFunctionType()->getParamType(i);
        if (param_type->isPointerTy() && arg_val->getType() == types_.string_type()) {
          auto* tmp = state.builder->CreateAlloca(types_.string_type(), nullptr, "str.arg");
          state.builder->CreateStore(arg_val, tmp);
          arg_val = tmp;
        }
      }
      args.push_back(arg_val);
    }
  }

  auto* call = state.builder->CreateCall(callee_fn->getFunctionType(),
                                          callee_fn, args);
  if (!callee_fn->getReturnType()->isVoidTy() && inst.result.valid()) {
    state.values[inst.result.id] = call;
    state.value_types[inst.result.id] = inst.type;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Terminators
// ---------------------------------------------------------------------------

auto LlvmBackend::lower_return(const MirInst& inst, FunctionState& state) -> bool {
  if (inst.has_return_value) {
    auto* val = get_value(inst.return_value, state);
    if (val == nullptr) {
      emit_diagnostic(inst.span, "return value not found");
      return false;
    }
    state.builder->CreateRet(val);
  } else {
    state.builder->CreateRetVoid();
  }
  return true;
}

auto LlvmBackend::lower_br(const MirInst& inst, FunctionState& state) -> bool {
  auto it = state.blocks.find(inst.br_target.id);
  if (it == state.blocks.end()) {
    emit_diagnostic(inst.span, "branch target block not found");
    return false;
  }
  state.builder->CreateBr(it->second);
  return true;
}

auto LlvmBackend::lower_cond_br(const MirInst& inst, FunctionState& state) -> bool {
  auto* cond = get_value(inst.cond, state);
  if (cond == nullptr) {
    emit_diagnostic(inst.span, "conditional branch condition not found");
    return false;
  }

  auto then_it = state.blocks.find(inst.then_block.id);
  auto else_it = state.blocks.find(inst.else_block.id);
  if (then_it == state.blocks.end() || else_it == state.blocks.end()) {
    emit_diagnostic(inst.span, "conditional branch target block not found");
    return false;
  }

  state.builder->CreateCondBr(cond, then_it->second, else_it->second);
  return true;
}

} // namespace dao
