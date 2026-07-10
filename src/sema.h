#pragma once

#include "ast.h"
#include "symbol_table.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace toyc {

struct FunctionSig {
  Type returnType = Type::Int;
  int paramCount = 0;
};

class Sema {
public:
  void analyze(const Program &program);
  const std::unordered_map<std::string, FunctionSig> &functions() const { return functions_; }

private:
  struct VarInfo {
    bool isConst = false;
    std::optional<int32_t> constValue;
  };

  [[noreturn]] void error(SourceLoc loc, const std::string &message) const;
  void analyzeTopDecl(const Decl &decl);
  void analyzeFunction(const Function &fn);
  void analyzeBlock(const BlockStmt &block, bool createScope);
  bool analyzeStmt(const Stmt &stmt);
  Type analyzeExpr(const Expr &expr, bool requireValue);
  std::optional<int32_t> evalConst(const Expr &expr);
  int32_t applyUnary(UnaryOp op, int32_t v) const;
  int32_t applyBinary(BinaryOp op, int32_t a, int32_t b) const;

  ScopedTable<VarInfo> vars_;
  std::unordered_map<std::string, FunctionSig> functions_;
  std::string currentFunction_;
  Type currentReturn_ = Type::Void;
  int loopDepth_ = 0;
};

} // namespace toyc
