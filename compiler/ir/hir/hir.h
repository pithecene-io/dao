#ifndef DAO_IR_HIR_HIR_H
#define DAO_IR_HIR_HIR_H

#include "frontend/ast/ast.h"
#include "frontend/diagnostics/source.h"
#include "frontend/resolve/symbol.h"
#include "frontend/types/type.h"
#include "ir/hir/hir_kind.h"
#include "support/variant.h"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace dao {

// Forward declarations for recursive references.
struct HirDecl;
struct HirStmt;
struct HirExpr;

// ---------------------------------------------------------------------------
// Shared types
// ---------------------------------------------------------------------------

struct HirParam {
  const Symbol* symbol;
  const Type* type;
  Span span;
};

enum class HirModeKind : std::uint8_t {
  Unsafe,
  Gpu,
  Parallel,
  Other,
};

auto hir_mode_kind_from_name(std::string_view name) -> HirModeKind;
auto hir_mode_kind_name(HirModeKind kind) -> const char*;

// ---------------------------------------------------------------------------
// Declaration payloads
// ---------------------------------------------------------------------------

struct HirFunction {
  const Symbol* symbol;
  std::vector<HirParam> params;
  const Type* return_type;
  std::vector<HirStmt*> body;
  bool is_extern;
};

struct HirClassDecl {
  const Symbol* symbol;
  const TypeStruct* struct_type;
};

using HirDeclPayload = std::variant<HirFunction, HirClassDecl>;

// ---------------------------------------------------------------------------
// Statement payloads
// ---------------------------------------------------------------------------

struct HirLet {
  const Symbol* symbol;
  const Type* type;
  HirExpr* initializer; // nullable
};

struct HirAssign {
  HirExpr* target;
  HirExpr* value;
};

struct HirIf {
  HirExpr* condition;
  std::vector<HirStmt*> then_body;
  std::vector<HirStmt*> else_body;
};

struct HirWhile {
  HirExpr* condition;
  std::vector<HirStmt*> body;
};

struct HirFor {
  const Symbol* var_symbol;
  HirExpr* iterable;
  std::vector<HirStmt*> body;
};

struct HirReturn {
  HirExpr* value; // nullable for bare return
};

struct HirExprStmt {
  HirExpr* expr;
};

struct HirMode {
  HirModeKind mode;
  std::string_view mode_name;
  std::vector<HirStmt*> body;
};

struct HirResource {
  std::string_view resource_kind;
  std::string_view resource_name;
  std::vector<HirStmt*> body;
};

using HirStmtPayload = std::variant<
    HirLet, HirAssign, HirIf, HirWhile, HirFor,
    HirReturn, HirExprStmt, HirMode, HirResource>;

// ---------------------------------------------------------------------------
// Expression payloads
// ---------------------------------------------------------------------------

struct HirIntLiteral   { int64_t value; };
struct HirFloatLiteral { double value; };
struct HirStringLiteral { std::string_view value; };
struct HirBoolLiteral  { bool value; };
struct HirSymbolRef    { const Symbol* symbol; };

struct HirUnary {
  UnaryOp op;
  HirExpr* operand;
};

struct HirBinary {
  BinaryOp op;
  HirExpr* left;
  HirExpr* right;
};

struct HirCall {
  HirExpr* callee;
  std::vector<HirExpr*> args;
};

struct HirConstruct {
  const TypeStruct* struct_type;
  std::vector<HirExpr*> args;
};

struct HirField {
  HirExpr* object;
  std::string_view field_name;
};

struct HirIndex {
  HirExpr* object;
  std::vector<HirExpr*> indices;
};

struct HirPipe {
  HirExpr* left;
  HirExpr* right;
};

struct HirLambda {
  std::vector<HirParam> params;
  HirExpr* body;
};

using HirExprPayload = std::variant<
    HirIntLiteral, HirFloatLiteral, HirStringLiteral, HirBoolLiteral,
    HirSymbolRef, HirUnary, HirBinary, HirCall, HirConstruct,
    HirField, HirIndex, HirPipe, HirLambda>;

// ---------------------------------------------------------------------------
// Container nodes — arena-allocated.
//
// Each container holds a span, a typed payload variant, and (for exprs)
// a semantic type. The kind() method derives HirKind from the active
// variant alternative. as<T>() / is<T>() provide typed access.
// ---------------------------------------------------------------------------

struct HirDecl {
  Span span;
  HirDeclPayload payload;

  [[nodiscard]] auto kind() const -> HirKind;

  template <typename T>
  [[nodiscard]] auto as() const -> const T& {
    return std::get<T>(payload);
  }
  template <typename T>
  [[nodiscard]] auto is() const -> bool {
    return std::holds_alternative<T>(payload);
  }
};

struct HirStmt {
  Span span;
  HirStmtPayload payload;

  [[nodiscard]] auto kind() const -> HirKind;

  template <typename T>
  [[nodiscard]] auto as() const -> const T& {
    return std::get<T>(payload);
  }
  template <typename T>
  [[nodiscard]] auto is() const -> bool {
    return std::holds_alternative<T>(payload);
  }
};

struct HirExpr {
  Span span;
  const Type* type;
  HirExprPayload payload;

  [[nodiscard]] auto kind() const -> HirKind;

  template <typename T>
  [[nodiscard]] auto as() const -> const T& {
    return std::get<T>(payload);
  }
  template <typename T>
  [[nodiscard]] auto is() const -> bool {
    return std::holds_alternative<T>(payload);
  }
};

// ---------------------------------------------------------------------------
// Module — top-level container.
// ---------------------------------------------------------------------------

struct HirModule {
  Span span;
  std::vector<HirDecl*> declarations;
};

} // namespace dao

#endif // DAO_IR_HIR_HIR_H
