#include "optimizer.h"

#include "utils.h"

#include <memory>
#include <string>
#include <unordered_set>

namespace toyc {

static std::unique_ptr<Expr> cloneExpr(const Expr &expr) {
  if (auto *num = dynamic_cast<const NumberExpr *>(&expr))
    return std::make_unique<NumberExpr>(num->loc, num->value);
  if (auto *var = dynamic_cast<const VarExpr *>(&expr))
    return std::make_unique<VarExpr>(var->loc, var->name);
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr))
    return std::make_unique<UnaryExpr>(un->loc, un->op, cloneExpr(*un->operand));
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr))
    return std::make_unique<BinaryExpr>(bin->loc, bin->op,
        cloneExpr(*bin->lhs), cloneExpr(*bin->rhs));
  if (auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    std::vector<std::unique_ptr<Expr>> args;
    for (const auto &a : call->args) args.push_back(cloneExpr(*a));
    return std::make_unique<CallExpr>(call->loc, call->callee, std::move(args));
  }
  return nullptr;
}

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
  inlineCalls(program);
  for (auto &item : program.items) {
    if (auto *fn = dynamic_cast<TopFunction *>(item.get())) {
      optimizeBlock(*fn->func.body);
    }
  }
  constTable_.pop();
}

bool Optimizer::optimizeStmt(std::unique_ptr<Stmt> &stmt) {
  return optimizeStmt(stmt, 0);
}

bool Optimizer::optimizeStmt(std::unique_ptr<Stmt> &stmt, int depth) {
  if (depth > 100) return false;
  if (auto *block = dynamic_cast<BlockStmt *>(stmt.get())) {
    optimizeBlock(*block);
    if (block->stmts.empty()) { stmt = std::make_unique<EmptyStmt>(stmt->loc); return true; }
    return true;
  }
  if (auto *decl = dynamic_cast<DeclStmt *>(stmt.get())) {
    bool changed = foldExpr(decl->decl.init);
    if (decl->decl.isConst) {
      int32_t v = 0;
      if (number(*decl->decl.init, v)) constTable_.declare(decl->decl.name, v);
    }
    return changed;
  }
  if (auto *expr = dynamic_cast<ExprStmt *>(stmt.get())) return foldExpr(expr->expr);
  if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) return foldExpr(assign->value);
  if (auto *ifs = dynamic_cast<IfStmt *>(stmt.get())) {
    bool changed = foldExpr(ifs->cond);
    changed |= optimizeStmt(ifs->thenBranch, depth + 1);
    if (ifs->elseBranch) changed |= optimizeStmt(ifs->elseBranch, depth + 1);
    int32_t v = 0;
    if (number(*ifs->cond, v)) {
      if (v != 0) stmt = std::move(ifs->thenBranch);
      else if (ifs->elseBranch) stmt = std::move(ifs->elseBranch);
      else stmt = std::make_unique<EmptyStmt>(ifs->loc);
      return true;
    }
    // Remove empty then-branch if no else and condition has no side effects
    if (auto *tb = dynamic_cast<BlockStmt *>(ifs->thenBranch.get())) {
      if (tb->stmts.empty() && !ifs->elseBranch && !hasSideEffects(*ifs->cond)) {
        stmt = std::make_unique<EmptyStmt>(ifs->loc);
        return true;
      }
    }
    return changed;
  }
  if (auto *wh = dynamic_cast<WhileStmt *>(stmt.get())) {
    bool changed = foldExpr(wh->cond);
    changed |= optimizeStmt(wh->body, depth + 1);
    int32_t v = 0;
    if (number(*wh->cond, v) && v == 0) { stmt = std::make_unique<EmptyStmt>(wh->loc); return true; }
    // Remove empty loop body only if condition has no side effects
    if (auto *b = dynamic_cast<BlockStmt *>(wh->body.get())) {
      if (b->stmts.empty() && !hasSideEffects(*wh->cond)) {
        stmt = std::make_unique<EmptyStmt>(wh->loc); return true;
      }
    }
    return changed;
  }
  if (auto *ret = dynamic_cast<ReturnStmt *>(stmt.get())) {
    if (ret->value) return foldExpr(ret->value);
    return false;
  }
  return false;
}

void Optimizer::optimizeBlock(BlockStmt &block) {
  constTable_.push();
  for (int iter = 0; iter < 5; ++iter) {
    bool changed = false;
    for (auto &stmt : block.stmts)
      changed |= optimizeStmt(stmt);
    std::vector<std::unique_ptr<Stmt>> kept;
    bool dead = false;
    for (auto &stmt : block.stmts) {
      if (dead) { changed = true; continue; }
      dead = isTerminator(*stmt);
      kept.push_back(std::move(stmt));
    }
    if (kept.size() < block.stmts.size()) changed = true;
    block.stmts = std::move(kept);
    if (!changed) break;
  }
  constTable_.pop();
  hoistCommonSubexprs(block);
  cseBlock(block);
  eliminateDeadStores(block);
  hoistLoopInvariants(block);
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
  return foldExpr(expr, 0);
}

