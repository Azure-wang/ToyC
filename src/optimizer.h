#pragma once

#include "ast.h"
#include "symbol_table.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace toyc {

class Optimizer {
public:
  void optimize(Program &program);

private:
  bool foldExpr(std::unique_ptr<Expr> &expr);
  bool foldExpr(std::unique_ptr<Expr> &expr, int depth);
  bool optimizeStmt(std::unique_ptr<Stmt> &stmt);
  bool optimizeStmt(std::unique_ptr<Stmt> &stmt, int depth);
  void optimizeBlock(BlockStmt &block);
  bool isTerminator(const Stmt &stmt) const;
  bool number(const Expr &expr, int32_t &value) const;
  int32_t applyUnary(UnaryOp op, int32_t v) const;
  int32_t applyBinary(BinaryOp op, int32_t a, int32_t b) const;
  std::string exprKey(const Expr &expr) const;
  std::string exprKey(const Expr &expr, int depth) const;
  bool hasCall(const Expr &expr) const;
  void cseBlock(BlockStmt &block);
  void cseStmt(std::unique_ptr<Stmt> &stmt);
  void hoistCommonSubexprs(BlockStmt &block);
  bool hoistInStmt(std::unique_ptr<Stmt> &stmt, int &cseCounter,
      std::vector<std::unique_ptr<Stmt>> &preStmts);
  void countSubexprs(const Expr &expr, std::unordered_map<std::string, int> &counts) const;
  void eliminateDeadStores(BlockStmt &block);
  void hoistLoopInvariants(BlockStmt &block);
  void computeModifiedVars(const Stmt &stmt, std::unordered_set<std::string> &vars) const;
  void collectInnerDecls(const Stmt &stmt, std::unordered_set<std::string> &decls) const;
  bool isInvariant(const Stmt &stmt, const std::unordered_set<std::string> &mustSet) const;
  bool exprRefsModified(const Expr &expr, const std::unordered_set<std::string> &mustSet) const;
  void collectReadVars(const Expr &expr, std::unordered_set<std::string> &vars) const;
  void collectReadVars(const Stmt &stmt, std::unordered_set<std::string> &vars) const;
  bool hasSideEffects(const Expr &expr) const;
  void inlineCalls(Program &program);
  void inlineInBlock(BlockStmt &block,
      const std::unordered_map<std::string, const Function *> &funcMap,
      const std::string &currentFn, int &counter);
  std::unique_ptr<Expr> inlineInExpr(std::unique_ptr<Expr> expr,
      const std::unordered_map<std::string, const Function *> &funcMap,
      const std::string &currentFn,
      std::vector<std::unique_ptr<Stmt>> &preStmts,
      int &counter, SourceLoc loc);
  std::unique_ptr<Stmt> cloneStmt(const Stmt &stmt);
  std::unique_ptr<Stmt> cloneStmt(const Stmt &stmt, int depth);
  std::unique_ptr<BlockStmt> cloneBlock(const BlockStmt &block);
  std::unique_ptr<BlockStmt> cloneBlock(const BlockStmt &block, int depth);
  static bool isInlinable(const Function &fn);
  std::unique_ptr<Expr> replaceSubexprs(std::unique_ptr<Expr> expr,
      const std::unordered_map<std::string, int> &hoistKeys,
      std::unordered_map<std::string, std::string> &tempNames,
      std::vector<std::pair<std::string, std::unique_ptr<Expr>>> &newTemps,
      int &counter, SourceLoc loc);

  ScopedTable<int32_t> constTable_;
  std::unordered_map<std::string, std::string> cseMap_;
};

} // namespace toyc
