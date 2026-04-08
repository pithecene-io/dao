#ifndef DAO_SUPPORT_TEST_UTILS_H
#define DAO_SUPPORT_TEST_UTILS_H

#include "support/module_utils.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace dao {

/// Read the entire contents of a file into a string.
/// Shared across test binaries to eliminate copy-pasted helpers.
inline auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// Prefix for the synthetic module declaration injected by test helpers.
inline constexpr const char* kTestModulePrefix = "module test\n";
inline constexpr uint32_t kTestModulePrefixBytes = 12;

/// Wrap a test source string with a synthetic `module test` declaration.
/// Idempotent: sources that already begin with `module` (after optional
/// whitespace/comments) are returned unchanged.
inline auto wrap_with_test_module(std::string_view src) -> std::string {
  if (starts_with_module(src)) {
    return std::string(src);
  }
  std::string wrapped = kTestModulePrefix;
  wrapped.append(src);
  return wrapped;
}

} // namespace dao

#endif // DAO_SUPPORT_TEST_UTILS_H
