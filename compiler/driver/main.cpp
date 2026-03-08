#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

auto main(int argc, char* argv[]) -> int {
  if (argc < 2) {
    std::cerr << "usage: daoc <file>\n";
    return EXIT_FAILURE;
  }

  std::filesystem::path path{argv[1]};

  if (!std::filesystem::exists(path)) {
    std::cerr << "error: file not found: " << path << "\n";
    return EXIT_FAILURE;
  }

  std::ifstream file{path};
  if (!file) {
    std::cerr << "error: could not open: " << path << "\n";
    return EXIT_FAILURE;
  }

  std::string contents{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

  return EXIT_SUCCESS;
}