bool Optimizer::foldExpr(std::unique_ptr<Expr> &expr, int depth) {
  if (depth > 400) return false;
  if (auto *var = dynamic_cast<VarExpr *>(expr.get())) {
    int32_t v = 0;
    if (number(*var, v)) {
      expr = std::make_unique<NumberExpr>(var->loc, v);
      return true;
    }
    return false;
  }
  if (auto *call = dynamic_cast<CallExpr *>(expr.get())) {
    for (auto &arg : call->args) foldExpr(arg, depth + 1);
    return false;
  }
  if (dynamic_cast<NumberExpr *>(expr.get())) return false;

  if (auto *un = dynamic_cast<UnaryExpr *>(expr.get())) {
    foldExpr(un->operand, depth + 1);
    int32_t v = 0;
    if (number(*un->operand, v)) {
      expr = std::make_unique<NumberExpr>(un->loc, applyUnary(un->op, v));
      return true;
    }
    if (un->op == UnaryOp::Plus) {
      expr = std::move(un->operand);
      return true;
    }
    if (un->op == UnaryOp::Minus) {
      if (auto *inner = dynamic_cast<UnaryExpr *>(un->operand.get())) {
        if (inner->op == UnaryOp::Minus) {
          expr = std::move(inner->operand);
          return true;
        }
      }
      if (auto *inner = dynamic_cast<BinaryExpr *>(un->operand.get())) {
        if (inner->op == BinaryOp::Sub) {
          expr = std::make_unique<BinaryExpr>(un->loc, BinaryOp::Sub,
              std::move(inner->rhs), std::move(inner->lhs));
          foldExpr(expr, depth + 1);
          return true;
        }
      }
    }
    return false;
  }
  if (auto *bin = dynamic_cast<BinaryExpr *>(expr.get())) {
    foldExpr(bin->lhs, depth + 1);
    foldExpr(bin->rhs, depth + 1);
    if (bin->op == BinaryOp::Add) {
      if (auto *rhsNeg = dynamic_cast<UnaryExpr *>(bin->rhs.get())) {
        if (rhsNeg->op == UnaryOp::Minus) {
          bin->op = BinaryOp::Sub;
          bin->rhs = std::move(rhsNeg->operand);
          foldExpr(expr, depth + 1);
          return true;
        }
      }
      if (auto *lhsNeg = dynamic_cast<UnaryExpr *>(bin->lhs.get())) {
        if (lhsNeg->op == UnaryOp::Minus) {
          bin->op = BinaryOp::Sub;
          bin->lhs = std::move(bin->rhs);
          bin->rhs = std::move(lhsNeg->operand);
          foldExpr(expr, depth + 1);
          return true;
        }
      }
    }
    if (bin->op == BinaryOp::Sub) {
      if (auto *rhsNeg = dynamic_cast<UnaryExpr *>(bin->rhs.get())) {
        if (rhsNeg->op == UnaryOp::Minus) {
          bin->op = BinaryOp::Add;
          bin->rhs = std::move(rhsNeg->operand);
          foldExpr(expr, depth + 1);
          return true;
        }
      }
    }
    int32_t a = 0, b = 0;
    bool hasA = number(*bin->lhs, a);
    bool hasB = number(*bin->rhs, b);
    if (hasA && hasB) {
      expr = std::make_unique<NumberExpr>(bin->loc, applyBinary(bin->op, a, b));
      return true;
    }
    if ((bin->op == BinaryOp::Mul || bin->op == BinaryOp::Add) && hasA && !hasB) {
      std::swap(bin->lhs, bin->rhs);
      std::swap(a, b);
      std::swap(hasA, hasB);
    }
    if (hasA && !hasB && bin->op == BinaryOp::Sub) {
      // C - x → (-x) + C
      auto x = std::move(bin->rhs);
      auto neg = std::make_unique<UnaryExpr>(bin->loc, UnaryOp::Minus, std::move(x));
      expr = std::make_unique<BinaryExpr>(bin->loc, BinaryOp::Add,
          std::move(neg), std::move(bin->lhs));
      foldExpr(expr, depth + 1);
      return true;
    }
    if (bin->op == BinaryOp::Add && hasB && b == 0) { expr = std::move(bin->lhs); return true; }
    else if (bin->op == BinaryOp::Add && hasA && a == 0) { expr = std::move(bin->rhs); return true; }
    else if (bin->op == BinaryOp::Sub && hasB && b == 0) { expr = std::move(bin->lhs); return true; }
    else if (bin->op == BinaryOp::Sub && hasA && a == 0) {
      expr = std::make_unique<UnaryExpr>(bin->loc, UnaryOp::Minus, std::move(bin->rhs));
      foldExpr(expr, depth + 1);
      return true;
    }
    else if (bin->op == BinaryOp::Mul && hasB && b == 1) { expr = std::move(bin->lhs); return true; }
    else if (bin->op == BinaryOp::Mul && hasA && a == 1) { expr = std::move(bin->rhs); return true; }
    else if (bin->op == BinaryOp::Mul && ((hasA && a == 0) || (hasB && b == 0))) { expr = std::make_unique<NumberExpr>(bin->loc, 0); return true; }
    else if (bin->op == BinaryOp::Div && hasB && b == 1) { expr = std::move(bin->lhs); return true; }
    else if (bin->op == BinaryOp::Div && hasB && b == -1) {
      expr = std::make_unique<UnaryExpr>(bin->loc, UnaryOp::Minus, std::move(bin->lhs));
      foldExpr(expr, depth + 1);
      return true;
    }
    else if (bin->op == BinaryOp::Mod && hasB && b == 1) { expr = std::make_unique<NumberExpr>(bin->loc, 0); return true; }
    else if (bin->op == BinaryOp::Sub && !hasCall(*bin->lhs) && !hasCall(*bin->rhs)) {
      std::string lk = exprKey(*bin->lhs);
      std::string rk = exprKey(*bin->rhs);
      if (!lk.empty() && lk == rk) { expr = std::make_unique<NumberExpr>(bin->loc, 0); return true; }
    }
    else if ((bin->op == BinaryOp::Eq || bin->op == BinaryOp::Le || bin->op == BinaryOp::Ge) &&
             !hasCall(*bin->lhs) && !hasCall(*bin->rhs)) {
      std::string lk = exprKey(*bin->lhs);
      std::string rk = exprKey(*bin->rhs);
      if (!lk.empty() && lk == rk) { expr = std::make_unique<NumberExpr>(bin->loc, 1); return true; }
    }
    else if ((bin->op == BinaryOp::Ne || bin->op == BinaryOp::Lt || bin->op == BinaryOp::Gt) &&
             !hasCall(*bin->lhs) && !hasCall(*bin->rhs)) {
      std::string lk = exprKey(*bin->lhs);
      std::string rk = exprKey(*bin->rhs);
      if (!lk.empty() && lk == rk) { expr = std::make_unique<NumberExpr>(bin->loc, 0); return true; }
    }
    if (hasB) {
      // Mul associativity: (x * C1) * C2 -> x * (C1*C2), (C1 * x) * C2 -> x * (C1*C2)
      if (bin->op == BinaryOp::Mul) {
        if (auto *inner = dynamic_cast<BinaryExpr *>(bin->lhs.get())) {
          if (inner->op == BinaryOp::Mul) {
            int32_t c = 0;
            if (number(*inner->rhs, c)) {
              int32_t d = c * b;
              auto x = std::move(inner->lhs);
              expr = std::make_unique<BinaryExpr>(bin->loc, BinaryOp::Mul,
                  std::move(x), std::make_unique<NumberExpr>(bin->loc, d));
              return true;
            }
            if (number(*inner->lhs, c)) {
              int32_t d = c * b;
              auto x = std::move(inner->rhs);
              expr = std::make_unique<BinaryExpr>(bin->loc, BinaryOp::Mul,
                  std::move(x), std::make_unique<NumberExpr>(bin->loc, d));
              return true;
            }
          }
        }
      }
      if (auto *inner = dynamic_cast<BinaryExpr *>(bin->lhs.get())) {
        int32_t c = 0;
        if ((inner->op == BinaryOp::Add || inner->op == BinaryOp::Sub)
            && number(*inner->rhs, c)) {
          if (bin->op == BinaryOp::Add || bin->op == BinaryOp::Sub) {
            int32_t d = (bin->op == BinaryOp::Add)
                ? ((inner->op == BinaryOp::Add) ? (c + b) : (b - c))
                : ((inner->op == BinaryOp::Add) ? (c - b) : (-c - b));
            auto x = std::move(inner->lhs);
            if (d == 0) expr = std::move(x);
            else if (d > 0)
              expr = std::make_unique<BinaryExpr>(bin->loc, BinaryOp::Add,
                  std::move(x), std::make_unique<NumberExpr>(bin->loc, d));
            else
              expr = std::make_unique<BinaryExpr>(bin->loc, BinaryOp::Sub,
                  std::move(x), std::make_unique<NumberExpr>(bin->loc, -d));
            return true;
          }
        }
      }
    }
    if (hasA && bin->op == BinaryOp::Add) {
      if (auto *inner = dynamic_cast<BinaryExpr *>(bin->rhs.get())) {
        int32_t c = 0;
        if ((inner->op == BinaryOp::Add || inner->op == BinaryOp::Sub)
            && number(*inner->rhs, c)) {
          int32_t d = (inner->op == BinaryOp::Add) ? (a + c) : (a - c);
          auto x = std::move(inner->lhs);
          if (d == 0) expr = std::move(x);
          else if (d > 0)
            expr = std::make_unique<BinaryExpr>(bin->loc, BinaryOp::Add,
                std::move(x), std::make_unique<NumberExpr>(bin->loc, d));
          else
            expr = std::make_unique<BinaryExpr>(bin->loc, BinaryOp::Sub,
                std::move(x), std::make_unique<NumberExpr>(bin->loc, -d));
          return true;
        }
      }
    }
    return false;
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
  return exprKey(expr, 0);
}

std::string Optimizer::exprKey(const Expr &expr, int depth) const {
  if (depth > 200) return "";
  if (auto *num = dynamic_cast<const NumberExpr *>(&expr))
    return "\x01N" + std::to_string(num->value);
  if (auto *var = dynamic_cast<const VarExpr *>(&expr))
    return "\x01V" + var->name + "\x01";
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr))
    return "\x01U" + std::to_string(static_cast<int>(un->op)) + exprKey(*un->operand, depth + 1);
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr))
    return "\x01B" + std::to_string(static_cast<int>(bin->op)) + exprKey(*bin->lhs, depth + 1) + exprKey(*bin->rhs, depth + 1);
  if (auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    std::string key = "\x01C" + call->callee;
    for (const auto &arg : call->args) key += exprKey(*arg, depth + 1);
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

bool Optimizer::hasCall(const Expr &expr) const {
  if (dynamic_cast<const CallExpr *>(&expr)) return true;
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr))
    return hasCall(*un->operand);
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr))
    return hasCall(*bin->lhs) || hasCall(*bin->rhs);
  return false;
}

