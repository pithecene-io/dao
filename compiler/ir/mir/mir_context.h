#ifndef DAO_IR_MIR_MIR_CONTEXT_H
#define DAO_IR_MIR_MIR_CONTEXT_H

#include "support/arena.h"

#include <utility>

namespace dao {

// ---------------------------------------------------------------------------
// MirContext — arena owner for all MIR nodes.
// ---------------------------------------------------------------------------

class MirContext {
public:
  MirContext() = default;
  ~MirContext() = default;

  MirContext(const MirContext&) = delete;
  auto operator=(const MirContext&) -> MirContext& = delete;
  MirContext(MirContext&&) noexcept = default;
  auto operator=(MirContext&&) noexcept -> MirContext& = default;

  template <typename T, typename... Args>
  auto alloc(Args&&... args) -> T* {
    return arena_.alloc<T>(std::forward<Args>(args)...);
  }

private:
  Arena arena_;
};

} // namespace dao

#endif // DAO_IR_MIR_MIR_CONTEXT_H
