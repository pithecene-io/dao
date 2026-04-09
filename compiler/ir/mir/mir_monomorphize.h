#ifndef DAO_IR_MIR_MIR_MONOMORPHIZE_H
#define DAO_IR_MIR_MIR_MONOMORPHIZE_H

#include "ir/mir/mir.h"
#include "ir/mir/mir_context.h"
#include "frontend/resolve/symbol.h"
#include "frontend/types/type_context.h"
#include "frontend/diagnostics/diagnostic.h"

#include <unordered_map>
#include <vector>

namespace dao {

struct MonomorphizeResult {
  std::vector<Diagnostic> diagnostics;
};

/// Replace generic functions with concrete specializations based on
/// observed call-site types.  Generic templates are provided separately
/// from the module's function list (the MIR builder excludes them from
/// module->functions).  After this pass, no TypeGenericParam remains
/// in the module and the LLVM backend can lower all types.
auto monomorphize(
    MirModule& module, MirContext& ctx, TypeContext& types,
    const std::unordered_map<const Symbol*, MirFunction*>& generic_templates)
    -> MonomorphizeResult;

} // namespace dao

#endif // DAO_IR_MIR_MIR_MONOMORPHIZE_H
