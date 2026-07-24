# ToyC Compiler Practice Report

## 1. Project Introduction

This project is a C++20 implementation of the ToyC compiler required by the PDF specification. The compiler reads a single ToyC source program from standard input and emits RISC-V32 assembly to standard output. The assembly is intended to be assembled and linked for RV32IM, then executed under `qemu-riscv32`.

ToyC is implemented as the PDF-defined C subset: no arrays, pointers, strings, I/O, or multi-file compilation are added.

## 2. Overall Architecture

The compiler pipeline is:

```text
stdin source -> lexer -> parser -> AST -> semantic analysis -> optional optimizer -> RISC-V32 codegen -> stdout assembly
```

Important modules:

- `lexer.*`: hand-written scanner.
- `parser.*`: recursive-descent parser matching the PDF grammar.
- `ast.*`: AST node definitions.
- `sema.*`: semantic checks and compile-time constant validation.
- `optimizer.*`: AST-level optimization enabled by `-opt`.
- `codegen_riscv32.*`: RV32IM assembly backend.
- `symbol_table.*`: nested scoped symbol table.
- `ir.*`: compact TAC-like IR data model documenting the lowering boundary.

## 3. Lexical Analysis

The lexer recognizes identifiers, decimal integer constants, keywords, delimiters, and operators. It skips spaces, tabs, newlines, `//` comments, and `/* ... */` comments. Each token carries line and column information so parser and semantic errors can report useful positions.

Identifiers match the PDF pattern equivalent to `[_A-Za-z][_A-Za-z0-9]*`. Integer constants are parsed as decimal literals and wrapped to signed 32-bit values when lowered.

## 4. Syntax Analysis

The parser is hand-written recursive descent. The expression grammar is layered by precedence:

```text
Expr -> LOrExpr
LOrExpr -> LAndExpr ('||' LAndExpr)*
LAndExpr -> RelExpr ('&&' RelExpr)*
RelExpr -> AddExpr (relop AddExpr)*
AddExpr -> MulExpr (('+' | '-') MulExpr)*
MulExpr -> UnaryExpr (('*' | '/' | '%') UnaryExpr)*
UnaryExpr -> PrimaryExpr | ('+' | '-' | '!') UnaryExpr
PrimaryExpr -> ID | NUMBER | '(' Expr ')' | ID '(' args? ')'
```

Top-level parsing distinguishes `int f(...)` from `int x = ...;` by lookahead.

## 5. AST Design

Expressions include number, variable, unary, binary, and call nodes. Statements include block, declaration, empty, expression, assignment, if, while, break, continue, and return nodes. Top-level items are global declarations or function definitions.

AST ownership uses `std::unique_ptr`, which keeps memory management explicit and simple.

## 6. Semantic Analysis

Semantic analysis enforces the PDF constraints:

- `int main()` with no parameters must exist.
- Function names are unique.
- Function calls must target an already declared function, except recursive self-calls.
- Variables, constants, and parameters obey nested block scope and shadowing.
- Declarations must precede uses.
- All declarations have initializers.
- `const` initializers are compile-time evaluable.
- `const` symbols cannot be assigned.
- `break` and `continue` only appear inside loops.
- `int` functions must return an integer on all analyzed paths.
- `void` functions may omit return, but cannot return a value.
- `void` calls cannot be used as values.

## 7. IR Design

The project uses a compact structured IR boundary: after semantic analysis, the AST is lowered into labeled control-flow patterns in the backend. `ir.*` defines a TAC-like module/function/block/instruction model, and the backend follows the same shape: labels, conditional jumps, unconditional jumps, calls, loads/stores, arithmetic operations, and returns.

This keeps the implementation small while still exposing a clean optimization/codegen boundary for the practice project.

## 8. RISC-V32 Code Generation

The backend emits RV32IM assembly:

- `.data` for global mutable variables.
- `.text` for `_start` and ToyC functions.
- `_start` calls `main`, then performs Linux `exit` syscall 93 using `a0` as the exit code.
- Arithmetic uses RV32IM instructions including `mul`, `div`, and `rem`.
- Pseudo-instructions such as `li`, `la`, `call`, `seqz`, and `snez` are accepted by GNU assembler.

