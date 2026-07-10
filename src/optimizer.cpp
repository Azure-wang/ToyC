#include "optimizer.h"

#include "utils.h"

#include <memory>
#include <string>

namespace toyc {

void Optimizer::optimize(Program &program) {
  constTable_.push();
  for (auto &item : program.items) {
    if (auto *decl = dynamic_cast<TopDecl *>(item.get())) {
      foldExpr(decl->decl.init);
      int32_t v = 0;
      if (number(*decl->decl.init, v) && decl->decl.isConst)
        constTable_.declare(decl->decl.name, v);
    }
  }
  for (auto &item : program.items) {
    if (auto *fn = dynamic_cast<TopFunction *>(item.get())) {
      optimizeBlock(*fn->func.body);
    }
  }
  constTable_.pop();
}

void Optimizer::optimizeStmt(std::unique_ptr<Stmt> &stmt) {
  if (auto *block = dynamic_cast<BlockStmt *>(stmt.get())) optimizeBlock(*block);
  else if (auto *decl = dynamic_cast<DeclStmt *>(stmt.get())) {
    foldExpr(decl->decl.init);
    if (decl->decl.isConst) {
      int32_t v = 0;
      if (number(*decl->decl.init, v)) constTable_.declare(decl->decl.name, v);
    }
  }
  else if (auto *expr = dynamic_cast<ExprStmt *>(stmt.get())) foldExpr(expr->expr);
  else if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) foldExpr(assign->value);
  else if (auto *ifs = dynamic_cast<IfStmt *>(stmt.get())) {
    foldExpr(ifs->cond);
    optimizeStmt(ifs->thenBranch);
    if (ifs->elseBranch) optimizeStmt(ifs->elseBranch);
    int32_t v = 0;
    if (number(*ifs->cond, v)) {
      if (v != 0) stmt = std::move(ifs->thenBranch);
      else if (ifs->elseBranch) stmt = std::move(ifs->elseBranch);
      else stmt = std::make_unique<EmptyStmt>(ifs->loc);
    }
  } else if (auto *wh = dynamic_cast<WhileStmt *>(stmt.get())) {
    foldExpr(wh->cond);
    optimizeStmt(wh->body);
    int32_t v = 0;
    if (number(*wh->cond, v) && v == 0) stmt = std::make_unique<EmptyStmt>(wh->loc);
  } else if (auto *ret = dynamic_cast<ReturnStmt *>(stmt.get())) {
    if (ret->value) foldExpr(ret->value);
  }
}

void Optimizer::optimizeBlock(BlockStmt &block) {
  constTable_.push();
  std::vector<std::unique_ptr<Stmt>> kept;
  bool dead = false;
  for (auto &stmt : block.stmts) {
    if (dead) continue;
    optimizeStmt(stmt);
    dead = isTerminator(*stmt);
    kept.push_back(std::move(stmt));
  }
  block.stmts = std::move(kept);
  constTable_.pop();
  cseBlock(block);
}

bool Optimizer::isTerminator(const Stmt &stmt) const {
  if (dynamic_cast<const ReturnStmt *>(&stmt) || dynamic_cast<const BreakStmt *>(&stmt) ||
      dynamic_cast<const ContinueStmt *>(&stmt)) {
    return true;
  }
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    return !block->stmts.empty() && isTerminator(*block->stmts.back());
  }
  if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    return ifs->elseBranch && isTerminator(*ifs->thenBranch) && isTerminator(*ifs->elseBranch);
  }
  return false;
}

bool Optimizer::foldExpr(std::unique_ptr<Expr> &expr) {
  if (auto *var = dynamic_cast<VarExpr *>(expr.get())) {
    int32_t v = 0;
    if (number(*var, v)) {
      expr = std::make_unique<NumberExpr>(var->loc, v);
      return true;
    }
    return false;
  }
  if (auto *call = dynamic_cast<CallExpr *>(expr.get())) {
    for (auto &arg : call->args) foldExpr(arg);
    return false;
  }
  if (dynamic_cast<NumberExpr *>(expr.get())) return false;

  if (auto *un = dynamic_cast<UnaryExpr *>(expr.get())) {
    foldExpr(un->operand);
    int32_t v = 0;
    if (number(*un->operand, v)) {
      expr = std::make_unique<NumberExpr>(un->loc, applyUnary(un->op, v));
      return true;
    }
    if (un->op == UnaryOp::Plus) {
      expr = std::move(un->operand);
      return true;
    }
    return false;
  }
  if (auto *bin = dynamic_cast<BinaryExpr *>(expr.get())) {
    foldExpr(bin->lhs);
    foldExpr(bin->rhs);
    int32_t a = 0, b = 0;
    bool hasA = number(*bin->lhs, a);
    bool hasB = number(*bin->rhs, b);
    if (hasA && hasB) {
      expr = std::make_unique<NumberExpr>(bin->loc, applyBinary(bin->op, a, b));
      return true;
    }
    if (bin->op == BinaryOp::Add && hasB && b == 0) expr = std::move(bin->lhs);
    else if (bin->op == BinaryOp::Add && hasA && a == 0) expr = std::move(bin->rhs);
    else if (bin->op == BinaryOp::Sub && hasB && b == 0) expr = std::move(bin->lhs);
    else if (bin->op == BinaryOp::Mul && hasB && b == 1) expr = std::move(bin->lhs);
    else if (bin->op == BinaryOp::Mul && hasA && a == 1) expr = std::move(bin->rhs);
    else if (bin->op == BinaryOp::Mul && ((hasA && a == 0) || (hasB && b == 0))) expr = std::make_unique<NumberExpr>(bin->loc, 0);
    else if (bin->op == BinaryOp::Div && hasB && b == 1) expr = std::move(bin->lhs);
    else if (bin->op == BinaryOp::Mod && hasB && b == 1) expr = std::make_unique<NumberExpr>(bin->loc, 0);
    else return false;
    return true;
  }
  return false;
}

