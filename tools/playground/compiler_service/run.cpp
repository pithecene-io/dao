// NOLINTBEGIN(readability-magic-numbers)
#include "run.h"

#include "backend/llvm/llvm_backend.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"
#include "frontend/resolve/resolve.h"
#include "frontend/typecheck/type_checker.h"
#include "frontend/types/type_context.h"
#include "ir/hir/hir_builder.h"
#include "ir/hir/hir_context.h"
#include "ir/mir/mir_builder.h"
#include "ir/mir/mir_context.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Program.h>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace dao::playground {

// ---------------------------------------------------------------------------
// Prelude loading (mirrors driver logic)
// ---------------------------------------------------------------------------

namespace {

auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  if (!file) {
    return {};
  }
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

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
    prelude += read_file(entry.path());
    prelude += '\n';
  }
  return prelude;
}

// Collect diagnostics into JSON, adjusting for prelude offset/lines.
void collect_diagnostics(nlohmann::json& out, const SourceBuffer& source,
                         const std::vector<Diagnostic>& diags,
                         uint32_t prelude_bytes,
                         uint32_t prelude_lines) {
  for (const auto& diag : diags) {
    // Skip prelude-origin diagnostics.
    if (diag.span.offset < prelude_bytes) {
      continue;
    }
    auto loc = source.line_col(diag.span.offset);
    auto line =
        loc.line > prelude_lines ? loc.line - prelude_lines : loc.line;
    const auto* severity =
        diag.severity == Severity::Warning ? "warning" : "error";
    out.push_back({
        {"severity", severity},
        {"offset", diag.span.offset - prelude_bytes},
        {"length", diag.span.length},
        {"line", line},
        {"col", loc.col},
        {"message", diag.message},
    });
  }
}

// Check whether any diagnostic originates from user code.
auto has_user_error(const std::vector<Diagnostic>& diags,
                    uint32_t prelude_bytes) -> bool {
  for (const auto& diag : diags) {
    if (diag.span.offset >= prelude_bytes) {
      return true;
    }
  }
  return false;
}

