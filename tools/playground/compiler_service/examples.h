#ifndef DAO_PLAYGROUND_EXAMPLES_H
#define DAO_PLAYGROUND_EXAMPLES_H

#include <httplib.h>

#include <filesystem>

namespace dao::playground {

void handle_examples_list(const httplib::Request& req,
                          httplib::Response& res,
                          const std::filesystem::path& examples_dir);

void handle_example_get(const httplib::Request& req,
                        httplib::Response& res,
                        const std::filesystem::path& examples_dir);

} // namespace dao::playground

#endif // DAO_PLAYGROUND_EXAMPLES_H
