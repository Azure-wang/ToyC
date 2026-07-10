#include "codegen_riscv32.h"
#include "lexer.h"
#include "optimizer.h"
#include "parser.h"
#include "sema.h"
#include "utils.h"

#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char **argv) {
  bool opt = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-opt") {
      opt = true;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 2;
    }
  }

  try {
    std::string source((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    toyc::Lexer lexer(source);
    auto tokens = lexer.tokenize();
    toyc::Parser parser(std::move(tokens));
    auto program = parser.parseProgram();
    toyc::Sema sema;
    sema.analyze(program);
    if (opt) {
      toyc::Optimizer optimizer;
      optimizer.optimize(program);
    }
    toyc::RiscV32CodeGen codegen(sema.functions(), opt);
    std::cout << codegen.generate(program);
    return 0;
  } catch (const toyc::CompileError &e) {
    std::cerr << e.what() << "\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "internal error: " << e.what() << "\n";
    return 1;
  }
}