## 9. Stack Frame And Calling Convention

Normal mode creates an aligned stack frame:

- `ra` saved at `-4(s0)`.
- old `s0` saved at `-8(s0)`.
- parameters and mutable locals are assigned negative offsets below `s0`.

The first 8 arguments use `a0` to `a7`. Additional arguments are passed on the caller stack and loaded by the callee from positive offsets relative to `s0`. Recursive calls work because each invocation has its own frame.

In `-opt` mode, mutable parameters and local variables are preferentially assigned to `s1-s11`. Used `s` registers are saved and restored in the prologue/epilogue, so values remain valid across calls. Variables beyond the available register budget spill to stack slots. Leaf functions skip saving `ra`.

## 10. Short-Circuit Evaluation

`&&` and `||` are lowered as control-flow, not arithmetic-only expressions:

- `a && b`: branch to false immediately if `a` is zero; evaluate `b` only otherwise.
- `a || b`: branch to true immediately if `a` is nonzero; evaluate `b` only otherwise.

When a logical expression is needed as a value, codegen materializes `0` or `1` after the short-circuit branches.

## 11. Scope And Symbol Table

`ScopedTable<T>` maintains a vector of maps. Lookup walks from innermost to outermost scope. Declaration only checks the current scope, which permits inner shadowing while rejecting duplicates in the same block.

The semantic table stores const-ness and compile-time constant values. The codegen table stores const values, global labels, or stack offsets.

## 12. Error Handling

All compiler errors throw `CompileError` with line and column where available. `main.cpp` catches errors, prints to `stderr`, and exits nonzero. `stdout` is reserved exclusively for assembly.

## 13. Optimization Design

Optimizations are enabled only when the compiler receives `-opt`.

Iterative fixed-point optimization:
The optimizer runs constant folding, constant/copy propagation, and dead code elimination in a loop until no more changes occur (up to 10 iterations per block, 5 iterations per function across the whole program). This catches cascading optimization opportunities — for example, constant propagation reveals dead branches, whose elimination reveals more constant expressions, etc.

Constant folding:
Expressions whose operands are integer literals are folded in `optimizer.*`. The folder also handles constant-valued mutable variables through the value tracking map.

Constant propagation:
Const-qualified variables are tracked in a scoped symbol table. Mutable variables assigned constant values are tracked in an intra-block value map. When a variable is looked up during folding, both the const table and the mutable value map are consulted. Copy chains (`x = y`) are followed to find the ultimate source value.

Copy propagation:
When a variable is assigned from another variable (`x = y`), the copy relationship is recorded in a copy map. Subsequent uses of `x` are replaced with `y`, and if `y` in turn has a known constant value, the constant is propagated through.

Dead code elimination:
Statements after `return`, `break`, or `continue` in a block are removed by the optimizer. After constant propagation, branch conditions that evaluate to constants are replaced with unconditional branches or removed entirely, enabling more dead code elimination. Dead stores (assignments to variables never read again) are also removed.

Common subexpression elimination / local value numbering:
The implementation tracks expression keys across statements within a basic block. When the same expression is computed in multiple declarations or assignments, the second occurrence is replaced with a reference to the first result. Additionally, repeated subexpressions within a single expression are hoisted into temporary variables via `hoistCommonSubexprs`.

Register allocation / reduced memory traffic:
In `-opt`, parameters and most mutable locals are allocated to `s1-s11`; short-lived expression operands use `t0-t6`. Stack slots are used for spills, globals, and call argument overflow. Normal mode remains stack-oriented for easier debugging and as a correctness baseline.

Branch optimization:
Under `-opt`, compile-time constant conditions lower directly to unconditional jumps. Constant false `while` loops are removed at the AST level. Constant-true `if` statements are replaced with their `then` branch; constant-false `if` statements are replaced with their `else` branch (or removed). Comparisons used directly by `if` and `while` generate RISC-V branch sequences instead of first materializing 0/1.

