#include "analyze.h"
#include "examples.h"
#include "run.h"

#include <httplib.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

auto find_repo_root() -> std::filesystem::path {
  // DAO_SOURCE_DIR is baked in at compile time.
  return DAO_SOURCE_DIR;
}

} // namespace

auto main(int argc, char* argv[]) -> int {
  int port = 8090; // NOLINT(readability-magic-numbers)
  auto root = find_repo_root();

  // Simple arg parsing: --port N and --root DIR
  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (arg == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]); // NOLINT(cert-err34-c)
    } else if (arg == "--root" && i + 1 < argc) {
      root = argv[++i];
    }
  }

  auto examples_dir = root / "examples";
  auto frontend_dir = root / "tools" / "playground" / "frontend" / "dist";

  bool serve_frontend = std::filesystem::exists(frontend_dir);
  if (!serve_frontend) {
    std::cerr << "warning: frontend dist not found: " << frontend_dir << "\n";
    std::cerr << "  API endpoints will work; use Vite dev server for the UI.\n";
  }

  // Initialize LLVM targets once for /api/run.
  dao::playground::init_run_support();

  httplib::Server svr;

  // API endpoints.
  // NOLINTBEGIN(modernize-use-trailing-return-type)
  svr.Post("/api/analyze", [&root](const httplib::Request& req, httplib::Response& res) {
    dao::playground::handle_analyze(req, res, root);
  });

  svr.Get("/api/examples", [&examples_dir](const httplib::Request& req, httplib::Response& res) {
    dao::playground::handle_examples_list(req, res, examples_dir);
  });

  svr.Get("/api/examples/:name",
          [&examples_dir](const httplib::Request& req, httplib::Response& res) {
            dao::playground::handle_example_get(req, res, examples_dir);
          });

  svr.Post("/api/run", [&root](const httplib::Request& req, httplib::Response& res) {
    dao::playground::handle_run(req, res, root);
  });

  svr.Post("/api/hover", [&root](const httplib::Request& req, httplib::Response& res) {
    dao::playground::handle_hover(req, res, root);
  });

  svr.Post("/api/goto-def", [&root](const httplib::Request& req, httplib::Response& res) {
    dao::playground::handle_goto_def(req, res, root);
  });

  svr.Post("/api/document-symbols", [&root](const httplib::Request& req, httplib::Response& res) {
    dao::playground::handle_document_symbols(req, res, root);
  });

  svr.Post("/api/references", [&root](const httplib::Request& req, httplib::Response& res) {
    dao::playground::handle_references(req, res, root);
  });
  // NOLINTEND(modernize-use-trailing-return-type)

  // Serve frontend static files when dist/ exists (prod mode).
  if (serve_frontend) {
    auto index_path = frontend_dir / "index.html";
    svr.set_mount_point("/", frontend_dir.string());

    // Explicit root handler to prevent any redirect behavior.
    // NOLINTNEXTLINE(modernize-use-trailing-return-type)
    svr.Get("/", [index_path](const httplib::Request& /*req*/, httplib::Response& res) {
      std::ifstream file(index_path);
      if (!file) {
        res.status = 500; // NOLINT(readability-magic-numbers)
        res.set_content("index.html not found", "text/plain");
        return;
      }
      std::string body{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
      res.set_content(body, "text/html");
    });
  }

  std::cout << "Dao playground: http://localhost:" << port << "\n";
  std::cout << "  frontend: " << (serve_frontend ? frontend_dir.string() : "(dev mode — use Vite)") << "\n";
  std::cout << "  examples: " << examples_dir << "\n";

  if (!svr.listen("127.0.0.1", port)) {
    std::cerr << "error: failed to start server on port " << port << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
