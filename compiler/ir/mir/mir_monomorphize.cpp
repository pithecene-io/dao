// NOLINTBEGIN(readability-magic-numbers,readability-identifier-length)
#include "ir/mir/mir_monomorphize.h"

#include "frontend/types/type.h"
#include "frontend/types/type_printer.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace dao {

namespace {

// ---------------------------------------------------------------------------
// Type substitution: replace TypeGenericParam with concrete types.
// ---------------------------------------------------------------------------

using TypeSubst = std::unordered_map<uint32_t, const Type*>;

auto substitute_type(const Type* type, const TypeSubst& subst,
                     TypeContext& types) -> const Type* {
  if (type == nullptr) {
    return nullptr;
  }

  switch (type->kind()) {
  case TypeKind::GenericParam: {
    const auto* gp = static_cast<const TypeGenericParam*>(type);
    auto it = subst.find(gp->index());
    if (it != subst.end()) {
      return it->second;
    }
    return type; // unbound — leave as-is (shouldn't happen in practice)
  }

  case TypeKind::Function: {
    const auto* fn = static_cast<const TypeFunction*>(type);
    std::vector<const Type*> params;
    params.reserve(fn->param_types().size());
    bool changed = false;
    for (const auto* param : fn->param_types()) {
      auto* sub = substitute_type(param, subst, types);
      if (sub != param) {
        changed = true;
      }
      params.push_back(sub);
    }
    auto* ret = substitute_type(fn->return_type(), subst, types);
    if (ret != fn->return_type()) {
      changed = true;
    }
    if (!changed) {
      return type;
    }
    return types.function_type(std::move(params), ret);
  }

  case TypeKind::Pointer: {
    const auto* ptr = static_cast<const TypePointer*>(type);
    auto* sub = substitute_type(ptr->pointee(), subst, types);
    if (sub == ptr->pointee()) {
      return type;
    }
    return types.pointer_to(sub);
  }

  case TypeKind::Generator: {
    const auto* gen = static_cast<const TypeGenerator*>(type);
    auto* sub = substitute_type(gen->yield_type(), subst, types);
    if (sub == gen->yield_type()) {
      return type;
    }
    return types.generator_type(sub);
  }

  default:
    return type;
  }
}

// ---------------------------------------------------------------------------
// Detect generic functions: any function whose locals, return type,
// or instruction types contain TypeGenericParam.
// ---------------------------------------------------------------------------

auto type_has_generic(const Type* type) -> bool {
  if (type == nullptr) {
    return false;
  }
  switch (type->kind()) {
  case TypeKind::GenericParam:
    return true;
  case TypeKind::Function: {
    const auto* fn = static_cast<const TypeFunction*>(type);
    for (const auto* param : fn->param_types()) {
      if (type_has_generic(param)) {
        return true;
      }
    }
    return type_has_generic(fn->return_type());
  }
  case TypeKind::Pointer:
    return type_has_generic(
        static_cast<const TypePointer*>(type)->pointee());
  case TypeKind::Generator:
    return type_has_generic(
        static_cast<const TypeGenerator*>(type)->yield_type());
  default:
    return false;
  }
}

auto is_generic_function(const MirFunction* fn) -> bool {
  if (type_has_generic(fn->return_type)) {
    return true;
  }
  for (const auto& local : fn->locals) {
    if (type_has_generic(local.type)) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Mangled name for specializations: "name$i32" or "name$i32_f64".
// ---------------------------------------------------------------------------

auto mangle_name(std::string_view base,
                 const std::vector<const Type*>& type_args) -> std::string {
  std::string result(base);
  result += '$';
  for (size_t i = 0; i < type_args.size(); ++i) {
    if (i > 0) {
      result += '_';
    }
    result += print_type(type_args[i]);
  }
  return result;
}

// ---------------------------------------------------------------------------
// Specialization key for deduplication.
// ---------------------------------------------------------------------------

struct SpecKey {
  const MirFunction* generic_fn;
  std::vector<const Type*> type_args;

  auto operator==(const SpecKey& other) const -> bool {
    return generic_fn == other.generic_fn && type_args == other.type_args;
  }
};

struct SpecKeyHash {
  auto operator()(const SpecKey& key) const -> size_t {
    size_t h = std::hash<const void*>{}(key.generic_fn);
    for (const auto* type : key.type_args) {
      h ^= std::hash<const void*>{}(type) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
  }
};

// ---------------------------------------------------------------------------
// Clone a MirFunction with type substitution.
// ---------------------------------------------------------------------------

auto clone_function(const MirFunction* src, const TypeSubst& subst,
                    const Symbol* new_symbol, MirContext& ctx,
                    TypeContext& types) -> MirFunction* {
  auto* dst = ctx.alloc<MirFunction>();
  dst->symbol = new_symbol;
  dst->return_type = substitute_type(src->return_type, subst, types);
  dst->span = src->span;
  dst->is_extern = src->is_extern;

  // Clone locals with substituted types.
  dst->locals.reserve(src->locals.size());
  for (const auto& local : src->locals) {
    MirLocal cloned = local;
    cloned.type = substitute_type(local.type, subst, types);
    dst->locals.push_back(cloned);
  }

  // Clone blocks and instructions.
  dst->blocks.reserve(src->blocks.size());
  for (const auto* src_block : src->blocks) {
    auto* dst_block = ctx.alloc<MirBlock>();
    dst_block->id = src_block->id;

    dst_block->insts.reserve(src_block->insts.size());
    for (const auto* src_inst : src_block->insts) {
      auto* dst_inst = ctx.alloc<MirInst>();
      dst_inst->result = src_inst->result;
      dst_inst->type = substitute_type(src_inst->type, subst, types);
      dst_inst->span = src_inst->span;

      // Deep-copy payload, handling heap-allocated members.
      dst_inst->payload = src_inst->payload;

      // For Call and Construct, the args/field_values vectors need
      // to be cloned since they're heap-allocated.
      if (auto* call = std::get_if<MirCall>(&dst_inst->payload)) {
        if (call->args != nullptr) {
          auto* new_args = ctx.alloc<std::vector<MirValueId>>(*call->args);
          call->args = new_args;
        }
      } else if (auto* ctor = std::get_if<MirConstruct>(&dst_inst->payload)) {
        if (ctor->field_values != nullptr) {
          auto* new_fv =
              ctx.alloc<std::vector<MirValueId>>(*ctor->field_values);
          ctor->field_values = new_fv;
        }
      } else if (auto* store = std::get_if<MirStore>(&dst_inst->payload)) {
        if (store->place != nullptr) {
          store->place = ctx.alloc<MirPlace>(*store->place);
        }
      } else if (auto* load = std::get_if<MirLoad>(&dst_inst->payload)) {
        if (load->place != nullptr) {
          load->place = ctx.alloc<MirPlace>(*load->place);
        }
      } else if (auto* addr = std::get_if<MirAddrOf>(&dst_inst->payload)) {
        if (addr->place != nullptr) {
          addr->place = ctx.alloc<MirPlace>(*addr->place);
        }
      }

      dst_block->insts.push_back(dst_inst);
    }

    dst->blocks.push_back(dst_block);
  }

  return dst;
}

// ---------------------------------------------------------------------------
// Infer type substitution from a call site.
// ---------------------------------------------------------------------------

auto infer_substitution(const MirFunction* generic_fn,
                        const std::vector<const Type*>& arg_types)
    -> TypeSubst {
  TypeSubst subst;
  size_t param_count = 0;
  for (const auto& local : generic_fn->locals) {
    if (!local.is_param) {
      break;
    }
    if (param_count < arg_types.size() &&
        local.type != nullptr &&
        local.type->kind() == TypeKind::GenericParam) {
      const auto* gp = static_cast<const TypeGenericParam*>(local.type);
      subst[gp->index()] = arg_types[param_count];
    }
    ++param_count;
  }
  return subst;
}

// Extract ordered type args from a substitution map.
auto subst_to_type_args(const TypeSubst& subst) -> std::vector<const Type*> {
  if (subst.empty()) {
    return {};
  }
  uint32_t max_idx = 0;
  for (const auto& [idx, _] : subst) {
    if (idx > max_idx) {
      max_idx = idx;
    }
  }
  std::vector<const Type*> args(max_idx + 1, nullptr);
  for (const auto& [idx, type] : subst) {
    args[idx] = type;
  }
  return args;
}

} // namespace

// ---------------------------------------------------------------------------
// Main pass
// ---------------------------------------------------------------------------

auto monomorphize(MirModule& module, MirContext& ctx,
                  TypeContext& types) -> MonomorphizeResult {
  MonomorphizeResult result;

  // Phase 1: identify generic functions by symbol.
  std::unordered_map<const Symbol*, MirFunction*> generic_fns;
  for (auto* fn : module.functions) {
    if (is_generic_function(fn)) {
      generic_fns[fn->symbol] = fn;
    }
  }

  if (generic_fns.empty()) {
    return result;
  }

  // Specialization cache: (generic fn, type args) → specialized fn.
  std::unordered_map<SpecKey, MirFunction*, SpecKeyHash> spec_cache;

  // Arena-owned name strings for mangled symbols.
  // (Stored as char arrays in MirContext arena.)

  // Phase 2+3+4: iterate until no new specializations are produced.
  bool changed = true;
  while (changed) {
    changed = false;

    for (auto* fn : module.functions) {
      // Skip generic functions themselves — they'll be removed later.
      if (generic_fns.count(fn->symbol) != 0) {
        continue;
      }

      for (auto* block : fn->blocks) {
        for (size_t inst_idx = 0; inst_idx < block->insts.size();
             ++inst_idx) {
          auto* inst = block->insts[inst_idx];
          auto* fn_ref = std::get_if<MirFnRef>(&inst->payload);
          if (fn_ref == nullptr) {
            continue;
          }

          auto git = generic_fns.find(fn_ref->symbol);
          if (git == generic_fns.end()) {
            continue;
          }

          // Found a call to a generic function. Find the matching
          // MirCall instruction that uses this FnRef.
          const MirCall* call_payload = nullptr;
          for (size_t j = inst_idx + 1; j < block->insts.size(); ++j) {
            auto* candidate = std::get_if<MirCall>(
                &block->insts[j]->payload);
            if (candidate != nullptr &&
                candidate->callee.id == inst->result.id) {
              call_payload = candidate;
              break;
            }
          }

          if (call_payload == nullptr || call_payload->args == nullptr) {
            continue;
          }

          // Collect argument types from the call.
          std::vector<const Type*> arg_types;
          arg_types.reserve(call_payload->args->size());
          for (auto arg_id : *call_payload->args) {
            // Find the instruction that produces this value to
            // get its type.
            const Type* arg_type = nullptr;
            for (const auto* blk : fn->blocks) {
              for (const auto* search_inst : blk->insts) {
                if (search_inst->result.id == arg_id.id) {
                  arg_type = search_inst->type;
                  break;
                }
              }
              if (arg_type != nullptr) {
                break;
              }
            }
            arg_types.push_back(arg_type);
          }

          // Infer substitution from argument types.
          auto subst = infer_substitution(git->second, arg_types);
          if (subst.empty()) {
            continue;
          }

          auto type_args = subst_to_type_args(subst);
          SpecKey key{git->second, type_args};

          // Check cache.
          auto cache_it = spec_cache.find(key);
          if (cache_it != spec_cache.end()) {
            // Rewrite the FnRef to point at the cached specialization.
            fn_ref->symbol = cache_it->second->symbol;
            inst->type = substitute_type(inst->type, subst, types);
            continue;
          }

          // Create mangled symbol.
          // Allocate name string on arena so it outlives this scope.
          auto* name_str = ctx.alloc<std::string>(
              mangle_name(git->second->symbol->name, type_args));

          auto* new_sym = ctx.alloc<Symbol>();
          new_sym->kind = SymbolKind::Function;
          new_sym->name = std::string_view(*name_str);
          new_sym->decl_span = git->second->symbol->decl_span;
          new_sym->decl = git->second->symbol->decl;

          // Clone the generic function with type substitution.
          auto* specialized =
              clone_function(git->second, subst, new_sym, ctx, types);

          // Register in cache and add to module.
          spec_cache[key] = specialized;
          module.functions.push_back(specialized);

          // Rewrite the FnRef.
          fn_ref->symbol = new_sym;
          inst->type = substitute_type(inst->type, subst, types);

          changed = true;
        }
      }
    }
  }

  // Phase 5: remove generic originals.
  std::erase_if(module.functions, [&](const MirFunction* fn) {
    return generic_fns.count(fn->symbol) != 0;
  });

  return result;
}

} // namespace dao
// NOLINTEND(readability-magic-numbers,readability-identifier-length)