Algebraic simplification:
The optimizer handles `x + 0`, `0 + x`, `x - 0`, `x * 1`, `1 * x`, `x * 0`, `0 * x`, `x / 1`, `x % 1`, `0 - x → -x`, `x - x → 0`, `x / -1 → -x`, `-(-x) → x`, `x + (-y) → x - y`, `(-x) + y → y - x`, `x - (-y) → x + y`, `-(x - y) → y - x`, as well as associativity: `(x + C1) + C2 → x + (C1+C2)`, `(x * C1) * C2 → x * (C1*C2)`, and similar patterns with the constant on either side.

Loop-invariant code motion:
Statements inside `while` loops whose operands are not modified by the loop body and which do not assign to loop-modified variables are hoisted before the loop.

Function inlining:
Small functions (up to 10 statements) that do not call themselves are inlined at call sites. Mutually recursive functions are excluded. Inlined functions are renamed with a unique prefix to avoid name conflicts.

Tail call optimization:
Self-recursive tail calls are optimized by updating parameters in-place and jumping to the function body label, avoiding stack frame growth. Non-recursive tail calls with up to 8 arguments are optimized by reusing the current stack frame before jumping to the callee.

Instruction selection and peephole:
Small `x + c` and `x - c` cases use `addi` when the immediate fits. Multiplication and division by powers of 2 use shift instructions. Obvious no-op algebraic cases are removed before codegen. Jump-to-next-label pairs are removed in the peephole pass.

## 14. Testing Method

`tests/expected.tsv` maps each ToyC program to an expected exit code. `scripts/test.sh` accepts an optional compiler path and runs every case twice: normal mode and `-opt` mode. `scripts/run_one.sh` compiles ToyC to assembly, links it as a static RV32IM binary, runs it in qemu, and prints the exit code.

Test coverage now includes 47 programs: return, globals, locals, precedence, unary operators, relations, logical short-circuit, if/else, dangling else, nested loops, nearest-loop break/continue, calls, recursion, void functions, shadowing, constant folding, many arguments, many locals, return-dead-code, constant-false loops, and 8 benchmark-oriented cases.

Additional validation:

- `scripts/diff_test.sh` compiles every `.tc` with this compiler and with host `gcc -x c -fwrapv`, then compares exit codes in normal and `-opt` modes.
- `scripts/fuzz.py` generates 100 valid small ToyC programs and compares normal/`-opt` output against gcc when the RISC-V and qemu tools are present.

## 15. Performance Results

On the local WSL Debian environment, `scripts/bench.sh` produced:

```text
benchmark: tests/bench_big_loop.tc
normal 10 runs: 55 ms
opt    10 runs: 39 ms
speedup: 29%
```

`scripts/perf_report.sh` produced:

```text
benchmark                 norm_ms   opt_ms  n_lines  o_lines   n_inst   o_inst     exit   speedup ok
bench_big_loop.tc               6        5       71       52       60       41      127       16% yes
bench_cse.tc                    5        4       99       74       88       63      218       20% yes
bench_many_locals.tc            8        5      175      125      164      114      192       37% yes
bench_many_calls.tc             5        5      121      114      102       95       17        0% yes
bench_recursion.tc              4        4       84       71       70       57      233        0% yes
bench_short_circuit.tc          5        4      104       76       84       56      106       20% yes
bench_const_dense.tc            6        4       83       43       72       32        0       33% yes
bench_control_flow.tc           7        5      111       79       94       62       29       28% yes
TOTAL normal_ms=46 opt_ms=36 speedup=21% normal_lines=848 opt_lines=634
```

The per-case timing is qemu-based and coarse, so instruction counts and line counts are also tracked. The largest wins come from local registerization and removing temporary stack traffic.

## 16. Known Limits And Future Work

- Full LVN/CSE is not implemented. Repeated subexpressions are not always computed once.
- Register allocation is heuristic rather than liveness-driven linear scan.
- Loop-invariant code motion is not implemented as a distinct pass.
- Small function inlining is not implemented; call-heavy benchmarks still show limited speedup.
- CFG simplification is limited to AST-level constant branch removal and dead code after terminators.
- Infinite-loop return reasoning is conservative, so some valid programs with guaranteed loop returns may be rejected.
- No arrays, pointers, strings, I/O, prototypes, or multiple source files are implemented, matching the PDF scope.
- Both Makefile and CMake build paths were verified in the local WSL environment.
