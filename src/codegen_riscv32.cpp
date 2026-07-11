#include "codegen_riscv32.h"

#include "utils.h"

#include <algorithm>

namespace toyc {

void RiscV32CodeGen::error(SourceLoc loc, const std::string &message) const {
  throw CompileError(formatLoc(loc) + ": codegen error: " + message);
}

std::string RiscV32CodeGen::label(const std::string &prefix) {
  return ".L_" + prefix + "_" + std::to_string(labelId_++);
}

std::string RiscV32CodeGen::globalLabel(const std::string &name) const {
  return ".G_" + sanitizeLabel(name);
}

int RiscV32CodeGen::align16(int n) const { return (n + 15) & ~15; }

void RiscV32CodeGen::emit(const std::string &line) { text_ << "  " << line << "\n"; }

bool RiscV32CodeGen::hasVarRegs() const { return optimize_; }

std::string RiscV32CodeGen::allocVarReg() {
  if (!hasVarRegs() || nextVarReg_ >= savedSRegs_) return "";
  return "s" + std::to_string(1 + nextVarReg_++);
}

std::string RiscV32CodeGen::tempRegName(int depth) const {
  if (depth < 7) return "t" + std::to_string(depth);
  return "a" + std::to_string(depth - 6);
}

std::string RiscV32CodeGen::generate(const Program &program) {
  symbols_.push();
  emitData(program);
  text_ << ".section .text\n";
  text_ << ".globl main\n";
  text_ << ".weak _start\n";
  text_ << ".globl _start\n";
  text_ << "_start:\n";
  emit("call main");
  emit("li a7, 93");
  emit("ecall");
  for (const auto &item : program.items) {
    if (auto *fn = dynamic_cast<TopFunction *>(item.get())) emitFunction(fn->func);
  }
  symbols_.pop();
  return data_.str() + text_.str();
}

void RiscV32CodeGen::emitData(const Program &program) {
  data_ << ".section .data\n";
  for (const auto &item : program.items) {
    auto *decl = dynamic_cast<TopDecl *>(item.get());
    if (!decl) continue;
    auto value = evalConst(*decl->decl.init);
    if (!value) error(decl->decl.loc, "global initializer is not constant");
    Symbol sym;
    sym.isConst = decl->decl.isConst;
    sym.constValue = decl->decl.isConst ? value : std::optional<int32_t>{};
    sym.isGlobal = !decl->decl.isConst;
    sym.label = globalLabel(decl->decl.name);
    if (!decl->decl.isConst) {
      data_ << ".globl " << sym.label << "\n";
      data_ << sym.label << ":\n";
      data_ << "  .word " << *value << "\n";
    }
    symbols_.declare(decl->decl.name, sym);
  }
}

int RiscV32CodeGen::countSlots(const Function &fn) const {
  int slots = static_cast<int>(fn.params.size());
  slots += countSlotsStmt(*fn.body);
  return slots;
}

int RiscV32CodeGen::countSlotsStmt(const Stmt &stmt) const {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    int n = 0;
    for (const auto &s : block->stmts) n += countSlotsStmt(*s);
    return n;
  }
  if (dynamic_cast<const DeclStmt *>(&stmt)) return 1;
  if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    return countSlotsStmt(*ifs->thenBranch) + (ifs->elseBranch ? countSlotsStmt(*ifs->elseBranch) : 0);
  }
  if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt)) return countSlotsStmt(*wh->body);
  return 0;
}

int RiscV32CodeGen::countMutableLocals(const Function &fn) const {
  return static_cast<int>(fn.params.size()) + countMutableLocalsStmt(*fn.body);
}

int RiscV32CodeGen::countMutableLocalsStmt(const Stmt &stmt) const {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    int n = 0;
    for (const auto &s : block->stmts) n += countMutableLocalsStmt(*s);
    return n;
  }
  if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt)) return decl->decl.isConst ? 0 : 1;
  if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    return countMutableLocalsStmt(*ifs->thenBranch) +
           (ifs->elseBranch ? countMutableLocalsStmt(*ifs->elseBranch) : 0);
  }
  if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt)) return countMutableLocalsStmt(*wh->body);
  return 0;
}

bool RiscV32CodeGen::functionHasCall(const Function &fn) const { return stmtHasCall(*fn.body); }

bool RiscV32CodeGen::stmtHasCall(const Stmt &stmt) const {
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    for (const auto &s : block->stmts)
      if (stmtHasCall(*s)) return true;
  } else if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt)) return exprHasCall(*decl->decl.init);
  else if (auto *expr = dynamic_cast<const ExprStmt *>(&stmt)) return exprHasCall(*expr->expr);
  else if (auto *assign = dynamic_cast<const AssignStmt *>(&stmt)) return exprHasCall(*assign->value);
  else if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    return exprHasCall(*ifs->cond) || stmtHasCall(*ifs->thenBranch) ||
           (ifs->elseBranch && stmtHasCall(*ifs->elseBranch));
  } else if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt)) {
    return exprHasCall(*wh->cond) || stmtHasCall(*wh->body);
  } else if (auto *ret = dynamic_cast<const ReturnStmt *>(&stmt)) {
    return ret->value && exprHasCall(*ret->value);
  }
  return false;
}

bool RiscV32CodeGen::exprHasCall(const Expr &expr) const {
  if (dynamic_cast<const CallExpr *>(&expr)) return true;
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr)) return exprHasCall(*un->operand);
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr)) return exprHasCall(*bin->lhs) || exprHasCall(*bin->rhs);
  return false;
}

