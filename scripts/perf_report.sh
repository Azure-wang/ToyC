#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
compiler="${1:-./build/toyc}"
if [[ ! -x "$compiler" ]]; then
  scripts/build.sh
fi

for tool in riscv64-linux-gnu-gcc qemu-riscv32; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "missing required performance tool: $tool" >&2
    echo "cannot run benchmark exit-code tests without RISC-V link/emulation tools" >&2
    exit 127
  fi
done

benchmarks=(
  bench_big_loop.tc
  bench_cse.tc
  bench_many_locals.tc
  bench_many_calls.tc
  bench_recursion.tc
  bench_short_circuit.tc
  bench_const_dense.tc
  bench_control_flow.tc
)

expected_for() {
  awk -F '\t' -v f="$1" '$1 == f { print $2 }' tests/expected.tsv
}

instr_count() {
  grep -Ev '^[[:space:]]*($|[.]|#|[A-Za-z0-9_.$]+:)' "$1" | wc -l
}

run_one_exe() {
  local exe="$1"
  local start end code
  start="$(date +%s%N)"
  set +e
  qemu-riscv32 "$exe" >/dev/null
  code=$?
  set -e
  end="$(date +%s%N)"
  echo "$code $(( (end - start) / 1000000 ))"
}

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

printf "%-24s %8s %8s %8s %8s %8s %8s %8s %9s %s\n" \
  "benchmark" "norm_ms" "opt_ms" "n_lines" "o_lines" "n_inst" "o_inst" "exit" "speedup" "ok"

total_normal=0
total_opt=0
total_lines_normal=0
total_lines_opt=0

for b in "${benchmarks[@]}"; do
  src="tests/$b"
  exp="$(expected_for "$b")"
  n_asm="$tmpdir/$b.normal.s"
  o_asm="$tmpdir/$b.opt.s"
  n_exe="$tmpdir/$b.normal.out"
  o_exe="$tmpdir/$b.opt.out"
  "$compiler" < "$src" > "$n_asm"
  "$compiler" -opt < "$src" > "$o_asm"
  riscv64-linux-gnu-gcc -march=rv32im -mabi=ilp32 -nostdlib -nostartfiles -static -no-pie -Wl,-e,_start "$n_asm" -o "$n_exe"
  riscv64-linux-gnu-gcc -march=rv32im -mabi=ilp32 -nostdlib -nostartfiles -static -no-pie -Wl,-e,_start "$o_asm" -o "$o_exe"
  read -r n_code n_ms < <(run_one_exe "$n_exe")
  read -r o_code o_ms < <(run_one_exe "$o_exe")
  n_lines="$(wc -l < "$n_asm")"
  o_lines="$(wc -l < "$o_asm")"
  n_inst="$(instr_count "$n_asm")"
  o_inst="$(instr_count "$o_asm")"
  ok="no"
  [[ "$n_code" == "$exp" && "$o_code" == "$exp" ]] && ok="yes"
  speed=0
  if ((n_ms > 0)); then speed=$(( (n_ms - o_ms) * 100 / n_ms )); fi
  printf "%-24s %8s %8s %8s %8s %8s %8s %8s %8s%% %s\n" \
    "$b" "$n_ms" "$o_ms" "$n_lines" "$o_lines" "$n_inst" "$o_inst" "$exp" "$speed" "$ok"
  total_normal=$((total_normal + n_ms))
  total_opt=$((total_opt + o_ms))
  total_lines_normal=$((total_lines_normal + n_lines))
  total_lines_opt=$((total_lines_opt + o_lines))
done

overall=0
if ((total_normal > 0)); then overall=$(( (total_normal - total_opt) * 100 / total_normal )); fi
echo "TOTAL normal_ms=$total_normal opt_ms=$total_opt speedup=${overall}% normal_lines=$total_lines_normal opt_lines=$total_lines_opt"
