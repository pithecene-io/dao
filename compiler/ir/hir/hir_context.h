#ifndef DAO_IR_HIR_HIR_CONTEXT_H
#define DAO_IR_HIR_HIR_CONTEXT_H

#include "ir/hir/hir.h"
#include "support/arena.h"

#include <utility>

namespace dao {

// ---------------------------------------------------------------------------
// HirContext — arena owner for all HIR nodes.
// ---------------------------------------------------------------------------

class HirContext {
public:
  HirContext() = default;
  ~HirContext() = default;

  HirContext(const HirContext&) = delete;
  auto operator=(const HirContext&) -> HirContext& = delete;
  HirContext(HirContext&&) noexcept = default;
  auto operator=(HirContext&&) noexcept -> HirContext& = default;

  template <typename T, typename... Args>
  auto alloc(Args&&... args) -> T* {
    return arena_.alloc<T>(std::forward<Args>(args)...);
  }

private:
  Arena arena_;
};

} // namespace dao

#endif // DAO_IR_HIR_HIR_CONTEXT_H
