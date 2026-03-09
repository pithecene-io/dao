#ifndef DAO_FRONTEND_TYPES_TYPE_PRINTER_H
#define DAO_FRONTEND_TYPES_TYPE_PRINTER_H

#include <ostream>
#include <string>

namespace dao {

class Type;

auto print_type(const Type* type) -> std::string;
void print_type(std::ostream& out, const Type* type);

} // namespace dao

#endif // DAO_FRONTEND_TYPES_TYPE_PRINTER_H
