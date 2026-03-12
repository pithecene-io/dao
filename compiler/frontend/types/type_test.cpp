#include "frontend/types/type.h"
#include "frontend/types/type_context.h"
#include "frontend/types/type_printer.h"

#include <boost/ut.hpp>

using namespace boost::ut;
using namespace dao;

namespace {

// Sentinel declaration identities for testing.
const int kDeclA = 1;
const int kDeclB = 2;

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

// ---------------------------------------------------------------------------
// Builtin types
// ---------------------------------------------------------------------------

suite<"type_builtin"> type_builtin = [] {
  "builtin types are non-null and have correct kind"_test = [] {
    TypeContext ctx;

    for (uint8_t i = 0; i < kBuiltinKindCount; ++i) {
      auto kind = static_cast<BuiltinKind>(i);
      auto* t = ctx.builtin(kind);
      expect(t != nullptr);
      expect(t->kind() == TypeKind::Builtin);
      expect(t->builtin() == kind);
    }
  };

  "requesting same builtin twice yields same pointer"_test = [] {
    TypeContext ctx;
    expect(ctx.i32() == ctx.i32());
    expect(ctx.f64() == ctx.f64());
    expect(ctx.bool_type() == ctx.bool_type());
  };

  "different builtins are distinct"_test = [] {
    TypeContext ctx;
    expect(ctx.i32() != ctx.f64());
    expect(ctx.i32() != ctx.u32());
  };
};

// ---------------------------------------------------------------------------
// Void type (not a builtin scalar)
// ---------------------------------------------------------------------------

suite<"type_void"> type_void = [] {
  "void_type is non-null and has Void kind"_test = [] {
    TypeContext ctx;
    auto* v = ctx.void_type();
    expect(v != nullptr);
    expect(v->kind() == TypeKind::Void);
  };

  "void_type is a singleton"_test = [] {
    TypeContext ctx;
    expect(ctx.void_type() == ctx.void_type());
  };

  "void is distinct from builtins"_test = [] {
    TypeContext ctx;
    // void_type() returns const TypeVoid*, builtins return const TypeBuiltin*.
    // Compare as const Type* to verify distinct identity.
    const Type* v = ctx.void_type();
    const Type* i = ctx.i32();
    const Type* b = ctx.bool_type();
    expect(v != i);
    expect(v != b);
  };
};

// ---------------------------------------------------------------------------
// Pointer types
// ---------------------------------------------------------------------------

suite<"type_pointer"> type_pointer = [] {
  "pointer_to canonicalizes"_test = [] {
    TypeContext ctx;
    auto* pi32a = ctx.pointer_to(ctx.i32());
    auto* pi32b = ctx.pointer_to(ctx.i32());
    expect(pi32a == pi32b) << "*i32 twice should yield same pointer";
  };

  "pointer_to different pointees are distinct"_test = [] {
    TypeContext ctx;
    auto* pi32 = ctx.pointer_to(ctx.i32());
    auto* pu32 = ctx.pointer_to(ctx.u32());
    expect(pi32 != pu32);
  };

  "nested pointer types"_test = [] {
    TypeContext ctx;
    auto* pi32 = ctx.pointer_to(ctx.i32());
    auto* ppi32 = ctx.pointer_to(pi32);
    expect(ppi32 != pi32);
    expect(ppi32->pointee() == pi32);
    expect(pi32->pointee() == ctx.i32());
  };
};

// ---------------------------------------------------------------------------
// Function types
// ---------------------------------------------------------------------------

suite<"type_function"> type_function = [] {
  "function type canonicalizes"_test = [] {
    TypeContext ctx;
    auto* fn1 = ctx.function_type({ctx.i32()}, ctx.i32());
    auto* fn2 = ctx.function_type({ctx.i32()}, ctx.i32());
    expect(fn1 == fn2) << "fn(i32): i32 twice should yield same pointer";
  };

  "function types with different return are distinct"_test = [] {
    TypeContext ctx;
    auto* fn1 = ctx.function_type({ctx.i32()}, ctx.i32());
    auto* fn2 = ctx.function_type({ctx.i32()}, ctx.f64());
    expect(fn1 != fn2);
  };

  "function types with different param count are distinct"_test = [] {
    TypeContext ctx;
    auto* fn1 = ctx.function_type({ctx.i32()}, ctx.bool_type());
    auto* fn2 = ctx.function_type({ctx.i32(), ctx.f64()}, ctx.bool_type());
    expect(fn1 != fn2);
  };

  "empty params function type"_test = [] {
    TypeContext ctx;
    auto* fn1 = ctx.function_type({}, ctx.void_type());
    auto* fn2 = ctx.function_type({}, ctx.void_type());
    expect(fn1 == fn2);
    expect(fn1->param_types().empty());
    expect(fn1->return_type() == ctx.void_type());
  };
};

// ---------------------------------------------------------------------------
// Named types
// ---------------------------------------------------------------------------

suite<"type_named"> type_named = [] {
  "named type with zero args"_test = [] {
    TypeContext ctx;
    auto* t1 = ctx.named_type(&kDeclA, "Point", {});
    auto* t2 = ctx.named_type(&kDeclA, "Point", {});
    expect(t1 == t2);
    expect(t1->name() == "Point");
    expect(t1->type_args().empty());
  };

  "named type with args canonicalizes"_test = [] {
    TypeContext ctx;
    auto* t1 = ctx.named_type(&kDeclA, "List", {ctx.i32()});
    auto* t2 = ctx.named_type(&kDeclA, "List", {ctx.i32()});
    expect(t1 == t2) << "List[i32] twice should yield same pointer";
  };

  "named types with different args are distinct"_test = [] {
    TypeContext ctx;
    auto* t1 = ctx.named_type(&kDeclA, "List", {ctx.i32()});
    auto* t2 = ctx.named_type(&kDeclA, "List", {ctx.f64()});
    expect(t1 != t2) << "List[i32] and List[f64] should be distinct";
  };

  "different decl_id with same name yields distinct types"_test = [] {
    TypeContext ctx;
    auto* t1 = ctx.named_type(&kDeclA, "Foo", {});
    auto* t2 = ctx.named_type(&kDeclB, "Foo", {});
    expect(t1 != t2) << "nominal identity is declaration-backed";
  };
};

// ---------------------------------------------------------------------------
// Generic parameters
// ---------------------------------------------------------------------------

suite<"type_generic_param"> type_generic_param = [] {
  "generic param creation"_test = [] {
    TypeContext ctx;
    auto* t = ctx.generic_param("T", 0);
    expect(t != nullptr);
    expect(t->name() == "T");
    expect(t->index() == 0u);
  };

  "same generic param canonicalizes"_test = [] {
    TypeContext ctx;
    auto* t1 = ctx.generic_param("T", 0);
    auto* t2 = ctx.generic_param("T", 0);
    expect(t1 == t2);
  };

  "different generic params are distinct"_test = [] {
    TypeContext ctx;
    auto* t1 = ctx.generic_param("T", 0);
    auto* t2 = ctx.generic_param("U", 1);
    expect(t1 != t2);
  };
};

// ---------------------------------------------------------------------------
// Struct and enum types (nominal — not interned)
// ---------------------------------------------------------------------------

suite<"type_struct_enum"> type_struct_enum = [] {
  "struct creation with fields"_test = [] {
    TypeContext ctx;
    auto* s = ctx.make_struct(&kDeclA, "Point",
                              {{"x", ctx.f64()}, {"y", ctx.f64()}});
    expect(s != nullptr);
    expect(s->kind() == TypeKind::Struct);
    expect(s->name() == "Point");
    expect(s->fields().size() == 2u);
    expect(s->fields()[0].name == "x");
    expect(s->fields()[0].type == ctx.f64());
  };

  "each make_struct call produces distinct object"_test = [] {
    TypeContext ctx;
    auto* s1 = ctx.make_struct(&kDeclA, "Point", {{"x", ctx.f64()}});
    auto* s2 = ctx.make_struct(&kDeclA, "Point", {{"x", ctx.f64()}});
    expect(s1 != s2) << "nominal types are not interned";
  };

  "enum creation with variants"_test = [] {
    TypeContext ctx;
    auto* e = ctx.make_enum(&kDeclA, "Option",
                            {{"None", {}}, {"Some", {ctx.i32()}}});
    expect(e != nullptr);
    expect(e->kind() == TypeKind::Enum);
    expect(e->name() == "Option");
    expect(e->variants().size() == 2u);
    expect(e->variants()[0].name == "None");
    expect(e->variants()[0].payload_types.empty());
    expect(e->variants()[1].name == "Some");
    expect(e->variants()[1].payload_types.size() == 1u);
  };
};

// ---------------------------------------------------------------------------
// Type printer
// ---------------------------------------------------------------------------

suite<"type_printer"> type_printer = [] {
  "print builtin"_test = [] {
    TypeContext ctx;
    expect(print_type(ctx.i32()) == "i32");
    expect(print_type(ctx.f64()) == "f64");
    expect(print_type(ctx.bool_type()) == "bool");
    expect(print_type(ctx.void_type()) == "void");
  };

  "print pointer"_test = [] {
    TypeContext ctx;
    expect(print_type(ctx.pointer_to(ctx.i32())) == "*i32");
  };

  "print function"_test = [] {
    TypeContext ctx;
    auto* fn = ctx.function_type({ctx.i32(), ctx.f64()}, ctx.bool_type());
    expect(print_type(fn) == "fn(i32, f64): bool");
  };

  "print empty-params function"_test = [] {
    TypeContext ctx;
    auto* fn = ctx.function_type({}, ctx.void_type());
    expect(print_type(fn) == "fn(): void");
  };

  "print named type without args"_test = [] {
    TypeContext ctx;
    auto* t = ctx.named_type(&kDeclA, "Point", {});
    expect(print_type(t) == "Point");
  };

  "print named type with args"_test = [] {
    TypeContext ctx;
    auto* t = ctx.named_type(&kDeclA, "List", {ctx.i32()});
    expect(print_type(t) == "List<i32>");
  };

  "print generic param"_test = [] {
    TypeContext ctx;
    auto* t = ctx.generic_param("T", 0);
    expect(print_type(t) == "T");
  };

  "print struct"_test = [] {
    TypeContext ctx;
    auto* s = ctx.make_struct(&kDeclA, "Point", {{"x", ctx.f64()}});
    expect(print_type(s) == "Point");
  };

  "print enum"_test = [] {
    TypeContext ctx;
    auto* e = ctx.make_enum(&kDeclA, "Color", {{"Red", {}}, {"Blue", {}}});
    expect(print_type(e) == "Color");
  };

  "print nested: pointer to function returning pointer"_test = [] {
    TypeContext ctx;
    auto* inner = ctx.function_type({ctx.i32()}, ctx.pointer_to(ctx.f64()));
    auto* outer = ctx.pointer_to(inner);
    expect(print_type(outer) == "*fn(i32): *f64");
  };

  "print null type"_test = [] {
    expect(print_type(nullptr) == "<null>");
  };
};

// ---------------------------------------------------------------------------
// Type kind name and builtin_kind_from_name
// ---------------------------------------------------------------------------

suite<"type_utilities"> type_utilities = [] {
  "type_kind_name covers all kinds"_test = [] {
    expect(std::string_view(type_kind_name(TypeKind::Builtin)) == "Builtin");
    expect(std::string_view(type_kind_name(TypeKind::Void)) == "Void");
    expect(std::string_view(type_kind_name(TypeKind::Pointer)) == "Pointer");
    expect(std::string_view(type_kind_name(TypeKind::Function)) == "Function");
    expect(std::string_view(type_kind_name(TypeKind::Named)) == "Named");
    expect(std::string_view(type_kind_name(TypeKind::GenericParam)) == "GenericParam");
    expect(std::string_view(type_kind_name(TypeKind::Struct)) == "Struct");
    expect(std::string_view(type_kind_name(TypeKind::Enum)) == "Enum");
  };

  "builtin_kind_from_name round-trips"_test = [] {
    for (uint8_t i = 0; i < kBuiltinKindCount; ++i) {
      auto kind = static_cast<BuiltinKind>(i);
      auto name = builtin_kind_name(kind);
      auto result = builtin_kind_from_name(name);
      expect(result.has_value()) << "should find " << name;
      expect(*result == kind);
    }
  };

  "builtin_kind_from_name returns nullopt for unknown"_test = [] {
    expect(!builtin_kind_from_name("string").has_value());
    expect(!builtin_kind_from_name("void").has_value()) << "void is not a builtin scalar";
    expect(!builtin_kind_from_name("int").has_value());
    expect(!builtin_kind_from_name("").has_value());
  };
};

// NOLINTEND(readability-magic-numbers)

auto main() -> int {} // NOLINT(readability-named-parameter)
