#ifndef DAO_FRONTEND_DIAGNOSTICS_DIAGNOSTIC_H
#define DAO_FRONTEND_DIAGNOSTICS_DIAGNOSTIC_H

#include "frontend/diagnostics/source.h"

#include <cstdint>
#include <string>
#include <utility>

namespace dao {

enum class Severity : std::uint8_t { Error, Warning, Note };

struct Diagnostic {
  Severity severity;
  Span span;
  std::string message;

  static auto error(Span span, std::string message) -> Diagnostic {
    return {.severity = Severity::Error, .span = span, .message = std::move(message)};
  }

  static auto warning(Span span, std::string message) -> Diagnostic {
    return {.severity = Severity::Warning, .span = span, .message = std::move(message)};
  }
};

} // namespace dao

#endif // DAO_FRONTEND_DIAGNOSTICS_DIAGNOSTIC_H
