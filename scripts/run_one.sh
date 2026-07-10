#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
if [[ $# -lt 1 ]]; then
  echo "usage: scripts/run_one.sh input.tc [-opt] [compiler]" >&2
  exit 2
fi

input="$1"
mode="${2:-}"
compiler="${3:-./build/toyc}"
if [[ "$mode" != "" && "$mode" != "-opt" ]]; then
  compiler="$mode"
  mode=""
fi

if [[ ! -x "$compiler" ]]; then
  if [[ "$compiler" == "./build/toyc" ]]; then
    scripts/build.sh
  else
    echo "compiler is not executable: $compiler" >&2
    exit 127
  fi
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
asm="$tmpdir/out.s"
exe="$tmpdir/a.out"

if [[ "$mode" == "-opt" ]]; then
  "$compiler" -opt < "$input" > "$asm"
else
  "$compiler" < "$input" > "$asm"
fi
echo "assembly generated successfully: $asm" >&2

if ! command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
  echo "missing RISC-V link tool: riscv64-linux-gnu-gcc" >&2
  echo "cannot run exit-code test; generated assembly at $asm" >&2
  exit 127
fi
if ! command -v qemu-riscv32 >/dev/null 2>&1; then
  echo "missing RISC-V emulator: qemu-riscv32" >&2
  echo "cannot run exit-code test; generated assembly at $asm" >&2
  exit 127
fi

riscv64-linux-gnu-gcc -march=rv32im -mabi=ilp32 -nostdlib -nostartfiles -static -no-pie -Wl,-e,_start "$asm" -o "$exe"
echo "assembly linked successfully: $exe" >&2
set +e
qemu-riscv32 "$exe"
code=$?
set -e
echo "$code"