void Optimizer::countSubexprs(const Expr &expr,
    std::unordered_map<std::string, int> &counts) const {
  if (dynamic_cast<const NumberExpr *>(&expr)) return;
  if (dynamic_cast<const VarExpr *>(&expr)) return;
  if (hasCall(expr)) return;
  std::string key = exprKey(expr);
  if (!key.empty()) counts[key]++;
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr))
    countSubexprs(*un->operand, counts);
  else if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr)) {
    countSubexprs(*bin->lhs, counts);
    countSubexprs(*bin->rhs, counts);
  }
}

std::unique_ptr<Expr> Optimizer::replaceSubexprs(std::unique_ptr<Expr> expr,
    const std::unordered_map<std::string, int> &hoistKeys,
    std::unordered_map<std::string, std::string> &tempNames,
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> &newTemps,
    int &counter, SourceLoc loc) {
  if (auto *un = dynamic_cast<UnaryExpr *>(expr.get())) {
    un->operand = replaceSubexprs(std::move(un->operand),
        hoistKeys, tempNames, newTemps, counter, loc);
  } else if (auto *bin = dynamic_cast<BinaryExpr *>(expr.get())) {
    bin->lhs = replaceSubexprs(std::move(bin->lhs),
        hoistKeys, tempNames, newTemps, counter, loc);
    bin->rhs = replaceSubexprs(std::move(bin->rhs),
        hoistKeys, tempNames, newTemps, counter, loc);
  } else if (auto *call = dynamic_cast<CallExpr *>(expr.get())) {
    for (auto &arg : call->args)
      arg = replaceSubexprs(std::move(arg),
          hoistKeys, tempNames, newTemps, counter, loc);
  }
  std::string key = exprKey(*expr);
  if (!key.empty() && hoistKeys.count(key)) {
    auto &name = tempNames[key];
    if (name.empty()) {
      name = "_cse_" + std::to_string(counter++);
      newTemps.emplace_back(name, cloneExpr(*expr));
    }
    return std::make_unique<VarExpr>(loc, name);
  }
  return expr;
}

