// NOLINTBEGIN(readability-magic-numbers)
#include "pipeline.h"
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
#include "ir/mir/mir_monomorphize.h"

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

  // Per CONTRACT_SYNTAX_SURFACE.md every source file must begin with
  // exactly one `module` declaration. The playground wraps the
  // combined (prelude + user) source with a single synthetic
  // `module playground` declaration. Both the synthetic module line
  // and the stdlib prelude are folded into `prelude_bytes` so that
  // user-visible diagnostic offsets remain zero-based from the user's
  // code. Any `module` header the user supplied (e.g. by loading one
  // of the migrated example files) is blanked in place — the bytes
  // become spaces so the parser ignores them, but the user source's
  // total byte count and every offset past the blanked region stay
  // identical to the frontend editor buffer. This keeps hover,
  // go-to-definition, completions, references, semantic tokens, and
  // diagnostic positions aligned with the editor. Real multi-file
  // compilation lands with Task 25+.
  const std::string module_header = "module playground\n";
  auto prelude_source = load_prelude(repo_root);
  auto user_source = request["source"].get<std::string>();
  blank_user_leading_module(user_source);

  std::string combined;
  combined.reserve(module_header.size() + prelude_source.size() +
                   user_source.size());
  combined.append(module_header);
  combined.append(prelude_source);
  auto prelude_bytes = static_cast<uint32_t>(combined.size());
  auto prelude_lines = count_lines(combined);
  combined.append(user_source);

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
      diagnostics.push_back(
          make_internal_error("HIR lowering failed (possible prelude error)"));
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
  if (mir_result.module != nullptr) {
    auto mono = monomorphize(*mir_result.module, mir_ctx, types);
    collect_diagnostics(diagnostics, source, mono.diagnostics,
                        prelude_bytes, prelude_lines);
  }
  if (mir_result.module == nullptr) {
    if (diagnostics.empty()) {
      diagnostics.push_back(
          make_internal_error("MIR lowering failed (possible prelude error)"));
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
