#pragma once

#include <string>
#include <vector>

namespace toyc {

// Structured IR data types — the current backend lowers AST directly to RISC-V
// assembly without going through an explicit TAC/CFG IR layer. These types
// document the IR shape used for analysis and reporting. Future work: emit
// these from a dedicated IR builder pass and generate code from IR instead of AST.

struct IRInstruction {
  std::string op;
  std::vector<std::string> args;
};

struct IRBlock {
  std::string label;
  std::vector<IRInstruction> instructions;
};

struct IRFunction {
  std::string name;
  std::vector<IRBlock> blocks;
};

struct IRModule {
  std::vector<IRFunction> functions;
};

} // namespace toyc
