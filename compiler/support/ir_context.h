#ifndef DAO_SUPPORT_IR_CONTEXT_H
#define DAO_SUPPORT_IR_CONTEXT_H

#include "support/arena.h"

#include <utility>

namespace dao {

/// Shared arena-owning context for IR nodes (HIR, MIR, or any future IR).
/// Each context owns an Arena that outlives all nodes allocated through it.
class IrContext {
public:
  IrContext() = default;
  ~IrContext() = default;

  IrContext(const IrContext&) = delete;
  auto operator=(const IrContext&) -> IrContext& = delete;
  IrContext(IrContext&&) noexcept = default;
  auto operator=(IrContext&&) noexcept -> IrContext& = default;

  template <typename T, typename... Args>
  auto alloc(Args&&... args) -> T* {
    return arena_.alloc<T>(std::forward<Args>(args)...);
  }

private:
  Arena arena_;
};

} // namespace dao

#endif // DAO_SUPPORT_IR_CONTEXT_H
