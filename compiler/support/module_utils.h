#ifndef DAO_SUPPORT_MODULE_UTILS_H
#define DAO_SUPPORT_MODULE_UTILS_H

#include <string>
#include <string_view>

namespace dao {

/// Blank a leading `module <path>` line in place — overwrite the
/// `module` keyword and path segments with spaces, preserving the
/// terminating newline and total byte count. Skips leading whitespace
/// and `//` line comments (Dao's only comment form per
/// spec/grammar/dao.ebnf). Used by the driver and playground to fold
/// per-file module headers into a single synthetic `module` declaration
/// without shifting any byte offsets.
inline void blank_leading_module(std::string& src) {
  size_t i = 0;
  while (i < src.size()) {
    char ch = src[i];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      ++i;
      continue;
    }
    if (ch == '/' && i + 1 < src.size() && src[i + 1] == '/') {
      auto nl = src.find('\n', i);
      if (nl == std::string::npos) {
        return;
      }
      i = nl + 1;
      continue;
    }
    break;
  }
  if (i + 6 >= src.size() || src.compare(i, 6, "module") != 0) {
    return;
  }
  char after = src[i + 6];
  if (after != ' ' && after != '\t') {
    return;
  }
  auto nl = src.find('\n', i);
  auto end = (nl == std::string::npos) ? src.size() : nl;
  for (size_t j = i; j < end; ++j) {
    src[j] = ' ';
  }
}

/// Strip a leading `module <path>` line (and any preceding blank/comment
/// lines) from a source snippet. Returns a view past the stripped line.
/// Same skip logic as blank_leading_module but returns a substring
/// instead of modifying in place. Used by test helpers that concatenate
/// prelude sources.
inline auto strip_leading_module(std::string_view src) -> std::string_view {
  size_t i = 0;
  while (i < src.size()) {
    char ch = src[i];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      ++i;
      continue;
    }
    if (ch == '/' && i + 1 < src.size() && src[i + 1] == '/') {
      auto nl = src.find('\n', i);
      if (nl == std::string_view::npos) {
        return src.substr(0, 0);
      }
      i = nl + 1;
      continue;
    }
    break;
  }
  if (i + 6 >= src.size() || src.substr(i, 6) != "module") {
    return src;
  }
  char after = src[i + 6];
  if (after != ' ' && after != '\t') {
    return src;
  }
  auto nl = src.find('\n', i);
  if (nl == std::string_view::npos) {
    return src.substr(0, 0);
  }
  return src.substr(nl + 1);
}

/// Detect whether source begins with a `module` declaration (after
/// optional whitespace and `//` comments). Used by test helpers to
/// decide whether to auto-wrap with `module test`.
inline auto starts_with_module(std::string_view src) -> bool {
  size_t i = 0;
  while (i < src.size()) {
    char ch = src[i];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      ++i;
      continue;
    }
    if (ch == '/' && i + 1 < src.size() && src[i + 1] == '/') {
      auto nl = src.find('\n', i);
      if (nl == std::string_view::npos) {
        return false;
      }
      i = nl + 1;
      continue;
    }
    break;
  }
  if (i + 6 >= src.size() || src.substr(i, 6) != "module") {
    return false;
  }
  char after = src[i + 6];
  return after == ' ' || after == '\t';
}

} // namespace dao

#endif // DAO_SUPPORT_MODULE_UTILS_H
