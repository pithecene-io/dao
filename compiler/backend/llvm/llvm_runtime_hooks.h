// NOLINTBEGIN(readability-identifier-length)
#ifndef DAO_BACKEND_LLVM_RUNTIME_HOOKS_H
#define DAO_BACKEND_LLVM_RUNTIME_HOOKS_H

// llvm_runtime_hooks.h — Centralized LLVM-side runtime hook declarations.
//
// This is the backend's single authoritative home for Dao runtime hook
// names and LLVM signatures. It must agree with:
//
//   - docs/contracts/CONTRACT_RUNTIME_ABI.md  (normative spec)
//   - runtime/core/dao_abi.h                  (C-side declarations)
//   - stdlib/core/*.dao                       (Dao extern declarations)
//
// Disagreement between any layer and the contract is a bug.

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

#include <string_view>

namespace dao {

class LlvmTypeLowering;

// ---------------------------------------------------------------------------
// Hook name constants — authoritative backend-side symbol names.
// ---------------------------------------------------------------------------

namespace runtime_hooks {

// IO domain
inline constexpr std::string_view kWriteStdout = "__dao_io_write_stdout";

// Equality domain
inline constexpr std::string_view kEqI32    = "__dao_eq_i32";
inline constexpr std::string_view kEqF64    = "__dao_eq_f64";
inline constexpr std::string_view kEqBool   = "__dao_eq_bool";
inline constexpr std::string_view kEqString = "__dao_eq_string";

// Conversion domain (to_string)
inline constexpr std::string_view kConvI32ToString  = "__dao_conv_i32_to_string";
inline constexpr std::string_view kConvF64ToString  = "__dao_conv_f64_to_string";
inline constexpr std::string_view kConvBoolToString = "__dao_conv_bool_to_string";

// Generator domain
inline constexpr std::string_view kGenAlloc = "__dao_gen_alloc";
inline constexpr std::string_view kGenFree  = "__dao_gen_free";

// Memory/resource domain
inline constexpr std::string_view kMemResourceEnter = "__dao_mem_resource_enter";
inline constexpr std::string_view kMemResourceExit  = "__dao_mem_resource_exit";

// String domain
inline constexpr std::string_view kStrConcat  = "__dao_str_concat";
inline constexpr std::string_view kStrLength  = "__dao_str_length";

// All hook names, for iteration / validation.
inline constexpr std::string_view kAllHooks[] = {
    kWriteStdout,
    kEqI32,    kEqF64,    kEqBool,    kEqString,
    kConvI32ToString, kConvF64ToString, kConvBoolToString,
    kGenAlloc, kGenFree,
    kMemResourceEnter, kMemResourceExit,
    kStrConcat, kStrLength,
};

} // namespace runtime_hooks

// ---------------------------------------------------------------------------
// LlvmRuntimeHooks — materializes runtime hook declarations in a module.
// ---------------------------------------------------------------------------

class LlvmRuntimeHooks {
public:
  explicit LlvmRuntimeHooks(llvm::Module& module, LlvmTypeLowering& types);

  // Ensure all runtime hooks are declared in the module.
  // Idempotent — safe to call multiple times.
  void declare_all();

  // Check whether a symbol name is a known runtime hook.
  static auto is_runtime_hook(std::string_view name) -> bool;

private:
  llvm::Module& module_;
  LlvmTypeLowering& types_;

  void declare_io_hooks();
  void declare_equality_hooks();
  void declare_conversion_hooks();
  void declare_generator_hooks();
  void declare_mem_resource_hooks();
  void declare_string_hooks();

  // Helper: get-or-create a function declaration.
  auto ensure_declared(std::string_view name, llvm::FunctionType* fn_type)
      -> llvm::Function*;
};

} // namespace dao

#endif // DAO_BACKEND_LLVM_RUNTIME_HOOKS_H
// NOLINTEND(readability-identifier-length)
