#!/usr/bin/env bash
set -euo pipefail

# Usage: ./evaluate.sh [build_dir]
# Default build directory is ./build

BUILD_DIR=${1:-build}
BIN_ATOMIC="./${BUILD_DIR}/bitonic_count"

if [[ ! -x "${BIN_ATOMIC}" ]];
then
    echo "error: binary not found: ${BIN_ATOMIC}" >&2
    echo "hint: run cmake/configure and build first (see README.md)" >&2
    exit 1
fi

echo "threads,atomic,bitonic,own"
for t in 2 4 8 16 32 64; do
    a=$(${BIN_ATOMIC} ${t})
    b=$(${BIN_ATOMIC} ${t} -b)
    o=$(${BIN_ATOMIC} ${t} -o)
    echo "${t},${a},${b},${o}"
done


