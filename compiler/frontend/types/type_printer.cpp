#include "frontend/types/type_printer.h"

#include "frontend/types/type.h"

#include <sstream>

namespace dao {

void print_type(std::ostream& out, const Type* type) {
  if (type == nullptr) {
    out << "<null>";
    return;
  }

  switch (type->kind()) {
  case TypeKind::Builtin: {
    const auto* b = static_cast<const TypeBuiltin*>(type);
    out << builtin_kind_name(b->builtin());
    break;
  }
  case TypeKind::Void:
    out << "void";
    break;
  case TypeKind::Pointer: {
    const auto* p = static_cast<const TypePointer*>(type);
    out << '*';
    print_type(out, p->pointee());
    break;
  }
  case TypeKind::Function: {
    const auto* f = static_cast<const TypeFunction*>(type);
    out << "fn(";
    bool first = true;
    for (const auto* param : f->param_types()) {
      if (!first) out << ", ";
      first = false;
      print_type(out, param);
    }
    out << "): ";
    print_type(out, f->return_type());
    break;
  }
  case TypeKind::Named: {
    const auto* n = static_cast<const TypeNamed*>(type);
    out << n->name();
    if (!n->type_args().empty()) {
      out << '[';
      bool first = true;
      for (const auto* arg : n->type_args()) {
        if (!first) out << ", ";
        first = false;
        print_type(out, arg);
      }
      out << ']';
    }
    break;
  }
  case TypeKind::GenericParam: {
    const auto* g = static_cast<const TypeGenericParam*>(type);
    out << g->name();
    break;
  }
  case TypeKind::Struct: {
    const auto* s = static_cast<const TypeStruct*>(type);
    out << s->name();
    break;
  }
  case TypeKind::Enum: {
    const auto* e = static_cast<const TypeEnum*>(type);
    out << e->name();
    break;
  }
  }
}

auto print_type(const Type* type) -> std::string {
  std::ostringstream out;
  print_type(out, type);
  return out.str();
}

} // namespace dao
