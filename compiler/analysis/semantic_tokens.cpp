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
  // Builtin scalar types only — predeclared named types (string, void)
  // are classified as type.nominal, not type.builtin.
  // See CONTRACT_TYPE_SYSTEM_FOUNDATIONS.md §5.
  static constexpr std::string_view builtins[] = {
      "i8",
      "i16",
      "i32",
      "i64",
      "u8",
      "u16",
      "u32",
      "u64",
      "f32",
      "f64",
      "bool",
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
  case TokenKind::KwExtern:
    return "keyword.extern";
  case TokenKind::KwFn:
    return "keyword.fn";
  case TokenKind::KwClass:
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

  // Keywords — concepts and conformance
  case TokenKind::KwConcept:
    return "keyword.concept";
  case TokenKind::KwDerived:
    return "keyword.derived";
  case TokenKind::KwAs:
    return "keyword.as";
  case TokenKind::KwExtend:
    return "keyword.extend";
  case TokenKind::KwDeny:
    return "keyword.deny";
  case TokenKind::KwSelf:
    return "keyword.self";
  case TokenKind::KwWhere:
    return "keyword.where";

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
    for (const auto* imp : file.imports) {
      visit_import(*imp);
    }
    for (const auto* decl : file.declarations) {
      visit_decl(*decl);
    }
  }

