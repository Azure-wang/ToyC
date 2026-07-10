#pragma once

#include "ast.h"

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
};

} // namespace toyc
