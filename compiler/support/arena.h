#ifndef DAO_SUPPORT_ARENA_H
#define DAO_SUPPORT_ARENA_H

#include <cstddef>
#include <functional>
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
      : blocks_(std::move(other.blocks_)), offset_(other.offset_),
        dtors_(std::move(other.dtors_)) {
    other.blocks_.clear();
    other.offset_ = kBlockSize;
    other.dtors_.clear();
  }

  auto operator=(Arena&& other) noexcept -> Arena& {
    if (this != &other) {
      destroy();
      blocks_ = std::move(other.blocks_);
      offset_ = other.offset_;
      dtors_ = std::move(other.dtors_);
      other.blocks_.clear();
      other.offset_ = kBlockSize;
      other.dtors_.clear();
    }
    return *this;
  }

  // Construct a T in arena-owned memory. The returned pointer is stable
  // for the lifetime of the arena. If T is non-trivially destructible,
  // its destructor is called (in reverse allocation order) when the
  // arena is destroyed.
  template <typename T, typename... Args>
  auto alloc(Args&&... args) -> T* {
    void* mem = allocate(sizeof(T), alignof(T));
    auto* ptr = new (mem) T(std::forward<Args>(args)...);
    if constexpr (!std::is_trivially_destructible_v<T>) {
      dtors_.push_back([ptr]() { ptr->~T(); }); // NOLINT(modernize-use-trailing-return-type)
    }
    return ptr;
  }

private:
  static constexpr size_t kBlockSize = 4096;

  struct Block {
    char data[kBlockSize]; // NOLINT(modernize-avoid-c-arrays)
  };

  std::vector<Block*> blocks_;
  size_t offset_ = kBlockSize; // Force first allocation to create a block.
  std::vector<std::function<void()>> dtors_;

  auto allocate(size_t size, size_t align) -> void* {
    // Align up.
    offset_ = (offset_ + align - 1) & ~(align - 1);
    if (offset_ + size > kBlockSize) {
      if (size > kBlockSize) {
        // Oversized allocation — give it its own block.
        auto* mem = ::operator new(size);
        blocks_.push_back(static_cast<Block*>(mem));
        return mem;
      }
      blocks_.push_back(new Block);
      offset_ = 0;
    }
    void* ptr =
        blocks_.back()->data + offset_; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    offset_ += size;
    return ptr;
  }

  void destroy() {
    for (auto& dtor : std::ranges::reverse_view(dtors_)) {
      dtor();
    }
    for (auto* block : blocks_) {
      ::operator delete(block);
    }
    blocks_.clear();
    offset_ = kBlockSize;
    dtors_.clear();
  }
};

} // namespace dao

#endif // DAO_SUPPORT_ARENA_H