bool Optimizer::hoistInStmt(std::unique_ptr<Stmt> &stmt, int &cseCounter,
    std::vector<std::unique_ptr<Stmt>> &preStmts) {
  if (auto *block = dynamic_cast<BlockStmt *>(stmt.get())) {
    hoistCommonSubexprs(*block);
    return false;
  }
  if (auto *ifs = dynamic_cast<IfStmt *>(stmt.get())) {
    bool changed = hoistInStmt(ifs->thenBranch, cseCounter, preStmts);
    if (ifs->elseBranch)
      changed |= hoistInStmt(ifs->elseBranch, cseCounter, preStmts);
    return changed;
  }
  if (auto *wh = dynamic_cast<WhileStmt *>(stmt.get())) {
    return hoistInStmt(wh->body, cseCounter, preStmts);
  }
  Expr *target = nullptr;
  SourceLoc loc;
  if (auto *decl = dynamic_cast<DeclStmt *>(stmt.get())) {
    target = decl->decl.init.get();
    loc = decl->decl.loc;
  } else if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) {
    target = assign->value.get();
    loc = assign->loc;
  } else if (auto *es = dynamic_cast<ExprStmt *>(stmt.get())) {
    target = es->expr.get();
    loc = es->loc;
  }
  if (!target) return false;

  std::unordered_map<std::string, int> counts;
  countSubexprs(*target, counts);

  std::unordered_map<std::string, int> hoistKeys;
  for (const auto &kv : counts)
    if (kv.second >= 2) hoistKeys.insert(kv);
  if (hoistKeys.empty()) return false;

  std::unordered_map<std::string, std::string> tempNames;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> newTemps;

  if (auto *decl = dynamic_cast<DeclStmt *>(stmt.get())) {
    decl->decl.init = replaceSubexprs(std::move(decl->decl.init),
        hoistKeys, tempNames, newTemps, cseCounter, loc);
  } else if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) {
    assign->value = replaceSubexprs(std::move(assign->value),
        hoistKeys, tempNames, newTemps, cseCounter, loc);
  } else if (auto *es = dynamic_cast<ExprStmt *>(stmt.get())) {
    es->expr = replaceSubexprs(std::move(es->expr),
        hoistKeys, tempNames, newTemps, cseCounter, loc);
  }

  for (auto &kv : newTemps) {
    Decl decl;
    decl.loc = loc;
    decl.name = kv.first;
    decl.init = std::move(kv.second);
    preStmts.push_back(std::make_unique<DeclStmt>(loc, std::move(decl)));
  }
  return !newTemps.empty();
}

void Optimizer::hoistCommonSubexprs(BlockStmt &block) {
  int cseCounter = 0;
  std::vector<std::unique_ptr<Stmt>> newStmts;
  for (auto &stmt : block.stmts) {
    std::vector<std::unique_ptr<Stmt>> preStmts;
    hoistInStmt(stmt, cseCounter, preStmts);
    for (auto &pre : preStmts)
      newStmts.push_back(std::move(pre));
    newStmts.push_back(std::move(stmt));
  }
  block.stmts = std::move(newStmts);
}

bool Optimizer::hasSideEffects(const Expr &expr) const {
  return hasCall(expr);
}

void Optimizer::collectReadVars(const Expr &expr,
    std::unordered_set<std::string> &vars) const {
  if (auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    vars.insert(var->name);
    return;
  }
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr))
    collectReadVars(*un->operand, vars);
  else if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr)) {
    collectReadVars(*bin->lhs, vars);
    collectReadVars(*bin->rhs, vars);
  } else if (auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    for (const auto &arg : call->args)
      collectReadVars(*arg, vars);
  }
}

void Optimizer::collectReadVars(const Stmt &stmt,
    std::unordered_set<std::string> &vars) const {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    // For inner blocks, collect from all statements
    // but only top-level liveness crosses block boundaries
    for (const auto &s : block->stmts)
      collectReadVars(*s, vars);
  } else if (auto *es = dynamic_cast<const ExprStmt *>(&stmt))
    collectReadVars(*es->expr, vars);
  else if (auto *assign = dynamic_cast<const AssignStmt *>(&stmt))
    collectReadVars(*assign->value, vars);
  else if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt))
    collectReadVars(*decl->decl.init, vars);
  else if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    collectReadVars(*ifs->cond, vars);
    collectReadVars(*ifs->thenBranch, vars);
    if (ifs->elseBranch) collectReadVars(*ifs->elseBranch, vars);
  } else if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt)) {
    collectReadVars(*wh->cond, vars);
    collectReadVars(*wh->body, vars);
  } else if (auto *ret = dynamic_cast<const ReturnStmt *>(&stmt)) {
    if (ret->value) collectReadVars(*ret->value, vars);
  }
}

