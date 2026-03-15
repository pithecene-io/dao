#include "pipeline.h"

#include <algorithm>
#include <fstream>

namespace dao::playground {

auto load_prelude(const std::filesystem::path& repo_root) -> std::string {
  auto stdlib_core = repo_root / "stdlib" / "core";
  std::string prelude;
  if (!std::filesystem::exists(stdlib_core)) {
    return prelude;
  }
  for (const auto& entry :
       std::filesystem::directory_iterator(stdlib_core)) {
    if (entry.path().extension() != ".dao") {
      continue;
    }
    std::ifstream file(entry.path());
    if (!file) {
      continue;
    }
    prelude.append(std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>());
    prelude += '\n';
  }
  return prelude;
}

auto count_lines(const std::string& text) -> uint32_t {
  return static_cast<uint32_t>(
      std::count(text.begin(), text.end(), '\n'));
}

auto has_user_error(const std::vector<Diagnostic>& diags,
                    uint32_t prelude_bytes) -> bool {
  return std::ranges::any_of(diags, [prelude_bytes](const auto& diag) {
    return diag.span.offset >= prelude_bytes;
  });
}

void collect_diagnostics(nlohmann::json& out, const SourceBuffer& source,
                         const std::vector<Diagnostic>& diags,
                         uint32_t prelude_bytes, uint32_t prelude_lines) {
  for (const auto& diag : diags) {
    if (diag.span.offset < prelude_bytes) {
      continue;
    }
    auto loc = source.line_col(diag.span.offset);
    auto line =
        loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
    out.push_back({
        {"severity",
         diag.severity == Severity::Warning ? "warning" : "error"},
        {"offset", diag.span.offset - prelude_bytes},
        {"length", diag.span.length},
        {"line", line},
        {"col", loc.col},
        {"message", diag.message},
    });
  }
}

auto make_internal_error(const std::string& message) -> nlohmann::json {
  return {
      {"severity", "error"},
      {"offset", 0},
      {"length", 0},
      {"line", 1},
      {"col", 1},
      {"message", message},
  };
}

} // namespace dao::playground
