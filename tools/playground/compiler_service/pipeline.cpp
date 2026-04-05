#include "pipeline.h"

#include <algorithm>
#include <fstream>

namespace dao::playground {

namespace {

// Blank a leading `module <path>` line in place — overwrite the
// `module` keyword and its path segments with spaces, preserving the
// terminating newline, total byte count, and all offsets past the
// blanked region. The transitional playground/driver pipelines
// concatenate stdlib files and user source into a single synthetic
// compilation unit with exactly one `module` declaration at the top
// (the injected header), per CONTRACT_SYNTAX_SURFACE.md. Blanking
// (rather than stripping) is load-bearing for the playground: frontend
// editor offsets and backend source offsets must stay byte-identical
// so hover/goto/completion/references/diagnostics positions line up
// with the editor buffer the user sees. Real multi-file compilation
// lands with Task 25+.
void blank_leading_module(std::string& src) {
  size_t i = 0;
  // Skip whitespace and `//` line comments until we reach either the
  // leading `module` keyword or the first non-comment token. Dao only
  // has `//` line comments (see spec/grammar/dao.ebnf).
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

} // namespace

// Exposed to run.cpp / analyze.cpp so request handlers can blank a
// user-provided leading `module` header before concatenation without
// shifting any byte offsets. See the blank_leading_module rationale
// above for why blanking (not stripping) is required for editor/
// backend offset alignment.
void blank_user_leading_module(std::string& src) {
  blank_leading_module(src);
}

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
    blank_leading_module(contents);
    prelude.append(contents);
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