void Optimizer::eliminateDeadStores(BlockStmt &block) {
  // Recurse into nested blocks first
  for (auto &stmt : block.stmts) {
    if (auto *inner = dynamic_cast<BlockStmt *>(stmt.get()))
      eliminateDeadStores(*inner);
    else if (auto *ifs = dynamic_cast<IfStmt *>(stmt.get())) {
      if (auto *b = dynamic_cast<BlockStmt *>(ifs->thenBranch.get()))
        eliminateDeadStores(*b);
      if (ifs->elseBranch)
        if (auto *b = dynamic_cast<BlockStmt *>(ifs->elseBranch.get()))
          eliminateDeadStores(*b);
    } else if (auto *wh = dynamic_cast<WhileStmt *>(stmt.get())) {
      if (auto *b = dynamic_cast<BlockStmt *>(wh->body.get()))
        eliminateDeadStores(*b);
    }
  }

  std::unordered_set<std::string> live;
  for (auto it = block.stmts.rbegin(); it != block.stmts.rend(); ++it) {
    auto &stmt = *it;
    collectReadVars(*stmt, live);
    if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) {
      if (!live.count(assign->name)) {
        if (hasSideEffects(*assign->value))
          stmt = std::make_unique<ExprStmt>(assign->loc, std::move(assign->value));
        else
          stmt = std::make_unique<EmptyStmt>(assign->loc);
      }
      live.erase(assign->name);
    }
    if (auto *decl = dynamic_cast<DeclStmt *>(stmt.get())) {
      if (!decl->decl.isConst)
        live.erase(decl->decl.name);
    }
  }
  std::vector<std::unique_ptr<Stmt>> kept;
  for (auto &stmt : block.stmts) {
    if (dynamic_cast<EmptyStmt *>(stmt.get())) continue;
    kept.push_back(std::move(stmt));
  }
  block.stmts = std::move(kept);
}

void Optimizer::computeModifiedVars(const Stmt &stmt,
    std::unordered_set<std::string> &vars) const {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    for (const auto &s : block->stmts)
      computeModifiedVars(*s, vars);
  } else if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt)) {
    if (!decl->decl.isConst) vars.insert(decl->decl.name);
  } else if (auto *assign = dynamic_cast<const AssignStmt *>(&stmt)) {
    vars.insert(assign->name);
  } else if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    computeModifiedVars(*ifs->thenBranch, vars);
    if (ifs->elseBranch) computeModifiedVars(*ifs->elseBranch, vars);
  } else if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt)) {
    computeModifiedVars(*wh->body, vars);
  }
}

bool Optimizer::exprRefsModified(const Expr &expr,
    const std::unordered_set<std::string> &mustSet) const {
  if (auto *var = dynamic_cast<const VarExpr *>(&expr))
    return mustSet.count(var->name) != 0;
  if (dynamic_cast<const CallExpr *>(&expr))
    return true; // conservative: call may modify anything
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr))
    return exprRefsModified(*un->operand, mustSet);
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr))
    return exprRefsModified(*bin->lhs, mustSet) ||
           exprRefsModified(*bin->rhs, mustSet);
  return false;
}

bool Optimizer::isInvariant(const Stmt &stmt,
    const std::unordered_set<std::string> &mustSet) const {
  if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt))
    return !exprRefsModified(*decl->decl.init, mustSet);
  if (auto *assign = dynamic_cast<const AssignStmt *>(&stmt))
    return !exprRefsModified(*assign->value, mustSet)
        && mustSet.count(assign->name) == 0;
  return false;
}

static void collectAssignedVars(const Stmt &stmt,
    std::unordered_set<std::string> &vars) {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    for (const auto &s : block->stmts)
      collectAssignedVars(*s, vars);
  } else if (auto *assign = dynamic_cast<const AssignStmt *>(&stmt)) {
    vars.insert(assign->name);
  } else if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    collectAssignedVars(*ifs->thenBranch, vars);
    if (ifs->elseBranch) collectAssignedVars(*ifs->elseBranch, vars);
  } else if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt)) {
    collectAssignedVars(*wh->body, vars);
  }
}

void Optimizer::collectInnerDecls(const Stmt &stmt,
    std::unordered_set<std::string> &decls) const {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    for (const auto &s : block->stmts) {
      if (auto *inner = dynamic_cast<const BlockStmt *>(s.get())) {
        for (const auto &ss : inner->stmts)
          if (auto *d = dynamic_cast<const DeclStmt *>(ss.get()))
            decls.insert(d->decl.name);
        collectInnerDecls(*inner, decls);
      } else if (auto *ifs = dynamic_cast<const IfStmt *>(s.get())) {
        collectInnerDecls(*ifs->thenBranch, decls);
        if (ifs->elseBranch) collectInnerDecls(*ifs->elseBranch, decls);
      } else if (auto *wh = dynamic_cast<const WhileStmt *>(s.get())) {
        collectInnerDecls(*wh->body, decls);
      }
    }
  }
}