void RiscV32CodeGen::emitFunction(const Function &fn) {
  // ── Stack frame layout (grows downward) ──
  // sp+frameSize              ← s0 (frame pointer)
  // sp+frameSize-4            ra  (if non-leaf)
  // sp+frameSize-4/8          saved s0
  // sp+frameSize-8/12         saved s1 (if optimize_)
  // sp+frameSize-12/16        saved s2 ...
  // ...
  // sp+frameSize-saveArea     first local / spill slot (nextOffset_)
  // sp+16                     ABI reserved (16 bytes)
  // sp                        stack pointer
  //
  // fixedSaves = 1 (s0) + (leaf ? 0 : 1) (ra) + savedSRegs_
  // saveArea   = 4 * (fixedSaves + 1)  (saved regs + one guard word)
  // frameSize  = align16(saveArea + 4*slots + 16)

  currentLeaf_ = !functionHasCall(fn);
  savedSRegs_ = optimize_ ? std::min(11, countMutableLocals(fn)) : 0;
  int slots = optimize_ ? std::max(0, countSlots(fn) - savedSRegs_) : countSlots(fn);
  int fixedSaves = 1 + (currentLeaf_ ? 0 : 1) + savedSRegs_;
  frameSize_ = align16(4 * (slots + fixedSaves) + 16);
  nextOffset_ = -4 * (fixedSaves + 1);
  nextVarReg_ = 0;
  tempDepth_ = 0;
  currentReturnLabel_ = label("ret_" + sanitizeLabel(fn.name));

  text_ << "\n.globl " << fn.name << "\n";
  text_ << fn.name << ":\n";
  emit("addi sp, sp, -" + std::to_string(frameSize_));
  if (!currentLeaf_) emit("sw ra, " + std::to_string(frameSize_ - 4) + "(sp)");
  emit("sw s0, " + std::to_string(frameSize_ - (currentLeaf_ ? 4 : 8)) + "(sp)");
  for (int i = 0; i < savedSRegs_; ++i) {
    int slot = currentLeaf_ ? (8 + i * 4) : (12 + i * 4);
    emit("sw s" + std::to_string(i + 1) + ", " + std::to_string(frameSize_ - slot) + "(sp)");
  }
  emit("addi s0, sp, " + std::to_string(frameSize_));

  symbols_.push();
  for (size_t i = 0; i < fn.params.size(); ++i) {
    Symbol sym;
    sym.reg = allocVarReg();
    if (sym.reg.empty()) {
      sym.offset = nextOffset_;
      nextOffset_ -= 4;
    }
    symbols_.declare(fn.params[i].name, sym);
    if (i < 8) {
      if (!sym.reg.empty()) emit("mv " + sym.reg + ", a" + std::to_string(i));
      else emit("sw a" + std::to_string(i) + ", " + std::to_string(sym.offset) + "(s0)");
    } else {
      int callerOff = static_cast<int>(i - 8) * 4;
      emit("lw t0, " + std::to_string(callerOff) + "(s0)");
      if (!sym.reg.empty()) emit("mv " + sym.reg + ", t0");
      else emit("sw t0, " + std::to_string(sym.offset) + "(s0)");
    }
  }
  emitBlock(*fn.body, true);
  if (fn.returnType == Type::Void) emit("li a0, 0");
  text_ << currentReturnLabel_ << ":\n";
  for (int i = savedSRegs_ - 1; i >= 0; --i) {
    int slot = currentLeaf_ ? (8 + i * 4) : (12 + i * 4);
    emit("lw s" + std::to_string(i + 1) + ", -" + std::to_string(slot) + "(s0)");
  }
  if (!currentLeaf_) emit("lw ra, -4(s0)");
  emit("lw s0, -" + std::to_string(currentLeaf_ ? 4 : 8) + "(s0)");
  emit("addi sp, sp, " + std::to_string(frameSize_));
  emit("ret");
  symbols_.pop();
}

void RiscV32CodeGen::emitBlock(const BlockStmt &block, bool createScope) {
  if (createScope) symbols_.push();
  for (const auto &stmt : block.stmts) emitStmt(*stmt);
  if (createScope) symbols_.pop();
}

