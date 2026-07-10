#include "sema.h"

#include "utils.h"

#include <sstream>

namespace toyc {

void Sema::error(SourceLoc loc, const std::string &message) const {
  throw CompileError(formatLoc(loc) + ": semantic error: " + message);
}

void Sema::analyze(const Program &program) {
  vars_.push();
  bool hasMain = false;
  for (const auto &item : program.items) {
    if (auto *decl = dynamic_cast<TopDecl *>(item.get())) {
      analyzeTopDecl(decl->decl);
    } else if (auto *topFn = dynamic_cast<TopFunction *>(item.get())) {
      const Function &fn = topFn->func;
      if (functions_.count(fn.name)) error(fn.loc, "duplicate function '" + fn.name + "'");
      functions_[fn.name] = FunctionSig{fn.returnType, static_cast<int>(fn.params.size())};
      if (fn.name == "main") {
        hasMain = true;
        if (fn.returnType != Type::Int || !fn.params.empty()) {
          error(fn.loc, "main must be 'int main()'");
        }
      }
      analyzeFunction(fn);
    }
  }
  if (!hasMain) error(SourceLoc{1, 1}, "program must define int main()");
  vars_.pop();
}

void Sema::analyzeTopDecl(const Decl &decl) {
  auto val = evalConst(*decl.init);
  if (!val) error(decl.loc, "global declaration initializer must be compile-time constant");
  if (!vars_.declare(decl.name, VarInfo{decl.isConst, decl.isConst ? val : std::optional<int32_t>{}})) {
    error(decl.loc, "duplicate global name '" + decl.name + "'");
  }
}

void Sema::analyzeFunction(const Function &fn) {
  currentFunction_ = fn.name;
  currentReturn_ = fn.returnType;
  vars_.push();
  for (const auto &param : fn.params) {
    if (!vars_.declare(param.name, VarInfo{false, std::nullopt})) {
      error(param.loc, "duplicate parameter '" + param.name + "'");
    }
  }
  bool returns = analyzeStmt(*fn.body);
  if (fn.returnType == Type::Int && !returns) {
    error(fn.loc, "int function '" + fn.name + "' may fall through without return");
  }
  vars_.pop();
  currentFunction_.clear();
}

void Sema::analyzeBlock(const BlockStmt &block, bool createScope) {
  if (createScope) vars_.push();
  for (const auto &stmt : block.stmts) analyzeStmt(*stmt);
  if (createScope) vars_.pop();
}

bool Sema::analyzeStmt(const Stmt &stmt) {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    bool always = false;
    vars_.push();
    for (const auto &s : block->stmts) {
      bool r = analyzeStmt(*s);
      always = always || r;
      if (always) {
        // Remaining statements are unreachable but still parsed; keep analysis permissive.
      }
    }
    vars_.pop();
    return always;
  }
  if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt)) {
    if (decl->decl.isConst) {
      auto val = evalConst(*decl->decl.init);
      if (!val) error(decl->loc, "const initializer must be compile-time constant");
      if (!vars_.declare(decl->decl.name, VarInfo{true, val})) error(decl->loc, "duplicate name in same scope");
    } else {
      analyzeExpr(*decl->decl.init, true);
      if (!vars_.declare(decl->decl.name, VarInfo{false, std::nullopt})) error(decl->loc, "duplicate name in same scope");
    }
    return false;
  }
  if (auto *expr = dynamic_cast<const ExprStmt *>(&stmt)) {
    analyzeExpr(*expr->expr, false);
    return false;
  }
  if (auto *assign = dynamic_cast<const AssignStmt *>(&stmt)) {
    auto *sym = vars_.find(assign->name);
    if (!sym) error(assign->loc, "assignment to undeclared variable '" + assign->name + "'");
    if (sym->isConst) error(assign->loc, "cannot assign to const '" + assign->name + "'");
    analyzeExpr(*assign->value, true);
    return false;
  }
  if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    if (analyzeExpr(*ifs->cond, true) != Type::Int) error(ifs->cond->loc, "if condition must be int");
    bool thenRet = analyzeStmt(*ifs->thenBranch);
    bool elseRet = ifs->elseBranch ? analyzeStmt(*ifs->elseBranch) : false;
    return thenRet && elseRet;
  }
  if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt)) {
    if (analyzeExpr(*wh->cond, true) != Type::Int) error(wh->cond->loc, "while condition must be int");
    ++loopDepth_;
    analyzeStmt(*wh->body);
    --loopDepth_;
    return false;
  }
  if (dynamic_cast<const BreakStmt *>(&stmt)) {
    if (loopDepth_ == 0) error(stmt.loc, "break outside loop");
    return false;
  }
  if (dynamic_cast<const ContinueStmt *>(&stmt)) {
    if (loopDepth_ == 0) error(stmt.loc, "continue outside loop");
    return false;
  }
  if (auto *ret = dynamic_cast<const ReturnStmt *>(&stmt)) {
    if (currentReturn_ == Type::Void) {
      if (ret->value) error(ret->loc, "void function cannot return a value");
    } else {
      if (!ret->value) error(ret->loc, "int function must return a value");
      analyzeExpr(*ret->value, true);
    }
    return true;
  }
  return false;
}

