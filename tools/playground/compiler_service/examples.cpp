#include "examples.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <ranges>
#include <string>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers)
namespace dao::playground {

void handle_examples_list(const httplib::Request& /*req*/,
                          httplib::Response& res,
                          const std::filesystem::path& examples_dir) {
  nlohmann::json examples = nlohmann::json::array();

  if (std::filesystem::exists(examples_dir)) {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(examples_dir)) {
      if (entry.path().extension() == ".dao") {
        names.push_back(entry.path().filename().string());
      }
    }
    std::ranges::sort(names);
    for (const auto& name : names) {
      examples.push_back({{"name", name}});
    }
  }

  nlohmann::json response = {{"examples", examples}};
  res.set_content(response.dump(), "application/json");
}

void handle_example_get(const httplib::Request& req,
                        httplib::Response& res,
                        const std::filesystem::path& examples_dir) {
  auto name = req.path_params.at("name");

  // Path traversal guard: name must end with .dao and contain no
  // slashes, backslashes, or parent references.
  if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
      name.find("..") != std::string::npos || name.size() < 5 ||
      name.substr(name.size() - 4) != ".dao") {
    res.status = 400;
    res.set_content(R"({"error":"invalid example name"})", "application/json");
    return;
  }

  auto path = examples_dir / name;
  if (!std::filesystem::exists(path)) {
    res.status = 404;
    res.set_content(R"({"error":"example not found"})", "application/json");
    return;
  }

  std::ifstream file(path);
  std::string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

  nlohmann::json response = {{"name", name}, {"source", contents}};
  res.set_content(response.dump(), "application/json");
}

// NOLINTEND(readability-magic-numbers)

} // namespace dao::playground