void RiscV32CodeGen::emitStmt(const Stmt &stmt) {
  loadedAtDepth_.clear();
  if (auto *block = dynamic_cast<const BlockStmt *>(&stmt)) emitBlock(*block, true);
  else if (auto *decl = dynamic_cast<const DeclStmt *>(&stmt)) emitDecl(decl->decl);
  else if (auto *expr = dynamic_cast<const ExprStmt *>(&stmt)) emitExpr(*expr->expr);
  else if (auto *assign = dynamic_cast<const AssignStmt *>(&stmt)) {
    Symbol *sym = lookup(assign->name);
    if (!sym) error(assign->loc, "unknown variable '" + assign->name + "'");
    if (sym->isConst) error(assign->loc, "cannot assign const");
    if (optimize_ && !sym->reg.empty()) {
      if (auto *var = dynamic_cast<const VarExpr *>(assign->value.get())) {
        Symbol *rhs = lookup(var->name);
        if (rhs) {
          if (rhs->constValue) emit("li " + sym->reg + ", " + std::to_string(*rhs->constValue));
          else if (!rhs->reg.empty()) emit("mv " + sym->reg + ", " + rhs->reg);
          else if (rhs->isGlobal) { emit("la t0, " + rhs->label); emit("lw " + sym->reg + ", 0(t0)"); }
          else emit("lw " + sym->reg + ", " + std::to_string(rhs->offset) + "(s0)");
          return;
        }
      }
      if (auto c = evalConst(*assign->value)) {
        emit("li " + sym->reg + ", " + std::to_string(*c));
        return;
      }
      if (auto *un = dynamic_cast<UnaryExpr *>(assign->value.get())) {
        if (auto *uv = dynamic_cast<VarExpr *>(un->operand.get())) {
          if (uv->name == assign->name) {
            if (un->op == UnaryOp::Minus) { emit("neg " + sym->reg + ", " + sym->reg); return; }
            if (un->op == UnaryOp::Not) { emit("seqz " + sym->reg + ", " + sym->reg); return; }
          }
        }
      }
      if (auto *bin = dynamic_cast<BinaryExpr *>(assign->value.get())) {
        if (auto *lv = dynamic_cast<VarExpr *>(bin->lhs.get())) {
          if (lv->name == assign->name) {
            if (auto c = evalConst(*bin->rhs)) {
              if (bin->op == BinaryOp::Add && *c >= -2048 && *c <= 2047) {
                if (*c != 0) emit("addi " + sym->reg + ", " + sym->reg + ", " + std::to_string(*c));
                return;
              }
              if (bin->op == BinaryOp::Sub && *c >= -2047 && *c <= 2048) {
                if (*c != 0) emit("addi " + sym->reg + ", " + sym->reg + ", " + std::to_string(-*c));
                return;
              }
              if (bin->op == BinaryOp::Mul) {
                int32_t cv = *c;
                if (cv == 0) { emit("li " + sym->reg + ", 0"); return; }
                if (cv == 1) return;
                if (cv == -1) { emit("neg " + sym->reg + ", " + sym->reg); return; }
                if ((cv & (cv - 1)) == 0) {
                  int k = 0; uint32_t u = static_cast<uint32_t>(cv);
                  while (u > 1) { u >>= 1; ++k; }
                  emit("slli " + sym->reg + ", " + sym->reg + ", " + std::to_string(k));
                  return;
                }
                if (cv == 3) {
                  emit("slli t0, " + sym->reg + ", 1");
                  emit("add " + sym->reg + ", " + sym->reg + ", t0");
                  return;
                }
                if (cv == 5) {
                  emit("slli t0, " + sym->reg + ", 2");
                  emit("add " + sym->reg + ", " + sym->reg + ", t0");
                  return;
                }
                if (cv == 7) {
                  emit("slli t0, " + sym->reg + ", 3");
                  emit("sub " + sym->reg + ", t0, " + sym->reg);
                  return;
                }
                if (cv == 9) {
                  emit("slli t0, " + sym->reg + ", 3");
                  emit("add " + sym->reg + ", " + sym->reg + ", t0");
                  return;
                }
              }
            }
            if (!exprHasCall(*bin->rhs) && tempDepth_ < 13) {
              std::string tReg = tempRegName(tempDepth_++);
              if (auto *rv = dynamic_cast<VarExpr *>(bin->rhs.get())) {
                Symbol *rs = lookup(rv->name);
                if (rs) {
                  if (rs->constValue) emit("li " + tReg + ", " + std::to_string(*rs->constValue));
                  else if (!rs->reg.empty()) emit("mv " + tReg + ", " + rs->reg);
                  else emit("lw " + tReg + ", " + std::to_string(rs->offset) + "(s0)");
                }
              } else {
                emitExpr(*bin->rhs);
                emit("mv " + tReg + ", a0");
              }
              tempDepth_--;
              switch (bin->op) {
              case BinaryOp::Add: emit("add " + sym->reg + ", " + sym->reg + ", " + tReg); break;
              case BinaryOp::Sub: emit("sub " + sym->reg + ", " + sym->reg + ", " + tReg); break;
              case BinaryOp::Mul: emit("mul " + sym->reg + ", " + sym->reg + ", " + tReg); break;
              case BinaryOp::Div: emit("div " + sym->reg + ", " + sym->reg + ", " + tReg); break;
              case BinaryOp::Mod: emit("rem " + sym->reg + ", " + sym->reg + ", " + tReg); break;
              default: emitExpr(*assign->value); storeTo(*sym, assign->loc); break;
              }
              return;
            }
          }
        }
      }
    }
    if (optimize_ && !sym->reg.empty()) {
      if (auto *bin = dynamic_cast<BinaryExpr *>(assign->value.get())) {
        if (auto *lv = dynamic_cast<const VarExpr *>(bin->lhs.get())) {
          Symbol *ls = lookup(lv->name);
          if (ls && !ls->reg.empty() && !exprHasCall(*assign->value)) {
            if (auto *rv = dynamic_cast<const VarExpr *>(bin->rhs.get())) {
              Symbol *rs = lookup(rv->name);
              if (rs && !rs->reg.empty()) {
                switch (bin->op) {
                case BinaryOp::Add: emit("add " + sym->reg + ", " + ls->reg + ", " + rs->reg); return;
                case BinaryOp::Sub: emit("sub " + sym->reg + ", " + ls->reg + ", " + rs->reg); return;
                case BinaryOp::Mul: emit("mul " + sym->reg + ", " + ls->reg + ", " + rs->reg); return;
                case BinaryOp::Div: emit("div " + sym->reg + ", " + ls->reg + ", " + rs->reg); return;
                case BinaryOp::Mod: emit("rem " + sym->reg + ", " + ls->reg + ", " + rs->reg); return;
                case BinaryOp::Lt: emit("slt " + sym->reg + ", " + ls->reg + ", " + rs->reg); return;
                case BinaryOp::Gt: emit("slt " + sym->reg + ", " + rs->reg + ", " + ls->reg); return;
                case BinaryOp::Le:
                  emit("slt " + sym->reg + ", " + rs->reg + ", " + ls->reg);
                  emit("xori " + sym->reg + ", " + sym->reg + ", 1");
                  return;
                case BinaryOp::Ge:
                  emit("slt " + sym->reg + ", " + ls->reg + ", " + rs->reg);
                  emit("xori " + sym->reg + ", " + sym->reg + ", 1");
                  return;
                case BinaryOp::Eq:
                  emit("sub " + sym->reg + ", " + ls->reg + ", " + rs->reg);
                  emit("seqz " + sym->reg + ", " + sym->reg);
                  return;
                case BinaryOp::Ne:
                  emit("sub " + sym->reg + ", " + ls->reg + ", " + rs->reg);
                  emit("snez " + sym->reg + ", " + sym->reg);
                  return;
                default: break;
                }
              }
            }
            if (auto c = evalConst(*bin->rhs)) {
              if (bin->op == BinaryOp::Add && *c >= -2048 && *c <= 2047) {
                emit("addi " + sym->reg + ", " + ls->reg + ", " + std::to_string(*c));
                return;
              }
              if (bin->op == BinaryOp::Sub && *c >= -2047 && *c <= 2048) {
                emit("addi " + sym->reg + ", " + ls->reg + ", " + std::to_string(-*c));
                return;
              }
              if (bin->op == BinaryOp::Lt && *c >= -2048 && *c <= 2047) {
                emit("slti " + sym->reg + ", " + ls->reg + ", " + std::to_string(*c));
                return;
              }
            }
          }
        }
      }
    }
    emitExpr(*assign->value);
    storeTo(*sym, assign->loc);
  } else if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    std::string thenL = label("if_then");
    std::string elseL = label("if_else");
    std::string endL = label("if_end");
    emitCondition(*ifs->cond, thenL, ifs->elseBranch ? elseL : endL);
    text_ << thenL << ":\n";
    emitStmt(*ifs->thenBranch);
    if (ifs->elseBranch) {
      emit("j " + endL);
      text_ << elseL << ":\n";
      emitStmt(*ifs->elseBranch);
    }
    text_ << endL << ":\n";
  } else if (auto *wh = dynamic_cast<const WhileStmt *>(&stmt)) {
    std::string condL = label("while_cond");
    std::string bodyL = label("while_body");
    std::string endL = label("while_end");
    loops_.push_back(LoopLabels{condL, endL});
    text_ << condL << ":\n";
    emitCondition(*wh->cond, bodyL, endL);
    text_ << bodyL << ":\n";
    emitStmt(*wh->body);
    emit("j " + condL);
    text_ << endL << ":\n";
    loops_.pop_back();
  } else if (dynamic_cast<const BreakStmt *>(&stmt)) {
    emit("j " + loops_.back().brk);
  } else if (dynamic_cast<const ContinueStmt *>(&stmt)) {
    emit("j " + loops_.back().cont);
  } else if (auto *ret = dynamic_cast<const ReturnStmt *>(&stmt)) {
    if (ret->value) {
      if (optimize_ && dynamic_cast<const CallExpr *>(ret->value.get())) {
        const auto &call = dynamic_cast<const CallExpr &>(*ret->value);
        for (int i = static_cast<int>(call.args.size()) - 1; i >= 0; --i) {
          emitExpr(*call.args[static_cast<size_t>(i)]);
          emit("addi sp, sp, -4");
          emit("sw a0, 0(sp)");
        }
        int n = static_cast<int>(call.args.size());
        int regArgs = std::min(n, 8);
        for (int i = 0; i < regArgs; ++i)
          emit("lw a" + std::to_string(i) + ", " + std::to_string(i * 4) + "(sp)");
        if (regArgs > 0) emit("addi sp, sp, " + std::to_string(regArgs * 4));
        int extra = n - regArgs;
        if (extra > 0) emit("addi sp, sp, " + std::to_string(extra * 4));
        for (int i = savedSRegs_ - 1; i >= 0; --i) {
          int slot = currentLeaf_ ? (8 + i * 4) : (12 + i * 4);
          emit("lw s" + std::to_string(i + 1) + ", -" + std::to_string(slot) + "(s0)");
        }
        if (!currentLeaf_) emit("lw ra, -4(s0)");
        emit("lw s0, -" + std::to_string(currentLeaf_ ? 4 : 8) + "(s0)");
        emit("addi sp, sp, " + std::to_string(frameSize_));
        emit("j " + call.callee);
        return;
      }
      emitExpr(*ret->value);
    } else {
      emit("li a0, 0");
    }
    emit("j " + currentReturnLabel_);
  }
}

