#pragma once

#include "token.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace toyc {

enum class Type { Int, Void };
enum class UnaryOp { Plus, Minus, Not };
enum class BinaryOp {
  Add, Sub, Mul, Div, Mod,
  Lt, Gt, Le, Ge, Eq, Ne,
  And, Or
};

struct Expr {
  explicit Expr(SourceLoc loc) : loc(loc) {}
  virtual ~Expr() = default;
  SourceLoc loc;
};

struct NumberExpr : Expr {
  NumberExpr(SourceLoc loc, int32_t value) : Expr(loc), value(value) {}
  int32_t value;
};

struct VarExpr : Expr {
  VarExpr(SourceLoc loc, std::string name) : Expr(loc), name(std::move(name)) {}
  std::string name;
};

struct UnaryExpr : Expr {
  UnaryExpr(SourceLoc loc, UnaryOp op, std::unique_ptr<Expr> operand)
      : Expr(loc), op(op), operand(std::move(operand)) {}
  UnaryOp op;
  std::unique_ptr<Expr> operand;
};

struct BinaryExpr : Expr {
  BinaryExpr(SourceLoc loc, BinaryOp op, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs)
      : Expr(loc), op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
  BinaryOp op;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
};

struct CallExpr : Expr {
  CallExpr(SourceLoc loc, std::string callee, std::vector<std::unique_ptr<Expr>> args)
      : Expr(loc), callee(std::move(callee)), args(std::move(args)) {}
  std::string callee;
  std::vector<std::unique_ptr<Expr>> args;
};

struct Decl {
  SourceLoc loc;
  bool isConst = false;
  std::string name;
  std::unique_ptr<Expr> init;
};

struct Stmt {
  explicit Stmt(SourceLoc loc) : loc(loc) {}
  virtual ~Stmt() = default;
  SourceLoc loc;
};

struct BlockStmt : Stmt {
  explicit BlockStmt(SourceLoc loc) : Stmt(loc) {}
  std::vector<std::unique_ptr<Stmt>> stmts;
};

struct DeclStmt : Stmt {
  DeclStmt(SourceLoc loc, Decl decl) : Stmt(loc), decl(std::move(decl)) {}
  Decl decl;
};

struct EmptyStmt : Stmt {
  explicit EmptyStmt(SourceLoc loc) : Stmt(loc) {}
};

struct ExprStmt : Stmt {
  ExprStmt(SourceLoc loc, std::unique_ptr<Expr> expr) : Stmt(loc), expr(std::move(expr)) {}
  std::unique_ptr<Expr> expr;
};

struct AssignStmt : Stmt {
  AssignStmt(SourceLoc loc, std::string name, std::unique_ptr<Expr> value)
      : Stmt(loc), name(std::move(name)), value(std::move(value)) {}
  std::string name;
  std::unique_ptr<Expr> value;
};

struct IfStmt : Stmt {
  IfStmt(SourceLoc loc, std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> thenBranch,
         std::unique_ptr<Stmt> elseBranch)
      : Stmt(loc), cond(std::move(cond)), thenBranch(std::move(thenBranch)),
        elseBranch(std::move(elseBranch)) {}
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> thenBranch;
  std::unique_ptr<Stmt> elseBranch;
};

struct WhileStmt : Stmt {
  WhileStmt(SourceLoc loc, std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> body)
      : Stmt(loc), cond(std::move(cond)), body(std::move(body)) {}
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> body;
};

struct BreakStmt : Stmt {
  explicit BreakStmt(SourceLoc loc) : Stmt(loc) {}
};

struct ContinueStmt : Stmt {
  explicit ContinueStmt(SourceLoc loc) : Stmt(loc) {}
};

struct ReturnStmt : Stmt {
  ReturnStmt(SourceLoc loc, std::unique_ptr<Expr> value) : Stmt(loc), value(std::move(value)) {}
  std::unique_ptr<Expr> value;
};

struct Param {
  SourceLoc loc;
  std::string name;
};

struct Function {
  SourceLoc loc;
  Type returnType = Type::Int;
  std::string name;
  std::vector<Param> params;
  std::unique_ptr<BlockStmt> body;
};

struct TopLevel {
  virtual ~TopLevel() = default;
  SourceLoc loc;
};

struct TopDecl : TopLevel {
  Decl decl;
};

struct TopFunction : TopLevel {
  Function func;
};

struct Program {
  std::vector<std::unique_ptr<TopLevel>> items;
};

const char *typeName(Type type);
const char *binaryOpName(BinaryOp op);

} // namespace toyc
