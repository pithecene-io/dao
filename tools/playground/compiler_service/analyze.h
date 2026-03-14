#ifndef DAO_PLAYGROUND_ANALYZE_H
#define DAO_PLAYGROUND_ANALYZE_H

#include <httplib.h>

#include <filesystem>

namespace dao::playground {

void handle_analyze(const httplib::Request& req, httplib::Response& res,
                    const std::filesystem::path& repo_root);

} // namespace dao::playground

#endif // DAO_PLAYGROUND_ANALYZE_H