void Optimizer::hoistLoopInvariants(BlockStmt &block) {
  // Recurse first (inner loops first)
  for (auto &stmt : block.stmts) {
    if (auto *ifs = dynamic_cast<IfStmt *>(stmt.get())) {
      if (auto *b = dynamic_cast<BlockStmt *>(ifs->thenBranch.get()))
        hoistLoopInvariants(*b);
      if (ifs->elseBranch)
        if (auto *b = dynamic_cast<BlockStmt *>(ifs->elseBranch.get()))
          hoistLoopInvariants(*b);
    } else if (auto *inner = dynamic_cast<BlockStmt *>(stmt.get())) {
      hoistLoopInvariants(*inner);
    }
  }
  // Now process while loops in this block and hoist invariants
  std::vector<std::unique_ptr<Stmt>> newStmts;
  for (auto &stmt : block.stmts) {
    if (auto *wh = dynamic_cast<WhileStmt *>(stmt.get())) {
      auto *body = dynamic_cast<BlockStmt *>(wh->body.get());
      if (!body) { newStmts.push_back(std::move(stmt)); continue; }

      std::unordered_set<std::string> mustSet;
      computeModifiedVars(*wh->body, mustSet);
      collectReadVars(*wh->cond, mustSet);

      // Collect assigned vars (only AssignStmt, not DeclStmt) for DeclStmt check
      std::unordered_set<std::string> assignedSet;
      collectAssignedVars(*wh->body, assignedSet);

      // Find and hoist invariant statements
      std::vector<std::unique_ptr<Stmt>> keptBody;
      for (auto &bs : body->stmts) {
        bool inv = false;
        if (auto *decl = dynamic_cast<const DeclStmt *>(bs.get())) {
          inv = !exprRefsModified(*decl->decl.init, mustSet)
              && assignedSet.count(decl->decl.name) == 0;
        } else if (auto *assign = dynamic_cast<const AssignStmt *>(bs.get())) {
          inv = !exprRefsModified(*assign->value, mustSet)
              && mustSet.count(assign->name) == 0;
        }
        if (inv) {
          newStmts.push_back(std::move(bs));
        } else {
          keptBody.push_back(std::move(bs));
        }
      }
      body->stmts = std::move(keptBody);
      hoistLoopInvariants(*body);
    }
    newStmts.push_back(std::move(stmt));
  }
  block.stmts = std::move(newStmts);
}

// ─── Function inlining ───────────────────────────────────────────

std::unique_ptr<Stmt> Optimizer::cloneStmt(const Stmt &stmt) {
  return cloneStmt(stmt, 0);
}

std::unique_ptr<Stmt> Optimizer::cloneStmt(const Stmt &stmt, int depth) {
  if (depth > 200) return std::make_unique<EmptyStmt>(stmt.loc);
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt))
    return cloneBlock(*block, depth + 1);
  if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt)) {
    Decl d;
    d.loc = decl->decl.loc;
    d.isConst = decl->decl.isConst;
    d.name = decl->decl.name;
    d.init = cloneExpr(*decl->decl.init);
    return std::make_unique<DeclStmt>(decl->loc, std::move(d));
  }
  if (auto *es = dynamic_cast<const ExprStmt *>(&stmt))
    return std::make_unique<ExprStmt>(es->loc, cloneExpr(*es->expr));
  if (auto *assign = dynamic_cast<const AssignStmt *>(&stmt))
    return std::make_unique<AssignStmt>(assign->loc, assign->name,
        cloneExpr(*assign->value));
  if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt))
    return std::make_unique<IfStmt>(ifs->loc, cloneExpr(*ifs->cond),
        cloneStmt(*ifs->thenBranch, depth + 1),
        ifs->elseBranch ? cloneStmt(*ifs->elseBranch, depth + 1) : nullptr);
  if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt))
    return std::make_unique<WhileStmt>(wh->loc, cloneExpr(*wh->cond),
        cloneStmt(*wh->body, depth + 1));
  if (auto *ret = dynamic_cast<const ReturnStmt *>(&stmt))
    return std::make_unique<ReturnStmt>(ret->loc,
        ret->value ? cloneExpr(*ret->value) : nullptr);
  if (dynamic_cast<const BreakStmt *>(&stmt))
    return std::make_unique<BreakStmt>(stmt.loc);
  if (dynamic_cast<const ContinueStmt *>(&stmt))
    return std::make_unique<ContinueStmt>(stmt.loc);
  if (dynamic_cast<const EmptyStmt *>(&stmt))
    return std::make_unique<EmptyStmt>(stmt.loc);
  return nullptr;
}

std::unique_ptr<BlockStmt> Optimizer::cloneBlock(const BlockStmt &block) {
  return cloneBlock(block, 0);
}

std::unique_ptr<BlockStmt> Optimizer::cloneBlock(const BlockStmt &block, int depth) {
  if (depth > 200) return std::make_unique<BlockStmt>(block.loc);
  auto b = std::make_unique<BlockStmt>(block.loc);
  for (const auto &s : block.stmts)
    b->stmts.push_back(cloneStmt(*s, depth + 1));
  return b;
}

static bool callsSelf(const Expr &expr, const std::string &name) {
  if (auto *call = dynamic_cast<const CallExpr *>(&expr))
    return call->callee == name;
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr))
    return callsSelf(*un->operand, name);
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr))
    return callsSelf(*bin->lhs, name) || callsSelf(*bin->rhs, name);
  return false;
}

