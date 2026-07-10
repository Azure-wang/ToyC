#pragma once

#include "ast.h"
#include "symbol_table.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace toyc {

class Optimizer {
public:
  void optimize(Program &program);

private:
  bool foldExpr(std::unique_ptr<Expr> &expr);
  void optimizeStmt(std::unique_ptr<Stmt> &stmt);
  void optimizeBlock(BlockStmt &block);
  bool isTerminator(const Stmt &stmt) const;
  bool number(const Expr &expr, int32_t &value) const;
  int32_t applyUnary(UnaryOp op, int32_t v) const;
  int32_t applyBinary(BinaryOp op, int32_t a, int32_t b) const;
  std::string exprKey(const Expr &expr) const;
  void cseBlock(BlockStmt &block);
  void cseStmt(std::unique_ptr<Stmt> &stmt);

  ScopedTable<int32_t> constTable_;
  std::unordered_map<std::string, std::string> cseMap_;
};

} // namespace toyc