// Read entire file into string, for capturing process output.
auto slurp(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void init_run_support() { LlvmBackend::initialize_targets(); }

void handle_run(const httplib::Request& req, httplib::Response& res,
                const std::filesystem::path& repo_root) {
  nlohmann::json request;
  try {
    request = nlohmann::json::parse(req.body);
  } catch (const nlohmann::json::parse_error&) {
    res.status = 400;
    res.set_content(R"({"error":"invalid JSON"})", "application/json");
    return;
  }

  if (!request.contains("source") || !request["source"].is_string()) {
    res.status = 400;
    res.set_content(R"({"error":"missing 'source' field"})",
                    "application/json");
    return;
  }

  nlohmann::json diagnostics = nlohmann::json::array();

  // Load prelude and prepend to user source.
  auto prelude_source = load_prelude(repo_root);
  auto prelude_bytes = static_cast<uint32_t>(prelude_source.size());
  uint32_t prelude_lines = 0;
  for (char chr : prelude_source) {
    if (chr == '\n') {
      ++prelude_lines;
    }
  }
  auto user_source = request["source"].get<std::string>();
  auto combined = prelude_source + user_source;

  SourceBuffer source("<playground>", std::move(combined));
  auto lex_result = lex(source);

  collect_diagnostics(diagnostics, source, lex_result.diagnostics,
                      prelude_bytes, prelude_lines);
  if (has_user_error(lex_result.diagnostics, prelude_bytes)) {
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  auto parse_result = parse(lex_result.tokens);
  collect_diagnostics(diagnostics, source, parse_result.diagnostics,
                      prelude_bytes, prelude_lines);
  if (has_user_error(parse_result.diagnostics, prelude_bytes) ||
      parse_result.file == nullptr) {
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  auto resolve_result = resolve(*parse_result.file, prelude_bytes);
  collect_diagnostics(diagnostics, source, resolve_result.diagnostics,
                      prelude_bytes, prelude_lines);
  if (has_user_error(resolve_result.diagnostics, prelude_bytes)) {
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  TypeContext types;
  auto check_result =
      typecheck(*parse_result.file, resolve_result, types);
  bool has_errors = false;
  for (const auto& diag : check_result.diagnostics) {
    if (diag.span.offset >= prelude_bytes &&
        diag.severity == Severity::Error) {
      has_errors = true;
    }
  }
  collect_diagnostics(diagnostics, source, check_result.diagnostics,
                      prelude_bytes, prelude_lines);
  if (has_errors) {
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  HirContext hir_ctx;
  auto hir_result = build_hir(*parse_result.file, resolve_result,
                               check_result, hir_ctx);
  collect_diagnostics(diagnostics, source, hir_result.diagnostics,
                      prelude_bytes, prelude_lines);
  if (hir_result.module == nullptr) {
    if (diagnostics.empty()) {
      diagnostics.push_back({
          {"severity", "error"}, {"offset", 0}, {"length", 0},
          {"line", 1}, {"col", 1},
          {"message", "HIR lowering failed (possible prelude error)"},
      });
    }
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  MirContext mir_ctx;
  auto mir_result =
      build_mir(*hir_result.module, mir_ctx, types);
  collect_diagnostics(diagnostics, source, mir_result.diagnostics,
                      prelude_bytes, prelude_lines);
  if (mir_result.module == nullptr) {
    if (diagnostics.empty()) {
      diagnostics.push_back({
          {"severity", "error"}, {"offset", 0}, {"length", 0},
          {"line", 1}, {"col", 1},
          {"message", "MIR lowering failed (possible prelude error)"},
      });
    }
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  // LLVM lowering.
  llvm::LLVMContext llvm_ctx;
  LlvmBackend backend(llvm_ctx);
  auto llvm_result = backend.lower(*mir_result.module, prelude_bytes);

  // Filter prelude-origin warnings (same as driver).
  std::vector<Diagnostic> user_diags;
  for (const auto& diag : llvm_result.diagnostics) {
    if (diag.severity == Severity::Warning &&
        diag.span.offset < prelude_bytes) {
      continue;
    }
    user_diags.push_back(diag);
  }
  collect_diagnostics(diagnostics, source, user_diags, prelude_bytes,
                      prelude_lines);

  has_errors = false;
  for (const auto& diag : user_diags) {
    if (diag.severity == Severity::Error) {
      has_errors = true;
    }
  }
  if (llvm_result.module == nullptr || has_errors) {
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  // Emit object file in a per-request temp directory.
  static std::atomic<uint64_t> request_id{0};
  auto run_id = std::to_string(request_id.fetch_add(1));
  auto tmp_dir =
      std::filesystem::temp_directory_path() / "dao_playground" / run_id;
  std::filesystem::create_directories(tmp_dir);
  auto obj_path = tmp_dir / "playground.o";
  auto exe_path = tmp_dir / "playground_exe";

  std::string emit_error;
  if (!LlvmBackend::emit_object(*llvm_result.module,
                                 obj_path.string(), emit_error)) {
    diagnostics.push_back({
        {"severity", "error"},
        {"offset", 0},
        {"length", 0},
        {"line", 1},
        {"col", 1},
        {"message", "emit object failed: " + emit_error},
    });
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  // Link: cc obj + runtime → executable.
  auto cc_path = llvm::sys::findProgramByName("cc");
  if (!cc_path) {
    std::filesystem::remove(obj_path);
    diagnostics.push_back({
        {"severity", "error"},
        {"offset", 0},
        {"length", 0},
        {"line", 1},
        {"col", 1},
        {"message", "cannot find 'cc' linker"},
    });
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  auto obj_str = obj_path.string();
  auto exe_str = exe_path.string();
  std::vector<llvm::StringRef> link_args = {
      *cc_path, obj_str, DAO_RUNTIME_LIB, "-o", exe_str,
  };

  std::string link_error;
  int link_status = llvm::sys::ExecuteAndWait(
      *cc_path, link_args, /*Env=*/std::nullopt,
      /*Redirects=*/{}, /*SecondsToWait=*/30, /*MemoryLimit=*/0,
      &link_error);
  std::filesystem::remove(obj_path);

  if (link_status != 0) {
    std::filesystem::remove(exe_path);
    std::string msg = "linking failed";
    if (!link_error.empty()) {
      msg += ": " + link_error;
    }
    diagnostics.push_back({
        {"severity", "error"},
        {"offset", 0},
        {"length", 0},
        {"line", 1},
        {"col", 1},
        {"message", msg},
    });
    nlohmann::json response = {
        {"stdout", ""},
        {"stderr", ""},
        {"exit_code", -1},
        {"diagnostics", diagnostics},
    };
    res.set_content(response.dump(), "application/json");
    return;
  }

  // Execute the program with stdout/stderr capture and timeout.
  auto stdout_path = tmp_dir / "stdout.txt";
  auto stderr_path = tmp_dir / "stderr.txt";

  // Redirects: stdin=none, stdout=file, stderr=file.
  // StringRef does not own data — keep string temporaries alive.
  auto stdout_str = stdout_path.string();
  auto stderr_str = stderr_path.string();
  std::array<std::optional<llvm::StringRef>, 3> redirects = {{
      llvm::StringRef(""),
      llvm::StringRef(stdout_str),
      llvm::StringRef(stderr_str),
  }};

  std::string exec_error;
  int exit_code = llvm::sys::ExecuteAndWait(
      exe_str, {exe_str}, /*Env=*/std::nullopt,
      redirects, /*SecondsToWait=*/5,
      /*MemoryLimit=*/256 * 1024 * 1024, &exec_error);

  auto stdout_text = slurp(stdout_path);
  auto stderr_text = slurp(stderr_path);

  // Append execution error info if process was killed.
  if (!exec_error.empty()) {
    if (!stderr_text.empty()) {
      stderr_text += "\n";
    }
    stderr_text += exec_error;
  }

  // Clean up per-request directory.
  std::error_code ec;
  std::filesystem::remove_all(tmp_dir, ec);

  nlohmann::json response = {
      {"stdout", stdout_text},
      {"stderr", stderr_text},
      {"exit_code", exit_code},
      {"diagnostics", diagnostics},
  };
  res.set_content(response.dump(), "application/json");
}

} // namespace dao::playground
// NOLINTEND(readability-magic-numbers)
