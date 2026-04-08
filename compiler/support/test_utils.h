#ifndef DAO_SUPPORT_TEST_UTILS_H
#define DAO_SUPPORT_TEST_UTILS_H

#include <filesystem>
#include <fstream>
#include <string>

namespace dao {

/// Read the entire contents of a file into a string.
/// Shared across test binaries to eliminate copy-pasted helpers.
inline auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace dao

#endif // DAO_SUPPORT_TEST_UTILS_H
