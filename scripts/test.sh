#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
compiler="${1:-./build/toyc}"
if [[ ! -x "$compiler" ]]; then
  if [[ "$compiler" == "./build/toyc" || "$compiler" == "./build-cmake/toyc" ]]; then
    scripts/build.sh
  else
    echo "compiler is not executable: $compiler" >&2
    exit 127
  fi
fi

fail=0
while IFS=$'\t' read -r file expected; do
  [[ -z "${file:-}" || "${file:0:1}" == "#" ]] && continue
  for mode in "" "-opt"; do
    actual="$(scripts/run_one.sh "tests/$file" $mode "$compiler")"
    label="$file ${mode:-normal}"
    if [[ "$actual" == "$expected" ]]; then
      printf "PASS %-32s exit=%s\n" "$label" "$actual"
    else
      printf "FAIL %-32s expected=%s actual=%s\n" "$label" "$expected" "$actual"
      fail=1
    fi
  done
done < tests/expected.tsv

exit "$fail"