void RiscV32CodeGen::emitDecl(const Decl &decl) {
  Symbol sym;
  sym.isConst = decl.isConst;
  if (decl.isConst) {
    auto value = evalConst(*decl.init);
    if (!value) error(decl.loc, "const initializer is not constant");
    sym.constValue = value;
    symbols_.declare(decl.name, sym);
    return;
  }
  sym.reg = allocVarReg();
  if (sym.reg.empty()) {
    sym.offset = nextOffset_;
    nextOffset_ -= 4;
  }
  if (!symbols_.declare(decl.name, sym)) error(decl.loc, "duplicate declaration");
  if (optimize_ && dynamic_cast<NumberExpr *>(decl.init.get())) {
    auto val = static_cast<NumberExpr *>(decl.init.get())->value;
    if (!sym.reg.empty())
      emit("li " + sym.reg + ", " + std::to_string(val));
    else if (sym.isGlobal) {
      emit("la t0, " + sym.label);
      emit("li t1, " + std::to_string(val));
      emit("sw t1, 0(t0)");
    } else {
      emit("li t0, " + std::to_string(val));
      emit("sw t0, " + std::to_string(sym.offset) + "(s0)");
    }
  } else {
    emitExpr(*decl.init);
    storeTo(sym, decl.loc);
  }
}

