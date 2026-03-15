#ifndef DAO_PLAYGROUND_PIPELINE_H
#define DAO_PLAYGROUND_PIPELINE_H

#include "frontend/diagnostics/diagnostic.h"
#include "frontend/diagnostics/source.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dao::playground {

// ---------------------------------------------------------------------------
// Shared prelude and diagnostic utilities for playground endpoints.
// ---------------------------------------------------------------------------

/// Load all .dao files from stdlib/core/ and concatenate them.
auto load_prelude(const std::filesystem::path& repo_root) -> std::string;

/// Count newlines in a string.
auto count_lines(const std::string& text) -> uint32_t;

/// True if any diagnostic has span offset >= prelude_bytes.
auto has_user_error(const std::vector<Diagnostic>& diags,
                    uint32_t prelude_bytes) -> bool;

/// Append user-visible diagnostics to a JSON array, adjusting spans
/// for prelude offset. Skips prelude-origin diagnostics.
void collect_diagnostics(nlohmann::json& out, const SourceBuffer& source,
                         const std::vector<Diagnostic>& diags,
                         uint32_t prelude_bytes, uint32_t prelude_lines);

/// Build a synthetic error diagnostic entry for when a phase fails
/// with no user-visible diagnostics (possible prelude error).
auto make_internal_error(const std::string& message) -> nlohmann::json;

} // namespace dao::playground

#endif // DAO_PLAYGROUND_PIPELINE_H
