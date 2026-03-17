// NOLINTBEGIN(readability-identifier-length)

#include "backend/llvm/llvm_runtime_hooks.h"

#include "backend/llvm/llvm_type_lowering.h"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>

#include <algorithm>
#include <string>

namespace dao {

LlvmRuntimeHooks::LlvmRuntimeHooks(llvm::Module& module,
                                     LlvmTypeLowering& types)
    : module_(module), types_(types) {}

void LlvmRuntimeHooks::declare_all() {
  declare_io_hooks();
  declare_equality_hooks();
  declare_conversion_hooks();
  declare_generator_hooks();
  declare_mem_resource_hooks();
  declare_string_hooks();
}

auto LlvmRuntimeHooks::is_runtime_hook(std::string_view name) -> bool {
  return std::any_of(
      std::begin(runtime_hooks::kAllHooks),
      std::end(runtime_hooks::kAllHooks),
      [&](std::string_view hook) { return hook == name; });
}

// ---------------------------------------------------------------------------
// IO hooks
// ---------------------------------------------------------------------------

void LlvmRuntimeHooks::declare_io_hooks() {
  auto& ctx = module_.getContext();

  // __dao_io_write_stdout(msg: *dao.string): void
  auto* str_ptr = llvm::PointerType::getUnqual(types_.string_type());
  auto* fn_type = llvm::FunctionType::get(
      llvm::Type::getVoidTy(ctx), {str_ptr}, /*isVarArg=*/false);
  ensure_declared(runtime_hooks::kWriteStdout, fn_type);
}

// ---------------------------------------------------------------------------
// Equality hooks
// ---------------------------------------------------------------------------

void LlvmRuntimeHooks::declare_equality_hooks() {
  auto& ctx = module_.getContext();
  auto* i1 = llvm::Type::getInt1Ty(ctx);

  // __dao_eq_i32(a: i32, b: i32): bool
  auto* i32 = llvm::Type::getInt32Ty(ctx);
  ensure_declared(runtime_hooks::kEqI32,
                  llvm::FunctionType::get(i1, {i32, i32}, false));

  // __dao_eq_f64(a: f64, b: f64): bool
  auto* f64 = llvm::Type::getDoubleTy(ctx);
  ensure_declared(runtime_hooks::kEqF64,
                  llvm::FunctionType::get(i1, {f64, f64}, false));

  // __dao_eq_bool(a: bool, b: bool): bool
  ensure_declared(runtime_hooks::kEqBool,
                  llvm::FunctionType::get(i1, {i1, i1}, false));

  // __dao_eq_string(a: *dao.string, b: *dao.string): bool
  auto* str_ptr = llvm::PointerType::getUnqual(types_.string_type());
  ensure_declared(runtime_hooks::kEqString,
                  llvm::FunctionType::get(i1, {str_ptr, str_ptr}, false));
}

// ---------------------------------------------------------------------------
// Conversion hooks
// ---------------------------------------------------------------------------

void LlvmRuntimeHooks::declare_conversion_hooks() {
  auto& ctx = module_.getContext();
  auto* str_type = types_.string_type();

  // __dao_conv_i32_to_string(x: i32): dao.string
  auto* i32 = llvm::Type::getInt32Ty(ctx);
  ensure_declared(runtime_hooks::kConvI32ToString,
                  llvm::FunctionType::get(str_type, {i32}, false));

  // __dao_conv_f64_to_string(x: f64): dao.string
  auto* f64 = llvm::Type::getDoubleTy(ctx);
  ensure_declared(runtime_hooks::kConvF64ToString,
                  llvm::FunctionType::get(str_type, {f64}, false));

  // __dao_conv_bool_to_string(x: bool): dao.string
  auto* i1 = llvm::Type::getInt1Ty(ctx);
  ensure_declared(runtime_hooks::kConvBoolToString,
                  llvm::FunctionType::get(str_type, {i1}, false));
}

// ---------------------------------------------------------------------------
// Generator hooks
// ---------------------------------------------------------------------------

void LlvmRuntimeHooks::declare_generator_hooks() {
  auto& ctx = module_.getContext();
  auto* ptr = llvm::PointerType::getUnqual(ctx);
  auto* i64 = llvm::Type::getInt64Ty(ctx);
  auto* void_ty = llvm::Type::getVoidTy(ctx);

  // __dao_gen_alloc(size: i64, align: i64): ptr
  ensure_declared(runtime_hooks::kGenAlloc,
                  llvm::FunctionType::get(ptr, {i64, i64}, false));

  // __dao_gen_free(ptr): void
  ensure_declared(runtime_hooks::kGenFree,
                  llvm::FunctionType::get(void_ty, {ptr}, false));
}

// ---------------------------------------------------------------------------
// Memory/resource domain hooks
// ---------------------------------------------------------------------------

void LlvmRuntimeHooks::declare_mem_resource_hooks() {
  auto& ctx = module_.getContext();
  auto* ptr = llvm::PointerType::getUnqual(ctx);
  auto* void_ty = llvm::Type::getVoidTy(ctx);

  // __dao_mem_resource_enter(): ptr
  ensure_declared(runtime_hooks::kMemResourceEnter,
                  llvm::FunctionType::get(ptr, {}, false));

  // __dao_mem_resource_exit(domain: ptr): void
  ensure_declared(runtime_hooks::kMemResourceExit,
                  llvm::FunctionType::get(void_ty, {ptr}, false));
}

// ---------------------------------------------------------------------------
// String hooks
// ---------------------------------------------------------------------------

void LlvmRuntimeHooks::declare_string_hooks() {
  auto* str_type = types_.string_type();
  auto* str_ptr = llvm::PointerType::getUnqual(str_type);

  // __dao_str_concat(a: *dao.string, b: *dao.string): dao.string
  ensure_declared(runtime_hooks::kStrConcat,
                  llvm::FunctionType::get(str_type, {str_ptr, str_ptr}, false));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

auto LlvmRuntimeHooks::ensure_declared(std::string_view name,
                                        llvm::FunctionType* fn_type)
    -> llvm::Function* {
  auto name_str = std::string(name);
  auto* existing = module_.getFunction(name_str);
  if (existing != nullptr) {
    return existing;
  }
  return llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                name_str, module_);
}

} // namespace dao

// NOLINTEND(readability-identifier-length)
