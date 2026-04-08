#ifndef DAO_SUPPORT_ARENA_H
#define DAO_SUPPORT_ARENA_H

#include <cstddef>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace dao {

// ---------------------------------------------------------------------------
// Arena — bump allocator with destructor tracking.
//
// Owns all objects allocated through it. Non-trivially-destructible
// objects are destroyed in reverse allocation order when the arena is
// destroyed or reset. Pointers into the arena are stable for its
// lifetime.
//
// Intended for compiler-owned object graphs (AST nodes, semantic types,
// etc.) where individual deallocation is unnecessary.
// ---------------------------------------------------------------------------

class Arena {
public:
  Arena() = default;

  ~Arena() { destroy(); }

  Arena(const Arena&) = delete;
  auto operator=(const Arena&) -> Arena& = delete;

  Arena(Arena&& other) noexcept
      : blocks_(std::move(other.blocks_)),
        oversized_(std::move(other.oversized_)), offset_(other.offset_),
        dtors_(std::move(other.dtors_)) {
    other.blocks_.clear();
    other.oversized_.clear();
    other.offset_ = kBlockSize;
    other.dtors_.clear();
  }

  auto operator=(Arena&& other) noexcept -> Arena& {
    if (this != &other) {
      destroy();
      blocks_ = std::move(other.blocks_);
      oversized_ = std::move(other.oversized_);
      offset_ = other.offset_;
      dtors_ = std::move(other.dtors_);
      other.blocks_.clear();
      other.oversized_.clear();
      other.offset_ = kBlockSize;
      other.dtors_.clear();
    }
    return *this;
  }

  /// Construct a T in arena-owned memory. The returned pointer is stable
  /// for the lifetime of the arena. If T is non-trivially destructible,
  /// its destructor is called (in reverse allocation order) when the
  /// arena is destroyed.
  template <typename T, typename... Args>
  auto alloc(Args&&... args) -> T* {
    void* mem = allocate(sizeof(T), alignof(T));
    auto* ptr = new (mem) T(std::forward<Args>(args)...);
    if constexpr (!std::is_trivially_destructible_v<T>) {
      dtors_.push_back({&invoke_dtor<T>, ptr});
    }
    return ptr;
  }

private:
  static constexpr size_t kBlockSize = 4096;

  // Block data is max-aligned so that bump-pointer arithmetic within
  // the block can satisfy any fundamental alignment requirement.
  struct alignas(std::max_align_t) Block {
    char data[kBlockSize]; // NOLINT(modernize-avoid-c-arrays)
  };

  // Type-erased destructor — avoids std::function heap allocation.
  struct Dtor {
    void (*invoke)(void*);
    void* obj;
  };

  template <typename T>
  static void invoke_dtor(void* obj) {
    static_cast<T*>(obj)->~T();
  }

  // Oversized allocation record — stores both the pointer and the
  // alignment used for allocation, so destroy() can pass the matching
  // alignment to operator delete (mismatched new/delete alignment is UB).
  struct Oversized {
    void* ptr;
    std::align_val_t align;
  };

  std::vector<Block*> blocks_;
  std::vector<Oversized> oversized_;
  size_t offset_ = kBlockSize; // Force first allocation to create a block.
  std::vector<Dtor> dtors_;

  auto allocate(size_t size, size_t align) -> void* {
    // Align the bump pointer up to the requested alignment.
    offset_ = (offset_ + align - 1) & ~(align - 1);
    if (offset_ + size > kBlockSize) {
      if (size > kBlockSize) {
        // Oversized allocation — give it its own raw memory region.
        // Tracked separately from blocks to avoid type-punning UB.
        auto al = std::align_val_t(align);
        auto* mem = ::operator new(size, al);
        oversized_.push_back({mem, al});
        return mem;
      }
      blocks_.push_back(new Block);
      offset_ = 0;
      // Re-align within the fresh block (block base is max-aligned,
      // so this is a no-op for alignments ≤ alignof(max_align_t)).
      offset_ = (offset_ + align - 1) & ~(align - 1);
    }
    void* ptr =
        blocks_.back()->data + offset_; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    offset_ += size;
    return ptr;
  }

  void destroy() {
    for (auto& dtor : std::ranges::reverse_view(dtors_)) {
      dtor.invoke(dtor.obj);
    }
    for (auto* block : blocks_) {
      delete block;
    }
    for (auto& [ptr, align] : oversized_) {
      ::operator delete(ptr, align);
    }
    blocks_.clear();
    oversized_.clear();
    offset_ = kBlockSize;
    dtors_.clear();
  }
};

} // namespace dao

#endif // DAO_SUPPORT_ARENA_H
