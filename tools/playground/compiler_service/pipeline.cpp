#include "pipeline.h"

#include <algorithm>
#include <fstream>

namespace dao::playground {

namespace {

// Strip a leading `module <path>\n` line from a source snippet. The
// transitional playground pipeline concatenates stdlib files and user
// source into a single synthetic compilation unit with exactly one
// `module` declaration at the top, per CONTRACT_SYNTAX_SURFACE.md.
// Real multi-file compilation lands with Task 25+.
auto strip_leading_module(std::string_view src) -> std::string_view {
  size_t i = 0;
  while (i < src.size() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n')) {
    ++i;
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

} // namespace

auto load_prelude(const std::filesystem::path& repo_root) -> std::string {
  auto stdlib_core = repo_root / "stdlib" / "core";
  std::string prelude;
  if (!std::filesystem::exists(stdlib_core)) {
    return prelude;
  }
  // Collect and sort entries so prelude loading order is stable
  // and dependency-aware (e.g. option.dao before overflow.dao).
  std::vector<std::filesystem::path> paths;
  for (const auto& entry :
       std::filesystem::directory_iterator(stdlib_core)) {
    if (entry.path().extension() == ".dao") {
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end());
  for (const auto& p : paths) {
    std::ifstream file(p);
    if (!file) {
      continue;
    }
    std::string contents{std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>()};
    prelude.append(strip_leading_module(contents));
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
