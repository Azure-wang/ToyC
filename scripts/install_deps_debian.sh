#!/usr/bin/env bash
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  make \
  ninja-build \
  python3 \
  gcc-riscv64-linux-gnu \
  qemu-user
