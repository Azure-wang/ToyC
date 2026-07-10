#!/usr/bin/env python3
import os
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / "build" / "toyc"


def have(name: str) -> bool:
    return shutil.which(name) is not None


def run(cmd, **kwargs):
    return subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)


def gen_expr(rng: random.Random, vars_, depth=0):
    if depth > 2 or rng.random() < 0.35:
        if vars_ and rng.random() < 0.65:
            return rng.choice(vars_)
        return str(rng.randint(0, 20))
    op = rng.choice(["+", "-", "*", "%", "<", ">", "==", "!="])
    if op == "%":
        return f"({gen_expr(rng, vars_, depth + 1)} % {rng.randint(1, 9)})"
    return f"({gen_expr(rng, vars_, depth + 1)} {op} {gen_expr(rng, vars_, depth + 1)})"


def gen_program(seed: int) -> str:
    rng = random.Random(seed)
    lines = []
    lines.append("int helper(int x, int y) {")
    lines.append("  int z = x + y;")
    lines.append("  if (z % 2 == 0) return z / 2;")
    lines.append("  return z + 3;")
    lines.append("}")
    lines.append("int main() {")
    vars_ = []
    for i in range(rng.randint(2, 5)):
        name = f"v{i}"
        vars_.append(name)
        if rng.random() < 0.3:
            lines.append(f"  const int c{i} = {rng.randint(0, 20)};")
            lines.append(f"  int {name} = c{i} + {rng.randint(0, 9)};")
        else:
            lines.append(f"  int {name} = {rng.randint(0, 20)};")
    lines.append("  int i = 0;")
    lines.append("  int acc = 0;")
    limit = rng.randint(0, 8)
    lines.append(f"  while (i < {limit}) {{")
    lines.append(f"    acc = acc + {gen_expr(rng, vars_ + ['i'], 0)};")
    if rng.random() < 0.5:
        target = rng.choice(vars_)
        lines.append(f"    {target} = {target} + i + 1;")
    lines.append("    i = i + 1;")
    lines.append("  }")
    cond = gen_expr(rng, vars_ + ["acc"], 0)
    lines.append(f"  if ({cond}) {{")
    lines.append(f"    acc = acc + helper({rng.choice(vars_)}, {rng.randint(0, 7)});")
    lines.append("  } else {")
    lines.append(f"    acc = acc - helper({rng.randint(0, 7)}, {rng.choice(vars_)});")
    lines.append("  }")
    lines.append("  return acc;")
    lines.append("}")
    return "\n".join(lines) + "\n"


def run_toyc(src: Path, opt: bool, tmp: Path):
    asm = tmp / ("out_opt.s" if opt else "out.s")
    exe = tmp / ("out_opt" if opt else "out")
    cmd = [str(COMPILER)]
    if opt:
        cmd.append("-opt")
    with src.open() as f:
        p = subprocess.run(cmd, cwd=ROOT, stdin=f, stdout=asm.open("w"), stderr=subprocess.PIPE, text=True)
    if p.returncode != 0:
        return None, f"compile failed: {p.stderr.strip()}"
    if not (have("riscv64-linux-gnu-gcc") and have("qemu-riscv32")):
        return 0, "asm-only"
    p = run(["riscv64-linux-gnu-gcc", "-march=rv32im", "-mabi=ilp32", "-nostdlib", "-nostartfiles", "-static", "-no-pie", "-Wl,-e,_start", str(asm), "-o", str(exe)])
    if p.returncode != 0:
        return None, f"link failed: {p.stderr.strip()}"
    p = subprocess.run(["qemu-riscv32", str(exe)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return p.returncode, ""


def run_gcc(src: Path, tmp: Path):
    if not have("gcc"):
        return None, "missing gcc"
    exe = tmp / "gcc.out"
    p = run(["gcc", "-x", "c", "-std=c11", "-fwrapv", str(src), "-o", str(exe)])
    if p.returncode != 0:
        return None, f"gcc failed: {p.stderr.strip()}"
    p = subprocess.run([str(exe)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return p.returncode, ""


def main():
    if not COMPILER.exists():
        p = run(["scripts/build.sh"])
        if p.returncode != 0:
            print(p.stderr, file=sys.stderr)
            return p.returncode
    full_oracle = have("gcc") and have("riscv64-linux-gnu-gcc") and have("qemu-riscv32")
    if not full_oracle:
        print("missing gcc or RISC-V/qemu tools; fuzz will at least compile generated programs to assembly", file=sys.stderr)
    with tempfile.TemporaryDirectory() as td:
        tmp_root = Path(td)
        for seed in range(100):
            case_dir = tmp_root / f"case_{seed}"
            case_dir.mkdir()
            src = case_dir / "fuzz.tc"
            src.write_text(gen_program(seed))
            oracle, err = run_gcc(src, case_dir) if full_oracle else (0, "")
            if oracle is None:
                print(f"FAIL seed={seed} oracle {err}")
                return 1
            for opt in (False, True):
                got, err = run_toyc(src, opt, case_dir)
                if got is None:
                    print(f"FAIL seed={seed} opt={opt} {err}")
                    print(src.read_text())
                    return 1
                if full_oracle and got != oracle:
                    print(f"FAIL seed={seed} opt={opt} gcc={oracle} toyc={got}")
                    print(src.read_text())
                    return 1
        print("PASS fuzz cases=100 modes=normal,opt")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
