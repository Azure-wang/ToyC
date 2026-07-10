#pragma once

#include <string>
#include <vector>

namespace toyc {

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