static bool callsSelf(const Stmt &stmt, const std::string &name) {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    for (const auto &s : block->stmts)
      if (callsSelf(*s, name)) return true;
  } else if (auto *es = dynamic_cast<const ExprStmt *>(&stmt))
    return callsSelf(*es->expr, name);
  else if (auto *assign = dynamic_cast<const AssignStmt *>(&stmt))
    return callsSelf(*assign->value, name);
  else if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt))
    return callsSelf(*decl->decl.init, name);
  else if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt))
    return callsSelf(*ifs->cond, name) ||
           callsSelf(*ifs->thenBranch, name) ||
           (ifs->elseBranch && callsSelf(*ifs->elseBranch, name));
  else if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt))
    return callsSelf(*wh->cond, name) || callsSelf(*wh->body, name);
  else if (auto *ret = dynamic_cast<const ReturnStmt *>(&stmt))
    return ret->value && callsSelf(*ret->value, name);
  return false;
}

bool Optimizer::isInlinable(const Function &fn) {
  if (!fn.body) return false;
  if (fn.body->stmts.empty()) return false;
  if (callsSelf(*fn.body, fn.name)) return false;
  int stmtCount = 0;
  for (const auto &s : fn.body->stmts) {
    stmtCount++;
    if (auto *inner = dynamic_cast<const BlockStmt *>(s.get()))
      stmtCount += static_cast<int>(inner->stmts.size()) - 1;
  }
  return stmtCount <= 5;
}

static void renameInExpr(Expr &expr,
    const std::unordered_map<std::string, std::string> &rmap, int depth = 0) {
  if (depth > 200) return;
  if (auto *var = dynamic_cast<VarExpr *>(&expr)) {
    auto it = rmap.find(var->name);
    if (it != rmap.end()) var->name = it->second;
  } else if (auto *un = dynamic_cast<UnaryExpr *>(&expr)) {
    renameInExpr(*un->operand, rmap, depth + 1);
  } else if (auto *bin = dynamic_cast<BinaryExpr *>(&expr)) {
    renameInExpr(*bin->lhs, rmap, depth + 1);
    renameInExpr(*bin->rhs, rmap, depth + 1);
  } else if (auto *call = dynamic_cast<CallExpr *>(&expr)) {
    for (auto &arg : call->args)
      renameInExpr(*arg, rmap, depth + 1);
  }
}

static void renameInStmt(Stmt &stmt,
    const std::unordered_map<std::string, std::string> &rmap, int depth = 0) {
  if (depth > 200) return;
  if (auto *block = dynamic_cast<BlockStmt *>(&stmt)) {
    for (auto &s : block->stmts) renameInStmt(*s, rmap, depth + 1);
  } else if (auto *decl = dynamic_cast<DeclStmt *>(&stmt)) {
    auto it = rmap.find(decl->decl.name);
    if (it != rmap.end()) decl->decl.name = it->second;
    renameInExpr(*decl->decl.init, rmap);
  } else if (auto *es = dynamic_cast<ExprStmt *>(&stmt)) {
    renameInExpr(*es->expr, rmap);
  } else if (auto *assign = dynamic_cast<AssignStmt *>(&stmt)) {
    auto it = rmap.find(assign->name);
    if (it != rmap.end()) assign->name = it->second;
    renameInExpr(*assign->value, rmap);
  } else if (auto *ifs = dynamic_cast<IfStmt *>(&stmt)) {
    renameInExpr(*ifs->cond, rmap);
    renameInStmt(*ifs->thenBranch, rmap, depth + 1);
    if (ifs->elseBranch) renameInStmt(*ifs->elseBranch, rmap, depth + 1);
  } else if (auto *wh = dynamic_cast<WhileStmt *>(&stmt)) {
    renameInExpr(*wh->cond, rmap);
    renameInStmt(*wh->body, rmap, depth + 1);
  } else if (auto *ret = dynamic_cast<ReturnStmt *>(&stmt)) {
    if (ret->value) renameInExpr(*ret->value, rmap);
  }
}

static void collectLocalNames(const Stmt &stmt,
    std::unordered_set<std::string> &names) {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    for (const auto &s : block->stmts) collectLocalNames(*s, names);
  } else if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt)) {
    if (decl->decl.name.compare(0, 5, "_inl_") != 0)
      names.insert(decl->decl.name);
  } else if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    collectLocalNames(*ifs->thenBranch, names);
    if (ifs->elseBranch) collectLocalNames(*ifs->elseBranch, names);
  } else if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt)) {
    collectLocalNames(*wh->body, names);
  }
}

static const int MAX_INLINE_DEPTH = 4;

