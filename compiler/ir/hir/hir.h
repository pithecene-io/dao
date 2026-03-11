#ifndef DAO_IR_HIR_HIR_H
#define DAO_IR_HIR_HIR_H

#include "frontend/ast/ast.h"
#include "frontend/diagnostics/source.h"
#include "frontend/resolve/symbol.h"
#include "frontend/types/type.h"
#include "ir/hir/hir_kind.h"

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace dao {

// ---------------------------------------------------------------------------
// Base classes
// ---------------------------------------------------------------------------

class HirNode {
public:
  explicit HirNode(HirKind kind, Span span) : kind_(kind), span_(span) {}
  virtual ~HirNode() = default;

  HirNode(const HirNode&) = delete;
  auto operator=(const HirNode&) -> HirNode& = delete;
  HirNode(HirNode&&) = delete;
  auto operator=(HirNode&&) -> HirNode& = delete;

  [[nodiscard]] auto kind() const -> HirKind { return kind_; }
  [[nodiscard]] auto span() const -> Span { return span_; }

private:
  HirKind kind_;
  Span span_;
};

class HirDecl : public HirNode {
public:
  using HirNode::HirNode;
};

class HirStmt : public HirNode {
public:
  using HirNode::HirNode;
};

class HirExpr : public HirNode {
public:
  HirExpr(HirKind kind, Span span, const Type* type)
      : HirNode(kind, span), type_(type) {}

  [[nodiscard]] auto type() const -> const Type* { return type_; }

private:
  const Type* type_;
};

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

class HirModule : public HirNode {
public:
  explicit HirModule(Span span, std::vector<HirDecl*> declarations)
      : HirNode(HirKind::Module, span),
        declarations_(std::move(declarations)) {}

  [[nodiscard]] auto declarations() const -> const std::vector<HirDecl*>& {
    return declarations_;
  }

private:
  std::vector<HirDecl*> declarations_;
};

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

struct HirParam {
  const Symbol* symbol;
  const Type* type;
  Span span;
};

class HirFunction : public HirDecl {
public:
  HirFunction(Span span, const Symbol* symbol, std::vector<HirParam> params,
              const Type* return_type, std::vector<HirStmt*> body,
              bool is_extern = false)
      : HirDecl(HirKind::Function, span), symbol_(symbol),
        params_(std::move(params)), return_type_(return_type),
        body_(std::move(body)), is_extern_(is_extern) {}

  [[nodiscard]] auto symbol() const -> const Symbol* { return symbol_; }
  [[nodiscard]] auto params() const -> const std::vector<HirParam>& {
    return params_;
  }
  [[nodiscard]] auto return_type() const -> const Type* {
    return return_type_;
  }
  [[nodiscard]] auto body() const -> const std::vector<HirStmt*>& {
    return body_;
  }
  [[nodiscard]] auto is_extern() const -> bool { return is_extern_; }

private:
  const Symbol* symbol_;
  std::vector<HirParam> params_;
  const Type* return_type_;
  std::vector<HirStmt*> body_;
  bool is_extern_ = false;
};

class HirStructDecl : public HirDecl {
public:
  HirStructDecl(Span span, const Symbol* symbol, const TypeStruct* type)
      : HirDecl(HirKind::StructDecl, span), symbol_(symbol), type_(type) {}

  [[nodiscard]] auto symbol() const -> const Symbol* { return symbol_; }
  [[nodiscard]] auto struct_type() const -> const TypeStruct* {
    return type_;
  }

private:
  const Symbol* symbol_;
  const TypeStruct* type_;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

class HirLet : public HirStmt {
public:
  HirLet(Span span, const Symbol* symbol, const Type* type,
         HirExpr* initializer)
      : HirStmt(HirKind::Let, span), symbol_(symbol), type_(type),
        initializer_(initializer) {}

  [[nodiscard]] auto symbol() const -> const Symbol* { return symbol_; }
  [[nodiscard]] auto type() const -> const Type* { return type_; }
  [[nodiscard]] auto initializer() const -> HirExpr* { return initializer_; }

private:
  const Symbol* symbol_;
  const Type* type_;
  HirExpr* initializer_; // nullable
};

class HirAssign : public HirStmt {
public:
  HirAssign(Span span, HirExpr* target, HirExpr* value)
      : HirStmt(HirKind::Assign, span), target_(target), value_(value) {}

  [[nodiscard]] auto target() const -> HirExpr* { return target_; }
  [[nodiscard]] auto value() const -> HirExpr* { return value_; }

private:
  HirExpr* target_;
  HirExpr* value_;
};

class HirIf : public HirStmt {
public:
  HirIf(Span span, HirExpr* condition, std::vector<HirStmt*> then_body,
        std::vector<HirStmt*> else_body)
      : HirStmt(HirKind::If, span), condition_(condition),
        then_body_(std::move(then_body)),
        else_body_(std::move(else_body)) {}

  [[nodiscard]] auto condition() const -> HirExpr* { return condition_; }
  [[nodiscard]] auto then_body() const -> const std::vector<HirStmt*>& {
    return then_body_;
  }
  [[nodiscard]] auto else_body() const -> const std::vector<HirStmt*>& {
    return else_body_;
  }

private:
  HirExpr* condition_;
  std::vector<HirStmt*> then_body_;
  std::vector<HirStmt*> else_body_;
};

class HirWhile : public HirStmt {
public:
  HirWhile(Span span, HirExpr* condition, std::vector<HirStmt*> body)
      : HirStmt(HirKind::While, span), condition_(condition),
        body_(std::move(body)) {}

  [[nodiscard]] auto condition() const -> HirExpr* { return condition_; }
  [[nodiscard]] auto body() const -> const std::vector<HirStmt*>& {
    return body_;
  }

private:
  HirExpr* condition_;
  std::vector<HirStmt*> body_;
};

class HirFor : public HirStmt {
public:
  HirFor(Span span, const Symbol* var_symbol, HirExpr* iterable,
         std::vector<HirStmt*> body)
      : HirStmt(HirKind::For, span), var_symbol_(var_symbol),
        iterable_(iterable), body_(std::move(body)) {}

  [[nodiscard]] auto var_symbol() const -> const Symbol* {
    return var_symbol_;
  }
  [[nodiscard]] auto iterable() const -> HirExpr* { return iterable_; }
  [[nodiscard]] auto body() const -> const std::vector<HirStmt*>& {
    return body_;
  }

private:
  const Symbol* var_symbol_;
  HirExpr* iterable_;
  std::vector<HirStmt*> body_;
};

class HirReturn : public HirStmt {
public:
  HirReturn(Span span, HirExpr* value)
      : HirStmt(HirKind::Return, span), value_(value) {}

  [[nodiscard]] auto value() const -> HirExpr* { return value_; }

private:
  HirExpr* value_; // nullable for bare return
};

class HirExprStmt : public HirStmt {
public:
  HirExprStmt(Span span, HirExpr* expr)
      : HirStmt(HirKind::ExprStmt, span), expr_(expr) {}

  [[nodiscard]] auto expr() const -> HirExpr* { return expr_; }

private:
  HirExpr* expr_;
};

enum class HirModeKind : std::uint8_t {
  Unsafe,
  Gpu,
  Parallel,
  Other,
};

auto hir_mode_kind_from_name(std::string_view name) -> HirModeKind;
auto hir_mode_kind_name(HirModeKind kind) -> const char*;

class HirMode : public HirStmt {
public:
  HirMode(Span span, HirModeKind mode, std::string_view mode_name,
          std::vector<HirStmt*> body)
      : HirStmt(HirKind::Mode, span), mode_(mode),
        mode_name_(mode_name), body_(std::move(body)) {}

  [[nodiscard]] auto mode() const -> HirModeKind { return mode_; }
  [[nodiscard]] auto mode_name() const -> std::string_view {
    return mode_name_;
  }
  [[nodiscard]] auto body() const -> const std::vector<HirStmt*>& {
    return body_;
  }

private:
  HirModeKind mode_;
  std::string_view mode_name_;
  std::vector<HirStmt*> body_;
};

class HirResource : public HirStmt {
public:
  HirResource(Span span, std::string_view resource_kind,
              std::string_view resource_name, std::vector<HirStmt*> body)
      : HirStmt(HirKind::Resource, span), resource_kind_(resource_kind),
        resource_name_(resource_name), body_(std::move(body)) {}

  [[nodiscard]] auto resource_kind() const -> std::string_view {
    return resource_kind_;
  }
  [[nodiscard]] auto resource_name() const -> std::string_view {
    return resource_name_;
  }
  [[nodiscard]] auto body() const -> const std::vector<HirStmt*>& {
    return body_;
  }

private:
  std::string_view resource_kind_;
  std::string_view resource_name_;
  std::vector<HirStmt*> body_;
};

// ---------------------------------------------------------------------------
// Expressions — all carry semantic Type*
// ---------------------------------------------------------------------------

class HirIntLiteral : public HirExpr {
public:
  HirIntLiteral(Span span, const Type* type, int64_t value)
      : HirExpr(HirKind::IntLiteral, span, type), value_(value) {}

  [[nodiscard]] auto value() const -> int64_t { return value_; }

private:
  int64_t value_;
};

class HirFloatLiteral : public HirExpr {
public:
  HirFloatLiteral(Span span, const Type* type, double value)
      : HirExpr(HirKind::FloatLiteral, span, type), value_(value) {}

  [[nodiscard]] auto value() const -> double { return value_; }

private:
  double value_;
};

class HirStringLiteral : public HirExpr {
public:
  HirStringLiteral(Span span, const Type* type, std::string_view value)
      : HirExpr(HirKind::StringLiteral, span, type), value_(value) {}

  [[nodiscard]] auto value() const -> std::string_view { return value_; }

private:
  std::string_view value_;
};

class HirBoolLiteral : public HirExpr {
public:
  HirBoolLiteral(Span span, const Type* type, bool value)
      : HirExpr(HirKind::BoolLiteral, span, type), value_(value) {}

  [[nodiscard]] auto value() const -> bool { return value_; }

private:
  bool value_;
};

class HirSymbolRef : public HirExpr {
public:
  HirSymbolRef(Span span, const Type* type, const Symbol* symbol)
      : HirExpr(HirKind::SymbolRef, span, type), symbol_(symbol) {}

  [[nodiscard]] auto symbol() const -> const Symbol* { return symbol_; }

private:
  const Symbol* symbol_;
};

class HirUnary : public HirExpr {
public:
  HirUnary(Span span, const Type* type, UnaryOp op, HirExpr* operand)
      : HirExpr(HirKind::Unary, span, type), op_(op), operand_(operand) {}

  [[nodiscard]] auto op() const -> UnaryOp { return op_; }
  [[nodiscard]] auto operand() const -> HirExpr* { return operand_; }

private:
  UnaryOp op_;
  HirExpr* operand_;
};

class HirBinary : public HirExpr {
public:
  HirBinary(Span span, const Type* type, BinaryOp op, HirExpr* left,
            HirExpr* right)
      : HirExpr(HirKind::Binary, span, type), op_(op), left_(left),
        right_(right) {}

  [[nodiscard]] auto op() const -> BinaryOp { return op_; }
  [[nodiscard]] auto left() const -> HirExpr* { return left_; }
  [[nodiscard]] auto right() const -> HirExpr* { return right_; }

private:
  BinaryOp op_;
  HirExpr* left_;
  HirExpr* right_;
};

class HirCall : public HirExpr {
public:
  HirCall(Span span, const Type* type, HirExpr* callee,
          std::vector<HirExpr*> args)
      : HirExpr(HirKind::Call, span, type), callee_(callee),
        args_(std::move(args)) {}

  [[nodiscard]] auto callee() const -> HirExpr* { return callee_; }
  [[nodiscard]] auto args() const -> const std::vector<HirExpr*>& {
    return args_;
  }

private:
  HirExpr* callee_;
  std::vector<HirExpr*> args_;
};

class HirField : public HirExpr {
public:
  HirField(Span span, const Type* type, HirExpr* object,
           std::string_view field_name)
      : HirExpr(HirKind::Field, span, type), object_(object),
        field_name_(field_name) {}

  [[nodiscard]] auto object() const -> HirExpr* { return object_; }
  [[nodiscard]] auto field_name() const -> std::string_view {
    return field_name_;
  }

private:
  HirExpr* object_;
  std::string_view field_name_;
};

class HirIndex : public HirExpr {
public:
  HirIndex(Span span, const Type* type, HirExpr* object,
           std::vector<HirExpr*> indices)
      : HirExpr(HirKind::Index, span, type), object_(object),
        indices_(std::move(indices)) {}

  [[nodiscard]] auto object() const -> HirExpr* { return object_; }
  [[nodiscard]] auto indices() const -> const std::vector<HirExpr*>& {
    return indices_;
  }

private:
  HirExpr* object_;
  std::vector<HirExpr*> indices_;
};

class HirPipe : public HirExpr {
public:
  HirPipe(Span span, const Type* type, HirExpr* left, HirExpr* right)
      : HirExpr(HirKind::Pipe, span, type), left_(left), right_(right) {}

  [[nodiscard]] auto left() const -> HirExpr* { return left_; }
  [[nodiscard]] auto right() const -> HirExpr* { return right_; }

private:
  HirExpr* left_;
  HirExpr* right_;
};

class HirLambda : public HirExpr {
public:
  HirLambda(Span span, const Type* type,
            std::vector<HirParam> params, HirExpr* body)
      : HirExpr(HirKind::Lambda, span, type), params_(std::move(params)),
        body_(body) {}

  [[nodiscard]] auto params() const -> const std::vector<HirParam>& {
    return params_;
  }
  [[nodiscard]] auto body() const -> HirExpr* { return body_; }

private:
  std::vector<HirParam> params_;
  HirExpr* body_;
};

} // namespace dao

#endif // DAO_IR_HIR_HIR_H