bool Optimizer::number(const Expr &expr, int32_t &value) const {
  if (auto *num = dynamic_cast<const NumberExpr *>(&expr)) {
    value = num->value;
    return true;
  }
  if (auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    auto *v = constTable_.find(var->name);
    if (v) { value = *v; return true; }
  }
  return false;
}

std::string Optimizer::exprKey(const Expr &expr) const {
  if (auto *num = dynamic_cast<const NumberExpr *>(&expr))
    return "\x01N" + std::to_string(num->value);
  if (auto *var = dynamic_cast<const VarExpr *>(&expr))
    return "\x01V" + var->name + "\x01";
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr))
    return "\x01U" + std::to_string(static_cast<int>(un->op)) + exprKey(*un->operand);
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr))
    return "\x01B" + std::to_string(static_cast<int>(bin->op)) + exprKey(*bin->lhs) + exprKey(*bin->rhs);
  if (auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    std::string key = "\x01C" + call->callee;
    for (const auto &arg : call->args) key += exprKey(*arg);
    return key;
  }
  return "";
}

static void invalidateVar(std::unordered_map<std::string, std::string> &m, const std::string &name) {
  std::string needle = "\x01V" + name + "\x01";
  for (auto it = m.begin(); it != m.end(); ) {
    if (it->first.find(needle) != std::string::npos || it->second == name)
      it = m.erase(it);
    else
      ++it;
  }
}

void Optimizer::cseBlock(BlockStmt &block) {
  cseMap_.clear();
  for (auto &stmt : block.stmts) cseStmt(stmt);
}

void Optimizer::cseStmt(std::unique_ptr<Stmt> &stmt) {
  if (auto *block = dynamic_cast<BlockStmt *>(stmt.get())) {
    cseBlock(*block);
    return;
  }
  if (auto *ifs = dynamic_cast<IfStmt *>(stmt.get())) {
    cseStmt(ifs->thenBranch);
    if (ifs->elseBranch) cseStmt(ifs->elseBranch);
    return;
  }
  if (auto *wh = dynamic_cast<WhileStmt *>(stmt.get())) {
    cseStmt(wh->body);
    return;
  }
  if (auto *decl = dynamic_cast<DeclStmt *>(stmt.get())) {
    invalidateVar(cseMap_, decl->decl.name);
    if (!decl->decl.isConst) {
      std::string key = exprKey(*decl->decl.init);
      if (!key.empty() && cseMap_.count(key))
        decl->decl.init = std::make_unique<VarExpr>(decl->decl.loc, cseMap_[key]);
      if (!key.empty()) cseMap_[key] = decl->decl.name;
    }
    return;
  }
  if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) {
    invalidateVar(cseMap_, assign->name);
    std::string key = exprKey(*assign->value);
    if (!key.empty() && cseMap_.count(key))
      assign->value = std::make_unique<VarExpr>(assign->value->loc, cseMap_[key]);
    if (!key.empty()) cseMap_[key] = assign->name;
    return;
  }
}

int32_t Optimizer::applyUnary(UnaryOp op, int32_t v) const {
  switch (op) {
  case UnaryOp::Plus: return v;
  case UnaryOp::Minus: return wrap32(-static_cast<int64_t>(v));
  case UnaryOp::Not: return v == 0 ? 1 : 0;
  }
  return v;
}

int32_t Optimizer::applyBinary(BinaryOp op, int32_t a, int32_t b) const {
  switch (op) {
  case BinaryOp::Add: return wrap32(static_cast<int64_t>(a) + b);
  case BinaryOp::Sub: return wrap32(static_cast<int64_t>(a) - b);
  case BinaryOp::Mul: return wrap32(static_cast<int64_t>(a) * b);
  case BinaryOp::Div: return b == 0 ? -1 : a / b;
  case BinaryOp::Mod: return b == 0 ? a : a % b;
  case BinaryOp::Lt: return a < b;
  case BinaryOp::Gt: return a > b;
  case BinaryOp::Le: return a <= b;
  case BinaryOp::Ge: return a >= b;
  case BinaryOp::Eq: return a == b;
  case BinaryOp::Ne: return a != b;
  case BinaryOp::And: return (a != 0) && (b != 0);
  case BinaryOp::Or: return (a != 0) || (b != 0);
  }
  return 0;
}

} // namespace toyc
