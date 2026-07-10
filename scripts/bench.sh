#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
compiler="${1:-./build/toyc}"
if [[ ! -x "$compiler" ]]; then
  scripts/build.sh
fi

for tool in riscv64-linux-gnu-gcc qemu-riscv32; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "missing required benchmark tool: $tool" >&2
    exit 127
  fi
done

case_file="tests/bench_big_loop.tc"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

build_exe() {
  local mode="$1"
  local asm="$tmpdir/${mode:-normal}.s"
  local exe="$tmpdir/${mode:-normal}.out"
  if [[ "$mode" == "opt" ]]; then
    "$compiler" -opt < "$case_file" > "$asm"
  else
    "$compiler" < "$case_file" > "$asm"
  fi
  riscv64-linux-gnu-gcc -march=rv32im -mabi=ilp32 -nostdlib -nostartfiles -static -no-pie -Wl,-e,_start "$asm" -o "$exe"
  echo "$exe"
}

run_exe() {
  local exe="$1"
  local start end
  start="$(date +%s%N)"
  for _ in $(seq 1 10); do
    qemu-riscv32 "$exe" >/dev/null
  done
  end="$(date +%s%N)"
  echo $(( (end - start) / 1000000 ))
}

normal_exe="$(build_exe normal)"
opt_exe="$(build_exe opt)"
normal_ms="$(run_exe "$normal_exe")"
opt_ms="$(run_exe "$opt_exe")"
echo "benchmark: $case_file"
echo "normal 10 runs: ${normal_ms} ms"
echo "opt    10 runs: ${opt_ms} ms"
if ((normal_ms > 0)); then
  echo "speedup: $(( (normal_ms - opt_ms) * 100 / normal_ms ))%"
fi
