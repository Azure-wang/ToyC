#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
compiler="${1:-./build/toyc}"
if [[ ! -x "$compiler" ]]; then
  scripts/build.sh
fi

missing=()
for tool in gcc riscv64-linux-gnu-gcc qemu-riscv32; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    missing+=("$tool")
  fi
done

if ((${#missing[@]})); then
  echo "missing tools for diff test: ${missing[*]}" >&2
  echo "ToyC-to-assembly generation can still be tested, but exit-code oracle comparison cannot run." >&2
  exit 127
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
fail=0

run_riscv() {
  local src="$1"
  local mode="$2"
  local base="$tmpdir/$(basename "$src").$mode"
  if [[ "$mode" == "opt" ]]; then
    "$compiler" -opt < "$src" > "$base.s"
  else
    "$compiler" < "$src" > "$base.s"
  fi
  riscv64-linux-gnu-gcc -march=rv32im -mabi=ilp32 -nostdlib -nostartfiles -static -no-pie -Wl,-e,_start "$base.s" -o "$base.out"
  set +e
  qemu-riscv32 "$base.out" >/dev/null
  local code=$?
  set -e
  echo "$code"
}

run_gcc() {
  local src="$1"
  local exe="$tmpdir/$(basename "$src").gcc.out"
  gcc -x c -std=c11 -fwrapv "$src" -o "$exe"
  set +e
  "$exe" >/dev/null
  local code=$?
  set -e
  echo "$code"
}

for src in tests/*.tc; do
  oracle="$(run_gcc "$src")"
  for mode in normal opt; do
    got="$(run_riscv "$src" "$mode")"
    label="$(basename "$src") $mode"
    if [[ "$got" == "$oracle" ]]; then
      printf "PASS %-32s exit=%s\n" "$label" "$got"
    else
      printf "FAIL %-32s gcc=%s toyc=%s\n" "$label" "$oracle" "$got"
      fail=1
    fi
  done
done

exit "$fail"