std::unique_ptr<Expr> Optimizer::inlineInExpr(std::unique_ptr<Expr> expr,
    const std::unordered_map<std::string, const Function *> &funcMap,
    const std::string &currentFn,
    std::vector<std::unique_ptr<Stmt>> &preStmts,
    int &counter, SourceLoc loc) {
  // Guard against excessive inlining
  if (counter > 50) return expr;
  // Bottom-up: recurse first
  if (auto *un = dynamic_cast<UnaryExpr *>(expr.get())) {
    un->operand = inlineInExpr(std::move(un->operand),
        funcMap, currentFn, preStmts, counter, loc);
  } else if (auto *bin = dynamic_cast<BinaryExpr *>(expr.get())) {
    bin->lhs = inlineInExpr(std::move(bin->lhs),
        funcMap, currentFn, preStmts, counter, loc);
    bin->rhs = inlineInExpr(std::move(bin->rhs),
        funcMap, currentFn, preStmts, counter, loc);
  } else if (auto *call = dynamic_cast<CallExpr *>(expr.get())) {
    // Inline arguments first
    for (auto &arg : call->args)
      arg = inlineInExpr(std::move(arg), funcMap, currentFn,
          preStmts, counter, loc);
    // Check if this call can be inlined
    auto it = funcMap.find(call->callee);
    if (it != funcMap.end() && call->callee != currentFn &&
        isInlinable(*it->second)) {
      const Function &fn = *it->second;
      std::string prefix = "_inl_" + std::to_string(counter++) + "_";
      std::unordered_map<std::string, std::string> rmap;
      for (size_t i = 0; i < fn.params.size(); ++i)
        rmap[fn.params[i].name] = prefix + fn.params[i].name;
      std::unordered_set<std::string> localNames;
      collectLocalNames(*fn.body, localNames);
      for (const auto &name : localNames)
        rmap[name] = prefix + name;
      auto cloned = cloneBlock(*fn.body);
      renameInStmt(*cloned, rmap);
      auto &lastStmt = cloned->stmts.back();
      ReturnStmt *ret = dynamic_cast<ReturnStmt *>(lastStmt.get());
      std::string retName = prefix + "ret";
      for (size_t i = 0; i < fn.params.size(); ++i) {
        Decl d;
        d.loc = loc;
        d.name = rmap[fn.params[i].name];
        d.init = std::move(call->args[i]);
        preStmts.push_back(std::make_unique<DeclStmt>(loc, std::move(d)));
      }
      for (size_t i = 0; i + 1 < cloned->stmts.size(); ++i)
        preStmts.push_back(std::move(cloned->stmts[i]));
      {
        Decl d;
        d.loc = loc;
        d.name = retName;
        d.init = (ret && ret->value) ? std::move(ret->value)
            : std::unique_ptr<Expr>(std::make_unique<NumberExpr>(loc, 0));
        preStmts.push_back(std::make_unique<DeclStmt>(loc, std::move(d)));
      }
      return std::make_unique<VarExpr>(loc, retName);
    }
  }
  return expr;
}

void Optimizer::inlineInBlock(BlockStmt &block,
    const std::unordered_map<std::string, const Function *> &funcMap,
    const std::string &currentFn, int &counter) {
  for (auto &stmt : block.stmts) {
    if (auto *inner = dynamic_cast<BlockStmt *>(stmt.get()))
      inlineInBlock(*inner, funcMap, currentFn, counter);
    else if (auto *ifs = dynamic_cast<IfStmt *>(stmt.get())) {
      if (auto *b = dynamic_cast<BlockStmt *>(ifs->thenBranch.get()))
        inlineInBlock(*b, funcMap, currentFn, counter);
      if (ifs->elseBranch)
        if (auto *b = dynamic_cast<BlockStmt *>(ifs->elseBranch.get()))
          inlineInBlock(*b, funcMap, currentFn, counter);
    } else if (auto *wh = dynamic_cast<WhileStmt *>(stmt.get())) {
      if (auto *b = dynamic_cast<BlockStmt *>(wh->body.get()))
        inlineInBlock(*b, funcMap, currentFn, counter);
    }
  }
  std::vector<std::unique_ptr<Stmt>> newStmts;
  for (auto &stmt : block.stmts) {
    std::vector<std::unique_ptr<Stmt>> preStmts;
    if (auto *decl = dynamic_cast<DeclStmt *>(stmt.get())) {
      decl->decl.init = inlineInExpr(std::move(decl->decl.init),
          funcMap, currentFn, preStmts, counter, decl->decl.loc);
    } else if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) {
      assign->value = inlineInExpr(std::move(assign->value),
          funcMap, currentFn, preStmts, counter, assign->loc);
    } else if (auto *es = dynamic_cast<ExprStmt *>(stmt.get())) {
      es->expr = inlineInExpr(std::move(es->expr),
          funcMap, currentFn, preStmts, counter, es->loc);
    } else if (auto *ret = dynamic_cast<ReturnStmt *>(stmt.get())) {
      if (ret->value)
        ret->value = inlineInExpr(std::move(ret->value),
            funcMap, currentFn, preStmts, counter, ret->loc);
    } else if (auto *ifs = dynamic_cast<IfStmt *>(stmt.get())) {
      ifs->cond = inlineInExpr(std::move(ifs->cond),
          funcMap, currentFn, preStmts, counter, ifs->loc);
    } else if (auto *wh = dynamic_cast<WhileStmt *>(stmt.get())) {
      wh->cond = inlineInExpr(std::move(wh->cond),
          funcMap, currentFn, preStmts, counter, wh->loc);
    }
    for (auto &pre : preStmts)
      newStmts.push_back(std::move(pre));
    newStmts.push_back(std::move(stmt));
  }
  block.stmts = std::move(newStmts);
}

void Optimizer::inlineCalls(Program &program) {
  std::unordered_map<std::string, const Function *> funcMap;
  std::unordered_set<std::string> noInline;
  for (const auto &item : program.items) {
    if (auto *tf = dynamic_cast<TopFunction *>(item.get()))
      funcMap[tf->func.name] = &tf->func;
  }
  // Detect mutual/simple recursion and mark for no inlining
  for (auto &kv : funcMap) {
    for (auto &kv2 : funcMap) {
      if (kv.first == kv2.first) continue;
      if (callsSelf(*kv.second->body, kv2.first) &&
          callsSelf(*kv2.second->body, kv.first)) {
        noInline.insert(kv.first);
        noInline.insert(kv2.first);
      }
    }
  }
  // Remove non-inlinable functions from the map
  for (const auto &name : noInline) funcMap.erase(name);
  bool changed = true;
  int counter = 0;
  int maxIter = 3;
  while (changed && maxIter-- > 0) {
    changed = false;
    for (auto &item : program.items) {
      if (auto *tf = dynamic_cast<TopFunction *>(item.get())) {
        int oldCounter = counter;
        inlineInBlock(*tf->func.body, funcMap, tf->func.name, counter);
        if (counter != oldCounter) changed = true;
      }
    }
  }
}

} // namespace toyc
