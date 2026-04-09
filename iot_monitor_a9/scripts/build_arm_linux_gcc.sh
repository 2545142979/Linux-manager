#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/arm-linux-gcc"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
C_COMPILER="${ARM_LINUX_GCC_COMPILER:-arm-linux-gnueabi-gcc}"
TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/arm-linux-gcc.cmake"

if ! command -v "${C_COMPILER}" >/dev/null 2>&1; then
    echo "error: cross compiler not found: ${C_COMPILER}" >&2
    echo "hint: export ARM_LINUX_GCC_COMPILER=/path/to/arm-linux-gnueabi-gcc" >&2
    exit 1
fi

cmake -S "${ROOT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DARM_LINUX_GCC_COMPILER="${C_COMPILER}"

cmake --build "${BUILD_DIR}" -- -j"${JOBS}"