Type Sema::analyzeExpr(const Expr &expr, bool requireValue) {
  if (dynamic_cast<const NumberExpr *>(&expr)) return Type::Int;
  if (auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    if (!vars_.find(var->name)) error(var->loc, "use of undeclared variable or const '" + var->name + "'");
    return Type::Int;
  }
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr)) {
    Type t = analyzeExpr(*un->operand, true);
    if (t != Type::Int) error(un->loc, "unary operator requires int operand");
    return Type::Int;
  }
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr)) {
    Type lt = analyzeExpr(*bin->lhs, true);
    Type rt = analyzeExpr(*bin->rhs, true);
    if (lt != Type::Int || rt != Type::Int) error(bin->loc, "binary operator requires int operands");
    return Type::Int;
  }
  if (auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    auto it = functions_.find(call->callee);
    if (it == functions_.end()) error(call->loc, "call to undeclared function '" + call->callee + "'");
    if (it->second.paramCount != static_cast<int>(call->args.size())) {
      error(call->loc, "wrong number of arguments in call to '" + call->callee + "'");
    }
    for (const auto &arg : call->args) {
      if (analyzeExpr(*arg, true) != Type::Int) error(arg->loc, "function argument must be int");
    }
    if (requireValue && it->second.returnType == Type::Void) {
      error(call->loc, "void function call cannot be used as a value");
    }
    return it->second.returnType;
  }
  return Type::Int;
}

std::optional<int32_t> Sema::evalConst(const Expr &expr) {
  if (auto *num = dynamic_cast<const NumberExpr *>(&expr)) return num->value;
  if (auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    auto *sym = vars_.find(var->name);
    if (!sym || !sym->isConst || !sym->constValue) return std::nullopt;
    return *sym->constValue;
  }
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr)) {
    auto v = evalConst(*un->operand);
    if (!v) return std::nullopt;
    return applyUnary(un->op, *v);
  }
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr)) {
    auto a = evalConst(*bin->lhs);
    auto b = evalConst(*bin->rhs);
    if (!a || !b) return std::nullopt;
    return applyBinary(bin->op, *a, *b);
  }
  return std::nullopt;
}

int32_t Sema::applyUnary(UnaryOp op, int32_t v) const {
  switch (op) {
  case UnaryOp::Plus: return v;
  case UnaryOp::Minus: return wrap32(-static_cast<int64_t>(v));
  case UnaryOp::Not: return v == 0 ? 1 : 0;
  }
  return v;
}

int32_t Sema::applyBinary(BinaryOp op, int32_t a, int32_t b) const {
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
