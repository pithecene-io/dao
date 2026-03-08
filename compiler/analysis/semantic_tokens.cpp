#include "analysis/semantic_tokens.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dao {

namespace {

// ---------------------------------------------------------------------------
// Builtin type names
// ---------------------------------------------------------------------------

auto is_builtin_type(std::string_view name) -> bool {
  // Core numeric and primitive types.
  static constexpr std::string_view builtins[] = {
      "int8",
      "int16",
      "int32",
      "int64",
      "uint8",
      "uint16",
      "uint32",
      "uint64",
      "float32",
      "float64",
      "bool",
      "string",
      "void",
  };
  for (auto b : builtins) {
    if (name == b) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Lexical classification — TokenKind → taxonomy category
// ---------------------------------------------------------------------------

auto lexical_category(TokenKind kind) -> std::string_view {
  switch (kind) {
  // Keywords — control
  case TokenKind::KwImport:
    return "keyword.import";
  case TokenKind::KwFn:
    return "keyword.fn";
  case TokenKind::KwStruct:
    return "keyword.type";
  case TokenKind::KwType:
    return "keyword.type";
  case TokenKind::KwLet:
    return "keyword.let";
  case TokenKind::KwIf:
    return "keyword.if";
  case TokenKind::KwElse:
    return "keyword.else";
  case TokenKind::KwWhile:
    return "keyword.while";
  case TokenKind::KwFor:
    return "keyword.for";
  case TokenKind::KwIn:
    return "keyword.in";
  case TokenKind::KwReturn:
    return "keyword.return";

  // Keywords — execution / resource constructs
  case TokenKind::KwMode:
    return "keyword.mode";
  case TokenKind::KwResource:
    return "keyword.resource";

  // Keyword literals and logical operators — no specific frozen
  // taxonomy entry. Omitted until the taxonomy expands.
  case TokenKind::KwTrue:
  case TokenKind::KwFalse:
  case TokenKind::KwAnd:
  case TokenKind::KwOr:
    return "";

  // Numeric literals
  case TokenKind::IntLiteral:
  case TokenKind::FloatLiteral:
    return "literal.number";

  // String literals
  case TokenKind::StringLiteral:
    return "literal.string";

  // Operators — specific taxonomy categories
  case TokenKind::PipeGt:
    return "operator.pipe";
  case TokenKind::Arrow:
  case TokenKind::FatArrow:
    return "operator.arrow";
  case TokenKind::Eq:
    return "operator.assignment";
  case TokenKind::ColonColon:
    return "operator.namespace";
  case TokenKind::Colon:
    return "operator.context";

  // Operators — general (no specific taxonomy entry)
  case TokenKind::EqEq:
  case TokenKind::BangEq:
  case TokenKind::Lt:
  case TokenKind::LtEq:
  case TokenKind::Gt:
  case TokenKind::GtEq:
  case TokenKind::Plus:
  case TokenKind::Minus:
  case TokenKind::Star:
  case TokenKind::Slash:
  case TokenKind::Percent:
  case TokenKind::Amp:
  case TokenKind::Bang:
  case TokenKind::Dot:
  case TokenKind::Pipe:
    return ""; // General operators — no frozen taxonomy category.

  // Punctuation
  case TokenKind::Comma:
  case TokenKind::LParen:
  case TokenKind::RParen:
  case TokenKind::LBracket:
  case TokenKind::RBracket:
    return "punctuation";

  // Identifiers need structural context.
  case TokenKind::Identifier:
    return "";

  // Synthetic and error tokens are not classified.
  case TokenKind::Newline:
  case TokenKind::Indent:
  case TokenKind::Dedent:
  case TokenKind::Eof:
  case TokenKind::Error:
    return "";
  }
  return "";
}

// ---------------------------------------------------------------------------
// AST walker — collects span → category mappings from structural context
// ---------------------------------------------------------------------------

class AstClassifier {
public:
  using SpanMap = std::unordered_map<uint32_t, std::string_view>;

  auto classifications() const -> const SpanMap& {
    return map_;
  }

  void visit_file(const FileNode& file) {
    for (const auto* imp : file.imports()) {
      visit_import(static_cast<const ImportNode&>(*imp));
    }
    for (const auto* decl : file.declarations()) {
      visit_decl(*decl);
    }
  }

private:
  SpanMap map_;

  void classify(Span span, std::string_view kind) {
    map_[span.offset] = kind;
  }

  // --- Imports ---

  void visit_import(const ImportNode& node) {
    // Qualified path segments in an import are module references.
    for (size_t i = 0; i + 1 < node.path().segments.size(); ++i) {
      // Module path segments — we don't have individual spans per segment,
      // so we skip for now. The import keyword is classified lexically.
    }
  }

  // --- Declarations ---

  void visit_decl(const Decl& decl) {
    switch (decl.kind()) {
    case NodeKind::FunctionDecl:
      visit_function(static_cast<const FunctionDeclNode&>(decl));
      break;
    case NodeKind::StructDecl:
      visit_struct(static_cast<const StructDeclNode&>(decl));
      break;
    case NodeKind::AliasDecl:
      visit_alias(static_cast<const AliasDeclNode&>(decl));
      break;
    default:
      break;
    }
  }

  void visit_function(const FunctionDeclNode& fn) {
    classify(fn.name_span(), "decl.function");

    for (const auto& param : fn.params()) {
      classify(param.name_span, "use.variable.param");
      if (param.type != nullptr) {
        visit_type(*param.type);
      }
    }

    if (fn.return_type() != nullptr) {
      visit_type(*fn.return_type());
    }

    for (const auto* stmt : fn.body()) {
      visit_stmt(*stmt);
    }

    if (fn.expr_body() != nullptr) {
      visit_expr(*fn.expr_body());
    }
  }

  void visit_struct(const StructDeclNode& st) {
    classify(st.name_span(), "decl.type");

    for (const auto* member : st.members()) {
      // Struct members declared with let are fields.
      if (member->kind() == NodeKind::LetStatement) {
        const auto& let_stmt = static_cast<const LetStatementNode&>(*member);
        classify(let_stmt.name_span(), "decl.field");
        if (let_stmt.type() != nullptr) {
          visit_type(*let_stmt.type());
        }
        if (let_stmt.initializer() != nullptr) {
          visit_expr(*let_stmt.initializer());
        }
      } else {
        visit_stmt(*member);
      }
    }
  }

  void visit_alias(const AliasDeclNode& alias) {
    classify(alias.name_span(), "decl.type");

    if (alias.type() != nullptr) {
      visit_type(*alias.type());
    }
  }

  // --- Statements ---

  void visit_stmt(const Stmt& stmt) {
    switch (stmt.kind()) {
    case NodeKind::LetStatement: {
      const auto& let_stmt = static_cast<const LetStatementNode&>(stmt);
      // Let bindings outside structs are local variables.
      // We don't have a use.variable.local.decl category, so we
      // classify the name as use.variable.local (closest match).
      classify(let_stmt.name_span(), "use.variable.local");
      if (let_stmt.type() != nullptr) {
        visit_type(*let_stmt.type());
      }
      if (let_stmt.initializer() != nullptr) {
        visit_expr(*let_stmt.initializer());
      }
      break;
    }
    case NodeKind::Assignment: {
      const auto& assign = static_cast<const AssignmentNode&>(stmt);
      visit_expr(*assign.target());
      visit_expr(*assign.value());
      break;
    }
    case NodeKind::IfStatement: {
      const auto& if_stmt = static_cast<const IfStatementNode&>(stmt);
      visit_expr(*if_stmt.condition());
      for (const auto* s : if_stmt.then_body()) {
        visit_stmt(*s);
      }
      for (const auto* s : if_stmt.else_body()) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::WhileStatement: {
      const auto& while_stmt = static_cast<const WhileStatementNode&>(stmt);
      visit_expr(*while_stmt.condition());
      for (const auto* s : while_stmt.body()) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::ForStatement: {
      const auto& for_stmt = static_cast<const ForStatementNode&>(stmt);
      classify(for_stmt.var_span(), "use.variable.local");
      visit_expr(*for_stmt.iterable());
      for (const auto* s : for_stmt.body()) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::ModeBlock: {
      const auto& mode = static_cast<const ModeBlockNode&>(stmt);
      auto mode_name = mode.mode_name();
      if (mode_name == "unsafe") {
        classify(mode.name_span(), "mode.unsafe");
      } else if (mode_name == "gpu") {
        classify(mode.name_span(), "mode.gpu");
      } else if (mode_name == "parallel") {
        classify(mode.name_span(), "mode.parallel");
      }
      for (const auto* s : mode.body()) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::ResourceBlock: {
      const auto& res = static_cast<const ResourceBlockNode&>(stmt);
      auto kind = res.resource_kind();
      if (kind == "memory") {
        classify(res.kind_span(), "resource.kind.memory");
      }
      classify(res.name_span(), "resource.binding");
      for (const auto* s : res.body()) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::ReturnStatement: {
      const auto& ret = static_cast<const ReturnStatementNode&>(stmt);
      if (ret.value() != nullptr) {
        visit_expr(*ret.value());
      }
      break;
    }
    case NodeKind::ExpressionStatement: {
      const auto& expr_stmt = static_cast<const ExpressionStatementNode&>(stmt);
      visit_expr(*expr_stmt.expr());
      break;
    }
    default:
      break;
    }
  }

  // --- Expressions ---

  void visit_expr(const Expr& expr) {
    switch (expr.kind()) {
    case NodeKind::BinaryExpr: {
      const auto& bin = static_cast<const BinaryExprNode&>(expr);
      visit_expr(*bin.left());
      visit_expr(*bin.right());
      break;
    }
    case NodeKind::UnaryExpr: {
      const auto& unary = static_cast<const UnaryExprNode&>(expr);
      visit_expr(*unary.operand());
      break;
    }
    case NodeKind::CallExpr: {
      const auto& call = static_cast<const CallExprNode&>(expr);
      visit_expr(*call.callee());
      for (const auto* arg : call.args()) {
        visit_expr(*arg);
      }
      break;
    }
    case NodeKind::IndexExpr: {
      const auto& idx = static_cast<const IndexExprNode&>(expr);
      visit_expr(*idx.object());
      for (const auto* i : idx.indices()) {
        visit_expr(*i);
      }
      break;
    }
    case NodeKind::FieldExpr: {
      const auto& field = static_cast<const FieldExprNode&>(expr);
      visit_expr(*field.object());
      classify(field.field_span(), "use.field");
      break;
    }
    case NodeKind::PipeExpr: {
      const auto& pipe = static_cast<const PipeExprNode&>(expr);
      visit_expr(*pipe.left());
      visit_expr(*pipe.right());
      break;
    }
    case NodeKind::Lambda: {
      const auto& lam = static_cast<const LambdaNode&>(expr);
      for (const auto& [name, span] : lam.params()) {
        classify(span, "lambda.param");
      }
      visit_expr(*lam.body());
      break;
    }
    case NodeKind::ListLiteral: {
      const auto& list = static_cast<const ListLiteralNode&>(expr);
      for (const auto* elem : list.elements()) {
        visit_expr(*elem);
      }
      break;
    }
    // Terminals — no structural classification needed.
    case NodeKind::Identifier:
    case NodeKind::QualifiedName:
    case NodeKind::IntLiteral:
    case NodeKind::FloatLiteral:
    case NodeKind::StringLiteral:
    case NodeKind::BoolLiteral:
      break;
    default:
      break;
    }
  }

  // --- Types ---

  void visit_type(const TypeNode& type) {
    switch (type.kind()) {
    case NodeKind::NamedType: {
      const auto& named = static_cast<const NamedTypeNode&>(type);
      // Classify the type name as builtin or nominal.
      // Use the last segment of the qualified path as the name.
      if (!named.name().segments.empty()) {
        auto type_name = named.name().segments.back();
        auto category = is_builtin_type(type_name) ? "type.builtin" : "type.nominal";
        classify(named.name().span, category);
      }
      for (const auto* arg : named.type_args()) {
        visit_type(*arg);
      }
      break;
    }
    case NodeKind::PointerType: {
      const auto& ptr = static_cast<const PointerTypeNode&>(type);
      visit_type(*ptr.pointee());
      break;
    }
    default:
      break;
    }
  }
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

auto classify_tokens(const std::vector<Token>& tokens, const FileNode* file)
    -> std::vector<SemanticToken> {
  // Step 1: Collect structural classifications from AST.
  AstClassifier::SpanMap ast_map;
  if (file != nullptr) {
    AstClassifier classifier;
    classifier.visit_file(*file);
    ast_map = classifier.classifications();
  }

  // Step 2: Walk tokens, preferring AST classification over lexical.
  std::vector<SemanticToken> result;
  result.reserve(tokens.size());

  for (const auto& tok : tokens) {
    // Skip synthetic tokens.
    if (tok.kind == TokenKind::Newline || tok.kind == TokenKind::Indent ||
        tok.kind == TokenKind::Dedent || tok.kind == TokenKind::Eof ||
        tok.kind == TokenKind::Error) {
      continue;
    }

    // Check AST classification first.
    auto it = ast_map.find(tok.span.offset);
    if (it != ast_map.end()) {
      result.push_back({.span = tok.span, .kind = it->second});
      continue;
    }

    // Fall back to lexical classification.
    auto category = lexical_category(tok.kind);
    if (!category.empty()) {
      result.push_back({.span = tok.span, .kind = category});
    }
    // Identifiers with no AST classification are omitted — they need
    // name resolution to distinguish use.function from use.variable.local.
  }

  return result;
}

} // namespace dao
