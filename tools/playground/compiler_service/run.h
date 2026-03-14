#ifndef DAO_PLAYGROUND_RUN_H
#define DAO_PLAYGROUND_RUN_H

#include <httplib.h>

#include <filesystem>

namespace dao::playground {

// Initialize LLVM targets. Must be called once before handle_run().
void init_run_support();

void handle_run(const httplib::Request& req, httplib::Response& res,
                const std::filesystem::path& repo_root);

} // namespace dao::playground

#endif // DAO_PLAYGROUND_RUN_H
