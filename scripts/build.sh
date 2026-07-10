#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

missing=()
for tool in make g++; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    missing+=("$tool")
  fi
done

if ((${#missing[@]})); then
  echo "missing required build tools: ${missing[*]}" >&2
  echo "install on Debian/WSL with:" >&2
  echo "sudo apt-get update" >&2
  echo "sudo apt-get install -y build-essential cmake make ninja-build python3 qemu-user gcc-riscv64-linux-gnu" >&2
  exit 127
fi

make -j"$(nproc)"

if ! command -v cmake >/dev/null 2>&1; then
  echo "missing required CMake tool: cmake" >&2
  echo "install on Debian/WSL with:" >&2
  echo "sudo apt-get update" >&2
  echo "sudo apt-get install -y build-essential cmake make ninja-build python3 qemu-user gcc-riscv64-linux-gnu" >&2
  exit 127
fi

cmake -S . -B build-cmake
cmake --build build-cmake -j"$(nproc)"

test -x ./build/toyc
test -x ./build-cmake/toyc
