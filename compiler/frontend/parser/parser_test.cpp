#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"

#define BOOST_UT_DISABLE_MODULE
#include <boost/ut.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

using namespace boost::ut;
using namespace dao;

auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct ParseOutput {
  std::unique_ptr<SourceBuffer> source;
  LexResult lex_result;
  ParseResult parse_result;
};

auto parse_string(std::string_view src) -> ParseOutput {
  auto source = std::make_unique<SourceBuffer>("<test>", std::string(src));
  auto lex_result = lex(*source);
  auto parse_result = parse(lex_result.tokens);
  return {.source = std::move(source),
          .lex_result = std::move(lex_result),
          .parse_result = std::move(parse_result)};
}

auto parse_file(const std::filesystem::path& path) -> ParseOutput {
  auto contents = read_file(path);
  auto source = std::make_unique<SourceBuffer>(path.filename().string(), contents);
  auto lex_result = lex(*source);
  auto parse_result = parse(lex_result.tokens);
  return {.source = std::move(source),
          .lex_result = std::move(lex_result),
          .parse_result = std::move(parse_result)};
}

// NOLINTBEGIN(readability-function-cognitive-complexity,readability-magic-numbers,modernize-use-trailing-return-type)

suite<"function_tests"> function_tests = [] {
  "expression-bodied function"_test = [] {
    auto output = parse_string("fn add(a: i32, b: i32): i32 -> a + b\n");
    expect(output.parse_result.diagnostics.empty()) << "no parse errors";
    auto* file = output.parse_result.file;
    expect(file != nullptr);
    expect(file->declarations().size() == 1_u);

    auto* fn = static_cast<FunctionDeclNode*>(file->declarations()[0]);
    expect(fn->kind() == NodeKind::FunctionDecl);
    expect(fn->name() == "add");
    expect(fn->params().size() == 2_u);
    expect(fn->params()[0].name == "a");
    expect(fn->params()[1].name == "b");
    expect(fn->return_type() != nullptr);
    expect(fn->is_expr_bodied());
    expect(fn->expr_body() != nullptr);
    expect(fn->expr_body()->kind() == NodeKind::BinaryExpr);
  };

  "block-bodied function"_test = [] {
    auto output = parse_string("fn main(): i32\n    0\n");
    expect(output.parse_result.diagnostics.empty()) << "no parse errors";
    auto* file = output.parse_result.file;
    expect(file->declarations().size() == 1_u);

    auto* fn = static_cast<FunctionDeclNode*>(file->declarations()[0]);
    expect(!fn->is_expr_bodied());
    expect(fn->body().size() == 1_u);
    expect(fn->body()[0]->kind() == NodeKind::ExpressionStatement);
  };

  "no-param function"_test = [] {
    auto output = parse_string("fn greet(): i32 -> 0\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    expect(fn->params().empty());
  };
};

suite<"let_tests"> let_tests = [] {
  "let with type and initializer"_test = [] {
    auto output = parse_string("fn f(): i32\n    let x: i32 = 42\n    x\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    expect(fn->body().size() == 2_u);

    auto* let_stmt = static_cast<LetStatementNode*>(fn->body()[0]);
    expect(let_stmt->kind() == NodeKind::LetStatement);
    expect(let_stmt->name() == "x");
    expect(let_stmt->type() != nullptr);
    expect(let_stmt->initializer() != nullptr);
  };

  "let with type only"_test = [] {
    auto output = parse_string("fn f(): i32\n    let x: i32\n    x\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* let_stmt = static_cast<LetStatementNode*>(fn->body()[0]);
    expect(let_stmt->type() != nullptr);
    expect(let_stmt->initializer() == nullptr);
  };

  "let with initializer only"_test = [] {
    auto output = parse_string("fn f(): i32\n    let x = 42\n    x\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* let_stmt = static_cast<LetStatementNode*>(fn->body()[0]);
    expect(let_stmt->type() == nullptr);
    expect(let_stmt->initializer() != nullptr);
  };
};

suite<"control_flow_tests"> control_flow_tests = [] {
  "if statement"_test = [] {
    auto output = parse_string("fn f(): i32\n    if true:\n        0\n    1\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* if_stmt = static_cast<IfStatementNode*>(fn->body()[0]);
    expect(if_stmt->kind() == NodeKind::IfStatement);
    expect(if_stmt->condition()->kind() == NodeKind::BoolLiteral);
    expect(if_stmt->then_body().size() == 1_u);
    expect(!if_stmt->has_else());
  };

  "if-else statement"_test = [] {
    auto output = parse_string("fn f(): i32\n    if true:\n        0\n    else:\n        1\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* if_stmt = static_cast<IfStatementNode*>(fn->body()[0]);
    expect(if_stmt->has_else());
    expect(if_stmt->else_body().size() == 1_u);
  };

  "while statement"_test = [] {
    auto output = parse_string("fn f(): i32\n    while true:\n        0\n    1\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* while_stmt = static_cast<WhileStatementNode*>(fn->body()[0]);
    expect(while_stmt->kind() == NodeKind::WhileStatement);
    expect(while_stmt->body().size() == 1_u);
  };

  "for statement"_test = [] {
    auto output = parse_string("fn f(): i32\n    for i in items:\n        i\n    0\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* for_stmt = static_cast<ForStatementNode*>(fn->body()[0]);
    expect(for_stmt->kind() == NodeKind::ForStatement);
    expect(for_stmt->var() == "i");
  };
};

suite<"expression_tests"> expression_tests = [] {
  "binary operators"_test = [] {
    auto output = parse_string("fn f(): i32 -> 1 + 2 * 3\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    // Should be BinaryExpr(+, 1, BinaryExpr(*, 2, 3)) — * binds tighter than +
    auto* add = static_cast<BinaryExprNode*>(fn->expr_body());
    expect(add->kind() == NodeKind::BinaryExpr);
    expect(add->op() == BinaryOp::Add);
    expect(add->left()->kind() == NodeKind::IntLiteral);
    expect(add->right()->kind() == NodeKind::BinaryExpr);
    auto* mul = static_cast<BinaryExprNode*>(add->right());
    expect(mul->op() == BinaryOp::Mul);
  };

  "unary operators"_test = [] {
    auto output = parse_string("fn f(p: *i32): i32 -> *p\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* deref = static_cast<UnaryExprNode*>(fn->expr_body());
    expect(deref->kind() == NodeKind::UnaryExpr);
    expect(deref->op() == UnaryOp::Deref);
  };

  "function call"_test = [] {
    auto output = parse_string("fn f(): i32\n    foo(1, 2)\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* expr_stmt = static_cast<ExpressionStatementNode*>(fn->body()[0]);
    auto* call = static_cast<CallExprNode*>(expr_stmt->expr());
    expect(call->kind() == NodeKind::CallExpr);
    expect(call->args().size() == 2_u);
  };

  "field access"_test = [] {
    auto output = parse_string("fn f(): i32\n    x.y.z\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* expr_stmt = static_cast<ExpressionStatementNode*>(fn->body()[0]);
    auto* outer = static_cast<FieldExprNode*>(expr_stmt->expr());
    expect(outer->kind() == NodeKind::FieldExpr);
    expect(outer->field() == "z");
    auto* inner = static_cast<FieldExprNode*>(outer->object());
    expect(inner->field() == "y");
  };

  "index expression"_test = [] {
    auto output = parse_string("fn f(): i32\n    a[0]\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* expr_stmt = static_cast<ExpressionStatementNode*>(fn->body()[0]);
    auto* idx = static_cast<IndexExprNode*>(expr_stmt->expr());
    expect(idx->kind() == NodeKind::IndexExpr);
    expect(idx->indices().size() == 1_u);
  };

  "multi-arg bracket"_test = [] {
    auto output = parse_string("fn f(): i32\n    Map[i32, string]()\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* expr_stmt = static_cast<ExpressionStatementNode*>(fn->body()[0]);
    auto* call = static_cast<CallExprNode*>(expr_stmt->expr());
    expect(call->kind() == NodeKind::CallExpr);
    auto* idx = static_cast<IndexExprNode*>(call->callee());
    expect(idx->indices().size() == 2_u);
  };

  "comparison operators"_test = [] {
    auto output = parse_string("fn f(): i32 -> 1 == 2\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* eq = static_cast<BinaryExprNode*>(fn->expr_body());
    expect(eq->op() == BinaryOp::EqEq);
  };

  "logical operators"_test = [] {
    auto output = parse_string("fn f(): i32 -> true and false or true\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    // or binds less tightly than and: or(and(true, false), true)
    auto* or_expr = static_cast<BinaryExprNode*>(fn->expr_body());
    expect(or_expr->op() == BinaryOp::Or);
    auto* and_expr = static_cast<BinaryExprNode*>(or_expr->left());
    expect(and_expr->op() == BinaryOp::And);
  };
};

suite<"lambda_tests"> lambda_tests = [] {
  "simple lambda"_test = [] {
    auto output = parse_string("fn f(): i32 -> |x| -> x + 1\n");
    expect(output.parse_result.diagnostics.empty()) << "no parse errors";
  };

  "lambda in pipe"_test = [] {
    auto output = parse_string("fn f(): i32\n    xs |> map |x| -> x * x\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* expr_stmt = static_cast<ExpressionStatementNode*>(fn->body()[0]);
    expect(expr_stmt->expr()->kind() == NodeKind::PipeExpr);
  };
};

suite<"pipe_tests"> pipe_tests = [] {
  "simple pipe"_test = [] {
    auto output = parse_string("fn f(): i32\n    x |> foo |> bar\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* expr_stmt = static_cast<ExpressionStatementNode*>(fn->body()[0]);
    auto* outer = static_cast<PipeExprNode*>(expr_stmt->expr());
    expect(outer->kind() == NodeKind::PipeExpr);
    expect(outer->left()->kind() == NodeKind::PipeExpr);
  };

  "multi-line pipe in suite"_test = [] {
    auto output = parse_string("fn f(): i32\n    xs\n        |> map |x| -> x\n    0\n");
    expect(output.parse_result.diagnostics.empty()) << "multi-line pipe in suite";
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    expect(fn->body().size() == 2_u) << "pipe expr + trailing 0";
    auto* expr_stmt = static_cast<ExpressionStatementNode*>(fn->body()[0]);
    expect(expr_stmt->expr()->kind() == NodeKind::PipeExpr);
  };
};

suite<"mode_resource_tests"> mode_resource_tests = [] {
  "mode block"_test = [] {
    auto output = parse_string("fn f(): i32\n    mode unsafe =>\n        0\n    1\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* mode = static_cast<ModeBlockNode*>(fn->body()[0]);
    expect(mode->kind() == NodeKind::ModeBlock);
    expect(mode->mode_name() == "unsafe");
    expect(mode->body().size() == 1_u);
  };

  "resource block"_test = [] {
    auto output = parse_string("fn f(): i32\n    resource memory Search =>\n        0\n    1\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* res = static_cast<ResourceBlockNode*>(fn->body()[0]);
    expect(res->kind() == NodeKind::ResourceBlock);
    expect(res->resource_kind() == "memory");
    expect(res->resource_name() == "Search");
    expect(res->body().size() == 1_u);
  };
};

suite<"type_tests"> type_tests = [] {
  "pointer type"_test = [] {
    auto output = parse_string("fn f(p: *i32): i32 -> 0\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* param_type = fn->params()[0].type;
    expect(param_type->kind() == NodeKind::PointerType);
  };

  "parameterized type"_test = [] {
    auto output = parse_string("fn f(): List[i32] -> []\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* ret = static_cast<NamedTypeNode*>(fn->return_type());
    expect(ret->kind() == NodeKind::NamedType);
    expect(ret->name().segments.size() == 1_u);
    expect(ret->name().segments[0] == "List");
    expect(ret->type_args().size() == 1_u);
  };

  "multi-param type"_test = [] {
    auto output = parse_string("fn f(): Map[i32, string] -> []\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* ret = static_cast<NamedTypeNode*>(fn->return_type());
    expect(ret->type_args().size() == 2_u);
  };
};

suite<"import_tests"> import_tests = [] {
  "simple import"_test = [] {
    auto output = parse_string("import foo\nfn f(): i32 -> 0\n");
    expect(output.parse_result.diagnostics.empty());
    expect(output.parse_result.file->imports().size() == 1_u);
    auto* imp = static_cast<ImportNode*>(output.parse_result.file->imports()[0]);
    expect(imp->path().segments.size() == 1_u);
    expect(imp->path().segments[0] == "foo");
  };

  "qualified import"_test = [] {
    auto output = parse_string("import net::http\nfn f(): i32 -> 0\n");
    expect(output.parse_result.diagnostics.empty());
    auto* imp = static_cast<ImportNode*>(output.parse_result.file->imports()[0]);
    expect(imp->path().segments.size() == 2_u);
    expect(imp->path().segments[0] == "net");
    expect(imp->path().segments[1] == "http");
  };
};

suite<"assignment_tests"> assignment_tests = [] {
  "simple assignment"_test = [] {
    auto output = parse_string("fn f(): i32\n    x = 42\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* assign = static_cast<AssignmentNode*>(fn->body()[0]);
    expect(assign->kind() == NodeKind::Assignment);
    expect(assign->target()->kind() == NodeKind::Identifier);
    expect(assign->value()->kind() == NodeKind::IntLiteral);
  };

  "field assignment"_test = [] {
    auto output = parse_string("fn f(): i32\n    x.y = 42\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* assign = static_cast<AssignmentNode*>(fn->body()[0]);
    expect(assign->target()->kind() == NodeKind::FieldExpr);
  };

  "index assignment"_test = [] {
    auto output = parse_string("fn f(): i32\n    a[0] = 42\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* assign = static_cast<AssignmentNode*>(fn->body()[0]);
    expect(assign->target()->kind() == NodeKind::IndexExpr);
  };

  "invalid assignment target"_test = [] {
    auto output = parse_string("fn f(): i32\n    1 = 2\n");
    expect(!output.parse_result.diagnostics.empty()) << "should reject invalid LHS";
  };
};

suite<"return_tests"> return_tests = [] {
  "return statement"_test = [] {
    auto output = parse_string("fn f(): i32\n    return 42\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* ret = static_cast<ReturnStatementNode*>(fn->body()[0]);
    expect(ret->kind() == NodeKind::ReturnStatement);
    expect(ret->value()->kind() == NodeKind::IntLiteral);
  };
};

suite<"class_tests"> class_tests = [] {
  "class with fields"_test = [] {
    auto output = parse_string("class Point:\n    x: i32\n    y: i32\n");
    expect(output.parse_result.diagnostics.empty()) << "no parse errors";
    auto* file = output.parse_result.file;
    expect(file->declarations().size() == 1_u);

    auto* cls = static_cast<ClassDeclNode*>(file->declarations()[0]);
    expect(cls->kind() == NodeKind::ClassDecl);
    expect(cls->name() == "Point");
    expect(cls->fields().size() == 2_u);
    expect(cls->fields()[0]->name() == "x");
    expect(cls->fields()[1]->name() == "y");
    expect(cls->fields()[0]->type() != nullptr);
    expect(cls->fields()[1]->type() != nullptr);
  };

  "class with single field"_test = [] {
    auto output = parse_string("class Wrapper:\n    value: string\n");
    expect(output.parse_result.diagnostics.empty());
    auto* cls = static_cast<ClassDeclNode*>(output.parse_result.file->declarations()[0]);
    expect(cls->fields().size() == 1_u);
    expect(cls->fields()[0]->name() == "value");
  };

  "class followed by function"_test = [] {
    auto output = parse_string("class Point:\n    x: i32\n    y: i32\nfn f(): i32 -> 0\n");
    expect(output.parse_result.diagnostics.empty());
    expect(output.parse_result.file->declarations().size() == 2_u);
    expect(output.parse_result.file->declarations()[0]->kind() == NodeKind::ClassDecl);
    expect(output.parse_result.file->declarations()[1]->kind() == NodeKind::FunctionDecl);
  };

  "let inside class body is rejected"_test = [] {
    auto output = parse_string("class Point:\n    let x: i32\n");
    expect(!output.parse_result.diagnostics.empty()) << "let in class body should fail";
  };

  "struct keyword produces migration diagnostic"_test = [] {
    auto output = parse_string("struct Point:\n    x: i32\n");
    expect(!output.parse_result.diagnostics.empty());
    bool found = false;
    for (const auto& diag : output.parse_result.diagnostics) {
      if (diag.message.find("renamed") != std::string::npos) {
        found = true;
      }
    }
    expect(found) << "should mention 'renamed'";
  };
};

suite<"file_tests"> file_tests = [] {
  "examples parse without error"_test = [] {
    std::filesystem::path root(DAO_SOURCE_DIR);
    for (const auto& entry : std::filesystem::directory_iterator(root / "examples")) {
      if (entry.path().extension() == ".dao") {
        auto output = parse_file(entry.path());
        expect(output.parse_result.diagnostics.empty())
            << "parse errors in " << entry.path().filename().string();
        expect(output.parse_result.file != nullptr)
            << "null file for " << entry.path().filename().string();
      }
    }
  };

  "syntax probes parse without error"_test = [] {
    std::filesystem::path root(DAO_SOURCE_DIR);
    for (const auto& entry : std::filesystem::directory_iterator(root / "spec" / "syntax_probes")) {
      if (entry.path().extension() == ".dao") {
        auto output = parse_file(entry.path());
        expect(output.parse_result.diagnostics.empty())
            << "parse errors in " << entry.path().filename().string();
        expect(output.parse_result.file != nullptr)
            << "null file for " << entry.path().filename().string();
      }
    }
  };
};

suite<"qualified_name_tests"> qualified_name_tests = [] {
  "qualified name in expression"_test = [] {
    auto output = parse_string("fn f(): i32\n    Foo::bar()\n");
    expect(output.parse_result.diagnostics.empty());
    auto* fn = static_cast<FunctionDeclNode*>(output.parse_result.file->declarations()[0]);
    auto* expr_stmt = static_cast<ExpressionStatementNode*>(fn->body()[0]);
    auto* call = static_cast<CallExprNode*>(expr_stmt->expr());
    expect(call->callee()->kind() == NodeKind::QualifiedName);
    auto* qn = static_cast<QualifiedNameNode*>(call->callee());
    expect(qn->segments().size() == 2_u);
    expect(qn->segments()[0] == "Foo");
    expect(qn->segments()[1] == "bar");
  };
};

// NOLINTEND(readability-function-cognitive-complexity,readability-magic-numbers,modernize-use-trailing-return-type)

} // namespace

auto main() -> int {
} // NOLINT(readability-named-parameter)
