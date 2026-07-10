cmake_minimum_required(VERSION 3.16)
project(toyc_cpp_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(toyc
  src/main.cpp
  src/lexer.cpp
  src/parser.cpp
  src/ast.cpp
  src/sema.cpp
  src/optimizer.cpp
  src/codegen_riscv32.cpp
  src/symbol_table.cpp
  src/ir.cpp
  src/utils.cpp
)

target_include_directories(toyc PRIVATE src)
target_compile_options(toyc PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(toyc PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