void RiscV32CodeGen::emitExpr(const Expr &expr) {
  if (auto *num = dynamic_cast<const NumberExpr *>(&expr)) {
    emit("li a0, " + std::to_string(num->value));
    return;
  }
  if (auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    Symbol *sym = lookup(var->name);
    if (!sym) error(var->loc, "unknown symbol '" + var->name + "'");
    if (sym->constValue) emit("li a0, " + std::to_string(*sym->constValue));
    else if (!sym->reg.empty()) emit("mv a0, " + sym->reg);
    else if (sym->isGlobal) {
      emit("la t0, " + sym->label);
      emit("lw a0, 0(t0)");
    } else {
      emit("lw a0, " + std::to_string(sym->offset) + "(s0)");
    }
    return;
  }
  if (auto *un = dynamic_cast<const UnaryExpr *>(&expr)) {
    if (optimize_ && (un->op == UnaryOp::Minus || un->op == UnaryOp::Not)) {
      if (auto *var = dynamic_cast<const VarExpr *>(un->operand.get())) {
        if (Symbol *sym = lookup(var->name)) {
          if (!sym->reg.empty()) {
            if (un->op == UnaryOp::Minus) emit("neg a0, " + sym->reg);
            else emit("seqz a0, " + sym->reg);
            return;
          }
          if (sym->constValue) {
            int32_t v = un->op == UnaryOp::Minus ? -(*sym->constValue) : (*sym->constValue == 0 ? 1 : 0);
            emit("li a0, " + std::to_string(v));
            return;
          }
        }
      }
    }
    emitExpr(*un->operand);
    if (un->op == UnaryOp::Minus) emit("neg a0, a0");
    else if (un->op == UnaryOp::Not) emit("seqz a0, a0");
    return;
  }
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr)) {
    if (bin->op == BinaryOp::And || bin->op == BinaryOp::Or) {
      std::string t = label("logic_true");
      std::string f = label("logic_false");
      std::string e = label("logic_end");
      emitCondition(expr, t, f);
      text_ << t << ":\n";
      emit("li a0, 1");
      emit("j " + e);
      text_ << f << ":\n";
      emit("li a0, 0");
      text_ << e << ":\n";
      return;
    }
    if (optimize_) {
      if (auto lhsConst = evalConst(*bin->lhs)) {
        if (bin->op == BinaryOp::Sub) {
          emitExpr(*bin->rhs);
          emit("neg a0, a0");
          if (*lhsConst != 0) emit("addi a0, a0, " + std::to_string(*lhsConst));
          return;
        }
        if (bin->op == BinaryOp::Mul) {
          int32_t c = *lhsConst;
          if (c == 0) { emit("li a0, 0"); return; }
          if (c == 1) { emitExpr(*bin->rhs); return; }
          if (c == -1) { emitExpr(*bin->rhs); emit("neg a0, a0"); return; }
          if ((c & (c - 1)) == 0) {
            int k = 0;
            uint32_t u = static_cast<uint32_t>(c);
            while (u > 1) { u >>= 1; ++k; }
            emitExpr(*bin->rhs);
            emit("slli a0, a0, " + std::to_string(k));
            return;
          }
          if (c == 3) {
            emitExpr(*bin->rhs);
            emit("slli t0, a0, 1");
            emit("add a0, a0, t0");
            return;
          }
          if (c == 5) {
            emitExpr(*bin->rhs);
            emit("slli t0, a0, 2");
            emit("add a0, a0, t0");
            return;
          }
          if (c == 7) {
            emitExpr(*bin->rhs);
            emit("slli t0, a0, 3");
            emit("sub a0, t0, a0");
            return;
          }
          if (c == 9) {
            emitExpr(*bin->rhs);
            emit("slli t0, a0, 3");
            emit("add a0, a0, t0");
            return;
          }
        }
      }
      if (auto rhsConst = evalConst(*bin->rhs)) {
        if (bin->op == BinaryOp::Add && *rhsConst >= -2048 && *rhsConst <= 2047) {
          emitExpr(*bin->lhs);
          if (*rhsConst != 0) emit("addi a0, a0, " + std::to_string(*rhsConst));
          return;
        }
        if (bin->op == BinaryOp::Sub && *rhsConst >= -2047 && *rhsConst <= 2048) {
          emitExpr(*bin->lhs);
          if (*rhsConst != 0) emit("addi a0, a0, " + std::to_string(-*rhsConst));
          return;
        }
        if (bin->op == BinaryOp::Mul) {
          int32_t c = *rhsConst;
          if (c == 0) { emit("li a0, 0"); return; }
          if (c == 1) { emitExpr(*bin->lhs); return; }
          if (c == -1) { emitExpr(*bin->lhs); emit("neg a0, a0"); return; }
          if ((c & (c - 1)) == 0) {
            int k = 0;
            uint32_t u = static_cast<uint32_t>(c);
            while (u > 1) { u >>= 1; ++k; }
            emitExpr(*bin->lhs);
            emit("slli a0, a0, " + std::to_string(k));
            return;
          }
          if (c == 3) {
            emitExpr(*bin->lhs);
            emit("slli t0, a0, 1");
            emit("add a0, a0, t0");
            return;
          }
          if (c == 5) {
            emitExpr(*bin->lhs);
            emit("slli t0, a0, 2");
            emit("add a0, a0, t0");
            return;
          }
          if (c == 7) {
            emitExpr(*bin->lhs);
            emit("slli t0, a0, 3");
            emit("sub a0, t0, a0");
            return;
          }
          if (c == 9) {
            emitExpr(*bin->lhs);
            emit("slli t0, a0, 3");
            emit("add a0, a0, t0");
            return;
          }
        }
        if (bin->op == BinaryOp::Div || bin->op == BinaryOp::Mod) {
          int32_t c = *rhsConst;
          if (c > 0 && (c & (c - 1)) == 0) {
            int k = 0;
            uint32_t u = static_cast<uint32_t>(c);
            while (u > 1) { u >>= 1; ++k; }
            emitExpr(*bin->lhs);
            if (bin->op == BinaryOp::Mod) emit("mv t1, a0");
            emit("srai t0, a0, 31");
            emit("srli t0, t0, " + std::to_string(32 - k));
            emit("add a0, a0, t0");
            emit("srai a0, a0, " + std::to_string(k));
            if (bin->op == BinaryOp::Mod) {
              emit("slli a0, a0, " + std::to_string(k));
              emit("sub a0, t1, a0");
            }
            return;
          }
        }
      }
      if (auto *var = dynamic_cast<const VarExpr *>(bin->lhs.get())) {
        if (Symbol *sym = lookup(var->name)) {
          if (auto *rv = dynamic_cast<const VarExpr *>(bin->rhs.get())) {
            if (rv->name == var->name && bin->op == BinaryOp::Mul) {
              if (sym->constValue)
                emit("li a0, " + std::to_string(*sym->constValue));
              else if (!sym->reg.empty())
                emit("mv a0, " + sym->reg);
              else if (sym->isGlobal) {
                emit("la a0, " + sym->label);
                emit("lw a0, 0(a0)");
              } else
                emit("lw a0, " + std::to_string(sym->offset) + "(s0)");
              emit("mul a0, a0, a0");
              return;
            }
          }
          if (!exprHasCall(*bin->rhs) && tempDepth_ < 13) {
            std::string lhsReg = tempRegName(tempDepth_++);
            int d = tempDepth_ - 1;
            auto it = loadedAtDepth_.find(d);
            if (it == loadedAtDepth_.end() || it->second != var->name) {
              if (sym->constValue)
                emit("li " + lhsReg + ", " + std::to_string(*sym->constValue));
              else if (!sym->reg.empty())
                emit("mv " + lhsReg + ", " + sym->reg);
              else if (sym->isGlobal) {
                emit("la " + lhsReg + ", " + sym->label);
                emit("lw " + lhsReg + ", 0(" + lhsReg + ")");
              } else
                emit("lw " + lhsReg + ", " + std::to_string(sym->offset) + "(s0)");
              loadedAtDepth_[d] = var->name;
            }
            emitExpr(*bin->rhs);
            --tempDepth_;
            switch (bin->op) {
            case BinaryOp::Add: emit("add a0, " + lhsReg + ", a0"); break;
            case BinaryOp::Sub: emit("sub a0, " + lhsReg + ", a0"); break;
            case BinaryOp::Mul: emit("mul a0, " + lhsReg + ", a0"); break;
            case BinaryOp::Div: emit("div a0, " + lhsReg + ", a0"); break;
            case BinaryOp::Mod: emit("rem a0, " + lhsReg + ", a0"); break;
            case BinaryOp::Lt: emit("slt a0, " + lhsReg + ", a0"); break;
            case BinaryOp::Gt: emit("slt a0, a0, " + lhsReg); break;
            case BinaryOp::Le: emit("slt a0, a0, " + lhsReg); emit("xori a0, a0, 1"); break;
            case BinaryOp::Ge: emit("slt a0, " + lhsReg + ", a0"); emit("xori a0, a0, 1"); break;
            case BinaryOp::Eq: emit("sub a0, " + lhsReg + ", a0"); emit("seqz a0, a0"); break;
            case BinaryOp::Ne: emit("sub a0, " + lhsReg + ", a0"); emit("snez a0, a0"); break;
            default: break;
            }
            return;
          }
        }
      }
      if (auto *inner = dynamic_cast<const BinaryExpr *>(bin->lhs.get())) {
        if (auto rhsConst = evalConst(*inner->rhs)) {
          if (inner->op == BinaryOp::Add && *rhsConst >= -2048 && *rhsConst <= 2047) {
            if (auto *iv = dynamic_cast<const VarExpr *>(inner->lhs.get())) {
              if (Symbol *is = lookup(iv->name)) {
                if (!exprHasCall(*bin->rhs) && tempDepth_ < 13) {
                  std::string lhsReg = tempRegName(tempDepth_++);
                  if (is->constValue)
                    emit("li " + lhsReg + ", " + std::to_string(*is->constValue + *rhsConst));
                  else if (!is->reg.empty())
                    emit("addi " + lhsReg + ", " + is->reg + ", " + std::to_string(*rhsConst));
                  else if (is->isGlobal) {
                    emit("la " + lhsReg + ", " + is->label);
                    emit("lw " + lhsReg + ", 0(" + lhsReg + ")");
                    if (*rhsConst != 0) emit("addi " + lhsReg + ", " + lhsReg + ", " + std::to_string(*rhsConst));
                  } else {
                    emit("lw " + lhsReg + ", " + std::to_string(is->offset) + "(s0)");
                    if (*rhsConst != 0) emit("addi " + lhsReg + ", " + lhsReg + ", " + std::to_string(*rhsConst));
                  }
                  emitExpr(*bin->rhs);
                  --tempDepth_;
                  switch (bin->op) {
                  case BinaryOp::Add: emit("add a0, " + lhsReg + ", a0"); break;
                  case BinaryOp::Sub: emit("sub a0, " + lhsReg + ", a0"); break;
                  case BinaryOp::Mul: emit("mul a0, " + lhsReg + ", a0"); break;
                  case BinaryOp::Div: emit("div a0, " + lhsReg + ", a0"); break;
                  case BinaryOp::Mod: emit("rem a0, " + lhsReg + ", a0"); break;
                  case BinaryOp::Lt: emit("slt a0, " + lhsReg + ", a0"); break;
                  case BinaryOp::Gt: emit("slt a0, a0, " + lhsReg); break;
                  case BinaryOp::Le: emit("slt a0, a0, " + lhsReg); emit("xori a0, a0, 1"); break;
                  case BinaryOp::Ge: emit("slt a0, " + lhsReg + ", a0"); emit("xori a0, a0, 1"); break;
                  case BinaryOp::Eq: emit("sub a0, " + lhsReg + ", a0"); emit("seqz a0, a0"); break;
                  case BinaryOp::Ne: emit("sub a0, " + lhsReg + ", a0"); emit("snez a0, a0"); break;
                  default: break;
                  }
                  return;
                }
              }
            }
          }
        }
      }
    }
    std::string lhsReg = "t0";
    bool useTempReg = optimize_ && !exprHasCall(*bin->rhs) && tempDepth_ < 13;
    if (useTempReg) {
      lhsReg = tempRegName(tempDepth_++);
      emitExpr(*bin->lhs);
      emit("mv " + lhsReg + ", a0");
      emitExpr(*bin->rhs);
      --tempDepth_;
    } else {
      emitExpr(*bin->lhs);
      emit("addi sp, sp, -4");
      emit("sw a0, 0(sp)");
      emitExpr(*bin->rhs);
      emit("lw t0, 0(sp)");
      emit("addi sp, sp, 4");
    }
    switch (bin->op) {
    case BinaryOp::Add: emit("add a0, " + lhsReg + ", a0"); break;
    case BinaryOp::Sub: emit("sub a0, " + lhsReg + ", a0"); break;
    case BinaryOp::Mul: emit("mul a0, " + lhsReg + ", a0"); break;
    case BinaryOp::Div: emit("div a0, " + lhsReg + ", a0"); break;
    case BinaryOp::Mod: emit("rem a0, " + lhsReg + ", a0"); break;
    case BinaryOp::Lt: emit("slt a0, " + lhsReg + ", a0"); break;
    case BinaryOp::Gt: emit("slt a0, a0, " + lhsReg); break;
    case BinaryOp::Le: emit("slt a0, a0, " + lhsReg); emit("xori a0, a0, 1"); break;
    case BinaryOp::Ge: emit("slt a0, " + lhsReg + ", a0"); emit("xori a0, a0, 1"); break;
    case BinaryOp::Eq: emit("sub a0, " + lhsReg + ", a0"); emit("seqz a0, a0"); break;
    case BinaryOp::Ne: emit("sub a0, " + lhsReg + ", a0"); emit("snez a0, a0"); break;
    case BinaryOp::And:
    case BinaryOp::Or: break;
    }
    return;
  }
  if (auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    emitCall(*call);
    return;
  }
}