private:
  SpanMap map_;

  void classify(Span span, std::string_view kind) {
    map_[span.offset] = kind;
  }

  // --- Qualified path helpers ---

  // Compute per-segment spans from a QualifiedPath. Segments are
  // separated by "::" (2 characters) in the source.
  static auto segment_spans(const QualifiedPath& path)
      -> std::vector<std::pair<std::string_view, Span>> {
    std::vector<std::pair<std::string_view, Span>> result;
    uint32_t offset = path.span.offset;
    for (const auto& seg : path.segments) {
      auto len = static_cast<uint32_t>(seg.size());
      result.emplace_back(seg, Span{.offset = offset, .length = len});
      offset += len + 2; // skip "::" separator
    }
    return result;
  }

  // Classify all segments in a qualified path: leading segments are
  // use.module, the trailing segment gets the given category.
  void classify_qualified(const QualifiedPath& path, std::string_view tail_category) {
    auto spans = segment_spans(path);
    for (size_t i = 0; i < spans.size(); ++i) {
      if (i + 1 < spans.size()) {
        classify(spans[i].second, "use.module");
      } else {
        classify(spans[i].second, tail_category);
      }
    }
  }

  // --- Imports ---

  void visit_import(const ImportNode& node) {
    // Leading segments of an import path are module references.
    // The last segment is the binding site — classified as decl.module.
    auto spans = segment_spans(node.path);
    for (size_t i = 0; i < spans.size(); ++i) {
      if (i + 1 < spans.size()) {
        classify(spans[i].second, "use.module");
      } else {
        classify(spans[i].second, "decl.module");
      }
    }
  }

  // --- Declarations ---

  void visit_decl(const Decl& decl) {
    switch (decl.kind()) {
    case NodeKind::FunctionDecl:
      visit_function(decl);
      break;
    case NodeKind::ClassDecl:
      visit_class(decl);
      break;
    case NodeKind::AliasDecl:
      visit_alias(decl);
      break;
    default:
      break;
    }
  }

  void visit_function(const Decl& decl) {
    const auto& fn = decl.as<FunctionDecl>();
    classify(fn.name_span, "decl.function");

    for (const auto& param : fn.params) {
      // Parameter binders are declaration sites, not uses. The frozen
      // taxonomy has use.variable.param but no decl.variable.param.
      // Omit until name resolution can classify actual references.
      if (param.type != nullptr) {
        visit_type(*param.type);
      }
    }

    if (fn.return_type != nullptr) {
      visit_type(*fn.return_type);
    }

    for (const auto* stmt : fn.body) {
      visit_stmt(*stmt);
    }

    if (fn.expr_body != nullptr) {
      visit_expr(*fn.expr_body);
    }
  }

  void visit_class(const Decl& decl) {
    const auto& st = decl.as<ClassDecl>();
    classify(st.name_span, "decl.type");

    for (const auto* field : st.fields) {
      classify(field->name_span, "decl.field");
      if (field->type != nullptr) {
        visit_type(*field->type);
      }
    }
  }

  void visit_alias(const Decl& decl) {
    const auto& alias = decl.as<AliasDecl>();
    classify(alias.name_span, "decl.type");

    if (alias.type != nullptr) {
      visit_type(*alias.type);
    }
  }

  // --- Statements ---

  void visit_stmt(const Stmt& stmt) {
    switch (stmt.kind()) {
    case NodeKind::LetStatement: {
      const auto& let_stmt = stmt.as<LetStatement>();
      // Let binders are declaration sites, not uses. The frozen
      // taxonomy has use.variable.local but no decl.variable.local.
      // Omit until name resolution can classify actual references.
      if (let_stmt.type != nullptr) {
        visit_type(*let_stmt.type);
      }
      if (let_stmt.initializer != nullptr) {
        visit_expr(*let_stmt.initializer);
      }
      break;
    }
    case NodeKind::Assignment: {
      const auto& assign = stmt.as<Assignment>();
      visit_expr(*assign.target);
      visit_expr(*assign.value);
      break;
    }
    case NodeKind::IfStatement: {
      const auto& if_stmt = stmt.as<IfStatement>();
      visit_expr(*if_stmt.condition);
      for (const auto* s : if_stmt.then_body) {
        visit_stmt(*s);
      }
      for (const auto* s : if_stmt.else_body) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::WhileStatement: {
      const auto& while_stmt = stmt.as<WhileStatement>();
      visit_expr(*while_stmt.condition);
      for (const auto* s : while_stmt.body) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::ForStatement: {
      const auto& for_stmt = stmt.as<ForStatement>();
      // For-loop binders are declaration sites — omit like let binders.
      visit_expr(*for_stmt.iterable);
      for (const auto* s : for_stmt.body) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::ModeBlock: {
      const auto& mode = stmt.as<ModeBlock>();
      auto mode_name = mode.mode_name;
      if (mode_name == "unsafe") {
        classify(mode.name_span, "mode.unsafe");
      } else if (mode_name == "gpu") {
        classify(mode.name_span, "mode.gpu");
      } else if (mode_name == "parallel") {
        classify(mode.name_span, "mode.parallel");
      }
      for (const auto* s : mode.body) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::ResourceBlock: {
      const auto& res = stmt.as<ResourceBlock>();
      auto kind = res.resource_kind;
      if (kind == "memory") {
        classify(res.kind_span, "resource.kind.memory");
      }
      classify(res.name_span, "resource.binding");
      for (const auto* s : res.body) {
        visit_stmt(*s);
      }
      break;
    }
    case NodeKind::ReturnStatement: {
      const auto& ret = stmt.as<ReturnStatement>();
      if (ret.value != nullptr) {
        visit_expr(*ret.value);
      }
      break;
    }
    case NodeKind::ExpressionStatement: {
      const auto& expr_stmt = stmt.as<ExpressionStatement>();
      visit_expr(*expr_stmt.expr);
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
      const auto& bin = expr.as<BinaryExpr>();
      visit_expr(*bin.left);
      visit_expr(*bin.right);
      break;
    }
    case NodeKind::UnaryExpr: {
      const auto& unary = expr.as<UnaryExpr>();
      visit_expr(*unary.operand);
      break;
    }
    case NodeKind::CallExpr: {
      const auto& call = expr.as<CallExpr>();
      visit_expr(*call.callee);
      for (const auto* arg : call.args) {
        visit_expr(*arg);
      }
      break;
    }
    case NodeKind::IndexExpr: {
      const auto& idx = expr.as<IndexExpr>();
      visit_expr(*idx.object);
      for (const auto* i : idx.indices) {
        visit_expr(*i);
      }
      break;
    }
    case NodeKind::FieldExpr: {
      const auto& field = expr.as<FieldExpr>();
      visit_expr(*field.object);
      classify(field.field_span, "use.field");
      break;
    }
    case NodeKind::PipeExpr: {
      const auto& pipe = expr.as<PipeExpr>();
      visit_expr(*pipe.left);
      visit_expr(*pipe.right);
      break;
    }
    case NodeKind::Lambda: {
      const auto& lam = expr.as<LambdaExpr>();
      for (const auto& [name, span] : lam.params) {
        classify(span, "lambda.param");
      }
      visit_expr(*lam.body);
      break;
    }
    case NodeKind::ListLiteral: {
      const auto& list = expr.as<ListLiteral>();
      for (const auto* elem : list.elements) {
        visit_expr(*elem);
      }
      break;
    }
    case NodeKind::QualifiedName:
      // Leading segment classification is deferred to the resolver
      // (which validates that it is actually a module binding). When
      // no resolver is available, these segments receive no
      // classification — the structural walker cannot be authoritative
      // about whether a leading segment is a module.
      break;
    // Terminals — no structural classification needed.
    case NodeKind::Identifier:
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
      const auto& named = type.as<NamedType>();
      // Classify the type name: leading segments are use.module,
      // the final segment is type.builtin or type.nominal.
      if (!named.name.segments.empty()) {
        auto type_name = named.name.segments.back();
        auto category = is_builtin_type(type_name) ? "type.builtin" : "type.nominal";
        classify_qualified(named.name, category);
      }
      for (const auto* arg : named.type_args) {
        visit_type(*arg);
      }
      break;
    }
    case NodeKind::PointerType: {
      const auto& ptr = type.as<PointerType>();
      visit_type(*ptr.pointee);
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

// Map a resolved symbol kind to a semantic token category for use sites.
auto resolve_use_category(SymbolKind kind) -> std::string_view {
  switch (kind) {
  case SymbolKind::Function:
    return "use.function";
  case SymbolKind::Param:
    return "use.variable.param";
  case SymbolKind::Local:
    return "use.variable.local";
  case SymbolKind::Module:
    return "use.module";
  case SymbolKind::LambdaParam:
    return "use.variable.param"; // reuse param category for lambda params
  case SymbolKind::GenericParam:
    return "use.type"; // generic type parameters classify as type uses
  case SymbolKind::Builtin:
  case SymbolKind::Predeclared:
    return ""; // type-position symbols — classified by visit_type(), not here
  default:
    return "";
  }
}

auto classify_tokens(const std::vector<Token>& tokens,
                     const FileNode* file,
                     const ResolveResult* resolve_result)
    -> std::vector<SemanticToken> {
  // Step 1: Collect structural classifications from AST.
  AstClassifier::SpanMap ast_map;
  if (file != nullptr) {
    AstClassifier classifier;
    classifier.visit_file(*file);
    ast_map = classifier.classifications();
  }

  // Step 2: Walk tokens, preferring AST classification over lexical,
  // with resolve-driven classifications filling in identifier gaps.
  std::vector<SemanticToken> result;
  result.reserve(tokens.size());

  for (const auto& tok : tokens) {
    // Skip synthetic tokens.
    if (tok.kind == TokenKind::Newline || tok.kind == TokenKind::Indent ||
        tok.kind == TokenKind::Dedent || tok.kind == TokenKind::Eof ||
        tok.kind == TokenKind::Error) {
      continue;
    }

    // For identifiers when a resolve result is available, check
    // resolve first — it gives authoritative use-site classification
    // that overrides structural guesses (e.g., QualifiedName leading
    // segments that the AST walker speculatively marks as use.module).
    if (resolve_result != nullptr && tok.kind == TokenKind::Identifier) {
      auto res_it = resolve_result->uses.find(tok.span.offset);
      if (res_it != resolve_result->uses.end()) {
        auto category = resolve_use_category(res_it->second->kind);
        if (!category.empty()) {
          result.push_back({.span = tok.span, .kind = category});
          continue;
        }
      }
      // Not in uses table — fall through to AST classification
      // (covers declaration-site identifiers like decl.function,
      // decl.type, lambda.param, etc.).
    }

    // Check AST structural classification.
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
    // Identifiers with no classification are omitted.
  }

  return result;
}

} // namespace dao
