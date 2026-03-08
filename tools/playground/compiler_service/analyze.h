#ifndef DAO_PLAYGROUND_ANALYZE_H
#define DAO_PLAYGROUND_ANALYZE_H

#include <httplib.h>

namespace dao::playground {

void handle_analyze(const httplib::Request& req, httplib::Response& res);

} // namespace dao::playground

#endif // DAO_PLAYGROUND_ANALYZE_H