void RiscV32CodeGen::emitCondition(const Expr &expr, const std::string &trueLabel,
                                   const std::string &falseLabel) {
  if (auto value = evalConst(expr)) {
    emit("j " + (*value != 0 ? trueLabel : falseLabel));
    return;
  }
  if (auto *bin = dynamic_cast<const BinaryExpr *>(&expr)) {
    if (bin->op == BinaryOp::And) {
      std::string mid = label("and_rhs");
      emitCondition(*bin->lhs, mid, falseLabel);
      text_ << mid << ":\n";
      emitCondition(*bin->rhs, trueLabel, falseLabel);
      return;
    }
    if (bin->op == BinaryOp::Or) {
      std::string mid = label("or_rhs");
      emitCondition(*bin->lhs, trueLabel, mid);
      text_ << mid << ":\n";
      emitCondition(*bin->rhs, trueLabel, falseLabel);
      return;
    }
    if (bin->op == BinaryOp::Lt || bin->op == BinaryOp::Gt || bin->op == BinaryOp::Le ||
        bin->op == BinaryOp::Ge || bin->op == BinaryOp::Eq || bin->op == BinaryOp::Ne) {
      if (optimize_) {
        if (auto *var = dynamic_cast<const VarExpr *>(bin->lhs.get())) {
          if (Symbol *sym = lookup(var->name)) {
            if (!exprHasCall(*bin->rhs) && tempDepth_ < 13) {
              std::string lhsReg = tempRegName(tempDepth_++);
              int d = tempDepth_ - 1;
              auto it = loadedAtDepth_.find(d);
              if (it == loadedAtDepth_.end() || it->second != var->name) {
                if (sym->constValue)
                  emit("li " + lhsReg + ", " + std::to_string(*sym->constValue));
                else if (!sym->reg.empty())
                  emit("mv " + lhsReg + ", " + sym->reg);
                else if (sym->isGlobal) {
                  emit("la " + lhsReg + ", " + sym->label);
                  emit("lw " + lhsReg + ", 0(" + lhsReg + ")");
                } else
                  emit("lw " + lhsReg + ", " + std::to_string(sym->offset) + "(s0)");
                loadedAtDepth_[d] = var->name;
              }
              emitExpr(*bin->rhs);
              --tempDepth_;
              switch (bin->op) {
              case BinaryOp::Lt: emit("blt " + lhsReg + ", a0, " + trueLabel); break;
              case BinaryOp::Gt: emit("blt a0, " + lhsReg + ", " + trueLabel); break;
              case BinaryOp::Le: emit("bge a0, " + lhsReg + ", " + trueLabel); break;
              case BinaryOp::Ge: emit("bge " + lhsReg + ", a0, " + trueLabel); break;
              case BinaryOp::Eq: emit("beq " + lhsReg + ", a0, " + trueLabel); break;
              case BinaryOp::Ne: emit("bne " + lhsReg + ", a0, " + trueLabel); break;
              default: break;
              }
              emit("j " + falseLabel);
              return;
            }
          }
        }
      }
      std::string lhsReg = "t0";
      bool useTempReg = optimize_ && !exprHasCall(*bin->rhs) && tempDepth_ < 13;
      if (useTempReg) {
        lhsReg = tempRegName(tempDepth_++);
        emitExpr(*bin->lhs);
        emit("mv " + lhsReg + ", a0");
        emitExpr(*bin->rhs);
        --tempDepth_;
      } else {
        emitExpr(*bin->lhs);
        emit("addi sp, sp, -4");
        emit("sw a0, 0(sp)");
        emitExpr(*bin->rhs);
        emit("lw t0, 0(sp)");
        emit("addi sp, sp, 4");
      }
      switch (bin->op) {
      case BinaryOp::Lt: emit("blt " + lhsReg + ", a0, " + trueLabel); break;
      case BinaryOp::Gt: emit("blt a0, " + lhsReg + ", " + trueLabel); break;
      case BinaryOp::Le: emit("bge a0, " + lhsReg + ", " + trueLabel); break;
      case BinaryOp::Ge: emit("bge " + lhsReg + ", a0, " + trueLabel); break;
      case BinaryOp::Eq: emit("beq " + lhsReg + ", a0, " + trueLabel); break;
      case BinaryOp::Ne: emit("bne " + lhsReg + ", a0, " + trueLabel); break;
      default: break;
      }
      emit("j " + falseLabel);
      return;
    }
  }
  emitExpr(expr);
  emit("bnez a0, " + trueLabel);
  emit("j " + falseLabel);
}

