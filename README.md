# ToyC C++20 Compiler

This project implements the ToyC compiler specified by `编译系统实践(1).pdf`. It reads ToyC source from `stdin` and writes RISC-V32 assembly to `stdout`. Diagnostics are printed to `stderr`.

## Build

Makefile path:

```bash
make clean
make -j
./build/toyc < tests/basic_return.tc > /tmp/out.s
```

CMake path:

```bash
rm -rf build-cmake
cmake -S . -B build-cmake
cmake --build build-cmake -j
./build-cmake/toyc < tests/basic_return.tc > /tmp/out.s
```

The helper script builds both paths:

```bash
scripts/build.sh
```

`build/` is the Makefile output directory. `build-cmake/` is the CMake output directory.

If required tools are missing, install them on Debian/WSL with:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake make ninja-build python3 qemu-user gcc-riscv64-linux-gnu
```

## Run

```bash
./build/toyc < tests/basic_return.tc > /tmp/out.s
./build/toyc -opt < tests/basic_return.tc > /tmp/out_opt.s
```

The generated assembly defines `_start`, calls `main`, and exits through the Linux RISC-V syscall `exit(93)`, so the program exit code is the `int main()` return value.

## Test

```bash
scripts/test.sh
scripts/test.sh ./build/toyc
scripts/test.sh ./build-cmake/toyc
scripts/run_one.sh tests/basic_return.tc
scripts/run_one.sh tests/basic_return.tc -opt
scripts/run_one.sh tests/basic_return.tc -opt ./build-cmake/toyc
scripts/bench.sh
scripts/perf_report.sh
scripts/diff_test.sh
python3 scripts/fuzz.py
```

`scripts/run_one.sh` compiles ToyC to assembly, links it as a static RV32IM executable with `riscv64-linux-gnu-gcc`, runs it with `qemu-riscv32`, and prints the exit code.

## Add A Test

Add `tests/name.tc`, then add one tab-separated line to `tests/expected.tsv`:

```text
name.tc	42
```

`scripts/test.sh` runs every test in both normal mode and `-opt` mode. The current suite has 47 checked ToyC programs.

## Implemented ToyC Features

- Global/local `int` variables and `const int` constants with required initialization.
- `int` and `void` functions, `int` parameters, recursion, calls, and more than 8 arguments via stack passing.
- Blocks, declarations, empty statements, expression statements, assignments, `if/else`, `while`, `break`, `continue`, and `return`.
- Integer arithmetic `+ - * / %`, unary `+ - !`, comparisons `< > <= >= == !=`, logical `&& ||` with short-circuit behavior.
- Nested scopes and shadowing; declarations must precede uses.
- Semantic checks for `main`, function order, const assignment, break/continue placement, return type rules, and void value misuse.

## Optimizations Under `-opt`

- Constant folding.
- Algebraic simplification such as `x + 0`, `x * 1`, `x * 0`, `x / 1`, and `x % 1`.
- Simple dead code removal after terminators.
- Constant-condition branch simplification.
- Local variable and parameter registerization in `s1-s11` when `-opt` is enabled.
- Short expression temporary register stack using `t0-t6` when the RHS has no function call.
- Direct conditional branches for comparisons used by `if` and `while`.
- `addi` selection for small integer additions/subtractions.
- Leaf functions skip saving `ra`.

## Dependencies

- C++20 compiler: `g++` or compatible.
- Preferred build tool: `cmake`.
- Fallback build tool: `make`.
- RISC-V test tools: `riscv64-linux-gnu-gcc` and `qemu-riscv32`.

On Debian/WSL:

```bash
scripts/install_deps_debian.sh
```

In this local WSL environment, `make`, `cmake`, `g++`, `python3`, `riscv64-linux-gnu-gcc`, and `qemu-riscv32` were all available and both build paths were verified.

## Project Structure

```text
CMakeLists.txt
Makefile
build/
build-cmake/
src/
  lexer.*, parser.*, ast.*, sema.*, optimizer.*, codegen_riscv32.*
  symbol_table.*, ir.*, utils.*, main.cpp
tests/
scripts/
docs/practice_report.md
```

## Current Performance Snapshot

`scripts/perf_report.sh` on the benchmark set reported:

```text
TOTAL normal_ms=47 opt_ms=36 speedup=23% normal_lines=848 opt_lines=634
```

The timing is qemu-based and still noisy, but assembly line and instruction counts consistently decrease under `-opt`.
