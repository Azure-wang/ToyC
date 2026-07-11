#pragma once

#include "ast.h"
#include "sema.h"
#include "symbol_table.h"

#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace toyc {

class RiscV32CodeGen {
public:
  RiscV32CodeGen(const std::unordered_map<std::string, FunctionSig> &functions, bool optimize)
      : functions_(functions), optimize_(optimize) {}
  std::string generate(const Program &program);

private:
  struct Symbol {
    bool isConst = false;
    std::optional<int32_t> constValue;
    bool isGlobal = false;
    std::string label;
    std::string reg;
    int offset = 0;
  };

  struct LoopLabels {
    std::string cont;
    std::string brk;
  };

  [[noreturn]] void error(SourceLoc loc, const std::string &message) const;
  std::string label(const std::string &prefix);
  std::string globalLabel(const std::string &name) const;
  int align16(int n) const;
  int countSlots(const Function &fn) const;
  int countSlotsStmt(const Stmt &stmt) const;
  int countMutableLocals(const Function &fn) const;
  int countMutableLocalsStmt(const Stmt &stmt) const;
  bool functionHasCall(const Function &fn) const;
  bool stmtHasCall(const Stmt &stmt) const;
  bool exprHasCall(const Expr &expr) const;
  std::string allocVarReg();
  bool hasVarRegs() const;
  std::string tempRegName(int depth) const;
  void emit(const std::string &line);
  void emitData(const Program &program);
  void emitFunction(const Function &fn);
  void emitBlock(const BlockStmt &block, bool createScope);
  void emitStmt(const Stmt &stmt);
  void emitDecl(const Decl &decl);
  void emitExpr(const Expr &expr);
  void emitCondition(const Expr &expr, const std::string &trueLabel, const std::string &falseLabel);
  void emitCall(const CallExpr &call);
  Symbol *lookup(const std::string &name);
  void storeTo(const Symbol &sym, SourceLoc loc);
  std::optional<int32_t> evalConst(const Expr &expr);
  int32_t applyUnary(UnaryOp op, int32_t v) const;
  int32_t applyBinary(BinaryOp op, int32_t a, int32_t b) const;
  std::string peepholeOptimize(const std::string &code);

  const std::unordered_map<std::string, FunctionSig> &functions_;
  bool optimize_ = false;
  std::ostringstream data_;
  std::ostringstream text_;
  ScopedTable<Symbol> symbols_;
  std::vector<LoopLabels> loops_;
  int labelId_ = 0;
  int nextOffset_ = -12;
  int frameSize_ = 0;
  int savedSRegs_ = 0;
  int nextVarReg_ = 0;
  int tempDepth_ = 0;
  int spillBase_ = 0;
  int spillDepth_ = 0;
  bool currentLeaf_ = false;
  std::string currentReturnLabel_;
  std::string currentFnName_;
  std::string tailBodyLabel_;
  std::vector<std::string> currentParamNames_;
};

} // namespace toyc