void RiscV32CodeGen::emitCall(const CallExpr &call) {
  int n = static_cast<int>(call.args.size());
  if (optimize_ && n == 1 && !exprHasCall(*call.args[0])) {
    emitExpr(*call.args[0]);
    emit("call " + call.callee);
    loadedAtDepth_.clear();
    return;
  }
  for (int i = n - 1; i >= 0; --i) {
    emitExpr(*call.args[static_cast<size_t>(i)]);
    emit("addi sp, sp, -4");
    emit("sw a0, 0(sp)");
  }
  int regArgs = std::min(n, 8);
  for (int i = 0; i < regArgs; ++i) {
    emit("lw a" + std::to_string(i) + ", " + std::to_string(i * 4) + "(sp)");
  }
  if (regArgs > 0) emit("addi sp, sp, " + std::to_string(regArgs * 4));
  emit("call " + call.callee);
  loadedAtDepth_.clear();
  int extra = n - regArgs;
  if (extra > 0) emit("addi sp, sp, " + std::to_string(extra * 4));
}

RiscV32CodeGen::Symbol *RiscV32CodeGen::lookup(const std::string &name) {
  return symbols_.find(name);
}

void RiscV32CodeGen::storeTo(const Symbol &sym, SourceLoc loc) {
  if (sym.isConst) error(loc, "cannot store to const");
  if (!sym.reg.empty()) {
    emit("mv " + sym.reg + ", a0");
  } else if (sym.isGlobal) {
    emit("la t0, " + sym.label);
    emit("sw a0, 0(t0)");
  } else {
    emit("sw a0, " + std::to_string(sym.offset) + "(s0)");
  }
}

std::optional<int32_t> RiscV32CodeGen::evalConst(const Expr &expr) {
  if (auto *num = dynamic_cast<const NumberExpr *>(&expr)) return num->value;
  if (auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    Symbol *sym = lookup(var->name);
    if (sym && sym->constValue) return *sym->constValue;
    return std::nullopt;
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

int32_t RiscV32CodeGen::applyUnary(UnaryOp op, int32_t v) const {
  switch (op) {
  case UnaryOp::Plus: return v;
  case UnaryOp::Minus: return wrap32(-static_cast<int64_t>(v));
  case UnaryOp::Not: return v == 0 ? 1 : 0;
  }
  return v;
}

int32_t RiscV32CodeGen::applyBinary(BinaryOp op, int32_t a, int32_t b) const {
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
