#ifndef DAO_FRONTEND_TYPECHECK_TYPE_CONVERSION_H
#define DAO_FRONTEND_TYPECHECK_TYPE_CONVERSION_H

#include "frontend/types/type.h"

namespace dao {

// ---------------------------------------------------------------------------
// Assignability and type comparison.
//
// Initial policy: exact semantic type equality.
// No implicit widening, no implicit promotion.
// See CONTRACT_TYPECHECKING_BASELINE.md §4.
// ---------------------------------------------------------------------------

/// Returns true if `source` is assignable to `target`.
/// Current rule: exact pointer equality (canonical interned types).
auto is_assignable(const Type* source, const Type* target) -> bool;

/// Returns true if the type is a numeric builtin (integer or float).
auto is_numeric(const Type* type) -> bool;

/// Returns true if the type is an integer builtin.
auto is_integer(const Type* type) -> bool;

/// Returns true if the type is a float builtin.
auto is_float(const Type* type) -> bool;

} // namespace dao

#endif // DAO_FRONTEND_TYPECHECK_TYPE_CONVERSION_H
