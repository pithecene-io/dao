#ifndef DAO_FRONTEND_DIAGNOSTICS_SOURCE_H
#define DAO_FRONTEND_DIAGNOSTICS_SOURCE_H

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dao {

struct Span {
  uint32_t offset = 0;
  uint32_t length = 0;
};

struct LineCol {
  uint32_t line = 1;
  uint32_t col = 1;
};

class SourceBuffer {
public:
  SourceBuffer(std::string filename, std::string contents)
      : filename_(std::move(filename)), contents_(std::move(contents)) {
    build_line_index();
  }

  [[nodiscard]] auto filename() const -> std::string_view {
    return filename_;
  }
  [[nodiscard]] auto contents() const -> std::string_view {
    return contents_;
  }
  [[nodiscard]] auto size() const -> uint32_t {
    return static_cast<uint32_t>(contents_.size());
  }

  [[nodiscard]] auto text(Span span) const -> std::string_view {
    return std::string_view(contents_).substr(span.offset, span.length);
  }

  [[nodiscard]] auto line_col(uint32_t offset) const -> LineCol {
    // Binary search for the line containing this offset.
    uint32_t low = 0;
    auto high = static_cast<uint32_t>(line_offsets_.size());
    while (low + 1 < high) {
      uint32_t mid = low + ((high - low) / 2);
      if (line_offsets_[mid] <= offset) {
        low = mid;
      } else {
        high = mid;
      }
    }
    return {.line = low + 1, .col = offset - line_offsets_[low] + 1};
  }

private:
  std::string filename_;
  std::string contents_;
  std::vector<uint32_t> line_offsets_;

  void build_line_index() {
    line_offsets_.push_back(0);
    for (uint32_t i = 0; i < contents_.size(); ++i) {
      if (contents_[i] == '\n' && i + 1 < contents_.size()) {
        line_offsets_.push_back(i + 1);
      }
    }
  }
};

} // namespace dao

#endif // DAO_FRONTEND_DIAGNOSTICS_SOURCE_H
